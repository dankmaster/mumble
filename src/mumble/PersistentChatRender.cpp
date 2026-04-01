// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatRender.h"

#include "QtUtils.h"

#include <QtGui/QTextDocumentFragment>

namespace {
	QString normalizedMessageText(QString text) {
		return text.replace(QLatin1String("\r\n"), QLatin1String("\n")).replace(QLatin1Char('\r'),
																			 QLatin1Char('\n'));
	}

	QString messageSourceText(const MumbleProto::ChatMessage &message) {
		if (message.has_body_text()) {
			return normalizedMessageText(u8(message.body_text()));
		}

		return QTextDocumentFragment::fromHtml(u8(message.message())).toPlainText();
	}

	bool startsPersistentChatGroup(const std::optional< MumbleProto::ChatMessage > &previousMessage,
								   const QDateTime &previousCreatedAt, const MumbleProto::ChatMessage &message,
								   const QDateTime &createdAt) {
		if (!previousMessage.has_value()) {
			return true;
		}

		if (previousCreatedAt.date() != createdAt.date()) {
			return true;
		}

		if (!PersistentChatRender::sameActor(previousMessage.value(), message)
			|| !PersistentChatRender::sameScope(previousMessage.value(), message)) {
			return true;
		}

		if (previousCreatedAt.isValid() && createdAt.isValid() && previousCreatedAt.secsTo(createdAt) > (5 * 60)) {
			return true;
		}

		if (PersistentChatRender::isSystemMessage(previousMessage.value())
			|| PersistentChatRender::isSystemMessage(message)) {
			return true;
		}

		return false;
	}
} // namespace

namespace PersistentChatRender {
	ActorKey actorKeyForMessage(const MumbleProto::ChatMessage &message) {
		ActorKey actor;
		if (message.has_actor()) {
			actor.session = message.actor();
		}
		if (message.has_actor_user_id()) {
			actor.userID = static_cast< int >(message.actor_user_id());
		}
		if (message.has_actor_name()) {
			actor.name = u8(message.actor_name());
		}

		return actor;
	}

	bool sameActor(const MumbleProto::ChatMessage &lhs, const MumbleProto::ChatMessage &rhs) {
		if (lhs.has_actor() || rhs.has_actor()) {
			return lhs.has_actor() && rhs.has_actor() && lhs.actor() == rhs.actor();
		}

		if (lhs.has_actor_user_id() || rhs.has_actor_user_id()) {
			return lhs.has_actor_user_id() && rhs.has_actor_user_id() && lhs.actor_user_id() == rhs.actor_user_id();
		}

		if (lhs.has_actor_name() || rhs.has_actor_name()) {
			return lhs.has_actor_name() && rhs.has_actor_name() && u8(lhs.actor_name()) == u8(rhs.actor_name());
		}

		return true;
	}

	bool sameScope(const MumbleProto::ChatMessage &lhs, const MumbleProto::ChatMessage &rhs) {
		const MumbleProto::ChatScope lhsScope = lhs.has_scope() ? lhs.scope() : MumbleProto::Channel;
		const MumbleProto::ChatScope rhsScope = rhs.has_scope() ? rhs.scope() : MumbleProto::Channel;
		const unsigned int lhsScopeID         = lhs.has_scope_id() ? lhs.scope_id() : 0;
		const unsigned int rhsScopeID         = rhs.has_scope_id() ? rhs.scope_id() : 0;

		return lhsScope == rhsScope && lhsScopeID == rhsScopeID;
	}

	bool isSystemMessage(const MumbleProto::ChatMessage &message) {
		const QString plainText = messageSourceText(message).trimmed();
		return plainText.startsWith(QLatin1String("[scope]"), Qt::CaseInsensitive)
			   || plainText.startsWith(QLatin1String("[stack]"), Qt::CaseInsensitive)
			   || plainText.startsWith(QLatin1String("[system]"), Qt::CaseInsensitive);
	}

	bool isSelfAuthored(const MumbleProto::ChatMessage &message, const SelfIdentity &selfIdentity) {
		if (selfIdentity.session != 0 && message.has_actor() && message.actor() == selfIdentity.session) {
			return true;
		}

		if (selfIdentity.userID >= 0 && message.has_actor_user_id()
			&& static_cast< int >(message.actor_user_id()) == selfIdentity.userID) {
			return true;
		}

		return !selfIdentity.name.isEmpty() && message.has_actor_name() && u8(message.actor_name()) == selfIdentity.name;
	}

	std::vector< PersistentChatRenderGroup > buildGroups(const std::vector< MumbleProto::ChatMessage > &messages,
														 const SelfIdentity &selfIdentity) {
		std::vector< PersistentChatRenderGroup > groups;
		groups.reserve(messages.size());

		std::optional< MumbleProto::ChatMessage > previousMessage;
		QDateTime previousCreatedAt;

		for (std::size_t i = 0; i < messages.size(); ++i) {
			const MumbleProto::ChatMessage &message = messages[i];
			const QDateTime createdAt = QDateTime::fromSecsSinceEpoch(
				static_cast< qint64 >(message.has_created_at() ? message.created_at() : 0));
			const bool systemMessage = isSystemMessage(message);
			const bool selfAuthored = !systemMessage && isSelfAuthored(message, selfIdentity);
			const bool startsGroup  = startsPersistentChatGroup(previousMessage, previousCreatedAt, message, createdAt);

			if (startsGroup || groups.empty()) {
				PersistentChatRenderGroup group;
				group.selfAuthored  = selfAuthored;
				group.systemMessage = systemMessage;
				group.date          = createdAt.isValid() ? createdAt.date() : QDate();
				group.actor         = actorKeyForMessage(message);
				group.scope         = message.has_scope() ? message.scope() : MumbleProto::Channel;
				group.scopeID       = message.has_scope_id() ? message.scope_id() : 0;
				group.startedAt     = createdAt;
				group.endedAt       = createdAt;
				group.firstMessageID = message.message_id();
				group.lastMessageID  = message.message_id();
				group.lastThreadID   = message.thread_id();
				groups.push_back(std::move(group));
			}

			PersistentChatRenderBubble bubble;
			bubble.messageIndex  = static_cast< int >(i);
			bubble.messageID     = message.message_id();
			bubble.threadID      = message.thread_id();
			bubble.createdAt     = createdAt;
			bubble.selfAuthored  = selfAuthored;
			bubble.systemMessage = systemMessage;

			PersistentChatRenderGroup &group = groups.back();
			group.bubbles.push_back(std::move(bubble));
			group.endedAt      = createdAt;
			group.lastMessageID = message.message_id();
			group.lastThreadID  = message.thread_id();

			previousMessage   = message;
			previousCreatedAt = createdAt;
		}

		return groups;
	}
} // namespace PersistentChatRender
