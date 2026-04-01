// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatStore.h"

#include <algorithm>

namespace {
	bool chatMessageLessThan(const MumbleProto::ChatMessage &lhs, const MumbleProto::ChatMessage &rhs) {
		const quint64 lhsCreatedAt = lhs.has_created_at() ? lhs.created_at() : 0;
		const quint64 rhsCreatedAt = rhs.has_created_at() ? rhs.created_at() : 0;
		if (lhsCreatedAt != rhsCreatedAt) {
			return lhsCreatedAt < rhsCreatedAt;
		}
		if (lhs.thread_id() != rhs.thread_id()) {
			return lhs.thread_id() < rhs.thread_id();
		}

		return lhs.message_id() < rhs.message_id();
	}
}

void PersistentChatStore::clear() {
	m_scopeStates.clear();
}

bool PersistentChatStore::hasScope(const PersistentChatScopeKey &key) const {
	return key.valid && m_scopeStates.contains(key.cacheKey());
}

PersistentChatStore::ScopeState *PersistentChatStore::scopeState(const PersistentChatScopeKey &key) {
	if (!key.valid) {
		return nullptr;
	}

	auto it = m_scopeStates.find(key.cacheKey());
	return it == m_scopeStates.end() ? nullptr : &it.value();
}

const PersistentChatStore::ScopeState *PersistentChatStore::scopeState(const PersistentChatScopeKey &key) const {
	if (!key.valid) {
		return nullptr;
	}

	const auto it = m_scopeStates.constFind(key.cacheKey());
	return it == m_scopeStates.cend() ? nullptr : &it.value();
}

PersistentChatStore::ScopeState &PersistentChatStore::ensureScope(const PersistentChatScopeKey &key) {
	ScopeState &state = m_scopeStates[key.cacheKey()];
	state.snapshot.key = key;
	return state;
}

QList< QString > PersistentChatStore::scopeKeys() const {
	return m_scopeStates.keys();
}

int PersistentChatStore::unreadCount(MumbleProto::ChatScope scope, unsigned int scopeID) const {
	const PersistentChatScopeKey key = PersistentChatScopeKey::fromScope(scope, scopeID);
	const ScopeState *state          = scopeState(key);
	return state ? state->snapshot.unreadCount : 0;
}

int PersistentChatStore::totalUnreadCount() const {
	int total = 0;
	for (auto it = m_scopeStates.cbegin(); it != m_scopeStates.cend(); ++it) {
		if (!it.value().snapshot.key.valid || it.value().snapshot.key.scope == MumbleProto::Aggregate) {
			continue;
		}

		total += it.value().snapshot.unreadCount;
	}
	return total;
}

std::size_t PersistentChatStore::unreadMessagesAfter(const QVector< MumbleProto::ChatMessage > &messages,
													 unsigned int lastReadMessageID) {
	std::size_t unreadCount = 0;
	for (const MumbleProto::ChatMessage &message : messages) {
		if (message.message_id() > lastReadMessageID) {
			++unreadCount;
		}
	}

	return unreadCount;
}

void PersistentChatStore::sortMessages(QVector< MumbleProto::ChatMessage > &messages) {
	std::sort(messages.begin(), messages.end(), chatMessageLessThan);
}

bool PersistentChatStore::mergeMessage(QVector< MumbleProto::ChatMessage > &messages,
									   const MumbleProto::ChatMessage &message) {
	for (int i = 0; i < messages.size(); ++i) {
		const MumbleProto::ChatMessage &current = messages.at(i);
		if (current.thread_id() == message.thread_id() && current.message_id() == message.message_id()) {
			messages[i] = message;
			return false;
		}
	}

	messages.push_back(message);
	sortMessages(messages);
	return true;
}

unsigned int PersistentChatStore::oldestMessageID(const QVector< MumbleProto::ChatMessage > &messages) {
	return messages.isEmpty() ? 0U : messages.front().message_id();
}
