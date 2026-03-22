// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_CHATREADSTATETABLE_H_
#define MUMBLE_SERVER_DATABASE_CHATREADSTATETABLE_H_

#include "DBChatReadState.h"

#include "database/Backend.h"
#include "database/Table.h"

#include <optional>

namespace soci {
class session;
}

namespace mumble {
namespace server {
	namespace db {

		class ChatThreadTable;
		class UserTable;

		class ChatReadStateTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "chat_read_state";

			struct column {
				column()                                     = delete;
				static constexpr const char *server_id       = "server_id";
				static constexpr const char *thread_id       = "thread_id";
				static constexpr const char *user_id         = "user_id";
				static constexpr const char *last_read_message_id = "last_read_message_id";
				static constexpr const char *updated_at      = "updated_at";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 12;

			ChatReadStateTable(soci::session &sql, ::mumble::db::Backend backend, const ChatThreadTable &threadTable,
							   const UserTable &userTable);
			~ChatReadStateTable() = default;

			void setReadState(const DBChatReadState &readState);
			std::optional< DBChatReadState > getReadState(unsigned int serverID, unsigned int threadID, unsigned int userID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_CHATREADSTATETABLE_H_
