// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBCHATTHREAD_H_
#define MUMBLE_SERVER_DATABASE_DBCHATTHREAD_H_

#include <chrono>
#include <optional>
#include <string>

namespace mumble {
namespace server {
	namespace db {

		enum class ChatThreadScope : unsigned int {
			Channel      = 0,
			ServerGlobal = 1,
			Private      = 2,
			TextChannel  = 3,
		};

		struct DBChatThread {
			unsigned int serverID                 = {};
			unsigned int threadID                 = {};
			ChatThreadScope scope                 = ChatThreadScope::Channel;
			std::string scopeKey                  = {};
			std::optional< unsigned int > createdByUserID = {};
			std::chrono::system_clock::time_point createdAt = {};
			std::chrono::system_clock::time_point updatedAt = {};

			DBChatThread() = default;
			DBChatThread(unsigned int serverID, unsigned int threadID);

			friend bool operator==(const DBChatThread &lhs, const DBChatThread &rhs);
			friend bool operator!=(const DBChatThread &lhs, const DBChatThread &rhs);
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBCHATTHREAD_H_
