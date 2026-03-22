// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBCHATREADSTATE_H_
#define MUMBLE_SERVER_DATABASE_DBCHATREADSTATE_H_

#include <chrono>

namespace mumble {
namespace server {
	namespace db {

		struct DBChatReadState {
			unsigned int serverID                          = {};
			unsigned int threadID                          = {};
			unsigned int userID                            = {};
			unsigned int lastReadMessageID                 = {};
			std::chrono::system_clock::time_point updatedAt = {};

			DBChatReadState() = default;
			DBChatReadState(unsigned int serverID, unsigned int threadID, unsigned int userID);

			friend bool operator==(const DBChatReadState &lhs, const DBChatReadState &rhs);
			friend bool operator!=(const DBChatReadState &lhs, const DBChatReadState &rhs);
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBCHATREADSTATE_H_
