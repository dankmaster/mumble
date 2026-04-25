[CmdletBinding()]
param(
	[string]$EnvironmentDir = "",
	[string]$EnvironmentRelease = "",
	[string]$EnvironmentCommit = "",
	[ValidateSet("shared", "static")]
	[string]$BuildType = "shared",
	[ValidateSet("x86_64")]
	[string]$Architecture = "x86_64",
	[string]$VolumeSize = "1900m",
	[string]$OutputDirectory = "",
	[string]$ReleaseTag = "",
	[string]$Repository = "",
	[switch]$Upload,
	[switch]$CreateRelease,
	[switch]$Clobber,
	[switch]$DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-SevenZipExecutable {
	$commandCandidates = @(
		(Get-Command 7z.exe -ErrorAction SilentlyContinue),
		(Get-Command 7za.exe -ErrorAction SilentlyContinue)
	) | Where-Object { $_ }

	foreach ($candidate in $commandCandidates) {
		return $candidate.Source
	}

	$pathCandidates = @(
		"C:\Program Files\7-Zip\7z.exe",
		"C:\Program Files (x86)\7-Zip\7z.exe",
		"C:\Program Files\PeaZip\res\bin\7z\7z.exe",
		"C:\Program Files (x86)\PeaZip\res\bin\7z\7z.exe",
		"C:\Apps\7-Zip\7z.exe",
		"C:\Apps\7-Zip\7za.exe",
		"C:\Apps\VMware\VMware Workstation\7za.exe"
	)

	foreach ($candidate in $pathCandidates) {
		if (Test-Path -LiteralPath $candidate) {
			return $candidate
		}
	}

	throw "Unable to locate 7z.exe or 7za.exe. Install 7-Zip or PeaZip, or add a 7-Zip-compatible CLI to PATH."
}

function Get-GitHubCliExecutable {
	$gh = Get-Command gh.exe -ErrorAction SilentlyContinue
	if ($gh) {
		return $gh.Source
	}

	throw "Unable to locate gh.exe. Install GitHub CLI before attempting release uploads."
}

function Get-OriginRepository {
	param(
		[Parameter(Mandatory = $true)]
		[string]$RepoRoot
	)

	$originUrl = (& git -C $RepoRoot remote get-url origin).Trim()
	if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($originUrl)) {
		throw "Unable to determine the origin remote URL for '$RepoRoot'."
	}

	if ($originUrl -match 'github\.com[:/](?<owner>[^/]+)/(?<repo>[^/]+?)(?:\.git)?$') {
		return "$($Matches.owner)/$($Matches.repo)"
	}

	throw "The origin remote does not look like a GitHub repository URL: '$originUrl'"
}

function Resolve-EnvironmentMetadata {
	param(
		[Parameter(Mandatory = $true)]
		[string]$RepoRoot,

		[string]$EnvironmentDir,
		[string]$EnvironmentRelease,
		[string]$EnvironmentCommit,
		[string]$BuildType
	)

	$resolvedBuildType = $BuildType
	$resolvedRelease = $EnvironmentRelease
	$resolvedCommit = $EnvironmentCommit
	$resolvedEnvironmentDir = $EnvironmentDir

	if ([string]::IsNullOrWhiteSpace($resolvedEnvironmentDir)) {
		if ([string]::IsNullOrWhiteSpace($resolvedRelease) -or [string]::IsNullOrWhiteSpace($resolvedCommit)) {
			throw "Specify -EnvironmentDir or both -EnvironmentRelease and -EnvironmentCommit."
		}

		$resolvedEnvironmentDir = Join-Path $RepoRoot "build_env\$resolvedRelease-$resolvedCommit-$resolvedBuildType"
	}

	$resolvedEnvironmentDir = [System.IO.Path]::GetFullPath($resolvedEnvironmentDir)
	$leafName = Split-Path -Path $resolvedEnvironmentDir -Leaf

	if ($leafName -match '^(?<release>.+)-(?<commit>[0-9a-f]{10})(?:-(?<kind>shared|static))?$') {
		if ([string]::IsNullOrWhiteSpace($resolvedRelease)) {
			$resolvedRelease = $Matches.release
		}
		if ([string]::IsNullOrWhiteSpace($resolvedCommit)) {
			$resolvedCommit = $Matches.commit
		}
		if ($Matches.kind) {
			$resolvedBuildType = $Matches.kind
		}
	}

	if ([string]::IsNullOrWhiteSpace($resolvedRelease) -or [string]::IsNullOrWhiteSpace($resolvedCommit)) {
		throw "Unable to determine the environment release/commit from '$resolvedEnvironmentDir'. Pass them explicitly."
	}

	return [PSCustomObject]@{
		EnvironmentDir = $resolvedEnvironmentDir
		EnvironmentRelease = $resolvedRelease
		EnvironmentCommit = $resolvedCommit
		BuildType = $resolvedBuildType
	}
}

function Get-VcpkgTriplet {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Architecture,

		[Parameter(Mandatory = $true)]
		[string]$BuildType
	)

	switch ("$Architecture/$BuildType") {
		"x86_64/shared" { return "x64-windows" }
		"x86_64/static" { return "x64-windows-static-md" }
		default {
			throw "Unsupported build environment combination: architecture='$Architecture', buildType='$BuildType'."
		}
	}
}

function Assert-EnvironmentLooksReady {
	param(
		[Parameter(Mandatory = $true)]
		[string]$EnvironmentDir,

		[Parameter(Mandatory = $true)]
		[string]$Triplet,

		[Parameter(Mandatory = $true)]
		[string]$BuildType
	)

	$requiredPaths = @(
		(Join-Path $EnvironmentDir "vcpkg.exe"),
		(Join-Path $EnvironmentDir "scripts\buildsystems\vcpkg.cmake"),
		(Join-Path $EnvironmentDir "installed\$Triplet")
	)

	if ($BuildType -eq "shared") {
		$requiredPaths += @(
			(Join-Path $EnvironmentDir "installed\$Triplet\share\Qt6WebChannel"),
			(Join-Path $EnvironmentDir "installed\$Triplet\share\Qt6WebEngineWidgets"),
			(Join-Path $EnvironmentDir "installed\$Triplet\tools\Qt6\bin\windeployqt.exe")
		)
	}

	$missing = @($requiredPaths | Where-Object { -not (Test-Path -LiteralPath $_) })
	if ($missing.Count -gt 0) {
		throw "The build environment under '$EnvironmentDir' is missing required content: $($missing -join ', ')"
	}
}

function New-ReleaseNotes {
	param(
		[Parameter(Mandatory = $true)]
		[string]$EnvironmentRelease,

		[Parameter(Mandatory = $true)]
		[string]$EnvironmentCommit
	)

	return @"
Prebuilt Windows build environment archives for environment release $EnvironmentRelease.

- Environment commit: $EnvironmentCommit
- Generated from a local build_env checkout
- Intended for GitHub Actions release-asset reuse
"@
}

function Test-GitHubReleaseExists {
	param(
		[Parameter(Mandatory = $true)]
		[string]$GitHubCli,

		[Parameter(Mandatory = $true)]
		[string]$Tag,

		[Parameter(Mandatory = $true)]
		[string]$Repository
	)

	# Windows PowerShell can surface native stderr text as an error record when a
	# release does not exist yet. Redirect stderr explicitly so we can inspect
	# the exit code and create the release in the normal path.
	& $GitHubCli release view $Tag --repo $Repository 2>$null | Out-Null
	return ($LASTEXITCODE -eq 0)
}

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..\..")).Path

$metadata = Resolve-EnvironmentMetadata `
	-RepoRoot $repoRoot `
	-EnvironmentDir $EnvironmentDir `
	-EnvironmentRelease $EnvironmentRelease `
	-EnvironmentCommit $EnvironmentCommit `
	-BuildType $BuildType

$triplet = Get-VcpkgTriplet -Architecture $Architecture -BuildType $metadata.BuildType
Assert-EnvironmentLooksReady -EnvironmentDir $metadata.EnvironmentDir -Triplet $triplet -BuildType $metadata.BuildType

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
	$OutputDirectory = Join-Path $repoRoot ".tmp\build-env-archives"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$assetName = "mumble_env.$triplet.$($metadata.EnvironmentCommit).7z"
$archivePath = Join-Path $OutputDirectory $assetName
$releaseTagToUse = if ([string]::IsNullOrWhiteSpace($ReleaseTag)) {
	"build-env-$($metadata.EnvironmentRelease)"
} else {
	$ReleaseTag
}

$repositoryToUse = $Repository
if ($Upload -and [string]::IsNullOrWhiteSpace($repositoryToUse)) {
	$repositoryToUse = Get-OriginRepository -RepoRoot $repoRoot
}

$existingArchiveParts = @(Get-ChildItem -Path "$archivePath*" -File -ErrorAction SilentlyContinue)
if ($existingArchiveParts.Count -gt 0) {
	if (-not $Clobber) {
		throw "Archive output already exists at '$archivePath*'. Pass -Clobber to replace it."
	}

	$existingArchiveParts | Remove-Item -Force
}

Write-Host "Build environment source: $($metadata.EnvironmentDir)"
Write-Host "Resolved environment release: $($metadata.EnvironmentRelease)"
Write-Host "Resolved environment commit: $($metadata.EnvironmentCommit)"
Write-Host "Resolved build type: $($metadata.BuildType)"
Write-Host "Triplet: $triplet"
Write-Host "Archive path: $archivePath"
Write-Host "Volume size: $VolumeSize"
if ($Upload) {
	Write-Host "GitHub repository: $repositoryToUse"
	Write-Host "GitHub release tag: $releaseTagToUse"
}

if ($DryRun) {
	Write-Host "Dry run requested; skipping archive creation and release upload."
	return
}

$sevenZip = Get-SevenZipExecutable
$environmentParent = Split-Path -Parent $metadata.EnvironmentDir
$environmentLeaf = Split-Path -Leaf $metadata.EnvironmentDir

Push-Location $environmentParent
try {
	$sevenZipArgs = @("a", "-t7z", "-mx=9", "-mmt=on")
	if (-not [string]::IsNullOrWhiteSpace($VolumeSize)) {
		$sevenZipArgs += "-v$VolumeSize"
	}
	$sevenZipArgs += @($archivePath, $environmentLeaf)

	& $sevenZip @sevenZipArgs
	if ($LASTEXITCODE -ne 0) {
		throw "7-Zip failed while creating '$archivePath'."
	}
}
finally {
	Pop-Location
}

$archiveParts = @(Get-ChildItem -Path "$archivePath*" -File -ErrorAction Stop | Sort-Object -Property Name)
if ($archiveParts.Count -eq 0) {
	throw "7-Zip completed, but no archive files were produced for '$archivePath'."
}

$totalBytes = ($archiveParts | Measure-Object -Property Length -Sum).Sum
Write-Host ("Archive parts: {0}" -f ($archiveParts.Name -join ", "))
Write-Host ("Total archive size: {0:N2} GB" -f ($totalBytes / 1GB))
$archiveParts | ForEach-Object {
	$archiveHash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
	Write-Host ("{0} SHA256: {1}" -f $_.Name, $archiveHash.Hash)
}

if (-not $Upload) {
	return
}

$gh = Get-GitHubCliExecutable
& $gh auth status | Out-Null
if ($LASTEXITCODE -ne 0) {
	throw "GitHub CLI is not authenticated. Run 'gh auth login' before using -Upload."
}

$releaseExists = Test-GitHubReleaseExists -GitHubCli $gh -Tag $releaseTagToUse -Repository $repositoryToUse
if (-not $releaseExists) {
	if (-not $CreateRelease) {
		throw "Release '$releaseTagToUse' does not exist in '$repositoryToUse'. Pass -CreateRelease to create it."
	}

	$releaseNotes = New-ReleaseNotes -EnvironmentRelease $metadata.EnvironmentRelease -EnvironmentCommit $metadata.EnvironmentCommit
	$releaseCreateArgs = @(
		"release", "create", $releaseTagToUse,
		"--repo", $repositoryToUse,
		"--title", "Build environment $($metadata.EnvironmentRelease)",
		"--notes", $releaseNotes
	) + @($archiveParts.FullName)
	& $gh @releaseCreateArgs
	if ($LASTEXITCODE -ne 0) {
		throw "Failed to create release '$releaseTagToUse' in '$repositoryToUse'."
	}
} else {
	$uploadArgs = @("release", "upload", $releaseTagToUse, "--repo", $repositoryToUse) + @($archiveParts.FullName)
	if ($Clobber) {
		$uploadArgs += "--clobber"
	}

	& $gh @uploadArgs
	if ($LASTEXITCODE -ne 0) {
		throw "Failed to upload '$archivePath' to release '$releaseTagToUse' in '$repositoryToUse'."
	}
}

Write-Host "Published $assetName to https://github.com/$repositoryToUse/releases/tag/$releaseTagToUse"
