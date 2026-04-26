// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SCREENSHARE_H_
#define MUMBLE_SCREENSHARE_H_

#include "Mumble.pb.h"

#include <QList>
#include <QString>
#include <QUrl>

namespace Mumble {
namespace ScreenShare {
	struct Capability {
		QList< int > codecs;
		unsigned int maxWidth  = 0;
		unsigned int maxHeight = 0;
		unsigned int maxFps    = 0;
	};

	struct NegotiatedParameters {
		MumbleProto::ScreenShareCodec codec = MumbleProto::ScreenShareCodecUnknown;
		unsigned int width                  = 0;
		unsigned int height                 = 0;
		unsigned int fps                    = 0;
		unsigned int bitrateKbps            = 0;
	};

	constexpr unsigned int DEFAULT_MAX_WIDTH           = 1280;
	constexpr unsigned int DEFAULT_MAX_HEIGHT          = 720;
	constexpr unsigned int DEFAULT_MAX_FPS             = 30;
	constexpr unsigned int DEFAULT_TARGET_BITRATE_KBPS = 3000;

	constexpr unsigned int HARD_MAX_WIDTH        = 7680;
	constexpr unsigned int HARD_MAX_HEIGHT       = 4320;
	constexpr unsigned int HARD_MAX_FPS          = 144;
	constexpr unsigned int HARD_MAX_BITRATE_KBPS = 50000;

	bool isValidCodec(MumbleProto::ScreenShareCodec codec);
	QString codecToConfigToken(MumbleProto::ScreenShareCodec codec);
	QList< int > defaultCodecPreferenceList();
	QList< int > sanitizeCodecList(const QList< int > &codecs);
	QList< int > parseCodecPreferenceString(const QString &codecList, const QList< int > &fallback = {});
	QString codecPreferenceString(const QList< int > &codecs);
	unsigned int sanitizeLimit(unsigned int value, unsigned int fallback, unsigned int hardMax);
	unsigned int negotiateLimit(unsigned int requested, unsigned int sourceLimit, unsigned int serverLimit,
								unsigned int defaultValue, unsigned int hardMax);
	MumbleProto::ScreenShareCodec selectPreferredCodec(const QList< int > &preferredCodecs,
													   const QList< int > &availableCodecs);
	unsigned int defaultBitrateKbps(MumbleProto::ScreenShareCodec codec, unsigned int width, unsigned int height,
									unsigned int fps);
	unsigned int sanitizeBitrateKbps(unsigned int bitrateKbps, MumbleProto::ScreenShareCodec codec, unsigned int width,
									 unsigned int height, unsigned int fps);
	MumbleProto::ScreenShareRelayTransport relayTransportFromUrl(const QString &relayUrl);
	QString relayTransportToConfigToken(MumbleProto::ScreenShareRelayTransport relayTransport);
	QString relayRoleToConfigToken(MumbleProto::ScreenShareRelayRole relayRole);
	bool relayTransportRequiresSignaling(MumbleProto::ScreenShareRelayTransport relayTransport);
	bool isDirectRelayTransport(MumbleProto::ScreenShareRelayTransport relayTransport);
	QString normalizeRelayUrl(const QString &relayUrl);
	bool isValidRelayUrl(const QString &relayUrl);

} // namespace ScreenShare
} // namespace Mumble

#endif // MUMBLE_SCREENSHARE_H_
