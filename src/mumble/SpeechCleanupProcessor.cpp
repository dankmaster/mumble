// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "SpeechCleanupProcessor.h"

#include "DTLNSpeechCleanup.h"
#include "DeepFilterNetSpeechCleanup.h"
#include "RNNoiseSpeechCleanup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <memory>

namespace {
	bool hasDtlnModelPair(const QString &directoryPath) {
		return QFileInfo::exists(QDir(directoryPath).filePath(QStringLiteral("model_1.onnx")))
			   && QFileInfo::exists(QDir(directoryPath).filePath(QStringLiteral("model_2.onnx")));
	}

	QString resolveDtlnModelDirectory(const Mumble::SpeechCleanup::Selection &selection, QString *activeModelId = nullptr) {
		const QString baseDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("dtln"));
		const QString normalizedModelId =
			Mumble::SpeechCleanup::normalizedModelId(selection.backend, selection.modelId);

		QString requestedDirectory = baseDir;
		if (normalizedModelId == QLatin1String("dtln:norm500h")) {
			requestedDirectory = QDir(baseDir).filePath(QStringLiteral("norm_500h"));
		} else if (normalizedModelId == QLatin1String("dtln:norm40h")) {
			requestedDirectory = QDir(baseDir).filePath(QStringLiteral("norm_40h"));
		} else if (normalizedModelId == QLatin1String("dtln:baseline")) {
			requestedDirectory = QDir(baseDir).filePath(QStringLiteral("baseline"));
		}

		if (hasDtlnModelPair(requestedDirectory)) {
			if (activeModelId) {
				*activeModelId = normalizedModelId;
			}
			return requestedDirectory;
		}

		const QString legacyBaselineDirectory = baseDir;
		const QString packagedBaselineDirectory = QDir(baseDir).filePath(QStringLiteral("baseline"));
		if (normalizedModelId != QLatin1String("dtln:baseline")) {
			qWarning("DTLNSpeechCleanup: Missing model variant \"%s\" under %s, falling back to baseline",
					 qUtf8Printable(normalizedModelId), qUtf8Printable(requestedDirectory));
		}

		if (hasDtlnModelPair(packagedBaselineDirectory)) {
			if (activeModelId) {
				*activeModelId = QStringLiteral("dtln:baseline");
			}
			return packagedBaselineDirectory;
		}
		if (hasDtlnModelPair(legacyBaselineDirectory)) {
			if (activeModelId) {
				*activeModelId = QStringLiteral("dtln:baseline");
			}
			return legacyBaselineDirectory;
		}

		if (activeModelId) {
			activeModelId->clear();
		}
		return {};
	}

	class DTLNSpeechCleanupProcessor final : public SpeechCleanupProcessor {
	public:
		explicit DTLNSpeechCleanupProcessor(const Mumble::SpeechCleanup::Selection &selection) {
#ifdef USE_DTLN
			const QString requestedModelId =
				Mumble::SpeechCleanup::normalizedModelId(selection.backend, selection.modelId);
			m_modelDirectory = resolveDtlnModelDirectory(selection, &m_activeModelId);
			m_usedFallback = !m_activeModelId.isEmpty() && m_activeModelId != requestedModelId;
			if (!m_modelDirectory.isEmpty()) {
				m_dtln = std::make_unique< DTLNSpeechCleanup >(m_modelDirectory);
				if (m_dtln && m_dtln->isReady()) {
					qInfo("DTLNSpeechCleanup: Initialized backend=DTLN requestedModelId=%s activeModelId=%s "
						  "modelDirectory=\"%s\"",
						  qUtf8Printable(requestedModelId), qUtf8Printable(m_activeModelId),
						  qUtf8Printable(m_modelDirectory));
				}
			}

			if (!m_dtln || !m_dtln->isReady()) {
				m_usedFallback = m_usedFallback || !m_modelDirectory.isEmpty() || requestedModelId != QLatin1String("dtln:baseline");
				qWarning("DTLNSpeechCleanup: Failed to initialize backend=DTLN requestedModelId=%s modelDirectory=\"%s\"",
						 qUtf8Printable(requestedModelId), qUtf8Printable(m_modelDirectory));
			}
#else
			(void) selection;
#endif
		}

		bool isReady() const override {
#ifdef USE_DTLN
			return m_dtln && m_dtln->isReady();
#else
			return false;
#endif
		}

		void reset() override {
#ifdef USE_DTLN
			if (m_dtln) {
				m_dtln->reset();
			}
#endif
		}

		void processInPlace(float *samples, unsigned int sampleCount, float mixFactor) override {
#ifdef USE_DTLN
			if (m_dtln) {
				m_dtln->processNormalizedMonoInPlace(samples, sampleCount, mixFactor);
			}
#else
			(void) samples;
			(void) sampleCount;
			(void) mixFactor;
#endif
		}

		QString activeModelId() const override {
			return m_activeModelId;
		}

		QString activeModelPath() const override {
			return m_modelDirectory;
		}

		bool usedFallback() const override {
			return m_usedFallback;
		}

	private:
#ifdef USE_DTLN
		std::unique_ptr< DTLNSpeechCleanup > m_dtln;
#endif
		QString m_activeModelId;
		QString m_modelDirectory;
		bool m_usedFallback = false;
	};
} // namespace

std::unique_ptr< SpeechCleanupProcessor > createSpeechCleanupProcessor(
	const Mumble::SpeechCleanup::Selection &requestedSelection) {
	const Mumble::SpeechCleanup::Selection selection = Mumble::SpeechCleanup::normalizeSelection(requestedSelection);

	switch (selection.backend) {
		case Settings::RNNoiseBackend:
			return std::make_unique< RNNoiseSpeechCleanup >(selection);
		case Settings::DTLNBackend:
			return std::make_unique< DTLNSpeechCleanupProcessor >(selection);
		case Settings::DeepFilterNetBackend:
			return std::make_unique< DeepFilterNetSpeechCleanup >(selection);
	}

	return {};
}
