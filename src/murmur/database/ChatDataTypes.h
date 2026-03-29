// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SERVER_DATABASE_CHATDATATYPES_H_
#define MUMBLE_SERVER_DATABASE_CHATDATATYPES_H_

namespace mumble {
namespace server {
	namespace db {

		enum class ChatMessageBodyFormat : unsigned int {
			PlainText    = 0,
			MarkdownLite = 1,
		};

		enum class ChatAssetKind : unsigned int {
			Unknown  = 0,
			Image    = 1,
			Video    = 2,
			Document = 3,
			Binary   = 4,
		};

		enum class ChatAssetRetentionClass : unsigned int {
			DefaultStorage = 0,
			PreviewCache   = 1,
		};

		enum class ChatEmbedStatus : unsigned int {
			Pending = 0,
			Ready   = 1,
			Blocked = 2,
			Failed  = 3,
		};

	} // namespace db
} // namespace server
} // namespace mumble

#endif // MUMBLE_SERVER_DATABASE_CHATDATATYPES_H_
