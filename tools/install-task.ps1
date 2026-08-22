<#
.SYNOPSIS
    Register the daily msh release push as a Windows scheduled task.

.DESCRIPTION
    Runs tools/daily-push.sh once a day through wsl.exe. wsl.exe starts the
    distribution if it is not already running, so this works with no WSL
    window open. -StartWhenAvailable means a day the machine was off is
    picked up at the next opportunity rather than skipped.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\install-task.ps1
    powershell -ExecutionPolicy Bypass -File tools\install-task.ps1 -At 21:30
    powershell -ExecutionPolicy Bypass -File tools\install-task.ps1 -Remove
#>
param(
    [string]$TaskName = "msh-daily-push",
    [string]$At       = "10:00",
    [string]$Distro   = "Ubuntu",
    [switch]$Remove
)

$ErrorActionPreference = "Stop"

if ($Remove) {
    if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
        Write-Host "Removed scheduled task '$TaskName'."
    } else {
        Write-Host "No scheduled task named '$TaskName'."
    }
    return
}

$script = 'bash ~/projects/msh/tools/daily-push.sh'
$argument = "-d $Distro -- bash -lc `"$script`""

$action = New-ScheduledTaskAction -Execute "wsl.exe" -Argument $argument

$trigger = New-ScheduledTaskTrigger -Daily -At $At

$settings = New-ScheduledTaskSettingsSet `
    -StartWhenAvailable `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit (New-TimeSpan -Minutes 10)

Register-ScheduledTask `
    -TaskName    $TaskName `
    -Action      $action `
    -Trigger     $trigger `
    -Settings    $settings `
    -Description "Publishes one prepared msh milestone commit per day." `
    -Force | Out-Null

Write-Host "Registered '$TaskName' to run daily at $At."
Write-Host ""
Write-Host "Check progress:  wsl.exe -d $Distro -- bash -lc 'bash ~/projects/msh/tools/daily-push.sh --status'"
Write-Host "Run it now:      Start-ScheduledTask -TaskName $TaskName"
Write-Host "Log:             wsl.exe -d $Distro -- bash -lc 'cat ~/.msh-release/push.log'"
Write-Host "Remove:          powershell -File tools\install-task.ps1 -Remove"
