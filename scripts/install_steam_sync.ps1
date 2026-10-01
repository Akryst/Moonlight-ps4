param(
    [Parameter(Mandatory=$true)][string]$PythonPath,
    [Parameter(Mandatory=$true)][string]$SteamPath,
    [string]$AppsPath = "$env:ProgramFiles\Sunshine\config\apps.json"
)
$ErrorActionPreference = 'Stop'
if (![Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this installer as administrator to replace the task and configure the firewall.'
}
$installDir = Join-Path $env:ProgramData 'MoonlightPS4'
$pythonWindowless = Join-Path (Split-Path -Parent $PythonPath) 'pythonw.exe'
foreach ($path in @($PythonPath, $pythonWindowless, $AppsPath, (Join-Path (Split-Path -Parent $AppsPath) 'credentials\cakey.pem'))) {
    if (!(Test-Path -LiteralPath $path)) { throw "Required file not found: $path" }
}
& $PythonPath -c 'from PIL import Image'
if ($LASTEXITCODE -ne 0) { throw 'Install Pillow for this Python before enabling Steam cover synchronization.' }
New-Item -ItemType Directory -Path $installDir -Force | Out-Null
$existing = Get-ScheduledTask -TaskName 'Moonlight PS4 Steam Sync' -ErrorAction SilentlyContinue
if ($existing) {
    Export-ScheduledTask -TaskName $existing.TaskName | Set-Content -LiteralPath (Join-Path $installDir 'task-before-on-open.xml')
    Disable-ScheduledTask -TaskName $existing.TaskName | Out-Null
    Stop-ScheduledTask -TaskName $existing.TaskName
}
foreach ($name in @('steam_sync.py', 'steam_sync_server.py')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $installDir $name) -Force
}
# Explicit file ACLs avoid inheritance-only directory ACEs on script files.
foreach ($item in @((Get-Item -LiteralPath $installDir)) + @(Get-ChildItem -LiteralPath $installDir -Recurse)) {
    $acl = if ($item.PSIsContainer) { [Security.AccessControl.DirectorySecurity]::new() } else { [Security.AccessControl.FileSecurity]::new() }
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($entry in @(@('S-1-5-18','FullControl'), @('S-1-5-32-544','FullControl'), @('S-1-5-32-545','ReadAndExecute'))) {
        $sid = [Security.Principal.SecurityIdentifier]::new($entry[0])
        $rule = if ($item.PSIsContainer) {
            [Security.AccessControl.FileSystemAccessRule]::new($sid, [Security.AccessControl.FileSystemRights]$entry[1], [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit', [Security.AccessControl.PropagationFlags]::None, [Security.AccessControl.AccessControlType]::Allow)
        } else {
            [Security.AccessControl.FileSystemAccessRule]::new($sid, [Security.AccessControl.FileSystemRights]$entry[1], [Security.AccessControl.AccessControlType]::Allow)
        }
        $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $item.FullName -AclObject $acl
}
$serverPath = Join-Path $installDir 'steam_sync_server.py'
$arguments = '"' + $serverPath + '" --steam "' + $SteamPath + '" --apps "' + $AppsPath + '"'
$action = New-ScheduledTaskAction -Execute $pythonWindowless -Argument $arguments
# Sign-in starts only the idle listener. There is no sync at sign-in or repetition.
$trigger = New-ScheduledTaskTrigger -AtLogOn
$principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -MultipleInstances IgnoreNew -ExecutionTimeLimit ([TimeSpan]::Zero) -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName 'Moonlight PS4 Steam Sync' -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Description 'Idle authenticated listener. Synchronizes Steam only when the updated PS4 client opens Moonlight.' -Force | Out-Null
$ruleName = 'Moonlight PS4 Steam Sync on open'
Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue | Remove-NetFirewallRule
New-NetFirewallRule -DisplayName $ruleName -Direction Inbound -Action Allow -Protocol TCP -LocalPort 47991 -RemoteAddress LocalSubnet -Program $pythonWindowless -Profile Any | Out-Null
Start-ScheduledTask -TaskName 'Moonlight PS4 Steam Sync'
Write-Output 'Installed: sync only on PS4 app open, authenticated by its Sunshine pairing certificate. Install the updated PS4 PKG; paired device must be named PS4.'
