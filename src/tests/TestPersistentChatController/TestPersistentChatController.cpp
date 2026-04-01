// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include "PersistentChatController.h"

PersistentChatGateway::PersistentChatGateway(QObject *parent) : QObject(parent) {
}

void PersistentChatGateway::setServerHandler(ServerHandler *) {
}

ServerHandler *PersistentChatGateway::serverHandler() const {
	return nullptr;
}

bool PersistentChatGateway::isReady() const {
	return false;
}

void PersistentChatGateway::requestInitialPage(MumbleProto::ChatScope, unsigned int) {
}

void PersistentChatGateway::requestOlder(MumbleProto::ChatScope, unsigned int, unsigned int) {
}

void PersistentChatGateway::send(MumbleProto::ChatScope, unsigned int, const QString &, MumbleProto::ChatBodyFormat,
								 std::optional< unsigned int >) {
}

void PersistentChatGateway::markRead(MumbleProto::ChatScope, unsigned int, unsigned int) {
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

namespace {
	MumbleProto::ChatMessage makeMessage(unsigned int messageID, quint64 createdAt, MumbleProto::ChatScope scope,
										 unsigned int scopeID, const QString &body = QStringLiteral("hello")) {
		MumbleProto::ChatMessage message;
		message.set_message_id(messageID);
		message.set_thread_id(messageID);
		message.set_created_at(createdAt);
		message.set_scope(scope);
		message.set_scope_id(scopeID);
		message.set_actor(1);
		message.set_actor_user_id(1);
		message.set_actor_name("Alice");
		message.set_body_text(body.toUtf8().constData());
		return message;
	}

	MumbleProto::ChatHistoryResponse makeHistory(MumbleProto::ChatScope scope, unsigned int scopeID,
												 std::initializer_list< MumbleProto::ChatMessage > messages,
												 unsigned int lastReadMessageID, unsigned int oldestMessageID, bool hasOlder) {
		MumbleProto::ChatHistoryResponse response;
		response.set_scope(scope);
		response.set_scope_id(scopeID);
		response.set_last_read_message_id(lastReadMessageID);
		response.set_oldest_message_id(oldestMessageID);
		response.set_has_older(hasOlder);
		for (const MumbleProto::ChatMessage &message : messages) {
			*response.add_messages() = message;
		}
		return response;
	}

	MumbleProto::ChatReadStateUpdate makeReadState(MumbleProto::ChatScope scope, unsigned int scopeID,
												   unsigned int lastReadMessageID) {
		MumbleProto::ChatReadStateUpdate update;
		update.set_scope(scope);
		update.set_scope_id(scopeID);
		update.set_last_read_message_id(lastReadMessageID);
		return update;
	}

	MumbleProto::ChatEmbedState makeEmbedState(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int messageID,
											   unsigned int threadID, const QString &url) {
		MumbleProto::ChatEmbedState state;
		state.set_scope(scope);
		state.set_scope_id(scopeID);
		state.set_message_id(messageID);
		state.set_thread_id(threadID);
		MumbleProto::ChatEmbedRef *embed = state.add_embeds();
		embed->set_url(url.toUtf8().constData());
		return state;
	}
}

class TestPersistentChatController : public QObject {
	Q_OBJECT

private slots:
	void restoresCachedScopeSnapshots();
	void mergesOlderHistoryAndReadState();
	void appliesEmbedUpdatesToCachedMessages();
};

void TestPersistentChatController::restoresCachedScopeSnapshots() {
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);

	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 11), false);
	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::TextChannel, 11,
					{ makeMessage(10, 1000, MumbleProto::TextChannel, 11),
					  makeMessage(11, 1010, MumbleProto::TextChannel, 11) },
					10, 10, true));

	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 22), false);
	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::TextChannel, 22, { makeMessage(20, 2000, MumbleProto::TextChannel, 22) }, 0, 20, false));

	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 11), false);
	const PersistentChatScopeStateSnapshot snapshot = controller.activeSnapshot();

	QCOMPARE(snapshot.key.scope, MumbleProto::TextChannel);
	QCOMPARE(snapshot.key.scopeID, 11U);
	QCOMPARE(snapshot.messages.size(), 2);
	QCOMPARE(snapshot.messages.front().message_id(), 10U);
	QCOMPARE(snapshot.messages.back().message_id(), 11U);
	QCOMPARE(snapshot.unreadCount, 1);
}

void TestPersistentChatController::mergesOlderHistoryAndReadState() {
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::Channel, 7), false);

	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::Channel, 7,
					{ makeMessage(20, 2000, MumbleProto::Channel, 7),
					  makeMessage(21, 2010, MumbleProto::Channel, 7) },
					20, 20, true));
	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::Channel, 7,
					{ makeMessage(10, 1000, MumbleProto::Channel, 7),
					  makeMessage(11, 1010, MumbleProto::Channel, 7) },
					20, 10, false));
	gateway.handleIncomingReadState(makeReadState(MumbleProto::Channel, 7, 21));

	const PersistentChatScopeStateSnapshot snapshot = controller.activeSnapshot();
	QCOMPARE(snapshot.messages.size(), 4);
	QCOMPARE(snapshot.messages.front().message_id(), 10U);
	QCOMPARE(snapshot.messages.back().message_id(), 21U);
	QCOMPARE(snapshot.oldestLoadedMessageId, 10U);
	QCOMPARE(snapshot.hasOlder, false);
	QCOMPARE(snapshot.unreadCount, 0);
}

void TestPersistentChatController::appliesEmbedUpdatesToCachedMessages() {
	PersistentChatGateway gateway;
	PersistentChatController controller;
	controller.setGateway(&gateway);
	controller.setActiveScope(PersistentChatScopeKey::fromScope(MumbleProto::TextChannel, 33), false);

	gateway.handleIncomingHistory(
		makeHistory(MumbleProto::TextChannel, 33, { makeMessage(30, 3000, MumbleProto::TextChannel, 33) }, 0, 30, false));

	QVERIFY(controller.applyEmbedState(
		makeEmbedState(MumbleProto::TextChannel, 33, 30, 30, QStringLiteral("https://example.com/preview"))));

	const PersistentChatScopeStateSnapshot snapshot = controller.activeSnapshot();
	QCOMPARE(snapshot.messages.size(), 1);
	QCOMPARE(snapshot.messages.front().embeds_size(), 1);
	QCOMPARE(QString::fromUtf8(snapshot.messages.front().embeds(0).url().c_str()), QStringLiteral("https://example.com/preview"));
}

QTEST_MAIN(TestPersistentChatController)
#include "TestPersistentChatController.moc"
