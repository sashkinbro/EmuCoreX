[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$NativeBuildDirectory,
    [string]$Serial,
    [ValidateSet('signal-chain', 'breakpoint-probes', 'ee-irq-events', 'ee-vu-sync', 'arm64-branches', 'classifier', 'jit-cache', 'vu-cache', 'spu2-volume', 'vif-mpg', 'vu-div-zero', 'vu-scalar-clamp', 'semaphore', 'vu', 'vu-direct', 'vu-pq', 'vu-itof-codegen', 'vu-ftoi', 'vectors', 'ee', 'ee-direct', 'ee-direct-stores', 'ee-branch-immediates', 'ee-cop1-mov', 'ee-cop1-memory', 'ee-cop1-compare', 'ee-vu-clip', 'ee-vu-abs', 'ee-stores', 'ee-bc1', 'ee-hot-loads', 'ee-fastmem', 'ee-cop1-unary', 'ee-cop1-maxmin', 'ee-fastmem-exit', 'ee-fastmem-cold', 'vu-maxmin-i', 'vu-clip', 'vu-clip-route', 'ee-vu-maxmin-i', 'ee-vu-maxmin-i-flags', 'vu-maxmin', 'vu-maxmin-route', 'vu-maxmin-broadcast', 'vu-maxmin-broadcast-route', 'ee-vu-maxmin-broadcast', 'ee-vu-maxmin', 'ir-immediates', 'iop-nclip', 'iop-avsz', 'ee-vu-itof', 'ee-vu-ftoi', 'iop-cop2', 'iop-cop2-memory', 'iop', 'iop-direct', 'gs-vertices', 'gs-alpha-selectors', 'gs-stq-pack', 'gs-masked-fill', 'gs-prepare', 'gs-job', 'gs-job-bridge', 'gs-job-runtime', 'gs-job-snapshot')][string[]]$Suite = @('classifier', 'jit-cache', 'vu-cache', 'spu2-volume', 'vif-mpg', 'vu-div-zero', 'vu-scalar-clamp', 'semaphore', 'vu-direct', 'ee-direct', 'iop-direct'),
    [string]$Adb = "$env:LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe",
    [string]$OutputDirectory = "$PSScriptRoot/results/jit-oracle",
    [string[]]$CaptureFiles = @(),
    [switch]$ReplayFallback,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
if ($PSBoundParameters.ContainsKey('CaptureFiles') -and !$CaptureFiles.Count) {
    throw 'CaptureFiles was specified but contains no inputs; refusing an empty replay run.'
}
$repoOracle = Split-Path $PSScriptRoot -Parent
$buildOracle = (Resolve-Path $NativeBuildDirectory).Path
$cacheOracle = Get-Content (Join-Path $buildOracle 'CMakeCache.txt')
function CacheValue([string]$name) {
    $line = $cacheOracle | Where-Object { $_ -match "^${name}:[^=]+=" } | Select-Object -First 1
    if (!$line) { throw "Missing CMake cache key $name" }
    return $line.Substring($line.IndexOf('=') + 1)
}
function Invoke-AdbOracle([string[]]$arguments) {
    # Native tools write transfer progress to stderr; PowerShell 5.1 converts
    # redirected stderr into a terminating NativeCommandError when
    # ErrorActionPreference is Stop, even for a successful command.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $result = & $Adb @script:deviceOracle @arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($exitCode -ne 0) { throw "adb failed ($exitCode): $result" }
    return $result
}
if ((CacheValue 'EMUCOREX_ENABLE_NATIVE_SELF_TESTS') -ne 'ON') {
    throw 'Use a debug native build configured with EMUCOREX_ENABLE_NATIVE_SELF_TESTS=ON.'
}
if (!$SkipBuild) {
    $cmakeOracle = CacheValue 'CMAKE_COMMAND'
    & $cmakeOracle --build $buildOracle --target emucore emucorex_jit_oracle -j 6
    if ($LASTEXITCODE -ne 0) { throw 'Native oracle build failed.' }
}
$script:deviceOracle = @()
if ($Serial) { $script:deviceOracle = @('-s', $Serial) }
else {
    $devicesOracle = @(& $Adb devices | Where-Object { $_ -match '^\S+\s+device$' })
    if ($devicesOracle.Count -ne 1) { throw 'Specify -Serial when exactly one authorized device is not connected.' }
    $Serial = ($devicesOracle[0] -split '\s+')[0]
    $script:deviceOracle = @('-s', $Serial)
}
$nativeOracle = CacheValue 'CMAKE_LIBRARY_OUTPUT_DIRECTORY'
$libraryOracle = Join-Path $nativeOracle ('lib' + (CacheValue 'EMUCOREX_NATIVE_LIBRARY_NAME') + '.so')
$runnerOracle = Join-Path $nativeOracle 'emucorex_jit_oracle'
$runOracle = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$remoteOracle = "/data/local/tmp/emucorex-oracle-$runOracle"
$reportOracle = Join-Path $OutputDirectory $runOracle
New-Item -ItemType Directory -Path $reportOracle -Force | Out-Null
Invoke-AdbOracle @('shell', 'mkdir', '-p', $remoteOracle)
Invoke-AdbOracle @('push', $libraryOracle, "$remoteOracle/core.so")
if ($Suite -contains 'classifier') {
    Invoke-AdbOracle @('shell', 'mkdir', '-p', "$remoteOracle/resources")
    foreach ($databaseOracle in @('GameIndex.yaml', 'emucorex_overrides.yaml')) {
        Invoke-AdbOracle @('push', (Join-Path $repoOracle "app/src/main/cpp/pcsx2/bin/resources/$databaseOracle"), "$remoteOracle/resources/$databaseOracle")
    }
}
if ($Suite -contains 'gs-prepare' -or $Suite -contains 'gs-job-bridge' -or $Suite -contains 'gs-job-runtime') {
    $sourceOracle = Join-Path $repoOracle 'app/src/main/cpp/pcsx2/bin/resources/shaders/vulkan/tfx.glsl'
    if (!(Test-Path -LiteralPath $sourceOracle)) { throw 'Missing canonical Vulkan TFX source.' }
    Invoke-AdbOracle @('shell', 'mkdir', '-p', "$remoteOracle/resources/shaders/vulkan")
    Invoke-AdbOracle @('push', $sourceOracle, "$remoteOracle/resources/shaders/vulkan/tfx.glsl")
}
# The native library links librashader dynamically; the loader needs it in the
# same search path the run command below sets.
$shaderLibraryOracle = Join-Path $buildOracle 'arm_android/third_party/librashader/cargo/aarch64-linux-android/release/liblibrashader_capi.so'
$shaderLibraryHashOracle = $null
if (Test-Path -LiteralPath $shaderLibraryOracle) {
    Invoke-AdbOracle @('push', $shaderLibraryOracle, "$remoteOracle/liblibrashader_capi.so")
    $shaderLibraryHashOracle = (Get-FileHash $shaderLibraryOracle -Algorithm SHA256).Hash
}
Invoke-AdbOracle @('push', $runnerOracle, "$remoteOracle/oracle")
Invoke-AdbOracle @('shell', 'chmod', '755', "$remoteOracle/oracle")
$metadataOracle = [ordered]@{
    commit = (& git -C $repoOracle rev-parse HEAD)
    dirty = [bool](& git -C $repoOracle status --porcelain)
    experimentalIr = (($cacheOracle | Where-Object { $_ -match '^EMUCOREX_ENABLE_EXPERIMENTAL_IR:BOOL=' } | Select-Object -First 1) -eq 'EMUCOREX_ENABLE_EXPERIMENTAL_IR:BOOL=ON')
    serial = $Serial
    librarySha256 = (Get-FileHash $libraryOracle -Algorithm SHA256).Hash
    shaderLibrarySha256 = $shaderLibraryHashOracle
    runnerSha256 = (Get-FileHash $runnerOracle -Algorithm SHA256).Hash
    device = (Invoke-AdbOracle @('shell', 'getprop', 'ro.product.model')) -join "`n"
    hardware = (Invoke-AdbOracle @('shell', 'getprop', 'ro.hardware')) -join "`n"
    eeIr = (Invoke-AdbOracle @('shell', 'getprop', 'debug.emucorex.ee_ir')) -join "`n"
    iopIr = (Invoke-AdbOracle @('shell', 'getprop', 'debug.emucorex.iop_ir')) -join "`n"
    eeIrMin = (Invoke-AdbOracle @('shell', 'getprop', 'debug.emucorex.ee_ir_min')) -join "`n"
    eeIrMax = (Invoke-AdbOracle @('shell', 'getprop', 'debug.emucorex.ee_ir_max')) -join "`n"
    remoteDirectory = $remoteOracle
}
$metadataOracle | ConvertTo-Json | Set-Content (Join-Path $reportOracle 'metadata.json') -Encoding utf8
$failedOracle = $false
foreach ($caseOracle in $Suite) {
    # A unique directory keeps concurrent agents/runs independent. Preserve all
    # failures, including crashes/timeouts; never convert an interrupted run to PASS.
    $ErrorActionPreference = 'Continue'
    $outputOracle = & $Adb @script:deviceOracle shell "LD_LIBRARY_PATH=$remoteOracle $remoteOracle/oracle $remoteOracle/core.so $caseOracle" 2>&1
    $exitOracle = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    $outputOracle | Tee-Object -FilePath (Join-Path $reportOracle "$caseOracle.txt")
    if ($exitOracle -ne 0 -or !($outputOracle -match '^RESULT checks=[1-9][0-9]* failures=0$')) {
        $failedOracle = $true
        Write-Warning "$caseOracle failed with exit code $exitOracle"
    }
}
Invoke-AdbOracle @('pull', "$remoteOracle/artifacts", (Join-Path $reportOracle 'artifacts'))
$captureManifestOracle = @()
$captureIndexOracle = 0
foreach ($captureOracle in $CaptureFiles) {
    $inputOracle = (Resolve-Path -LiteralPath $captureOracle).Path
    $caseNameOracle = 'replay-{0:D4}' -f $captureIndexOracle++
    $caseRemoteOracle = "$remoteOracle/$caseNameOracle"
    Invoke-AdbOracle @('shell', 'mkdir', $caseRemoteOracle)
    Invoke-AdbOracle @('shell', 'ln', '-s', '../core.so', "$caseRemoteOracle/core.so")
    Invoke-AdbOracle @('push', $inputOracle, "$caseRemoteOracle/input.vuo")
    if (Test-Path -LiteralPath "$inputOracle.actual") {
        Invoke-AdbOracle @('push', "$inputOracle.actual", "$caseRemoteOracle/input.vuo.actual")
    }
    if (Test-Path -LiteralPath "$inputOracle.actual.gif.bin") {
        Invoke-AdbOracle @('push', "$inputOracle.actual.gif.bin", "$caseRemoteOracle/input.vuo.actual.gif.bin")
    }
    $captureManifestOracle += [ordered]@{ name = $caseNameOracle; path = $inputOracle; sha256 = (Get-FileHash $inputOracle -Algorithm SHA256).Hash }
    $replayModeOracle = if ($ReplayFallback) { 'replay-vu1-fallback' } else { 'replay-vu1' }
    $ErrorActionPreference = 'Continue'
    $outputOracle = & $Adb @script:deviceOracle shell "LD_LIBRARY_PATH=$remoteOracle $remoteOracle/oracle $caseRemoteOracle/core.so $replayModeOracle input.vuo" 2>&1
    $exitOracle = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    $outputOracle | Tee-Object -FilePath (Join-Path $reportOracle "$caseNameOracle.txt")
    if ($exitOracle -ne 0 -or !($outputOracle -match '^RESULT checks=[1-9][0-9]* failures=0$')) {
        $failedOracle = $true
        Write-Warning "$caseNameOracle failed with exit code $exitOracle"
    }
    Invoke-AdbOracle @('pull', "$caseRemoteOracle/artifacts", (Join-Path $reportOracle "$caseNameOracle-artifacts"))
}
$captureManifestOracle | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $reportOracle 'captures.json') -Encoding utf8
Write-Host "Reports: $reportOracle"
if ($failedOracle) { exit 1 }
