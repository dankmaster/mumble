// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBTEXTCHANNEL_H_
#define MUMBLE_SERVER_DATABASE_DBTEXTCHANNEL_H_

#include <string>

namespace mumble {
namespace server {
	namespace db {

		struct DBTextChannel {
			unsigned int serverID      = 0;
			unsigned int textChannelID = 0;
			unsigned int aclChannelID  = 0;
			unsigned int position      = 0;
			std::string name           = {};
			std::string description    = {};

			DBTextChannel() = default;
			DBTextChannel(unsigned int serverID, unsigned int textChannelID);

			friend bool operator==(const DBTextChannel &lhs, const DBTextChannel &rhs);
			friend bool operator!=(const DBTextChannel &lhs, const DBTextChannel &rhs);
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBTEXTCHANNEL_H_
