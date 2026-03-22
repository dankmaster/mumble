// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShare.h"

#include <algorithm>

#include <QRegularExpression>
#include <QStringList>

namespace Mumble {
namespace ScreenShare {

	bool isValidCodec(const MumbleProto::ScreenShareCodec codec) {
		switch (codec) {
			case MumbleProto::ScreenShareCodecH264:
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
		return { static_cast< int >(MumbleProto::ScreenShareCodecAV1),
				 static_cast< int >(MumbleProto::ScreenShareCodecVP9),
				 static_cast< int >(MumbleProto::ScreenShareCodecH264) };
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

} // namespace ScreenShare
} // namespace Mumble
