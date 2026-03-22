// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DBChatThread.h"
#include "ChronoUtils.h"

namespace mumble {
namespace server {
	namespace db {

		DBChatThread::DBChatThread(unsigned int serverID, unsigned int threadID) : serverID(serverID), threadID(threadID) {
		}

		bool operator==(const DBChatThread &lhs, const DBChatThread &rhs) {
			return lhs.serverID == rhs.serverID && lhs.threadID == rhs.threadID && lhs.scope == rhs.scope
				   && lhs.scopeKey == rhs.scopeKey && lhs.createdByUserID == rhs.createdByUserID
				   && toEpochSeconds(lhs.createdAt) == toEpochSeconds(rhs.createdAt)
				   && toEpochSeconds(lhs.updatedAt) == toEpochSeconds(rhs.updatedAt);
		}

		bool operator!=(const DBChatThread &lhs, const DBChatThread &rhs) { return !(lhs == rhs); }

	} // namespace db
} // namespace server
} // namespace mumble
