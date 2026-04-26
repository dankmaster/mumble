## Screen-Share Relay Deployment

This fork now supports two practical relay execution modes:

1. `direct-runtime`
   Use `file://`, `rtmp://`, or `rtmps://` relay URLs. The helper executes
   `ffmpeg` or `ffplay` directly.

2. `browser-webrtc-runtime`
   Use `http://`, `https://`, `ws://`, or `wss://` relay URLs. The helper
   launches an isolated browser app window and hands the session to a hosted
   relay web client.

### Recommended Production Shape

- `Murmur`: auth, ACL, channel membership, screen-share session state
- `LiveKit`: actual SFU/WebRTC transport
- `relay web app`: a tiny page hosted next to the relay that joins the right
  room with the token Murmur minted

The repo now includes a minimal hosted client shell in
`relay-webapp/`. It is meant to be served by your reverse proxy or
small static host on the same endpoint family as the announced relay URL.

For small Windows-heavy groups, this keeps the UX simple while avoiding
server-side transcoding in Murmur.

### Server Config

Use these settings in `mumble-server.ini`:

```ini
screen_share_enabled=true
screen_share_relay_url="wss://relay.example.com/mumble-screen"
screen_share_relay_api_key="your-livekit-api-key"
screen_share_relay_api_secret="your-livekit-api-secret"
screen_share_diagnostics_logging=true
```

`screen_share_relay_url` is the URL announced to clients. For the browser
runtime:

- if Murmur announces `wss://relay.example.com/mumble-screen`
- the helper launches `https://relay.example.com/mumble-screen?...`
- the original `wss://...` value is still passed to the web app as query data
- the relay join token is passed in the URL fragment, not in the HTTP request
  query, so normal static-host access logs do not receive it

That lets one deployed endpoint serve:

- normal HTTP GET for the app shell
- WebSocket upgrade for the actual relay path

### Browser Launch Contract

The helper appends query parameters like:

- `relay_url`
- `relay_room_id`
- `relay_session_id`
- `stream_id`
- `relay_role`
- `codec`
- `transport`
- `width`
- `height`
- `fps`
- `bitrate_kbps`

The sensitive `relay_token` is passed as a fragment parameter, for example
`#relay_token=...`. The hosted page accepts the old query form for compatibility
but scrubs either form from the visible URL after startup.

The hosted page should treat those as ephemeral session inputs and should not
persist them.

While video is attached, the hosted page samples WebRTC stats and reports a
compact bitrate, frame-rate, RTT, jitter, loss, and repair summary back through
the in-app Qt bridge when diagnostics are enabled.

### LiveKit Token Contract

When both `screen_share_relay_api_key` and `screen_share_relay_api_secret` are
configured and the relay transport is WebRTC, Murmur now mints LiveKit-style
JWT join tokens per recipient. Tokens are short-lived and are refreshed by
Murmur when it resends session state.

Publisher grant:

- `roomJoin=true`
- `canPublish=true`
- `canPublishSources=["screen_share", "screen_share_audio"]`
- `canSubscribe=true`

Viewer grant:

- `roomJoin=true`
- `canPublish=false`
- `canSubscribe=true`

### UX Expectation

Users should not manually copy URLs.

- `Start Screen Share`: Mumble starts the helper, the helper opens the isolated
  app window, and the user selects a screen/window
- `Watch Screen Share`: Mumble starts the helper and opens the viewer window

The browser is only an implementation detail in this phase.
