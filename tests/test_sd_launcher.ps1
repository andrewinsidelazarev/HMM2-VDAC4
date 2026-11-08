# Codex: среда Z80 с перенаправлением всех временных файлов на E:.
. "$PSScriptRoot\..\tools\env.ps1"
& 'E:\zx\esp\fat32-venv\Scripts\python.exe' "$PSScriptRoot\test_sd_launcher.py"
exit $LASTEXITCODE
