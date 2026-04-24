[CmdletBinding()]
param(
	[string]$BuildRoot = ".\build",
	[string]$ArtifactListPath = "",
	[string]$StageRoot = "",
	[switch]$RequireStage,
	[switch]$RequireClient,
	[switch]$RequireServer,
	[switch]$RequireScreenHelper,
	[switch]$RequireClientInstaller,
	[switch]$RequireServerInstaller,
	[switch]$RequireSpeechCleanup
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-ExistingPath {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Path
	)

	if (-not (Test-Path -LiteralPath $Path)) {
		throw "Required path does not exist: '$Path'."
	}

	return (Resolve-Path -LiteralPath $Path).Path
}

function Get-RequiredBinaryNames {
	$explicitSelection = $RequireClient.IsPresent -or $RequireServer.IsPresent -or $RequireScreenHelper.IsPresent
	if (-not $explicitSelection) {
		return @(
			"mumble.exe",
			"mumble-server.exe",
			"mumble-screen-helper.exe"
		)
	}

	$required = New-Object System.Collections.Generic.List[string]
	if ($RequireClient) {
		$required.Add("mumble.exe")
	}
	if ($RequireServer) {
		$required.Add("mumble-server.exe")
	}
	if ($RequireScreenHelper) {
		$required.Add("mumble-screen-helper.exe")
	}

	if ($required.Count -eq 0) {
		throw "No required Windows binaries were selected for validation."
	}

	return $required
}

function Find-Binary {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Root,

		[Parameter(Mandatory = $true)]
		[string]$BinaryName
	)

	return Get-ChildItem -Path $Root -Recurse -File -Filter $BinaryName -ErrorAction SilentlyContinue |
		Sort-Object -Property FullName |
		Select-Object -First 1
}

function Assert-BinarySet {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Label,

		[Parameter(Mandatory = $true)]
		[string]$Root,

		[Parameter(Mandatory = $true)]
		[string[]]$BinaryNames
	)

	$verifiedPaths = New-Object System.Collections.Generic.List[string]
	$missing = New-Object System.Collections.Generic.List[string]

	foreach ($binaryName in $BinaryNames) {
		$binary = Find-Binary -Root $Root -BinaryName $binaryName
		if ($binary) {
			$verifiedPaths.Add($binary.FullName)
		} else {
			$missing.Add($binaryName)
		}
	}

	if ($missing.Count -gt 0) {
		throw "$Label is missing required Windows build outputs: $($missing -join ', ')."
	}

	Write-Host "$Label verified:"
	foreach ($path in $verifiedPaths) {
		Write-Host "  $path"
	}

	return $verifiedPaths
}

function Assert-SpeechCleanupPayload {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Label,

		[Parameter(Mandatory = $true)]
		[string]$Root
	)

	$requiredRelativePaths = @(
		"onnxruntime.dll",
		"deepfilter.dll",
		"rnnoise.dll",
		"dtln\baseline\model_1.onnx",
		"dtln\baseline\model_2.onnx",
		"dtln\norm_500h\model_1.onnx",
		"dtln\norm_500h\model_2.onnx",
		"dtln\norm_40h\model_1.onnx",
		"dtln\norm_40h\model_2.onnx",
		"rnnoise\rnnoise_little.weights_blob.bin"
	)

	$missing = New-Object System.Collections.Generic.List[string]
	foreach ($relativePath in $requiredRelativePaths) {
		$fullPath = Join-Path $Root $relativePath
		if (-not (Test-Path -LiteralPath $fullPath)) {
			$missing.Add($relativePath)
		}
	}

	$deepFilterArchive = Get-ChildItem -Path (Join-Path $Root "deepfilternet") -File -Filter "*.tar.gz" -ErrorAction SilentlyContinue |
		Select-Object -First 1
	if (-not $deepFilterArchive) {
		$missing.Add("deepfilternet\*.tar.gz")
	}

	if ($missing.Count -gt 0) {
		throw "$Label is missing required speech-cleanup payload files: $($missing -join ', ')."
	}

	Write-Host "$Label speech-cleanup payload verified."
}

function Get-ArtifactPaths {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Root
	)

	return Get-ChildItem -Path $Root -Recurse -File -Include "mumble*.exe", "mumble*.msi" -ErrorAction SilentlyContinue |
		Sort-Object -Property FullName |
		ForEach-Object { $_.FullName }
}

function Assert-InstallerPattern {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Label,

		[Parameter(Mandatory = $true)]
		[string]$Root,

		[Parameter(Mandatory = $true)]
		[string]$Pattern,

		[Parameter(Mandatory = $true)]
		[string]$Description
	)

	$matches = @(Get-ChildItem -Path $Root -Recurse -File -Filter $Pattern -ErrorAction SilentlyContinue |
		Sort-Object -Property FullName)
	if ($matches.Count -eq 0) {
		throw "$Label is missing required Windows installer output: $Description ($Pattern)."
	}

	Write-Host "$Label installer output verified ($Description):"
	foreach ($match in $matches) {
		Write-Host "  $($match.FullName)"
	}

	return $matches.FullName
}

$buildRootPath = Resolve-ExistingPath -Path $BuildRoot
$allArtifacts = New-Object System.Collections.Generic.List[string]
$requiredBinaryNames = Get-RequiredBinaryNames

foreach ($verifiedPath in Assert-BinarySet -Label "Build root '$buildRootPath'" -Root $buildRootPath -BinaryNames $requiredBinaryNames) {
	$allArtifacts.Add($verifiedPath)
}

if ($RequireSpeechCleanup) {
	Assert-SpeechCleanupPayload -Label "Build root '$buildRootPath'" -Root $buildRootPath
}

foreach ($artifactPath in Get-ArtifactPaths -Root $buildRootPath) {
	$allArtifacts.Add($artifactPath)
}

if ($RequireClientInstaller) {
	foreach ($artifactPath in Assert-InstallerPattern -Label "Build root '$buildRootPath'" -Root $buildRootPath -Pattern "*_client-*.exe" -Description "client installer bootstrapper") {
		$allArtifacts.Add($artifactPath)
	}
	foreach ($artifactPath in Assert-InstallerPattern -Label "Build root '$buildRootPath'" -Root $buildRootPath -Pattern "*client*.msi" -Description "client installer MSI") {
		$allArtifacts.Add($artifactPath)
	}
}

if ($RequireServerInstaller) {
	foreach ($artifactPath in Assert-InstallerPattern -Label "Build root '$buildRootPath'" -Root $buildRootPath -Pattern "*_server-*.exe" -Description "server installer bootstrapper") {
		$allArtifacts.Add($artifactPath)
	}
	foreach ($artifactPath in Assert-InstallerPattern -Label "Build root '$buildRootPath'" -Root $buildRootPath -Pattern "*server*.msi" -Description "server installer MSI") {
		$allArtifacts.Add($artifactPath)
	}
}

if ($RequireStage) {
	if ([string]::IsNullOrWhiteSpace($StageRoot)) {
		throw "RequireStage was specified but no StageRoot was provided."
	}

	$stageRootPath = Resolve-ExistingPath -Path $StageRoot
	foreach ($verifiedPath in Assert-BinarySet -Label "Stage root '$stageRootPath'" -Root $stageRootPath -BinaryNames $requiredBinaryNames) {
		$allArtifacts.Add($verifiedPath)
	}
	if ($RequireSpeechCleanup) {
		Assert-SpeechCleanupPayload -Label "Stage root '$stageRootPath'" -Root $stageRootPath
	}
	foreach ($artifactPath in Get-ArtifactPaths -Root $stageRootPath) {
		$allArtifacts.Add($artifactPath)
	}
}

$uniqueArtifacts = @($allArtifacts | Sort-Object -Unique)
if ($uniqueArtifacts.Count -eq 0) {
	throw "No Windows build artifacts were found under '$buildRootPath'."
}

if (-not [string]::IsNullOrWhiteSpace($ArtifactListPath)) {
	$artifactListParent = Split-Path -Parent $ArtifactListPath
	if (-not [string]::IsNullOrWhiteSpace($artifactListParent)) {
		New-Item -ItemType Directory -Force -Path $artifactListParent | Out-Null
	}

	Set-Content -LiteralPath $ArtifactListPath -Value $uniqueArtifacts
	Write-Host "Wrote Windows artifact manifest to '$ArtifactListPath'."
}

Write-Host ""
Write-Host "Collected Windows artifacts:"
foreach ($artifactPath in $uniqueArtifacts) {
	Write-Host $artifactPath
}
