// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DBChatMessageReaction.h"

namespace mumble {
namespace server {
	namespace db {

		bool operator==(const DBChatMessageReaction &lhs, const DBChatMessageReaction &rhs) {
			return lhs.serverID == rhs.serverID && lhs.messageID == rhs.messageID
				   && lhs.actorUserID == rhs.actorUserID && lhs.emoji == rhs.emoji;
		}

		bool operator!=(const DBChatMessageReaction &lhs, const DBChatMessageReaction &rhs) {
			return !(lhs == rhs);
		}

	} // namespace db
} // namespace server
} // namespace mumble
