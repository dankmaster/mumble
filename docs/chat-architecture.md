# Chat Architecture

## Goal

Build a fork-specific chat system for Mumble with the following rollout:

1. Persistent channel chat history
2. Optional server-global chat thread
3. Text-only first
4. Link previews / richer embeds
5. Attachments / richer media improvements

This should be treated as a client + server feature set, not as a server-only patch.

## Why This Needs A Fork

Current Mumble text chat is routed as a transient `TextMessage` with only:

- sender session
- target sessions
- target channels / trees
- message body

There is no first-class concept of:

- chat threads
- message IDs
- history
- read state
- attachments
- structured embeds

Those concepts need new protocol messages, new server storage, and new client UI.

## Product Direction

Introduce a new chat subsystem with explicit scopes:

- `private`
- `channel`
- `server_global`

The server-global chat is not a broadcast hack. It is just a thread with a different scope.

## Compatibility Direction

Preferred direction:

- New forked clients talk to new forked servers using new chat messages.
- Existing Mumble voice behavior should keep working.
- Legacy `TextMessage` should remain functional for basic interoperability where practical.

Non-goal:

- Full feature parity for persistent chat on stock upstream clients.

## Server Model

Initial schema direction:

- `chat_threads`
- `chat_messages`
- `chat_read_state`

Possible future tables:

- `chat_attachments`
- `chat_message_embeds`
- `chat_moderation_events`

Suggested minimum fields:

### `chat_threads`

- `thread_id`
- `server_id`
- `scope`
- `scope_key` canonical scope identifier
- `created_by_user_id`
- `created_at`
- `updated_at`

Examples for `scope_key`:

- `channel:42`
- `global`
- `users:12:98`

### `chat_messages`

- `message_id`
- `thread_id`
- `author_user_id` nullable for system messages
- `author_session` nullable
- `body_markdown` or `body_text`
- `body_html` nullable
- `created_at`
- `edited_at` nullable
- `deleted_at` nullable

### `chat_read_state`

- `thread_id`
- `user_id`
- `last_read_message_id`
- `updated_at`

## Protocol Direction

Do not overload the current `TextMessage` message for persistence and history sync.

Add new protocol messages for fork-specific chat, e.g.:

- `ChatSend`
- `ChatMessage`
- `ChatHistoryRequest`
- `ChatHistoryResponse`
- `ChatThreadState`
- `ChatReadStateUpdate`

Legacy `TextMessage` can remain for compatibility, but should not be the primary wire format for the new system.

## Client Direction

Initial client UI should include:

- a thread view for the active channel
- a server-global thread view when enabled
- basic history loading
- unread state
- send box for plain text / markdown-backed text

Defer until later:

- rich embeds
- attachment upload UX
- inline media galleries
- moderation tools beyond basic deletion / hide

## ACL Direction

History visibility must follow channel visibility rules.

Initial rules:

- channel thread history is visible only if the user can currently access the channel
- server-global thread is controlled by a new server option
- private threads are visible only to participants

Open question:

- whether channel history should be visible retroactively if a user gains access later

Recommendation:

- yes, unless a future retention or moderation policy says otherwise

## Retention Direction

Start simple:

- persistent by default
- no automatic expiry in MVP

Future server settings:

- retention days per scope
- maximum messages per thread
- maximum total attachment storage

## Embed / Preview Direction

Phase 2 should support link previews.

Recommendation:

- client-side preview fetch first, not server-side

Reason:

- avoids turning the server into a URL fetcher
- avoids SSRF and caching complexity in the first pass

## Media Direction

Current Mumble already supports HTML and inline data URL images in text rendering, but that is not the same as a structured media system.

For the new system:

- start with text only
- then add typed attachments
- then add preview cards

## MVP

MVP for first implementation:

1. DB tables for thread/message/read state
2. New protocol messages for send + history fetch
3. Server support for persistent channel thread history
4. Client UI for one persistent channel thread
5. Optional server-global thread

## First Implementation Slice

Recommended first coding slice:

1. Add server DB tables and migrations
2. Add protocol definitions for new chat messages
3. Add server handlers for send + history fetch
4. Add minimal client model for chat threads/messages
5. Add basic chat panel for current-channel history

## Branching

Current working branch for this fork should be based on `v1.6.870`, not on upstream moving `master`.

Suggested long-lived branches:

- `dank/main`
- `feature/chat-foundation`
- `feature/chat-global-thread`
- `feature/chat-previews`
- `feature/chat-attachments`
