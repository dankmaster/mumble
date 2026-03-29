// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBCHATMESSAGEEMBED_H_
#define MUMBLE_SERVER_DATABASE_DBCHATMESSAGEEMBED_H_

#include "ChatDataTypes.h"

#include <chrono>
#include <optional>
#include <string>

namespace mumble {
namespace server {
	namespace db {

		struct DBChatMessageEmbed {
			unsigned int serverID                        = {};
			unsigned int messageID                       = {};
			std::optional< unsigned int > previewAssetID = {};
			std::string urlHash                          = {};
			std::string canonicalUrl                     = {};
			std::string title                            = {};
			std::string description                      = {};
			std::string siteName                         = {};
			std::string errorCode                        = {};
			ChatEmbedStatus status                       = ChatEmbedStatus::Pending;
			std::chrono::system_clock::time_point fetchedAt = {};
			std::chrono::system_clock::time_point expiresAt = {};

			DBChatMessageEmbed() = default;
			DBChatMessageEmbed(unsigned int serverID, unsigned int messageID) : serverID(serverID), messageID(messageID) {}
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBCHATMESSAGEEMBED_H_
