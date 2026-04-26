// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_RELAYWINDOWBRIDGE_H_
#define MUMBLE_MUMBLE_RELAYWINDOWBRIDGE_H_

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#	include <QtCore/QObject>
#	include <QtCore/QString>

class RelayWindowBridge : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(RelayWindowBridge)

public:
	explicit RelayWindowBridge(QObject *parent = nullptr);

	Q_INVOKABLE void ready();
	Q_INVOKABLE void requestFallback(const QString &reason = QString());
	Q_INVOKABLE void requestClose(const QString &reason = QString());
	Q_INVOKABLE void reportStats(const QString &summary);

signals:
	void bootReady();
	void fallbackRequested(const QString &reason);
	void closeRequested(const QString &reason);
	void statsReported(const QString &summary);
};

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)

#endif // MUMBLE_MUMBLE_RELAYWINDOWBRIDGE_H_
