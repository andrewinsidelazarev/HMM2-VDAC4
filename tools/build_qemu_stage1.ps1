# Codex: образ стенда QEMU; прошивка платы и её исходники не меняются.
param([string]$Module='', [switch]$DirectBuild, [switch]$Reconfigure)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\env.ps1"
# Codex: экспорт инструментов без переноса кэша за пределы заданного HMM2_TMP.
$env:IDF_PATH='E:\zx\esp\esp-idf'
$env:IDF_TOOLS_PATH='E:\zx\esp\.espressif'
$env:IDF_COMPONENT_MANAGER='0'
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
$ErrorActionPreference='Continue'
# Codex: предупреждение IDF о штатном пути QEMU не делает успешный make ошибкой.
# Стенд использует HMM2_QEMU_EXE; сообщения export остаются в рабочем каталоге.
$exports = & $Python "$env:IDF_PATH\tools\idf_tools.py" export --format key-value 2>&1
$ErrorActionPreference='Stop'
# Codex: отдельный журнал процесса позволяет собирать независимые стадии одновременно.
$exports | Out-File -LiteralPath (Join-Path $env:HMM2_TMP "idf_export_$PID.log") -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw 'idf_tools.py export failed' }
foreach ($line in $exports) {
    if ($line -match '^([^=]+)=(.*)$') {
        $name=$Matches[1];$value=$Matches[2]
        if ($name -eq 'PATH') { $value=$value -replace '%PATH%',$env:PATH -replace '\$PATH',$env:PATH }
        Set-Item -Path "Env:$name" -Value $value
    }
}
$stage=if ($env:HMM2_STAGE1_BASE) { $env:HMM2_STAGE1_BASE } else { 'E:\zx\cache\tmp\hmm2_esp\stage1' }
$out=Join-Path $stage 'qemu_stage1'
New-Item -ItemType Directory -Force $out,"$out\main","$out\run" | Out-Null
# Codex: неизменённые CMake-файлы сохраняют время; новый ELF не требует повторной конфигурации IDF.
function Copy-Changed($source, $target) {
    if (!(Test-Path -LiteralPath $target) -or (Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $target).Hash) {
        Copy-Item -LiteralPath $source -Destination $target -Force
        # Codex: Copy-Item сохраняет старое время; Ninja обязан пересобрать и прежний сохранённый ELF.
        (Get-Item -LiteralPath $target).LastWriteTimeUtc=[DateTime]::UtcNow
    }
}
Get-ChildItem -LiteralPath "$PSScriptRoot\..\tests\qemu_stage1" -File | ForEach-Object { Copy-Changed $_.FullName (Join-Path $out $_.Name) }
Get-ChildItem -LiteralPath "$PSScriptRoot\..\tests\qemu_stage1\main" -File | ForEach-Object { Copy-Changed $_.FullName (Join-Path "$out\main" $_.Name) }
# Codex: отдельный сохранённый модуль позволяет сравнить исходную сборку, не подменяя текущую.
$moduleSource=if ($Module) { $Module } else { Join-Path $stage 'module\HMM2.ELF' }
Copy-Changed $moduleSource "$out\main\HMM2.ELF"
Copy-Changed "E:/zx/Tang Nano 20k VDAC/VDAC4/ESP32 S3 Zero N8R8/Sound/modules/out/OGG.ELF" "$out\main\OGG.ELF"
# Codex: абсолютные пути semihosting относятся только к выбранному каталогу стенда.
$pathHeader='#define HMM2_TEST_BASE "'+$stage.Replace('\','/')+'"'+"`n"
[System.IO.File]::WriteAllText("$out\main\test_paths.h",$pathHeader,[System.Text.UTF8Encoding]::new($false))
# Codex: существующий кэш sdkconfig тоже обязан включать аппаратную защиту конца стека.
if (Test-Path -LiteralPath "$out\sdkconfig.quad") {
    $stackConfig = Get-Content -LiteralPath "$out\sdkconfig.quad" -Raw -Encoding UTF8
    $stackConfig = $stackConfig.Replace('# CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK is not set', 'CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK=y')
    $stackConfig = $stackConfig.Replace('# CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS is not set', 'CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y')
    # Codex: таймауты сигналов и звук должны иметь ту же частоту тика, что и плата.
    $stackConfig = $stackConfig -replace 'CONFIG_FREERTOS_HZ=\d+', 'CONFIG_FREERTOS_HZ=1000'
    if ($stackConfig -ne (Get-Content -LiteralPath "$out\sdkconfig.quad" -Raw -Encoding UTF8)) {
        [System.IO.File]::WriteAllText("$out\sdkconfig.quad", $stackConfig, [System.Text.UTF8Encoding]::new($false))
    }
}
Push-Location -LiteralPath $out
try {
    $ErrorActionPreference='Continue'
    if ($DirectBuild -and (Test-Path -LiteralPath "$out/build/build.ninja")) {
        & 'E:/zx/esp/.espressif/tools/ninja/1.12.1/ninja.exe' -C "$out/build" -j 6 *> "$out/build.log"
        $result=$LASTEXITCODE
    } elseif ($DirectBuild) {
        # Codex: прямые CMake/MinGW Make обходятся без именованных каналов asyncio IDF и Ninja.
        $env:PATH=($env:PATH.Split(';') | Where-Object { $_ -notmatch 'Git[/\\]usr[/\\]bin' }) -join ';'
        $make='E:/zx/bl616/bouffalo_sdk/tools/make/mingw32-make.exe'
        # Codex: при замене только встроенного ELF достаточно зависимостей Make.
        # Настройки и CMakeLists проверяет сам check_build_system; принудительный
        # повтор конфигурации доступен отдельным ключом.
        $result=0
        if ($Reconfigure -or !(Test-Path -LiteralPath "$out/build/Makefile")) {
            & cmake -G 'MinGW Makefiles' "-DCMAKE_MAKE_PROGRAM=$make" -B "$out/build" -DPYTHON_DEPS_CHECKED=1 "-DPYTHON=$Python" -DESP_PLATFORM=1 -DIDF_TARGET=esp32s3 "-DSDKCONFIG=$out/sdkconfig.quad" -DCCACHE_ENABLE=OFF $out *> "$out/build.log"
            $result=$LASTEXITCODE
        # Codex: Windows PowerShell дописывает *> в UTF-16; начало журнала должно иметь ту же кодировку.
        } else { Set-Content -LiteralPath "$out/build.log" -Value '' -Encoding Unicode }
        if ($result -eq 0) { & $make -C "$out/build" -j 6 *>> "$out/build.log"; $result=$LASTEXITCODE }
    } else {
        & $Python "$env:IDF_PATH\tools\idf.py" -B "$out\build" -D IDF_TARGET=esp32s3 -D "SDKCONFIG=$out/sdkconfig.quad" build *> "$out\build.log"
        $result=$LASTEXITCODE
    }
}
finally { $ErrorActionPreference='Stop'; Pop-Location }
Get-Content -LiteralPath "$out\build.log" -Tail 30
exit $result
