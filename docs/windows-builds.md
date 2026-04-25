# Windows Client Builds

This fork uses two GitHub workflow paths for Windows client coverage:

- Workflow: `CI`
- File: [ci.yml](../.github/workflows/ci.yml)
- Trigger: push, pull request, and manual dispatch
- Static Windows runner: `windows-2025-vs2026`
- Purpose: required PR/build validation for the static Windows client/server lane

The heavier shared/WebEngine client lane is kept separate:

- Workflow: `Windows Shared Client Installer`
- File: [windows-shared-client.yml](../.github/workflows/windows-shared-client.yml)
- Trigger: push to `master`, pull request to `master`, and manual dispatch
- Shared Windows runner: `windows-2022`
- Output: unsigned shared/WebEngine client payload and installer artifacts

## Recommended use

- Use `CI` for the normal pull-request gate and static Windows artifact validation.
- Use `Windows Shared Client Installer` when you need the shared/WebEngine payload
  under `build-shared-webengine\shared-webengine-stage` or downloadable Windows
  shared client artifacts.

## How to run the shared client workflow

1. Push your branch to GitHub.
2. Open `Actions` in the fork.
3. Select `Windows Shared Client Installer`.
4. Click `Run workflow` on the branch you want to build.
5. Download the uploaded artifact from the completed run.

## Notes

- `CI` keeps Windows tests disabled and validates the Windows build through
  binary, installer, payload, and screen-share helper artifact checks.
- `CI` keeps the static Linux server artifact lane separate from a shared Linux
  server-focused `ctest` lane, so pull requests have one practical test gate
  without making the Windows lane slower.
- The shared/WebEngine workflow skips installer generation on pull requests, but
  still stages and validates the shared payload.
- The shared/WebEngine workflow verifies the screen-share helper runtime only
  for manual dispatch runs; normal PR helper runtime coverage lives in `CI`.
- The shared/WebEngine workflow pins the reusable environment to release
  `2025-11`, commit `127cccc01d`, and ONNX Runtime `1.18.1`.
- The shared/WebEngine workflow looks for `mumble_env.x64-windows.<commit>.7z`
  or split `mumble_env.x64-windows.<commit>.7z.001/.002/...` assets under this
  repo's `build-env-<release>` GitHub release tag before falling back to a slow
  local Qt/vcpkg bootstrap path.
- Expect the first shared/WebEngine dependency bootstrap to be heavy. The full
  Windows vcpkg environment is typically tens of GB on disk and Qt WebEngine can
  take a long time on a fresh machine.

## Local Windows build

If you want to build unsigned Windows client artifacts on your own PC instead
of GitHub Actions, use:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\scripts\windows\build-local-windows-client.ps1 -InstallDependencies
```

Optional runtime verification of the screen-share helper:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\scripts\windows\build-local-windows-client.ps1 `
  -InstallDependencies `
  -InstallFfmpeg `
  -VerifyHelperRuntime
```

If you only need a fast local client build and want to skip the experimental
screen-share helper target, pass an extra CMake option:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\scripts\windows\build-local-windows-client.ps1 `
  -AdditionalCMakeOptions -Dscreen-helper=OFF
```

If Windows still has a pending reboot marker from servicing and you
intentionally want to continue anyway, the local build script also accepts:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\scripts\windows\build-local-windows-client.ps1 `
  -AllowPendingReboot
```

Notes for local use:

- Install Visual Studio 2022 with the C++ build tools before running the script.
- Install Git for Windows. The script prefers Git Bash and auto-detects Visual
  Studio's bundled `cmake` and `ninja`, so they do not need to be added to
  `PATH` manually.
- The local build script auto-detects `ONNXRUNTIME_ROOT` from the newest
  `.tmp\onnxruntime-win-x64-*` directory when present, which enables the DTLN
  backend in local Windows client builds without extra manual flags.
- If Rust was installed with `rustup`, the script prepends
  `%USERPROFILE%\.cargo\bin` to `PATH` so DeepFilterNet can build its runtime
  DLL from the vendored `libDF` C API. Without `cargo` or a packaged
  `deepfilter.dll`, DeepFilterNet is left disabled while the rest of the client
  still builds.
- `-InstallFfmpeg` downloads a portable Windows `ffmpeg` bundle into
  `build_tools\ffmpeg` and prepends it to `PATH` for the current run. It does
  not require Chocolatey or an administrator shell.
- The local build script skips MSI packaging by default for a faster local test
  loop. Pass `-EnablePackaging` only if you need installers and already have
  WiX available.
- Override the compatibility version only if you explicitly need a different
  upgrade relationship: `-DMUMBLE_WINDOWS_INSTALLER_VERSION=<version>`.
- The script mirrors the shared workflow's configure/build path when
  `-SharedWebEngine` is used.
- The shared workflow bootstraps the pinned ONNX Runtime archive for DTLN and
  installs Rust so the DeepFilterNet runtime DLL can be built on the Windows
  runner as part of the client artifact build.
- `-AllowPendingReboot` is an opt-in escape hatch for local use: it downgrades
  hard pending-reboot blockers to warnings for that run instead of aborting.
- It skips local MySQL setup because that workflow has tests disabled.
- Static lane artifacts are written into `build\`; shared/WebEngine artifacts
  are written into `build-shared-webengine\`.

## Publishing a reusable Windows build environment

If you already have a populated local Windows build environment under
`build_env\`, you can package and publish the exact `.7z` archive that the
shared Windows CI lane expects:

```powershell
.\scripts\windows\publish-windows-build-environment.ps1 `
  -EnvironmentRelease 2025-11 `
  -EnvironmentCommit 127cccc01d `
  -BuildType shared `
  -Upload `
  -CreateRelease
```

Notes:

- By default the script creates split `mumble_env.x64-windows.<commit>.7z.001`
  style volumes under `.tmp\build-env-archives\` using a `1900m` size cap, so
  the assets fit under GitHub's per-release-asset upload limit.
- By default it publishes to the GitHub repo from your `origin` remote and
  uses the release tag `build-env-<release>`.
- Pass `-Repository <owner>/<repo>` and optionally `-ReleaseTag <tag>` if you
  want to publish to a sister repo instead of this one.
- It uploads only when `-Upload` is passed. Without that flag it just creates
  the local archive so you can inspect it first.
- If the release already exists and you want to replace the asset, rerun with
  `-Clobber`.
- The script requires a 7-Zip-compatible CLI (`7z.exe` or `7za.exe`) and
  `gh.exe` for uploads.
