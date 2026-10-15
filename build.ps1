<#
.SYNOPSIS
    Builds Pawn.Json for Windows and Linux and puts the release files in dist/.

.DESCRIPTION
    Windows: compiled with Visual Studio (C++ workload) and CMake, then the unit
             tests run.
    Linux:   cross-compiled from Windows with Zig, which this script downloads
             into .tools/ on first use. No Linux machine, WSL or Docker needed.

    The open.mp SDK, the SA-MP plugin SDK and nlohmann/json are downloaded by
    CMake on the first build.

    Result (one folder and one zip per platform):
      dist/
        PawnJson-<version>-windows-x86/
          open.mp/components/PawnJson.dll
          open.mp/qawno/include/PawnJson.inc
          samp/plugins/PawnJson.dll
          samp/pawno/include/PawnJson.inc
          README.md
        PawnJson-<version>-windows-x86.zip
        PawnJson-<version>-linux-x86/        (same layout, PawnJson.so)
        PawnJson-<version>-linux-x86.zip

.PARAMETER Target
    all (default), windows or linux.

.PARAMETER Version
    Version embedded in the binaries, the include and the package names
    (MAJOR.MINOR.PATCH). Default: the one in CMakeLists.txt.

.PARAMETER Clean
    Deletes the build folders first.

.PARAMETER SkipTests
    Does not build or run the unit tests (Windows only; Linux binaries cannot
    run here).

.PARAMETER NoZip
    Only creates the dist folders, without zips.

.EXAMPLE
    .\build.ps1

.EXAMPLE
    .\build.ps1 -Target linux -Version 1.1.0 -Clean

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\build.ps1
#>
[CmdletBinding()]
param(
    [ValidateSet('all', 'windows', 'linux')]
    [string]$Target = 'all',

    [string]$Version,

    [switch]$Clean,

    [switch]$SkipTests,

    [switch]$NoZip
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Invoke-WebRequest is many times slower with its progress bar.
$ProgressPreference = 'SilentlyContinue'

$Root = $PSScriptRoot
$BuildRoot = Join-Path $Root 'build'
$ToolsRoot = Join-Path $Root '.tools'
$DistRoot = Join-Path $Root 'dist'

$ZigRelease = @{
    Name   = 'zig-0.15.2'
    Url    = 'https://ziglang.org/download/0.15.2/zig-x86_64-windows-0.15.2.zip'
    Sha256 = '3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c'
}
$NinjaRelease = @{
    Name   = 'ninja-1.13.2'
    Url    = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip'
    Sha256 = '07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65'
}
# Oldest glibc the Linux build may run on; 2.17 covers practically every
# Linux host still in use.
$LinuxTarget = 'x86-linux-gnu.2.17'

function Write-Step([string]$Message) {
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Invoke-Native([string]$FilePath, [string[]]$Arguments) {
    # Out-Host: show the output instead of returning it from the caller.
    & $FilePath @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$([IO.Path]::GetFileName($FilePath)) failed (exit code $LASTEXITCODE)"
    }
}

function Reset-Directory([string]$Path) {
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Recurse -Force }
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

# Downloads a zip, checks its SHA-256 and extracts it into .tools/<name>.
function Get-Tool([hashtable]$Release) {
    $destination = Join-Path $ToolsRoot $Release.Name
    if (Test-Path -LiteralPath $destination) { return $destination }

    New-Item -ItemType Directory -Path $ToolsRoot -Force | Out-Null
    $archive = Join-Path $ToolsRoot "$($Release.Name).zip"
    Write-Step "Downloading $($Release.Name)"
    Invoke-WebRequest -Uri $Release.Url -OutFile $archive -UseBasicParsing

    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($hash -ne $Release.Sha256.ToUpperInvariant()) {
        Remove-Item -LiteralPath $archive -Force
        throw "$($Release.Name): download is corrupt (SHA-256 mismatch)"
    }

    $staging = "$destination.partial"
    if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
    Expand-Archive -LiteralPath $archive -DestinationPath $staging
    Move-Item -LiteralPath $staging -Destination $destination
    Remove-Item -LiteralPath $archive -Force
    return $destination
}

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { return $null }
    $found = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
    $instance = @($found) | Select-Object -First 1
    if (-not $instance) { return $null }

    $generators = @{ 16 = 'Visual Studio 16 2019'; 17 = 'Visual Studio 17 2022'; 18 = 'Visual Studio 18 2026' }
    $major = [int]($instance.installationVersion.Split('.')[0])
    if (-not $generators.ContainsKey($major)) {
        throw "Unsupported Visual Studio version $($instance.installationVersion)"
    }
    return [pscustomobject]@{
        Path      = $instance.installationPath
        Name      = $instance.displayName
        Generator = $generators[$major]
    }
}

function Find-CMake {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $vs = Find-VisualStudio
    if ($vs) {
        $bundled = Join-Path $vs.Path 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path -LiteralPath $bundled) { return $bundled }
    }
    throw 'CMake was not found. Install it from https://cmake.org/download/ or add the "C++ CMake tools" component to Visual Studio.'
}

function Find-Ninja {
    $command = Get-Command ninja -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    return Join-Path (Get-Tool $NinjaRelease) 'ninja.exe'
}

function Get-ProjectVersion {
    $match = Select-String -LiteralPath (Join-Path $Root 'CMakeLists.txt') -Pattern 'set\(PAWN_JSON_VERSION "(\d+\.\d+\.\d+)"' | Select-Object -First 1
    if (-not $match) { throw 'Could not read PAWN_JSON_VERSION from CMakeLists.txt' }
    return $match.Matches[0].Groups[1].Value
}

function Test-ElfLibrary([string]$Path) {
    # A 32-bit x86 ELF file: magic, ELFCLASS32, machine EM_386.
    $bytes = [IO.File]::ReadAllBytes($Path)
    $isElf = $bytes.Length -gt 20 -and $bytes[0] -eq 0x7F -and $bytes[1] -eq 0x45 -and $bytes[2] -eq 0x4C -and $bytes[3] -eq 0x46
    if (-not $isElf -or $bytes[4] -ne 1 -or $bytes[18] -ne 3) {
        throw "$Path is not a 32-bit x86 Linux binary"
    }
}

function Build-Windows([string]$CMake) {
    $vs = Find-VisualStudio
    if (-not $vs) {
        throw 'Visual Studio with the "Desktop development with C++" workload was not found.'
    }
    Write-Step "Windows x86 ($($vs.Name))"

    $buildDir = Join-Path $BuildRoot 'windows-x86'
    if ($Clean -and (Test-Path -LiteralPath $buildDir)) { Remove-Item -LiteralPath $buildDir -Recurse -Force }
    $testing = if ($SkipTests) { 'OFF' } else { 'ON' }

    Invoke-Native $CMake @('-S', $Root, '-B', $buildDir, '-G', $vs.Generator, '-A', 'Win32',
        '--log-level=WARNING', '-Wno-dev', '-Wno-deprecated', '--no-warn-unused-cli',
        "-DPAWN_JSON_VERSION=$Version", "-DBUILD_TESTING=$testing")
    Invoke-Native $CMake @('--build', $buildDir, '--config', 'Release', '--parallel', '--', '/nologo', '/verbosity:minimal')

    if (-not $SkipTests) {
        Write-Step 'Windows unit tests'
        $ctest = Join-Path (Split-Path $CMake) 'ctest.exe'
        Invoke-Native $ctest @('--test-dir', $buildDir, '-C', 'Release', '--output-on-failure')
    }

    return [pscustomobject]@{
        Plugin  = Join-Path $buildDir 'plugins\PawnJson.dll'
        Include = Join-Path $buildDir 'pawno\include\PawnJson.inc'
    }
}

function Build-Linux([string]$CMake) {
    Write-Step "Linux x86 (cross-compiled with Zig for glibc $($LinuxTarget.Split('.', 2)[1])+)"

    $zigRoot = Get-Tool $ZigRelease
    $zigExe = Get-ChildItem -LiteralPath $zigRoot -Recurse -Filter 'zig.exe' | Select-Object -First 1
    if (-not $zigExe) { throw "zig.exe not found in $zigRoot" }
    $zig = $zigExe.FullName
    $ninja = Find-Ninja

    # CMake needs one executable per tool; these forward to zig.exe.
    $wrappers = Join-Path $ToolsRoot 'zig-wrappers'
    New-Item -ItemType Directory -Path $wrappers -Force | Out-Null
    $commands = @{
        'zig-cc.cmd'     = "cc -target $LinuxTarget"
        'zig-c++.cmd'    = "c++ -target $LinuxTarget"
        'zig-ar.cmd'     = 'ar'
        'zig-ranlib.cmd' = 'ranlib'
    }
    foreach ($name in $commands.Keys) {
        Set-Content -LiteralPath (Join-Path $wrappers $name) -Value "@`"$zig`" $($commands[$name]) %*" -Encoding Ascii
    }

    $env:PAWN_JSON_ZIG_WRAPPERS = $wrappers
    # Keep Zig's caches inside the project instead of %LOCALAPPDATA%.
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $ToolsRoot 'zig-cache'
    $env:ZIG_LOCAL_CACHE_DIR = Join-Path $ToolsRoot 'zig-cache'

    $buildDir = Join-Path $BuildRoot 'linux-x86'
    if ($Clean -and (Test-Path -LiteralPath $buildDir)) { Remove-Item -LiteralPath $buildDir -Recurse -Force }

    Invoke-Native $CMake @('-S', $Root, '-B', $buildDir, '-G', 'Ninja',
        '--log-level=WARNING', '-Wno-dev', '-Wno-deprecated', '--no-warn-unused-cli',
        "-DCMAKE_MAKE_PROGRAM=$ninja",
        "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $Root 'cmake\zig-linux-x86.cmake')",
        '-DCMAKE_BUILD_TYPE=Release',
        '-DBUILD_TESTING=OFF',
        "-DPAWN_JSON_VERSION=$Version")
    Invoke-Native $CMake @('--build', $buildDir, '--parallel')

    $result = [pscustomobject]@{
        Plugin  = Join-Path $buildDir 'plugins\PawnJson.so'
        Include = Join-Path $buildDir 'pawno\include\PawnJson.inc'
    }
    Test-ElfLibrary $result.Plugin
    return $result
}

# Lays out one platform package: each server's folder structure plus the docs.
function Publish-Platform([string]$Platform, $Output) {
    foreach ($file in $Output.Plugin, $Output.Include) {
        if (-not (Test-Path -LiteralPath $file)) { throw "Expected build output is missing: $file" }
    }

    $packageName = "PawnJson-$Version-$Platform-x86"
    $packageDir = Join-Path $DistRoot $packageName
    Reset-Directory $packageDir

    $layout = @(
        @{ Source = $Output.Plugin;  Target = 'open.mp\components' },
        @{ Source = $Output.Include; Target = 'open.mp\qawno\include' },
        @{ Source = $Output.Plugin;  Target = 'samp\plugins' },
        @{ Source = $Output.Include; Target = 'samp\pawno\include' },
        @{ Source = (Join-Path $Root 'README.md'); Target = '.' }
    )
    foreach ($entry in $layout) {
        $targetDir = Join-Path $packageDir $entry.Target
        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
        Copy-Item -LiteralPath $entry.Source -Destination $targetDir -Force
    }

    $zip = Join-Path $DistRoot "$packageName.zip"
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    if (-not $NoZip) {
        Compress-Archive -Path (Join-Path $packageDir '*') -DestinationPath $zip
    }
}

# ---------------------------------------------------------------------------

if (-not $Version) { $Version = Get-ProjectVersion }
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Invalid version '$Version' (expected MAJOR.MINOR.PATCH)"
}

$started = Get-Date
$cmake = Find-CMake
Write-Host "Pawn.Json $Version - target: $Target"
Write-Host "CMake: $cmake"

$windows = $null
$linux = $null
if ($Target -in 'all', 'windows') { $windows = Build-Windows $cmake }
if ($Target -in 'all', 'linux') { $linux = Build-Linux $cmake }

Write-Step 'Collecting files in dist/'
New-Item -ItemType Directory -Path $DistRoot -Force | Out-Null
if ($windows) { Publish-Platform 'windows' $windows }
if ($linux) { Publish-Platform 'linux' $linux }

$elapsed = (Get-Date) - $started
Write-Host ''
Write-Host "Done in $([int][math]::Floor($elapsed.TotalMinutes))m $($elapsed.Seconds)s:" -ForegroundColor Green
Get-ChildItem -LiteralPath $DistRoot -Recurse -File |
    Where-Object { $_.Name -like "PawnJson-$Version-*" -or $_.FullName -like "*\PawnJson-$Version-*\*" } |
    Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($DistRoot.Length + 1)
        '  dist\{0,-64} {1,8:N0} KB' -f $relative, ($_.Length / 1KB)
    }
