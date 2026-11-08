$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\env.ps1"
& $Python "$PSScriptRoot\inventory.py"
exit $LASTEXITCODE
