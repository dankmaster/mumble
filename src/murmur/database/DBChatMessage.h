// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBCHATMESSAGE_H_
#define MUMBLE_SERVER_DATABASE_DBCHATMESSAGE_H_

#include <chrono>
#include <optional>
#include <string>

namespace mumble {
namespace server {
	namespace db {

		struct DBChatMessage {
			unsigned int serverID                          = {};
			unsigned int messageID                         = {};
			unsigned int threadID                          = {};
			std::optional< unsigned int > authorUserID     = {};
			std::optional< unsigned int > authorSession    = {};
			std::string body                               = {};
			std::chrono::system_clock::time_point createdAt = {};
			std::chrono::system_clock::time_point editedAt  = {};
			std::chrono::system_clock::time_point deletedAt = {};

			DBChatMessage() = default;
			DBChatMessage(unsigned int serverID, unsigned int messageID, unsigned int threadID);

			friend bool operator==(const DBChatMessage &lhs, const DBChatMessage &rhs);
			friend bool operator!=(const DBChatMessage &lhs, const DBChatMessage &rhs);
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBCHATMESSAGE_H_
