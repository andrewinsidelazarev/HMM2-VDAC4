# Codex: все временные файлы и результаты остаются внутри изолированной рабочей копии.
param([Parameter(Mandatory=$true)][string]$Scenario, [Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$workspace=Split-Path (Split-Path $PSScriptRoot)
$env:HMM2_TMP=Join-Path $workspace 'temp'
# Codex: явная стадия нужна для независимой пары «до/после» внутри workspace/stage.
if (!$env:HMM2_STAGE1_BASE) { $env:HMM2_STAGE1_BASE=(Join-Path $workspace 'stage').Replace('\','/') }
. "$PSScriptRoot/env.ps1"
$localQemu=Join-Path $workspace 'local_tools/qemu/bin/qemu-system-xtensa.exe'
if (Test-Path -LiteralPath $localQemu) { $env:HMM2_QEMU_EXE=$localQemu }
& $Python "$PSScriptRoot/../tests/memory_session.py" $Scenario $Output
exit $LASTEXITCODE
