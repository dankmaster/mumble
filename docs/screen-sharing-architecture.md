# Screen Sharing Architecture

## Goal

Build a fork-specific screen sharing system for Mumble with the following direction:

1. Live desktop or window sharing
2. Discord-like perceived quality
3. Voice chat remains on the existing Mumble transport
4. Recording is optional and comes after live viewing works

This should be treated as a client + server feature set, not as a Murmur-only patch.

## Feasibility Summary

Short version:

- reliable Discord-style screen sharing is not realistic on the current Mumble media transport
- it is realistic on this server only if media is moved to a separate screen-share transport and relay
- the current server looks fine for signaling and a small-group relay, but uplink capacity still has to be measured before promising higher resolutions or many viewers

## Why This Needs A Fork

Current Mumble transport is still fundamentally voice-oriented:

- UDP messages are only `Audio` and `Ping`
- UDP packet size is capped at `1024` bytes
- the primary real-time payload is Opus audio
- `PluginDataTransmission` is a TCP relay intended for plugins, not media streaming

In this codebase, plugin relay payloads are capped at `1000` bytes and rate-limited on the server. That makes them useful for control messages, but not for video frames.

Because of that, Discord-class screen sharing is not a matter of adding a widget or a server toggle. It needs:

- new client capture and encode paths
- new signaling messages
- a real media transport for video
- new viewer UI
- ACL and moderation rules for starting and viewing streams

## Compatibility Direction

Backward compatibility must be explicit:

- old clients on new servers must keep voice and text working normally
- new clients on old servers must keep voice and text working normally
- screen sharing itself is a fork feature and may degrade to unavailable, but it must not break the base session

Compatibility matrix:

- `new client <-> new server`: full screen-share feature set
- `new client <-> old server`: hide or disable screen-share UI and never send screen-share messages
- `old client <-> new server`: keep core voice and text behavior unchanged and never push screen-share messages to that client
- `old client <-> old server`: unchanged

Important constraint:

- backward compatibility does not mean stock clients can watch the new screen-share stream
- it means unsupported peers remain fully usable for normal Mumble behavior

## Capability Negotiation Direction

Do not assume unknown TCP message types are safe to spray at older peers.

Preferred compatibility strategy:

- advertise support using added optional fields on existing protobuf messages that old peers already parse, such as `Version` and `ServerConfig`
- only send new screen-share-specific message types after both client and server have explicitly advertised support
- track support per session, not just per server version

That gives a safe rollout path because unknown fields inside known protobuf messages are discarded by older peers, while brand-new message types stay gated until both sides opt in.

## Non-Goals

Do not pursue these paths:

- tunneling video through existing Mumble audio UDP packets
- tunneling video through `PluginDataTransmission`
- tunneling video through `TextMessage`
- server-side transcoding in the MVP
- server-side recording in the MVP

Those approaches will be brittle, inefficient, and far below the quality target.

## Recommended Architecture

Preferred direction:

- keep Murmur responsible for auth, presence, channels, and control-plane permissions
- add a separate screen-share media service for the actual video path
- let forked clients use new signaling messages to negotiate screen-share sessions
- use SFU-style fanout for viewers instead of having Murmur duplicate media itself

Suggested pieces:

- `Murmur`: auth, ACL, channel membership, stream permissions, stream metadata
- `Screen relay service`: WebRTC or another modern low-latency video transport
- `TURN/STUN`: connectivity support for NAT traversal
- `Forked client`: screen capture, hardware encode when available, decode/render for viewers

## Reliability Direction

Reliable on this server:

- control-plane signaling
- auth and permission checks
- small-room screen sharing if media is handled by a proper relay

Not reliable on this server:

- relaying Discord-style screen video through stock Mumble transport
- scaling one shared stream to many viewers by abusing TCP plugin messages

Practical expectation for a first target:

- target `720p30` first
- treat `1080p60` as a later goal after uplink and client encode performance are measured
- assume bandwidth scales with viewers when using a relay, even if server CPU stays moderate

## Protocol Direction

Use Mumble only for signaling and policy.

Capability gating comes first.

Add new fork-specific protocol messages for control, for example:

- `ScreenShareCreate`
- `ScreenShareState`
- `ScreenShareOffer`
- `ScreenShareAnswer`
- `ScreenShareIceCandidate`
- `ScreenShareStop`

These messages should describe:

- who is sharing
- which channel or sessions may view
- what codec or quality profile is requested
- how the client connects to the relay

Do not send encoded video payloads inside these messages.
Do not send these messages at all until both sides have advertised screen-share support.

## Client Direction

The client work is substantial and platform-specific:

- screen or window picker
- start/stop share controls
- capture pipeline
- encoder selection and quality presets
- viewer surface in the main UI
- mute/deafen and screen-share interaction rules

Recommended MVP:

1. Start one screen share in a channel
2. Watch from forked clients only
3. No recording
4. No multi-stream mosaic
5. No upstream compatibility promise for this feature

## Server Direction

Murmur changes should stay narrow:

- validate whether a user may start a share
- expose stream state to other clients
- tear down shares when users disconnect or switch context
- optionally mint short-lived relay tokens

The relay service should handle:

- video transport
- viewer fanout
- congestion control
- bitrate adaptation

## Recording Direction

If recording is desired later, add it after live sharing works.

Preferred direction:

- have the relay subscribe to the screen-share stream and write a file asynchronously
- keep recording off the critical path for the first release

That avoids making the live path depend on muxing or disk I/O.

## First Implementation Slice

Recommended first coding slice:

1. Add protocol messages for screen-share signaling only
2. Add Murmur session and ACL handling for a single active share per channel
3. Add a minimal relay service stub and client negotiation flow
4. Add a client UI entry point for start/stop share
5. Prove end-to-end `720p30` for one sharer and one viewer

## Branching

Suggested working branch for this feature:

- `feature/screen-recording`
