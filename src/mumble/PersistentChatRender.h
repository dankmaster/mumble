// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PERSISTENTCHATRENDER_H_
#define MUMBLE_MUMBLE_PERSISTENTCHATRENDER_H_

#include "Mumble.pb.h"

#include <QtCore/QDate>
#include <QtCore/QDateTime>
#include <QtCore/QString>

#include <optional>
#include <vector>

namespace PersistentChatRender {
	struct SelfIdentity {
		unsigned int session = 0;
		int userID          = -1;
		QString name;
	};

	struct ActorKey {
		std::optional< unsigned int > session;
		std::optional< int > userID;
		QString name;
	};

	struct PersistentChatRenderBubble {
		int messageIndex         = -1;
		unsigned int messageID   = 0;
		unsigned int threadID    = 0;
		QDateTime createdAt;
		bool selfAuthored        = false;
	};

	struct PersistentChatRenderGroup {
		std::vector< PersistentChatRenderBubble > bubbles;
		bool selfAuthored               = false;
		QDate date;
		ActorKey actor;
		MumbleProto::ChatScope scope    = MumbleProto::Channel;
		unsigned int scopeID            = 0;
		QDateTime startedAt;
		QDateTime endedAt;
		unsigned int firstMessageID     = 0;
		unsigned int lastMessageID      = 0;
		unsigned int lastThreadID       = 0;
	};

	ActorKey actorKeyForMessage(const MumbleProto::ChatMessage &message);
	bool sameActor(const MumbleProto::ChatMessage &lhs, const MumbleProto::ChatMessage &rhs);
	bool sameScope(const MumbleProto::ChatMessage &lhs, const MumbleProto::ChatMessage &rhs);
	bool isSelfAuthored(const MumbleProto::ChatMessage &message, const SelfIdentity &selfIdentity);
	std::vector< PersistentChatRenderGroup > buildGroups(const std::vector< MumbleProto::ChatMessage > &messages,
														 const SelfIdentity &selfIdentity);
} // namespace PersistentChatRender

#endif // MUMBLE_MUMBLE_PERSISTENTCHATRENDER_H_
