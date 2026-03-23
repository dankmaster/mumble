// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareManager.h"

#include "ClientUser.h"
#include "Global.h"
#include "Log.h"
#include "ProtoUtils.h"
#include "QtUtils.h"
#include "ScreenShare.h"
#include "ServerHandler.h"

#include <algorithm>

#include <QtCore/QDateTime>
#include <QtCore/QProcessEnvironment>

namespace {
	bool envFlagEnabled(const char *name) {
		const QString value = qEnvironmentVariable(name).trimmed().toLower();
		return value == QLatin1String("1") || value == QLatin1String("true") || value == QLatin1String("yes")
			|| value == QLatin1String("on");
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
} // namespace

ScreenShareManager::ScreenShareManager(QObject *parent) : QObject(parent) {
	m_helperClient = new ScreenShareHelperClient(this);
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

bool ScreenShareManager::canRequestLocalShare() const {
	const ScreenShareHelperClient::CapabilitySnapshot &capabilities = m_helperClient->capabilities();
	if (!Global::get().bScreenShareEnabled || !capabilities.captureSupported) {
		return false;
	}
	if (!Mumble::ScreenShare::isValidRelayUrl(Global::get().qsScreenShareRelayUrl)) {
		return false;
	}
	const MumbleProto::ScreenShareRelayTransport relayTransport =
		Mumble::ScreenShare::relayTransportFromUrl(Global::get().qsScreenShareRelayUrl);
	if (!capabilities.runtimeRelayTransports.isEmpty()
		&& !capabilities.runtimeRelayTransports.contains(static_cast< int >(relayTransport))) {
		return false;
	}
	if (Global::get().bScreenShareHelperRequired && !capabilities.helperAvailable) {
		return false;
	}

	return static_cast< bool >(Global::get().sh);
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

void ScreenShareManager::requestStartChannelShare(unsigned int channelID) {
	if (!Global::get().sh) {
		return;
	}
	if (!Mumble::ScreenShare::isValidRelayUrl(Global::get().qsScreenShareRelayUrl)) {
		if (Global::get().l) {
			Global::get().l->log(Log::Warning,
								 tr("Screen sharing is unavailable because the server has no valid relay endpoint configured."));
		}
		return;
	}
	if (!canRequestLocalShare()) {
		return;
	}

	const ScreenShareHelperClient::CapabilitySnapshot &capabilities = m_helperClient->capabilities();
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
	for (const int codec : capabilities.supportedCodecs) {
		msg.add_requested_codecs(static_cast< MumbleProto::ScreenShareCodec >(codec));
	}

	const unsigned int targetWidth = Global::get().uiScreenShareMaxWidth > 0
										 ? std::min(capabilities.maxWidth, Global::get().uiScreenShareMaxWidth)
										 : capabilities.maxWidth;
	const unsigned int targetHeight = Global::get().uiScreenShareMaxHeight > 0
										  ? std::min(capabilities.maxHeight, Global::get().uiScreenShareMaxHeight)
										  : capabilities.maxHeight;
	const unsigned int targetFps = Global::get().uiScreenShareMaxFps > 0
									   ? std::min(capabilities.maxFps, Global::get().uiScreenShareMaxFps)
									   : capabilities.maxFps;
	const QList< int > preferredCodecs = Global::get().qlPreferredScreenShareCodecs.isEmpty()
											 ? capabilities.supportedCodecs
											 : Global::get().qlPreferredScreenShareCodecs;
	const MumbleProto::ScreenShareCodec codec =
		Mumble::ScreenShare::selectPreferredCodec(preferredCodecs, capabilities.supportedCodecs);

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
			Global::get().l->log(Log::Warning,
								 tr("Screen share %1 is not viewable on this client right now.")
									 .arg(streamID.toHtmlEscaped()));
		}
		return;
	}

	startLocalViewSession(it.value());
}

void ScreenShareManager::requestStopViewing(const QString &streamID) {
	stopLocalViewSession(streamID);
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

	m_activePublishSessions.clear();
	m_activeViewSessions.clear();
	m_announcedViewableSessions.clear();
	m_sessions.clear();
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
	if (!Global::get().bScreenShareEnabled || !Global::get().sh) {
		return false;
	}
	if (session.ownerSession != Global::get().uiSession
		|| session.state != MumbleProto::ScreenShareLifecycleStateActive) {
		return false;
	}
	if (!capabilities.captureSupported) {
		return false;
	}
	if (Global::get().bScreenShareHelperRequired && !capabilities.helperAvailable) {
		return false;
	}
	if (!Mumble::ScreenShare::isValidRelayUrl(session.relayUrl) || session.relayRoomID.trimmed().isEmpty()) {
		return false;
	}
	if (!capabilities.runtimeRelayTransports.isEmpty()
		&& !capabilities.runtimeRelayTransports.contains(static_cast< int >(session.relayTransport))) {
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
	return capabilities.supportedCodecs.isEmpty()
		|| capabilities.supportedCodecs.contains(static_cast< int >(session.codec));
}

bool ScreenShareManager::canViewSession(const ScreenShareSession &session) const {
	const ScreenShareHelperClient::CapabilitySnapshot &capabilities = m_helperClient->capabilities();
	if (!Global::get().bScreenShareEnabled || session.ownerSession == Global::get().uiSession
		|| session.state != MumbleProto::ScreenShareLifecycleStateActive) {
		return false;
	}
	if (!capabilities.viewSupported) {
		return false;
	}
	if (Global::get().bScreenShareHelperRequired && !capabilities.helperAvailable) {
		return false;
	}
	if (!Mumble::ScreenShare::isValidRelayUrl(session.relayUrl) || session.relayRoomID.trimmed().isEmpty()) {
		return false;
	}
	if (!capabilities.runtimeRelayTransports.isEmpty()
		&& !capabilities.runtimeRelayTransports.contains(static_cast< int >(session.relayTransport))) {
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
	return envFlagEnabled("MUMBLE_SCREENSHARE_AUTOVIEW") || envFlagEnabled("MUMBLE_SCREENSHARE_AUTO_VIEW");
}

void ScreenShareManager::startLocalPublishSession(const ScreenShareSession &session) {
	if (m_activePublishSessions.contains(session.streamID)) {
		return;
	}

	QString errorMessage;
	if (m_helperClient->startPublish(session, &errorMessage)) {
		m_activePublishSessions.insert(session.streamID);
		if (Global::get().l) {
			Global::get().l->log(Log::Information,
								 tr("Prepared local screen-share helper session %1.")
									 .arg(session.streamID.toHtmlEscaped()));
		}
		return;
	}

	if (Global::get().l) {
		Global::get().l->log(Log::Warning,
							 tr("Unable to start local screen-share helper for %1: %2")
								 .arg(session.streamID.toHtmlEscaped(), errorMessage.toHtmlEscaped()));
	}
	requestStopShare(session.streamID);
}

void ScreenShareManager::startLocalViewSession(const ScreenShareSession &session) {
	if (m_activeViewSessions.contains(session.streamID)) {
		return;
	}

	QString errorMessage;
	if (m_helperClient->startView(session, &errorMessage)) {
		m_activeViewSessions.insert(session.streamID);
		if (Global::get().l) {
			Global::get().l->log(
				Log::Information,
				tr("Opened screen-share viewer for %1.").arg(session.streamID.toHtmlEscaped()));
		}
		return;
	}

	if (Global::get().l) {
		Global::get().l->log(Log::Warning,
							 tr("Unable to start screen-share viewer for %1: %2")
								 .arg(session.streamID.toHtmlEscaped(), errorMessage.toHtmlEscaped()));
	}
}

void ScreenShareManager::stopLocalPublishSession(const QString &streamID) {
	if (m_activePublishSessions.remove(streamID)) {
		m_helperClient->stopPublish(streamID);
	}
}

void ScreenShareManager::stopLocalViewSession(const QString &streamID) {
	if (m_activeViewSessions.remove(streamID)) {
		m_helperClient->stopView(streamID);
	}
}

void ScreenShareManager::logRemoteViewAvailability(const ScreenShareSession &session) {
	if (m_announcedViewableSessions.contains(session.streamID) || !Global::get().l) {
		return;
	}

	m_announcedViewableSessions.insert(session.streamID);
	const ClientUser *owner = ClientUser::get(session.ownerSession);
	const QString ownerLabel =
		owner ? owner->qsName.toHtmlEscaped()
			  : tr("session %1").arg(QString::number(session.ownerSession).toHtmlEscaped());
	Global::get().l->log(
		Log::Information,
		tr("Screen share %1 from %2 is available in this channel. Set MUMBLE_SCREENSHARE_AUTOVIEW=1 to auto-open it until the in-app watch UI lands.")
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
								 tr("Screen share %1 ended: %2")
									 .arg(streamID.toHtmlEscaped(), reason.toHtmlEscaped()));
		}
		return;
	}

	m_sessions.remove(streamID);
	if (!reason.isEmpty() && Global::get().l) {
		Global::get().l->log(Log::Information,
							 tr("Screen share %1 ended: %2")
								 .arg(streamID.toHtmlEscaped(), reason.toHtmlEscaped()));
	}
	emit sessionStopped(streamID);
}

void ScreenShareManager::stopLocalHelperSessions(const QString &streamID) {
	stopLocalPublishSession(streamID);
	stopLocalViewSession(streamID);
}
