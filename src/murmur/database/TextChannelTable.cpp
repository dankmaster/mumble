// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "TextChannelTable.h"
#include "ChannelTable.h"
#include "ServerTable.h"

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

		constexpr const char *TextChannelTable::NAME;
		constexpr const char *TextChannelTable::column::server_id;
		constexpr const char *TextChannelTable::column::text_channel_id;
		constexpr const char *TextChannelTable::column::name;
		constexpr const char *TextChannelTable::column::description;
		constexpr const char *TextChannelTable::column::acl_channel_id;
		constexpr const char *TextChannelTable::column::position;
		constexpr unsigned int TextChannelTable::INTRODUCED_IN_SCHEMA_VERSION;

		TextChannelTable::TextChannelTable(soci::session &sql, ::mdb::Backend backend, const ServerTable &serverTable,
										   const ChannelTable &channelTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column textChannelIDCol(column::text_channel_id, ::mdb::DataType(::mdb::DataType::Integer));
			textChannelIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column nameCol(column::name, ::mdb::DataType(::mdb::DataType::VarChar, 255));
			nameCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column descriptionCol(column::description, ::mdb::DataType(::mdb::DataType::Text));
			descriptionCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column aclChannelIDCol(column::acl_channel_id, ::mdb::DataType(::mdb::DataType::Integer));
			aclChannelIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			::mdb::Column positionCol(column::position, ::mdb::DataType(::mdb::DataType::Integer));
			positionCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			setColumns({ serverCol, textChannelIDCol, nameCol, descriptionCol, aclChannelIDCol, positionCol });

			::mdb::PrimaryKey pk({ serverCol.getName(), textChannelIDCol.getName() });
			setPrimaryKey(pk);

			::mdb::ForeignKey serverFK(serverTable, { serverCol });
			addForeignKey(serverFK);

			::mdb::ForeignKey aclChannelFK(channelTable, { serverCol, aclChannelIDCol });
			addForeignKey(aclChannelFK);

			addIndex(::mdb::Index(std::string(NAME) + "_position", { column::server_id, column::position }), false);
			addIndex(::mdb::Index(std::string(NAME) + "_name", { column::server_id, column::name }, ::mdb::Index::UNIQUE),
					 false);
		}

		void TextChannelTable::addTextChannel(const DBTextChannel &textChannel) {
			if (textChannel.name.empty()) {
				throw ::mdb::FormatException("A text channel requires a non-empty name");
			}

			try {
				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \""
					  << column::text_channel_id << "\", \"" << column::name << "\", \"" << column::description
					  << "\", \"" << column::acl_channel_id << "\", \"" << column::position
					  << "\") VALUES (:serverID, :textChannelID, :name, :description, :aclChannelID, :position)",
					soci::use(textChannel.serverID), soci::use(textChannel.textChannelID), soci::use(textChannel.name),
					soci::use(textChannel.description), soci::use(textChannel.aclChannelID), soci::use(textChannel.position);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at adding text channel with ID "
															  + std::to_string(textChannel.textChannelID)
															  + " on server with ID "
															  + std::to_string(textChannel.serverID)));
			}
		}

		void TextChannelTable::updateTextChannel(const DBTextChannel &textChannel) {
			if (textChannel.name.empty()) {
				throw ::mdb::FormatException("A text channel requires a non-empty name");
			}

			try {
				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "UPDATE \"" << NAME << "\" SET \"" << column::name << "\" = :name, \"" << column::description
					  << "\" = :description, \"" << column::acl_channel_id << "\" = :aclChannelID, \"" << column::position
					  << "\" = :position WHERE \"" << column::server_id << "\" = :serverID AND \"" << column::text_channel_id
					  << "\" = :textChannelID",
					soci::use(textChannel.name), soci::use(textChannel.description), soci::use(textChannel.aclChannelID),
					soci::use(textChannel.position), soci::use(textChannel.serverID), soci::use(textChannel.textChannelID);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at updating text channel with ID "
															  + std::to_string(textChannel.textChannelID)
															  + " on server with ID "
															  + std::to_string(textChannel.serverID)));
			}
		}

		void TextChannelTable::removeTextChannel(unsigned int serverID, unsigned int textChannelID) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::text_channel_id << "\" = :textChannelID",
					soci::use(serverID), soci::use(textChannelID);

				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at removing text channel with ID "
															  + std::to_string(textChannelID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		bool TextChannelTable::textChannelExists(unsigned int serverID, unsigned int textChannelID) {
			try {
				int exists = false;

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "SELECT 1 FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::text_channel_id << "\" = :textChannelID LIMIT 1",
					soci::use(serverID), soci::use(textChannelID), soci::into(exists);

				transaction.commit();

				return exists;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at checking for existence of text channel with ID "
															  + std::to_string(textChannelID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		std::optional< DBTextChannel > TextChannelTable::getTextChannel(unsigned int serverID, unsigned int textChannelID) {
			try {
				DBTextChannel textChannel(serverID, textChannelID);

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "SELECT \"" << column::name << "\", \"" << column::description << "\", \""
					  << column::acl_channel_id << "\", \"" << column::position << "\" FROM \"" << NAME << "\" WHERE \""
					  << column::server_id << "\" = :serverID AND \"" << column::text_channel_id
					  << "\" = :textChannelID LIMIT 1",
					soci::into(textChannel.name), soci::into(textChannel.description),
					soci::into(textChannel.aclChannelID), soci::into(textChannel.position), soci::use(serverID),
					soci::use(textChannelID);

				transaction.commit();

				if (!m_sql.got_data()) {
					return std::nullopt;
				}

				return textChannel;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting text channel with ID "
															  + std::to_string(textChannelID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		std::vector< DBTextChannel > TextChannelTable::getTextChannels(unsigned int serverID) {
			try {
				std::vector< DBTextChannel > textChannels;
				soci::row row;

				::mdb::TransactionHolder transaction = ensureTransaction();

				soci::statement stmt =
					(m_sql.prepare << "SELECT \"" << column::text_channel_id << "\", \"" << column::name << "\", \""
								   << column::description << "\", \"" << column::acl_channel_id << "\", \""
								   << column::position << "\" FROM \"" << NAME << "\" WHERE \"" << column::server_id
								   << "\" = :serverID ORDER BY \"" << column::position << "\" ASC, \""
								   << column::text_channel_id << "\" ASC",
					 soci::use(serverID), soci::into(row));

				stmt.execute(false);

				while (stmt.fetch()) {
					assert(row.size() == 5);

					DBTextChannel textChannel(serverID, static_cast< unsigned int >(row.get< int >(0)));
					textChannel.name         = row.get< std::string >(1);
					textChannel.description  = row.get< std::string >(2);
					textChannel.aclChannelID = static_cast< unsigned int >(row.get< int >(3));
					textChannel.position     = static_cast< unsigned int >(row.get< int >(4));

					textChannels.push_back(std::move(textChannel));
				}

				transaction.commit();

				return textChannels;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting text channels for server with ID "
															  + std::to_string(serverID)));
			}
		}

		unsigned int TextChannelTable::getFreeTextChannelID(unsigned int serverID) {
			try {
				unsigned int id = 0;

				::mdb::TransactionHolder transaction = ensureTransaction();

				m_sql << "SELECT COALESCE(MAX(\"" << column::text_channel_id << "\"), -1) + 1 FROM \"" << NAME
					  << "\" WHERE \"" << column::server_id << "\" = :serverID",
					soci::use(serverID, "serverID"), soci::into(id);

				transaction.commit();
				::mdb::utils::verifyQueryResultedInData(m_sql);

				return id;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at fetching a free text channel ID for server with ID "
															  + std::to_string(serverID)));
			}
		}

		void TextChannelTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			assert(fromSchemaVersion <= toSchemaVersion);

			if (fromSchemaVersion < INTRODUCED_IN_SCHEMA_VERSION) {
				return;
			}

			::mdb::Table::migrate(fromSchemaVersion, toSchemaVersion);
		}

	} // namespace db
} // namespace server
} // namespace mumble
