// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CRASHREPORTER_H_
#	define MUMBLE_MUMBLE_CRASHREPORTER_H_

#	include <QtCore/QObject>
#	include <QtWidgets/QDialog>

class CrashReporter : QDialog {
	Q_OBJECT
	Q_DISABLE_COPY(CrashReporter)

public:
	CrashReporter(QWidget *p = 0);
	~CrashReporter() Q_DECL_OVERRIDE;
	void run();
};

#else
class CrashReporter;
#endif
