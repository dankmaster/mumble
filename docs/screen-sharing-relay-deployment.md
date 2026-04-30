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
screen_share_codec_preferences="vp8 h264 av1 vp9"
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
- `requested_codec`
- `transport`
- `width`
- `height`
- `fps`
- `bitrate_kbps`

For the browser WebRTC runtime, current clients request VP8 because it is the
most reliable baseline across the bundled Qt WebEngine path and external
browser fallback. The relay page publishes one VP8 low-latency stream with
LiveKit backup codec publishing disabled. Direct helper transports remain
H.264-first by default and can still negotiate H.264, AV1, VP9, or explicit VP8
when the host ffmpeg runtime supports the selected encoder.

The sensitive `relay_token` is passed as a fragment parameter, for example
`#relay_token=...`. The hosted page accepts the old query form for compatibility
but scrubs either form from the visible URL after startup.

The hosted page should treat those as ephemeral session inputs and should not
persist them.

While video is attached, the hosted page samples WebRTC stats and reports a
compact bitrate, frame-rate, RTT, jitter, loss, repair, and actual codec summary
back through the in-app Qt bridge when diagnostics are enabled. The page logs
requested codec, negotiated session codec, and actual WebRTC stats codec; a
requested/actual mismatch is logged as a warning once per session.

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

Rotate any proof-of-concept LiveKit API secret before exposing a real relay.
Deploy the new Murmur config and LiveKit key/secret together, then restart
Murmur so newly minted screen-share tokens are signed with the new secret. Old
join tokens are intentionally short-lived and should expire without manual
cleanup.

### Relay Hardening Checklist

- Serve the relay app and LiveKit WebSocket over TLS from the public
  `screen_share_relay_url`; do not expose the internal LiveKit API port directly
  without a reverse proxy or load balancer.
- Keep `7881/tcp` exposed for WebRTC-over-TCP fallback and either expose
  `7882/udp` when using LiveKit UDP mux or the configured UDP port range when
  not using mux.
- Monitor the relay process, TLS endpoint, WebSocket upgrade path, and LiveKit
  Prometheus metrics if `prometheus_port` is enabled in the LiveKit config.
- Keep Murmur `screen_share_diagnostics_logging=true` during rollout so codec
  negotiation and relay connection failures are visible, then reduce verbosity
  after the relay has been stable.
- Verify each deploy with one publisher and one viewer: requested codec VP8,
  negotiated session codec VP8, actual stats codec VP8, stable frame rate, and
  no fallback request.

### UX Expectation

Users should not manually copy URLs.

- `Start Screen Share`: Mumble starts the helper, the helper opens the isolated
  app window, and the user selects a screen/window
- `Watch Screen Share`: Mumble starts the helper and opens the viewer window

The browser is only an implementation detail in this phase.
