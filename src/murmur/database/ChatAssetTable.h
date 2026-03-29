// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_CHATASSETTABLE_H_
#define MUMBLE_SERVER_DATABASE_CHATASSETTABLE_H_

#include "DBChatAsset.h"

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

		class ChatAssetTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "chat_assets";

			struct column {
				column() = delete;
				static constexpr const char *server_id = "server_id";
				static constexpr const char *asset_id = "asset_id";
				static constexpr const char *owner_user_id = "owner_user_id";
				static constexpr const char *owner_session = "owner_session";
				static constexpr const char *preview_asset_id = "preview_asset_id";
				static constexpr const char *sha256 = "sha256";
				static constexpr const char *storage_key = "storage_key";
				static constexpr const char *mime = "mime";
				static constexpr const char *byte_size = "byte_size";
				static constexpr const char *kind = "kind";
				static constexpr const char *width = "width";
				static constexpr const char *height = "height";
				static constexpr const char *duration_ms = "duration_ms";
				static constexpr const char *retention_class = "retention_class";
				static constexpr const char *created_at = "created_at";
				static constexpr const char *last_accessed_at = "last_accessed_at";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 16;

			ChatAssetTable(soci::session &sql, ::mumble::db::Backend backend, const ServerTable &serverTable,
						   const UserTable &userTable);
			~ChatAssetTable() = default;

			void addAsset(const DBChatAsset &asset);
			bool assetExists(unsigned int serverID, unsigned int assetID);
			DBChatAsset getAsset(unsigned int serverID, unsigned int assetID);
			void updatePreviewAssetID(unsigned int serverID, unsigned int assetID,
									 std::optional< unsigned int > previewAssetID);
			void touchAsset(unsigned int serverID, unsigned int assetID,
						   const std::chrono::system_clock::time_point &timepoint = std::chrono::system_clock::now());
			unsigned int getFreeAssetID(unsigned int serverID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_CHATASSETTABLE_H_
