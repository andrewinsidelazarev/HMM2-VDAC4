# Codex: временные файлы и кэши всех дочерних инструментов держим на E:.
if (!$env:HMM2_TMP) { $env:HMM2_TMP = 'E:\zx\cache\tmp\hmm2_esp' }
$env:TEMP = $env:HMM2_TMP
$env:TMP = $env:HMM2_TMP
$env:TMPDIR = $env:HMM2_TMP
$env:CCACHE_DIR = Join-Path $env:HMM2_TMP 'ccache'
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:PYTHONIOENCODING = 'utf-8'
$env:PYTHONPYCACHEPREFIX = Join-Path $env:HMM2_TMP 'pycache'
$env:PIP_CACHE_DIR = Join-Path $env:HMM2_TMP 'pip'
$env:XDG_CACHE_HOME = Join-Path $env:HMM2_TMP 'cache'
$env:XDG_CONFIG_HOME = Join-Path $env:HMM2_TMP 'config'
$env:APPDATA = Join-Path $env:HMM2_TMP 'appdata'
$env:LOCALAPPDATA = Join-Path $env:HMM2_TMP 'localappdata'
$env:USERPROFILE = Join-Path $env:HMM2_TMP 'profile'
New-Item -ItemType Directory -Force $env:HMM2_TMP,$env:CCACHE_DIR,$env:APPDATA,$env:LOCALAPPDATA,$env:USERPROFILE | Out-Null
$script:Python = 'E:\zx\esp\.espressif\python_env\idf6.0_py3.14_env\Scripts\python.exe'

# Codex: установленный QEMU требует libiconv из локального набора BL616. Только PATH процесса.
$taskQemuDll = "E:\zx\bl616\bouffalo_sdk\tools\make"
if (Test-Path -LiteralPath (Join-Path $taskQemuDll "libiconv-2.dll")) { $env:PATH = $taskQemuDll + ";" + $env:PATH }
