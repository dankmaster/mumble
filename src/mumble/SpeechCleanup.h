// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SPEECHCLEANUP_H_
#define MUMBLE_MUMBLE_SPEECHCLEANUP_H_

#include "Settings.h"

#include <QObject>
#include <QString>

namespace Mumble::SpeechCleanup {

inline const char *backendDisplayName(Settings::SpeechCleanupBackend backend) {
	switch (backend) {
		case Settings::RNNoiseBackend:
			return "RNNoise";
		case Settings::DTLNBackend:
			return "DTLN";
	}

	return "Unknown";
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
	}

	return QObject::tr("This speech cleanup backend is not available.");
}

inline bool hasAnyAvailableBackend() {
	return isBackendAvailable(Settings::RNNoiseBackend) || isBackendAvailable(Settings::DTLNBackend);
}

} // namespace Mumble::SpeechCleanup

#endif // MUMBLE_MUMBLE_SPEECHCLEANUP_H_
