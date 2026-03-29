// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include "PersistentChatRender.h"

namespace {
	MumbleProto::ChatMessage makeMessage(unsigned int messageID, quint64 createdAt, unsigned int actorSession,
										 int actorUserID, const QString &actorName, MumbleProto::ChatScope scope,
										 unsigned int scopeID) {
		MumbleProto::ChatMessage message;
		message.set_message_id(messageID);
		message.set_thread_id(messageID);
		message.set_created_at(createdAt);
		message.set_actor(actorSession);
		message.set_actor_user_id(actorUserID);
		message.set_actor_name(actorName.toUtf8().constData());
		message.set_scope(scope);
		message.set_scope_id(scopeID);
		message.set_body_text("hello");
		return message;
	}
} // namespace

class TestPersistentChatRender : public QObject {
	Q_OBJECT

private slots:
	void groupsMessagesByActorScopeAndTime();
	void breaksGroupWhenGapExceedsFiveMinutes();
	void detectsSelfAuthoredGroups();
};

void TestPersistentChatRender::groupsMessagesByActorScopeAndTime() {
	std::vector< MumbleProto::ChatMessage > messages;
	messages.push_back(makeMessage(1, 1000, 7, 7, QStringLiteral("Alice"), MumbleProto::TextChannel, 11));
	messages.push_back(makeMessage(2, 1020, 7, 7, QStringLiteral("Alice"), MumbleProto::TextChannel, 11));
	messages.push_back(makeMessage(3, 1040, 8, 8, QStringLiteral("Bob"), MumbleProto::TextChannel, 11));
	messages.push_back(makeMessage(4, 1060, 8, 8, QStringLiteral("Bob"), MumbleProto::TextChannel, 12));

	const auto groups = PersistentChatRender::buildGroups(messages, {});

	QCOMPARE(groups.size(), static_cast< std::size_t >(3));
	QCOMPARE(groups[0].bubbles.size(), static_cast< std::size_t >(2));
	QCOMPARE(groups[1].bubbles.size(), static_cast< std::size_t >(1));
	QCOMPARE(groups[2].bubbles.size(), static_cast< std::size_t >(1));
	QCOMPARE(groups[0].scopeID, 11U);
	QCOMPARE(groups[2].scopeID, 12U);
}

void TestPersistentChatRender::breaksGroupWhenGapExceedsFiveMinutes() {
	std::vector< MumbleProto::ChatMessage > messages;
	messages.push_back(makeMessage(10, 5000, 3, 3, QStringLiteral("Alice"), MumbleProto::TextChannel, 11));
	messages.push_back(makeMessage(11, 5301, 3, 3, QStringLiteral("Alice"), MumbleProto::TextChannel, 11));

	const auto groups = PersistentChatRender::buildGroups(messages, {});

	QCOMPARE(groups.size(), static_cast< std::size_t >(2));
	QCOMPARE(groups[0].lastMessageID, 10U);
	QCOMPARE(groups[1].firstMessageID, 11U);
}

void TestPersistentChatRender::detectsSelfAuthoredGroups() {
	std::vector< MumbleProto::ChatMessage > messages;
	messages.push_back(makeMessage(20, 8000, 42, 9, QStringLiteral("Self"), MumbleProto::ServerGlobal, 0));
	messages.push_back(makeMessage(21, 8020, 42, 9, QStringLiteral("Self"), MumbleProto::ServerGlobal, 0));
	messages.push_back(makeMessage(22, 8040, 18, 18, QStringLiteral("Other"), MumbleProto::ServerGlobal, 0));

	PersistentChatRender::SelfIdentity selfIdentity;
	selfIdentity.session = 42;
	selfIdentity.userID  = 9;
	selfIdentity.name    = QStringLiteral("Self");

	const auto groups = PersistentChatRender::buildGroups(messages, selfIdentity);

	QCOMPARE(groups.size(), static_cast< std::size_t >(2));
	QVERIFY(groups[0].selfAuthored);
	QVERIFY(!groups[1].selfAuthored);
	QVERIFY(groups[0].bubbles[0].selfAuthored);
}

QTEST_MAIN(TestPersistentChatRender)
#include "TestPersistentChatRender.moc"
