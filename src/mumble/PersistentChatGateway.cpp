// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatGateway.h"

#include "ServerHandler.h"

PersistentChatGateway::PersistentChatGateway(QObject *parent) : QObject(parent) {
}

void PersistentChatGateway::setServerHandler(ServerHandler *serverHandler) {
	m_serverHandler = serverHandler;
}

ServerHandler *PersistentChatGateway::serverHandler() const {
	return m_serverHandler.data();
}

bool PersistentChatGateway::isReady() const {
	return m_serverHandler && m_serverHandler->isRunning();
}

void PersistentChatGateway::requestInitialPage(MumbleProto::ChatScope scope, unsigned int scopeID) {
	if (!isReady()) {
		return;
	}

	m_serverHandler->requestChatHistory(scope, scopeID, 0, 50, std::nullopt);
}

void PersistentChatGateway::requestOlder(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int beforeMessageID) {
	if (!isReady() || beforeMessageID == 0) {
		return;
	}

	m_serverHandler->requestChatHistory(scope, scopeID, 0, 50, beforeMessageID);
}

void PersistentChatGateway::send(MumbleProto::ChatScope scope, unsigned int scopeID, const QString &body,
								 MumbleProto::ChatBodyFormat bodyFormat,
								 std::optional< unsigned int > replyToMessageID) {
	if (!isReady()) {
		return;
	}

	m_serverHandler->sendChatMessage(scope, scopeID, body, bodyFormat, replyToMessageID);
}

void PersistentChatGateway::toggleReaction(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int threadID,
										   unsigned int messageID, const QString &emoji, bool active) {
	if (!isReady() || messageID == 0 || emoji.trimmed().isEmpty()) {
		return;
	}

	m_serverHandler->sendChatReactionToggle(scope, scopeID, threadID, messageID, emoji, active);
}

void PersistentChatGateway::deleteMessage(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int threadID,
										  unsigned int messageID) {
	if (!isReady() || messageID == 0) {
		return;
	}

	m_serverHandler->sendChatMessageDelete(scope, scopeID, threadID, messageID);
}

void PersistentChatGateway::markRead(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int lastReadMessageID) {
	if (!isReady() || lastReadMessageID == 0) {
		return;
	}

	m_serverHandler->updateChatReadState(scope, scopeID, lastReadMessageID);
}

void PersistentChatGateway::handleIncomingHistory(const MumbleProto::ChatHistoryResponse &response) {
	emit historyReceived(response);
}

void PersistentChatGateway::handleIncomingMessage(const MumbleProto::ChatMessage &message) {
	emit messageReceived(message);
}

void PersistentChatGateway::handleIncomingReadState(const MumbleProto::ChatReadStateUpdate &update) {
	emit readStateReceived(update);
}
