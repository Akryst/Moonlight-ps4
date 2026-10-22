$ErrorActionPreference = 'Stop'
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run update-companion.bat as administrator. The installed companion is protected by Windows ACLs.'
}
$source = Join-Path $PSScriptRoot 'steam_sync.py'
$destination = Join-Path $env:ProgramData 'MoonlightPS4\steam_sync.py'
if (!(Test-Path -LiteralPath $destination -PathType Leaf)) {
    throw 'The companion is not installed. Use install_steam_sync.ps1 first.'
}
$temporary = "$destination.update.tmp"
$backup = "$destination.before-$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
try {
    Copy-Item -LiteralPath $source -Destination $temporary -Force
    if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $temporary).Hash) {
        throw 'Companion update checksum mismatch.'
    }
    [IO.File]::Replace($temporary, $destination, $backup)
    Write-Output 'Updated. Open Moonlight on PS4 to synchronize Steam and installed Playnite. No periodic task was added.'
    Write-Output "Backup: $backup"
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
}
