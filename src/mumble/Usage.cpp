// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Usage.h"

#include "ClientUser.h"
#include "LCD.h"
#include "NetworkConfig.h"
#include "OSInfo.h"
#include "Version.h"
#include "Global.h"

#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>

Usage::Usage(QObject *p) : QObject(p) {
	qbReport.open(QBuffer::ReadWrite);
	qdsReport.setDevice(&qbReport);
	qdsReport.setVersion(QDataStream::Qt_4_4);
	qdsReport << static_cast< unsigned int >(2);

	// Wait 10 minutes (so we know they're actually using this), then...
	QTimer::singleShot(60 * 10 * 1000, this, SLOT(registerUsage()));
}

void Usage::registerUsage() {
	// Usage reporting is disabled in this build.
}
