[CmdletBinding()]
param(
	[string]$BuildNumber = "0",
	[string]$BuildType = "Release",
	[switch]$InstallDependencies,
	[switch]$InstallFfmpeg,
	[switch]$VerifyHelperRuntime,
	[switch]$SkipConfigure,
	[switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-BashExecutable {
	$candidate = Get-Command bash.exe -ErrorAction SilentlyContinue
	if ($candidate) {
		return $candidate.Source
	}

	$fallbacks = @(
		"C:\Program Files\Git\bin\bash.exe",
		"C:\Program Files\Git\usr\bin\bash.exe"
	)

	foreach ($path in $fallbacks) {
		if (Test-Path -LiteralPath $path) {
			return $path
		}
	}

	throw "Unable to locate bash.exe. Install Git for Windows and ensure bash.exe is available."
}

function To-BashPath([string]$Path) {
	return $Path.Replace('\', '/')
}

function Invoke-BashScript {
	param(
		[Parameter(Mandatory = $true)]
		[string]$ScriptPath,

		[Parameter()]
		[string[]]$Arguments = @()
	)

	$bashPath = Get-BashExecutable
	$scriptPathForBash = To-BashPath $ScriptPath

	& $bashPath $scriptPathForBash @Arguments
	if ($LASTEXITCODE -ne 0) {
		throw "Bash command failed: $scriptPathForBash"
	}
}

function Set-EnvironmentVariablesFromFile {
	param(
		[Parameter(Mandatory = $true)]
		[string]$FilePath
	)

	Get-Content -LiteralPath $FilePath | ForEach-Object {
		if ($_ -match '^(?<name>[^=]+)=(?<value>.*)$') {
			[System.Environment]::SetEnvironmentVariable($Matches.name, $Matches.value, "Process")
		}
	}
}

function Assert-CommandAvailable {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Name
	)

	if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
		throw "Required command '$Name' was not found in PATH."
	}
}

function Get-RepoArtifactPaths {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Root
	)

	$buildRoot = Join-Path $Root "build"
	if (-not (Test-Path -LiteralPath $buildRoot)) {
		return @()
	}

	return Get-ChildItem -Path $buildRoot -Recurse -File -Include "mumble*.msi", "mumble*.exe" |
		Where-Object { $_.Name -ne "mumble-screen-helper.exe" } |
		Sort-Object -Property FullName
}

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..\..")).Path
$repoRootForBash = To-BashPath $repoRoot
$runnerTemp = Join-Path $repoRoot ".tmp"
$null = New-Item -ItemType Directory -Force -Path $runnerTemp

$githubEnvFile = [System.IO.Path]::GetTempFileName()
try {
	$env:GITHUB_WORKSPACE = $repoRootForBash
	$env:GITHUB_ENV = To-BashPath $githubEnvFile
	$env:RUNNER_TEMP = To-BashPath $runnerTemp
	$env:BUILD_TYPE = $BuildType
	$env:CMAKE_OPTIONS = "-Dtests=OFF -Dsymbols=ON -Ddisplay-install-paths=ON -Dtest-lto=OFF"
	$env:MUMBLE_ENABLE_WINDOWS_PACKAGING = "ON"
	$env:MUMBLE_ENABLE_WINDOWS_OVERLAY_XCOMPILE = "ON"
	$env:MUMBLE_SKIP_DATABASE_SETUP = "ON"
	$env:MUMBLE_BUILD_NUMBER = $BuildNumber

	Assert-CommandAvailable git
	Assert-CommandAvailable cmake
	Assert-CommandAvailable ninja

	Invoke-BashScript -ScriptPath (Join-Path $repoRoot ".github\workflows\set_environment_variables.sh") -Arguments @(
		"windows-2025-vs2026",
		"static",
		"x86_64",
		$repoRootForBash
	)
	Set-EnvironmentVariablesFromFile -FilePath $githubEnvFile

	if ($InstallDependencies) {
		Invoke-BashScript -ScriptPath (Join-Path $repoRoot ".github\actions\install-dependencies\main.sh") -Arguments @(
			"windows-2025-vs2026",
			"static",
			"x86_64"
		)
	}

	if ($InstallFfmpeg) {
		Assert-CommandAvailable choco
		& choco install ffmpeg -y
		if ($LASTEXITCODE -ne 0) {
			throw "Failed to install ffmpeg via Chocolatey."
		}
	}

	$buildScript = Join-Path $repoRoot ".github\workflows\build.sh"

	if (-not $SkipConfigure) {
		$env:MUMBLE_CI_PHASE = "configure"
		Invoke-BashScript -ScriptPath $buildScript -Arguments @(
			"windows-2025-vs2026",
			"static",
			"x86_64"
		)
	}

	if (-not $SkipBuild) {
		$env:MUMBLE_CI_PHASE = "build"
		Invoke-BashScript -ScriptPath $buildScript -Arguments @(
			"windows-2025-vs2026",
			"static",
			"x86_64"
		)
	}

	Remove-Item Env:MUMBLE_CI_PHASE -ErrorAction SilentlyContinue

	if ($VerifyHelperRuntime) {
		Assert-CommandAvailable ffmpeg

		$env:MUMBLE_SCREENSHARE_TEST_PATTERN = "1"
		$env:MUMBLE_SCREENSHARE_CAPTURE_SOURCE = "test-pattern"
		$env:MUMBLE_SCREENSHARE_HEADLESS_VIEW = "1"

		$helper = Get-ChildItem -Path (Join-Path $repoRoot "build") -Recurse -File -Filter "mumble-screen-helper.exe" |
			Select-Object -First 1
		if (-not $helper) {
			throw "Unable to locate mumble-screen-helper.exe under build\."
		}

		$logPath = Join-Path $repoRoot "build\windows-screen-share-helper.log"
		$capabilitiesPath = Join-Path $repoRoot "build\windows-screen-share-capabilities.json"
		$selfTestPath = Join-Path $repoRoot "build\windows-screen-share-self-test.json"
		$artifactListPath = Join-Path $repoRoot "build\windows-client-artifacts.txt"

		$capabilitiesJson = & $helper.FullName --diagnostics-log-file $logPath --print-capabilities-json
		if ($LASTEXITCODE -ne 0) {
			throw "Helper capability probe failed."
		}
		Set-Content -LiteralPath $capabilitiesPath -Value $capabilitiesJson -NoNewline

		$capabilities = Get-Content -LiteralPath $capabilitiesPath -Raw | ConvertFrom-Json
		if (-not $capabilities.ffmpeg_available) {
			throw "Helper runtime probe reports ffmpeg_available=false."
		}
		if (-not $capabilities.capture_supported) {
			throw "Helper runtime probe reports capture_supported=false."
		}
		if (-not $capabilities.browser_webrtc_available) {
			throw "Helper runtime probe reports browser_webrtc_available=false."
		}
		if (-not ($capabilities.edge_available -or $capabilities.chrome_available -or $capabilities.firefox_available)) {
			throw "Helper runtime probe found no supported browser on this machine."
		}
		if (-not ($capabilities.h264_mf_available -or $capabilities.h264_qsv_available -or $capabilities.libx264_available)) {
			throw "Helper runtime probe found no usable H.264 encoder."
		}

		$selfTestJson = & $helper.FullName --diagnostics-log-file $logPath --self-test
		if ($LASTEXITCODE -ne 0) {
			throw "Helper self-test failed."
		}
		Set-Content -LiteralPath $selfTestPath -Value $selfTestJson -NoNewline

		$selfTest = Get-Content -LiteralPath $selfTestPath -Raw | ConvertFrom-Json
		if (-not $selfTest.success) {
			throw "Helper self-test reported success=false."
		}
		if (-not $selfTest.output_file) {
			throw "Helper self-test did not report an output_file."
		}

		$artifacts = Get-RepoArtifactPaths -Root $repoRoot
		$artifacts.FullName | Set-Content -LiteralPath $artifactListPath
		if (-not $artifacts) {
			throw "No installer artifacts were found under build\."
		}
	}

	$finalArtifacts = Get-RepoArtifactPaths -Root $repoRoot
	if (-not $finalArtifacts) {
		Write-Warning "Build completed, but no installer artifacts were found under build\."
	} else {
		Write-Host ""
		Write-Host "Windows installer artifacts:"
		$finalArtifacts | ForEach-Object { Write-Host $_.FullName }
	}
}
finally {
	if (Test-Path -LiteralPath $githubEnvFile) {
		Remove-Item -LiteralPath $githubEnvFile -Force
	}
}
