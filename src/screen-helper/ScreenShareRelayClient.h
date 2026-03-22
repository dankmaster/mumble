// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SCREENHELPER_SCREENSHARERELAYCLIENT_H_
#define MUMBLE_SCREENHELPER_SCREENSHARERELAYCLIENT_H_

#include "Mumble.pb.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

class ScreenShareRelayClient {
public:
	struct Contract {
		bool valid = false;
		bool runtimeExecutable = false;
		bool requiresSignaling = false;
		bool publishSupported = false;
		bool viewSupported = false;
		QString errorMessage;
		QString relayUrl;
		QString relayRoomID;
		QString relayToken;
		QString relaySessionID;
		quint64 relayTokenExpiresAt = 0;
		MumbleProto::ScreenShareRelayTransport relayTransport = MumbleProto::ScreenShareRelayTransportUnknown;
		MumbleProto::ScreenShareRelayRole relayRole = MumbleProto::ScreenShareRelayRoleViewer;
		QString contractMode;
		QString description;
		QStringList warnings;
	};

	static Contract contractForPublish(const QJsonObject &payload);
	static Contract contractForView(const QJsonObject &payload);
	static QJsonArray advertisedContracts();
	static QJsonArray runtimeRelayTransports();
	static QJsonArray signalingRelayTransports();
};

#endif // MUMBLE_SCREENHELPER_SCREENSHARERELAYCLIENT_H_
