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
- `relay_token`
- `relay_session_id`
- `stream_id`
- `relay_role`
- `codec`
- `transport`
- `width`
- `height`
- `fps`
- `bitrate_kbps`

Users should never type or copy those URLs manually.
