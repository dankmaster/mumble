// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_CHATMESSAGEEMBEDTABLE_H_
#define MUMBLE_SERVER_DATABASE_CHATMESSAGEEMBEDTABLE_H_

#include "DBChatMessageEmbed.h"

#include "database/Backend.h"
#include "database/Table.h"

#include <vector>

namespace soci {
class session;
}

namespace mumble {
namespace server {
	namespace db {

		class ChatMessageTable;
		class ChatAssetTable;

		class ChatMessageEmbedTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "chat_message_embeds";

			struct column {
				column() = delete;
				static constexpr const char *server_id = "server_id";
				static constexpr const char *message_id = "message_id";
				static constexpr const char *url_hash = "url_hash";
				static constexpr const char *canonical_url = "canonical_url";
				static constexpr const char *title = "title";
				static constexpr const char *description = "description";
				static constexpr const char *site_name = "site_name";
				static constexpr const char *preview_asset_id = "preview_asset_id";
				static constexpr const char *status = "status";
				static constexpr const char *fetched_at = "fetched_at";
				static constexpr const char *expires_at = "expires_at";
				static constexpr const char *error_code = "error_code";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 16;

			ChatMessageEmbedTable(soci::session &sql, ::mumble::db::Backend backend, const ChatMessageTable &messageTable,
								  const ChatAssetTable &assetTable);
			~ChatMessageEmbedTable() = default;

			void clearEmbeds(unsigned int serverID, unsigned int messageID);
			void setEmbeds(unsigned int serverID, unsigned int messageID,
						  const std::vector< DBChatMessageEmbed > &embeds);
			std::vector< DBChatMessageEmbed > getEmbeds(unsigned int serverID, unsigned int messageID);
			std::vector< unsigned int > getThreadIDsForPreviewAsset(unsigned int serverID, unsigned int assetID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_CHATMESSAGEEMBEDTABLE_H_
