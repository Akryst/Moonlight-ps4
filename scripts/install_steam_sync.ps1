param(
    [Parameter(Mandatory=$true)][string]$PythonPath,
    [Parameter(Mandatory=$true)][string]$SteamPath,
    [string]$AppsPath = "$env:ProgramFiles\Sunshine\config\apps.json"
)
$ErrorActionPreference = 'Stop'
$installDir = Join-Path $env:ProgramData 'MoonlightPS4'
if (!(Test-Path -LiteralPath $PythonPath) -or !(Test-Path -LiteralPath $AppsPath)) {
    throw 'Python or Sunshine configuration not found.'
}
& $PythonPath -c 'from PIL import Image'
if ($LASTEXITCODE -ne 0) { throw 'Install Pillow for this Python before enabling Steam cover synchronization.' }
New-Item -ItemType Directory -Path $installDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'steam_sync.py') -Destination (Join-Path $installDir 'steam_sync.py') -Force
# Literal arguments are escaped for PowerShell, not interpolated as commands.
$quote = { param($value) "'" + $value.Replace("'", "''") + "'" }
$logAssignment = '$logPath = ' + (& $quote (Join-Path $installDir 'steam-sync.log'))
$syncCommand = '& ' + (& $quote $PythonPath) + ' ' + (& $quote (Join-Path $installDir 'steam_sync.py')) +
    ' --steam ' + (& $quote $SteamPath) + ' --apps ' + (& $quote $AppsPath) + ' --apply --safe-reload *>> $logPath'
$launcher = @(
    '$ErrorActionPreference = ''Stop''',
    $logAssignment,
    'if ((Test-Path -LiteralPath $logPath) -and (Get-Item -LiteralPath $logPath).Length -gt 1048576) { Move-Item -LiteralPath $logPath -Destination ($logPath + ''.1'') -Force }',
    $syncCommand,
    'exit $LASTEXITCODE'
) -join "`r`n"
$launcherPath = Join-Path $installDir 'run-steam-sync.ps1'
[IO.File]::WriteAllText($launcherPath, $launcher, [Text.UTF8Encoding]::new($false))
$identity = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument ('-NoProfile -WindowStyle Hidden -File "' + $launcherPath + '"')
$triggers = @(
    (New-ScheduledTaskTrigger -AtLogOn -User $identity),
    (New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(5) -RepetitionInterval (New-TimeSpan -Minutes 5))
)
$principal = New-ScheduledTaskPrincipal -UserId $identity -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 2) -StartWhenAvailable
Register-ScheduledTask -TaskName 'Moonlight PS4 Steam Sync' -Action $action -Trigger $triggers -Principal $principal -Settings $settings -Description 'Publishes installed Steam applications and cover art to Sunshine. Defers reload while a session is active.' -Force | Out-Null
& $PythonPath (Join-Path $installDir 'steam_sync.py') --steam $SteamPath --apps $AppsPath --apply --safe-reload
if ($LASTEXITCODE -ne 0) { throw 'Initial Steam synchronization failed.' }
Write-Output 'Steam sync installed: at login and every five minutes while signed in.'
