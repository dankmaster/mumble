// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_TEXTCHANNELTABLE_H_
#define MUMBLE_SERVER_DATABASE_TEXTCHANNELTABLE_H_

#include "DBTextChannel.h"

#include "database/Backend.h"
#include "database/Table.h"

#include <optional>
#include <vector>

namespace soci {
class session;
}

namespace mumble {
namespace server {
	namespace db {

		class ServerTable;
		class ChannelTable;

		class TextChannelTable : public ::mumble::db::Table {
		public:
			static constexpr const char *NAME = "text_channels";

			struct column {
				column()                                      = delete;
				static constexpr const char *server_id        = "server_id";
				static constexpr const char *text_channel_id  = "text_channel_id";
				static constexpr const char *name             = "name";
				static constexpr const char *description      = "description";
				static constexpr const char *acl_channel_id   = "acl_channel_id";
				static constexpr const char *position         = "position";
			};

			static constexpr unsigned int INTRODUCED_IN_SCHEMA_VERSION = 13;

			TextChannelTable(soci::session &sql, ::mumble::db::Backend backend, const ServerTable &serverTable,
							 const ChannelTable &channelTable);
			~TextChannelTable() = default;

			void addTextChannel(const DBTextChannel &textChannel);
			bool textChannelExists(unsigned int serverID, unsigned int textChannelID);
			std::optional< DBTextChannel > getTextChannel(unsigned int serverID, unsigned int textChannelID);
			std::vector< DBTextChannel > getTextChannels(unsigned int serverID);
			unsigned int getFreeTextChannelID(unsigned int serverID);

			void migrate(unsigned int fromSchemaVersion, unsigned int toSchemaVersion) override;
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_TEXTCHANNELTABLE_H_
