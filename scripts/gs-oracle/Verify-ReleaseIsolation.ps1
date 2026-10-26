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
if ((Read-GSCache $ReleaseBuildDirectory 'EMUCOREX_ENABLE_NATIVE_SELF_TESTS') -ne 'OFF') {
    throw 'Native self-tests must be compiled out of Release.'
}
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
    if ($directoryGS -eq $ReleaseBuildDirectory) {
        $forbiddenGS = 'EmuCoreXRun.*(?:Oracle|Tests)|GameplayProfilingDeviceTest|' +
            'NativeApp_(?:queueGsDumpNative|runBootSmokeProbeNative|runJitExecutableMemorySmokeTest|' +
            'runEeFpuDivRoundingSelfTest|startJitProfiler|stopJitProfiler|isJitProfilerActive|' +
            'startHangTrace|stopHangTrace|isHangTraceActive)$'
        if ($symbolsGS -match $forbiddenGS) { throw 'Debug/test JNI or oracle export leaked into Release.' }
        $fullSymbolsGS = & $nmGS --defined-only --demangle $libraryGS
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect internal symbols: $libraryGS" }
        if ($fullSymbolsGS -match 'JitProfiler::|HangTrace::|GSDumpBase::|GSDumpReplayer::') {
            throw 'Profiler or GS dumper implementation leaked into Release.'
        }
        if (!($symbolsGS -match 'EmuCoreXDumpNativeCrashDiagnostics$') -or
            !($symbolsGS -match 'NativeApp_setNativeCrashLogFilePath$')) {
            throw 'Required production crash diagnostics are missing.'
        }
    }
}
if (Select-String -LiteralPath (Join-Path $ReleaseBuildDirectory 'build.ninja') -Pattern 'ARM_ANDROID[/\\]src[/\\]debug[/\\]|emucorex_(gs|jit)_oracle') {
    throw 'Oracle source or executable leaked into the Release build graph.'
}
Write-Host 'PASS: Release excludes GS/JIT oracles, test JNI, GS dumpers and JIT/hang profilers; production crash diagnostics remain. Experimental IR is OFF in both builds.'
