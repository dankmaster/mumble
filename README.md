# Mumble - Dankmaster Fork

<p align="center">
  <img src="icons/mumble_256x256.png" width="96" height="96" alt="Mumble logo">
</p>

<p align="center">
  <strong>A fork of Mumble for a small community server, focused on persistent chat, richer media, and screen-share experiments.</strong>
</p>

<p align="center">
  <a href="https://www.mumble.info"><img alt="Mumble website" src="https://img.shields.io/badge/Mumble-website-2f80ed?style=for-the-badge"></a>
  <a href="https://github.com/mumble-voip/mumble"><img alt="Upstream project" src="https://img.shields.io/badge/upstream-mumble--voip%2Fmumble-555?style=for-the-badge"></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-BSD--3--Clause-green?style=for-the-badge"></a>
  <a href="https://github.com/dankmaster/mumble/actions/workflows/build.yml"><img alt="Build workflow" src="https://img.shields.io/github/actions/workflow/status/dankmaster/mumble/build.yml?branch=master&label=build&style=for-the-badge"></a>
  <a href="https://github.com/dankmaster/mumble/actions/workflows/windows-client.yml"><img alt="Windows client workflow" src="https://img.shields.io/github/actions/workflow/status/dankmaster/mumble/windows-client.yml?branch=master&label=windows&style=for-the-badge"></a>
</p>

<p align="center">
  <img src="screenshots/Mumble.png" alt="Mumble client in light and dark themes" width="900">
</p>

## What This Is

This repository is a fork of [Mumble](https://github.com/mumble-voip/mumble).
Full credit for the original project, architecture, and the vast majority of
the codebase belongs to the Mumble team and upstream contributors.

Mumble is an open source, low-latency, high-quality voice chat application
built on Qt and Opus. The project contains the desktop client, `mumble`, and
the server, `mumble-server` (formerly Murmur).

This fork is not an official Mumble release. It is an experimental,
server-specific build for one group of friends running a private community
server. The goal is to keep the core Mumble voice experience intact while
adding features that make that server feel more modern and easier to live in.

If you want the official stable Mumble project, start at
[mumble.info](https://www.mumble.info/) or
[mumble-voip/mumble](https://github.com/mumble-voip/mumble).

## Fork Highlights

| Area | Status | Notes |
| --- | --- | --- |
| Persistent chat | Active fork feature | Stored channel history, unread state, and history sync for forked clients and servers. |
| Server-global chat | Optional server feature | A persistent global thread can be enabled per server. |
| Aggregate chat | Active fork feature | Users can view an ACL-filtered stream across readable channels. |
| Rich chat surface | Active fork feature | Reworked chat layout, MOTD treatment, inline images, and link preview groundwork. |
| Screen sharing | Experimental | Capability negotiation, server config, external helper plumbing, and relay scaffolding are in progress. |
| Identity overrides | Fork-specific utility | Optional advertised release and OS strings for controlled community deployments. |

## Screenshots

| Main Client | Developer Tooling |
| --- | --- |
| <img src="screenshots/Mumble.png" alt="Mumble client screenshot" width="520"> | <img src="docs/media/images/Mumble_Settings_DeveloperMenu.png" alt="Mumble developer menu setting" width="520"> |

## Repository Map

- [`src/`](src/) contains the client, server, protocol, helper, and test code.
- [`relay-webapp/`](relay-webapp/) contains the experimental browser relay shell for screen sharing.
- [`docs/chat-architecture.md`](docs/chat-architecture.md) describes the fork-specific persistent chat direction.
- [`docs/rich-chat-server.md`](docs/rich-chat-server.md) covers server-side rich chat storage and configuration.
- [`docs/screen-sharing-architecture.md`](docs/screen-sharing-architecture.md) explains the screen-share architecture.
- [`docs/screen-sharing-relay-deployment.md`](docs/screen-sharing-relay-deployment.md) covers relay deployment notes.
- [`docs/dev/build-instructions/README.md`](docs/dev/build-instructions/README.md) is the upstream build documentation.
- [`docs/windows-builds.md`](docs/windows-builds.md) captures this fork's tracked Windows build notes.

## Building

General Mumble build instructions live in
[`docs/dev/build-instructions/README.md`](docs/dev/build-instructions/README.md).
Those docs are version-specific, so make sure you are reading them from the
branch you intend to build.

For this fork, the main CMake switches are still the standard Mumble ones:

```bash
cmake -S . -B build
cmake --build build --parallel
```

Useful optional features in this tree include:

```bash
-Dclient=ON
-Dserver=ON
-Dscreen-helper=ON
-Dmodern-layout-webengine=ON
-Drnnoise=ON
-Ddtln=ON
-Ddeepfilternet=ON
```

Windows-specific notes for the tracked build flow are in
[`docs/windows-builds.md`](docs/windows-builds.md).

## Server Configuration

The server configuration template is
[`auxiliary_files/mumble-server.ini`](auxiliary_files/mumble-server.ini).

Fork-specific settings include:

```ini
persistentglobalchat=false
chat_asset_storage_path=chat-assets
chat_asset_max_bytes=26214400
chat_asset_total_quota_bytes=2147483648

screen_share_enabled=false
screen_share_relay_url="wss://relay.example.com/mumble-screen"
screen_share_max_width=1920
screen_share_max_height=1080
screen_share_max_fps=60
```

Persistent chat media storage is documented in
[`docs/rich-chat-server.md`](docs/rich-chat-server.md). Screen-share relay
deployment is documented in
[`docs/screen-sharing-relay-deployment.md`](docs/screen-sharing-relay-deployment.md).

## Compatibility

Core Mumble voice behavior is intended to remain compatible with ordinary
Mumble clients and servers wherever possible.

Fork-specific features are capability-gated. A forked client connected to an
older server should keep voice and basic text chat working, while features such
as persistent rich chat or screen-share controls may be hidden or disabled.
Stock Mumble clients are not expected to receive full fork feature parity.

## Contributing

Small, focused pull requests are welcome. Please follow the existing style and
the upstream [commit guidelines](COMMIT_GUIDELINES.md).

If a change is generally useful to Mumble, consider contributing it upstream to
[mumble-voip/mumble](https://github.com/mumble-voip/mumble). If a change is
specific to this community build, open it against this fork.

Useful starting points:

- [Introduction to the Mumble source code](docs/dev/TheMumbleSourceCode.md)
- [Plugin documentation](docs/dev/plugins/README.md)
- [Build documentation](docs/dev/build-instructions/README.md)
- [Fork chat architecture](docs/chat-architecture.md)
- [Fork screen-share architecture](docs/screen-sharing-architecture.md)

## Reporting Issues

For bugs or feature requests specific to this fork, use this repository's
[GitHub issues](https://github.com/dankmaster/mumble/issues).

For official Mumble bugs that reproduce in upstream builds, report them to
[mumble-voip/mumble](https://github.com/mumble-voip/mumble/issues/new/choose).

## License And Credits

This fork keeps Mumble's original license. See [LICENSE](LICENSE).

Mumble is made possible by the Mumble team, upstream contributors, translators,
plugin authors, packagers, and everyone who has maintained the project over the
years.

The official project uses free code signing provided by
[SignPath.io](https://signpath.io?utm_source=foundation&utm_medium=github&utm_campaign=mumble)
and a free code signing certificate by the
[SignPath Foundation](https://signpath.org?utm_source=foundation&utm_medium=github&utm_campaign=mumble).
