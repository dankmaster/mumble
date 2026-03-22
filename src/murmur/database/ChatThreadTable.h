// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_CHATTHREADTABLE_H_
#define MUMBLE_SERVER_DATABASE_CHATTHREADTABLE_H_

#include "DBChatThread.h"

#include "database/Backend.h"
#include "database/Table.h"

#include <chrono>
#include <optional>

namespace soci {
class session;
}

namespace mumble {
namespace server {
	namespace db {

		class ServerTable;
		class UserTable;

		class ChatThreadTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "chat_threads";

			struct column {
				column()                                     = delete;
				static constexpr const char *server_id       = "server_id";
				static constexpr const char *thread_id       = "thread_id";
				static constexpr const char *thread_scope    = "thread_scope";
				static constexpr const char *scope_key       = "scope_key";
				static constexpr const char *created_by_user_id = "created_by_user_id";
				static constexpr const char *created_at      = "created_at";
				static constexpr const char *updated_at      = "updated_at";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 12;

			ChatThreadTable(soci::session &sql, ::mumble::db::Backend backend, const ServerTable &serverTable,
							const UserTable &userTable);
			~ChatThreadTable() = default;

			void addThread(const DBChatThread &thread);

			bool threadExists(unsigned int serverID, unsigned int threadID);
			DBChatThread getThread(unsigned int serverID, unsigned int threadID);
			std::optional< DBChatThread > getThreadByScope(unsigned int serverID, ChatThreadScope scope,
														   const std::string &scopeKey);
			void touchThread(unsigned int serverID, unsigned int threadID,
							 const std::chrono::system_clock::time_point &timepoint = std::chrono::system_clock::now());

			unsigned int getFreeThreadID(unsigned int serverID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_CHATTHREADTABLE_H_
