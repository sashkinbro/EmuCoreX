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
    if ((Read-GSCache $directoryGS 'EMUCOREX_ENABLE_EXPERIMENTAL_IR') -ne 'OFF') {
        throw "Experimental IR must stay disabled in app builds: $directoryGS"
    }
    $libraryGS = Join-Path (Read-GSCache $directoryGS 'CMAKE_LIBRARY_OUTPUT_DIRECTORY') ('lib' + (Read-GSCache $directoryGS 'EMUCOREX_NATIVE_LIBRARY_NAME') + '.so')
    $symbolsGS = & $nmGS -D --defined-only $libraryGS
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $libraryGS" }
    $hasGS = [bool]($symbolsGS -match 'EmuCoreXRunGSOracle$')
    if ($hasGS -ne ($directoryGS -eq $DebugBuildDirectory)) { throw "Unexpected GS export state: $libraryGS" }
    $hasGameplayGS = [bool]($symbolsGS -match 'Java_com_sbro_emucorex_core_GameplayProfilingDeviceTest_readMetricsSample$')
    if ($hasGameplayGS -ne ($directoryGS -eq $DebugBuildDirectory)) { throw "Unexpected gameplay profiling export state: $libraryGS" }
}
if (Select-String -LiteralPath (Join-Path $ReleaseBuildDirectory 'build.ninja') -Pattern 'src/debug/gs_oracle|emucorex_gs_oracle') {
    throw 'GS source or executable leaked into the Release build graph.'
}
Write-Host 'PASS: Debug GS/gameplay profiling exports exist; Release excludes GS oracle sources, executable, and profiling exports. Experimental IR is OFF in both builds.'
