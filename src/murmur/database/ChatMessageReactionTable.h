// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_CHATMESSAGEREACTIONTABLE_H_
#define MUMBLE_SERVER_DATABASE_CHATMESSAGEREACTIONTABLE_H_

#include "DBChatMessageReaction.h"

#include "database/Backend.h"
#include "database/Table.h"

#include <vector>

namespace soci {
class session;
}

namespace mumble {
namespace server {
	namespace db {

		class ChatMessageTable;
		class UserTable;

		class ChatMessageReactionTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "chat_message_reactions";

			struct column {
				column() = delete;
				static constexpr const char *server_id = "server_id";
				static constexpr const char *message_id = "message_id";
				static constexpr const char *actor_user_id = "actor_user_id";
				static constexpr const char *emoji = "emoji";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 17;

			ChatMessageReactionTable(soci::session &sql, ::mumble::db::Backend backend,
									 const ChatMessageTable &messageTable, const UserTable &userTable);
			~ChatMessageReactionTable() = default;

			void setReactionActive(unsigned int serverID, unsigned int messageID, unsigned int actorUserID,
								   const std::string &emoji, bool active);
			std::vector< DBChatMessageReaction > getReactions(unsigned int serverID, unsigned int messageID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_CHATMESSAGEREACTIONTABLE_H_
