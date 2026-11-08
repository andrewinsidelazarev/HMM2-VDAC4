# Codex: парная выдача56; перед записью сохранить текущие файлы на E: и проверить SHA.
$ErrorActionPreference='Stop'
. "$PSScriptRoot\env.ps1"
$sourceFolder=Join-Path $PSScriptRoot '..\dist\HMM2_build56_audio_cache_test_update'
$remoteFolder='\\ZX-Evo\1\GAMES\VDAC4\HMM2 VDAC4'
$manifest=Get-Content -LiteralPath (Join-Path $sourceFolder 'manifest.json') -Raw -Encoding utf8 | ConvertFrom-Json
if($manifest.build -ne 56){throw 'Unexpected build'}
$names=@('HMM2.ELF','_HMM2.SPG')
foreach($name in $names){
 $local=Join-Path $sourceFolder $name
 if((Get-FileHash -LiteralPath $local).Hash.ToLowerInvariant() -ne $manifest.files.$name.sha256){throw "Local SHA mismatch: $name"}
 if(!(Test-Path -LiteralPath (Join-Path $remoteFolder $name))){throw "SMB file unavailable: $name"}
}
$backupFolder=Join-Path 'E:\zx\cache\tmp\hmm2_esp\integration_audio' ('smb_delivery56_'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ'))
New-Item -ItemType Directory -Path $backupFolder | Out-Null
$before=@{};$staged=@{};$expected=@{};$activated=@()
foreach($name in $names){
 Write-Output "BACKUP $name"
 Copy-Item -LiteralPath (Join-Path $remoteFolder $name) -Destination (Join-Path $backupFolder $name)
 $before[$name]=(Get-FileHash -LiteralPath (Join-Path $backupFolder $name)).Hash
 $expected[$name]=$manifest.files.$name.sha256.ToUpperInvariant()
}
Copy-Item -LiteralPath (Join-Path $remoteFolder 'fheroes2.cfg') -Destination $backupFolder -ErrorAction Stop
foreach($name in $names){
 $staged[$name]=Join-Path $remoteFolder ('H6'+[Guid]::NewGuid().ToString('N').Substring(0,6).ToUpperInvariant()+'.TMP')
 Write-Output "UPLOAD staged $name"
 Copy-Item -LiteralPath (Join-Path $sourceFolder $name) -Destination $staged[$name]
 if((Get-FileHash -LiteralPath $staged[$name]).Hash -ne $expected[$name]){throw "Staged SHA mismatch: $name"}
}
foreach($name in $names){if((Get-FileHash -LiteralPath (Join-Path $remoteFolder $name)).Hash -ne $before[$name]){throw "Active file changed: $name"}}
try {
 foreach($name in $names){
  Write-Output "ACTIVATE $name"
  $active=Join-Path $remoteFolder $name
  # Codex: имя рабочее заменяется только после проверки обоих временных файлов.
  $activated+=$name
  try {Move-Item -LiteralPath $staged[$name] -Destination $active -Force}
  catch {Copy-Item -LiteralPath (Join-Path $sourceFolder $name) -Destination $active -Force}
  if((Get-FileHash -LiteralPath $active).Hash -ne $expected[$name]){throw "Active SHA mismatch: $name"}
 }
} catch {
 foreach($name in $activated){Copy-Item -LiteralPath (Join-Path $backupFolder $name) -Destination (Join-Path $remoteFolder $name) -Force}
 throw
}
foreach($name in $names){if(Test-Path -LiteralPath $staged[$name]){Remove-Item -LiteralPath $staged[$name]}}
$record=@{build=56;delivered=$true;target=$remoteFolder;files=$expected;previous_files=$before;backup=$backupFolder;dat_changed=$false;settings_changed=$false;saves_changed=$false;verified_utc=[DateTime]::UtcNow.ToString('o')}
$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $backupFolder 'delivery.json') -Encoding utf8
$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $sourceFolder 'smb_delivery.json') -Encoding utf8
$record | ConvertTo-Json -Depth 6 -Compress
