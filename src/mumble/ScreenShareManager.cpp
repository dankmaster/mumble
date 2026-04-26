// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareManager.h"

#include "ClientUser.h"
#include "Log.h"
#include "Global.h"
#if defined(MUMBLE_HAS_MODERN_LAYOUT)
#	include "RelayWindowHost.h"
#endif
#include "ProtoUtils.h"
#include "QtUtils.h"
#include "ScreenShare.h"
#include "ServerHandler.h"

#include <algorithm>
#include <optional>

#include <QtCore/QDateTime>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QStringList>

namespace {
bool envFlagEnabled(const char *name) {
	const QString value = qEnvironmentVariable(name).trimmed().toLower();
	return value == QLatin1String("1") || value == QLatin1String("true") || value == QLatin1String("yes")
		   || value == QLatin1String("on");
}

std::optional< bool > envFlagOverride(const char *name) {
	if (!qEnvironmentVariableIsSet(name)) {
		return std::nullopt;
	}

	const QString value = qEnvironmentVariable(name).trimmed().toLower();
	if (value == QLatin1String("1") || value == QLatin1String("true") || value == QLatin1String("yes")
		|| value == QLatin1String("on")) {
		return true;
	}
	if (value == QLatin1String("0") || value == QLatin1String("false") || value == QLatin1String("no")
		|| value == QLatin1String("off")) {
		return false;
	}

	return envFlagEnabled(name);
}

QList< int > codecFallbackOrderFromState(const MumbleProto::ScreenShareState &msg) {
	QList< int > codecs;
	codecs.reserve(msg.codec_fallback_order_size());
	for (int i = 0; i < msg.codec_fallback_order_size(); ++i) {
		codecs.append(static_cast< int >(msg.codec_fallback_order(i)));
	}

	return Mumble::ScreenShare::sanitizeCodecList(codecs);
}

bool tokenExpired(const quint64 expiresAt, const qint64 skewMsec = 5000) {
	if (expiresAt == 0) {
		return false;
	}

	return static_cast< quint64 >(QDateTime::currentMSecsSinceEpoch() + skewMsec) >= expiresAt;
}

bool isWebRtcRelayTransport(const MumbleProto::ScreenShareRelayTransport transport) {
	return transport == MumbleProto::ScreenShareRelayTransportWebRTC;
}
} // namespace

ScreenShareManager::ScreenShareManager(QObject *parent) : QObject(parent) {
	m_helperClient = new ScreenShareHelperClient(this);
	connect(m_helperClient, &ScreenShareHelperClient::capabilitiesChanged, this,
			[this]() { logLocalShareAvailabilityDiagnostic(QStringLiteral("helper-capabilities")); });
}

ScreenShareHelperClient::CapabilitySnapshot ScreenShareManager::detectAdvertisedCapabilities() {
	return ScreenShareHelperClient::detectLocalCapabilities();
}

ScreenShareHelperClient &ScreenShareManager::helperClient() {
	return *m_helperClient;
}

const ScreenShareHelperClient &ScreenShareManager::helperClient() const {
	return *m_helperClient;
}

bool ScreenShareManager::supportsInAppRelayTransport(const MumbleProto::ScreenShareRelayTransport transport) const {
#if defined(MUMBLE_HAS_MODERN_LAYOUT)
	return isWebRtcRelayTransport(transport);
#else
	Q_UNUSED(transport);
	return false;
#endif
}

void ScreenShareManager::logLocalShareAvailabilityDiagnostic(const QString &context) const {
	if (!Global::get().s.bScreenShareDiagnostics) {
		return;
	}

	const QString reason = localShareUnavailableReason();
	if (context == m_lastLoggedAvailabilityContext && reason == m_lastLoggedAvailabilityReason) {
		return;
	}

	m_lastLoggedAvailabilityContext = context;
	m_lastLoggedAvailabilityReason  = reason;

	const ScreenShareHelperClient::CapabilitySnapshot &capabilities = m_helperClient->capabilities();
	QStringList relayTransports;
	for (const int transport : capabilities.runtimeRelayTransports) {
		relayTransports << QString::number(transport);
	}

	qInfo().noquote()
		<< QStringLiteral("ScreenShareManager: availability context=%1 connected=%2 enabled=%3 helper_required=%4 "
						  "capture_supported=%5 view_supported=%6 helper_available=%7 relay_url=%8 "
						  "runtime_transports=[%9] in_app_relay=%10 capture_backend=%11 hw_encode=%12 "
						  "zero_copy=%13 roi=%14 reason=%15")
			   .arg(context.isEmpty() ? QStringLiteral("-") : context,
					Global::get().sh ? QStringLiteral("true") : QStringLiteral("false"),
					Global::get().bScreenShareEnabled ? QStringLiteral("true") : QStringLiteral("false"),
					Global::get().bScreenShareHelperRequired ? QStringLiteral("true") : QStringLiteral("false"),
					capabilities.captureSupported ? QStringLiteral("true") : QStringLiteral("false"),
					capabilities.viewSupported ? QStringLiteral("true") : QStringLiteral("false"),
					capabilities.helperAvailable ? QStringLiteral("true") : QStringLiteral("false"),
					Global::get().qsScreenShareRelayUrl.isEmpty() ? QStringLiteral("-")
																  : Global::get().qsScreenShareRelayUrl,
					relayTransports.join(QStringLiteral(",")),
					supportsInAppRelayTransport(
						Mumble::ScreenShare::relayTransportFromUrl(Global::get().qsScreenShareRelayUrl))
						? QStringLiteral("true")
						: QStringLiteral("false"),
					capabilities.captureBackend.isEmpty() ? QStringLiteral("-") : capabilities.captureBackend,
					capabilities.hardwareEncodeSupported ? QStringLiteral("true") : QStringLiteral("false"),
					capabilities.zeroCopySupported ? QStringLiteral("true") : QStringLiteral("false"),
					capabilities.roiSupported ? QStringLiteral("true") : QStringLiteral("false"),
					reason.isEmpty() ? QStringLiteral("available") : reason);
}

bool ScreenShareManager::canRequestLocalShare() const {
	return localShareUnavailableReason().isEmpty();
}

QString ScreenShareManager::localShareUnavailableReason() const {
	if (!Global::get().sh) {
		return tr("Connect to a server before starting screen sharing.");
	}

	const ScreenShareHelperClient::CapabilitySnapshot &capabilities = m_helperClient->capabilities();
	const MumbleProto::ScreenShareRelayTransport relayTransport =
		Mumble::ScreenShare::relayTransportFromUrl(Global::get().qsScreenShareRelayUrl);
	if (!Global::get().bScreenShareEnabled) {
		return tr("Screen sharing is disabled on this server.");
	}
	if (!Mumble::ScreenShare::isValidRelayUrl(Global::get().qsScreenShareRelayUrl)) {
		return tr("This server has no valid screen-share relay endpoint configured.");
	}
	if (!supportsInAppRelayTransport(relayTransport) && !capabilities.captureSupported) {
		return tr("No supported local capture source is available.");
	}
	if (!supportsInAppRelayTransport(relayTransport) && !capabilities.runtimeRelayTransports.isEmpty()
		&& !capabilities.runtimeRelayTransports.contains(static_cast< int >(relayTransport))) {
		return tr("The local screen-share helper does not support this server's relay transport.");
	}
	if (!supportsInAppRelayTransport(relayTransport) && Global::get().bScreenShareHelperRequired
		&& !capabilities.helperAvailable) {
		return tr("The local screen-share helper is required but unavailable.");
	}

	return QString();
}

bool ScreenShareManager::canViewSession(const QString &streamID) const {
	const auto it = m_sessions.constFind(streamID);
	return it != m_sessions.cend() && canViewSession(it.value());
}

bool ScreenShareManager::isPublishingSession(const QString &streamID) const {
	return m_activePublishSessions.contains(streamID);
}

bool ScreenShareManager::isViewingSession(const QString &streamID) const {
	return m_activeViewSessions.contains(streamID);
}

bool ScreenShareManager::hasDetachedWindow(const QString &streamID) const {
#if defined(MUMBLE_HAS_MODERN_LAYOUT)
	if (m_inAppPublishWindows.contains(streamID) || m_inAppViewWindows.contains(streamID)) {
		return true;
	}
#endif

	return m_activePublishSessions.contains(streamID) || m_activeViewSessions.contains(streamID);
}

bool ScreenShareManager::focusOrReopenDetachedWindow(const QString &streamID) {
#if defined(MUMBLE_HAS_MODERN_LAYOUT)
	auto focusHost = [](RelayWindowHost *host) {
		if (!host) {
			return false;
		}

		if (host->isMinimized()) {
			host->showNormal();
		} else {
			host->show();
		}
		host->raise();
		host->activateWindow();
		return true;
	};

	if (focusHost(m_inAppPublishWindows.value(streamID, nullptr))
		|| focusHost(m_inAppViewWindows.value(streamID, nullptr))) {
		return true;
	}
#endif

	const auto it = m_sessions.constFind(streamID);
	if (it == m_sessions.cend()) {
		return false;
	}

	QString errorMessage;
	bool reopened = false;
	if (m_activePublishSessions.contains(streamID)) {
		reopened = m_helperClient->startPublish(it.value(), &errorMessage);
	} else if (m_activeViewSessions.contains(streamID)) {
		reopened = m_helperClient->startView(it.value(), &errorMessage);
	}

	if (reopened) {
		if (Global::get().l) {
			Global::get().l->log(
				Log::Information,
				tr("Reopened the helper/browser screen-share window for %1.").arg(streamID.toHtmlEscaped()));
		}
		return true;
	}

	if (!errorMessage.isEmpty() && Global::get().l) {
		Global::get().l->log(Log::Warning, tr("Unable to reopen the screen-share window for %1: %2")
											   .arg(streamID.toHtmlEscaped(), errorMessage.toHtmlEscaped()));
	}

	return false;
}

bool ScreenShareManager::isUsingFallbackRuntime(const QString &streamID) const {
	if (m_activePublishSessions.contains(streamID)) {
#if defined(MUMBLE_HAS_MODERN_LAYOUT)
		return !m_inAppPublishSessionIDs.contains(streamID);
#else
		return true;
#endif
	}

	if (m_activeViewSessions.contains(streamID)) {
#if defined(MUMBLE_HAS_MODERN_LAYOUT)
		return !m_inAppViewSessionIDs.contains(streamID);
#else
		return true;
#endif
	}

	return false;
}

void ScreenShareManager::requestStartChannelShare(unsigned int channelID) {
	if (!Global::get().sh) {
		return;
	}
	logLocalShareAvailabilityDiagnostic(QStringLiteral("request-start"));
	if (!Mumble::ScreenShare::isValidRelayUrl(Global::get().qsScreenShareRelayUrl)) {
		if (Global::get().l) {
			Global::get().l->log(
				Log::Warning,
				tr("Screen sharing is unavailable because the server has no valid relay endpoint configured."));
		}
		return;
	}
	if (!canRequestLocalShare()) {
		return;
	}

	const MumbleProto::ScreenShareRelayTransport relayTransport =
		Mumble::ScreenShare::relayTransportFromUrl(Global::get().qsScreenShareRelayUrl);
	const ScreenShareHelperClient::CapabilitySnapshot &capabilities = m_helperClient->capabilities();
	auto clampRequestedLimit = [](const unsigned int requested, const unsigned int capabilityLimit) {
		if (requested > 0 && capabilityLimit > 0) {
			return std::min(requested, capabilityLimit);
		}

		return requested > 0 ? requested : capabilityLimit;
	};
	if (channelID == 0) {
		const ClientUser *self = ClientUser::get(Global::get().uiSession);
		if (self && self->cChannel) {
			channelID = self->cChannel->iId;
		}
	}

	MumbleProto::ScreenShareCreate msg;
	msg.set_scope(MumbleProto::ScreenShareScopeChannel);
	if (channelID != 0) {
		msg.set_scope_id(channelID);
	}
	const QList< int > availableCodecs = capabilities.supportedCodecs.isEmpty()
											 ? Mumble::ScreenShare::defaultCodecPreferenceList()
											 : capabilities.supportedCodecs;
	const QList< int > preferredCodecs = Global::get().qlPreferredScreenShareCodecs.isEmpty()
											 ? availableCodecs
											 : Global::get().qlPreferredScreenShareCodecs;
	const MumbleProto::ScreenShareCodec codec =
		Mumble::ScreenShare::selectPreferredCodec(preferredCodecs, availableCodecs);
	for (const int availableCodec : availableCodecs) {
		msg.add_requested_codecs(static_cast< MumbleProto::ScreenShareCodec >(availableCodec));
	}

	unsigned int targetWidth  = clampRequestedLimit(Global::get().uiScreenShareMaxWidth, capabilities.maxWidth);
	unsigned int targetHeight = clampRequestedLimit(Global::get().uiScreenShareMaxHeight, capabilities.maxHeight);
	unsigned int targetFps    = clampRequestedLimit(Global::get().uiScreenShareMaxFps, capabilities.maxFps);
	if (supportsInAppRelayTransport(relayTransport)) {
		targetWidth = std::min(Mumble::ScreenShare::sanitizeLimit(targetWidth, Mumble::ScreenShare::DEFAULT_MAX_WIDTH,
																  Mumble::ScreenShare::HARD_MAX_WIDTH),
							   Mumble::ScreenShare::DEFAULT_MAX_WIDTH);
		targetHeight =
			std::min(Mumble::ScreenShare::sanitizeLimit(targetHeight, Mumble::ScreenShare::DEFAULT_MAX_HEIGHT,
														Mumble::ScreenShare::HARD_MAX_HEIGHT),
					 Mumble::ScreenShare::DEFAULT_MAX_HEIGHT);
		targetFps = std::min(Mumble::ScreenShare::sanitizeLimit(targetFps, Mumble::ScreenShare::DEFAULT_MAX_FPS,
																Mumble::ScreenShare::HARD_MAX_FPS),
							 Mumble::ScreenShare::DEFAULT_MAX_FPS);
	}

	msg.set_requested_width(targetWidth);
	msg.set_requested_height(targetHeight);
	msg.set_requested_fps(targetFps);
	msg.set_requested_bitrate_kbps(
		Mumble::ScreenShare::defaultBitrateKbps(codec, targetWidth, targetHeight, targetFps));
	msg.set_prefer_hardware_encoding(true);

	Global::get().sh->sendMessage(msg);
}

void ScreenShareManager::requestStartViewing(const QString &streamID) {
	const auto it = m_sessions.constFind(streamID);
	if (it == m_sessions.cend()) {
		return;
	}

	if (!canViewSession(it.value())) {
		if (Global::get().l) {
			Global::get().l->log(
				Log::Warning,
				tr("Screen share %1 is not viewable on this client right now.").arg(streamID.toHtmlEscaped()));
		}
		return;
	}

	startLocalViewSession(it.value());
	emit sessionUpdated(streamID);
}

void ScreenShareManager::requestStopViewing(const QString &streamID) {
	const bool knownSession = m_sessions.contains(streamID);
	stopLocalViewSession(streamID);
	if (knownSession) {
		emit sessionUpdated(streamID);
	}
}

void ScreenShareManager::requestStopShare(const QString &streamID) {
	if (!Global::get().sh || streamID.isEmpty()) {
		return;
	}

	MumbleProto::ScreenShareStop msg;
	msg.set_stream_id(u8(streamID));
	Global::get().sh->sendMessage(msg);
}

const QHash< QString, ScreenShareSession > &ScreenShareManager::sessions() const {
	return m_sessions;
}

bool ScreenShareManager::hasSession(const QString &streamID) const {
	return m_sessions.contains(streamID);
}

void ScreenShareManager::resetState() {
	for (const QString &streamID : m_activePublishSessions) {
		m_helperClient->stopPublish(streamID);
	}
	for (const QString &streamID : m_activeViewSessions) {
		m_helperClient->stopView(streamID);
	}

#if defined(MUMBLE_HAS_MODERN_LAYOUT)
	for (RelayWindowHost *host : m_inAppPublishWindows.values()) {
		if (host) {
			host->closeFromManager();
			host->deleteLater();
		}
	}
	for (RelayWindowHost *host : m_inAppViewWindows.values()) {
		if (host) {
			host->closeFromManager();
			host->deleteLater();
		}
	}
	m_inAppPublishWindows.clear();
	m_inAppViewWindows.clear();
	m_inAppPublishSessionIDs.clear();
	m_inAppViewSessionIDs.clear();
#endif

	m_activePublishSessions.clear();
	m_activeViewSessions.clear();
	m_announcedViewableSessions.clear();
	m_sessions.clear();
	m_lastLoggedAvailabilityContext.clear();
	m_lastLoggedAvailabilityReason.clear();
}

ScreenShareSession ScreenShareManager::sessionFromState(const MumbleProto::ScreenShareState &msg) const {
	ScreenShareSession session;
	if (msg.has_stream_id()) {
		session.streamID = u8(msg.stream_id());
	}
	if (msg.has_owner_session()) {
		session.ownerSession = msg.owner_session();
	}
	if (msg.has_scope()) {
		session.scope = msg.scope();
	}
	if (msg.has_scope_id()) {
		session.scopeID = msg.scope_id();
	}
	if (msg.has_relay_url()) {
		session.relayUrl = u8(msg.relay_url());
	}
	if (msg.has_relay_room_id()) {
		session.relayRoomID = u8(msg.relay_room_id());
	}
	if (msg.has_relay_token()) {
		session.relayToken = u8(msg.relay_token());
	}
	if (msg.has_relay_session_id()) {
		session.relaySessionID = u8(msg.relay_session_id());
	}
	if (msg.has_relay_transport()) {
		session.relayTransport = msg.relay_transport();
	}
	if (msg.has_relay_role()) {
		session.relayRole = msg.relay_role();
	}
	if (msg.has_relay_token_expires_at()) {
		session.relayTokenExpiresAt = msg.relay_token_expires_at();
	}
	if (session.relayTransport == MumbleProto::ScreenShareRelayTransportUnknown
		&& Mumble::ScreenShare::isValidRelayUrl(session.relayUrl)) {
		session.relayTransport = Mumble::ScreenShare::relayTransportFromUrl(session.relayUrl);
	}
	if (!msg.has_relay_role()) {
		session.relayRole = session.ownerSession == Global::get().uiSession ? MumbleProto::ScreenShareRelayRolePublisher
																			: MumbleProto::ScreenShareRelayRoleViewer;
	}
	if (msg.has_created_at()) {
		session.createdAt = msg.created_at();
	}
	if (msg.has_state()) {
		session.state = msg.state();
	}
	if (msg.has_codec()) {
		session.codec = msg.codec();
	}
	session.codecFallbackOrder = codecFallbackOrderFromState(msg);
	if (msg.has_width()) {
		session.width = msg.width();
	}
	if (msg.has_height()) {
		session.height = msg.height();
	}
	if (msg.has_fps()) {
		session.fps = msg.fps();
	}
	if (msg.has_bitrate_kbps()) {
		session.bitrateKbps = msg.bitrate_kbps();
	}

	return session;
}

bool ScreenShareManager::canPublishSession(const ScreenShareSession &session) const {
	const ScreenShareHelperClient::CapabilitySnapshot &capabilities = m_helperClient->capabilities();
	const bool inAppRelayAvailable = supportsInAppRelayTransport(session.relayTransport);
	if (!Global::get().bScreenShareEnabled || !Global::get().sh) {
		return false;
	}
	if (session.ownerSession != Global::get().uiSession
		|| session.state != MumbleProto::ScreenShareLifecycleStateActive) {
		return false;
	}
	if (!Mumble::ScreenShare::isValidRelayUrl(session.relayUrl) || session.relayRoomID.trimmed().isEmpty()) {
		return false;
	}
	if (!session.relayToken.isEmpty() && tokenExpired(session.relayTokenExpiresAt)) {
		return false;
	}
	if (Mumble::ScreenShare::relayTransportRequiresSignaling(session.relayTransport)
		&& (session.relaySessionID.trimmed().isEmpty() || session.relayToken.trimmed().isEmpty()
			|| session.relayRole != MumbleProto::ScreenShareRelayRolePublisher)) {
		return false;
	}
	if (!Mumble::ScreenShare::isValidCodec(session.codec)) {
		return false;
	}
	if (inAppRelayAvailable) {
		return true;
	}
	if (!capabilities.captureSupported) {
		return false;
	}
	if (Global::get().bScreenShareHelperRequired && !capabilities.helperAvailable) {
		return false;
	}
	if (!capabilities.runtimeRelayTransports.isEmpty()
		&& !capabilities.runtimeRelayTransports.contains(static_cast< int >(session.relayTransport))) {
		return false;
	}
	return capabilities.supportedCodecs.isEmpty()
		   || capabilities.supportedCodecs.contains(static_cast< int >(session.codec));
}

bool ScreenShareManager::canViewSession(const ScreenShareSession &session) const {
	const ScreenShareHelperClient::CapabilitySnapshot &capabilities = m_helperClient->capabilities();
	const bool inAppRelayAvailable = supportsInAppRelayTransport(session.relayTransport);
	if (!Global::get().bScreenShareEnabled || session.ownerSession == Global::get().uiSession
		|| session.state != MumbleProto::ScreenShareLifecycleStateActive) {
		return false;
	}
	if (!Mumble::ScreenShare::isValidRelayUrl(session.relayUrl) || session.relayRoomID.trimmed().isEmpty()) {
		return false;
	}
	if (!session.relayToken.isEmpty() && tokenExpired(session.relayTokenExpiresAt)) {
		return false;
	}
	if (Mumble::ScreenShare::relayTransportRequiresSignaling(session.relayTransport)
		&& (session.relaySessionID.trimmed().isEmpty() || session.relayToken.trimmed().isEmpty()
			|| session.relayRole != MumbleProto::ScreenShareRelayRoleViewer)) {
		return false;
	}
	if (!Mumble::ScreenShare::isValidCodec(session.codec)) {
		return false;
	}
	if (inAppRelayAvailable) {
		const ClientUser *self = ClientUser::get(Global::get().uiSession);
		if (!self || !self->cChannel) {
			return false;
		}

		return session.scope == MumbleProto::ScreenShareScopeChannel && session.scopeID != 0
			   && self->cChannel->iId == session.scopeID;
	}
	if (!capabilities.viewSupported) {
		return false;
	}
	if (Global::get().bScreenShareHelperRequired && !capabilities.helperAvailable) {
		return false;
	}
	if (!capabilities.runtimeRelayTransports.isEmpty()
		&& !capabilities.runtimeRelayTransports.contains(static_cast< int >(session.relayTransport))) {
		return false;
	}
	if (!capabilities.supportedCodecs.isEmpty()
		&& !capabilities.supportedCodecs.contains(static_cast< int >(session.codec))) {
		return false;
	}

	const ClientUser *self = ClientUser::get(Global::get().uiSession);
	if (!self || !self->cChannel) {
		return false;
	}

	return session.scope == MumbleProto::ScreenShareScopeChannel && session.scopeID != 0
		   && self->cChannel->iId == session.scopeID;
}

bool ScreenShareManager::shouldAutoViewSession(const ScreenShareSession &session) const {
	Q_UNUSED(session);

	if (const std::optional< bool > override = envFlagOverride("MUMBLE_SCREENSHARE_AUTOVIEW"); override.has_value()) {
		return override.value();
	}
	if (const std::optional< bool > override = envFlagOverride("MUMBLE_SCREENSHARE_AUTO_VIEW"); override.has_value()) {
		return override.value();
	}

	return Global::get().s.bScreenShareAutoOpenCurrentRoom;
}

#if defined(MUMBLE_HAS_MODERN_LAYOUT)
bool ScreenShareManager::startInAppPublishSession(const ScreenShareSession &session) {
	if (m_inAppPublishWindows.contains(session.streamID)) {
		if (RelayWindowHost *existingHost = m_inAppPublishWindows.value(session.streamID)) {
			existingHost->updateSession(session);
		}
		return true;
	}

	auto *host = new RelayWindowHost(session, RelayWindowHost::Mode::Publish, qobject_cast< QWidget * >(parent()));
	connect(host, &RelayWindowHost::fallbackRequested, this,
			[this, streamID = session.streamID](const QString &reason) {
				handleInAppRelayFailure(streamID, true, reason);
			});
	connect(host, &RelayWindowHost::closeRequested, this, [this, streamID = session.streamID](const QString &) {
		stopLocalPublishSession(streamID);
		requestStopShare(streamID);
	});
	connect(host, &QObject::destroyed, this,
			[this, streamID = session.streamID]() { m_inAppPublishWindows.remove(streamID); });

	m_inAppPublishWindows.insert(session.streamID, host);
	m_inAppPublishSessionIDs.insert(session.streamID);
	QString errorMessage;
	if (!host->start(&errorMessage)) {
		m_inAppPublishWindows.remove(session.streamID);
		m_inAppPublishSessionIDs.remove(session.streamID);
		host->deleteLater();
		if (Global::get().l) {
			Global::get().l->log(Log::Warning,
								 tr("Unable to start the in-app screen-share publisher for %1: %2")
									 .arg(session.streamID.toHtmlEscaped(),
										  errorMessage.isEmpty() ? tr("unknown error") : errorMessage.toHtmlEscaped()));
		}
		return false;
	}

	return true;
}

bool ScreenShareManager::startInAppViewSession(const ScreenShareSession &session) {
	if (m_inAppViewWindows.contains(session.streamID)) {
		if (RelayWindowHost *existingHost = m_inAppViewWindows.value(session.streamID)) {
			existingHost->updateSession(session);
		}
		return true;
	}

	auto *host = new RelayWindowHost(session, RelayWindowHost::Mode::View, qobject_cast< QWidget * >(parent()));
	connect(host, &RelayWindowHost::fallbackRequested, this,
			[this, streamID = session.streamID](const QString &reason) {
				handleInAppRelayFailure(streamID, false, reason);
			});
	connect(host, &RelayWindowHost::closeRequested, this,
			[this, streamID = session.streamID](const QString &) { requestStopViewing(streamID); });
	connect(host, &QObject::destroyed, this,
			[this, streamID = session.streamID]() { m_inAppViewWindows.remove(streamID); });

	m_inAppViewWindows.insert(session.streamID, host);
	m_inAppViewSessionIDs.insert(session.streamID);
	QString errorMessage;
	if (!host->start(&errorMessage)) {
		m_inAppViewWindows.remove(session.streamID);
		m_inAppViewSessionIDs.remove(session.streamID);
		host->deleteLater();
		if (Global::get().l) {
			Global::get().l->log(Log::Warning,
								 tr("Unable to start the in-app screen-share viewer for %1: %2")
									 .arg(session.streamID.toHtmlEscaped(),
										  errorMessage.isEmpty() ? tr("unknown error") : errorMessage.toHtmlEscaped()));
		}
		return false;
	}

	return true;
}

void ScreenShareManager::stopInAppPublishSession(const QString &streamID) {
	m_inAppPublishSessionIDs.remove(streamID);
	m_activePublishSessions.remove(streamID);
	const auto it = m_inAppPublishWindows.find(streamID);
	if (it == m_inAppPublishWindows.end()) {
		return;
	}

	RelayWindowHost *host = it.value();
	m_inAppPublishWindows.erase(it);
	if (host) {
		host->closeFromManager();
		host->deleteLater();
	}
}

void ScreenShareManager::stopInAppViewSession(const QString &streamID) {
	m_inAppViewSessionIDs.remove(streamID);
	m_activeViewSessions.remove(streamID);
	const auto it = m_inAppViewWindows.find(streamID);
	if (it == m_inAppViewWindows.end()) {
		return;
	}

	RelayWindowHost *host = it.value();
	m_inAppViewWindows.erase(it);
	if (host) {
		host->closeFromManager();
		host->deleteLater();
	}
}

void ScreenShareManager::handleInAppRelayFailure(const QString &streamID, const bool publish, const QString &reason) {
	const auto sessionIt = m_sessions.constFind(streamID);
	if (sessionIt == m_sessions.cend()) {
		if (Global::get().l) {
			Global::get().l->log(Log::Warning,
								 tr("In-app screen-share %1 fallback requested for unknown session %2.")
									 .arg(publish ? tr("publisher") : tr("viewer"), streamID.toHtmlEscaped()));
		}
		if (publish) {
			requestStopShare(streamID);
		} else {
			requestStopViewing(streamID);
		}
		return;
	}

	const ScreenShareSession session = sessionIt.value();
	const QString trimmedReason      = reason.trimmed();
	if (Global::get().l && !trimmedReason.isEmpty()) {
		Global::get().l->log(Log::Warning, tr("In-app screen-share %1 for %2 failed: %3")
											   .arg(publish ? tr("publisher") : tr("viewer"),
													session.streamID.toHtmlEscaped(), trimmedReason.toHtmlEscaped()));
	}

	if (publish) {
		stopInAppPublishSession(streamID);
	} else {
		stopInAppViewSession(streamID);
	}

	QString errorMessage;
	const bool started = publish ? m_helperClient->startPublish(session, &errorMessage)
								 : m_helperClient->startView(session, &errorMessage);
	if (started) {
		m_inAppPublishSessionIDs.remove(streamID);
		m_inAppViewSessionIDs.remove(streamID);
		if (publish) {
			m_activePublishSessions.insert(streamID);
		} else {
			m_activeViewSessions.insert(streamID);
		}
		if (Global::get().l) {
			Global::get().l->log(
				Log::Information,
				tr("Fell back to the helper/browser relay runtime for %1.").arg(streamID.toHtmlEscaped()));
		}
		return;
	}

	if (Global::get().l) {
		Global::get().l->log(Log::Warning,
							 tr("Unable to start the helper/browser relay runtime for %1: %2")
								 .arg(streamID.toHtmlEscaped(),
									  errorMessage.isEmpty() ? tr("unknown error") : errorMessage.toHtmlEscaped()));
	}
	if (publish) {
		requestStopShare(streamID);
	} else {
		requestStopViewing(streamID);
	}
}
#endif

void ScreenShareManager::startLocalPublishSession(const ScreenShareSession &session) {
	if (m_activePublishSessions.contains(session.streamID)) {
		return;
	}

#if defined(MUMBLE_HAS_MODERN_LAYOUT)
	const bool canUseInAppRelay = supportsInAppRelayTransport(session.relayTransport);
	const bool preferInAppRelay = Global::get().s.bScreenSharePreferInAppRelay;
	if (canUseInAppRelay && preferInAppRelay && startInAppPublishSession(session)) {
		m_activePublishSessions.insert(session.streamID);
		if (Global::get().l) {
			Global::get().l->log(
				Log::Information,
				tr("Opened in-app screen-share publisher for %1.").arg(session.streamID.toHtmlEscaped()));
		}
		return;
	}
#endif

	QString errorMessage;
	if (m_helperClient->startPublish(session, &errorMessage)) {
		m_activePublishSessions.insert(session.streamID);
		if (Global::get().l) {
			Global::get().l->log(
				Log::Information,
				tr("Using the helper/browser screen-share runtime for %1.").arg(session.streamID.toHtmlEscaped()));
		}
		return;
	}

#if defined(MUMBLE_HAS_MODERN_LAYOUT)
	if (canUseInAppRelay && !preferInAppRelay && startInAppPublishSession(session)) {
		m_activePublishSessions.insert(session.streamID);
		if (Global::get().l) {
			Global::get().l->log(
				Log::Information,
				tr("Opened in-app screen-share publisher for %1 after helper/browser startup was unavailable.")
					.arg(session.streamID.toHtmlEscaped()));
		}
		return;
	}
#endif

	if (Global::get().l) {
		Global::get().l->log(Log::Warning, tr("Unable to start local screen-share helper for %1: %2")
											   .arg(session.streamID.toHtmlEscaped(), errorMessage.toHtmlEscaped()));
	}
	requestStopShare(session.streamID);
}

void ScreenShareManager::startLocalViewSession(const ScreenShareSession &session) {
	if (m_activeViewSessions.contains(session.streamID)) {
		return;
	}

#if defined(MUMBLE_HAS_MODERN_LAYOUT)
	const bool canUseInAppRelay = supportsInAppRelayTransport(session.relayTransport);
	const bool preferInAppRelay = Global::get().s.bScreenSharePreferInAppRelay;
	if (canUseInAppRelay && preferInAppRelay && startInAppViewSession(session)) {
		m_activeViewSessions.insert(session.streamID);
		if (Global::get().l) {
			Global::get().l->log(Log::Information,
								 tr("Opened in-app screen-share viewer for %1.").arg(session.streamID.toHtmlEscaped()));
		}
		return;
	}
#endif

	QString errorMessage;
	if (m_helperClient->startView(session, &errorMessage)) {
		m_activeViewSessions.insert(session.streamID);
		if (Global::get().l) {
			Global::get().l->log(
				Log::Information,
				tr("Using the helper/browser screen-share runtime for %1.").arg(session.streamID.toHtmlEscaped()));
		}
		return;
	}

#if defined(MUMBLE_HAS_MODERN_LAYOUT)
	if (canUseInAppRelay && !preferInAppRelay && startInAppViewSession(session)) {
		m_activeViewSessions.insert(session.streamID);
		if (Global::get().l) {
			Global::get().l->log(
				Log::Information,
				tr("Opened in-app screen-share viewer for %1 after helper/browser startup was unavailable.")
					.arg(session.streamID.toHtmlEscaped()));
		}
		return;
	}
#endif

	if (Global::get().l) {
		Global::get().l->log(Log::Warning, tr("Unable to start screen-share viewer for %1: %2")
											   .arg(session.streamID.toHtmlEscaped(), errorMessage.toHtmlEscaped()));
	}
}

void ScreenShareManager::stopLocalPublishSession(const QString &streamID) {
#if defined(MUMBLE_HAS_MODERN_LAYOUT)
	if (m_inAppPublishSessionIDs.contains(streamID)) {
		stopInAppPublishSession(streamID);
		return;
	}
#endif

	if (m_activePublishSessions.remove(streamID)) {
		m_helperClient->stopPublish(streamID);
	}
}

void ScreenShareManager::stopLocalViewSession(const QString &streamID) {
#if defined(MUMBLE_HAS_MODERN_LAYOUT)
	if (m_inAppViewSessionIDs.contains(streamID)) {
		stopInAppViewSession(streamID);
		return;
	}
#endif

	if (m_activeViewSessions.remove(streamID)) {
		m_helperClient->stopView(streamID);
	}
}

void ScreenShareManager::logRemoteViewAvailability(const ScreenShareSession &session) {
	if (m_announcedViewableSessions.contains(session.streamID) || !Global::get().l) {
		return;
	}

	m_announcedViewableSessions.insert(session.streamID);
	const ClientUser *owner  = ClientUser::get(session.ownerSession);
	const QString ownerLabel = owner ? owner->qsName.toHtmlEscaped()
									 : tr("session %1").arg(QString::number(session.ownerSession).toHtmlEscaped());
	Global::get().l->log(Log::Information,
						 tr("Screen share %1 from %2 is available in this channel. Enable auto-open in Settings > "
							"Screen Sharing or set MUMBLE_SCREENSHARE_AUTOVIEW=1 to open it automatically.")
							 .arg(session.streamID.toHtmlEscaped(), ownerLabel));
}

void ScreenShareManager::handleScreenShareState(const MumbleProto::ScreenShareState &msg) {
	if (!msg.has_stream_id()) {
		return;
	}

	const ScreenShareSession session = sessionFromState(msg);
	m_sessions.insert(session.streamID, session);

	if (canPublishSession(session)) {
		startLocalPublishSession(session);
	} else {
		stopLocalPublishSession(session.streamID);
	}

	if (canViewSession(session)) {
		if (m_activeViewSessions.contains(session.streamID)) {
			m_announcedViewableSessions.remove(session.streamID);
		} else if (shouldAutoViewSession(session)) {
			startLocalViewSession(session);
			m_announcedViewableSessions.remove(session.streamID);
		} else {
			logRemoteViewAvailability(session);
		}
	} else {
		stopLocalViewSession(session.streamID);
		m_announcedViewableSessions.remove(session.streamID);
	}

	emit sessionUpdated(session.streamID);
}

void ScreenShareManager::handleScreenShareOffer(const MumbleProto::ScreenShareOffer &) {
}

void ScreenShareManager::handleScreenShareAnswer(const MumbleProto::ScreenShareAnswer &) {
}

void ScreenShareManager::handleScreenShareIceCandidate(const MumbleProto::ScreenShareIceCandidate &) {
}

void ScreenShareManager::handleScreenShareStop(const MumbleProto::ScreenShareStop &msg) {
	if (!msg.has_stream_id()) {
		return;
	}

	const QString streamID = u8(msg.stream_id());
	const QString reason   = msg.has_reason() ? u8(msg.reason()) : QString();
	m_announcedViewableSessions.remove(streamID);
	stopLocalHelperSessions(streamID);
	if (!m_sessions.contains(streamID)) {
		if (!reason.isEmpty() && Global::get().l) {
			Global::get().l->log(Log::Information,
								 tr("Screen share %1 ended: %2").arg(streamID.toHtmlEscaped(), reason.toHtmlEscaped()));
		}
		return;
	}

	m_sessions.remove(streamID);
	if (!reason.isEmpty() && Global::get().l) {
		Global::get().l->log(Log::Information,
							 tr("Screen share %1 ended: %2").arg(streamID.toHtmlEscaped(), reason.toHtmlEscaped()));
	}
	emit sessionStopped(streamID);
}

void ScreenShareManager::stopLocalHelperSessions(const QString &streamID) {
	stopLocalPublishSession(streamID);
	stopLocalViewSession(streamID);
}
