$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\tools\env.ps1"
& $Python "$PSScriptRoot\build_profile.py" @args
exit $LASTEXITCODE
