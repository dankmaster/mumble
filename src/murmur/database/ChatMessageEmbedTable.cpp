// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatMessageEmbedTable.h"
#include "ChatAssetTable.h"
#include "ChatMessageTable.h"
#include "ChronoUtils.h"

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

		constexpr const char *ChatMessageEmbedTable::NAME;
		constexpr const char *ChatMessageEmbedTable::column::server_id;
		constexpr const char *ChatMessageEmbedTable::column::message_id;
		constexpr const char *ChatMessageEmbedTable::column::url_hash;
		constexpr const char *ChatMessageEmbedTable::column::canonical_url;
		constexpr const char *ChatMessageEmbedTable::column::title;
		constexpr const char *ChatMessageEmbedTable::column::description;
		constexpr const char *ChatMessageEmbedTable::column::site_name;
		constexpr const char *ChatMessageEmbedTable::column::preview_asset_id;
		constexpr const char *ChatMessageEmbedTable::column::status;
		constexpr const char *ChatMessageEmbedTable::column::fetched_at;
		constexpr const char *ChatMessageEmbedTable::column::expires_at;
		constexpr const char *ChatMessageEmbedTable::column::error_code;
		constexpr unsigned int ChatMessageEmbedTable::INTRODUCED_IN_SCHEMA_VERSION;

		ChatMessageEmbedTable::ChatMessageEmbedTable(soci::session &sql, ::mdb::Backend backend,
													 const ChatMessageTable &messageTable, const ChatAssetTable &assetTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column messageCol(column::message_id, ::mdb::DataType(::mdb::DataType::Integer));
			messageCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column hashCol(column::url_hash, ::mdb::DataType(::mdb::DataType::VarChar, 128));
			hashCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column urlCol(column::canonical_url, ::mdb::DataType(::mdb::DataType::VarChar, 1024));
			urlCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column titleCol(column::title, ::mdb::DataType(::mdb::DataType::VarChar, 512));
			titleCol.setDefaultValue("''");
			::mdb::Column descCol(column::description, ::mdb::DataType(::mdb::DataType::Text));
			::mdb::Column siteCol(column::site_name, ::mdb::DataType(::mdb::DataType::VarChar, 255));
			siteCol.setDefaultValue("''");
			::mdb::Column previewCol(column::preview_asset_id, ::mdb::DataType(::mdb::DataType::Integer));
			previewCol.setDefaultValue("NULL");
			::mdb::Column statusCol(column::status, ::mdb::DataType(::mdb::DataType::SmallInteger));
			statusCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column fetchedCol(column::fetched_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			fetchedCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column expiresCol(column::expires_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			expiresCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column errorCol(column::error_code, ::mdb::DataType(::mdb::DataType::VarChar, 255));
			errorCol.setDefaultValue("''");

			setColumns({ serverCol, messageCol, hashCol, urlCol, titleCol, descCol, siteCol, previewCol, statusCol,
						 fetchedCol, expiresCol, errorCol });

			::mdb::PrimaryKey pk({ serverCol.getName(), messageCol.getName(), hashCol.getName() });
			setPrimaryKey(pk);

			::mdb::ForeignKey messageFK(messageTable, { serverCol, messageCol });
			addForeignKey(messageFK);
			::mdb::ForeignKey previewFK(assetTable, { serverCol, previewCol });
			addForeignKey(previewFK);

			::mdb::Index messageIndex(std::string(NAME) + "_message", { column::server_id, column::message_id });
			addIndex(messageIndex, false);
		}

		void ChatMessageEmbedTable::clearEmbeds(unsigned int serverID, unsigned int messageID) {
			::mdb::TransactionHolder transaction = ensureTransaction();
			m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
				  << column::message_id << "\" = :messageID",
				soci::use(serverID), soci::use(messageID);
			transaction.commit();
		}

		void ChatMessageEmbedTable::setEmbeds(unsigned int serverID, unsigned int messageID,
											 const std::vector< DBChatMessageEmbed > &embeds) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();
				clearEmbeds(serverID, messageID);
				for (const DBChatMessageEmbed &embed : embeds) {
					unsigned int previewAssetID = 0;
					soci::indicator previewInd  = soci::i_null;
					if (embed.previewAssetID) {
						previewAssetID = embed.previewAssetID.value();
						previewInd     = soci::i_ok;
					}
					const unsigned int statusValue = static_cast< unsigned int >(embed.status);
					const std::size_t fetchedAt = toEpochSeconds(embed.fetchedAt);
					const std::size_t expiresAt = toEpochSeconds(embed.expiresAt);
					m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \"" << column::message_id
						  << "\", \"" << column::url_hash << "\", \"" << column::canonical_url << "\", \"" << column::title
						  << "\", \"" << column::description << "\", \"" << column::site_name << "\", \""
						  << column::preview_asset_id << "\", \"" << column::status << "\", \"" << column::fetched_at
						  << "\", \"" << column::expires_at << "\", \"" << column::error_code
						  << "\") VALUES (:serverID, :messageID, :urlHash, :canonicalUrl, :title, :description, :siteName, "
							 ":previewAssetID, :status, :fetchedAt, :expiresAt, :errorCode)",
						soci::use(serverID), soci::use(messageID), soci::use(embed.urlHash), soci::use(embed.canonicalUrl),
						soci::use(embed.title), soci::use(embed.description), soci::use(embed.siteName),
						soci::use(previewAssetID, previewInd), soci::use(statusValue), soci::use(fetchedAt),
						soci::use(expiresAt), soci::use(embed.errorCode);
				}
				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at storing chat embeds for message ID "
															  + std::to_string(messageID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		std::vector< DBChatMessageEmbed > ChatMessageEmbedTable::getEmbeds(unsigned int serverID, unsigned int messageID) {
			try {
				std::vector< DBChatMessageEmbed > embeds;
				soci::row row;
				::mdb::TransactionHolder transaction = ensureTransaction();
				soci::statement stmt = (m_sql.prepare
										  << "SELECT \"" << column::url_hash << "\", \"" << column::canonical_url << "\", \""
										  << column::title << "\", \"" << column::description << "\", \"" << column::site_name
										  << "\", \"" << column::preview_asset_id << "\", \"" << column::status << "\", \""
										  << column::fetched_at << "\", \"" << column::expires_at << "\", \""
										  << column::error_code << "\" FROM \"" << NAME << "\" WHERE \""
										  << column::server_id << "\" = :serverID AND \"" << column::message_id
										  << "\" = :messageID ORDER BY \"" << column::url_hash << "\" ASC",
									  soci::use(serverID), soci::use(messageID), soci::into(row));

				stmt.execute(false);
				while (stmt.fetch()) {
					DBChatMessageEmbed embed(serverID, messageID);
					embed.urlHash      = row.get< std::string >(0);
					embed.canonicalUrl = row.get< std::string >(1);
					embed.title        = row.get< std::string >(2);
					embed.description  = row.get< std::string >(3);
					embed.siteName     = row.get< std::string >(4);
					if (row.get_indicator(5) == soci::i_ok) {
						embed.previewAssetID = static_cast< unsigned int >(row.get< int >(5));
					}
					embed.status    = static_cast< ChatEmbedStatus >(row.get< int >(6));
					embed.fetchedAt = std::chrono::system_clock::time_point(std::chrono::seconds(row.get< long long >(7)));
					embed.expiresAt = std::chrono::system_clock::time_point(std::chrono::seconds(row.get< long long >(8)));
					embed.errorCode = row.get< std::string >(9);
					embeds.push_back(std::move(embed));
				}

				transaction.commit();
				return embeds;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting chat embeds for message ID "
															  + std::to_string(messageID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		std::vector< unsigned int > ChatMessageEmbedTable::getThreadIDsForPreviewAsset(unsigned int serverID,
																						   unsigned int assetID) {
			try {
				std::vector< unsigned int > threadIDs;
				soci::row row;
				::mdb::TransactionHolder transaction = ensureTransaction();
				soci::statement stmt =
					(m_sql.prepare
						 << "SELECT DISTINCT cm.\"" << ChatMessageTable::column::thread_id << "\" FROM \"" << NAME
						 << "\" cme JOIN \"" << ChatMessageTable::NAME << "\" cm ON cm.\""
						 << ChatMessageTable::column::server_id << "\" = cme.\"" << column::server_id << "\" AND cm.\""
						 << ChatMessageTable::column::message_id << "\" = cme.\"" << column::message_id << "\" WHERE cme.\""
						 << column::server_id << "\" = :serverID AND cme.\"" << column::preview_asset_id
						 << "\" = :assetID",
						soci::use(serverID), soci::use(assetID), soci::into(row));

				stmt.execute(false);
				while (stmt.fetch()) {
					threadIDs.push_back(static_cast< unsigned int >(row.get< int >(0)));
				}

				transaction.commit();
				return threadIDs;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException(
					"Failed at getting thread IDs for chat embed preview asset with ID " + std::to_string(assetID)
					+ " on server with ID " + std::to_string(serverID)));
			}
		}

		void ChatMessageEmbedTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			(void) fromSchemaVersion;
			(void) toSchemaVersion;
		}

	} // namespace db
} // namespace server
} // namespace mumble
