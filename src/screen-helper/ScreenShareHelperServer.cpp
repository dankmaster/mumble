// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareHelperServer.h"

#include "ScreenShare.h"
#include "ScreenShareExternalProcess.h"
#include "ScreenShareIPC.h"
#include "ScreenShareRelayClient.h"
#include "ScreenShareSessionPlanner.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QThread>
#include <QtCore/QUrl>
#include <QtNetwork/QLocalSocket>

namespace {
constexpr int IDLE_TIMEOUT_MSEC        = 30000;
constexpr int SELF_TEST_FILE_WAIT_MSEC = 5000;

QString streamIDFromPayload(const QJsonObject &payload) {
	return payload.value(QStringLiteral("stream_id")).toString().trimmed();
}

QString roleTokenFromPayload(const QJsonObject &payload) {
	const QString roleToken = payload.value(QStringLiteral("relay_role_token")).toString().trimmed();
	if (!roleToken.isEmpty()) {
		return roleToken;
	}

	switch (static_cast< MumbleProto::ScreenShareRelayRole >(payload.value(QStringLiteral("relay_role")).toInt())) {
		case MumbleProto::ScreenShareRelayRolePublisher:
			return QStringLiteral("publisher");
		case MumbleProto::ScreenShareRelayRoleViewer:
			return QStringLiteral("viewer");
		default:
			return QStringLiteral("unknown");
	}
}

QString codecTokenFromPayload(const QJsonObject &payload) {
	const QString codecToken = payload.value(QStringLiteral("codec_token")).toString().trimmed();
	if (!codecToken.isEmpty()) {
		return codecToken;
	}

	return Mumble::ScreenShare::codecToConfigToken(
		Mumble::ScreenShare::IPC::codecFromJson(payload.value(QStringLiteral("codec"))));
}

QString relaySchemeFromPayload(const QJsonObject &payload) {
	const QUrl relayUrl(payload.value(QStringLiteral("relay_url")).toString());
	const QString scheme = relayUrl.scheme().trimmed().toLower();
	return scheme.isEmpty() ? QStringLiteral("unknown") : scheme;
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

QJsonArray stringListToJson(const QStringList &values) {
	QJsonArray array;
	for (const QString &value : values) {
		if (!value.trimmed().isEmpty()) {
			array.push_back(value.trimmed());
		}
	}
	return array;
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

bool replySucceeded(const QJsonObject &reply, QString *errorMessage = nullptr) {
	return Mumble::ScreenShare::IPC::replySucceeded(reply, errorMessage);
}

QJsonObject makeSelfTestPayload(const QString &streamID, const QString &relayUrl, const QString &relayRoomID,
								const MumbleProto::ScreenShareRelayRole relayRole) {
	const quint64 now = static_cast< quint64 >(QDateTime::currentMSecsSinceEpoch());

	QJsonObject payload;
	payload.insert(QStringLiteral("stream_id"), streamID);
	payload.insert(QStringLiteral("owner_session"), 1);
	payload.insert(QStringLiteral("scope"), static_cast< int >(MumbleProto::ScreenShareScopeChannel));
	payload.insert(QStringLiteral("scope_id"), 1);
	payload.insert(QStringLiteral("relay_url"), relayUrl);
	payload.insert(QStringLiteral("relay_room_id"), relayRoomID);
	payload.insert(QStringLiteral("relay_transport"), static_cast< int >(MumbleProto::ScreenShareRelayTransportDirect));
	payload.insert(QStringLiteral("relay_transport_token"), QStringLiteral("direct"));
	payload.insert(QStringLiteral("relay_role"), static_cast< int >(relayRole));
	payload.insert(QStringLiteral("relay_role_token"), Mumble::ScreenShare::relayRoleToConfigToken(relayRole));
	payload.insert(QStringLiteral("relay_token"), QStringLiteral("self-test-token"));
	payload.insert(QStringLiteral("relay_session_id"), QStringLiteral("self-test-session"));
	payload.insert(QStringLiteral("relay_token_expires_at"), QString::number(now + 60000));
	payload.insert(QStringLiteral("created_at"), QString::number(now));
	payload.insert(QStringLiteral("state"), static_cast< int >(MumbleProto::ScreenShareLifecycleStateActive));
	payload.insert(QStringLiteral("codec"), static_cast< int >(MumbleProto::ScreenShareCodecH264));
	payload.insert(QStringLiteral("codec_token"), QStringLiteral("h264"));
	payload.insert(QStringLiteral("width"), 1280);
	payload.insert(QStringLiteral("height"), 720);
	payload.insert(QStringLiteral("fps"), 30);
	payload.insert(QStringLiteral("bitrate_kbps"), 4500);
	payload.insert(QStringLiteral("prefer_hardware_encoding"), true);
	return payload;
}

bool waitForFileToAppear(const QString &filePath, qint64 *fileSize = nullptr) {
	const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + SELF_TEST_FILE_WAIT_MSEC;
	while (QDateTime::currentMSecsSinceEpoch() < deadline) {
		const QFileInfo fileInfo(filePath);
		if (fileInfo.exists() && fileInfo.isFile() && fileInfo.size() > 0) {
			if (fileSize) {
				*fileSize = fileInfo.size();
			}
			return true;
		}

		QThread::msleep(100);
	}

	if (fileSize) {
		*fileSize = QFileInfo(filePath).size();
	}
	return false;
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

QJsonObject ScreenShareHelperServer::capabilityPayload() const {
	QJsonObject payload;
	payload.insert(QStringLiteral("supports_signaling"), true);
	payload.insert(QStringLiteral("helper_available"), true);
	payload.insert(QStringLiteral("capture_supported"), m_capabilities.captureSupported);
	payload.insert(QStringLiteral("view_supported"), m_capabilities.viewSupported);
	payload.insert(QStringLiteral("hardware_encoding_preferred"), m_capabilities.hardwareEncodingPreferred);
	payload.insert(QStringLiteral("hardware_encode_supported"), m_capabilities.hardwareEncodeSupported);
	payload.insert(QStringLiteral("hardware_decode_supported"), m_capabilities.hardwareDecodeSupported);
	payload.insert(QStringLiteral("zero_copy_supported"), m_capabilities.zeroCopySupported);
	payload.insert(QStringLiteral("roi_supported"), m_capabilities.roiSupported);
	payload.insert(QStringLiteral("damage_metadata_supported"), m_capabilities.damageMetadataSupported);
	payload.insert(QStringLiteral("supported_codecs"),
				   Mumble::ScreenShare::IPC::codecListToJson(m_capabilities.supportedCodecs));
	payload.insert(QStringLiteral("max_width"), static_cast< int >(m_capabilities.maxWidth));
	payload.insert(QStringLiteral("max_height"), static_cast< int >(m_capabilities.maxHeight));
	payload.insert(QStringLiteral("max_fps"), static_cast< int >(m_capabilities.maxFps));
	payload.insert(QStringLiteral("capture_backend"), m_capabilities.captureBackend);
	payload.insert(QStringLiteral("capture_backends"), stringListToJson(m_capabilities.captureBackends));
	payload.insert(QStringLiteral("supported_ingest_protocols"), stringListToJson(m_capabilities.ingestProtocols));
	payload.insert(QStringLiteral("drm_playback_supported"), false);
	payload.insert(QStringLiteral("drm_systems"), stringListToJson(m_capabilities.drmSystems));
	payload.insert(QStringLiteral("queue_budget_frames"), static_cast< int >(m_capabilities.queueBudgetFrames));
	payload.insert(QStringLiteral("status"), m_capabilities.statusMessage);
	payload.insert(QStringLiteral("mode"), QStringLiteral("external-process"));
	payload.insert(QStringLiteral("encoder_backends"), ScreenShareSessionPlanner::advertisedEncoderBackends());
	payload.insert(QStringLiteral("relay_contracts"), ScreenShareRelayClient::advertisedContracts());
	payload.insert(QStringLiteral("runtime_relay_transports"), ScreenShareRelayClient::runtimeRelayTransports());
	payload.insert(QStringLiteral("signaling_relay_transports"), ScreenShareRelayClient::signalingRelayTransports());
	payload.insert(QStringLiteral("runtime_support"),
				   ScreenShareExternalProcess::runtimeSupportToJson(ScreenShareExternalProcess::probeRuntimeSupport()));
	return payload;
}

QJsonObject ScreenShareHelperServer::handleQueryCapabilities() const {
	return Mumble::ScreenShare::IPC::makeSuccessReply(capabilityPayload());
}

QJsonObject ScreenShareHelperServer::runSelfTest() {
	const QString tempBase = QStandardPaths::writableLocation(QStandardPaths::TempLocation).trimmed().isEmpty()
								 ? QDir::tempPath()
								 : QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	const QString relayDirectory = QDir(tempBase).filePath(QStringLiteral("mumble-screen-share/self-test-relay"));
	QDir(relayDirectory).removeRecursively();
	QDir().mkpath(relayDirectory);

	const QString streamID = QStringLiteral("self-test-%1").arg(QString::number(QDateTime::currentMSecsSinceEpoch()));
	const QString relayRoomID = QStringLiteral("room-%1").arg(streamID);
	const QString relayUrl    = QUrl::fromLocalFile(relayDirectory + QDir::separator()).toString();

	const QJsonObject publishPayload =
		makeSelfTestPayload(streamID, relayUrl, relayRoomID, MumbleProto::ScreenShareRelayRolePublisher);
	const QJsonObject publishReply = handleStartPublish(publishPayload);
	QString errorMessage;
	if (!replySucceeded(publishReply, &errorMessage)) {
		return Mumble::ScreenShare::IPC::makeErrorReply(
			QStringLiteral("Self-test publish start failed: %1").arg(errorMessage), publishReply);
	}

	const QJsonObject publishResult = publishReply.value(QStringLiteral("payload")).toObject();
	const QString outputUrl         = publishResult.value(QStringLiteral("active_endpoint_url")).toString();
	const QString outputPath        = QUrl(outputUrl).toLocalFile();
	qint64 outputSize               = 0;
	if (outputPath.isEmpty() || !waitForFileToAppear(outputPath, &outputSize)) {
		handleStopPublish(QJsonObject{ { QStringLiteral("stream_id"), streamID } });
		return Mumble::ScreenShare::IPC::makeErrorReply(
			QStringLiteral("Self-test did not observe a non-empty relay file."),
			QJsonObject{
				{ QStringLiteral("stream_id"), streamID },
				{ QStringLiteral("active_endpoint_url"), outputUrl },
				{ QStringLiteral("observed_file_size"), QString::number(outputSize) },
			});
	}

	const QJsonObject stopPublishReply = handleStopPublish(QJsonObject{ { QStringLiteral("stream_id"), streamID } });
	if (!replySucceeded(stopPublishReply, &errorMessage)) {
		return Mumble::ScreenShare::IPC::makeErrorReply(
			QStringLiteral("Self-test publish stop failed: %1").arg(errorMessage), stopPublishReply);
	}

	const QJsonObject viewPayload =
		makeSelfTestPayload(streamID, relayUrl, relayRoomID, MumbleProto::ScreenShareRelayRoleViewer);
	const QJsonObject viewReply = handleStartView(viewPayload);
	if (!replySucceeded(viewReply, &errorMessage)) {
		return Mumble::ScreenShare::IPC::makeErrorReply(
			QStringLiteral("Self-test view start failed: %1").arg(errorMessage), viewReply);
	}

	QThread::msleep(500);
	const QJsonObject stopViewReply = handleStopView(QJsonObject{ { QStringLiteral("stream_id"), streamID } });
	if (!replySucceeded(stopViewReply, &errorMessage)) {
		return Mumble::ScreenShare::IPC::makeErrorReply(
			QStringLiteral("Self-test view stop failed: %1").arg(errorMessage), stopViewReply);
	}

	QJsonObject result;
	result.insert(QStringLiteral("stream_id"), streamID);
	result.insert(QStringLiteral("relay_url"), relayUrl);
	result.insert(QStringLiteral("relay_room_id"), relayRoomID);
	result.insert(QStringLiteral("output_file"), outputPath);
	result.insert(QStringLiteral("output_size"), QString::number(outputSize));
	result.insert(QStringLiteral("publish_reply"), publishResult);
	result.insert(QStringLiteral("view_reply"), viewReply.value(QStringLiteral("payload")).toObject());
	result.insert(QStringLiteral("runtime_support"),
				  ScreenShareExternalProcess::runtimeSupportToJson(ScreenShareExternalProcess::probeRuntimeSupport()));
	return Mumble::ScreenShare::IPC::makeSuccessReply(result);
}

QJsonObject ScreenShareHelperServer::handleStartPublish(const QJsonObject &payload) {
	const ScreenShareSessionPlanner::Plan plan = ScreenShareSessionPlanner::planPublish(payload, m_capabilities);
	if (!plan.valid) {
		return Mumble::ScreenShare::IPC::makeErrorReply(plan.errorMessage);
	}
	logSessionPlanSummary(plan.payload, QStringLiteral("publish"), QStringLiteral("planned"));

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
	logSessionPlanSummary(session.payload, QStringLiteral("publish"), QStringLiteral("started"));
	logPayloadWarnings(session.payload, QStringLiteral("publish"), streamID);
	m_publishSessions.insert(streamID, session);
	if (launch.process) {
		attachProcessLogging(streamID, true, QStringLiteral("publish"));
	}
	qInfo().nospace() << "ScreenShareHelper: started publish session " << streamID << " via "
					  << session.payload.value(QStringLiteral("active_encoder_backend"))
							 .toString(session.payload.value(QStringLiteral("planned_encoder_backend")).toString())
					  << " -> "
					  << redactEndpointForLog(
							 session.payload.value(QStringLiteral("active_endpoint_url"))
								 .toString(session.payload.value(QStringLiteral("relay_url")).toString()));

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
	logSessionPlanSummary(plan.payload, QStringLiteral("view"), QStringLiteral("planned"));

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
	logSessionPlanSummary(session.payload, QStringLiteral("view"), QStringLiteral("started"));
	logPayloadWarnings(session.payload, QStringLiteral("view"), streamID);
	m_viewSessions.insert(streamID, session);
	if (launch.process) {
		attachProcessLogging(streamID, false, QStringLiteral("view"));
	}
	qInfo().nospace() << "ScreenShareHelper: started viewer session " << streamID << " -> "
					  << redactEndpointForLog(
							 session.payload.value(QStringLiteral("active_endpoint_url"))
								 .toString(session.payload.value(QStringLiteral("relay_url")).toString()));

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

void ScreenShareHelperServer::logSessionPlanSummary(const QJsonObject &payload, const QString &label,
													const QString &phase) const {
	const QString streamID       = payload.value(QStringLiteral("stream_id")).toString().trimmed();
	const QString role           = roleTokenFromPayload(payload);
	const QString relayScheme    = relaySchemeFromPayload(payload);
	const QString codec          = codecTokenFromPayload(payload);
	const QString plannedBackend = payload.value(QStringLiteral("planned_encoder_backend")).toString().trimmed();
	const QString actualBackend  = payload.value(QStringLiteral("active_encoder_backend"))
									  .toString(payload.value(QStringLiteral("active_renderer_backend")).toString())
									  .trimmed();
	const QString captureSource = payload.value(QStringLiteral("active_capture_source")).toString().trimmed();
	const QString executionMode = payload.value(QStringLiteral("execution_mode")).toString().trimmed();

	qInfo().noquote() << QStringLiteral("ScreenShareHelper[%1:%2]: %3 summary role=%4 relay_scheme=%5 codec=%6 "
										"planned_backend=%7 actual_backend=%8 capture_source=%9 execution_mode=%10")
							 .arg(label, streamID, phase, role, relayScheme, codec,
								  plannedBackend.isEmpty() ? QStringLiteral("-") : plannedBackend,
								  actualBackend.isEmpty() ? QStringLiteral("-") : actualBackend,
								  captureSource.isEmpty() ? QStringLiteral("-") : captureSource,
								  executionMode.isEmpty() ? QStringLiteral("-") : executionMode);
}

void ScreenShareHelperServer::logPayloadWarnings(const QJsonObject &payload, const QString &label,
												 const QString &streamID) {
	const QJsonArray warnings = payload.value(QStringLiteral("warnings")).toArray();
	for (const QJsonValue &warningValue : warnings) {
		const QString warning = warningValue.toString().trimmed();
		if (!warning.isEmpty()) {
			qWarning().noquote()
				<< QStringLiteral("ScreenShareHelper[%1:%2]: warning: %3").arg(label, streamID, warning);
		}
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
		qWarning().noquote() << QStringLiteral("ScreenShareHelper[%1:%2]: process error %3")
									.arg(label, streamID)
									.arg(static_cast< int >(error));
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
