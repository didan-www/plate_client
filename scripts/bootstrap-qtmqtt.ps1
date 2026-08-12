[CmdletBinding()]
param(
    [string]$QtPrefix = 'D:\Qt\6.11.1\mingw_64',
    [string]$MingwRoot = 'D:\Qt\Tools\mingw1310_64',
    [string]$CMakePath = 'D:\Qt\Tools\CMake_64\bin\cmake.exe',
    [string]$NinjaPath = 'D:\Qt\Tools\Ninja\ninja.exe',
    [string]$GitPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ExpectedQtVersion = '6.11.1'
$ExpectedMingwVersion = '13.1.0'
$ExpectedCMakeVersion = '3.30.5'
$ExpectedNinjaVersion = '1.12.1'
$ExpectedTarget = 'x86_64-w64-mingw32'
$QtmqttTag = 'v6.11.1'
$QtmqttTagObject = '8d3b643deab4b849724221e711030afdc7afd676'
$QtmqttCommit = '53d13cb6c11c0d8e4451284c391e48f5efcec9b5'
$QtmqttRepository = 'https://code.qt.io/qt/qtmqtt.git'

$ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$DependencyRoot = Join-Path $ProjectRoot '.deps'
$SourceDirectory = Join-Path $DependencyRoot 'src\qtmqtt'
$BuildDirectory = Join-Path $DependencyRoot 'build\qtmqtt'
$InstallDirectory = Join-Path $DependencyRoot 'install\qtmqtt'
$ProbeSourceDirectory = Join-Path $DependencyRoot 'build\qtmqtt-probe-src'
$ProbeBuildDirectory = Join-Path $DependencyRoot 'build\qtmqtt-probe'

function Stop-Bootstrap {
    param([Parameter(Mandatory = $true)][string]$Message)

    throw "[bootstrap-qtmqtt] $Message"
}

function Resolve-RequiredFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Stop-Bootstrap "$Description was not found at '$Path'."
    }

    return (Resolve-Path -LiteralPath $Path).Path
}

function Get-ExternalOutput {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @()
    )

    $output = & $FilePath @ArgumentList 2>&1
    if ($LASTEXITCODE -ne 0) {
        $details = (($output | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine).Trim()
        Stop-Bootstrap "Command failed with exit code ${LASTEXITCODE}: '$FilePath $($ArgumentList -join ' ')'. $details"
    }

    return (($output | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine).Trim()
}

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @()
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        Stop-Bootstrap "Command failed with exit code ${LASTEXITCODE}: '$FilePath $($ArgumentList -join ' ')'."
    }
}

function Assert-Version {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Actual,
        [Parameter(Mandatory = $true)][string]$Expected
    )

    if ($Actual -cne $Expected) {
        Stop-Bootstrap "$Name version mismatch. Expected '$Expected', found '$Actual'."
    }
}

function Get-NormalizedPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return [System.IO.Path]::GetFullPath($Path.Replace('/', '\')).TrimEnd('\')
}

function Assert-QtEnvironment {
    if (-not (Test-Path -LiteralPath $QtPrefix -PathType Container)) {
        Stop-Bootstrap "Qt prefix was not found at '$QtPrefix'. Install Qt $ExpectedQtVersion Desktop MinGW 64-bit first or pass -QtPrefix."
    }

    $script:QtPrefix = (Resolve-Path -LiteralPath $QtPrefix).Path
    $qmake = Resolve-RequiredFile (Join-Path $script:QtPrefix 'bin\qmake.exe') 'qmake'
    $script:QtCMake = Resolve-RequiredFile (Join-Path $script:QtPrefix 'bin\qt-cmake.bat') 'qt-cmake'

    $qtVersion = Get-ExternalOutput $qmake @('-query', 'QT_VERSION')
    Assert-Version 'Qt' $qtVersion $ExpectedQtVersion

    $reportedPrefix = Get-ExternalOutput $qmake @('-query', 'QT_INSTALL_PREFIX')
    if (-not [string]::Equals(
            (Get-NormalizedPath $reportedPrefix),
            (Get-NormalizedPath $script:QtPrefix),
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Stop-Bootstrap "qmake reports Qt prefix '$reportedPrefix', but -QtPrefix is '$($script:QtPrefix)'."
    }

    $qmakeSpec = Get-ExternalOutput $qmake @('-query', 'QMAKE_XSPEC')
    if ($qmakeSpec -cne 'win32-g++') {
        Stop-Bootstrap "Qt ABI mismatch. Expected QMAKE_XSPEC 'win32-g++', found '$qmakeSpec'."
    }
}

function Assert-Toolchain {
    if (-not (Test-Path -LiteralPath $MingwRoot -PathType Container)) {
        Stop-Bootstrap "MinGW root was not found at '$MingwRoot'."
    }

    $script:MingwRoot = (Resolve-Path -LiteralPath $MingwRoot).Path
    $script:Gcc = Resolve-RequiredFile (Join-Path $script:MingwRoot 'bin\gcc.exe') 'gcc'
    $script:Gxx = Resolve-RequiredFile (Join-Path $script:MingwRoot 'bin\g++.exe') 'g++'
    $script:CMake = Resolve-RequiredFile $CMakePath 'CMake'
    $script:Ninja = Resolve-RequiredFile $NinjaPath 'Ninja'

    $compilerVersion = Get-ExternalOutput $script:Gxx @('-dumpfullversion', '-dumpversion')
    Assert-Version 'MinGW-w64 GCC' $compilerVersion $ExpectedMingwVersion

    $compilerTarget = Get-ExternalOutput $script:Gxx @('-dumpmachine')
    if ($compilerTarget -cne $ExpectedTarget) {
        Stop-Bootstrap "MinGW ABI mismatch. Expected target '$ExpectedTarget', found '$compilerTarget'."
    }

    $cmakeOutput = Get-ExternalOutput $script:CMake @('--version')
    if ($cmakeOutput -notmatch '(?m)^cmake version ([0-9]+\.[0-9]+\.[0-9]+)') {
        Stop-Bootstrap "Unable to parse CMake version from '$cmakeOutput'."
    }
    Assert-Version 'CMake' $Matches[1] $ExpectedCMakeVersion

    $ninjaVersion = Get-ExternalOutput $script:Ninja @('--version')
    Assert-Version 'Ninja' $ninjaVersion $ExpectedNinjaVersion
}

function Resolve-Git {
    if ($GitPath) {
        $script:Git = Resolve-RequiredFile $GitPath 'Git'
        return
    }

    $gitCommand = Get-Command git -CommandType Application -ErrorAction SilentlyContinue
    if (-not $gitCommand) {
        Stop-Bootstrap 'Git was not found on PATH. Install Git or pass -GitPath.'
    }
    $script:Git = $gitCommand.Source
}

function Assert-QtmqttSource {
    if (-not (Test-Path -LiteralPath (Join-Path $SourceDirectory '.git') -PathType Container)) {
        Stop-Bootstrap "Qt MQTT source at '$SourceDirectory' is not a Git checkout."
    }

    $origin = Get-ExternalOutput $script:Git @('-C', $SourceDirectory, 'remote', 'get-url', 'origin')
    if ($origin.TrimEnd('/') -cne $QtmqttRepository.TrimEnd('/')) {
        Stop-Bootstrap "Qt MQTT source origin mismatch. Expected '$QtmqttRepository', found '$origin'."
    }

    $head = Get-ExternalOutput $script:Git @('-C', $SourceDirectory, 'rev-parse', 'HEAD')
    if ($head -cne $QtmqttCommit) {
        Stop-Bootstrap "Qt MQTT source revision mismatch. Expected '$QtmqttCommit' ($QtmqttTag), found '$head'."
    }

    $tagObject = Get-ExternalOutput $script:Git @('-C', $SourceDirectory, 'rev-parse', "refs/tags/$QtmqttTag")
    if ($tagObject -cne $QtmqttTagObject) {
        Stop-Bootstrap "Qt MQTT tag object mismatch. Expected '$QtmqttTagObject', found '$tagObject'."
    }

    $exactTag = Get-ExternalOutput $script:Git @('-C', $SourceDirectory, 'describe', '--tags', '--exact-match', 'HEAD')
    if ($exactTag -cne $QtmqttTag) {
        Stop-Bootstrap "Qt MQTT source tag mismatch. Expected '$QtmqttTag', found '$exactTag'."
    }

    $sourceChanges = Get-ExternalOutput $script:Git @('-C', $SourceDirectory, 'status', '--porcelain')
    if ($sourceChanges) {
        Stop-Bootstrap "Qt MQTT source at '$SourceDirectory' contains local changes. Refusing to build an unverified source tree."
    }
}

function Initialize-QtmqttSource {
    if (Test-Path -LiteralPath $SourceDirectory) {
        Assert-QtmqttSource
        return
    }

    $sourceParent = Split-Path -Parent $SourceDirectory
    New-Item -ItemType Directory -Force -Path $sourceParent | Out-Null
    $temporarySource = "$SourceDirectory.clone-$PID"
    if (Test-Path -LiteralPath $temporarySource) {
        Stop-Bootstrap "Temporary source directory already exists at '$temporarySource'."
    }

    try {
        Invoke-External $script:Git @(
            'clone',
            '--branch', $QtmqttTag,
            '--depth', '1',
            '--single-branch',
            $QtmqttRepository,
            $temporarySource
        )
        Move-Item -LiteralPath $temporarySource -Destination $SourceDirectory
    }
    finally {
        if (Test-Path -LiteralPath $temporarySource) {
            Remove-Item -LiteralPath $temporarySource -Recurse -Force
        }
    }

    Assert-QtmqttSource
}

function Write-PackageProbe {
    New-Item -ItemType Directory -Force -Path $ProbeSourceDirectory | Out-Null
    $probe = @'
cmake_minimum_required(VERSION 3.21)
project(qtmqtt_package_probe LANGUAGES CXX)

find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core)
find_package(Qt6Mqtt 6.11.1 EXACT CONFIG REQUIRED)

if(NOT TARGET Qt6::Mqtt)
    message(FATAL_ERROR "Qt6::Mqtt target is missing")
endif()

add_executable(qtmqtt_package_probe main.cpp)
target_link_libraries(qtmqtt_package_probe PRIVATE Qt6::Mqtt)
'@
    Set-Content -LiteralPath (Join-Path $ProbeSourceDirectory 'CMakeLists.txt') -Value $probe -Encoding Ascii

    $probeSource = @'
#include <QMqttClient>

int main()
{
    QMqttClient client;
    client.setHostname(QStringLiteral("localhost"));
    return 0;
}
'@
    Set-Content -LiteralPath (Join-Path $ProbeSourceDirectory 'main.cpp') -Value $probeSource -Encoding Ascii
}

function Test-InstalledPackage {
    $packageConfig = Join-Path $InstallDirectory 'lib\cmake\Qt6Mqtt\Qt6MqttConfig.cmake'
    if (-not (Test-Path -LiteralPath $packageConfig -PathType Leaf)) {
        return $false
    }

    Write-PackageProbe
    New-Item -ItemType Directory -Force -Path $ProbeBuildDirectory | Out-Null
    $prefixPath = "$InstallDirectory;$($script:QtPrefix)"
    & $script:CMake @(
        '-S', $ProbeSourceDirectory,
        '-B', $ProbeBuildDirectory,
        '-G', 'Ninja',
        "-DCMAKE_PREFIX_PATH=$prefixPath",
        "-DCMAKE_MAKE_PROGRAM=$($script:Ninja)",
        "-DCMAKE_CXX_COMPILER=$($script:Gxx)"
    ) 2>&1 | Out-Host

    if ($LASTEXITCODE -ne 0) {
        return $false
    }

    & $script:CMake @('--build', $ProbeBuildDirectory) 2>&1 | Out-Host
    return ($LASTEXITCODE -eq 0)
}

function Build-And-InstallQtmqtt {
    New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $InstallDirectory) | Out-Null

    $savedPath = $env:PATH
    try {
        $env:PATH = @(
            (Join-Path $script:MingwRoot 'bin')
            (Join-Path $script:QtPrefix 'bin')
            (Split-Path -Parent $script:CMake)
            (Split-Path -Parent $script:Ninja)
            $savedPath
        ) -join ';'
        Invoke-External $script:QtCMake @(
            '-S', $SourceDirectory,
            '-B', $BuildDirectory,
            '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release',
            "-DCMAKE_INSTALL_PREFIX=$InstallDirectory",
            "-DCMAKE_MAKE_PROGRAM=$($script:Ninja)",
            "-DCMAKE_C_COMPILER=$($script:Gcc)",
            "-DCMAKE_CXX_COMPILER=$($script:Gxx)",
            '-DQT_BUILD_EXAMPLES=OFF',
            '-DQT_BUILD_TESTS=OFF'
        )
        Invoke-External $script:CMake @('--build', $BuildDirectory, '--parallel')
        Invoke-External $script:CMake @('--install', $BuildDirectory)
    }
    finally {
        $env:PATH = $savedPath
    }
}

Write-Host "[bootstrap-qtmqtt] Project root: $ProjectRoot"
Assert-QtEnvironment
Assert-Toolchain
Resolve-Git

Write-Host "[bootstrap-qtmqtt] Validated Qt $ExpectedQtVersion, GCC $ExpectedMingwVersion, CMake $ExpectedCMakeVersion, and Ninja $ExpectedNinjaVersion."

if (Test-Path -LiteralPath $InstallDirectory) {
    if (Test-InstalledPackage) {
        Write-Host "[bootstrap-qtmqtt] Qt MQTT $ExpectedQtVersion is already installed and valid at '$InstallDirectory'."
        exit 0
    }
    Stop-Bootstrap "An incomplete or incompatible Qt MQTT installation exists at '$InstallDirectory'. Remove that project-local directory and rerun the script."
}

Initialize-QtmqttSource
Build-And-InstallQtmqtt

if (-not (Test-InstalledPackage)) {
    Stop-Bootstrap "Qt MQTT was installed, but the CMake package probe could not load Qt6::Mqtt $ExpectedQtVersion."
}

Write-Host "[bootstrap-qtmqtt] Qt MQTT $ExpectedQtVersion was installed successfully at '$InstallDirectory'."
