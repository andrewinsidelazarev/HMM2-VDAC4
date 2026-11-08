# Codex: параметры передаются Python без запуска эмулятора или обращения к плате.
. "$PSScriptRoot\..\tools\env.ps1"
& $Python "$PSScriptRoot\run_profile.py" @args
exit $LASTEXITCODE
