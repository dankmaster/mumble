// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SCREENHELPER_SCREENSHAREHELPERSERVER_H_
#define MUMBLE_SCREENHELPER_SCREENSHAREHELPERSERVER_H_

#include "ScreenShareMediaSupport.h"

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtNetwork/QLocalServer>

class QLocalSocket;
class QProcess;

class ScreenShareHelperServer : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ScreenShareHelperServer)

public:
	explicit ScreenShareHelperServer(QObject *parent = nullptr);
	~ScreenShareHelperServer() override;

	bool start(QString *errorMessage = nullptr);
	QJsonObject capabilityPayload() const;
	QJsonObject runSelfTest();

	struct ManagedSession {
		QJsonObject payload;
		QProcess *process = nullptr;
	};

private slots:
	void handleNewConnection();
	void handleSocketReadyRead();
	void handleSocketDisconnected();
	void handleIdleTimeout();

private:
	QJsonObject dispatchRequest(const QJsonObject &request);
	QJsonObject handleQueryCapabilities() const;
	QJsonObject handleStartPublish(const QJsonObject &payload);
	QJsonObject handleStopPublish(const QJsonObject &payload);
	QJsonObject handleStartView(const QJsonObject &payload);
	QJsonObject handleStopView(const QJsonObject &payload);
	void stopAllSessions();
	void stopSession(QHash< QString, ManagedSession > &sessions, const QString &streamID);
	void attachProcessLogging(const QString &streamID, bool publish, const QString &label);
	void refreshIdleTimer();

	QLocalServer *m_server;
	QTimer m_idleTimer;
	ScreenShareMediaSupport::CapabilitySummary m_capabilities;
	QHash< QLocalSocket *, QByteArray > m_socketBuffers;
	QHash< QString, ManagedSession > m_publishSessions;
	QHash< QString, ManagedSession > m_viewSessions;
};

#endif // MUMBLE_SCREENHELPER_SCREENSHAREHELPERSERVER_H_
