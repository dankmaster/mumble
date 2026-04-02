[CmdletBinding()]
param(
	[string]$BuildNumber = "0",
	[string]$BuildType = "Release",
	[string[]]$AdditionalCMakeOptions = @(),
	[string]$EnvironmentRelease = "",
	[string]$EnvironmentCommit = "",
	[switch]$UseBundledSpdlog,
	[switch]$UseBundledCli11,
	[switch]$CleanBuild,
	[switch]$InstallDependencies,
	[switch]$InstallFfmpeg,
	[switch]$EnablePackaging,
	[switch]$VerifyHelperRuntime,
	[switch]$SkipConfigure,
	[switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Add-ToPathFront {
	param(
		[Parameter(Mandatory = $true)]
		[string]$PathEntry
	)

	if (-not (Test-Path -LiteralPath $PathEntry)) {
		return
	}

	$pathEntries = @($env:PATH -split ';' | Where-Object { $_ })
	if ($pathEntries -contains $PathEntry) {
		return
	}

	$env:PATH = "$PathEntry;$env:PATH"
}

function Get-VsWhereExecutable {
	$candidates = @(
		(Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"),
		(Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe")
	) | Where-Object { $_ }

	foreach ($path in $candidates) {
		if (Test-Path -LiteralPath $path) {
			return $path
		}
	}

	return $null
}

function Get-VisualStudioInstallationPath {
	$vsWhere = Get-VsWhereExecutable
	if (-not $vsWhere) {
		return $null
	}

	$installationPath = & $vsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
	if ($LASTEXITCODE -ne 0) {
		return $null
	}

	if ([string]::IsNullOrWhiteSpace($installationPath)) {
		return $null
	}

	return $installationPath.Trim()
}

function Test-IsWindowsBashShim {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Path
	)

	$normalizedPath = [System.IO.Path]::GetFullPath($Path).ToLowerInvariant()
	return $normalizedPath -eq "c:\windows\system32\bash.exe" -or
		$normalizedPath -like "c:\users\*\appdata\local\microsoft\windowsapps\bash.exe"
}

function Get-BashExecutable {
	$candidates = New-Object System.Collections.Generic.List[string]

	$git = Get-Command git.exe -ErrorAction SilentlyContinue
	if ($git) {
		$gitCmdDir = Split-Path -Parent $git.Source
		$gitRoot = Resolve-Path (Join-Path $gitCmdDir "..") -ErrorAction SilentlyContinue
		if ($gitRoot) {
			$candidates.Add((Join-Path $gitRoot.Path "bin\bash.exe"))
			$candidates.Add((Join-Path $gitRoot.Path "usr\bin\bash.exe"))
		}
	}

	$fallbacks = @(
		"C:\Program Files\Git\bin\bash.exe",
		"C:\Program Files\Git\usr\bin\bash.exe",
		"C:\Program Files (x86)\Git\bin\bash.exe",
		"C:\Program Files (x86)\Git\usr\bin\bash.exe"
	)
	foreach ($path in $fallbacks) {
		$candidates.Add($path)
	}

	$discoveredBashExecutables = Get-Command bash.exe -All -ErrorAction SilentlyContinue |
		ForEach-Object { $_.Source }
	foreach ($path in @($discoveredBashExecutables)) {
		$candidates.Add($path)
	}

	foreach ($path in $candidates | Select-Object -Unique) {
		if ((Test-Path -LiteralPath $path) -and -not (Test-IsWindowsBashShim -Path $path)) {
			return $path
		}
	}

	throw "Unable to locate Git Bash. Install Git for Windows and ensure bash.exe is available."
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

function Ensure-LocalBuildTooling {
	$visualStudioPath = Get-VisualStudioInstallationPath
	if ($visualStudioPath) {
		Add-ToPathFront (Join-Path $visualStudioPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin")
		Add-ToPathFront (Join-Path $visualStudioPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja")
	}
}

function Initialize-LocalOnnxRuntimeRoot {
	param(
		[Parameter(Mandatory = $true)]
		[string]$RepoRoot
	)

	if (-not [string]::IsNullOrWhiteSpace($env:ONNXRUNTIME_ROOT)) {
		return
	}

	$tmpRoot = Join-Path $RepoRoot ".tmp"
	if (-not (Test-Path -LiteralPath $tmpRoot)) {
		return
	}

	$candidate = Get-ChildItem -LiteralPath $tmpRoot -Directory -Filter "onnxruntime-win-x64-*" -ErrorAction SilentlyContinue |
		Where-Object {
			(Test-Path -LiteralPath (Join-Path $_.FullName "include")) -and
			(Test-Path -LiteralPath (Join-Path $_.FullName "lib"))
		} |
		Sort-Object -Property Name -Descending |
		Select-Object -First 1

	if ($candidate) {
		$env:ONNXRUNTIME_ROOT = $candidate.FullName
		Write-Host "Using local ONNXRUNTIME_ROOT=$($env:ONNXRUNTIME_ROOT)"
	}
}

function Initialize-RustCargoPath {
	$cargoBin = Join-Path $env:USERPROFILE ".cargo\bin"
	Add-ToPathFront $cargoBin
}

function Get-PortableFfmpegInstallRoot {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Root
	)

	return (Join-Path $Root "build_tools\ffmpeg")
}

function Get-PortableFfmpegBinPath {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Root
	)

	$installRoot = Get-PortableFfmpegInstallRoot -Root $Root
	if (-not (Test-Path -LiteralPath $installRoot)) {
		return $null
	}

	$ffmpeg = Get-ChildItem -Path $installRoot -Recurse -File -Filter "ffmpeg.exe" -ErrorAction SilentlyContinue |
		Where-Object { $_.DirectoryName -match '[\\/]bin$' } |
		Sort-Object -Property FullName |
		Select-Object -First 1
	if (-not $ffmpeg) {
		return $null
	}

	$ffplayPath = Join-Path $ffmpeg.DirectoryName "ffplay.exe"
	if (-not (Test-Path -LiteralPath $ffplayPath)) {
		return $null
	}

	return $ffmpeg.DirectoryName
}

function Expand-ArchivePortable {
	param(
		[Parameter(Mandatory = $true)]
		[string]$ArchivePath,

		[Parameter(Mandatory = $true)]
		[string]$DestinationPath
	)

	$tar = Get-Command tar.exe -ErrorAction SilentlyContinue
	if ($tar) {
		& $tar.Source -xf $ArchivePath -C $DestinationPath
		if ($LASTEXITCODE -eq 0) {
			return
		}
	}

	$sevenZip = Get-Command 7z.exe -ErrorAction SilentlyContinue
	if (-not $sevenZip) {
		$sevenZipFallback = "C:\Apps\7-Zip\7z.exe"
		if (Test-Path -LiteralPath $sevenZipFallback) {
			$sevenZip = Get-Item -LiteralPath $sevenZipFallback
		}
	}

	if ($sevenZip) {
		& $sevenZip.Source x -y "-o$DestinationPath" $ArchivePath
		if ($LASTEXITCODE -eq 0) {
			return
		}
	}

	throw "Unable to extract '$ArchivePath'. Install tar.exe or 7z.exe."
}

function Ensure-PortableFfmpeg {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Root,

		[Parameter(Mandatory = $true)]
		[string]$TempRoot
	)

	$existingBinPath = Get-PortableFfmpegBinPath -Root $Root
	if ($existingBinPath) {
		Add-ToPathFront $existingBinPath
		return $existingBinPath
	}

	Assert-CommandAvailable curl.exe

	$downloadUrl = [System.Environment]::GetEnvironmentVariable("MUMBLE_FFMPEG_DOWNLOAD_URL", "Process")
	if ([string]::IsNullOrWhiteSpace($downloadUrl)) {
		$downloadUrl = "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip"
	}

	$installRoot = Get-PortableFfmpegInstallRoot -Root $Root
	$archivePath = Join-Path $TempRoot "ffmpeg-release-essentials.zip"
	$extractRoot = Join-Path $TempRoot "ffmpeg-release-essentials"

	if (Test-Path -LiteralPath $extractRoot) {
		Remove-Item -LiteralPath $extractRoot -Recurse -Force
	}

	New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
	Write-Host "Downloading portable ffmpeg from $downloadUrl ..."
	& curl.exe -L --fail --output $archivePath $downloadUrl
	if ($LASTEXITCODE -ne 0) {
		throw "Failed to download ffmpeg from '$downloadUrl'."
	}

	Expand-ArchivePortable -ArchivePath $archivePath -DestinationPath $extractRoot

	if (Test-Path -LiteralPath $installRoot) {
		Remove-Item -LiteralPath $installRoot -Recurse -Force
	}

	New-Item -ItemType Directory -Force -Path $installRoot | Out-Null
	Copy-Item -Path (Join-Path $extractRoot "*") -Destination $installRoot -Recurse -Force

	$installedBinPath = Get-PortableFfmpegBinPath -Root $Root
	if (-not $installedBinPath) {
		throw "Portable ffmpeg install did not produce ffmpeg.exe and ffplay.exe under a bin directory."
	}

	Add-ToPathFront $installedBinPath
	return $installedBinPath
}

function Test-ObjectHasProperty {
	param(
		[Parameter(Mandatory = $true)]
		[object]$Object,

		[Parameter(Mandatory = $true)]
		[string]$Name
	)

	return $Object -and $Object.PSObject.Properties.Name -contains $Name
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
$buildRoot = Join-Path $repoRoot "build"
$runnerTemp = Join-Path $repoRoot ".tmp"
$null = New-Item -ItemType Directory -Force -Path $runnerTemp

if ($CleanBuild -and (Test-Path -LiteralPath $buildRoot)) {
	Remove-Item -LiteralPath $buildRoot -Recurse -Force
}

$githubEnvFile = [System.IO.Path]::GetTempFileName()
try {
	$env:GITHUB_WORKSPACE = $repoRootForBash
	$env:GITHUB_ENV = To-BashPath $githubEnvFile
	$env:RUNNER_TEMP = To-BashPath $runnerTemp
	$env:BUILD_TYPE = $BuildType
	$env:CMAKE_OPTIONS = "-Dtests=OFF -Dsymbols=ON -Ddisplay-install-paths=ON -Dtest-lto=OFF"
	if ($AdditionalCMakeOptions.Count -gt 0) {
		$env:CMAKE_OPTIONS = "$($env:CMAKE_OPTIONS) $($AdditionalCMakeOptions -join ' ')"
	}
	if ($EnablePackaging) {
		$env:MUMBLE_ENABLE_WINDOWS_PACKAGING = "ON"
	} else {
		$env:MUMBLE_ENABLE_WINDOWS_PACKAGING = "OFF"
	}
	$env:MUMBLE_ENABLE_WINDOWS_OVERLAY_XCOMPILE = "ON"
	$env:MUMBLE_SKIP_DATABASE_SETUP = "ON"
	$env:MUMBLE_BUILD_NUMBER = $BuildNumber
	if ($UseBundledSpdlog) {
		$env:MUMBLE_BUNDLED_SPDLOG_OVERRIDE = "ON"
	}
	if ($UseBundledCli11) {
		$env:MUMBLE_BUNDLED_CLI11_OVERRIDE = "ON"
	}
	if (-not [string]::IsNullOrWhiteSpace($EnvironmentRelease)) {
		if ([string]::IsNullOrWhiteSpace($EnvironmentCommit)) {
			throw "EnvironmentCommit must be specified when EnvironmentRelease is used."
		}

		$env:MUMBLE_ENVIRONMENT_SOURCE_OVERRIDE = "https://github.com/mumble-voip/vcpkg/releases/download/$EnvironmentRelease"
		$env:MUMBLE_ENVIRONMENT_COMMIT_OVERRIDE = $EnvironmentCommit
		$customEnvironmentDir = Join-Path $repoRoot "build_env\$EnvironmentRelease-$EnvironmentCommit"
		$env:MUMBLE_ENVIRONMENT_DIR_OVERRIDE = To-BashPath $customEnvironmentDir
	}

	Ensure-LocalBuildTooling
	Initialize-LocalOnnxRuntimeRoot -RepoRoot $repoRoot
	Initialize-RustCargoPath

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

	$portableFfmpegBinPath = Get-PortableFfmpegBinPath -Root $repoRoot
	if ($portableFfmpegBinPath) {
		Add-ToPathFront $portableFfmpegBinPath
	}

	if ($InstallFfmpeg) {
		$portableFfmpegBinPath = Ensure-PortableFfmpeg -Root $repoRoot -TempRoot $runnerTemp
		Write-Host "Portable ffmpeg installed at $portableFfmpegBinPath"
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
		if (-not (Get-Command ffmpeg.exe -ErrorAction SilentlyContinue)) {
			throw "ffmpeg was not found in PATH. Re-run with -InstallFfmpeg to download a portable build for the screen-helper runtime check."
		}

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

		$capabilitiesEnvelope = Get-Content -LiteralPath $capabilitiesPath -Raw | ConvertFrom-Json
		$capabilities = if (Test-ObjectHasProperty -Object $capabilitiesEnvelope -Name "payload") {
			$capabilitiesEnvelope.payload
		} else {
			$capabilitiesEnvelope
		}
		$runtimeSupport = if (Test-ObjectHasProperty -Object $capabilities -Name "runtime_support") {
			$capabilities.runtime_support
		} else {
			$capabilities
		}

		if (-not $runtimeSupport.ffmpeg_available) {
			throw "Helper runtime probe reports ffmpeg_available=false."
		}
		if (-not $capabilities.capture_supported) {
			throw "Helper runtime probe reports capture_supported=false."
		}
		if (-not $runtimeSupport.browser_webrtc_available) {
			throw "Helper runtime probe reports browser_webrtc_available=false."
		}
		if (-not ($runtimeSupport.edge_available -or $runtimeSupport.chrome_available -or $runtimeSupport.firefox_available)) {
			throw "Helper runtime probe found no supported browser on this machine."
		}
		if (-not ($runtimeSupport.h264_mf_available -or $runtimeSupport.h264_qsv_available -or $runtimeSupport.libx264_available)) {
			throw "Helper runtime probe found no usable H.264 encoder."
		}

		$selfTestJson = & $helper.FullName --diagnostics-log-file $logPath --self-test
		if ($LASTEXITCODE -ne 0) {
			throw "Helper self-test failed."
		}
		Set-Content -LiteralPath $selfTestPath -Value $selfTestJson -NoNewline

		$selfTestEnvelope = Get-Content -LiteralPath $selfTestPath -Raw | ConvertFrom-Json
		$selfTest = if (Test-ObjectHasProperty -Object $selfTestEnvelope -Name "payload") {
			$selfTestEnvelope.payload
		} else {
			$selfTestEnvelope
		}

		$selfTestSucceeded = $false
		if (Test-ObjectHasProperty -Object $selfTestEnvelope -Name "ok") {
			$selfTestSucceeded = [bool]$selfTestEnvelope.ok
		} elseif (Test-ObjectHasProperty -Object $selfTestEnvelope -Name "success") {
			$selfTestSucceeded = [bool]$selfTestEnvelope.success
		} elseif (Test-ObjectHasProperty -Object $selfTest -Name "success") {
			$selfTestSucceeded = [bool]$selfTest.success
		}

		if (-not $selfTestSucceeded) {
			throw "Helper self-test reported success=false."
		}
		if (-not $selfTest.output_file) {
			throw "Helper self-test did not report an output_file."
		}

		$artifacts = Get-RepoArtifactPaths -Root $repoRoot
		$artifacts.FullName | Set-Content -LiteralPath $artifactListPath
		if (-not $artifacts) {
			throw "No build artifacts were found under build\."
		}
	}

	$finalArtifacts = Get-RepoArtifactPaths -Root $repoRoot
	if (-not $finalArtifacts) {
		Write-Warning "Build completed, but no build artifacts were found under build\."
	} else {
		Write-Host ""
		Write-Host "Windows build artifacts:"
		$finalArtifacts | ForEach-Object { Write-Host $_.FullName }
	}
}
finally {
	if (Test-Path -LiteralPath $githubEnvFile) {
		Remove-Item -LiteralPath $githubEnvFile -Force
	}
}
