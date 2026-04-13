param(
    [string]$Version = "",
    [string]$CMakeBuildDir = "build-windows-release",
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [string]$PackageBaseName = "",
    [string]$DotnetPath = ""
)

$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Get-RepositoryVersion {
    param([string]$RepoRoot)

    $cmakeLists = Join-Path $RepoRoot "CMakeLists.txt"
    if (-not (Test-Path $cmakeLists)) {
        throw "CMakeLists.txt not found at $cmakeLists"
    }

    $content = Get-Content -LiteralPath $cmakeLists -Raw
    $match = [regex]::Match($content, 'project\s*\(\s*network_speaker\s+VERSION\s+(?<version>\d+\.\d+\.\d+)')
    if (-not $match.Success) {
        throw "Unable to parse repository version from $cmakeLists"
    }

    return $match.Groups["version"].Value
}

function Get-DotnetExecutable {
    param([string]$RequestedPath)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        if (-not (Test-Path $RequestedPath)) {
            throw "dotnet executable not found at $RequestedPath"
        }

        return (Resolve-Path $RequestedPath).Path
    }

    $command = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        (Join-Path ${env:ProgramFiles} "dotnet\dotnet.exe"),
        (Join-Path ${env:ProgramW6432} "dotnet\dotnet.exe"),
        "C:\Program Files\dotnet\dotnet.exe",
        "E:\Program Files\dotnet\dotnet.exe"
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "dotnet is not installed or not in PATH. Install the .NET 10 SDK first, or pass -DotnetPath."
}

function Assert-DotnetSdk {
    param([string]$DotnetExecutable)

    $sdks = & $DotnetExecutable --list-sdks
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to inspect installed .NET SDKs via $DotnetExecutable"
    }

    if (-not ($sdks | Select-String -Pattern '^10\.' -Quiet)) {
        throw ".NET 10 SDK was not found. Install .NET 10 SDK before packaging."
    }
}

function Get-VcVarsPath {
    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found at $vswhere"
    }

    $vcvars = & $vswhere -products * -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find **\vcvars64.bat |
        Select-Object -First 1

    if (-not $vcvars) {
        throw "vcvars64.bat not found. Install Visual Studio Build Tools with C++ workload."
    }

    return $vcvars
}

function Invoke-VsDevCommand {
    param(
        [string]$Command,
        [string]$WorkingDirectory
    )

    $vcvars = Get-VcVarsPath
    Push-Location $WorkingDirectory
    try {
        $cmd = "call `"$vcvars`" >nul && $Command"
        cmd.exe /c $cmd
        if ($LASTEXITCODE -ne 0) {
            throw "Command failed: $Command"
        }
    }
    finally {
        Pop-Location
    }
}

function Get-NumericVersion {
    param([string]$DisplayVersion)

    $match = [regex]::Match($DisplayVersion, '^(?<version>\d+\.\d+\.\d+)')
    if (-not $match.Success) {
        throw "Version '$DisplayVersion' must start with a numeric x.y.z prefix."
    }

    return $match.Groups["version"].Value
}

$repoRoot = Get-RepoRoot
$dotnet = Get-DotnetExecutable -RequestedPath $DotnetPath
Assert-DotnetSdk -DotnetExecutable $dotnet

if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = Get-RepositoryVersion -RepoRoot $repoRoot
}

$numericVersion = Get-NumericVersion -DisplayVersion $Version
if ([string]::IsNullOrWhiteSpace($PackageBaseName)) {
    $PackageBaseName = "NetworkSpeaker-$Version-win-x64"
}

$publishDir = Join-Path $repoRoot "artifacts\windows-launcher\publish"
$releaseDir = Join-Path $repoRoot "artifacts\release"
$launcherProject = Join-Path $repoRoot "apps\windows-launcher\NetworkSpeaker.Launcher\NetworkSpeaker.Launcher.csproj"
$installerProject = Join-Path $repoRoot "installer\windows\NetworkSpeaker.Installer.wixproj"

Remove-Item -LiteralPath $publishDir -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $releaseDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $publishDir -Force | Out-Null
New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null

Invoke-VsDevCommand -WorkingDirectory $repoRoot -Command "cmake -S . -B `"$CMakeBuildDir`" -G `"NMake Makefiles`" -DCMAKE_BUILD_TYPE=$Configuration -DBUILD_TESTING=OFF"
Invoke-VsDevCommand -WorkingDirectory $repoRoot -Command "cmake --build `"$CMakeBuildDir`" --target hostd"

$hostdPath = Join-Path (Join-Path $repoRoot $CMakeBuildDir) "hostd.exe"
if (-not (Test-Path $hostdPath)) {
    throw "hostd.exe not found at $hostdPath"
}

& $dotnet publish $launcherProject `
    -c $Configuration `
    -r $RuntimeIdentifier `
    --self-contained true `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:Version=$numericVersion `
    -p:InformationalVersion=$Version `
    -o $publishDir
if ($LASTEXITCODE -ne 0) {
    throw "dotnet publish failed."
}

Copy-Item -LiteralPath $hostdPath -Destination (Join-Path $publishDir "hostd.exe") -Force

& $dotnet build $installerProject `
    -c $Configuration `
    -p:PublishDir=$publishDir `
    -p:ProductVersion=$numericVersion `
    -p:DisplayVersion=$Version `
    -p:PackageBaseName=$PackageBaseName `
    -p:InstallerOutputDir=$releaseDir
if ($LASTEXITCODE -ne 0) {
    throw "dotnet build for the WiX installer failed."
}

$msiPath = Join-Path $releaseDir "$PackageBaseName.msi"
if (-not (Test-Path $msiPath)) {
    throw "MSI was not generated at $msiPath"
}

$checksumPath = Join-Path $releaseDir "$PackageBaseName.sha256.txt"
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $msiPath).Hash.ToLowerInvariant()
"$hash *$(Split-Path $msiPath -Leaf)" | Set-Content -LiteralPath $checksumPath -Encoding ascii

Write-Host "Using dotnet: $dotnet"
Write-Host "MSI: $msiPath"
Write-Host "SHA256: $checksumPath"
