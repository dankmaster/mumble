// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DBChatMessage.h"
#include "ChronoUtils.h"

namespace mumble {
namespace server {
	namespace db {

		DBChatMessage::DBChatMessage(unsigned int serverID, unsigned int messageID, unsigned int threadID)
			: serverID(serverID), messageID(messageID), threadID(threadID) {}

		bool operator==(const DBChatMessage &lhs, const DBChatMessage &rhs) {
			return lhs.serverID == rhs.serverID && lhs.messageID == rhs.messageID && lhs.threadID == rhs.threadID
				   && lhs.authorUserID == rhs.authorUserID && lhs.authorSession == rhs.authorSession
				   && lhs.body == rhs.body && toEpochSeconds(lhs.createdAt) == toEpochSeconds(rhs.createdAt)
				   && toEpochSeconds(lhs.editedAt) == toEpochSeconds(rhs.editedAt)
				   && toEpochSeconds(lhs.deletedAt) == toEpochSeconds(rhs.deletedAt);
		}

		bool operator!=(const DBChatMessage &lhs, const DBChatMessage &rhs) { return !(lhs == rhs); }

	} // namespace db
} // namespace server
} // namespace mumble
