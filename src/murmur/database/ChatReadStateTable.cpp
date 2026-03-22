// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatReadStateTable.h"
#include "ChatThreadTable.h"
#include "ChronoUtils.h"
#include "UserTable.h"

#include "database/AccessException.h"
#include "database/Column.h"
#include "database/Constraint.h"
#include "database/DataType.h"
#include "database/ForeignKey.h"
#include "database/TransactionHolder.h"
#include "database/Utils.h"

#include <soci/soci.h>

#include <cassert>
#include <exception>

namespace mdb = ::mumble::db;

namespace mumble {
namespace server {
	namespace db {

		constexpr const char *ChatReadStateTable::NAME;
		constexpr const char *ChatReadStateTable::column::server_id;
		constexpr const char *ChatReadStateTable::column::thread_id;
		constexpr const char *ChatReadStateTable::column::user_id;
		constexpr const char *ChatReadStateTable::column::last_read_message_id;
		constexpr const char *ChatReadStateTable::column::updated_at;
		constexpr unsigned int ChatReadStateTable::INTRODUCED_IN_SCHEMA_VERSION;

		ChatReadStateTable::ChatReadStateTable(soci::session &sql, ::mdb::Backend backend,
											   const ChatThreadTable &threadTable, const UserTable &userTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column threadIDCol(column::thread_id, ::mdb::DataType(::mdb::DataType::Integer));
			threadIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column userIDCol(column::user_id, ::mdb::DataType(::mdb::DataType::Integer));
			userIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column lastReadMessageIDCol(column::last_read_message_id, ::mdb::DataType(::mdb::DataType::Integer));
			lastReadMessageIDCol.setDefaultValue("0");
			lastReadMessageIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column updatedAtCol(column::updated_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			updatedAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			setColumns({ serverCol, threadIDCol, userIDCol, lastReadMessageIDCol, updatedAtCol });

			::mdb::PrimaryKey pk({ serverCol.getName(), threadIDCol.getName(), userIDCol.getName() });
			setPrimaryKey(pk);

			::mdb::ForeignKey threadFK(threadTable, { serverCol, threadIDCol });
			addForeignKey(threadFK);

			::mdb::ForeignKey userFK(userTable, { serverCol, userIDCol });
			addForeignKey(userFK);
		}

		void ChatReadStateTable::setReadState(const DBChatReadState &readState) {
			try {
				auto updatedAt = readState.updatedAt;
				if (updatedAt == std::chrono::system_clock::time_point()) {
					updatedAt = std::chrono::system_clock::now();
				}

				std::size_t updatedAtEpoch = toEpochSeconds(updatedAt);

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::thread_id << "\" = :threadID AND \"" << column::user_id << "\" = :userID",
					soci::use(readState.serverID), soci::use(readState.threadID), soci::use(readState.userID);

				m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \"" << column::thread_id
					  << "\", \"" << column::user_id << "\", \"" << column::last_read_message_id << "\", \""
					  << column::updated_at
					  << "\") VALUES (:serverID, :threadID, :userID, :lastReadMessageID, :updatedAt)",
					soci::use(readState.serverID), soci::use(readState.threadID), soci::use(readState.userID),
					soci::use(readState.lastReadMessageID), soci::use(updatedAtEpoch);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at setting chat read state for user with ID "
															  + std::to_string(readState.userID)
															  + " in thread with ID " + std::to_string(readState.threadID)
															  + " on server with ID "
															  + std::to_string(readState.serverID)));
			}
		}

		std::optional< DBChatReadState > ChatReadStateTable::getReadState(unsigned int serverID, unsigned int threadID,
																		  unsigned int userID) {
			try {
				unsigned int lastReadMessageID = 0;
				std::size_t updatedAt          = 0;

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "SELECT \"" << column::last_read_message_id << "\", \"" << column::updated_at << "\" FROM \""
					  << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \"" << column::thread_id
					  << "\" = :threadID AND \"" << column::user_id << "\" = :userID",
					soci::use(serverID), soci::use(threadID), soci::use(userID), soci::into(lastReadMessageID),
					soci::into(updatedAt);

				transaction.commit();

				if (!m_sql.got_data()) {
					return std::nullopt;
				}

				DBChatReadState readState(serverID, threadID, userID);
				readState.lastReadMessageID = lastReadMessageID;
				readState.updatedAt         = std::chrono::system_clock::time_point(std::chrono::seconds(updatedAt));

				return readState;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting chat read state for user with ID "
															  + std::to_string(userID) + " in thread with ID "
															  + std::to_string(threadID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		void ChatReadStateTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			assert(fromSchemaVersion <= toSchemaVersion);

			if (fromSchemaVersion < INTRODUCED_IN_SCHEMA_VERSION) {
				return;
			}

			::mdb::Table::migrate(fromSchemaVersion, toSchemaVersion);
		}

	} // namespace db
} // namespace server
} // namespace mumble
