// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareHelperServer.h"

#include "ScreenShareExternalProcess.h"
#include "ScreenShareIPC.h"
#include "ScreenShareRelayClient.h"
#include "ScreenShareSessionPlanner.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QProcess>
#include <QtCore/QUrl>
#include <QtNetwork/QLocalSocket>

namespace {
	constexpr int IDLE_TIMEOUT_MSEC = 30000;

	QString streamIDFromPayload(const QJsonObject &payload) {
		return payload.value(QStringLiteral("stream_id")).toString().trimmed();
	}

	QByteArray serializeReply(const QJsonObject &reply) {
		QByteArray data = QJsonDocument(reply).toJson(QJsonDocument::Compact);
		data.append('\n');
		return data;
	}

	void appendWarnings(QJsonObject *payload, const QStringList &warnings) {
		if (!payload || warnings.isEmpty()) {
			return;
		}

		QJsonArray warningArray = payload->value(QStringLiteral("warnings")).toArray();
		for (const QString &warning : warnings) {
			if (!warning.trimmed().isEmpty()) {
				warningArray.push_back(warning);
			}
		}
		payload->insert(QStringLiteral("warnings"), warningArray);
	}

	QString redactEndpointForLog(const QString &endpointUrl) {
		const QUrl url(endpointUrl);
		if (!url.isValid() || url.scheme().trimmed().toLower() == QLatin1String("file")) {
			return endpointUrl;
		}

		QUrl redacted = url;
		redacted.setQuery(QString());
		return redacted.toString(QUrl::FullyEncoded);
	}
} // namespace

ScreenShareHelperServer::ScreenShareHelperServer(QObject *parent)
	: QObject(parent), m_server(new QLocalServer(this)), m_capabilities(ScreenShareMediaSupport::probe()) {
	m_idleTimer.setInterval(IDLE_TIMEOUT_MSEC);
	m_idleTimer.setSingleShot(true);

	connect(m_server, &QLocalServer::newConnection, this, &ScreenShareHelperServer::handleNewConnection);
	connect(&m_idleTimer, &QTimer::timeout, this, &ScreenShareHelperServer::handleIdleTimeout);
}

ScreenShareHelperServer::~ScreenShareHelperServer() {
	stopAllSessions();
}

bool ScreenShareHelperServer::start(QString *errorMessage) {
	const QString socketName = Mumble::ScreenShare::IPC::socketPath();

	QLocalServer::removeServer(socketName);
#ifndef Q_OS_WIN
	QFile::remove(socketName);
#endif

	if (!m_server->listen(socketName)) {
		if (errorMessage) {
			*errorMessage = m_server->errorString();
		}

		return false;
	}

	qInfo().nospace() << "ScreenShareHelper: listening on " << socketName;
	refreshIdleTimer();
	return true;
}

void ScreenShareHelperServer::handleNewConnection() {
	while (QLocalSocket *socket = m_server->nextPendingConnection()) {
		m_socketBuffers.insert(socket, QByteArray());
		connect(socket, &QLocalSocket::readyRead, this, &ScreenShareHelperServer::handleSocketReadyRead);
		connect(socket, &QLocalSocket::disconnected, this, &ScreenShareHelperServer::handleSocketDisconnected);
	}

	refreshIdleTimer();
}

void ScreenShareHelperServer::handleSocketReadyRead() {
	QLocalSocket *socket = qobject_cast< QLocalSocket * >(sender());
	if (!socket || !m_socketBuffers.contains(socket)) {
		return;
	}

	QByteArray &buffer = m_socketBuffers[socket];
	buffer.append(socket->readAll());

	const qsizetype newlineIndex = buffer.indexOf('\n');
	if (newlineIndex < 0) {
		return;
	}

	const QByteArray rawRequest = buffer.left(newlineIndex).trimmed();
	buffer.clear();

	QJsonObject reply;
	if (rawRequest.isEmpty()) {
		reply = Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("Empty request."));
	} else {
		QJsonParseError parseError;
		const QJsonDocument requestDoc = QJsonDocument::fromJson(rawRequest, &parseError);
		if (parseError.error != QJsonParseError::NoError || !requestDoc.isObject()) {
			reply = Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("Malformed JSON request."));
		} else {
			reply = dispatchRequest(requestDoc.object());
		}
	}

	socket->write(serializeReply(reply));
	socket->flush();
	socket->disconnectFromServer();
}

void ScreenShareHelperServer::handleSocketDisconnected() {
	QLocalSocket *socket = qobject_cast< QLocalSocket * >(sender());
	if (!socket) {
		return;
	}

	m_socketBuffers.remove(socket);
	socket->deleteLater();
	refreshIdleTimer();
}

void ScreenShareHelperServer::handleIdleTimeout() {
	if (!m_publishSessions.isEmpty() || !m_viewSessions.isEmpty()) {
		refreshIdleTimer();
		return;
	}

	qInfo("ScreenShareHelper: idle timeout reached, shutting down.");
	QCoreApplication::quit();
}

QJsonObject ScreenShareHelperServer::dispatchRequest(const QJsonObject &request) {
	if (request.value(QStringLiteral("version")).toInt() != Mumble::ScreenShare::IPC::PROTOCOL_VERSION) {
		return Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("Unsupported protocol version."));
	}

	const QString commandName = request.value(QStringLiteral("command")).toString();
	const std::optional< Mumble::ScreenShare::IPC::Command > command =
		Mumble::ScreenShare::IPC::commandFromName(commandName);
	if (!command.has_value()) {
		return Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("Unknown command."));
	}

	const QJsonObject payload = request.value(QStringLiteral("payload")).toObject();
	switch (*command) {
		case Mumble::ScreenShare::IPC::Command::QueryCapabilities:
			return handleQueryCapabilities();
		case Mumble::ScreenShare::IPC::Command::StartPublish:
			return handleStartPublish(payload);
		case Mumble::ScreenShare::IPC::Command::StopPublish:
			return handleStopPublish(payload);
		case Mumble::ScreenShare::IPC::Command::StartView:
			return handleStartView(payload);
		case Mumble::ScreenShare::IPC::Command::StopView:
			return handleStopView(payload);
	}

	return Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("Unhandled command."));
}

QJsonObject ScreenShareHelperServer::handleQueryCapabilities() {
	QJsonObject payload;
	payload.insert(QStringLiteral("supports_signaling"), true);
	payload.insert(QStringLiteral("helper_available"), true);
	payload.insert(QStringLiteral("capture_supported"), m_capabilities.captureSupported);
	payload.insert(QStringLiteral("view_supported"), m_capabilities.viewSupported);
	payload.insert(QStringLiteral("hardware_encoding_preferred"), m_capabilities.hardwareEncodingPreferred);
	payload.insert(QStringLiteral("supported_codecs"),
				   Mumble::ScreenShare::IPC::codecListToJson(m_capabilities.supportedCodecs));
	payload.insert(QStringLiteral("max_width"), static_cast< int >(m_capabilities.maxWidth));
	payload.insert(QStringLiteral("max_height"), static_cast< int >(m_capabilities.maxHeight));
	payload.insert(QStringLiteral("max_fps"), static_cast< int >(m_capabilities.maxFps));
	payload.insert(QStringLiteral("capture_backend"), m_capabilities.captureBackend);
	payload.insert(QStringLiteral("status"), m_capabilities.statusMessage);
	payload.insert(QStringLiteral("mode"), QStringLiteral("external-process"));
	payload.insert(QStringLiteral("encoder_backends"), ScreenShareSessionPlanner::advertisedEncoderBackends());
	payload.insert(QStringLiteral("relay_contracts"), ScreenShareRelayClient::advertisedContracts());
	payload.insert(QStringLiteral("runtime_relay_transports"), ScreenShareRelayClient::runtimeRelayTransports());
	payload.insert(QStringLiteral("signaling_relay_transports"), ScreenShareRelayClient::signalingRelayTransports());
	payload.insert(QStringLiteral("runtime_support"),
				   ScreenShareExternalProcess::runtimeSupportToJson(ScreenShareExternalProcess::probeRuntimeSupport()));
	return Mumble::ScreenShare::IPC::makeSuccessReply(payload);
}

QJsonObject ScreenShareHelperServer::handleStartPublish(const QJsonObject &payload) {
	const ScreenShareSessionPlanner::Plan plan = ScreenShareSessionPlanner::planPublish(payload, m_capabilities);
	if (!plan.valid) {
		return Mumble::ScreenShare::IPC::makeErrorReply(plan.errorMessage);
	}

	const QString streamID = plan.payload.value(QStringLiteral("stream_id")).toString();
	stopSession(m_publishSessions, streamID);

	ScreenShareExternalProcess::LaunchResult launch = ScreenShareExternalProcess::startPublish(plan.payload, this);
	if (!launch.started) {
		return Mumble::ScreenShare::IPC::makeErrorReply(launch.errorMessage, plan.payload);
	}

	ManagedSession session;
	session.payload = plan.payload;
	session.process = launch.process;
	session.payload.insert(QStringLiteral("mode"),
						   launch.usedStub ? QStringLiteral("publish-stub") : QStringLiteral("publish-live"));
	session.payload.insert(QStringLiteral("execution_mode"), launch.executionMode);
	if (!launch.endpointUrl.isEmpty()) {
		session.payload.insert(QStringLiteral("active_endpoint_url"), launch.endpointUrl);
	}
	if (!launch.selectedEncoder.isEmpty()) {
		session.payload.insert(QStringLiteral("active_encoder_backend"), launch.selectedEncoder);
	}
	if (!launch.selectedCaptureSource.isEmpty()) {
		session.payload.insert(QStringLiteral("active_capture_source"), launch.selectedCaptureSource);
	}
	appendWarnings(&session.payload, launch.warnings);
	m_publishSessions.insert(streamID, session);
	if (launch.process) {
		attachProcessLogging(streamID, true, QStringLiteral("publish"));
	}
	qInfo().nospace() << "ScreenShareHelper: started publish session " << streamID << " via "
					  << session.payload.value(QStringLiteral("active_encoder_backend")).toString(
							 session.payload.value(QStringLiteral("planned_encoder_backend")).toString())
					  << " -> "
					  << redactEndpointForLog(session.payload.value(QStringLiteral("active_endpoint_url")).toString(
							 session.payload.value(QStringLiteral("relay_url")).toString()));

	refreshIdleTimer();
	return Mumble::ScreenShare::IPC::makeSuccessReply(session.payload);
}

QJsonObject ScreenShareHelperServer::handleStopPublish(const QJsonObject &payload) {
	const QString streamID = streamIDFromPayload(payload);
	if (streamID.isEmpty()) {
		return Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("stop-publish requires stream_id."));
	}

	stopSession(m_publishSessions, streamID);
	refreshIdleTimer();
	return Mumble::ScreenShare::IPC::makeSuccessReply(QJsonObject{ { QStringLiteral("stream_id"), streamID } });
}

QJsonObject ScreenShareHelperServer::handleStartView(const QJsonObject &payload) {
	const ScreenShareSessionPlanner::Plan plan = ScreenShareSessionPlanner::planView(payload, m_capabilities);
	if (!plan.valid) {
		return Mumble::ScreenShare::IPC::makeErrorReply(plan.errorMessage);
	}

	const QString streamID = plan.payload.value(QStringLiteral("stream_id")).toString();
	stopSession(m_viewSessions, streamID);

	ScreenShareExternalProcess::LaunchResult launch = ScreenShareExternalProcess::startView(plan.payload, this);
	if (!launch.started) {
		return Mumble::ScreenShare::IPC::makeErrorReply(launch.errorMessage, plan.payload);
	}

	ManagedSession session;
	session.payload = plan.payload;
	session.process = launch.process;
	session.payload.insert(QStringLiteral("mode"),
						   launch.usedStub ? QStringLiteral("view-stub") : QStringLiteral("view-live"));
	session.payload.insert(QStringLiteral("execution_mode"), launch.executionMode);
	if (!launch.endpointUrl.isEmpty()) {
		session.payload.insert(QStringLiteral("active_endpoint_url"), launch.endpointUrl);
	}
	if (!launch.selectedRenderer.isEmpty()) {
		session.payload.insert(QStringLiteral("active_renderer_backend"), launch.selectedRenderer);
	}
	appendWarnings(&session.payload, launch.warnings);
	m_viewSessions.insert(streamID, session);
	if (launch.process) {
		attachProcessLogging(streamID, false, QStringLiteral("view"));
	}
	qInfo().nospace() << "ScreenShareHelper: started viewer session " << streamID << " -> "
					  << redactEndpointForLog(session.payload.value(QStringLiteral("active_endpoint_url")).toString(
							 session.payload.value(QStringLiteral("relay_url")).toString()));

	refreshIdleTimer();
	return Mumble::ScreenShare::IPC::makeSuccessReply(session.payload);
}

QJsonObject ScreenShareHelperServer::handleStopView(const QJsonObject &payload) {
	const QString streamID = streamIDFromPayload(payload);
	if (streamID.isEmpty()) {
		return Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("stop-view requires stream_id."));
	}

	stopSession(m_viewSessions, streamID);
	refreshIdleTimer();
	return Mumble::ScreenShare::IPC::makeSuccessReply(QJsonObject{ { QStringLiteral("stream_id"), streamID } });
}

void ScreenShareHelperServer::stopAllSessions() {
	const QStringList publishIDs = m_publishSessions.keys();
	for (const QString &streamID : publishIDs) {
		stopSession(m_publishSessions, streamID);
	}

	const QStringList viewIDs = m_viewSessions.keys();
	for (const QString &streamID : viewIDs) {
		stopSession(m_viewSessions, streamID);
	}
}

void ScreenShareHelperServer::stopSession(QHash< QString, ManagedSession > &sessions, const QString &streamID) {
	if (streamID.isEmpty() || !sessions.contains(streamID)) {
		return;
	}

	const ManagedSession session = sessions.take(streamID);
	if (session.process) {
		ScreenShareExternalProcess::stop(session.process);
	}
}

void ScreenShareHelperServer::attachProcessLogging(const QString &streamID, const bool publish, const QString &label) {
	QHash< QString, ManagedSession > &sessions = publish ? m_publishSessions : m_viewSessions;
	if (!sessions.contains(streamID) || !sessions.value(streamID).process) {
		return;
	}

	QProcess *process = sessions.value(streamID).process;
	connect(process, &QProcess::readyRead, this, [process, streamID, label]() {
		const QString output = QString::fromUtf8(process->readAll()).trimmed();
		if (!output.isEmpty()) {
			qInfo().noquote() << QStringLiteral("ScreenShareHelper[%1:%2]: %3").arg(label, streamID, output);
		}
	});

	connect(process, &QProcess::errorOccurred, this, [streamID, label](QProcess::ProcessError error) {
		qWarning().noquote()
			<< QStringLiteral("ScreenShareHelper[%1:%2]: process error %3").arg(label, streamID).arg(static_cast< int >(error));
	});

	connect(process, qOverload< int, QProcess::ExitStatus >(&QProcess::finished), this,
			[this, streamID, publish, label, process](const int exitCode, const QProcess::ExitStatus exitStatus) {
				QHash< QString, ManagedSession > &sessionMap = publish ? m_publishSessions : m_viewSessions;
				if (sessionMap.contains(streamID) && sessionMap.value(streamID).process == process) {
					sessionMap.remove(streamID);
				}

				const QString output = QString::fromUtf8(process->readAll()).trimmed();
				if (!output.isEmpty()) {
					qInfo().noquote() << QStringLiteral("ScreenShareHelper[%1:%2]: %3").arg(label, streamID, output);
				}

				qInfo().nospace() << "ScreenShareHelper: " << label << " session " << streamID << " exited with code "
								  << exitCode << " status " << static_cast< int >(exitStatus);
				process->deleteLater();
				refreshIdleTimer();
			});
}

void ScreenShareHelperServer::refreshIdleTimer() {
	if (!m_publishSessions.isEmpty() || !m_viewSessions.isEmpty()) {
		m_idleTimer.stop();
		return;
	}

	m_idleTimer.start();
}
