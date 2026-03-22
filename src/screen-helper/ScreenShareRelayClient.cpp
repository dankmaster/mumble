// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareRelayClient.h"

#include "ScreenShare.h"
#include "ScreenShareExternalProcess.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>

namespace {
	quint64 parseUInt64(const QJsonValue &value) {
		if (value.isString()) {
			bool ok                = false;
			const quint64 parsed = value.toString().trimmed().toULongLong(&ok);
			return ok ? parsed : 0;
		}
		if (value.isDouble()) {
			const double parsed = value.toDouble();
			return parsed > 0.0 ? static_cast< quint64 >(parsed) : 0;
		}

		return 0;
	}

	QString sanitizeRoomToken(const QString &value) {
		QString sanitized = value.trimmed();
		sanitized.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("-"));
		sanitized.remove(QRegularExpression(QStringLiteral("^-+")));
		sanitized.remove(QRegularExpression(QStringLiteral("-+$")));
		return sanitized.isEmpty() ? QStringLiteral("screen-share") : sanitized;
	}

	MumbleProto::ScreenShareRelayRole relayRoleFromPayload(const QJsonObject &payload,
														   const MumbleProto::ScreenShareRelayRole fallback) {
		if (!payload.contains(QStringLiteral("relay_role"))) {
			return fallback;
		}

		switch (static_cast< MumbleProto::ScreenShareRelayRole >(payload.value(QStringLiteral("relay_role")).toInt())) {
			case MumbleProto::ScreenShareRelayRolePublisher:
			case MumbleProto::ScreenShareRelayRoleViewer:
				return static_cast< MumbleProto::ScreenShareRelayRole >(
					payload.value(QStringLiteral("relay_role")).toInt());
			default:
				return fallback;
		}
	}

	QJsonArray transportArray(const QList< MumbleProto::ScreenShareRelayTransport > &transports) {
		QJsonArray json;
		for (const MumbleProto::ScreenShareRelayTransport transport : transports) {
			json.push_back(static_cast< int >(transport));
		}
		return json;
	}

	QStringList supportedDirectSchemes(const ScreenShareExternalProcess::RuntimeSupport &support) {
		QStringList schemes;
		if (support.fileProtocolAvailable) {
			schemes << QStringLiteral("file");
		}
		if (support.rtmpProtocolAvailable) {
			schemes << QStringLiteral("rtmp");
		}
		if (support.rtmpsProtocolAvailable) {
			schemes << QStringLiteral("rtmps");
		}
		return schemes;
	}

	ScreenShareRelayClient::Contract buildContract(const QJsonObject &payload, const bool publish) {
		ScreenShareRelayClient::Contract contract;
		contract.publishSupported = true;
		contract.viewSupported    = true;
		contract.relayRole =
			relayRoleFromPayload(payload, publish ? MumbleProto::ScreenShareRelayRolePublisher
												 : MumbleProto::ScreenShareRelayRoleViewer);

		const QString relayUrl = Mumble::ScreenShare::normalizeRelayUrl(payload.value(QStringLiteral("relay_url")).toString());
		if (relayUrl.isEmpty()) {
			contract.errorMessage = QStringLiteral("Missing or invalid relay_url.");
			return contract;
		}

		contract.relayUrl            = relayUrl;
		contract.relayRoomID         = sanitizeRoomToken(payload.value(QStringLiteral("relay_room_id")).toString());
		contract.relayToken          = payload.value(QStringLiteral("relay_token")).toString().trimmed();
		contract.relaySessionID      = payload.value(QStringLiteral("relay_session_id")).toString().trimmed();
		contract.relayTokenExpiresAt = parseUInt64(payload.value(QStringLiteral("relay_token_expires_at")));
		contract.relayTransport      = Mumble::ScreenShare::relayTransportFromUrl(relayUrl);
		contract.requiresSignaling   = Mumble::ScreenShare::relayTransportRequiresSignaling(contract.relayTransport);
		contract.contractMode =
			contract.requiresSignaling ? QStringLiteral("webrtc-signaling-contract")
									   : QStringLiteral("direct-runtime");

		if (publish && contract.relayRole != MumbleProto::ScreenShareRelayRolePublisher) {
			contract.errorMessage = QStringLiteral("Publish sessions require publisher relay credentials.");
			return contract;
		}
		if (!publish && contract.relayRole != MumbleProto::ScreenShareRelayRoleViewer) {
			contract.errorMessage = QStringLiteral("Viewer sessions require viewer relay credentials.");
			return contract;
		}
		if (contract.relayTokenExpiresAt > 0
			&& contract.relayTokenExpiresAt <= static_cast< quint64 >(QDateTime::currentMSecsSinceEpoch())) {
			contract.errorMessage =
				QStringLiteral("Relay credentials for this screen-share session have already expired.");
			return contract;
		}

		if (contract.requiresSignaling) {
			if (contract.relaySessionID.isEmpty()) {
				contract.errorMessage =
					QStringLiteral("WebRTC relay sessions require relay_session_id metadata.");
				return contract;
			}
			if (contract.relayToken.isEmpty()) {
				contract.errorMessage =
					QStringLiteral("WebRTC relay sessions require a short-lived relay token.");
				return contract;
			}

			contract.valid             = true;
			contract.runtimeExecutable = false;
			contract.description =
				QStringLiteral("WebRTC relay session metadata is present and can be negotiated over Murmur signaling, but the helper does not yet ship an executable WebRTC transport backend.");
			contract.warnings << contract.description;
			return contract;
		}

		const ScreenShareExternalProcess::RuntimeSupport runtimeSupport =
			ScreenShareExternalProcess::probeRuntimeSupport();
		const QString scheme = QUrl(relayUrl).scheme().trimmed().toLower();
		if (scheme == QLatin1String("file") && !runtimeSupport.fileProtocolAvailable) {
			contract.errorMessage = QStringLiteral("ffmpeg on this host has no file protocol support.");
			return contract;
		}
		if (scheme == QLatin1String("rtmp") && !runtimeSupport.rtmpProtocolAvailable) {
			contract.errorMessage = QStringLiteral("ffmpeg on this host has no RTMP transport support.");
			return contract;
		}
		if (scheme == QLatin1String("rtmps") && !runtimeSupport.rtmpsProtocolAvailable) {
			contract.errorMessage = QStringLiteral("ffmpeg on this host has no RTMPS transport support.");
			return contract;
		}

		contract.valid             = true;
		contract.runtimeExecutable = true;
		contract.description =
			QStringLiteral("Direct relay transport is executable in the current helper runtime.");
		return contract;
	}
} // namespace

ScreenShareRelayClient::Contract ScreenShareRelayClient::contractForPublish(const QJsonObject &payload) {
	return buildContract(payload, true);
}

ScreenShareRelayClient::Contract ScreenShareRelayClient::contractForView(const QJsonObject &payload) {
	return buildContract(payload, false);
}

QJsonArray ScreenShareRelayClient::advertisedContracts() {
	const ScreenShareExternalProcess::RuntimeSupport runtimeSupport =
		ScreenShareExternalProcess::probeRuntimeSupport();
	const QStringList directSchemes = supportedDirectSchemes(runtimeSupport);

	QJsonArray contracts;

	QJsonObject direct;
	direct.insert(QStringLiteral("transport"),
				  static_cast< int >(MumbleProto::ScreenShareRelayTransportDirect));
	direct.insert(QStringLiteral("transport_token"),
				  Mumble::ScreenShare::relayTransportToConfigToken(MumbleProto::ScreenShareRelayTransportDirect));
	direct.insert(QStringLiteral("runtime_executable"), !directSchemes.isEmpty());
	direct.insert(QStringLiteral("requires_signaling"), false);
	direct.insert(QStringLiteral("publish_supported"), !directSchemes.isEmpty());
	direct.insert(QStringLiteral("view_supported"), !directSchemes.isEmpty());
	direct.insert(QStringLiteral("contract_mode"), QStringLiteral("direct-runtime"));
	direct.insert(QStringLiteral("description"),
				  directSchemes.isEmpty()
					  ? QStringLiteral("The helper understands direct relays, but this host has no executable direct relay protocols available in ffmpeg.")
					  : QStringLiteral("The helper can execute direct relays over the available ffmpeg protocols."));
	{
		QJsonArray schemes;
		for (const QString &scheme : directSchemes) {
			schemes.push_back(scheme);
		}
		direct.insert(QStringLiteral("schemes"), schemes);
	}
	contracts.push_back(direct);

	QJsonObject webrtc;
	webrtc.insert(QStringLiteral("transport"),
				  static_cast< int >(MumbleProto::ScreenShareRelayTransportWebRTC));
	webrtc.insert(QStringLiteral("transport_token"),
				  Mumble::ScreenShare::relayTransportToConfigToken(MumbleProto::ScreenShareRelayTransportWebRTC));
	webrtc.insert(QStringLiteral("runtime_executable"), false);
	webrtc.insert(QStringLiteral("requires_signaling"), true);
	webrtc.insert(QStringLiteral("publish_supported"), true);
	webrtc.insert(QStringLiteral("view_supported"), true);
	webrtc.insert(QStringLiteral("contract_mode"), QStringLiteral("webrtc-signaling-contract"));
	webrtc.insert(QStringLiteral("description"),
				  QStringLiteral("The helper understands WebRTC relay session metadata and Murmur signaling, but an executable WebRTC backend still needs to be added."));
	contracts.push_back(webrtc);

	return contracts;
}

QJsonArray ScreenShareRelayClient::runtimeRelayTransports() {
	const ScreenShareExternalProcess::RuntimeSupport runtimeSupport =
		ScreenShareExternalProcess::probeRuntimeSupport();
	QList< MumbleProto::ScreenShareRelayTransport > transports;
	if (!supportedDirectSchemes(runtimeSupport).isEmpty()) {
		transports.append(MumbleProto::ScreenShareRelayTransportDirect);
	}

	return transportArray(transports);
}

QJsonArray ScreenShareRelayClient::signalingRelayTransports() {
	return transportArray(QList< MumbleProto::ScreenShareRelayTransport >{
		MumbleProto::ScreenShareRelayTransportWebRTC });
}
