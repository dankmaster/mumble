// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatMessageAttachmentTable.h"
#include "ChatAssetTable.h"
#include "ChatMessageTable.h"

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

		constexpr const char *ChatMessageAttachmentTable::NAME;
		constexpr const char *ChatMessageAttachmentTable::column::server_id;
		constexpr const char *ChatMessageAttachmentTable::column::message_id;
		constexpr const char *ChatMessageAttachmentTable::column::asset_id;
		constexpr const char *ChatMessageAttachmentTable::column::display_order;
		constexpr const char *ChatMessageAttachmentTable::column::filename;
		constexpr const char *ChatMessageAttachmentTable::column::inline_safe;
		constexpr unsigned int ChatMessageAttachmentTable::INTRODUCED_IN_SCHEMA_VERSION;

		ChatMessageAttachmentTable::ChatMessageAttachmentTable(soci::session &sql, ::mdb::Backend backend,
															   const ChatMessageTable &messageTable,
															   const ChatAssetTable &assetTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column messageCol(column::message_id, ::mdb::DataType(::mdb::DataType::Integer));
			messageCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column assetCol(column::asset_id, ::mdb::DataType(::mdb::DataType::Integer));
			assetCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column orderCol(column::display_order, ::mdb::DataType(::mdb::DataType::Integer));
			orderCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column filenameCol(column::filename, ::mdb::DataType(::mdb::DataType::VarChar, 512));
			filenameCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column inlineCol(column::inline_safe, ::mdb::DataType(::mdb::DataType::SmallInteger));
			inlineCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			setColumns({ serverCol, messageCol, assetCol, orderCol, filenameCol, inlineCol });

			::mdb::PrimaryKey pk({ serverCol.getName(), messageCol.getName(), assetCol.getName() });
			setPrimaryKey(pk);

			::mdb::ForeignKey messageFK(messageTable, { serverCol, messageCol });
			addForeignKey(messageFK);
			::mdb::ForeignKey assetFK(assetTable, { serverCol, assetCol });
			addForeignKey(assetFK);

			::mdb::Index orderIndex(std::string(NAME) + "_message_order",
									{ column::server_id, column::message_id, column::display_order });
			addIndex(orderIndex, false);
		}

		void ChatMessageAttachmentTable::clearAttachments(unsigned int serverID, unsigned int messageID) {
			::mdb::TransactionHolder transaction = ensureTransaction();
			m_sql << "DELETE FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
				  << column::message_id << "\" = :messageID",
				soci::use(serverID), soci::use(messageID);
			transaction.commit();
		}

		void ChatMessageAttachmentTable::addAttachments(unsigned int serverID, unsigned int messageID,
														const std::vector< DBChatMessageAttachment > &attachments) {
			try {
				::mdb::TransactionHolder transaction = ensureTransaction();
				clearAttachments(serverID, messageID);
				for (const DBChatMessageAttachment &attachment : attachments) {
					const int inlineSafeValue = attachment.inlineSafe ? 1 : 0;
					m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \"" << column::message_id
						  << "\", \"" << column::asset_id << "\", \"" << column::display_order << "\", \""
						  << column::filename << "\", \"" << column::inline_safe
						  << "\") VALUES (:serverID, :messageID, :assetID, :displayOrder, :filename, :inlineSafe)",
						soci::use(serverID), soci::use(messageID), soci::use(attachment.assetID),
						soci::use(attachment.displayOrder), soci::use(attachment.filename),
						soci::use(inlineSafeValue);
				}
				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at storing chat attachments for message ID "
															  + std::to_string(messageID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		std::vector< DBChatMessageAttachment > ChatMessageAttachmentTable::getAttachments(unsigned int serverID,
																						   unsigned int messageID) {
			try {
				std::vector< DBChatMessageAttachment > attachments;
				soci::row row;
				::mdb::TransactionHolder transaction = ensureTransaction();
				soci::statement stmt =
					(m_sql.prepare
						 << "SELECT cma.\"" << column::asset_id << "\", cma.\"" << column::display_order << "\", cma.\""
						 << column::filename << "\", cma.\"" << column::inline_safe << "\", ca.\""
						 << ChatAssetTable::column::mime << "\", ca.\"" << ChatAssetTable::column::byte_size << "\", ca.\""
						 << ChatAssetTable::column::kind << "\", ca.\"" << ChatAssetTable::column::width << "\", ca.\""
						 << ChatAssetTable::column::height << "\", ca.\"" << ChatAssetTable::column::duration_ms
						 << "\", ca.\"" << ChatAssetTable::column::preview_asset_id << "\" FROM \"" << NAME << "\" cma "
						 << "JOIN \"" << ChatAssetTable::NAME << "\" ca ON ca.\"" << ChatAssetTable::column::server_id
						 << "\" = cma.\"" << column::server_id << "\" AND ca.\"" << ChatAssetTable::column::asset_id
						 << "\" = cma.\"" << column::asset_id << "\" WHERE cma.\"" << column::server_id
						 << "\" = :serverID AND cma.\"" << column::message_id << "\" = :messageID ORDER BY cma.\""
						 << column::display_order << "\" ASC",
						soci::use(serverID), soci::use(messageID), soci::into(row));

				stmt.execute(false);
				while (stmt.fetch()) {
					DBChatMessageAttachment attachment(serverID, messageID, static_cast< unsigned int >(row.get< int >(0)));
					attachment.displayOrder = static_cast< unsigned int >(row.get< int >(1));
					attachment.filename     = row.get< std::string >(2);
					attachment.inlineSafe   = row.get< int >(3) != 0;
					attachment.mime         = row.get< std::string >(4);
					attachment.byteSize     = static_cast< std::uint64_t >(row.get< long long >(5));
					attachment.kind         = static_cast< ChatAssetKind >(row.get< int >(6));
					attachment.width        = static_cast< unsigned int >(row.get< int >(7));
					attachment.height       = static_cast< unsigned int >(row.get< int >(8));
					attachment.durationMs   = static_cast< std::uint64_t >(row.get< long long >(9));
					if (row.get_indicator(10) == soci::i_ok) {
						attachment.previewAssetID = static_cast< unsigned int >(row.get< int >(10));
					}
					attachments.push_back(std::move(attachment));
				}

				transaction.commit();
				return attachments;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting chat attachments for message ID "
															  + std::to_string(messageID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		std::vector< unsigned int > ChatMessageAttachmentTable::getThreadIDsForAsset(unsigned int serverID,
																						 unsigned int assetID) {
			try {
				std::vector< unsigned int > threadIDs;
				soci::row row;
				::mdb::TransactionHolder transaction = ensureTransaction();
				soci::statement stmt =
					(m_sql.prepare
						 << "SELECT DISTINCT cm.\"" << ChatMessageTable::column::thread_id << "\" FROM \"" << NAME
						 << "\" cma JOIN \"" << ChatMessageTable::NAME << "\" cm ON cm.\""
						 << ChatMessageTable::column::server_id << "\" = cma.\"" << column::server_id << "\" AND cm.\""
						 << ChatMessageTable::column::message_id << "\" = cma.\"" << column::message_id
						 << "\" JOIN \"" << ChatAssetTable::NAME << "\" ca ON ca.\"" << ChatAssetTable::column::server_id
						 << "\" = cma.\"" << column::server_id << "\" AND ca.\""
						 << ChatAssetTable::column::asset_id << "\" = cma.\"" << column::asset_id << "\" WHERE cma.\""
						 << column::server_id << "\" = :serverID AND (cma.\"" << column::asset_id
						 << "\" = :assetID OR ca.\"" << ChatAssetTable::column::preview_asset_id << "\" = :assetID)",
						soci::use(serverID), soci::use(assetID), soci::into(row));

				stmt.execute(false);
				while (stmt.fetch()) {
					threadIDs.push_back(static_cast< unsigned int >(row.get< int >(0)));
				}

				transaction.commit();
				return threadIDs;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting thread IDs for chat asset with ID "
															  + std::to_string(assetID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		void ChatMessageAttachmentTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			(void) fromSchemaVersion;
			(void) toSchemaVersion;
		}

	} // namespace db
} // namespace server
} // namespace mumble
