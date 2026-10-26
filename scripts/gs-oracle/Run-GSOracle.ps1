# SPDX-License-Identifier: GPL-3.0+
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$NativeBuildDirectory,
    [Parameter(Mandatory)][string]$Serial,
    [string]$Adb = "$env:LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe",
    [string]$Python = 'python',
    [string]$InputDump,
    [ValidateSet('sw', 'vk', 'gl')][string[]]$Backends = @('sw', 'vk', 'gl'),
    [ValidateRange(1,100)][int]$Warmup = 2,
    [ValidateRange(1,100)][int]$Repeats = 7,
    [ValidateRange(0,255)][int]$Tolerance = 0,
    [ValidateRange(0,4096)][int]$Cases = 256,
    [ValidateRange(1,2147483647)][int]$Seed = 1,
    [ValidateRange(-1,4139)][int]$ExportCase = -1,
    [string]$OutputDirectory = "$PSScriptRoot/results",
    [string]$Baseline,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$repoGS = (Resolve-Path "$PSScriptRoot/../..").Path
$buildGS = (Resolve-Path -LiteralPath $NativeBuildDirectory).Path
$cacheGS = Get-Content -LiteralPath (Join-Path $buildGS 'CMakeCache.txt')
function Get-GSCache([string]$Name) {
    $line = $cacheGS | Where-Object { $_ -match "^${Name}:[^=]+=" } | Select-Object -First 1
    if (!$line) { throw "Missing CMake cache entry $Name" }
    $line.Substring($line.IndexOf('=') + 1)
}
function Invoke-GSAdb([string[]]$Arguments) {
    $previousGS = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $resultGS = & $Adb -s $Serial @Arguments 2>&1; $codeGS = $LASTEXITCODE }
    finally { $ErrorActionPreference = $previousGS }
    if ($codeGS -ne 0) { throw "ADB failed ($codeGS): $resultGS" }
    $resultGS
}
if ((Get-GSCache 'CMAKE_BUILD_TYPE') -cne 'Debug' -or (Get-GSCache 'EMUCOREX_ENABLE_GS_ORACLE') -ne 'ON') {
    throw 'GS oracle requires a Debug native build with EMUCOREX_ENABLE_GS_ORACLE=ON.'
}
if ($Backends -notcontains 'sw' -or ($Backends | Select-Object -Unique).Count -ne $Backends.Count -or $Backends.Count -lt 2) {
    throw 'Select Software (sw) and at least one unique hardware backend.'
}
if ($InputDump) { $InputDump = (Resolve-Path -LiteralPath $InputDump).Path }
if ($Baseline) { $Baseline = (Resolve-Path -LiteralPath $Baseline).Path }
if (!$SkipBuild) {
    & (Get-GSCache 'CMAKE_COMMAND') --build $buildGS --target emucore emucorex_gs_oracle -j 6
    if ($LASTEXITCODE -ne 0) { throw 'GS oracle build failed.' }
}
$binGS = Get-GSCache 'CMAKE_LIBRARY_OUTPUT_DIRECTORY'
$coreGS = Join-Path $binGS ('lib' + (Get-GSCache 'EMUCOREX_NATIVE_LIBRARY_NAME') + '.so')
$runnerGS = Join-Path (Get-GSCache 'CMAKE_RUNTIME_OUTPUT_DIRECTORY') 'emucorex_gs_oracle'
$runIdGS = (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + ([guid]::NewGuid().ToString('N').Substring(0,8))
$remoteGS = "/data/local/tmp/emucorex-gs-$runIdGS"
$resultDirGS = Join-Path $OutputDirectory $runIdGS
New-Item -ItemType Directory -Force -Path $resultDirGS | Out-Null
$resultDirGS = (Resolve-Path -LiteralPath $resultDirGS).Path
$modelGS = (Invoke-GSAdb @('shell','getprop','ro.product.model') | Out-String).Trim()
$fingerprintGS = (Invoke-GSAdb @('shell','getprop','ro.build.fingerprint') | Out-String).Trim()
$hardwareGS = (Invoke-GSAdb @('shell','getprop','ro.hardware') | Out-String).Trim()
$metadataGS = [ordered]@{
    schema = 1; serial = $Serial; model = $modelGS; hardware = $hardwareGS; fingerprint = $fingerprintGS
    device = "$Serial|$modelGS|$hardwareGS|$fingerprintGS"
    commit = (& git -C $repoGS rev-parse HEAD); dirty = [bool](& git -C $repoGS status --porcelain)
    core_sha256 = (Get-FileHash -LiteralPath $coreGS -Algorithm SHA256).Hash.ToLowerInvariant()
    runner_sha256 = (Get-FileHash -LiteralPath $runnerGS -Algorithm SHA256).Hash.ToLowerInvariant()
    input = $(if ($InputDump) { [IO.Path]::GetFileName($InputDump) } else { 'builtin' })
    input_sha256 = $(if ($InputDump) { (Get-FileHash -LiteralPath $InputDump -Algorithm SHA256).Hash.ToLowerInvariant() } else { $null })
    backends = @($Backends); warmup = $Warmup; repeats = $Repeats
    cases = $Cases; seed = $Seed
    build_type = 'Debug'; remote_directory = $remoteGS
}
$metadataGS | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$resultDirGS/metadata.json" -Encoding utf8
Invoke-GSAdb @('shell','mkdir','-p', $remoteGS) | Out-Null
Invoke-GSAdb @('push', $coreGS, "$remoteGS/core.so") | Out-Null
Invoke-GSAdb @('push', $runnerGS, "$remoteGS/oracle") | Out-Null
Invoke-GSAdb @('push', "$repoGS/app/src/main/cpp/PCSX2/bin/resources", "$remoteGS/resources") | Out-Null
$rashaderGS = Join-Path $buildGS 'arm_android/third_party/librashader/cargo/aarch64-linux-android/release/liblibrashader_capi.so'
if (Test-Path -LiteralPath $rashaderGS) { Invoke-GSAdb @('push', $rashaderGS, "$remoteGS/liblibrashader_capi.so") | Out-Null }
Invoke-GSAdb @('shell','chmod','755', "$remoteGS/oracle") | Out-Null
$inputGS = 'builtin'
if ($InputDump) {
    $suffixGS = if ($InputDump.EndsWith('.xz')) { '.gs.xz' } elseif ($InputDump.EndsWith('.zst')) { '.gs.zst' } else { '.gs' }
    $inputGS = "$remoteGS/input$suffixGS"
    Invoke-GSAdb @('push', $InputDump, $inputGS) | Out-Null
    Copy-Item -LiteralPath $InputDump -Destination "$resultDirGS/input$suffixGS"
}
$failedGS = $false
foreach ($backendGS in $Backends) {
    foreach ($modeGS in @('capture', 'benchmark', 'profile')) {
        $remoteOutputGS = "$remoteGS/$backendGS/$modeGS"
        $localParentGS = Join-Path $resultDirGS $backendGS
        New-Item -ItemType Directory -Force -Path $localParentGS | Out-Null
        Write-Host "GS oracle: $modelGS $backendGS $modeGS"
        $ErrorActionPreference = 'Continue'
        $logGS = & $Adb -s $Serial shell "LD_LIBRARY_PATH=$remoteGS $remoteGS/oracle $remoteGS/core.so $backendGS $inputGS $remoteOutputGS $modeGS $Warmup $Repeats $Cases $Seed $ExportCase" 2>&1
        $codeGS = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        $logGS | Set-Content -LiteralPath "$localParentGS/$modeGS.log" -Encoding utf8
        # Pull even after failure: partial artifacts are useful evidence, never PASS.
        try { Invoke-GSAdb @('pull', $remoteOutputGS, $localParentGS) | Out-Null }
        catch { $failedGS = $true; Write-Warning $_ }
        if ($codeGS -ne 0 -or !($logGS -match "^GS_ORACLE_COMPLETE backend=$backendGS mode=$modeGS ")) {
            $failedGS = $true
            Write-Warning "Failed $backendGS $modeGS (exit $codeGS); see $localParentGS/$modeGS.log"
        }
    }
}
Write-Host "GS evidence: $resultDirGS"
if ($failedGS) { throw 'GS execution failed. Evidence was retained; no comparison PASS can be reported.' }
$reportArgsGS = @("$PSScriptRoot/compare.py", 'report', $resultDirGS, '--tolerance', "$Tolerance")
if ($Baseline) { $reportArgsGS += @('--baseline', $Baseline) }
& $Python @reportArgsGS
if ($LASTEXITCODE -eq 1) { throw "GS differences detected. See $resultDirGS/report.md" }
if ($LASTEXITCODE -ne 0) { throw "GS report failed. See $resultDirGS" }
