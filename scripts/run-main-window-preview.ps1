[CmdletBinding()]
param(
    [string]$QtPrefix = 'D:\Qt\6.11.1\mingw_64',
    [string]$MingwRoot = 'D:\Qt\Tools\mingw1310_64',
    [string]$CMakePath = 'D:\Qt\Tools\CMake_64\bin\cmake.exe',
    [string]$NinjaPath = 'D:\Qt\Tools\Ninja\ninja.exe',
    [switch]$SmokeTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$BuildDirectory = Join-Path $ProjectRoot 'build\mingw-debug'
$PreviewExecutable = Join-Path $BuildDirectory 'tests\plate_client_main_window_preview.exe'

function Stop-Preview {
    param([Parameter(Mandatory = $true)][string]$Message)

    throw "[main-window-preview] $Message"
}

function Resolve-RequiredFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Stop-Preview "$Description was not found at '$Path'."
    }

    return (Resolve-Path -LiteralPath $Path).Path
}

function Resolve-RequiredDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        Stop-Preview "$Description was not found at '$Path'."
    }

    return (Resolve-Path -LiteralPath $Path).Path
}

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @()
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        Stop-Preview "Command failed with exit code ${LASTEXITCODE}: '$FilePath $($ArgumentList -join ' ')'."
    }
}

$CMakePath = Resolve-RequiredFile $CMakePath 'CMake executable'
$NinjaPath = Resolve-RequiredFile $NinjaPath 'Ninja executable'
$QtPrefix = Resolve-RequiredDirectory $QtPrefix 'Qt prefix'
$MingwRoot = Resolve-RequiredDirectory $MingwRoot 'MinGW root'
$CompilerPath = Resolve-RequiredFile (Join-Path $MingwRoot 'bin\g++.exe') 'MinGW compiler'
$QtBin = Resolve-RequiredDirectory (Join-Path $QtPrefix 'bin') 'Qt runtime directory'
$MingwBin = Resolve-RequiredDirectory (Join-Path $MingwRoot 'bin') 'MinGW runtime directory'

$ConfigureArguments = @(
    '-S', $ProjectRoot,
    '-B', $BuildDirectory,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Debug',
    "-DCMAKE_CXX_COMPILER=$CompilerPath",
    "-DCMAKE_MAKE_PROGRAM=$NinjaPath",
    "-DCMAKE_PREFIX_PATH=$QtPrefix"
)

Write-Host '[main-window-preview] Configuring test build.'
Invoke-External $CMakePath $ConfigureArguments
Write-Host '[main-window-preview] Building isolated preview target.'
Invoke-External $CMakePath @('--build', $BuildDirectory, '--target', 'plate_client_main_window_preview')

if (-not (Test-Path -LiteralPath $PreviewExecutable -PathType Leaf)) {
    Stop-Preview "Preview executable was not generated at '$PreviewExecutable'."
}

$OriginalPath = $env:PATH
try {
    $env:PATH = "$QtBin;$MingwBin;$OriginalPath"
    $PreviewArguments = @()
    if ($SmokeTest) {
        $PreviewArguments += '--smoke-test'
    }

    Write-Host "[main-window-preview] Starting '$PreviewExecutable'."
    Invoke-External $PreviewExecutable $PreviewArguments
}
finally {
    $env:PATH = $OriginalPath
}
