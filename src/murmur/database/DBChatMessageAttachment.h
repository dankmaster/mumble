// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBCHATMESSAGEATTACHMENT_H_
#define MUMBLE_SERVER_DATABASE_DBCHATMESSAGEATTACHMENT_H_

#include "ChatDataTypes.h"

#include <cstdint>
#include <optional>
#include <string>

namespace mumble {
namespace server {
	namespace db {

		struct DBChatMessageAttachment {
			unsigned int serverID                        = {};
			unsigned int messageID                       = {};
			unsigned int assetID                         = {};
			unsigned int displayOrder                    = {};
			std::optional< unsigned int > previewAssetID = {};
			std::string filename                         = {};
			std::string mime                             = {};
			std::uint64_t byteSize                       = {};
			ChatAssetKind kind                           = ChatAssetKind::Unknown;
			unsigned int width                           = {};
			unsigned int height                          = {};
			std::uint64_t durationMs                     = {};
			bool inlineSafe                              = false;

			DBChatMessageAttachment() = default;
			DBChatMessageAttachment(unsigned int serverID, unsigned int messageID, unsigned int assetID)
				: serverID(serverID), messageID(messageID), assetID(assetID) {}
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBCHATMESSAGEATTACHMENT_H_
