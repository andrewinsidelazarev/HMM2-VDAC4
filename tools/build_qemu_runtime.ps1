# Codex: собираем только стенд QEMU; вызовов flash и записи на карту здесь нет.
$ErrorActionPreference='Stop'
. "$PSScriptRoot\env.ps1"
. 'E:\zx\esp\idf-env.ps1'
. "$PSScriptRoot\env.ps1"
$out='E:\zx\cache\tmp\hmm2_esp\stage0b\qemu_runtime'
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot\..\tests\qemu_runtime\CMakeLists.txt" -Destination $out -Force
Copy-Item -LiteralPath "$PSScriptRoot\..\tests\qemu_runtime\sdkconfig.defaults" -Destination $out -Force
New-Item -ItemType Directory -Force "$out\main" | Out-Null
Get-ChildItem -LiteralPath "$PSScriptRoot\..\tests\qemu_runtime\main" -File | Copy-Item -Destination "$out\main" -Force
Copy-Item -LiteralPath 'E:\zx\cache\tmp\hmm2_esp\stage0b\pic_runtime\HRT.ELF' -Destination "$out\main\HRT.ELF" -Force
Push-Location -LiteralPath $out
try { $ErrorActionPreference='Continue'; & $Python "$env:IDF_PATH\tools\idf.py" -B "$out\build" -D IDF_TARGET=esp32s3 -D "SDKCONFIG=$out/sdkconfig.quad" build *> "$out\build.log"; $result=$LASTEXITCODE }
finally { $ErrorActionPreference='Stop'; Pop-Location }
Get-Content -LiteralPath "$out\build.log" -Tail 25
exit $result
