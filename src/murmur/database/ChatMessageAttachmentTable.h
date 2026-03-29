// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_CHATMESSAGEATTACHMENTTABLE_H_
#define MUMBLE_SERVER_DATABASE_CHATMESSAGEATTACHMENTTABLE_H_

#include "DBChatMessageAttachment.h"

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

		class ChatMessageAttachmentTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "chat_message_attachments";

			struct column {
				column() = delete;
				static constexpr const char *server_id = "server_id";
				static constexpr const char *message_id = "message_id";
				static constexpr const char *asset_id = "asset_id";
				static constexpr const char *display_order = "display_order";
				static constexpr const char *filename = "filename";
				static constexpr const char *inline_safe = "inline_safe";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 16;

			ChatMessageAttachmentTable(soci::session &sql, ::mumble::db::Backend backend,
									   const ChatMessageTable &messageTable, const ChatAssetTable &assetTable);
			~ChatMessageAttachmentTable() = default;

			void clearAttachments(unsigned int serverID, unsigned int messageID);
			void addAttachments(unsigned int serverID, unsigned int messageID,
							   const std::vector< DBChatMessageAttachment > &attachments);
			std::vector< DBChatMessageAttachment > getAttachments(unsigned int serverID, unsigned int messageID);
			std::vector< unsigned int > getThreadIDsForAsset(unsigned int serverID, unsigned int assetID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_CHATMESSAGEATTACHMENTTABLE_H_
