// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PERSISTENTCHATSTORE_H_
#define MUMBLE_MUMBLE_PERSISTENTCHATSTORE_H_

#include "PersistentChatState.h"

#include <QtCore/QHash>

class PersistentChatStore {
public:
	struct ScopeState {
		PersistentChatScopeStateSnapshot snapshot;
		bool initialRequestInFlight          = false;
		bool olderRequestInFlight            = false;
		bool dropPendingHistoryResponses     = false;
	};

	void clear();

	bool hasScope(const PersistentChatScopeKey &key) const;
	ScopeState *scopeState(const PersistentChatScopeKey &key);
	const ScopeState *scopeState(const PersistentChatScopeKey &key) const;
	ScopeState &ensureScope(const PersistentChatScopeKey &key);
	QList< QString > scopeKeys() const;

	int unreadCount(MumbleProto::ChatScope scope, unsigned int scopeID) const;
	int totalUnreadCount() const;

	static std::size_t unreadMessagesAfter(const QVector< MumbleProto::ChatMessage > &messages,
										   unsigned int lastReadMessageID);
	static void sortMessages(QVector< MumbleProto::ChatMessage > &messages);
	static bool mergeMessage(QVector< MumbleProto::ChatMessage > &messages, const MumbleProto::ChatMessage &message);
	static unsigned int oldestMessageID(const QVector< MumbleProto::ChatMessage > &messages);

private:
	QHash< QString, ScopeState > m_scopeStates;
};

#endif // MUMBLE_MUMBLE_PERSISTENTCHATSTORE_H_
