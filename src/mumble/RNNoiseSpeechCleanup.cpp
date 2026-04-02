// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "RNNoiseSpeechCleanup.h"

#ifdef USE_RNNOISE
extern "C" {
#	include "rnnoise.h"
}
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace {
	QString resolvePackagedLittleModelPath() {
		return QDir(QCoreApplication::applicationDirPath())
			.filePath(QStringLiteral("rnnoise/rnnoise_little.weights_blob.bin"));
	}

	void logRnnoiseFallback(const QString &requestedModelId, const QString &requestedCustomModelPath,
							const QString &reason) {
		qWarning("RNNoiseSpeechCleanup: backend=RNNoise requestedModelId=%s requestedCustomModelPath=\"%s\" "
				 "activeModelId=rnnoise:embedded fallbackReason=\"%s\"",
				 qUtf8Printable(requestedModelId), qUtf8Printable(requestedCustomModelPath), qUtf8Printable(reason));
	}
}

RNNoiseSpeechCleanup::RNNoiseSpeechCleanup(const Mumble::SpeechCleanup::Selection &selection)
	: m_selection(Mumble::SpeechCleanup::normalizeSelection(selection)) {
#ifdef USE_RNNOISE
	reset();
#else
	(void) m_selection;
#endif
}

RNNoiseSpeechCleanup::~RNNoiseSpeechCleanup() {
#ifdef USE_RNNOISE
	if (m_state) {
		rnnoise_destroy(m_state);
	}
	if (m_model) {
		rnnoise_model_free(m_model);
	}
#endif
}

bool RNNoiseSpeechCleanup::isReady() const {
#ifdef USE_RNNOISE
	return m_state != nullptr;
#else
	return false;
#endif
}

QString RNNoiseSpeechCleanup::activeModelId() const {
	return m_activeModelId;
}

QString RNNoiseSpeechCleanup::activeModelPath() const {
	return m_activeModelPath;
}

bool RNNoiseSpeechCleanup::usedFallback() const {
	return m_usedFallback;
}

void RNNoiseSpeechCleanup::reset() {
#ifdef USE_RNNOISE
	if (m_state) {
		rnnoise_destroy(m_state);
		m_state = nullptr;
	}
	if (m_model) {
		rnnoise_model_free(m_model);
		m_model = nullptr;
	}

	const QString normalizedModelId = Mumble::SpeechCleanup::normalizedModelId(m_selection.backend, m_selection.modelId);
	const QString requestedCustomModelPath = m_selection.customModelPath.trimmed();
	QString modelPath;
	m_activeModelId   = QStringLiteral("rnnoise:embedded");
	m_activeModelPath.clear();
	m_usedFallback = false;

	if (normalizedModelId == QLatin1String("rnnoise:little")) {
		modelPath = resolvePackagedLittleModelPath();
		if (!QFileInfo::exists(modelPath)) {
			m_usedFallback = true;
			logRnnoiseFallback(normalizedModelId, requestedCustomModelPath,
							   QStringLiteral("Missing packaged little model at %1").arg(modelPath));
			modelPath.clear();
		}
	} else if (normalizedModelId == QLatin1String("rnnoise:custom")) {
		modelPath = requestedCustomModelPath;
		if (modelPath.isEmpty() || !QFileInfo::exists(modelPath)) {
			m_usedFallback = true;
			logRnnoiseFallback(normalizedModelId, requestedCustomModelPath,
							   QStringLiteral("Invalid custom model path"));
			modelPath.clear();
		}
	}

	if (!modelPath.isEmpty()) {
		m_model = rnnoise_model_from_filename(QDir::toNativeSeparators(modelPath).toUtf8().constData());
		if (!m_model) {
			m_usedFallback = true;
			logRnnoiseFallback(normalizedModelId, requestedCustomModelPath,
							   QStringLiteral("Failed to load model from %1").arg(modelPath));
		}
	}

	m_state = rnnoise_create(m_model);
	if (!m_state) {
		if (m_model) {
			m_usedFallback = true;
			logRnnoiseFallback(normalizedModelId, requestedCustomModelPath,
							   QStringLiteral("Failed to initialize RNNoise with model %1").arg(modelPath));
			rnnoise_model_free(m_model);
			m_model = nullptr;
			m_state = rnnoise_create(nullptr);
		}
	}

	if (!m_state) {
		qWarning("RNNoiseSpeechCleanup: backend=RNNoise requestedModelId=%s requestedCustomModelPath=\"%s\" "
				 "initializationFailed=true",
				 qUtf8Printable(normalizedModelId), qUtf8Printable(requestedCustomModelPath));
		return;
	}

	if (m_model) {
		m_activeModelId   = normalizedModelId;
		m_activeModelPath = modelPath;
	} else if (normalizedModelId != QLatin1String("rnnoise:embedded")) {
		m_usedFallback = true;
	}

	qInfo("RNNoiseSpeechCleanup: Initialized backend=RNNoise requestedModelId=%s activeModelId=%s "
		  "requestedCustomModelPath=\"%s\" activeModelPath=\"%s\"",
		  qUtf8Printable(normalizedModelId), qUtf8Printable(m_activeModelId),
		  qUtf8Printable(requestedCustomModelPath), qUtf8Printable(m_activeModelPath));
#endif
}

void RNNoiseSpeechCleanup::processInPlace(float *samples, unsigned int sampleCount, float mixFactor) {
#ifdef USE_RNNOISE
	if (!m_state || !samples || sampleCount == 0) {
		return;
	}

	mixFactor = std::clamp(mixFactor, 0.0f, 1.0f);
	if (mixFactor <= 0.0f) {
		return;
	}

	const float dryFactor = 1.0f - mixFactor;
	for (unsigned int offset = 0; offset < sampleCount; offset += FRAME_SIZE) {
		const unsigned int chunkSize = std::min(FRAME_SIZE, sampleCount - offset);

		m_inputBuffer.fill(0.0f);
		for (unsigned int i = 0; i < chunkSize; ++i) {
			m_inputBuffer[i] = samples[offset + i];
		}

		rnnoise_process_frame(m_state, m_outputBuffer.data(), m_inputBuffer.data());

		for (unsigned int i = 0; i < chunkSize; ++i) {
			samples[offset + i] =
				std::clamp(m_outputBuffer[i] * mixFactor + m_inputBuffer[i] * dryFactor, -1.0f, 1.0f);
		}
	}
#else
	(void) samples;
	(void) sampleCount;
	(void) mixFactor;
#endif
}
