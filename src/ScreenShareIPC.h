// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SCREENSHAREIPC_H_
#define MUMBLE_SCREENSHAREIPC_H_

#include "Mumble.pb.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QString>

#include <optional>

namespace Mumble {
namespace ScreenShare {
namespace IPC {
	constexpr int PROTOCOL_VERSION = 1;

	enum class Command { QueryCapabilities, StartPublish, StopPublish, StartView, StopView };

	QString commandName(Command command);
	std::optional< Command > commandFromName(const QString &commandName);

	QString socketBaseName();
	QString socketPath(const QString &baseName = socketBaseName());

	QJsonObject makeRequest(Command command, const QJsonObject &payload = {});
	QJsonObject makeSuccessReply(const QJsonObject &payload = {});
	QJsonObject makeErrorReply(const QString &errorMessage, const QJsonObject &payload = {});
	bool replySucceeded(const QJsonObject &reply, QString *errorMessage = nullptr);

	QJsonArray codecListToJson(const QList< int > &codecs);
	QList< int > codecListFromJson(const QJsonValue &codecs);
	QString codecName(MumbleProto::ScreenShareCodec codec);
	MumbleProto::ScreenShareCodec codecFromJson(const QJsonValue &value);
	QJsonValue codecToJson(MumbleProto::ScreenShareCodec codec);
} // namespace IPC
} // namespace ScreenShare
} // namespace Mumble

#endif // MUMBLE_SCREENSHAREIPC_H_
