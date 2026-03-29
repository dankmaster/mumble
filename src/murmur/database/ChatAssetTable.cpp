// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ChatAssetTable.h"
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

		constexpr const char *ChatAssetTable::NAME;
		constexpr const char *ChatAssetTable::column::server_id;
		constexpr const char *ChatAssetTable::column::asset_id;
		constexpr const char *ChatAssetTable::column::owner_user_id;
		constexpr const char *ChatAssetTable::column::owner_session;
		constexpr const char *ChatAssetTable::column::preview_asset_id;
		constexpr const char *ChatAssetTable::column::sha256;
		constexpr const char *ChatAssetTable::column::storage_key;
		constexpr const char *ChatAssetTable::column::mime;
		constexpr const char *ChatAssetTable::column::byte_size;
		constexpr const char *ChatAssetTable::column::kind;
		constexpr const char *ChatAssetTable::column::width;
		constexpr const char *ChatAssetTable::column::height;
		constexpr const char *ChatAssetTable::column::duration_ms;
		constexpr const char *ChatAssetTable::column::retention_class;
		constexpr const char *ChatAssetTable::column::created_at;
		constexpr const char *ChatAssetTable::column::last_accessed_at;
		constexpr unsigned int ChatAssetTable::INTRODUCED_IN_SCHEMA_VERSION;

		ChatAssetTable::ChatAssetTable(soci::session &sql, ::mdb::Backend backend, const ServerTable &serverTable,
									   const UserTable &userTable)
			: ::mdb::Table(sql, backend, NAME) {
			::mdb::Column serverCol(column::server_id, ::mdb::DataType(::mdb::DataType::Integer));
			serverCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column assetIDCol(column::asset_id, ::mdb::DataType(::mdb::DataType::Integer));
			assetIDCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column ownerUserCol(column::owner_user_id, ::mdb::DataType(::mdb::DataType::Integer));
			ownerUserCol.setDefaultValue("NULL");
			::mdb::Column ownerSessionCol(column::owner_session, ::mdb::DataType(::mdb::DataType::Integer));
			ownerSessionCol.setDefaultValue("NULL");
			::mdb::Column previewAssetCol(column::preview_asset_id, ::mdb::DataType(::mdb::DataType::Integer));
			previewAssetCol.setDefaultValue("NULL");
			::mdb::Column shaCol(column::sha256, ::mdb::DataType(::mdb::DataType::VarChar, 128));
			shaCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column storageCol(column::storage_key, ::mdb::DataType(::mdb::DataType::VarChar, 512));
			storageCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column mimeCol(column::mime, ::mdb::DataType(::mdb::DataType::VarChar, 255));
			mimeCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column byteSizeCol(column::byte_size, ::mdb::DataType(::mdb::DataType::Integer));
			byteSizeCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column kindCol(column::kind, ::mdb::DataType(::mdb::DataType::SmallInteger));
			kindCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column widthCol(column::width, ::mdb::DataType(::mdb::DataType::Integer));
			widthCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column heightCol(column::height, ::mdb::DataType(::mdb::DataType::Integer));
			heightCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column durationCol(column::duration_ms, ::mdb::DataType(::mdb::DataType::Integer));
			durationCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column retentionCol(column::retention_class, ::mdb::DataType(::mdb::DataType::SmallInteger));
			retentionCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column createdAtCol(column::created_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			createdAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));
			::mdb::Column accessedAtCol(column::last_accessed_at, ::mdb::DataType(::mdb::DataType::EpochTime));
			accessedAtCol.addConstraint(::mdb::Constraint(::mdb::Constraint::NotNull));

			setColumns({ serverCol, assetIDCol, ownerUserCol, ownerSessionCol, previewAssetCol, shaCol, storageCol,
						 mimeCol, byteSizeCol, kindCol, widthCol, heightCol, durationCol, retentionCol, createdAtCol,
						 accessedAtCol });

			::mdb::PrimaryKey pk({ serverCol.getName(), assetIDCol.getName() });
			setPrimaryKey(pk);

			::mdb::ForeignKey serverFK(serverTable, { serverCol });
			addForeignKey(serverFK);

			::mdb::ForeignKey ownerFK(userTable, { serverCol, ownerUserCol });
			addForeignKey(ownerFK);

			::mdb::Index shaIndex(std::string(NAME) + "_sha256", { column::server_id, column::sha256 });
			addIndex(shaIndex, false);
		}

		void ChatAssetTable::addAsset(const DBChatAsset &asset) {
			if (asset.sha256.empty() || asset.storageKey.empty() || asset.mime.empty()) {
				throw ::mdb::FormatException("A chat asset requires sha256, storage key, and mime");
			}

			try {
				unsigned int ownerUserID  = 0;
				unsigned int ownerSession = 0;
				unsigned int previewAssetID = 0;
				soci::indicator ownerUserInd = soci::i_null;
				soci::indicator ownerSessionInd = soci::i_null;
				soci::indicator previewAssetInd = soci::i_null;
				if (asset.ownerUserID) {
					ownerUserID  = asset.ownerUserID.value();
					ownerUserInd = soci::i_ok;
				}
				if (asset.ownerSession) {
					ownerSession    = asset.ownerSession.value();
					ownerSessionInd = soci::i_ok;
				}
				if (asset.previewAssetID) {
					previewAssetID  = asset.previewAssetID.value();
					previewAssetInd = soci::i_ok;
				}

				const std::size_t createdAt = toEpochSeconds(
					asset.createdAt == std::chrono::system_clock::time_point() ? std::chrono::system_clock::now()
																	: asset.createdAt);
				const std::size_t accessedAt =
					toEpochSeconds(asset.lastAccessedAt == std::chrono::system_clock::time_point()
									   ? std::chrono::system_clock::now()
									   : asset.lastAccessedAt);
				const unsigned int kindValue = static_cast< unsigned int >(asset.kind);
				const unsigned int retentionValue = static_cast< unsigned int >(asset.retentionClass);

				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "INSERT INTO \"" << NAME << "\" (\"" << column::server_id << "\", \"" << column::asset_id
					  << "\", \"" << column::owner_user_id << "\", \"" << column::owner_session << "\", \""
					  << column::preview_asset_id << "\", \"" << column::sha256 << "\", \"" << column::storage_key
					  << "\", \"" << column::mime << "\", \"" << column::byte_size << "\", \"" << column::kind
					  << "\", \"" << column::width << "\", \"" << column::height << "\", \"" << column::duration_ms
					  << "\", \"" << column::retention_class << "\", \"" << column::created_at << "\", \""
					  << column::last_accessed_at << "\") VALUES (:serverID, :assetID, :ownerUserID, :ownerSession, "
						 ":previewAssetID, :sha256, :storageKey, :mime, :byteSize, :kind, :width, :height, "
						 ":durationMs, :retentionClass, :createdAt, :lastAccessedAt)",
					soci::use(asset.serverID), soci::use(asset.assetID), soci::use(ownerUserID, ownerUserInd),
					soci::use(ownerSession, ownerSessionInd), soci::use(previewAssetID, previewAssetInd),
					soci::use(asset.sha256), soci::use(asset.storageKey), soci::use(asset.mime), soci::use(asset.byteSize),
					soci::use(kindValue), soci::use(asset.width), soci::use(asset.height), soci::use(asset.durationMs),
					soci::use(retentionValue), soci::use(createdAt), soci::use(accessedAt);
				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at adding chat asset with ID "
															  + std::to_string(asset.assetID) + " on server with ID "
															  + std::to_string(asset.serverID)));
			}
		}

		bool ChatAssetTable::assetExists(unsigned int serverID, unsigned int assetID) {
			try {
				int exists = false;
				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "SELECT 1 FROM \"" << NAME << "\" WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::asset_id << "\" = :assetID LIMIT 1",
					soci::use(serverID), soci::use(assetID), soci::into(exists);
				transaction.commit();
				return exists;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at checking existence of chat asset with ID "
															  + std::to_string(assetID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		DBChatAsset ChatAssetTable::getAsset(unsigned int serverID, unsigned int assetID) {
			try {
				DBChatAsset asset(serverID, assetID);
				unsigned int ownerUserID = 0;
				unsigned int ownerSession = 0;
				unsigned int previewAssetID = 0;
				unsigned int kindValue = 0;
				unsigned int retentionValue = 0;
				std::size_t createdAt = 0;
				std::size_t accessedAt = 0;
				soci::indicator ownerUserInd;
				soci::indicator ownerSessionInd;
				soci::indicator previewAssetInd;

				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "SELECT \"" << column::owner_user_id << "\", \"" << column::owner_session << "\", \""
					  << column::preview_asset_id << "\", \"" << column::sha256 << "\", \"" << column::storage_key
					  << "\", \"" << column::mime << "\", \"" << column::byte_size << "\", \"" << column::kind
					  << "\", \"" << column::width << "\", \"" << column::height << "\", \"" << column::duration_ms
					  << "\", \"" << column::retention_class << "\", \"" << column::created_at << "\", \""
					  << column::last_accessed_at << "\" FROM \"" << NAME << "\" WHERE \"" << column::server_id
					  << "\" = :serverID AND \"" << column::asset_id << "\" = :assetID",
					soci::use(serverID), soci::use(assetID), soci::into(ownerUserID, ownerUserInd),
					soci::into(ownerSession, ownerSessionInd), soci::into(previewAssetID, previewAssetInd),
					soci::into(asset.sha256), soci::into(asset.storageKey), soci::into(asset.mime),
					soci::into(asset.byteSize), soci::into(kindValue), soci::into(asset.width), soci::into(asset.height),
					soci::into(asset.durationMs), soci::into(retentionValue), soci::into(createdAt), soci::into(accessedAt);
				::mdb::utils::verifyQueryResultedInData(m_sql);
				transaction.commit();

				if (ownerUserInd == soci::i_ok) {
					asset.ownerUserID = ownerUserID;
				}
				if (ownerSessionInd == soci::i_ok) {
					asset.ownerSession = ownerSession;
				}
				if (previewAssetInd == soci::i_ok) {
					asset.previewAssetID = previewAssetID;
				}
				asset.kind = static_cast< ChatAssetKind >(kindValue);
				asset.retentionClass = static_cast< ChatAssetRetentionClass >(retentionValue);
				asset.createdAt = std::chrono::system_clock::time_point(std::chrono::seconds(createdAt));
				asset.lastAccessedAt = std::chrono::system_clock::time_point(std::chrono::seconds(accessedAt));
				return asset;
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting chat asset with ID "
															  + std::to_string(assetID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		void ChatAssetTable::updatePreviewAssetID(unsigned int serverID, unsigned int assetID,
												  std::optional< unsigned int > previewAssetID) {
			try {
				unsigned int previewAssetValue = 0;
				soci::indicator previewAssetInd = soci::i_null;
				if (previewAssetID) {
					previewAssetValue = previewAssetID.value();
					previewAssetInd   = soci::i_ok;
				}

				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "UPDATE \"" << NAME << "\" SET \"" << column::preview_asset_id
					  << "\" = :previewAssetID WHERE \"" << column::server_id << "\" = :serverID AND \""
					  << column::asset_id << "\" = :assetID",
					soci::use(previewAssetValue, previewAssetInd), soci::use(serverID), soci::use(assetID);
				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at updating preview asset for chat asset with ID "
															  + std::to_string(assetID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		void ChatAssetTable::touchAsset(unsigned int serverID, unsigned int assetID,
										const std::chrono::system_clock::time_point &timepoint) {
			try {
				const std::size_t accessedAt = toEpochSeconds(timepoint);
				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "UPDATE \"" << NAME << "\" SET \"" << column::last_accessed_at << "\" = :accessedAt WHERE \""
					  << column::server_id << "\" = :serverID AND \"" << column::asset_id << "\" = :assetID",
					soci::use(accessedAt), soci::use(serverID), soci::use(assetID);
				transaction.commit();
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at touching chat asset with ID "
															  + std::to_string(assetID) + " on server with ID "
															  + std::to_string(serverID)));
			}
		}

		unsigned int ChatAssetTable::getFreeAssetID(unsigned int serverID) {
			try {
				int maxAssetID = 0;
				soci::indicator indicator;
				::mdb::TransactionHolder transaction = ensureTransaction();
				m_sql << "SELECT MAX(\"" << column::asset_id << "\") FROM \"" << NAME << "\" WHERE \""
					  << column::server_id << "\" = :serverID",
					soci::use(serverID), soci::into(maxAssetID, indicator);
				transaction.commit();
				return indicator == soci::i_null ? 1U : static_cast< unsigned int >(maxAssetID + 1);
			} catch (const soci::soci_error &) {
				std::throw_with_nested(::mdb::AccessException("Failed at getting free chat asset ID on server with ID "
															  + std::to_string(serverID)));
			}
		}

		void ChatAssetTable::migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) {
			(void) fromSchemaVersion;
			(void) toSchemaVersion;
		}

	} // namespace db
} // namespace server
} // namespace mumble
