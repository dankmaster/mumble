// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareHelperServer.h"

#include <QtCore/QCoreApplication>

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	app.setApplicationName(QStringLiteral("mumble-screen-helper"));

	ScreenShareHelperServer helperServer;
	QString errorMessage;
	if (!helperServer.start(&errorMessage)) {
		qCritical().noquote() << QStringLiteral("ScreenShareHelper: failed to start: %1").arg(errorMessage);
		return 1;
	}

	return app.exec();
}
