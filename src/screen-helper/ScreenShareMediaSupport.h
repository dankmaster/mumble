// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SCREENHELPER_SCREENSHAREMEDIASUPPORT_H_
#define MUMBLE_SCREENHELPER_SCREENSHAREMEDIASUPPORT_H_

#include <QtCore/QList>
#include <QtCore/QString>

class ScreenShareMediaSupport {
public:
	struct CapabilitySummary {
		bool captureSupported = false;
		bool viewSupported = true;
		bool hardwareEncodingPreferred = true;
		QList< int > supportedCodecs;
		unsigned int maxWidth = 0;
		unsigned int maxHeight = 0;
		unsigned int maxFps = 0;
		QString captureBackend;
		QString statusMessage;
	};

	static CapabilitySummary probe();
};

#endif // MUMBLE_SCREENHELPER_SCREENSHAREMEDIASUPPORT_H_
