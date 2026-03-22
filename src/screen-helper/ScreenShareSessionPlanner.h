// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SCREENHELPER_SCREENSHARESESSIONPLANNER_H_
#define MUMBLE_SCREENHELPER_SCREENSHARESESSIONPLANNER_H_

#include "ScreenShareMediaSupport.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

class ScreenShareSessionPlanner {
public:
	struct Plan {
		bool valid = false;
		QString errorMessage;
		QJsonObject payload;
	};

	static QJsonArray advertisedEncoderBackends();
	static Plan planPublish(const QJsonObject &payload, const ScreenShareMediaSupport::CapabilitySummary &capabilities);
	static Plan planView(const QJsonObject &payload, const ScreenShareMediaSupport::CapabilitySummary &capabilities);
};

#endif // MUMBLE_SCREENHELPER_SCREENSHARESESSIONPLANNER_H_
