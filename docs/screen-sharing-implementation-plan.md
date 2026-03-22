# Screen Sharing Implementation Plan

## Status

Planning only.

This document turns the high-level architecture in `docs/screen-sharing-architecture.md` into an execution plan for this fork.

## Hard Constraints From The Current Codebase

These constraints should drive every implementation choice:

1. The existing real-time transport is voice-centric.
2. UDP only carries `Audio` and `Ping`.
3. Mumble UDP packets are capped at `1024` bytes.
4. `PluginDataTransmission` is payload-capped and rate-limited.
5. The current fork already uses feature advertisement in `Version` for persistent chat.
6. The current fork already uses `ServerConfig` for server-side feature and policy flags.
7. There is no existing video transport or WebRTC stack in this repository.
8. The Linux client already has PipeWire runtime plumbing for audio, but not screen sharing.

Implication:

- do not attempt to build Discord-like screen sharing on top of the existing Mumble audio transport
- do not attempt to repurpose plugin messages for media
- do use the existing fork pattern for per-client capability advertisement and per-server runtime policy

## Product Definition

Feature name:

- `Screen Share`

MVP definition:

1. A forked client can start one live screen share in its current channel.
2. Other forked clients in that channel can view it.
3. Old clients still connect, talk, and chat normally.
4. Old servers still accept the new client, but screen share UI stays disabled.
5. No recording in MVP.
6. No server-side transcoding in MVP.
7. No stock-upstream client compatibility for viewing the stream.

First quality target:

- `720p30` stable

Later quality target:

- `1080p60` when uplink, helper packaging, and client encode performance are proven

## Compatibility Contract

This is mandatory:

- `new client <-> new server`: screen share available
- `new client <-> old server`: voice and chat work, screen share hidden or disabled
- `old client <-> new server`: voice and chat work, old client is never sent screen-share traffic
- `old client <-> old server`: unchanged

This fork should treat screen share as capability-gated, not version-assumed.

## Architecture Decision

### Chosen Direction

Split the feature into three planes:

1. `Mumble/Murmur control plane`
2. `Client-side screen helper`
3. `External media relay`

Responsibilities:

- `Murmur`: auth, ACL, channel membership, stream permissions, session state, relay token minting, compatibility gating
- `Mumble client`: UI, capability advertisement, stream session management, helper lifecycle
- `Screen helper`: capture, encode, decode, WebRTC/media stack, local rendering bridge
- `Relay`: fanout, congestion control, NAT traversal support, optional later recording tap

### Rejected Directions

- video over existing Mumble UDP
- video over `PluginDataTransmission`
- custom in-tree SFU inside Murmur
- server-side transcoding for MVP
- requiring all clients to upgrade in lockstep

## Media Stack Decision

### What We Know

This repository does not currently include:

- WebRTC
- libdatachannel
- FFmpeg integration
- GStreamer integration for video
- H.264 or VPx encode/decode integration

### Recommended Implementation Shape

Do not embed a full media stack directly into the existing Mumble client process in the first pass.

Preferred shape:

- add a separate `mumble-screen-helper` executable in this repo
- launch it from the client using `QProcess`
- communicate with it over local IPC using `QLocalSocket` or loopback HTTP/WebSocket

Why:

- keeps the Mumble GUI process smaller and easier to debug
- isolates capture and codec crashes from the main client
- avoids forcing the current client architecture to absorb a large media subsystem immediately
- makes platform-specific capture backends easier to compartmentalize

### Mandatory Spike

Before building the full helper, complete a media-stack spike and lock one helper implementation approach:

Candidates:

- `GStreamer + WebRTC` helper
- `native capture + external WebRTC library` helper
- `platform-native capture + custom RTP/WebRTC transport` helper

Recommendation:

- optimize for fastest path to first-pixel on one platform, not for theoretical elegance

Exit criteria for the spike:

- capture one screen
- encode and publish one stream
- subscribe and render one viewer
- hold `720p30` for at least 10 minutes on a test channel

## Protocol Plan

### Capability Advertisement

Follow the chat feature pattern already present in this fork.

Extend `Version` with new optional capability fields, for example:

- `supports_screen_share_signaling`
- `supports_screen_share_capture`
- `supports_screen_share_view`

Rules:

- clients advertise these in `Version`
- Murmur stores them per session
- no screen-share-specific TCP messages are sent until both sides opt in

### Server Policy Advertisement

Extend `ServerConfig` with runtime policy fields, for example:

- `screen_share_enabled`
- `screen_share_recording_enabled`
- `screen_share_max_resolution`
- `screen_share_max_fps`
- `screen_share_helper_required`

Rules:

- old clients ignore unknown fields
- new clients disable the feature if fields are absent or false

### New TCP Messages

Append new messages after the existing custom chat messages.

Recommended message set:

1. `ScreenShareCreate`
2. `ScreenShareState`
3. `ScreenShareOffer`
4. `ScreenShareAnswer`
5. `ScreenShareIceCandidate`
6. `ScreenShareStop`

Suggested responsibilities:

- `ScreenShareCreate`: publisher requests a new share
- `ScreenShareState`: server broadcasts current active share state
- `ScreenShareOffer`: opaque signaling blob from publisher or viewer
- `ScreenShareAnswer`: opaque signaling blob response
- `ScreenShareIceCandidate`: trickle ICE or equivalent candidate relay
- `ScreenShareStop`: explicit teardown

Do not place encoded media payloads in these messages.

### Message Fields

Use stable identifiers from the beginning:

- `stream_id`
- `owner_session`
- `scope`
- `scope_id`
- `viewer_session` where needed
- `relay_room_id`
- `relay_token`
- `sdp` or opaque offer/answer blob
- `candidate` or opaque connectivity blob
- `created_at`
- `state`

## Murmur Plan

### MVP Server Behavior

Murmur should manage live state in memory only for MVP.

No DB schema changes are required for basic live screen sharing if we do not persist share history or recording metadata.

### Server Rules

Initial rules:

- one active stream per user
- one active stream per channel for MVP
- only users in the channel can watch the stream
- if the owner disconnects, moves channel, or loses permission, the share stops
- if the server disables screen sharing, all new create attempts are denied

### Murmur Files To Touch

- `src/Mumble.proto`
- `src/MumbleProtocol.h`
- `src/murmur/ServerUserInfo.h`
- `src/murmur/Server.h`
- `src/murmur/Server.cpp`
- `src/murmur/Messages.cpp`
- `src/murmur/Meta.h`
- `src/murmur/Meta.cpp`

### Murmur Data To Add

Per-user session flags:

- supports signaling
- supports capture
- supports viewing

Per-server config:

- screen share enabled
- recording enabled later
- relay base URL
- relay shared secret or token issuer config
- max resolution
- max fps

Per-channel runtime state:

- active stream ID
- owner session
- relay room ID
- creation timestamp
- viewer count optional

### Murmur Integration Steps

1. Parse and store client support flags from `Version`.
2. Advertise server policy through `ServerConfig`.
3. Add handlers for the new screen-share messages.
4. Validate ACL and channel membership on every control message.
5. Mint short-lived relay tokens for authorized publishers and viewers.
6. Broadcast `ScreenShareState` only to clients that advertised support.
7. Tear down share state on disconnect, kick, or channel change.

## Client Plan

### Runtime Model

Add a dedicated screen-share manager on the client side rather than folding all behavior into `MainWindow`.

Recommended new client classes:

- `ScreenShareManager`
- `ScreenShareSession`
- `ScreenShareCapabilityState`
- `ScreenShareViewerController`
- `ScreenShareHelperClient`

### Client Files To Touch

Core protocol and runtime:

- `src/Mumble.proto`
- `src/MumbleProtocol.h`
- `src/mumble/ServerHandler.h`
- `src/mumble/ServerHandler.cpp`
- `src/mumble/Messages.cpp`
- `src/mumble/Global.h`
- `src/mumble/Global.cpp`

UI and user flow:

- `src/mumble/MainWindow.h`
- `src/mumble/MainWindow.cpp`
- `src/mumble/MainWindow.ui`

Settings and configuration:

- `src/mumble/Settings.h`
- `src/mumble/Settings.cpp`
- `src/mumble/SettingsKeys.h`
- `src/mumble/NetworkConfig.ui`
- `src/mumble/NetworkConfig.cpp`

New files:

- `src/mumble/ScreenShareManager.h`
- `src/mumble/ScreenShareManager.cpp`
- `src/mumble/ScreenShareHelperClient.h`
- `src/mumble/ScreenShareHelperClient.cpp`
- `src/mumble/ScreenShareWidget.h`
- `src/mumble/ScreenShareWidget.cpp`

### Client UI Flow

MVP UI:

1. User sees `Start Screen Share` only when connected to a supporting server.
2. Clicking it opens a simple source picker.
3. Client requests share creation from Murmur.
4. On approval, client launches or attaches to the helper.
5. Helper publishes to the relay.
6. Other supporting clients see `Watch Screen Share`.
7. Viewer launches helper-backed viewing surface.
8. On stop or disconnect, UI tears down immediately.

### UI Scope Decisions

Recommended MVP UI:

- start or stop action in the main window
- one viewer surface at a time
- viewer surface may be a separate helper-owned window in MVP if in-app embedding slows the project down

Deferred:

- multiple simultaneous viewer panes
- picture-in-picture
- stream thumbnails in channel list
- moderator takeover tools

## Helper Plan

### Repository Shape

Add a new target in this repo:

- `src/screen-helper/`

Recommended helper components:

- capture backend abstraction
- encoder abstraction
- relay session client
- local IPC server
- local viewer window or frame bridge

### Local IPC Contract

Prefer a narrow command surface:

- `list_sources`
- `start_publish`
- `stop_publish`
- `start_view`
- `stop_view`
- `get_state`

Return structured JSON or protobuf messages over local IPC.

### Capture Backends

Target backends:

- Linux: PipeWire plus portal flow first
- Windows: modern Windows capture API first
- macOS: ScreenCaptureKit

Recommendation:

- implement Linux first because the current repo already carries PipeWire runtime integration patterns
- implement Windows second
- implement macOS third unless your actual user mix says otherwise

## Relay Plan

### Relay Direction

Do not write a custom SFU in this fork for MVP.

Use an external relay that already handles:

- WebRTC-style fanout
- congestion control
- ICE/TURN support
- room and participant concepts

Murmur should authenticate users to the relay by issuing short-lived publish or subscribe credentials.

### Deployment Shape

Recommended deploy units:

- existing `mumble-server.service`
- new `mumble-screen-relay.service`
- optional TURN service

### Relay API Contract

Murmur should need only a minimal integration surface:

- create or authorize room access for `channel:<id>`
- mint publish token for owner
- mint subscribe token for viewers
- revoke or expire token on stop

## Phase Plan

### Phase 0: Decision Spikes

Deliverables:

- helper media-stack decision
- relay product decision
- local IPC decision
- first bandwidth and latency baseline for this server

Exit criteria:

- written ADRs or notes
- one-platform loopback proof of concept

### Phase 1: Capability And Policy Plumbing

Deliverables:

- `Version` capability flags
- `ServerConfig` screen-share flags
- per-session support tracking on Murmur
- client-side feature gating

Exit criteria:

- compatibility matrix works without media
- unsupported combinations show no broken behavior

### Phase 2: Murmur Live Session State

Deliverables:

- in-memory active-share registry
- create and stop handlers
- ACL and channel membership checks
- state broadcasts

Exit criteria:

- server can represent one active share per channel
- teardown works on disconnect and channel moves

### Phase 3: Helper Skeleton

Deliverables:

- helper target builds
- local IPC works
- Mumble launches and supervises helper
- source listing and dummy start or stop path works

Exit criteria:

- GUI can talk to helper reliably

### Phase 4: First Media Path

Deliverables:

- one-platform capture backend
- publish to relay
- subscribe from second client
- helper viewer window

Exit criteria:

- first successful end-to-end share in a real channel

### Phase 5: Mumble UX Integration

Deliverables:

- start or stop share action
- watch action
- visible stream state in channel context
- better error reporting and teardown

Exit criteria:

- user can reliably start and watch a share without using debug tools

### Phase 6: Cross-Platform Expansion

Deliverables:

- second platform support
- packaging updates
- codec and hardware acceleration policy

Exit criteria:

- at least Linux plus Windows are supported, if those are your primary users

### Phase 7: Recording

Deliverables:

- relay-side or helper-side recording flow
- recording policy flags in `ServerConfig`
- storage path and retention behavior

Exit criteria:

- recording does not degrade live share quality

## Testing Plan

### Compatibility Matrix

Test all four combinations:

1. new client + new server
2. new client + old server
3. old client + new server
4. old client + old server

### Session Scenarios

Test:

- start share
- join viewer late
- stop share
- sharer disconnect
- viewer disconnect
- sharer changes channel
- viewer changes channel
- permission revoked mid-session
- server restart
- helper crash

### Quality Scenarios

Test:

- 720p30 stability
- multi-viewer fanout
- packet loss
- temporary relay loss
- helper restart recovery

### Regression Areas

Watch closely:

- standard voice path
- standard text and persistent chat
- channel move behavior
- reconnect logic
- CPU usage on the client
- server memory growth

## Deployment Plan

### Server Readiness Checklist

Before promising production use:

- measure actual uplink throughput
- measure packet loss to typical clients
- verify relay TLS
- confirm TURN need for your real user population
- define firewall rules for relay ports

### Config Additions

Plan to add new `mumble-server.ini` keys such as:

- `screen_share_enabled`
- `screen_share_relay_url`
- `screen_share_token_secret`
- `screen_share_max_resolution`
- `screen_share_max_fps`

## Risks

Main risks:

- helper media-stack choice causing packaging drag
- cross-platform capture differences
- NAT traversal complexity
- viewer embedding complexity
- codec licensing or packaging constraints
- uplink bottlenecks on multi-viewer sessions

## Recommended Order Of Work

If we start implementing now, the most pragmatic order is:

1. capability and policy plumbing
2. Murmur live session state
3. helper skeleton plus local IPC
4. one-platform end-to-end media spike
5. UI polish
6. second platform
7. recording

## Immediate Next Branches

Recommended sub-branches from `feature/screen-recording`:

- `feature/screen-share-capabilities`
- `feature/screen-share-server-state`
- `feature/screen-share-helper-skeleton`
- `feature/screen-share-linux-spike`
