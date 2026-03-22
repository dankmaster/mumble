// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatThreadTable.h"
#include "ChronoUtils.h"
#include "ServerTable.h"
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

#include <cassert>
#include <exception>

namespace mdb = ::mumble::db;

namespace mumble {
namespace server {
	namespace db {
		namespace {
			ChatThreadScope decodeThreadScope(unsigned int scopeValue) {
				switch (scopeValue) {
					case static_cast< unsigned int >(ChatThreadScope::Channel):
						return ChatThreadScope::Channel;
					case static_cast< unsigned int >(ChatThreadScope::ServerGlobal):
						return ChatThreadScope::ServerGlobal;
					case static_cast< unsigned int >(ChatThreadScope::Private):
						return ChatThreadScope::Private;
					default:
						throw ::mdb::FormatException("Encountered invalid chat thread scope value "
													 + std::to_string(scopeValue));
				}
			}
		} // namespace

		constexpr const char *ChatThreadTable::NAME;
		constexpr const char *ChatThreadTable::column::server_id;
		constexpr const char *ChatThreadTable::column::thread_id;
		constexpr const char *ChatThreadTable::column::thread_scope;
		constexpr const char *ChatThreadTable::column::scope_key;
		constexpr const char *ChatThreadTable::column::created_by_user_id;
		constexpr const char *ChatThreadTable::column::created_at;
		constexpr const char *ChatThreadTable::column::updated_at;
		constexpr unsigned int ChatThreadTable::INTRODUCED_IN_SCHEMA_VERSION;

		ChatThreadTable::ChatThreadTable(soci::session &sql, ::mdb::Backend backend, const ServerTable &serverTable,
										 const UserTable &userTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column threadIDCol(column::thread_id, ::mdb::DataType(::mdb::DataType::Integer));
			threadIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column scopeCol(column::thread_scope, ::mdb::DataType(::mdb::DataType::SmallInteger));
			scopeCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column scopeKeyCol(column::scope_key, ::mdb::DataType(::mdb::DataType::VarChar, 255));
			scopeKeyCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column creatorCol(column::created_by_user_id, ::mdb::DataType(::mdb::DataType::Integer));
			creatorCol.setDefaultValue("NULL");

			::mdb::Column createdAtCol(column::created_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			createdAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column updatedAtCol(column::updated_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			updatedAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			setColumns({ serverCol, threadIDCol, scopeCol, scopeKeyCol, creatorCol, createdAtCol, updatedAtCol });

			::mdb::PrimaryKey pk({ serverCol.getName(), threadIDCol.getName() });
			setPrimaryKey(pk);

			::mdb::ForeignKey serverFK(serverTable, { serverCol });
			addForeignKey(serverFK);

			::mdb::ForeignKey creatorFK(userTable, { serverCol, creatorCol });
			addForeignKey(creatorFK);

			::mdb::Index uniqueScopeIndex(std::string(NAME) + "_unique_scope",
										  { column::server_id, column::thread_scope, column::scope_key },
										  ::mdb::Index::UNIQUE);
			addIndex(uniqueScopeIndex, false);
		}

		void ChatThreadTable::addThread(const DBChatThread &thread) {
			if (thread.scopeKey.empty()) {
				throw ::mdb::FormatException("A chat thread requires a non-empty scope key");
			}

			try {
				unsigned int createdByUserID = 0;
				soci::indicator creatorInd   = soci::i_null;

				if (thread.createdByUserID) {
					createdByUserID = thread.createdByUserID.value();
					creatorInd      = soci::i_ok;
				}

				auto createdAt = thread.createdAt;
				if (createdAt == std::chrono::system_clock::time_point()) {
					createdAt = std::chrono::system_clock::now();
				}

				auto updatedAt = thread.updatedAt;
				if (updatedAt == std::chrono::system_clock::time_point()) {
					updatedAt = createdAt;
				}

				std::size_t createdAtEpoch = toEpochSeconds(createdAt);
				std::size_t updatedAtEpoch = toEpochSeconds(updatedAt);
				unsigned int scopeValue    = static_cast< unsigned int >(thread.scope);

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \"" << column::thread_id
					  << "\", \"" << column::thread_scope << "\", \"" << column::scope_key << "\", \""
					  << column::created_by_user_id << "\", \"" << column::created_at << "\", \"" << column::updated_at
					  << "\") VALUES (:serverID, :threadID, :scope, :scopeKey, :createdByUserID, :createdAt, :updatedAt)",
					soci::use(thread.serverID), soci::use(thread.threadID), soci::use(scopeValue),
					soci::use(thread.scopeKey), soci::use(createdByUserID, creatorInd), soci::use(createdAtEpoch),
					soci::use(updatedAtEpoch);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at adding chat thread with ID "
															  + std::to_string(thread.threadID) + " on server with ID "
															  + std::to_string(thread.serverID)));
			}
		}

		bool ChatThreadTable::threadExists(unsigned int serverID, unsigned int threadID) {
			try {
				int exists = false;

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "SELECT 1 FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::thread_id << "\" = :threadID LIMIT 1",
					soci::use(serverID), soci::use(threadID), soci::into(exists);

				transaction.commit();

				return exists;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at checking for existence of chat thread with ID "
															  + std::to_string(threadID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		DBChatThread ChatThreadTable::getThread(unsigned int serverID, unsigned int threadID) {
			try {
				unsigned int scopeValue     = 0;
				unsigned int createdByUserID = 0;
				std::size_t createdAt       = 0;
				std::size_t updatedAt       = 0;
				std::string scopeKey;
				soci::indicator creatorInd;

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "SELECT \"" << column::thread_scope << "\", \"" << column::scope_key << "\", \""
					  << column::created_by_user_id << "\", \"" << column::created_at << "\", \"" << column::updated_at
					  << "\" FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::thread_id << "\" = :threadID",
					soci::use(serverID), soci::use(threadID), soci::into(scopeValue), soci::into(scopeKey),
					soci::into(createdByUserID, creatorInd), soci::into(createdAt), soci::into(updatedAt);

				::mdb::utils::verifyQueryResultedInData(m_sql);

				transaction.commit();

				DBChatThread thread(serverID, threadID);
				thread.scope     = decodeThreadScope(scopeValue);
				thread.scopeKey  = std::move(scopeKey);
				thread.createdAt = std::chrono::system_clock::time_point(std::chrono::seconds(createdAt));
				thread.updatedAt = std::chrono::system_clock::time_point(std::chrono::seconds(updatedAt));
				if (creatorInd == soci::i_ok) {
					thread.createdByUserID = createdByUserID;
				}

				return thread;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting chat thread with ID "
															  + std::to_string(threadID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		std::optional< DBChatThread > ChatThreadTable::getThreadByScope(unsigned int serverID, ChatThreadScope scope,
																		const std::string &scopeKey) {
			try {
				unsigned int threadID       = 0;
				unsigned int createdByUserID = 0;
				std::size_t createdAt       = 0;
				std::size_t updatedAt       = 0;
				soci::indicator creatorInd;
				unsigned int scopeValue = static_cast< unsigned int >(scope);

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "SELECT \"" << column::thread_id << "\", \"" << column::created_by_user_id << "\", \""
					  << column::created_at << "\", \"" << column::updated_at << "\" FROM \"" << NAME << "\" WHERE \""
					  << column::server_id << "\" = :serverID AND \"" << column::thread_scope << "\" = :scope AND \""
					  << column::scope_key << "\" = :scopeKey LIMIT 1",
					soci::use(serverID), soci::use(scopeValue), soci::use(scopeKey), soci::into(threadID),
					soci::into(createdByUserID, creatorInd), soci::into(createdAt), soci::into(updatedAt);

				transaction.commit();

				if (!m_sql.got_data()) {
					return std::nullopt;
				}

				DBChatThread thread(serverID, threadID);
				thread.scope     = scope;
				thread.scopeKey  = scopeKey;
				thread.createdAt = std::chrono::system_clock::time_point(std::chrono::seconds(createdAt));
				thread.updatedAt = std::chrono::system_clock::time_point(std::chrono::seconds(updatedAt));
				if (creatorInd == soci::i_ok) {
					thread.createdByUserID = createdByUserID;
				}

				return thread;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at searching for chat thread on server with ID "
															  + std::to_string(serverID) + " for scope key \""
															  + scopeKey + "\""));
			}
		}

		void ChatThreadTable::touchThread(unsigned int serverID, unsigned int threadID,
										  const std::chrono::system_clock::time_point &timepoint) {
			try {
				std::size_t updatedAt = toEpochSeconds(timepoint);

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "UPDATE \"" << NAME << "\" SET \"" << column::updated_at << "\" = :updatedAt WHERE \""
					  << column::server_id << "\" = :serverID AND \"" << column::thread_id << "\" = :threadID",
					soci::use(updatedAt), soci::use(serverID), soci::use(threadID);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at touching chat thread with ID "
															  + std::to_string(threadID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		unsigned int ChatThreadTable::getFreeThreadID(unsigned int serverID) {
			try {
				unsigned int id = 0;

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << ::mdb::utils::getLowestUnoccupiedIDStatement(
					m_backend, NAME, column::thread_id, { ::mdb::utils::ColAlias(column::server_id, "serverID") }),
					soci::use(serverID, "serverID"), soci::into(id);

				::mdb::utils::verifyQueryResultedInData(m_sql);

				transaction.commit();

				return id;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting free chat thread ID on server with ID "
															  + std::to_string(serverID)));
			}
		}

		void ChatThreadTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			assert(fromSchemaVersion <= toSchemaVersion);

			if (fromSchemaVersion < INTRODUCED_IN_SCHEMA_VERSION) {
				return;
			}

			::mdb::Table::migrate(fromSchemaVersion, toSchemaVersion);
		}

	} // namespace db
} // namespace server
} // namespace mumble
