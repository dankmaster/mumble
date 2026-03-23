# Windows Crash Dump Flow

This repo now includes a helper script for collecting a Mumble crash dump on a Windows PC and uploading it to a remote server for analysis.

## Scope

- `mumble-screen-helper` is **not** bundled into `mumble.exe`.
- It is a separate executable target in `src/screen-helper/CMakeLists.txt`.
- It is currently only added to the build on Linux in `src/CMakeLists.txt`.

## What the script does

The PowerShell script:

- starts ProcDump in wait mode for `mumble.exe`
- launches Mumble
- waits for ProcDump to capture a crash dump
- writes a small `metadata.txt`
- zips the dump session directory
- uploads the zip with `scp` to `dank-server`

Script path:

- `scripts/windows/collect-mumble-dump.ps1`

## Requirements on the Windows PC

- ProcDump from Sysinternals
- Windows OpenSSH client (`ssh.exe` and `scp.exe`)
- a Mumble build or install containing `mumble.exe`
- SSH access to `dank-server`

ProcDump reference:

- https://learn.microsoft.com/en-us/sysinternals/downloads/procdump

## Example usage

From PowerShell on the Windows PC:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows\collect-mumble-dump.ps1 `
  -ProcDumpExe C:\Tools\Sysinternals\procdump64.exe `
  -MumbleExe 'C:\Program Files (x86)\Mumble\mumble.exe' `
  -SshTarget dank-server `
  -RemotePath '~/incoming/mumble-dumps'
```

If `procdump64.exe`, `ssh.exe`, `scp.exe`, and `mumble.exe` are in standard locations or on `PATH`, the short form is enough:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows\collect-mumble-dump.ps1
```

## Output

The script creates a timestamped directory under:

```text
scripts\..\..\artifacts\windows-dumps
```

Each session contains:

- one or more `.dmp` files
- `procdump.stdout.log`
- `procdump.stderr.log`
- `metadata.txt`
- a zipped archive of the session directory

## Notes

- The script waits for a crash-triggered dump, so it is intended for repro runs.
- If no dump is produced before the timeout, it stops waiting and prints the ProcDump log path.
- The default remote upload target is `dank-server:~/incoming/mumble-dumps/`.
