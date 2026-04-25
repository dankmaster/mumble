# Windows Client Builds

This fork already inherits Mumble's full cross-platform CI in
[build.yml](../.github/workflows/build.yml). That workflow runs on push and pull
request and includes a Windows build.

For faster client artifacts on demand, this fork also has a dedicated manual
workflow:

- Workflow: `Windows Client`
- File: [windows-client.yml](../.github/workflows/windows-client.yml)
- Trigger: `workflow_dispatch`
- Runner: `windows-2025-vs2026`
- Output: unsigned Windows client artifacts (`mumble*.msi`, `mumble*.exe`)

## Recommended use

- Use `Build` when you want the full upstream-style matrix.
- Use `Windows Client` when you only want a downloadable Windows client artifact
  for the current branch.

## How to run it

1. Push your branch to GitHub.
2. Open `Actions` in the fork.
3. Select `Windows Client`.
4. Click `Run workflow` on the branch you want to build.
5. Download the uploaded artifact from the completed run.

## Notes

- This workflow uses `${{ github.run_number }}` as the local build number.
- Windows packaging now uses a separate compatibility version lane for the
  installer: by default `major.minor.0.build`. That keeps official Mumble
  installers newer on the upgrade graph so users can switch back by running the
  stock installer, while fork installers can still replace an existing Mumble
  install in place.
- It does not sign the installer.
- It disables tests to keep the manual client build faster.

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
- The local build script now auto-detects `ONNXRUNTIME_ROOT` from the newest
  `.tmp\onnxruntime-win-x64-*` directory when present, which enables the DTLN
  backend in local Windows client builds without extra manual flags.
- If Rust was installed with `rustup`, the script also prepends
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
- The script mirrors the `Windows Client` workflow's configure/build path.
- The `Windows Client` workflow now bootstraps the pinned ONNX Runtime archive
  for DTLN and installs Rust so the DeepFilterNet runtime DLL can be built on
  the Windows runner as part of the client artifact build.
- It builds unsigned Windows client artifacts. Pass `-EnablePackaging` only if
  you explicitly need the MSI installer output.
- `-AllowPendingReboot` is an opt-in escape hatch for local use: it downgrades
  hard pending-reboot blockers to warnings for that run instead of aborting.
- It skips local MySQL setup because that workflow has tests disabled.
- Artifacts are written into the repo's `build\` directory.
- The shared/WebEngine workflow now looks for
  `mumble_env.x64-windows.<commit>.7z` or split
  `mumble_env.x64-windows.<commit>.7z.001/.002/...` assets under this repo's
  `build-env-<release>` GitHub release tag before it falls back to the slow
  local Qt/vcpkg bootstrap path.
- Expect that first shared/WebEngine dependency bootstrap to be heavy. The full
  Windows vcpkg environment is typically tens of GB on disk and Qt WebEngine
  builds can take a long time on a fresh machine.

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
