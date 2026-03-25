// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "VersionCheck.h"

#include "MainWindow.h"
#include "Global.h"

VersionCheck::VersionCheck(bool autocheck, QObject *p, bool) : QObject(p), m_preparationWatcher() {
	if (!autocheck && Global::get().mw) {
		Global::get().mw->msgBox(
			tr("Version checks are disabled in this build. No central update service is contacted."));
	}
	deleteLater();
}

void VersionCheck::performRequest() {
	deleteLater();
}

void VersionCheck::fetched(QByteArray a, QUrl url) {
	Q_UNUSED(a);
	Q_UNUSED(url);
}
