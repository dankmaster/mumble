[CmdletBinding()]
param(
	[string]$ProcDumpExe,
	[string]$MumbleExe,
	[string]$DumpRoot = (Join-Path $PSScriptRoot "..\..\artifacts\windows-dumps"),
	[string]$SshTarget = "dank-server",
	[string]$RemotePath = "~/incoming/mumble-dumps",
	[string[]]$MumbleArgs = @(),
	[int]$TimeoutMinutes = 30,
	[switch]$SkipUpload
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-ExistingPath {
	param(
		[string]$ExplicitPath,
		[string[]]$Candidates
	)

	if ($ExplicitPath) {
		$resolved = Resolve-Path -LiteralPath $ExplicitPath -ErrorAction Stop
		return $resolved.Path
	}

	foreach ($candidate in $Candidates) {
		if ($candidate -and (Test-Path -LiteralPath $candidate)) {
			return (Resolve-Path -LiteralPath $candidate).Path
		}
	}

	return $null
}

function Resolve-CommandPath {
	param(
		[string[]]$CommandNames
	)

	foreach ($commandName in $CommandNames) {
		$command = Get-Command -Name $commandName -ErrorAction SilentlyContinue
		if ($command) {
			return $command.Source
		}
	}

	return $null
}

$procDumpPath = Resolve-ExistingPath -ExplicitPath $ProcDumpExe -Candidates @(
	(Resolve-CommandPath -CommandNames @("procdump64.exe", "procdump.exe")),
	"$env:ProgramFiles\ProcDump\procdump64.exe",
	"$env:ProgramFiles\ProcDump\procdump.exe",
	"$env:ProgramFiles\Sysinternals\procdump64.exe",
	"$env:ProgramFiles\Sysinternals\procdump.exe",
	"$env:USERPROFILE\Downloads\Procdump\procdump64.exe",
	"$env:USERPROFILE\Downloads\Procdump\procdump.exe"
)

if (-not $procDumpPath) {
	throw "ProcDump was not found. Pass -ProcDumpExe or put procdump64.exe on PATH."
}

$mumblePath = Resolve-ExistingPath -ExplicitPath $MumbleExe -Candidates @(
	"$env:ProgramFiles\Mumble\mumble.exe",
	"${env:ProgramFiles(x86)}\Mumble\mumble.exe",
	(Resolve-CommandPath -CommandNames @("mumble.exe"))
)

if (-not $mumblePath) {
	throw "mumble.exe was not found. Pass -MumbleExe or install Mumble in the default location."
}

$sshPath = $null
$scpPath = $null
if (-not $SkipUpload) {
	$sshPath = Resolve-CommandPath -CommandNames @("ssh.exe", "ssh")
	$scpPath = Resolve-CommandPath -CommandNames @("scp.exe", "scp")

	if (-not $sshPath) {
		throw "ssh.exe was not found. Install the Windows OpenSSH client or use -SkipUpload."
	}
	if (-not $scpPath) {
		throw "scp.exe was not found. Install the Windows OpenSSH client or use -SkipUpload."
	}
}

$dumpRootPath = [System.IO.Path]::GetFullPath($DumpRoot)
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$sessionName = "mumble-crash-$timestamp"
$sessionDir = Join-Path $dumpRootPath $sessionName
$procDumpStdoutLog = Join-Path $sessionDir "procdump.stdout.log"
$procDumpStderrLog = Join-Path $sessionDir "procdump.stderr.log"
$metadataPath = Join-Path $sessionDir "metadata.txt"
$zipPath = Join-Path $dumpRootPath "$sessionName.zip"

New-Item -ItemType Directory -Path $sessionDir -Force | Out-Null

$metadata = @(
	"timestamp_utc=$([DateTime]::UtcNow.ToString('o'))"
	"computer_name=$env:COMPUTERNAME"
	"user_name=$env:USERNAME"
	"mumble_exe=$mumblePath"
	"mumble_args=$($MumbleArgs -join ' ')"
	"procdump_exe=$procDumpPath"
	"dump_session_dir=$sessionDir"
)
Set-Content -LiteralPath $metadataPath -Value $metadata -Encoding ASCII

$processName = Split-Path -Leaf $mumblePath
$procDumpArgs = @(
	"-accepteula"
	"-ma"
	"-e"
	"-w"
	$processName
	$sessionDir
)

Write-Host "Starting ProcDump watcher for $processName"
$procDumpProcess = Start-Process `
	-FilePath $procDumpPath `
	-ArgumentList $procDumpArgs `
	-RedirectStandardOutput $procDumpStdoutLog `
	-RedirectStandardError $procDumpStderrLog `
	-PassThru `
	-WindowStyle Hidden

Start-Sleep -Seconds 1

Write-Host "Launching Mumble: $mumblePath"
$mumbleProcess = Start-Process -FilePath $mumblePath -ArgumentList $MumbleArgs -PassThru

try {
	$null = Wait-Process -Id $procDumpProcess.Id -Timeout ($TimeoutMinutes * 60) -ErrorAction Stop
} catch {
	if (-not $procDumpProcess.HasExited) {
		Stop-Process -Id $procDumpProcess.Id -Force
	}
	throw "Timed out waiting for ProcDump to capture a dump after $TimeoutMinutes minutes."
}

$dumpFiles = Get-ChildItem -LiteralPath $sessionDir -Filter "*.dmp" -File
if (-not $dumpFiles) {
	throw "ProcDump exited without producing a .dmp file. Check $procDumpStdoutLog and $procDumpStderrLog."
}

if (Test-Path -LiteralPath $zipPath) {
	Remove-Item -LiteralPath $zipPath -Force
}

Compress-Archive -Path (Join-Path $sessionDir "*") -DestinationPath $zipPath -Force
Write-Host "Created dump archive: $zipPath"

if (-not $SkipUpload) {
	Write-Host "Ensuring remote directory exists on $SshTarget"
	& $sshPath $SshTarget "mkdir -p '$RemotePath'"

	$remoteDestination = "$SshTarget`:$RemotePath/"
	Write-Host "Uploading archive to $remoteDestination"
	& $scpPath $zipPath $remoteDestination
}

Write-Host "Dump session directory: $sessionDir"
Write-Host "Archive: $zipPath"
