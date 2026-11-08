# Codex: повтор уже собранной игры на стендах; только файлы E:, без прошивки и SD Evo.
param([switch]$NoPackage,[switch]$QemuOnly)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\env.ps1"
Push-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
function Check-Python {
    & $Python @args
    if ($LASTEXITCODE -ne 0) { throw "Codex: проверка не прошла: $args" }
}
try {
    if (-not $QemuOnly) {
    Check-Python tests/test_stage1_rules_tables.py
    Check-Python tools/measure_stage1_stack.py
    Check-Python tools/audit_stage1_callgraph.py
    Check-Python tests/test_stage1_platform.py
    Check-Python tests/test_stage1_packing.py
    Check-Python tests/test_stage1_puff.py
    Check-Python tests/test_stage1_delivery.py
    Check-Python tests/test_stage1_launcher.py
    Check-Python tests/test_stage1_launcher_failures.py
    Check-Python tests/run_stage1_menu.py
    Check-Python tests/run_stage1_scene.py --x64
    Check-Python tests/run_stage1_scene.py --packed
    Check-Python tests/compare_stage1_scenes.py
    Check-Python tests/check_stage1_gpu.py
    Check-Python tests/run_stage1_scene.py --x64 --allmaps
    Check-Python tests/run_stage1_scene.py --packed --allmaps
    Check-Python tests/check_stage1_saves.py
    Check-Python tests/compare_stage1_maps.py
    }
    & powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_qemu_stage1.ps1
    if ($LASTEXITCODE -ne 0) { throw 'Codex: стенд QEMU не собрался' }
    Check-Python tests/prepare_qemu_scene.py GateHell.mp2 --extended
    Check-Python tools/run_qemu_stage1.py
    Check-Python tests/archive_stage1_qemu.py gatehell 200 600 900
    Check-Python tests/check_stage1_target_save.py
    Check-Python tests/prepare_qemu_scene.py KINGDTH.MX2 --town
    Check-Python tools/run_qemu_stage1.py
    Check-Python tests/archive_stage1_qemu.py kingdth 300 440 600 700
    Check-Python tests/prepare_qemu_scene.py battle
    Check-Python tools/run_qemu_stage1.py
    Check-Python tests/archive_stage1_qemu.py battle 80 200 400
    Check-Python tests/prepare_qemu_scene.py menu
    Check-Python tools/run_qemu_stage1.py
    Check-Python tests/check_stage1_qemu.py
    Check-Python tests/archive_stage1_qemu.py menu 1 30
    Copy-Item -LiteralPath "$env:HMM2_TMP\stage1\qemu_stage1\compare.json" -Destination Docs/evidence/stage1_qemu_menu_compare.json -Force
    Check-Python tests/prepare_qemu_scene.py BELTWAY.MP2 --week
    Check-Python tools/run_qemu_stage1.py
    Check-Python tests/archive_stage1_qemu.py week 300 1000 1900
    Check-Python tests/check_stage1_target_save.py week
    Check-Python tests/prepare_qemu_scene.py load
    Check-Python tools/run_qemu_stage1.py
    Check-Python tests/archive_stage1_qemu.py loaded 300 800 1000
    Check-Python tests/check_stage1_target_save.py loaded
    Check-Python tests/prepare_qemu_scene.py KINGDTH.MX2 --deep-town
    Check-Python tools/run_qemu_stage1.py
    Check-Python tests/archive_stage1_qemu.py deep_town 300 500 630 700 1000 --keys
    Check-Python tests/check_stage1_stack.py
    Check-Python tools/archive_stage1_native.py
    if (-not $NoPackage) { Check-Python tools/package_stage1.py }
} finally { Pop-Location }
