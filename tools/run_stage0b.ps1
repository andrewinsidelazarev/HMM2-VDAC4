# Codex: оставляем ключи Python без привязки параметров PowerShell.
$Script = $args[0]
$ToolArgs = @($args | Select-Object -Skip 1)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\env.ps1"
& $Python (Join-Path (Split-Path -Parent $PSScriptRoot) $Script) @ToolArgs
exit $LASTEXITCODE
