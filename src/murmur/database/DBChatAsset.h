// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_DBCHATASSET_H_
#define MUMBLE_SERVER_DATABASE_DBCHATASSET_H_

#include "ChatDataTypes.h"

#include <chrono>
#include <optional>
#include <string>

namespace mumble {
namespace server {
	namespace db {

		struct DBChatAsset {
			unsigned int serverID                       = {};
			unsigned int assetID                        = {};
			std::optional< unsigned int > ownerUserID   = {};
			std::optional< unsigned int > ownerSession  = {};
			std::optional< unsigned int > previewAssetID = {};
			std::string sha256                          = {};
			std::string storageKey                      = {};
			std::string mime                            = {};
			std::uint64_t byteSize                      = {};
			ChatAssetKind kind                          = ChatAssetKind::Unknown;
			unsigned int width                          = {};
			unsigned int height                         = {};
			std::uint64_t durationMs                    = {};
			ChatAssetRetentionClass retentionClass      = ChatAssetRetentionClass::DefaultStorage;
			std::chrono::system_clock::time_point createdAt = {};
			std::chrono::system_clock::time_point lastAccessedAt = {};

			DBChatAsset() = default;
			DBChatAsset(unsigned int serverID, unsigned int assetID) : serverID(serverID), assetID(assetID) {}
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_DBCHATASSET_H_
