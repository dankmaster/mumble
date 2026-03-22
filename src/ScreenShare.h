// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SCREENSHARE_H_
#define MUMBLE_SCREENSHARE_H_

#include "Mumble.pb.h"

#include <QList>
#include <QString>

namespace Mumble {
namespace ScreenShare {

	constexpr unsigned int DEFAULT_MAX_WIDTH  = 1920;
	constexpr unsigned int DEFAULT_MAX_HEIGHT = 1080;
	constexpr unsigned int DEFAULT_MAX_FPS    = 60;

	constexpr unsigned int HARD_MAX_WIDTH  = 7680;
	constexpr unsigned int HARD_MAX_HEIGHT = 4320;
	constexpr unsigned int HARD_MAX_FPS    = 144;

	bool isValidCodec(MumbleProto::ScreenShareCodec codec);
	QString codecToConfigToken(MumbleProto::ScreenShareCodec codec);
	QList< int > defaultCodecPreferenceList();
	QList< int > sanitizeCodecList(const QList< int > &codecs);
	QList< int > parseCodecPreferenceString(const QString &codecList, const QList< int > &fallback = {});
	QString codecPreferenceString(const QList< int > &codecs);
	unsigned int sanitizeLimit(unsigned int value, unsigned int fallback, unsigned int hardMax);

} // namespace ScreenShare
} // namespace Mumble

#endif // MUMBLE_SCREENSHARE_H_
