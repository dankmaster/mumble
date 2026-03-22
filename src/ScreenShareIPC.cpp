// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareIPC.h"

#include "ScreenShare.h"

#include <QtCore/QDir>
#include <QtCore/QJsonValue>
#include <QtCore/QProcessEnvironment>

namespace Mumble {
namespace ScreenShare {
namespace IPC {

	QString commandName(const Command command) {
		switch (command) {
			case Command::QueryCapabilities:
				return QStringLiteral("query-capabilities");
			case Command::StartPublish:
				return QStringLiteral("start-publish");
			case Command::StopPublish:
				return QStringLiteral("stop-publish");
			case Command::StartView:
				return QStringLiteral("start-view");
			case Command::StopView:
				return QStringLiteral("stop-view");
		}

		return QString();
	}

	std::optional< Command > commandFromName(const QString &name) {
		if (name == QLatin1String("query-capabilities")) {
			return Command::QueryCapabilities;
		}
		if (name == QLatin1String("start-publish")) {
			return Command::StartPublish;
		}
		if (name == QLatin1String("stop-publish")) {
			return Command::StopPublish;
		}
		if (name == QLatin1String("start-view")) {
			return Command::StartView;
		}
		if (name == QLatin1String("stop-view")) {
			return Command::StopView;
		}

		return std::nullopt;
	}

	QString socketBaseName() {
		return QStringLiteral("MumbleScreenShareHelper");
	}

	QString socketPath(const QString &baseName) {
#ifdef Q_OS_WIN
		return baseName;
#else
		const QString xdgRuntimePath = QProcessEnvironment::systemEnvironment().value(QLatin1String("XDG_RUNTIME_DIR"));
		const QDir xdgRuntimeDir(xdgRuntimePath);
		if (!xdgRuntimePath.isNull() && xdgRuntimeDir.exists()) {
			return xdgRuntimeDir.absoluteFilePath(baseName + QLatin1String("Socket"));
		}

		return QDir::home().absoluteFilePath(QLatin1String(".") + baseName + QLatin1String("Socket"));
#endif
	}

	QJsonObject makeRequest(const Command command, const QJsonObject &payload) {
		QJsonObject request;
		request.insert(QStringLiteral("version"), PROTOCOL_VERSION);
		request.insert(QStringLiteral("command"), commandName(command));
		request.insert(QStringLiteral("payload"), payload);
		return request;
	}

	QJsonObject makeSuccessReply(const QJsonObject &payload) {
		QJsonObject reply;
		reply.insert(QStringLiteral("version"), PROTOCOL_VERSION);
		reply.insert(QStringLiteral("ok"), true);
		reply.insert(QStringLiteral("payload"), payload);
		return reply;
	}

	QJsonObject makeErrorReply(const QString &errorMessage, const QJsonObject &payload) {
		QJsonObject reply = makeSuccessReply(payload);
		reply.insert(QStringLiteral("ok"), false);
		reply.insert(QStringLiteral("error"), errorMessage);
		return reply;
	}

	bool replySucceeded(const QJsonObject &reply, QString *errorMessage) {
		const bool ok = reply.value(QStringLiteral("ok")).toBool(false);
		if (!ok && errorMessage) {
			*errorMessage = reply.value(QStringLiteral("error")).toString();
		}

		return ok;
	}

	QJsonArray codecListToJson(const QList< int > &codecs) {
		QJsonArray jsonCodecs;
		for (const int codec : Mumble::ScreenShare::sanitizeCodecList(codecs)) {
			jsonCodecs.push_back(codec);
		}

		return jsonCodecs;
	}

	QList< int > codecListFromJson(const QJsonValue &codecs) {
		QList< int > parsedCodecs;
		const QJsonArray jsonCodecs = codecs.toArray();
		for (const QJsonValue &codec : jsonCodecs) {
			parsedCodecs.append(codec.toInt());
		}

		return Mumble::ScreenShare::sanitizeCodecList(parsedCodecs);
	}

	QString codecName(const MumbleProto::ScreenShareCodec codec) {
		return Mumble::ScreenShare::codecToConfigToken(codec);
	}

	MumbleProto::ScreenShareCodec codecFromJson(const QJsonValue &value) {
		if (value.isString()) {
			const QList< int > codecs = Mumble::ScreenShare::parseCodecPreferenceString(value.toString());
			if (!codecs.isEmpty()) {
				return static_cast< MumbleProto::ScreenShareCodec >(codecs.front());
			}
		}

		return static_cast< MumbleProto::ScreenShareCodec >(value.toInt());
	}

	QJsonValue codecToJson(const MumbleProto::ScreenShareCodec codec) {
		return static_cast< int >(codec);
	}
} // namespace IPC
} // namespace ScreenShare
} // namespace Mumble
