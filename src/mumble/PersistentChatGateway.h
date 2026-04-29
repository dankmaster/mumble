// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PERSISTENTCHATGATEWAY_H_
#define MUMBLE_MUMBLE_PERSISTENTCHATGATEWAY_H_

#include "Mumble.pb.h"

#include <QtCore/QObject>
#include <QtCore/QPointer>

#include <optional>

class ServerHandler;

class PersistentChatGateway : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(PersistentChatGateway)

public:
	explicit PersistentChatGateway(QObject *parent = nullptr);

	void setServerHandler(ServerHandler *serverHandler);
	ServerHandler *serverHandler() const;
	bool isReady() const;

	void requestInitialPage(MumbleProto::ChatScope scope, unsigned int scopeID);
	void requestOlder(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int beforeMessageID);
	void send(MumbleProto::ChatScope scope, unsigned int scopeID, const QString &body,
			  MumbleProto::ChatBodyFormat bodyFormat = MumbleProto::ChatBodyFormatPlainText,
			  std::optional< unsigned int > replyToMessageID = std::nullopt);
	void toggleReaction(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int threadID,
						unsigned int messageID, const QString &emoji, bool active);
	void deleteMessage(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int threadID,
					   unsigned int messageID);
	void markRead(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int lastReadMessageID);

	void handleIncomingHistory(const MumbleProto::ChatHistoryResponse &response);
	void handleIncomingMessage(const MumbleProto::ChatMessage &message);
	void handleIncomingReadState(const MumbleProto::ChatReadStateUpdate &update);

signals:
	void historyReceived(const MumbleProto::ChatHistoryResponse &response);
	void messageReceived(const MumbleProto::ChatMessage &message);
	void readStateReceived(const MumbleProto::ChatReadStateUpdate &update);

private:
	QPointer< ServerHandler > m_serverHandler;
};

#endif // MUMBLE_MUMBLE_PERSISTENTCHATGATEWAY_H_
