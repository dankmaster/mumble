// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShare.h"

#include <algorithm>

#include <QDir>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

namespace Mumble {
namespace ScreenShare {

	bool isValidCodec(const MumbleProto::ScreenShareCodec codec) {
		switch (codec) {
			case MumbleProto::ScreenShareCodecH264:
			case MumbleProto::ScreenShareCodecVP8:
			case MumbleProto::ScreenShareCodecVP9:
			case MumbleProto::ScreenShareCodecAV1:
				return true;
			case MumbleProto::ScreenShareCodecUnknown:
			default:
				return false;
		}
	}

	QString codecToConfigToken(const MumbleProto::ScreenShareCodec codec) {
		switch (codec) {
			case MumbleProto::ScreenShareCodecH264:
				return QStringLiteral("h264");
			case MumbleProto::ScreenShareCodecVP8:
				return QStringLiteral("vp8");
			case MumbleProto::ScreenShareCodecVP9:
				return QStringLiteral("vp9");
			case MumbleProto::ScreenShareCodecAV1:
				return QStringLiteral("av1");
			case MumbleProto::ScreenShareCodecUnknown:
			default:
				return QString();
		}
	}

	QList< int > defaultCodecPreferenceList() {
		return { static_cast< int >(MumbleProto::ScreenShareCodecH264),
				 static_cast< int >(MumbleProto::ScreenShareCodecAV1),
				 static_cast< int >(MumbleProto::ScreenShareCodecVP9),
				 static_cast< int >(MumbleProto::ScreenShareCodecVP8) };
	}

	QList< int > webRtcRelayCodecPreferenceList() {
		return { static_cast< int >(MumbleProto::ScreenShareCodecVP8),
				 static_cast< int >(MumbleProto::ScreenShareCodecH264),
				 static_cast< int >(MumbleProto::ScreenShareCodecAV1),
				 static_cast< int >(MumbleProto::ScreenShareCodecVP9) };
	}

	QList< int > browserWebRtcCodecPreferenceList() {
		return { static_cast< int >(MumbleProto::ScreenShareCodecVP8) };
	}

	QList< int > sanitizeCodecList(const QList< int > &codecs) {
		QList< int > sanitized;
		for (const int codec : codecs) {
			const MumbleProto::ScreenShareCodec typedCodec = static_cast< MumbleProto::ScreenShareCodec >(codec);
			if (!isValidCodec(typedCodec) || sanitized.contains(codec)) {
				continue;
			}

			sanitized.append(codec);
		}

		return sanitized;
	}

	QList< int > parseCodecPreferenceString(const QString &codecList, const QList< int > &fallback) {
		QList< int > parsedCodecs;

		for (const QString &rawToken :
			 codecList.split(QRegularExpression(QLatin1String("[,\\s]+")), Qt::SkipEmptyParts)) {
			const QString token = rawToken.trimmed().toLower();
			if (token == QLatin1String("h264")) {
				parsedCodecs.append(static_cast< int >(MumbleProto::ScreenShareCodecH264));
			} else if (token == QLatin1String("vp8")) {
				parsedCodecs.append(static_cast< int >(MumbleProto::ScreenShareCodecVP8));
			} else if (token == QLatin1String("vp9")) {
				parsedCodecs.append(static_cast< int >(MumbleProto::ScreenShareCodecVP9));
			} else if (token == QLatin1String("av1")) {
				parsedCodecs.append(static_cast< int >(MumbleProto::ScreenShareCodecAV1));
			}
		}

		parsedCodecs = sanitizeCodecList(parsedCodecs);
		if (parsedCodecs.isEmpty()) {
			return sanitizeCodecList(fallback);
		}

		return parsedCodecs;
	}

	QString codecPreferenceString(const QList< int > &codecs) {
		QStringList tokens;
		for (const int codec : sanitizeCodecList(codecs)) {
			const QString token = codecToConfigToken(static_cast< MumbleProto::ScreenShareCodec >(codec));
			if (!token.isEmpty()) {
				tokens << token;
			}
		}

		return tokens.join(QLatin1Char(' '));
	}

	unsigned int sanitizeLimit(const unsigned int value, const unsigned int fallback, const unsigned int hardMax) {
		if (value == 0) {
			return fallback;
		}

		return std::min(value, hardMax);
	}

	unsigned int negotiateLimit(const unsigned int requested, const unsigned int sourceLimit,
								const unsigned int serverLimit, const unsigned int defaultValue,
								const unsigned int hardMax) {
		const unsigned int sanitizedRequested = sanitizeLimit(requested, defaultValue, hardMax);
		const unsigned int sanitizedSource    = sanitizeLimit(sourceLimit, defaultValue, hardMax);
		const unsigned int sanitizedServer    = sanitizeLimit(serverLimit, defaultValue, hardMax);

		return std::min(sanitizedRequested, std::min(sanitizedSource, sanitizedServer));
	}

	MumbleProto::ScreenShareCodec selectPreferredCodec(const QList< int > &preferredCodecs,
													   const QList< int > &availableCodecs) {
		const QList< int > sanitizedAvailable = sanitizeCodecList(availableCodecs);
		if (sanitizedAvailable.isEmpty()) {
			return MumbleProto::ScreenShareCodecUnknown;
		}

		for (const int preferredCodec : sanitizeCodecList(preferredCodecs)) {
			if (sanitizedAvailable.contains(preferredCodec)) {
				return static_cast< MumbleProto::ScreenShareCodec >(preferredCodec);
			}
		}

		return static_cast< MumbleProto::ScreenShareCodec >(sanitizedAvailable.front());
	}

	unsigned int defaultBitrateKbps(const MumbleProto::ScreenShareCodec codec, const unsigned int width,
									const unsigned int height, const unsigned int fps) {
		const unsigned int sanitizedWidth  = sanitizeLimit(width, DEFAULT_MAX_WIDTH, HARD_MAX_WIDTH);
		const unsigned int sanitizedHeight = sanitizeLimit(height, DEFAULT_MAX_HEIGHT, HARD_MAX_HEIGHT);
		const unsigned int sanitizedFps    = sanitizeLimit(fps, DEFAULT_MAX_FPS, HARD_MAX_FPS);

		const double pixelsPerFrame = static_cast< double >(sanitizedWidth) * static_cast< double >(sanitizedHeight);
		const double motionFactor   = static_cast< double >(sanitizedFps) / 30.0;
		double bitrateFactor        = (pixelsPerFrame / (1280.0 * 720.0)) * motionFactor;

		switch (codec) {
			case MumbleProto::ScreenShareCodecAV1:
				bitrateFactor *= 0.7;
				break;
			case MumbleProto::ScreenShareCodecVP8:
				bitrateFactor *= 1.05;
				break;
			case MumbleProto::ScreenShareCodecVP9:
				bitrateFactor *= 0.8;
				break;
			case MumbleProto::ScreenShareCodecH264:
			case MumbleProto::ScreenShareCodecUnknown:
			default:
				break;
		}

		const unsigned int bitrateKbps = static_cast< unsigned int >(DEFAULT_TARGET_BITRATE_KBPS * bitrateFactor);
		return qBound(2500U, bitrateKbps, HARD_MAX_BITRATE_KBPS);
	}

	unsigned int sanitizeBitrateKbps(const unsigned int bitrateKbps, const MumbleProto::ScreenShareCodec codec,
									 const unsigned int width, const unsigned int height, const unsigned int fps) {
		if (bitrateKbps == 0) {
			return defaultBitrateKbps(codec, width, height, fps);
		}

		return std::min(bitrateKbps, HARD_MAX_BITRATE_KBPS);
	}

	MumbleProto::ScreenShareRelayTransport relayTransportFromUrl(const QString &relayUrl) {
		const QString normalizedUrl = normalizeRelayUrl(relayUrl);
		if (normalizedUrl.isEmpty()) {
			return MumbleProto::ScreenShareRelayTransportUnknown;
		}

		const QString scheme = QUrl(normalizedUrl).scheme().trimmed().toLower();
		if (scheme == QLatin1String("file") || scheme == QLatin1String("rtmp") || scheme == QLatin1String("rtmps")) {
			return MumbleProto::ScreenShareRelayTransportDirect;
		}
		if (scheme == QLatin1String("wss") || scheme == QLatin1String("ws") || scheme == QLatin1String("https")
			|| scheme == QLatin1String("http")) {
			return MumbleProto::ScreenShareRelayTransportWebRTC;
		}

		return MumbleProto::ScreenShareRelayTransportUnknown;
	}

	QString relayTransportToConfigToken(const MumbleProto::ScreenShareRelayTransport relayTransport) {
		switch (relayTransport) {
			case MumbleProto::ScreenShareRelayTransportDirect:
				return QStringLiteral("direct");
			case MumbleProto::ScreenShareRelayTransportWebRTC:
				return QStringLiteral("webrtc");
			case MumbleProto::ScreenShareRelayTransportUnknown:
			default:
				return QStringLiteral("unknown");
		}
	}

	QString relayRoleToConfigToken(const MumbleProto::ScreenShareRelayRole relayRole) {
		switch (relayRole) {
			case MumbleProto::ScreenShareRelayRolePublisher:
				return QStringLiteral("publisher");
			case MumbleProto::ScreenShareRelayRoleViewer:
			default:
				return QStringLiteral("viewer");
		}
	}

	bool relayTransportRequiresSignaling(const MumbleProto::ScreenShareRelayTransport relayTransport) {
		return relayTransport == MumbleProto::ScreenShareRelayTransportWebRTC;
	}

	bool isDirectRelayTransport(const MumbleProto::ScreenShareRelayTransport relayTransport) {
		return relayTransport == MumbleProto::ScreenShareRelayTransportDirect;
	}

	bool isWebRtcRelayTransport(const MumbleProto::ScreenShareRelayTransport relayTransport) {
		return relayTransport == MumbleProto::ScreenShareRelayTransportWebRTC;
	}

	QString normalizeRelayUrl(const QString &relayUrl) {
		const QString trimmed = relayUrl.trimmed();
		if (trimmed.isEmpty()) {
			return QString();
		}

		const QUrl url(trimmed);
		if (!url.isValid() || url.isRelative()) {
			return QString();
		}

		const QString scheme = url.scheme().trimmed().toLower();
		if (scheme == QLatin1String("file")) {
			if (!url.isLocalFile()) {
				return QString();
			}

			const QString localPath = url.toLocalFile().trimmed();
			if (localPath.isEmpty() || !QDir::isAbsolutePath(localPath)) {
				return QString();
			}

			return QUrl::fromLocalFile(localPath).toString();
		}

		if (url.host().trimmed().isEmpty()) {
			return QString();
		}

		if (scheme != QLatin1String("wss") && scheme != QLatin1String("ws") && scheme != QLatin1String("https")
			&& scheme != QLatin1String("http") && scheme != QLatin1String("rtmp") && scheme != QLatin1String("rtmps")) {
			return QString();
		}

		return url.toString(QUrl::NormalizePathSegments);
	}

	bool isValidRelayUrl(const QString &relayUrl) { return !normalizeRelayUrl(relayUrl).isEmpty(); }
} // namespace ScreenShare
} // namespace Mumble
