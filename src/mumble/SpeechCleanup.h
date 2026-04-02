// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SPEECHCLEANUP_H_
#define MUMBLE_MUMBLE_SPEECHCLEANUP_H_

#include "Settings.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <array>

namespace Mumble::SpeechCleanup {

struct Selection {
	Settings::SpeechCleanupBackend backend = Settings::RNNoiseBackend;
	QString modelId                       = QStringLiteral("rnnoise:embedded");
	QString customModelPath               = {};
};

inline bool operator==(const Selection &lhs, const Selection &rhs) {
	return lhs.backend == rhs.backend && lhs.modelId == rhs.modelId && lhs.customModelPath == rhs.customModelPath;
}

inline bool operator!=(const Selection &lhs, const Selection &rhs) {
	return !(lhs == rhs);
}

inline constexpr std::array< Settings::SpeechCleanupBackend, 3 > supportedBackends = {
	Settings::RNNoiseBackend,
	Settings::DTLNBackend,
	Settings::DeepFilterNetBackend
};

inline const char *backendDisplayName(Settings::SpeechCleanupBackend backend) {
	switch (backend) {
		case Settings::RNNoiseBackend:
			return "RNNoise";
		case Settings::DTLNBackend:
			return "DTLN";
		case Settings::DeepFilterNetBackend:
			return "DeepFilterNet";
	}

	return "Unknown";
}

inline const char *defaultModelId(Settings::SpeechCleanupBackend backend) {
	switch (backend) {
		case Settings::RNNoiseBackend:
			return "rnnoise:embedded";
		case Settings::DTLNBackend:
			return "dtln:baseline";
		case Settings::DeepFilterNetBackend:
			return "deepfilternet:default";
	}

	return "rnnoise:embedded";
}

inline QStringList supportedModelIds(Settings::SpeechCleanupBackend backend) {
	switch (backend) {
		case Settings::RNNoiseBackend:
			return {
				QStringLiteral("rnnoise:embedded"),
				QStringLiteral("rnnoise:little"),
				QStringLiteral("rnnoise:custom"),
			};
		case Settings::DTLNBackend:
			return {
				QStringLiteral("dtln:baseline"),
				QStringLiteral("dtln:norm500h"),
				QStringLiteral("dtln:norm40h"),
			};
		case Settings::DeepFilterNetBackend:
			return { QStringLiteral("deepfilternet:default") };
	}

	return { QString::fromLatin1(defaultModelId(backend)) };
}

inline QString normalizedModelId(Settings::SpeechCleanupBackend backend, const QString &modelId) {
	const QString requested = modelId.trimmed();
	if (requested.isEmpty()) {
		return QString::fromLatin1(defaultModelId(backend));
	}

	const QStringList supportedIds = supportedModelIds(backend);
	for (const QString &supportedId : supportedIds) {
		if (supportedId == requested) {
			return supportedId;
		}
	}

	return QString::fromLatin1(defaultModelId(backend));
}

inline QString modelDisplayName(Settings::SpeechCleanupBackend backend, const QString &modelId) {
	const QString normalized = normalizedModelId(backend, modelId);

	switch (backend) {
		case Settings::RNNoiseBackend:
			if (normalized == QLatin1String("rnnoise:little")) {
				return QObject::tr("Little model");
			}
			if (normalized == QLatin1String("rnnoise:custom")) {
				return QObject::tr("Custom file");
			}
			return QObject::tr("Embedded default");
		case Settings::DTLNBackend:
			if (normalized == QLatin1String("dtln:norm500h")) {
				return QObject::tr("Norm 500h");
			}
			if (normalized == QLatin1String("dtln:norm40h")) {
				return QObject::tr("Norm 40h");
			}
			return QObject::tr("Baseline");
		case Settings::DeepFilterNetBackend:
			return QObject::tr("Default model");
	}

	return QObject::tr("Default model");
}

inline bool usesCustomModelPath(Settings::SpeechCleanupBackend backend, const QString &modelId) {
	return backend == Settings::RNNoiseBackend && normalizedModelId(backend, modelId) == QLatin1String("rnnoise:custom");
}

inline Selection normalizeSelection(Selection selection) {
	selection.modelId = normalizedModelId(selection.backend, selection.modelId);
	if (!usesCustomModelPath(selection.backend, selection.modelId)) {
		selection.customModelPath.clear();
	}
	return selection;
}

inline bool isBackendAvailable(Settings::SpeechCleanupBackend backend) {
	switch (backend) {
		case Settings::RNNoiseBackend:
#ifdef USE_RNNOISE
			return true;
#else
			return false;
#endif
		case Settings::DTLNBackend:
#ifdef USE_DTLN
			return true;
#else
			return false;
#endif
		case Settings::DeepFilterNetBackend:
#ifdef USE_DEEPFILTERNET
			return true;
#else
			return false;
#endif
	}

	return false;
}

inline QString unavailableReason(Settings::SpeechCleanupBackend backend) {
	switch (backend) {
		case Settings::RNNoiseBackend:
#ifdef USE_RNNOISE
			return QString();
#else
			return QObject::tr("RNNoise support is not compiled into this build.");
#endif
		case Settings::DTLNBackend:
#ifdef USE_DTLN
			return QString();
#else
			return QObject::tr("DTLN support is not compiled into this build.");
#endif
		case Settings::DeepFilterNetBackend:
#ifdef USE_DEEPFILTERNET
			return QString();
#else
			return QObject::tr("DeepFilterNet support is not compiled into this build.");
#endif
	}

	return QObject::tr("This speech cleanup backend is not available.");
}

inline bool hasAnyAvailableBackend() {
	for (Settings::SpeechCleanupBackend backend : supportedBackends) {
		if (isBackendAvailable(backend)) {
			return true;
		}
	}

	return false;
}

inline Settings::SpeechCleanupBackend fallbackBackend() {
	for (Settings::SpeechCleanupBackend backend : supportedBackends) {
		if (isBackendAvailable(backend)) {
			return backend;
		}
	}

	return Settings::RNNoiseBackend;
}

} // namespace Mumble::SpeechCleanup

#endif // MUMBLE_MUMBLE_SPEECHCLEANUP_H_
