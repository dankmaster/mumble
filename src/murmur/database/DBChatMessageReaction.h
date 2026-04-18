// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBCHATMESSAGEREACTION_H_
#define MUMBLE_SERVER_DATABASE_DBCHATMESSAGEREACTION_H_

#include <string>
#include <utility>

namespace mumble {
namespace server {
	namespace db {

		struct DBChatMessageReaction {
			unsigned int serverID    = {};
			unsigned int messageID   = {};
			unsigned int actorUserID = {};
			std::string emoji        = {};

			DBChatMessageReaction() = default;
			DBChatMessageReaction(unsigned int serverID, unsigned int messageID, unsigned int actorUserID,
								  std::string emoji = {})
				: serverID(serverID), messageID(messageID), actorUserID(actorUserID), emoji(std::move(emoji)) {}

			friend bool operator==(const DBChatMessageReaction &lhs, const DBChatMessageReaction &rhs);
			friend bool operator!=(const DBChatMessageReaction &lhs, const DBChatMessageReaction &rhs);
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBCHATMESSAGEREACTION_H_
