// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DeepFilterNetSpeechCleanup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QStringList>

#include <algorithm>
#include <utility>
#include <vector>

namespace {
	struct DFState;

	using DfCreateFn            = DFState *(*)(const char *path, float attenLim, const char *logLevel);
	using DfGetFrameLengthFn    = std::size_t (*)(DFState *state);
	using DfProcessFrameFn      = float (*)(DFState *state, float *input, float *output);
	using DfSetAttenLimFn       = void (*)(DFState *state, float limitDb);
	using DfSetPostFilterBetaFn = void (*)(DFState *state, float beta);
	using DfFreeFn              = void (*)(DFState *state);

	QStringList candidateLibraryPaths() {
		const QString appDir = QCoreApplication::applicationDirPath();
		return {
			QDir(appDir).filePath(QStringLiteral("deepfilter.dll")),
			QDir(appDir).filePath(QStringLiteral("libdeepfilter.dll")),
			QDir(appDir).filePath(QStringLiteral("deepfilternet/deepfilter.dll")),
			QDir(appDir).filePath(QStringLiteral("deepfilternet/libdeepfilter.dll"))
		};
	}

	QString resolveModelPath() {
		const QString baseDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("deepfilternet"));
		const QStringList candidates = {
			QDir(baseDir).filePath(QStringLiteral("DeepFilterNet3_ll_onnx.tar.gz")),
			QDir(baseDir).filePath(QStringLiteral("DeepFilterNet3_onnx.tar.gz")),
		};

		for (const QString &candidate : candidates) {
			if (QFileInfo::exists(candidate)) {
				return candidate;
			}
		}

		return {};
	}
} // namespace

class DeepFilterNetSpeechCleanup::Implementation {
public:
	Implementation() {
#ifdef USE_DEEPFILTERNET
		m_modelPath = resolveModelPath();
		if (m_modelPath.isEmpty()) {
			return;
		}

		for (const QString &candidate : candidateLibraryPaths()) {
			m_library.setFileName(candidate);
			if (m_library.load()) {
				break;
			}
		}

		if (!m_library.isLoaded()) {
			return;
		}

		m_create            = reinterpret_cast< DfCreateFn >(m_library.resolve("df_create"));
		m_getFrameLength    = reinterpret_cast< DfGetFrameLengthFn >(m_library.resolve("df_get_frame_length"));
		m_processFrame      = reinterpret_cast< DfProcessFrameFn >(m_library.resolve("df_process_frame"));
		m_setAttenLim       = reinterpret_cast< DfSetAttenLimFn >(m_library.resolve("df_set_atten_lim"));
		m_setPostFilterBeta = reinterpret_cast< DfSetPostFilterBetaFn >(m_library.resolve("df_set_post_filter_beta"));
		m_free              = reinterpret_cast< DfFreeFn >(m_library.resolve("df_free"));

		if (!m_create || !m_getFrameLength || !m_processFrame || !m_setAttenLim || !m_setPostFilterBeta || !m_free) {
			m_library.unload();
			return;
		}

		if (!initializeState()) {
			return;
		}
#endif
	}

	~Implementation() {
#ifdef USE_DEEPFILTERNET
		releaseState();
		if (m_library.isLoaded()) {
			m_library.unload();
		}
#endif
	}

	bool isReady() const {
		return m_ready;
	}

	void reset() {
#ifdef USE_DEEPFILTERNET
		m_pendingInput.clear();
		m_outputQueue.clear();
		if (!m_library.isLoaded()) {
			m_ready = false;
			return;
		}

		releaseState();
		m_ready = initializeState();
#endif
	}

	void processInPlace(float *samples, unsigned int sampleCount, float mixFactor) {
#ifdef USE_DEEPFILTERNET
		if (!m_ready || !samples || sampleCount == 0) {
			return;
		}

		mixFactor = std::clamp(mixFactor, 0.0f, 1.0f);
		if (mixFactor <= 0.0f) {
			return;
		}

		const std::vector< float > original(samples, samples + sampleCount);

		for (unsigned int i = 0; i < sampleCount; ++i) {
			m_pendingInput.push_back(original[i]);
			if (m_pendingInput.size() == m_frameLength) {
				for (std::size_t frameIndex = 0; frameIndex < m_frameLength; ++frameIndex) {
					m_inputFrame[frameIndex] = m_pendingInput[frameIndex];
				}

				m_processFrame(m_state, m_inputFrame.data(), m_outputFrame.data());
				for (float sample : m_outputFrame) {
					m_outputQueue.push_back(sample);
				}

				m_pendingInput.clear();
			}
		}

		const float dryFactor = 1.0f - mixFactor;
		for (unsigned int i = 0; i < sampleCount; ++i) {
			float cleaned = original[i];
			if (!m_outputQueue.empty()) {
				cleaned = m_outputQueue.front();
				m_outputQueue.pop_front();
			}

			samples[i] = std::clamp(cleaned * mixFactor + original[i] * dryFactor, -1.0f, 1.0f);
		}
#else
		(void) samples;
		(void) sampleCount;
		(void) mixFactor;
#endif
	}

private:
	bool initializeState() {
		if (!m_create || !m_getFrameLength || !m_processFrame || !m_setAttenLim || !m_setPostFilterBeta || !m_free
			|| m_modelPath.isEmpty()) {
			return false;
		}

		const QByteArray modelPathBytes = QDir::toNativeSeparators(m_modelPath).toUtf8();
		m_state = m_create(modelPathBytes.constData(), 100.0f, nullptr);
		if (!m_state) {
			return false;
		}

		m_frameLength = m_getFrameLength(m_state);
		if (m_frameLength == 0 || m_frameLength > 4096) {
			releaseState();
			return false;
		}

		m_setAttenLim(m_state, 100.0f);
		m_setPostFilterBeta(m_state, 0.0f);
		m_inputFrame.assign(m_frameLength, 0.0f);
		m_outputFrame.assign(m_frameLength, 0.0f);
		return true;
	}

	void releaseState() {
		if (m_state && m_free) {
			m_free(m_state);
		}
		m_state = nullptr;
		m_frameLength = 0;
	}

	QLibrary m_library;
	QString m_modelPath;
	DFState *m_state = nullptr;
	DfCreateFn m_create = nullptr;
	DfGetFrameLengthFn m_getFrameLength = nullptr;
	DfProcessFrameFn m_processFrame = nullptr;
	DfSetAttenLimFn m_setAttenLim = nullptr;
	DfSetPostFilterBetaFn m_setPostFilterBeta = nullptr;
	DfFreeFn m_free = nullptr;
	std::size_t m_frameLength = 0;
	std::vector< float > m_inputFrame;
	std::vector< float > m_outputFrame;
	std::vector< float > m_pendingInput;
	std::deque< float > m_outputQueue;
	bool m_ready = false;
};

DeepFilterNetSpeechCleanup::DeepFilterNetSpeechCleanup(const Mumble::SpeechCleanup::Selection &selection)
	: m_impl(std::make_unique< Implementation >()) {
	(void) selection;
}

DeepFilterNetSpeechCleanup::~DeepFilterNetSpeechCleanup() = default;

bool DeepFilterNetSpeechCleanup::isReady() const {
	return m_impl && m_impl->isReady();
}

void DeepFilterNetSpeechCleanup::reset() {
	if (m_impl) {
		m_impl->reset();
	}
}

void DeepFilterNetSpeechCleanup::processInPlace(float *samples, unsigned int sampleCount, float mixFactor) {
	if (m_impl) {
		m_impl->processInPlace(samples, sampleCount, mixFactor);
	}
}
