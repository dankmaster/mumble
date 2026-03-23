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
- It does not sign the installer.
- It disables tests to keep the manual client build faster.

## Local Windows build

If you want to build the unsigned Windows installer on your own PC instead of
GitHub Actions, use:

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

Notes for local use:

- The script mirrors the `Windows Client` workflow's configure/build path.
- It builds an unsigned installer.
- It skips local MySQL setup because that workflow has tests disabled.
- Artifacts are written into the repo's `build\` directory.
