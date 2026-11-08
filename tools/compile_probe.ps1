$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\env.ps1"
& $Python "$PSScriptRoot\compile_probe.py"
exit $LASTEXITCODE
