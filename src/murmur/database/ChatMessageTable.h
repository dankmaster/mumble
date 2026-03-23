// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_CHATMESSAGETABLE_H_
#define MUMBLE_SERVER_DATABASE_CHATMESSAGETABLE_H_

#include "DBChatMessage.h"

#include "database/Backend.h"
#include "database/Table.h"

#include <limits>
#include <vector>

namespace soci {
class session;
}

namespace mumble {
namespace server {
	namespace db {

		class ChatThreadTable;
		class UserTable;

		class ChatMessageTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "chat_messages";

			struct column {
				column()                                     = delete;
				static constexpr const char *server_id       = "server_id";
				static constexpr const char *message_id      = "message_id";
				static constexpr const char *thread_id       = "thread_id";
				static constexpr const char *author_user_id  = "author_user_id";
				static constexpr const char *author_session  = "author_session";
				static constexpr const char *author_name     = "author_name";
				static constexpr const char *body            = "body";
				static constexpr const char *created_at      = "created_at";
				static constexpr const char *edited_at       = "edited_at";
				static constexpr const char *deleted_at      = "deleted_at";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 12;

			ChatMessageTable(soci::session &sql, ::mumble::db::Backend backend, const ChatThreadTable &threadTable,
							 const UserTable &userTable);
			~ChatMessageTable() = default;

			void addMessage(const DBChatMessage &message);

			std::vector< DBChatMessage >
				getMessages(unsigned int serverID, unsigned int threadID,
							unsigned int maxEntries  = static_cast< unsigned int >(std::numeric_limits< int >::max()),
							unsigned int startOffset = 0);

			unsigned int getFreeMessageID(unsigned int serverID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_CHATMESSAGETABLE_H_
