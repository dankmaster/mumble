// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DBTextChannel.h"

namespace mumble {
namespace server {
	namespace db {

		DBTextChannel::DBTextChannel(unsigned int serverID, unsigned int textChannelID)
			: serverID(serverID), textChannelID(textChannelID) {}

		bool operator==(const DBTextChannel &lhs, const DBTextChannel &rhs) {
			return lhs.serverID == rhs.serverID && lhs.textChannelID == rhs.textChannelID
				   && lhs.aclChannelID == rhs.aclChannelID && lhs.position == rhs.position && lhs.name == rhs.name
				   && lhs.description == rhs.description;
		}

		bool operator!=(const DBTextChannel &lhs, const DBTextChannel &rhs) { return !(lhs == rhs); }

	} // namespace db
} // namespace server
} // namespace mumble
