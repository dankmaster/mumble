// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DBChatReadState.h"
#include "ChronoUtils.h"

namespace mumble {
namespace server {
	namespace db {

		DBChatReadState::DBChatReadState(unsigned int serverID, unsigned int threadID, unsigned int userID)
			: serverID(serverID), threadID(threadID), userID(userID) {}

		bool operator==(const DBChatReadState &lhs, const DBChatReadState &rhs) {
			return lhs.serverID == rhs.serverID && lhs.threadID == rhs.threadID && lhs.userID == rhs.userID
				   && lhs.lastReadMessageID == rhs.lastReadMessageID
				   && toEpochSeconds(lhs.updatedAt) == toEpochSeconds(rhs.updatedAt);
		}

		bool operator!=(const DBChatReadState &lhs, const DBChatReadState &rhs) { return !(lhs == rhs); }

	} // namespace db
} // namespace server
} // namespace mumble
