// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatMessageTable.h"
#include "ChatThreadTable.h"
#include "ChronoUtils.h"
#include "UserTable.h"

#include "database/AccessException.h"
#include "database/Column.h"
#include "database/Constraint.h"
#include "database/DataType.h"
#include "database/ForeignKey.h"
#include "database/FormatException.h"
#include "database/Index.h"
#include "database/TransactionHolder.h"
#include "database/Utils.h"

#include <soci/soci.h>

#include <algorithm>
#include <cassert>
#include <exception>

namespace mdb = ::mumble::db;

namespace mumble {
namespace server {
	namespace db {

		constexpr const char *ChatMessageTable::NAME;
		constexpr const char *ChatMessageTable::column::server_id;
		constexpr const char *ChatMessageTable::column::message_id;
		constexpr const char *ChatMessageTable::column::thread_id;
		constexpr const char *ChatMessageTable::column::reply_to_message_id;
		constexpr const char *ChatMessageTable::column::author_user_id;
		constexpr const char *ChatMessageTable::column::author_session;
		constexpr const char *ChatMessageTable::column::author_name;
		constexpr const char *ChatMessageTable::column::body;
		constexpr const char *ChatMessageTable::column::created_at;
		constexpr const char *ChatMessageTable::column::edited_at;
		constexpr const char *ChatMessageTable::column::deleted_at;
		constexpr unsigned int ChatMessageTable::INTRODUCED_IN_SCHEMA_VERSION;

		ChatMessageTable::ChatMessageTable(soci::session &sql, ::mdb::Backend backend,
										   const ChatThreadTable &threadTable, const UserTable &userTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column messageIDCol(column::message_id, ::mdb::DataType(::mdb::DataType::Integer));
			messageIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column threadIDCol(column::thread_id, ::mdb::DataType(::mdb::DataType::Integer));
			threadIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column replyToMessageIDCol(column::reply_to_message_id, ::mdb::DataType(::mdb::DataType::Integer));
			replyToMessageIDCol.setDefaultValue("NULL");

			::mdb::Column authorUserCol(column::author_user_id, ::mdb::DataType(::mdb::DataType::Integer));
			authorUserCol.setDefaultValue("NULL");

			::mdb::Column authorSessionCol(column::author_session, ::mdb::DataType(::mdb::DataType::Integer));
			authorSessionCol.setDefaultValue("NULL");

			::mdb::Column authorNameCol(column::author_name, ::mdb::DataType(::mdb::DataType::VarChar, 255));
			authorNameCol.setDefaultValue("NULL");

			::mdb::Column bodyCol(column::body, ::mdb::DataType(::mdb::DataType::Text));
			bodyCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column createdAtCol(column::created_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			createdAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column editedAtCol(column::edited_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			editedAtCol.setDefaultValue("0");
			editedAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column deletedAtCol(column::deleted_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			deletedAtCol.setDefaultValue("0");
			deletedAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			setColumns({ serverCol, messageIDCol, threadIDCol, replyToMessageIDCol, authorUserCol, authorSessionCol,
						 authorNameCol, bodyCol, createdAtCol, editedAtCol, deletedAtCol });

			::mdb::PrimaryKey pk({ serverCol.getName(), messageIDCol.getName() });
			setPrimaryKey(pk);

			::mdb::ForeignKey threadFK(threadTable, { serverCol, threadIDCol });
			addForeignKey(threadFK);

			::mdb::ForeignKey authorFK(userTable, { serverCol, authorUserCol });
			addForeignKey(authorFK);

			::mdb::Index threadMessageIndex(std::string(NAME) + "_thread_messages",
											{ column::server_id, column::thread_id, column::message_id });
			addIndex(threadMessageIndex, false);
		}

		void ChatMessageTable::addMessage(const DBChatMessage &message) {
			if (message.body.empty()) {
				throw ::mdb::FormatException("A chat message requires a non-empty body");
			}

			try {
				unsigned int authorUserID   = 0;
				unsigned int authorSession  = 0;
				unsigned int replyToMessageID = 0;
				std::string authorName;
				soci::indicator replyToMessageInd = soci::i_null;
				soci::indicator authorUserInd    = soci::i_null;
				soci::indicator authorSessionInd = soci::i_null;
				soci::indicator authorNameInd    = soci::i_null;

				if (message.replyToMessageID) {
					replyToMessageID = message.replyToMessageID.value();
					replyToMessageInd = soci::i_ok;
				}
				if (message.authorUserID) {
					authorUserID  = message.authorUserID.value();
					authorUserInd = soci::i_ok;
				}
				if (message.authorSession) {
					authorSession    = message.authorSession.value();
					authorSessionInd = soci::i_ok;
				}
				if (message.authorName && !message.authorName->empty()) {
					authorName    = message.authorName.value();
					authorNameInd = soci::i_ok;
				}

				auto createdAt = message.createdAt;
				if (createdAt == std::chrono::system_clock::time_point()) {
					createdAt = std::chrono::system_clock::now();
				}

				std::size_t createdAtEpoch = toEpochSeconds(createdAt);
				std::size_t editedAtEpoch  = toEpochSeconds(message.editedAt);
				std::size_t deletedAtEpoch = toEpochSeconds(message.deletedAt);

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \"" << column::message_id
					  << "\", \"" << column::thread_id << "\", \"" << column::reply_to_message_id << "\", \""
					  << column::author_user_id << "\", \"" << column::author_session << "\", \"" << column::author_name << "\", \"" << column::body << "\", \"" << column::created_at
					  << "\", \"" << column::edited_at << "\", \"" << column::deleted_at
					  << "\") VALUES (:serverID, :messageID, :threadID, :replyToMessageID, :authorUserID, :authorSession, :authorName, :body, "
						 ":createdAt, :editedAt, :deletedAt)",
					soci::use(message.serverID), soci::use(message.messageID), soci::use(message.threadID),
					soci::use(replyToMessageID, replyToMessageInd),
					soci::use(authorUserID, authorUserInd), soci::use(authorSession, authorSessionInd),
					soci::use(authorName, authorNameInd), soci::use(message.body), soci::use(createdAtEpoch), soci::use(editedAtEpoch),
					soci::use(deletedAtEpoch);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at adding chat message with ID "
															  + std::to_string(message.messageID)
															  + " to thread with ID " + std::to_string(message.threadID)
															  + " on server with ID "
															  + std::to_string(message.serverID)));
			}
		}

		std::vector< DBChatMessage > ChatMessageTable::getMessages(unsigned int serverID, unsigned int threadID,
																  unsigned int maxEntries, unsigned int startOffset) {
			assert(maxEntries <= std::numeric_limits< int >::max());
			assert(startOffset <= std::numeric_limits< int >::max());

			try {
				std::vector< DBChatMessage > messages;
				soci::row row;

				::mdb::TransactionHolder transaction = ensureTransaction();

				soci::statement stmt =
					(m_sql.prepare << "SELECT \"" << column::message_id << "\", \"" << column::author_user_id
								   << "\", \"" << column::reply_to_message_id << "\", \"" << column::author_session << "\", \"" << column::author_name << "\", \""
								   << column::body << "\", \"" << column::created_at << "\", \"" << column::edited_at << "\", \""
								   << column::deleted_at << "\" FROM \"" << NAME << "\" WHERE \"" << column::server_id
								   << "\" = :serverID AND \"" << column::thread_id << "\" = :threadID ORDER BY \""
								   << column::message_id << "\" DESC "
								   << ::mdb::utils::limitOffset(m_backend, ":limit", ":offset"),
					 soci::use(serverID), soci::use(threadID), soci::use(maxEntries), soci::use(startOffset),
					 soci::into(row));

				stmt.execute(false);

				while (stmt.fetch()) {
					assert(row.size() == 9);
					assert(row.get_properties(0).get_data_type() == soci::dt_integer);
					assert(row.get_properties(1).get_data_type() == soci::dt_integer);
					assert(row.get_properties(2).get_data_type() == soci::dt_integer);
					assert(row.get_properties(3).get_data_type() == soci::dt_integer);
					assert(row.get_properties(4).get_data_type() == soci::dt_string);
					assert(row.get_properties(5).get_data_type() == soci::dt_string);
					assert(row.get_properties(6).get_data_type() == soci::dt_long_long);
					assert(row.get_properties(7).get_data_type() == soci::dt_long_long);
					assert(row.get_properties(8).get_data_type() == soci::dt_long_long);

					DBChatMessage message(serverID, static_cast< unsigned int >(row.get< int >(0)), threadID);
					if (row.get_indicator(1) == soci::i_ok) {
						message.authorUserID = static_cast< unsigned int >(row.get< int >(1));
					}
					if (row.get_indicator(2) == soci::i_ok) {
						message.replyToMessageID = static_cast< unsigned int >(row.get< int >(2));
					}
					if (row.get_indicator(3) == soci::i_ok) {
						message.authorSession = static_cast< unsigned int >(row.get< int >(3));
					}
					if (row.get_indicator(4) == soci::i_ok) {
						message.authorName = row.get< std::string >(4);
					}
					message.body      = row.get< std::string >(5);
					message.createdAt = std::chrono::system_clock::time_point(std::chrono::seconds(row.get< long long >(6)));
					message.editedAt  = std::chrono::system_clock::time_point(std::chrono::seconds(row.get< long long >(7)));
					message.deletedAt = std::chrono::system_clock::time_point(std::chrono::seconds(row.get< long long >(8)));

					messages.push_back(std::move(message));
				}

				std::reverse(messages.begin(), messages.end());

				transaction.commit();

				return messages;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting chat messages for thread with ID "
															  + std::to_string(threadID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		unsigned int ChatMessageTable::getFreeMessageID(unsigned int serverID) {
			try {
				unsigned int id = 0;

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << ::mdb::utils::getLowestUnoccupiedIDStatement(
					m_backend, NAME, column::message_id, { ::mdb::utils::ColAlias(column::server_id, "serverID") }),
					soci::use(serverID, "serverID"), soci::into(id);

				::mdb::utils::verifyQueryResultedInData(m_sql);

				transaction.commit();

				return id;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting free chat message ID on server with ID "
															  + std::to_string(serverID)));
			}
		}

		void ChatMessageTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			assert(fromSchemaVersion <= toSchemaVersion);

			if (fromSchemaVersion < INTRODUCED_IN_SCHEMA_VERSION) {
				return;
			}

			try {
				if (fromSchemaVersion < 14) {
					m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \"" << column::message_id
						  << "\", \"" << column::thread_id << "\", \"" << column::reply_to_message_id << "\", \""
						  << column::author_user_id << "\", \"" << column::author_session << "\", \"" << column::author_name << "\", \"" << column::body
						  << "\", \"" << column::created_at << "\", \"" << column::edited_at << "\", \""
						  << column::deleted_at << "\") SELECT cm.\"server_id\", cm.\"message_id\", cm.\"thread_id\", "
						  << "NULL, cm.\"author_user_id\", cm.\"author_session\", u.\"user_name\", cm.\"body\", "
						  << "cm.\"created_at\", cm.\"edited_at\", cm.\"deleted_at\" FROM \"chat_messages"
						  << mdb::Database::OLD_TABLE_SUFFIX
						  << "\" AS cm LEFT JOIN \"" << UserTable::NAME
						  << "\" AS u ON u.\"server_id\" = cm.\"server_id\" AND u.\"user_id\" = cm.\"author_user_id\"";
				} else if (fromSchemaVersion < 15) {
					m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \"" << column::message_id
						  << "\", \"" << column::thread_id << "\", \"" << column::reply_to_message_id << "\", \""
						  << column::author_user_id << "\", \"" << column::author_session << "\", \"" << column::author_name
						  << "\", \"" << column::body << "\", \"" << column::created_at << "\", \"" << column::edited_at
						  << "\", \"" << column::deleted_at << "\") SELECT old.\"server_id\", old.\"message_id\", "
						  << "old.\"thread_id\", NULL, old.\"author_user_id\", old.\"author_session\", old.\"author_name\", "
						  << "old.\"body\", old.\"created_at\", old.\"edited_at\", old.\"deleted_at\" FROM \"" << NAME
						  << mdb::Database::OLD_TABLE_SUFFIX << "\" AS old";
				} else {
					::mdb::Table::migrate(fromSchemaVersion, toSchemaVersion);
				}
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::MigrationException(
					std::string("Failed at migrating table \"") + NAME + "\" from schema version "
					+ std::to_string(fromSchemaVersion) + " to " + std::to_string(toSchemaVersion)));
			}
		}

	} // namespace db
} // namespace server
} // namespace mumble
