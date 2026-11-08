$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\tools\env.ps1"
& $Python "$PSScriptRoot\run_smk_profile.py" @args
exit $LASTEXITCODE
