## Relay Web App

This folder contains the minimal hosted web client for the
`browser-webrtc-runtime` screen-share path.

Expected deployment shape:

- serve `index.html`, `relay-app.js`, and `styles.css` over `https://`
- expose the same path over `wss://` for the actual LiveKit-compatible relay
- announce that endpoint from Murmur via `screen_share_relay_url`

The helper launches this page automatically and passes ephemeral query
parameters such as:

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

The sensitive `relay_token` should be passed in the URL fragment instead of the
normal query string, for example `#relay_token=...`. The page still accepts the
legacy query parameter for older launchers, but new launchers keep the token out
of normal static-host request logs and scrub it from the visible URL after
startup.

The in-app relay currently clamps browser capture to a 1280x720 @ 30 fps
low-latency profile, uses VP8 as the browser-managed WebRTC codec, disables
LiveKit backup codec publishing, and publishes a single screen-share stream.
This keeps the viewer out of adaptive simulcast layers without building large
encoder or relay queues.

The page also samples browser WebRTC stats while video is attached and reports a
compact bitrate/frame-rate/loss/repair/actual-codec summary to the in-app Qt
bridge for diagnostics. It logs requested, negotiated, and actual WebRTC codecs
so codec negotiation drift is visible during rollout.

Users should never type or copy those URLs manually.
