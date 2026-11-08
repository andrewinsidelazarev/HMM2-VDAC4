$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\env.ps1"
& $Python "$PSScriptRoot\audit_evidence.py"
exit $LASTEXITCODE
