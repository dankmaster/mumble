// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatMessageReactionTable.h"
#include "ChatMessageTable.h"
#include "UserTable.h"

#include "database/AccessException.h"
#include "database/Column.h"
#include "database/Constraint.h"
#include "database/DataType.h"
#include "database/ForeignKey.h"
#include "database/Index.h"
#include "database/TransactionHolder.h"

#include <soci/soci.h>

#include <exception>

namespace mdb = ::mumble::db;

namespace mumble {
namespace server {
	namespace db {

		constexpr const char *ChatMessageReactionTable::NAME;
		constexpr const char *ChatMessageReactionTable::column::server_id;
		constexpr const char *ChatMessageReactionTable::column::message_id;
		constexpr const char *ChatMessageReactionTable::column::actor_user_id;
		constexpr const char *ChatMessageReactionTable::column::emoji;
		constexpr unsigned int ChatMessageReactionTable::INTRODUCED_IN_SCHEMA_VERSION;

		ChatMessageReactionTable::ChatMessageReactionTable(soci::session &sql, ::mdb::Backend backend,
														   const ChatMessageTable &messageTable, const UserTable &userTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column messageCol(column::message_id, ::mdb::DataType(::mdb::DataType::Integer));
			messageCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column actorUserCol(column::actor_user_id, ::mdb::DataType(::mdb::DataType::Integer));
			actorUserCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column emojiCol(column::emoji, ::mdb::DataType(::mdb::DataType::VarChar, 64));
			emojiCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			setColumns({ serverCol, messageCol, actorUserCol, emojiCol });

			::mdb::PrimaryKey pk(
				{ serverCol.getName(), messageCol.getName(), actorUserCol.getName(), emojiCol.getName() });
			setPrimaryKey(pk);

			::mdb::ForeignKey messageFK(messageTable, { serverCol, messageCol });
			addForeignKey(messageFK);
			::mdb::ForeignKey userFK(userTable, { serverCol, actorUserCol });
			addForeignKey(userFK);

			::mdb::Index messageIndex(std::string(NAME) + "_message", { column::server_id, column::message_id });
			addIndex(messageIndex, false);
			::mdb::Index actorMessageIndex(std::string(NAME) + "_actor_message",
										   { column::server_id, column::actor_user_id, column::message_id });
			addIndex(actorMessageIndex, false);
		}

		void ChatMessageReactionTable::setReactionActive(unsigned int serverID, unsigned int messageID,
														 unsigned int actorUserID, const std::string &emoji, bool active) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();

				int existing = 0;
				m_sql << "SELECT COUNT(*) FROM \"" << NAME << "\" WHERE \"" << column::server_id
					  << "\" = :serverID AND \"" << column::message_id << "\" = :messageID AND \""
					  << column::actor_user_id << "\" = :actorUserID AND \"" << column::emoji << "\" = :emoji",
					soci::use(serverID), soci::use(messageID), soci::use(actorUserID), soci::use(emoji),
					soci::into(existing);

				if (active && existing == 0) {
					m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \""
						  << column::message_id << "\", \"" << column::actor_user_id << "\", \"" << column::emoji
						  << "\") VALUES (:serverID, :messageID, :actorUserID, :emoji)",
						soci::use(serverID), soci::use(messageID), soci::use(actorUserID), soci::use(emoji);
				} else if (!active && existing > 0) {
					m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
						  << column::message_id << "\" = :messageID AND \"" << column::actor_user_id
						  << "\" = :actorUserID AND \"" << column::emoji << "\" = :emoji",
						soci::use(serverID), soci::use(messageID), soci::use(actorUserID), soci::use(emoji);
				}

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException(
					"Failed at updating reaction state for message ID " + std::to_string(messageID)
					+ " on server with ID " + std::to_string(serverID)));
			}
		}

		void ChatMessageReactionTable::clearReactions(unsigned int serverID, unsigned int messageID) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::message_id << "\" = :messageID",
					soci::use(serverID), soci::use(messageID);
				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at clearing chat reactions for message ID "
															  + std::to_string(messageID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		std::vector< DBChatMessageReaction > ChatMessageReactionTable::getReactions(unsigned int serverID,
																						unsigned int messageID) {
			try {
				std::vector< DBChatMessageReaction > reactions;
				soci::row row;
				::mdb::TransactionHolder transaction = ensureTransaction();
				soci::statement stmt = (m_sql.prepare
										  << "SELECT \"" << column::actor_user_id << "\", \"" << column::emoji
										  << "\" FROM \"" << NAME << "\" WHERE \"" << column::server_id
										  << "\" = :serverID AND \"" << column::message_id
										  << "\" = :messageID ORDER BY \"" << column::emoji << "\" ASC, \""
										  << column::actor_user_id << "\" ASC",
									  soci::use(serverID), soci::use(messageID), soci::into(row));

				stmt.execute(false);
				while (stmt.fetch()) {
					reactions.emplace_back(serverID, messageID, static_cast< unsigned int >(row.get< int >(0)),
										   row.get< std::string >(1));
				}

				transaction.commit();
				return reactions;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting chat reactions for message ID "
															  + std::to_string(messageID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		void ChatMessageReactionTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			(void) fromSchemaVersion;
			(void) toSchemaVersion;
		}

	} // namespace db
} // namespace server
} // namespace mumble
