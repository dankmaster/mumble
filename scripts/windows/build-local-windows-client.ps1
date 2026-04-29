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
	[switch]$SkipSharedInstaller,
	[switch]$SharedWebEngine,
	[switch]$VerifyHelperRuntime,
	[switch]$SkipConfigure,
	[switch]$SkipBuild,
	[switch]$AllowPendingReboot
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

function Get-CMakeCacheValue {
	param(
		[Parameter(Mandatory = $true)]
		[string]$CachePath,

		[Parameter(Mandatory = $true)]
		[string]$Name
	)

	if (-not (Test-Path -LiteralPath $CachePath)) {
		return $null
	}

	foreach ($line in Get-Content -LiteralPath $CachePath) {
		if ($line -match "^(?<name>[^:]+):[^=]+=?(?<value>.*)$" -and $Matches.name -eq $Name) {
			return $Matches.value.Trim()
		}
	}

	return $null
}

function Get-CMakeCacheBoolean {
	param(
		[Parameter(Mandatory = $true)]
		[string]$CachePath,

		[Parameter(Mandatory = $true)]
		[string]$Name
	)

	$value = Get-CMakeCacheValue -CachePath $CachePath -Name $Name
	if ([string]::IsNullOrWhiteSpace($value)) {
		return $false
	}

	return $value -match '^(ON|TRUE|1)$'
}

function Get-WixSharpExecutable {
	$cmd = Get-Command cscs.exe -ErrorAction SilentlyContinue
	if ($cmd) {
		return $cmd.Source
	}

	$fallback = "C:\WixSharp\cscs.exe"
	if (Test-Path -LiteralPath $fallback) {
		return $fallback
	}

	throw "Unable to locate cscs.exe. Install the local WixSharp dependency set first."
}

function Get-WindeployQtExecutable {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Root
	)

	$candidates = @(
		(Join-Path $Root "installed\$env:MUMBLE_VCPKG_TRIPLET\tools\Qt6\bin\windeployqt.exe"),
		(Join-Path $Root "installed\$env:MUMBLE_VCPKG_TRIPLET\tools\Qt6\bin\windeployqt6.exe"),
		(Join-Path $Root "installed\$env:MUMBLE_VCPKG_TRIPLET\tools\Qt6\bin\windeployqt")
	)

	foreach ($candidate in $candidates) {
		if (Test-Path -LiteralPath $candidate) {
			return $candidate
		}
	}

	throw "Unable to locate windeployqt.exe for triplet '$env:MUMBLE_VCPKG_TRIPLET' under '$Root'."
}

function Copy-DirectoryContents {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Source,

		[Parameter(Mandatory = $true)]
		[string]$Destination
	)

	if (-not (Test-Path -LiteralPath $Source)) {
		return
	}

	New-Item -ItemType Directory -Force -Path $Destination | Out-Null
	Copy-Item -Path (Join-Path $Source "*") -Destination $Destination -Recurse -Force
}

function Copy-FileIfExists {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Source,

		[Parameter(Mandatory = $true)]
		[string]$Destination
	)

	if (-not (Test-Path -LiteralPath $Source)) {
		return
	}

	New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
	Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Invoke-ProcessWithTimeout {
	param(
		[Parameter(Mandatory = $true)]
		[string]$FilePath,

		[Parameter(Mandatory = $true)]
		[string[]]$Arguments,

		[Parameter(Mandatory = $true)]
		[string]$Description,

		[int]$TimeoutSeconds = 180
	)

	$escapedArguments = foreach ($argument in $Arguments) {
		if ($argument -notmatch '[\s"]') {
			$argument
		} else {
			$escapedArgument = $argument -replace '(\\*)"', '$1$1\"'
			$escapedArgument = $escapedArgument -replace '(\\+)$', '$1$1'
			'"' + $escapedArgument + '"'
		}
	}
	$argumentLine = $escapedArguments -join ' '

	$tempRoot = if (-not [string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) {
		$env:RUNNER_TEMP
	} else {
		[System.IO.Path]::GetTempPath()
	}
	New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null

	$outputToken = [Guid]::NewGuid().ToString("N")
	$stdoutPath = Join-Path $tempRoot "$outputToken.stdout.log"
	$stderrPath = Join-Path $tempRoot "$outputToken.stderr.log"

	Write-Host "$Description timeout: $TimeoutSeconds seconds"

	try {
		$process = Start-Process -FilePath $FilePath -ArgumentList $argumentLine -NoNewWindow -PassThru `
			-RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath

		if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
			& taskkill.exe /PID $process.Id /T /F *> $null
			throw "$Description timed out after $TimeoutSeconds seconds."
		}

		[string]$stdout = ""
		[string]$stderr = ""
		if (Test-Path -LiteralPath $stdoutPath) {
			$content = Get-Content -LiteralPath $stdoutPath -Raw -ErrorAction SilentlyContinue
			if ($null -ne $content) {
				$stdout = $content
			}
		}
		if (Test-Path -LiteralPath $stderrPath) {
			$content = Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue
			if ($null -ne $content) {
				$stderr = $content
			}
		}

		return [PSCustomObject]@{
			ExitCode = $process.ExitCode
			StdOut   = $stdout
			StdErr   = $stderr
		}
	} finally {
		Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
	}
}

function Copy-SharedEnvironmentRuntime {
	param(
		[Parameter(Mandatory = $true)]
		[string]$StageRoot,

		[Parameter(Mandatory = $true)]
		[string]$EnvironmentRoot,

		[Parameter(Mandatory = $true)]
		[string]$Triplet
	)

	$environmentBin = Join-Path $EnvironmentRoot "installed\$Triplet\bin"
	if (-not (Test-Path -LiteralPath $environmentBin)) {
		throw "Shared environment runtime bin directory is missing: '$environmentBin'."
	}

	# The shared WebEngine lane is a portable local payload. Mirror the shared
	# environment runtime DLL set so Qt/WebEngine and Mumble's shared third-party
	# dependencies resolve without depending on a developer PATH.
	Get-ChildItem -LiteralPath $environmentBin -File -Filter "*.dll" | ForEach-Object {
		Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $StageRoot $_.Name) -Force
	}
}

function Copy-SharedQtOpenSslBackend {
	param(
		[Parameter(Mandatory = $true)]
		[string]$StageRoot,

		[Parameter(Mandatory = $true)]
		[string]$EnvironmentRoot,

		[Parameter(Mandatory = $true)]
		[string]$Triplet
	)

	$openSslBackend = Join-Path $EnvironmentRoot "installed\$Triplet\Qt6\plugins\tls\qopensslbackend.dll"
	if (-not (Test-Path -LiteralPath $openSslBackend)) {
		throw "Qt OpenSSL TLS backend is missing from the shared environment: '$openSslBackend'."
	}

	$stageTlsRoot = Join-Path $StageRoot "tls"
	New-Item -ItemType Directory -Force -Path $stageTlsRoot | Out-Null
	Copy-Item -LiteralPath $openSslBackend -Destination (Join-Path $stageTlsRoot "qopensslbackend.dll") -Force
}

function Write-SharedQtConf {
	param(
		[Parameter(Mandatory = $true)]
		[string]$StageRoot
	)

	$qtConfPath = Join-Path $StageRoot "qt.conf"
	$qtConfContent = @'
[Paths]
Prefix=.
Binaries=.
Plugins=.
LibraryExecutables=.
Qml2Imports=qml
ArchData=.
Data=.
Translations=translations
'@

	Set-Content -LiteralPath $qtConfPath -Value $qtConfContent -NoNewline
}

function Get-PendingRebootReasons {
	$reasons = [ordered]@{
		Hard = New-Object System.Collections.Generic.List[string]
		Soft = New-Object System.Collections.Generic.List[string]
	}

	if (Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending') {
		$reasons.Hard.Add('Component Based Servicing is waiting for a reboot')
	}

	if (Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired') {
		$reasons.Hard.Add('Windows Update is waiting for a reboot')
	}

	$sessionManagerKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager'
	try {
		$pendingRename = (Get-ItemProperty -Path $sessionManagerKey -Name 'PendingFileRenameOperations' -ErrorAction Stop).PendingFileRenameOperations
		if ($pendingRename) {
			$reasons.Soft.Add('Pending file rename operations are present')
		}
	} catch {
	}

	try {
		$bootExecute = (Get-ItemProperty -Path $sessionManagerKey -Name 'BootExecute' -ErrorAction Stop).BootExecute
		$bootEntries = @($bootExecute | Where-Object { $_ -and $_.Trim() })
		if ($bootEntries.Count -gt 1 -or ($bootEntries.Count -eq 1 -and $bootEntries[0].Trim() -ne 'autocheck autochk *')) {
			$reasons.Soft.Add("BootExecute is non-default: $($bootEntries -join '; ')")
		}
	} catch {
	}

	return $reasons
}

function Assert-NoPendingReboot {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Context,

		[switch]$AllowHardPendingReboot
	)

	$reasons = Get-PendingRebootReasons
	if ($reasons.Hard.Count -eq 0 -and $reasons.Soft.Count -eq 0) {
		return
	}

	if ($reasons.Soft.Count -gt 0) {
		$softDetails = ($reasons.Soft | Select-Object -Unique) -join '; '
		Write-Warning "Windows has non-fatal reboot markers before $Context. Proceeding, but the host may still be unstable for long-running work. Details: $softDetails"
	}

	if ($reasons.Hard.Count -gt 0) {
		$hardDetails = ($reasons.Hard | Select-Object -Unique) -join '; '
		if ($AllowHardPendingReboot) {
			Write-Warning "Windows reports a required reboot before $Context, but -AllowPendingReboot was specified. Proceeding anyway. Details: $hardDetails"
			return
		}
		throw "Windows reports a required reboot before $Context. Resolve it first to avoid long-running shared builds being interrupted. Details: $hardDetails"
	}
}

function Assert-SharedWebEngineDeployment {
	param(
		[Parameter(Mandatory = $true)]
		[string]$StageRoot
	)

	$requirements = @(
		@{ Description = "QtWebEngineProcess.exe"; Filter = "QtWebEngineProcess.exe"; Directory = $false },
		@{ Description = "Qt WebEngine ICU payload"; Filter = "icudtl.dat"; Directory = $false },
		@{ Description = "Qt WebEngine resource pack"; Filter = "qtwebengine_resources*.pak"; Directory = $false },
		@{ Description = "Qt WebEngine locale payload"; Filter = "qtwebengine_locales"; Directory = $true },
		@{ Description = "Qt OpenSSL TLS backend"; Filter = "qopensslbackend.dll"; Directory = $false }
	)

	$missing = New-Object System.Collections.Generic.List[string]
	foreach ($requirement in $requirements) {
		$entry = if ($requirement.Directory) {
			Get-ChildItem -Path $StageRoot -Recurse -Directory -Filter $requirement.Filter -ErrorAction SilentlyContinue | Select-Object -First 1
		} else {
			Get-ChildItem -Path $StageRoot -Recurse -File -Filter $requirement.Filter -ErrorAction SilentlyContinue | Select-Object -First 1
		}

		if (-not $entry) {
			$missing.Add($requirement.Description)
		}
	}

	if ($missing.Count -gt 0) {
		throw "Shared WebEngine staging is missing required deployed runtime content after windeployqt: $($missing -join ', ')."
	}
}

function Assert-SharedInstallerPrerequisites {
	param(
		[Parameter(Mandatory = $true)]
		[string]$BuildRoot
	)

	$requiredPaths = @(
		(Join-Path $BuildRoot "installer\dlgbmp.bmp"),
		(Join-Path $BuildRoot "installer\Theme.xml"),
		(Join-Path $BuildRoot "installer\VC_redist.x64.exe"),
		(Join-Path $BuildRoot "installer\icons\mumble.ico"),
		(Join-Path $BuildRoot "licenses\Mumble.rtf")
	)

	$missing = @($requiredPaths | Where-Object { -not (Test-Path -LiteralPath $_) })
	if ($missing.Count -gt 0) {
		throw "Shared installer prerequisites are missing: $($missing -join ', '). Re-run configure with packaging enabled so CMake restores the shared installer assets before building the package."
	}
}

function Invoke-SharedWindowsPackaging {
	param(
		[Parameter(Mandatory = $true)]
		[string]$RepoRoot,

		[Parameter(Mandatory = $true)]
		[string]$BuildRoot,

		[Parameter(Mandatory = $true)]
		[string]$BuildType,

		[switch]$SkipInstaller
	)

	$stageRoot = Join-Path $BuildRoot "shared-webengine-stage"
	$installerWorkDir = Join-Path $BuildRoot "installer\client"
	$cachePath = Join-Path $BuildRoot "CMakeCache.txt"
	$windeployqt = Get-WindeployQtExecutable -Root $env:MUMBLE_ENVIRONMENT_DIR
	$qtToolsBin = Split-Path -Parent $windeployqt
	$environmentBin = Join-Path $env:MUMBLE_ENVIRONMENT_DIR "installed\$env:MUMBLE_VCPKG_TRIPLET\bin"

	Import-VisualStudioDeveloperEnvironment | Out-Null
	Add-ToPathFront $qtToolsBin
	Add-ToPathFront $environmentBin

	if (Test-Path -LiteralPath $stageRoot) {
		Remove-Item -LiteralPath $stageRoot -Recurse -Force
	}

	New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null

	& cmake --install $BuildRoot --config $BuildType --prefix $stageRoot
	if ($LASTEXITCODE -ne 0) {
		throw "cmake --install failed for shared WebEngine payload staging."
	}

	Copy-DirectoryContents -Source (Join-Path $BuildRoot "licenses") -Destination (Join-Path $stageRoot "licenses")
	Copy-DirectoryContents -Source (Join-Path $BuildRoot "plugins") -Destination (Join-Path $stageRoot "plugins")
	Copy-DirectoryContents -Source (Join-Path $BuildRoot "dtln") -Destination (Join-Path $stageRoot "dtln")
	Copy-DirectoryContents -Source (Join-Path $BuildRoot "deepfilternet") -Destination (Join-Path $stageRoot "deepfilternet")
	Copy-DirectoryContents -Source (Join-Path $BuildRoot "rnnoise") -Destination (Join-Path $stageRoot "rnnoise")
	Copy-FileIfExists -Source (Join-Path $BuildRoot "speexdsp.dll") -Destination (Join-Path $stageRoot "speexdsp.dll")
	Copy-FileIfExists -Source (Join-Path $BuildRoot "rnnoise.dll") -Destination (Join-Path $stageRoot "rnnoise.dll")
	Copy-FileIfExists -Source (Join-Path $BuildRoot "deepfilter.dll") -Destination (Join-Path $stageRoot "deepfilter.dll")
	Copy-FileIfExists -Source (Join-Path $BuildRoot "onnxruntime.dll") -Destination (Join-Path $stageRoot "onnxruntime.dll")
	Copy-FileIfExists -Source (Join-Path $BuildRoot "mumble-screen-helper.exe") -Destination (Join-Path $stageRoot "mumble-screen-helper.exe")
	Copy-FileIfExists -Source (Join-Path $BuildRoot "mumble-g15-helper.exe") -Destination (Join-Path $stageRoot "mumble-g15-helper.exe")
	Copy-FileIfExists -Source (Join-Path $BuildRoot "mumble_ol.dll") -Destination (Join-Path $stageRoot "mumble_ol.dll")
	Copy-FileIfExists -Source (Join-Path $BuildRoot "mumble_ol_helper.exe") -Destination (Join-Path $stageRoot "mumble_ol_helper.exe")
	Copy-FileIfExists -Source (Join-Path $BuildRoot "mumble_ol_helper_x64.exe") -Destination (Join-Path $stageRoot "mumble_ol_helper_x64.exe")
	Copy-FileIfExists -Source (Join-Path $BuildRoot "mumble_ol_x64.dll") -Destination (Join-Path $stageRoot "mumble_ol_x64.dll")

	$stageExe = Join-Path $stageRoot "mumble.exe"
	if (-not (Test-Path -LiteralPath $stageExe)) {
		throw "Staged payload is missing mumble.exe at '$stageExe'."
	}

	# Seed the stage with the shared runtime first so windeployqt can resolve
	# ICU and other DLL dependencies without noisy "module could not be found"
	# warnings during its initial scan.
	Copy-SharedEnvironmentRuntime -StageRoot $stageRoot -EnvironmentRoot $env:MUMBLE_ENVIRONMENT_DIR -Triplet $env:MUMBLE_VCPKG_TRIPLET

	$windeployqtArgs = @()
	if ($BuildType -match '^(?i:debug)$') {
		$windeployqtArgs += "--debug"
	} else {
		$windeployqtArgs += "--release"
	}
	$windeployqtArgs += @(
		"--no-system-dxc-compiler",
		"--dir", $stageRoot,
		$stageExe
	)

	& $windeployqt @windeployqtArgs
	if ($LASTEXITCODE -ne 0) {
		throw "windeployqt failed for staged shared WebEngine payload."
	}
	Copy-SharedEnvironmentRuntime -StageRoot $stageRoot -EnvironmentRoot $env:MUMBLE_ENVIRONMENT_DIR -Triplet $env:MUMBLE_VCPKG_TRIPLET
	Copy-SharedQtOpenSslBackend -StageRoot $stageRoot -EnvironmentRoot $env:MUMBLE_ENVIRONMENT_DIR -Triplet $env:MUMBLE_VCPKG_TRIPLET
	Write-SharedQtConf -StageRoot $stageRoot
	Assert-SharedWebEngineDeployment -StageRoot $stageRoot

	if ($SkipInstaller) {
		Write-Host "Skipping shared WebEngine installer generation. The staged payload at '$stageRoot' is ready for validation."
		return
	}

	$vcRedistVersion = Get-CMakeCacheValue -CachePath $cachePath -Name "VC_REDIST_VERSION"
	if ([string]::IsNullOrWhiteSpace($vcRedistVersion)) {
		$vcRedistInstaller = Join-Path $BuildRoot "installer\VC_redist.x64.exe"
		if (Test-Path -LiteralPath $vcRedistInstaller) {
			$vcRedistVersion = (Get-Item -LiteralPath $vcRedistInstaller).VersionInfo.ProductVersion
		}
	}
	if ([string]::IsNullOrWhiteSpace($vcRedistVersion)) {
		throw "Unable to determine VC_REDIST_VERSION from '$cachePath' or '$vcRedistInstaller'."
	}

	$projectVersion = & python (Join-Path $RepoRoot "scripts\mumble-version.py")
	if ($LASTEXITCODE -ne 0) {
		throw "Unable to determine the Mumble project version."
	}
	$projectVersion = $projectVersion.Trim()
	if ($projectVersion -match '^\d+\.\d+$') {
		$projectVersion = "$projectVersion.0"
	}

	$installerVersion = Get-CMakeCacheValue -CachePath $cachePath -Name "MUMBLE_WINDOWS_INSTALLER_VERSION"
	if ([string]::IsNullOrWhiteSpace($installerVersion)) {
		$installerVersion = $projectVersion
	}
	$installerVersion = $installerVersion.Trim()
	if ($installerVersion -match '^\d+\.\d+$') {
		$installerVersion = "$installerVersion.0"
	}

	$installerArgs = @(
		"--version", $projectVersion,
		"--installer-version", $installerVersion,
		"--arch", "x64",
		"--vc-redist-required", $vcRedistVersion,
		"--payload-root", $stageRoot
	)

	if (Get-CMakeCacheBoolean -CachePath $cachePath -Name "translations") {
		$installerArgs += "--all-languages"
	}

	if (Test-Path -LiteralPath (Join-Path $BuildRoot "mumble_ol.dll")) {
		$installerArgs += "--overlay"
	}
	if (Test-Path -LiteralPath (Join-Path $BuildRoot "mumble-g15-helper.exe")) {
		$installerArgs += "--g15"
	}
	if (Test-Path -LiteralPath (Join-Path $BuildRoot "rnnoise.dll")) {
		$installerArgs += "--rnnoise"
	}
	if (Test-Path -LiteralPath (Join-Path $BuildRoot "mumble-screen-helper.exe")) {
		$installerArgs += "--screen-share-helper"
	}

	$wixToolsetAvailable = $false
	if (-not [string]::IsNullOrWhiteSpace($env:WIXSHARP_WIXDIR)) {
		$wixBinDir = $env:WIXSHARP_WIXDIR
		if ((Test-Path -LiteralPath (Join-Path $wixBinDir "wix.exe")) `
			-or ((Test-Path -LiteralPath (Join-Path $wixBinDir "candle.exe")) `
				-and (Test-Path -LiteralPath (Join-Path $wixBinDir "light.exe")))) {
			$wixToolsetAvailable = $true
		}
	}
	if (-not $wixToolsetAvailable) {
		$wixToolsetAvailable = ($null -ne (Get-Command wix.exe -ErrorAction SilentlyContinue)) `
			-or (($null -ne (Get-Command candle.exe -ErrorAction SilentlyContinue)) `
				-and ($null -ne (Get-Command light.exe -ErrorAction SilentlyContinue)))
	}
	if (-not $wixToolsetAvailable) {
		Write-Warning "Skipping shared WebEngine installer generation because WiX binaries are unavailable. The staged payload at '$stageRoot' is ready for local bring-up."
		return
	}

	Assert-SharedInstallerPrerequisites -BuildRoot $BuildRoot
	New-Item -ItemType Directory -Force -Path $installerWorkDir | Out-Null
	Copy-Item -LiteralPath (Join-Path $RepoRoot "installer\MumbleInstall.cs") -Destination $installerWorkDir -Force
	Copy-Item -LiteralPath (Join-Path $RepoRoot "installer\ClientInstaller.cs") -Destination $installerWorkDir -Force
	$cscs = Get-WixSharpExecutable

	Get-ChildItem -LiteralPath $installerWorkDir -File -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -like "*.msi" -or $_.Name -like "*_client-*.exe" } |
		Remove-Item -Force -ErrorAction SilentlyContinue

	Push-Location $installerWorkDir
	try {
		& $cscs -cd MumbleInstall.cs
		if ($LASTEXITCODE -ne 0) {
			throw "cscs failed while compiling MumbleInstall.cs for shared WebEngine packaging."
		}

		& $cscs ClientInstaller.cs @installerArgs
		if ($LASTEXITCODE -ne 0) {
			throw "cscs failed while building the shared WebEngine client installer."
		}
	}
	finally {
		Pop-Location
	}
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

function Import-VisualStudioDeveloperEnvironment {
	param(
		[string]$HostArch = "x64",
		[string]$TargetArch = "x64"
	)

	$vsInstallPath = Get-VisualStudioInstallationPath
	if ([string]::IsNullOrWhiteSpace($vsInstallPath)) {
		Write-Warning "Unable to locate a Visual Studio installation for the shared packaging environment."
		return $false
	}

	$vsDevCmd = Join-Path $vsInstallPath "Common7\Tools\VsDevCmd.bat"
	if (-not (Test-Path -LiteralPath $vsDevCmd)) {
		Write-Warning "VsDevCmd.bat was not found under '$vsInstallPath'."
		return $false
	}

	$command = "`"$vsDevCmd`" -no_logo -host_arch=$HostArch -arch=$TargetArch >nul && set"
	$environmentLines = & cmd.exe /d /s /c $command
	if ($LASTEXITCODE -ne 0) {
		Write-Warning "Failed to import the Visual Studio developer environment from '$vsDevCmd'."
		return $false
	}

	foreach ($line in $environmentLines) {
		if ($line -notmatch '^(?<name>[^=]+)=(?<value>.*)$') {
			continue
		}

		Set-Item -Path "Env:$($Matches.name)" -Value $Matches.value
	}

	return $true
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

function Test-RemoteEnvironmentArchiveExists {
	param(
		[Parameter(Mandatory = $true)]
		[string]$ArchiveUrl
	)

	try {
		$response = Invoke-WebRequest -Method Head -Uri $ArchiveUrl -UseBasicParsing
		return $response.StatusCode -ge 200 -and $response.StatusCode -lt 400
	}
	catch {
		return $false
	}
}

function Assert-RemoteEnvironmentArchiveAvailable {
	param(
		[Parameter(Mandatory = $true)]
		[string]$EnvironmentSource,

		[Parameter(Mandatory = $true)]
		[string]$EnvironmentVersion,

		[Parameter(Mandatory = $true)]
		[string]$EnvironmentDir
	)

	if ((Test-Path -LiteralPath $EnvironmentDir) -and (Get-ChildItem -LiteralPath $EnvironmentDir -Force -ErrorAction SilentlyContinue | Select-Object -First 1)) {
		return
	}

	$archiveUrl = "$EnvironmentSource/$EnvironmentVersion.7z"
	$splitArchiveUrl = "$archiveUrl.001"
	if ((Test-RemoteEnvironmentArchiveExists -ArchiveUrl $archiveUrl) -or
		(Test-RemoteEnvironmentArchiveExists -ArchiveUrl $splitArchiveUrl)) {
		return
	}

	if ($env:MUMBLE_ALLOW_ENVIRONMENT_BOOTSTRAP -eq "ON") {
		Write-Warning "The requested build environment archive is not published; falling back to local bootstrap: $archiveUrl"
		return
	}

	throw "The requested build environment archive is not published: $archiveUrl"
}

function Get-ShortSharedEnvironmentPath {
	param(
		[Parameter(Mandatory = $true)]
		[string]$TargetPath
	)

	$resolvedTarget = [System.IO.Path]::GetFullPath($TargetPath).TrimEnd('\')
	New-Item -ItemType Directory -Force -Path $resolvedTarget | Out-Null

	# Keep the shared environment on the same drive as the checkout. Some Windows
	# dependency builds (for example harfbuzz) invoke Python helpers that compute
	# relative paths and fail when the source tree is seen through a different
	# drive letter than the generated source root.
	$driveRoot = [System.IO.Path]::GetPathRoot($resolvedTarget)
	if ([string]::IsNullOrWhiteSpace($driveRoot)) {
		return $resolvedTarget
	}

	$junctionRoot = Join-Path $driveRoot ".mbe"
	New-Item -ItemType Directory -Force -Path $junctionRoot | Out-Null

	$leafName = Split-Path -Path $resolvedTarget -Leaf
	$sha256 = [System.Security.Cryptography.SHA256]::Create()
	try {
		$hashBytes = $sha256.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($resolvedTarget.ToLowerInvariant()))
	}
	finally {
		$sha256.Dispose()
	}
	$hash = ([System.BitConverter]::ToString($hashBytes)).Replace('-', '').Substring(0, 8).ToLowerInvariant()
	$junctionPath = Join-Path $junctionRoot "$leafName-$hash"

	if (Test-Path -LiteralPath $junctionPath) {
		$existingItem = Get-Item -LiteralPath $junctionPath -ErrorAction SilentlyContinue
		$existingTarget = @($existingItem.Target | ForEach-Object {
			if ([string]::IsNullOrWhiteSpace($_)) {
				return $null
			}

			[System.IO.Path]::GetFullPath($_).TrimEnd('\')
		}) | Select-Object -First 1

		if ($existingItem `
			-and ($existingItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) `
			-and $existingItem.LinkType -eq 'Junction' `
			-and [System.StringComparer]::OrdinalIgnoreCase.Equals($existingTarget, $resolvedTarget)) {
			return $junctionPath
		}

		Write-Warning "Shared environment shortcut path '$junctionPath' already exists but is not the expected junction. Continuing with the original path."
		return $resolvedTarget
	}

	& cmd /d /c "mklink /J `"$junctionPath`" `"$resolvedTarget`"" | Out-Null
	if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $junctionPath)) {
		return $junctionPath
	}

	Write-Warning "Unable to create a short same-drive junction for '$resolvedTarget'. Continuing with the original path."
	return $resolvedTarget
}

function Test-SharedEnvironmentReady {
	param(
		[Parameter(Mandatory = $true)]
		[string]$EnvironmentDir,

		[Parameter(Mandatory = $true)]
		[string]$Triplet
	)

	$requiredPaths = @(
		(Join-Path $EnvironmentDir "vcpkg.exe"),
		(Join-Path $EnvironmentDir "scripts\buildsystems\vcpkg.cmake"),
		(Join-Path $EnvironmentDir "installed\$Triplet\share\Qt6WebChannel"),
		(Join-Path $EnvironmentDir "installed\$Triplet\share\Qt6WebEngineWidgets"),
		(Join-Path $EnvironmentDir "installed\$Triplet\tools\Qt6\bin\windeployqt.exe")
	)

	foreach ($path in $requiredPaths) {
		if (-not (Test-Path -LiteralPath $path)) {
			return $false
		}
	}

	return $true
}

function Ensure-LocalBuildTooling {
	$visualStudioPath = Get-VisualStudioInstallationPath
	if ($visualStudioPath) {
		Add-ToPathFront (Join-Path $visualStudioPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin")
		Add-ToPathFront (Join-Path $visualStudioPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja")
	}

	foreach ($gitPathEntry in @(
		"C:\Program Files\Git\cmd",
		"C:\Program Files\Git\bin",
		"C:\Program Files (x86)\Git\cmd",
		"C:\Program Files (x86)\Git\bin"
	)) {
		Add-ToPathFront $gitPathEntry
	}

	foreach ($pythonRoot in @(
		(Join-Path $env:LOCALAPPDATA "Programs\Python\Python312"),
		"C:\Program Files\Python312",
		"C:\Python312"
	)) {
		Add-ToPathFront $pythonRoot
		Add-ToPathFront (Join-Path $pythonRoot "Scripts")
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
		[string]$BuildRoot
	)

	if (-not (Test-Path -LiteralPath $BuildRoot)) {
		return @()
	}

	return Get-ChildItem -Path $BuildRoot -Recurse -File -Include "mumble*.msi", "mumble*.exe" |
		Where-Object { $_.Name -ne "mumble-screen-helper.exe" } |
		Sort-Object -Property FullName
}

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..\..")).Path
$repoRootForBash = To-BashPath $repoRoot
$buildDirectoryName = if ($SharedWebEngine) { "build-shared-webengine" } else { "build" }
$buildRoot = Join-Path $repoRoot $buildDirectoryName
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
	$env:MUMBLE_BUILD_DIR_OVERRIDE = To-BashPath $buildRoot
	$env:BUILD_TYPE = $BuildType
	$env:CMAKE_OPTIONS = "-Dtests=OFF -Dsymbols=ON -Ddisplay-install-paths=ON -Dtest-lto=OFF"
	if ($SharedWebEngine) {
		# The current shared zeroc-ice-mumble install is incomplete for consumers, so
		# keep the shared client bring-up path from configuring murmur's optional Ice RPC.
		$env:CMAKE_OPTIONS = "$($env:CMAKE_OPTIONS) -Dice=OFF"
	}
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

		$legacyEnvironmentDir = Join-Path $repoRoot "build_env\$EnvironmentRelease-$EnvironmentCommit"
		$preferredEnvironmentDir = if ($SharedWebEngine) {
			Join-Path $repoRoot "build_env\$EnvironmentRelease-$EnvironmentCommit-shared"
		} else {
			Join-Path $repoRoot "build_env\$EnvironmentRelease-$EnvironmentCommit-static"
		}

		if ([string]::IsNullOrWhiteSpace($env:MUMBLE_ENVIRONMENT_SOURCE_OVERRIDE)) {
			$env:MUMBLE_ENVIRONMENT_SOURCE_OVERRIDE = "https://github.com/mumble-voip/vcpkg/releases/download/$EnvironmentRelease"
		}
		$env:MUMBLE_ENVIRONMENT_COMMIT_OVERRIDE = $EnvironmentCommit
		$customEnvironmentDir = $preferredEnvironmentDir
		if ((-not $SharedWebEngine) -and (-not $InstallDependencies) `
			-and (-not (Test-Path -LiteralPath $preferredEnvironmentDir)) `
			-and (Test-Path -LiteralPath $legacyEnvironmentDir)) {
			$customEnvironmentDir = $legacyEnvironmentDir
		}
		if ($SharedWebEngine) {
			$customEnvironmentDir = Get-ShortSharedEnvironmentPath -TargetPath $customEnvironmentDir
		}
		$env:MUMBLE_ENVIRONMENT_DIR_OVERRIDE = To-BashPath $customEnvironmentDir
	}

	Ensure-LocalBuildTooling
	Initialize-LocalOnnxRuntimeRoot -RepoRoot $repoRoot
	Initialize-RustCargoPath
	$windowsBuildType = if ($SharedWebEngine) { "shared" } else { "static" }
	if ($SharedWebEngine) {
		$env:MUMBLE_ALLOW_ENVIRONMENT_BOOTSTRAP = "ON"
	}

	if ($InstallDependencies -or $SharedWebEngine) {
		Assert-NoPendingReboot -Context 'the Windows shared build/dependency bootstrap' `
			-AllowHardPendingReboot:$AllowPendingReboot
	}

	Assert-CommandAvailable git
	Assert-CommandAvailable cmake
	Assert-CommandAvailable ninja

	Invoke-BashScript -ScriptPath (Join-Path $repoRoot ".github\workflows\set_environment_variables.sh") -Arguments @(
		"windows-2025-vs2026",
		$windowsBuildType,
		"x86_64",
		$repoRootForBash
	)
	Set-EnvironmentVariablesFromFile -FilePath $githubEnvFile

	if ($SharedWebEngine -and -not $InstallDependencies) {
		if (-not (Test-SharedEnvironmentReady -EnvironmentDir $env:MUMBLE_ENVIRONMENT_DIR -Triplet $env:MUMBLE_VCPKG_TRIPLET)) {
			throw "Shared WebEngine dependencies are not ready under '$env:MUMBLE_ENVIRONMENT_DIR'. Re-run with -SharedWebEngine -InstallDependencies to bootstrap the local x64-windows environment."
		}
	}

	if ($InstallDependencies -and -not $SharedWebEngine) {
		Assert-RemoteEnvironmentArchiveAvailable -EnvironmentSource $env:MUMBLE_ENVIRONMENT_SOURCE `
			-EnvironmentVersion $env:MUMBLE_ENVIRONMENT_VERSION -EnvironmentDir $env:MUMBLE_ENVIRONMENT_DIR
	}

	if ($InstallDependencies) {
		Invoke-BashScript -ScriptPath (Join-Path $repoRoot ".github\actions\install-dependencies\main.sh") -Arguments @(
			"windows-2025-vs2026",
			$windowsBuildType,
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
			$windowsBuildType,
			"x86_64"
		)
	}

	if (-not $SkipBuild) {
		$env:MUMBLE_CI_PHASE = "build"
		Invoke-BashScript -ScriptPath $buildScript -Arguments @(
			"windows-2025-vs2026",
			$windowsBuildType,
			"x86_64"
		)
	}

	Remove-Item Env:MUMBLE_CI_PHASE -ErrorAction SilentlyContinue

	if ($SharedWebEngine -and $EnablePackaging) {
		Invoke-SharedWindowsPackaging -RepoRoot $repoRoot -BuildRoot $buildRoot -BuildType $BuildType `
			-SkipInstaller:$SkipSharedInstaller
	}

	if ($VerifyHelperRuntime) {
		if (-not (Get-Command ffmpeg.exe -ErrorAction SilentlyContinue)) {
			throw "ffmpeg was not found in PATH. Re-run with -InstallFfmpeg to download a portable build for the screen-helper runtime check."
		}

		$env:MUMBLE_SCREENSHARE_TEST_PATTERN = "1"
		$env:MUMBLE_SCREENSHARE_CAPTURE_SOURCE = "test-pattern"
		$env:MUMBLE_SCREENSHARE_HEADLESS_VIEW = "1"

		$helperSearchRoots = @()
		if ($SharedWebEngine) {
			$stagedHelperRoot = Join-Path $buildRoot "shared-webengine-stage"
			if (Test-Path -LiteralPath $stagedHelperRoot) {
				$helperSearchRoots += $stagedHelperRoot
			}
		}
		$helperSearchRoots += $buildRoot

		$helper = $null
		foreach ($helperRoot in $helperSearchRoots) {
			$helper = Get-ChildItem -Path $helperRoot -Recurse -File -Filter "mumble-screen-helper.exe" |
				Select-Object -First 1
			if ($helper) {
				break
			}
		}
		if (-not $helper) {
			throw "Unable to locate mumble-screen-helper.exe under '$buildRoot'."
		}

		$logPath = Join-Path $buildRoot "windows-screen-share-helper.log"
		$capabilitiesPath = Join-Path $buildRoot "windows-screen-share-capabilities.json"
		$selfTestPath = Join-Path $buildRoot "windows-screen-share-self-test.json"
		$artifactListPath = Join-Path $buildRoot "windows-client-artifacts.txt"

		$capabilityProbe = Invoke-ProcessWithTimeout -FilePath $helper.FullName `
			-Arguments @("--diagnostics-log-file", $logPath, "--print-capabilities-json") `
			-Description "Helper capability probe" `
			-TimeoutSeconds 180
		if (-not [string]::IsNullOrWhiteSpace($capabilityProbe.StdErr)) {
			Write-Host $capabilityProbe.StdErr.TrimEnd()
		}
		if ($capabilityProbe.ExitCode -ne 0) {
			throw "Helper capability probe failed with exit code $($capabilityProbe.ExitCode)."
		}
		$capabilitiesJson = $capabilityProbe.StdOut
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

		$selfTestProbe = Invoke-ProcessWithTimeout -FilePath $helper.FullName `
			-Arguments @("--diagnostics-log-file", $logPath, "--self-test") `
			-Description "Helper self-test" `
			-TimeoutSeconds 180
		if (-not [string]::IsNullOrWhiteSpace($selfTestProbe.StdErr)) {
			Write-Host $selfTestProbe.StdErr.TrimEnd()
		}
		if ($selfTestProbe.ExitCode -ne 0) {
			throw "Helper self-test failed with exit code $($selfTestProbe.ExitCode)."
		}
		$selfTestJson = $selfTestProbe.StdOut
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

		$artifacts = Get-RepoArtifactPaths -BuildRoot $buildRoot
		$artifacts.FullName | Set-Content -LiteralPath $artifactListPath
		if (-not $artifacts) {
			throw "No build artifacts were found under '$buildRoot'."
		}
	}

	$finalArtifacts = Get-RepoArtifactPaths -BuildRoot $buildRoot
	if (-not $finalArtifacts) {
		Write-Warning "Build completed, but no build artifacts were found under '$buildRoot'."
	} else {
		Write-Host ""
		Write-Host "Windows build artifacts:"
		$finalArtifacts | ForEach-Object { Write-Host $_.FullName }
	}
} catch {
	$message = $_.Exception.Message
	if ($env:GITHUB_ACTIONS -eq "true") {
		$escapedMessage = $message.Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
		Write-Host "::error file=scripts/windows/build-local-windows-client.ps1,title=Windows build wrapper failed::$escapedMessage"
	}
	throw
} finally {
	if (Test-Path -LiteralPath $githubEnvFile) {
		Remove-Item -LiteralPath $githubEnvFile -Force
	}
}
