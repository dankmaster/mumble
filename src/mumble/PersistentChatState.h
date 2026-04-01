// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PERSISTENTCHATSTATE_H_
#define MUMBLE_MUMBLE_PERSISTENTCHATSTATE_H_

#include "Mumble.pb.h"

#include <QtCore/QVector>
#include <QtCore/QString>

enum class PersistentChatLoadingState {
	Idle,
	Initial,
	Refreshing,
	Older
};

struct PersistentChatScopeKey {
	bool valid                = false;
	MumbleProto::ChatScope scope = MumbleProto::Channel;
	unsigned int scopeID      = 0;

	static PersistentChatScopeKey fromScope(MumbleProto::ChatScope scopeValue, unsigned int scopeIDValue) {
		PersistentChatScopeKey key;
		key.valid   = true;
		key.scope   = scopeValue;
		key.scopeID = scopeIDValue;
		return key;
	}

	QString cacheKey() const {
		return valid ? QString::fromLatin1("%1:%2").arg(static_cast< int >(scope)).arg(scopeID) : QString();
	}

	bool matches(MumbleProto::ChatScope scopeValue, unsigned int scopeIDValue) const {
		return valid && scope == scopeValue && scopeID == scopeIDValue;
	}
};

struct PersistentChatScopeStateSnapshot {
	PersistentChatScopeKey key;
	QVector< MumbleProto::ChatMessage > messages;
	bool initialLoaded                    = false;
	bool hasOlder                         = false;
	unsigned int oldestLoadedMessageId    = 0;
	unsigned int lastReadMessageId        = 0;
	int unreadCount                       = 0;
	PersistentChatLoadingState loadingState = PersistentChatLoadingState::Idle;
	QString errorMessage;
};

#endif // MUMBLE_MUMBLE_PERSISTENTCHATSTATE_H_
