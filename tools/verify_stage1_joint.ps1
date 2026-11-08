# Codex: последовательный повтор связки; всё хранится на E:, карта Evo не открывается.
$ErrorActionPreference='Stop'
. "$PSScriptRoot\env.ps1"
Push-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
function Check-Python {
    & $Python @args
    if ($LASTEXITCODE -ne 0) { throw "Codex: совместная проверка не прошла: $args" }
}
try {
    Check-Python tools/build_stage1.py --host --x64 --packed
    Check-Python tools/build_stage1_joint.py
    Check-Python tools/prepare_fix3_baseline.py
    # Codex: старый SPG обязан упасть; ожидаемый код возврата — 1.
    & $Python tests/test_stage1_joint.py --name before6 --spg "$env:HMM2_TMP/stage1/joint/before/_HMM2.SPG" --nested --period 6 --timeout 85
    if ($LASTEXITCODE -ne 1) { throw 'Codex: старый загрузчик не воспроизвёл отказ' }
    Check-Python tests/test_stage1_joint.py --name map1 --nested --play --timeout 170
    Check-Python tests/test_stage1_joint.py --name map10 --nested --play --period 10 --latency 3 --lose-reply --timeout 420
    Check-Python tests/test_stage1_joint.py --name bad_seq2 --nested --bad-seq --timeout 70
    Check-Python tests/check_stage1_joint.py
} finally { Pop-Location }
