# SPDX-License-Identifier: GPL-3.0+
param(
    [Parameter(Mandatory)][string]$DebugBuildDirectory,
    [Parameter(Mandatory)][string]$ReleaseBuildDirectory
)
$ErrorActionPreference = 'Stop'
function Read-GSCache([string]$Directory, [string]$Name) {
    $entry = Get-Content -LiteralPath (Join-Path $Directory 'CMakeCache.txt') | Where-Object { $_ -match "^${Name}:[^=]+=" } | Select-Object -First 1
    if (!$entry) { throw "Missing $Name in $Directory" }
    $entry.Substring($entry.IndexOf('=') + 1)
}
if ((Read-GSCache $DebugBuildDirectory 'CMAKE_BUILD_TYPE') -ne 'Debug' -or
    (Read-GSCache $DebugBuildDirectory 'EMUCOREX_ENABLE_GS_ORACLE') -ne 'ON') { throw 'Expected GS Debug build.' }
if ((Read-GSCache $ReleaseBuildDirectory 'CMAKE_BUILD_TYPE') -ne 'Release' -or
    (Read-GSCache $ReleaseBuildDirectory 'EMUCOREX_ENABLE_GS_ORACLE') -ne 'OFF') { throw 'Expected GS-free Release build.' }
$ndkGS = Read-GSCache $DebugBuildDirectory 'CMAKE_ANDROID_NDK'
$nmGS = Join-Path $ndkGS 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-nm.exe'
foreach ($directoryGS in @($DebugBuildDirectory, $ReleaseBuildDirectory)) {
    $libraryGS = Join-Path (Read-GSCache $directoryGS 'CMAKE_LIBRARY_OUTPUT_DIRECTORY') ('lib' + (Read-GSCache $directoryGS 'EMUCOREX_NATIVE_LIBRARY_NAME') + '.so')
    $symbolsGS = & $nmGS -D --defined-only $libraryGS
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $libraryGS" }
    $hasGS = [bool]($symbolsGS -match 'EmuCoreXRunGSOracle$')
    if ($hasGS -ne ($directoryGS -eq $DebugBuildDirectory)) { throw "Unexpected GS export state: $libraryGS" }
}
if (Select-String -LiteralPath (Join-Path $ReleaseBuildDirectory 'build.ninja') -Pattern 'src/debug/gs_oracle|emucorex_gs_oracle') {
    throw 'GS source or executable leaked into the Release build graph.'
}
Write-Host 'PASS: Debug GS entry point exists; Release excludes GS oracle sources, executable, and entry point.'
