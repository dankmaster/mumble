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
- Shared/WebEngine builds no longer require a published
  `mumble_env.x64-windows.*.7z` release asset. If that archive is missing, the
  dependency installer now clones the pinned `mumble-voip/vcpkg` repo, bootstraps
  `vcpkg.exe`, and installs the `x64-windows` dependency set locally.
- Expect that first shared/WebEngine dependency bootstrap to be heavy. The full
  Windows vcpkg environment is typically tens of GB on disk and Qt WebEngine
  builds can take a long time on a fresh machine.
