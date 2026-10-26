# SPDX-License-Identifier: GPL-3.0+
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$NativeBuildDirectory,
    [Parameter(Mandatory)][string]$Serial,
    [string]$Adb = "$env:LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe",
    [string]$Python = 'python',
    [ValidateRange(1,2147483647)][int[]]$Seeds = @(1, 7, 42),
    [ValidateRange(0,4096)][int]$Cases = 512,
    [string]$DumpDirectory,
    [ValidateSet('sw', 'vk', 'gl')][string[]]$Backends = @('sw', 'vk', 'gl'),
    [ValidateRange(1,100)][int]$Warmup = 2,
    [ValidateRange(1,100)][int]$Repeats = 7,
    [string]$OutputDirectory = "$PSScriptRoot/results",
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
if (!$Seeds.Count -or $Seeds.Count -gt 32 -or @($Seeds | Select-Object -Unique).Count -ne $Seeds.Count) {
    throw 'Specify 1..32 unique seeds.'
}
if ($Backends -notcontains 'sw' -or $Backends.Count -lt 2 -or @($Backends | Select-Object -Unique).Count -ne $Backends.Count) {
    throw 'Select Software and at least one unique hardware backend.'
}
$jobsGS = @($Seeds | ForEach-Object { [ordered]@{ id = "seed-$_"; seed = $_; input = $null; input_name = 'builtin'; input_sha256 = $null; report_directory = $null; error = $null } })
if ($DumpDirectory) {
    $dumpsGS = @(Get-ChildItem -LiteralPath (Resolve-Path -LiteralPath $DumpDirectory).Path -Recurse -File |
        Where-Object { $_.Name -match '\.gs(\.xz|\.zst)?$' } | Sort-Object FullName)
    if (!$dumpsGS.Count) { throw 'DumpDirectory contains no GS dumps.' }
    for ($iGS = 0; $iGS -lt $dumpsGS.Count; ++$iGS) {
        $jobsGS += [ordered]@{ id = "dump-$iGS"; seed = $Seeds[0]; input = $dumpsGS[$iGS].FullName;
            input_name = $dumpsGS[$iGS].Name; input_sha256 = (Get-FileHash -LiteralPath $dumpsGS[$iGS].FullName -Algorithm SHA256).Hash.ToLowerInvariant();
            report_directory = $null; error = $null }
    }
}
$campaignGS = Join-Path $OutputDirectory ('campaign-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $campaignGS -Force | Out-Null
$campaignGS = (Resolve-Path -LiteralPath $campaignGS).Path
$manifestGS = [ordered]@{ schema = 1; complete = $false; serial = $Serial; cases = $Cases; backends = @($Backends); runs = @($jobsGS) }
function Save-GSCampaign {
    $manifestGS | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath "$campaignGS/campaign.json" -Encoding utf8
}
Save-GSCampaign
$skipGS = $SkipBuild.IsPresent
foreach ($jobGS in $jobsGS) {
    $parentGS = Join-Path $campaignGS $jobGS.id
    $argumentsGS = @{
        NativeBuildDirectory = $NativeBuildDirectory; Serial = $Serial; Adb = $Adb; Python = $Python
        Backends = $Backends; Cases = $Cases; Seed = $jobGS.seed; Warmup = $Warmup; Repeats = $Repeats
        OutputDirectory = $parentGS; SkipBuild = $skipGS
    }
    if ($jobGS.input) { $argumentsGS.InputDump = $jobGS.input }
    Write-Host "Campaign: $($jobGS.id)"
    try { & "$PSScriptRoot/Run-GSOracle.ps1" @argumentsGS *> "$campaignGS/$($jobGS.id).log" }
    catch { $jobGS.error = $_.Exception.Message }
    # A mismatch intentionally throws in Run-GSOracle, but has a complete report.
    # Native/transport/build errors cannot acquire a successful report this way.
    if (Test-Path -LiteralPath $parentGS) {
        $reportsGS = @(Get-ChildItem -LiteralPath $parentGS -Directory |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'report.json') })
        if ($reportsGS.Count -eq 1) {
            $jobGS.report_directory = "$($jobGS.id)/$($reportsGS[0].Name)"
            $jobGS.error = $null
        }
    }
    Save-GSCampaign
    # Only reuse the build once the runner has produced a completed report.
    if ($jobGS.report_directory) { $skipGS = $true }
}
$manifestGS.complete = $true
Save-GSCampaign
Write-Host "Campaign evidence: $campaignGS"
& $Python "$PSScriptRoot/compare.py" campaign $campaignGS
if ($LASTEXITCODE -eq 2) { throw "Campaign has execution errors: $campaignGS/campaign-report.md" }
if ($LASTEXITCODE -ne 0) { throw "Campaign found renderer differences: $campaignGS/campaign-report.md" }
