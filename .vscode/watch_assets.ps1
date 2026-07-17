#!/usr/bin/env pwsh
# KeeperFX Asset File Watcher
# Automatically compiles assets when PNG, WAV, or PO files change
# 
# Usage: .\.vscode\watch_assets.ps1
# Stop: Press Ctrl+C

param(
    [string]$WorkspaceRoot = $PWD,
    [int]$DebounceMs = 500,
    [switch]$AutoDeploy
)

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "=" * 80 -ForegroundColor Cyan
Write-Host "🔍 KeeperFX Asset File Watcher" -ForegroundColor Cyan
Write-Host "=" * 80 -ForegroundColor Cyan
Write-Host ""
Write-Host "Workspace: $WorkspaceRoot" -ForegroundColor Yellow
Write-Host "Debounce: ${DebounceMs}ms" -ForegroundColor Yellow
Write-Host "Auto-deploy: $(if ($AutoDeploy) { 'Enabled' } else { 'Disabled' })" -ForegroundColor Yellow
Write-Host ""
Write-Host "Watching directories:" -ForegroundColor Green
Write-Host "  📁 gfx/ (*.png)" -ForegroundColor White
Write-Host "  📁 sfx/ (*.wav)" -ForegroundColor White
Write-Host "  📁 lang/ (*.po)" -ForegroundColor White
Write-Host ""
Write-Host "Press Ctrl+C to stop" -ForegroundColor Yellow
Write-Host ""

# Check required directories exist
$requiredDirs = @("gfx", "sfx", "lang")
foreach ($dir in $requiredDirs) {
    $path = Join-Path $WorkspaceRoot $dir
    if (-not (Test-Path $path)) {
        Write-Host "⚠️  Warning: $dir/ directory not found" -ForegroundColor Yellow
    }
}

# File system watchers
$watchers = @()

# Graphics watcher (gfx/**/*.png)
if (Test-Path (Join-Path $WorkspaceRoot "gfx")) {
    $gfxWatcher = New-Object System.IO.FileSystemWatcher
    $gfxWatcher.Path = Join-Path $WorkspaceRoot "gfx"
    $gfxWatcher.Filter = "*.png"
    $gfxWatcher.IncludeSubdirectories = $true
    $gfxWatcher.EnableRaisingEvents = $true
    $watchers += $gfxWatcher
}

# Sounds watcher (sfx/**/*.wav)
if (Test-Path (Join-Path $WorkspaceRoot "sfx")) {
    $sfxWatcher = New-Object System.IO.FileSystemWatcher
    $sfxWatcher.Path = Join-Path $WorkspaceRoot "sfx"
    $sfxWatcher.Filter = "*.wav"
    $sfxWatcher.IncludeSubdirectories = $true
    $sfxWatcher.EnableRaisingEvents = $true
    $watchers += $sfxWatcher
}

# Localization watcher (lang/**/*.po)
if (Test-Path (Join-Path $WorkspaceRoot "lang")) {
    $langWatcher = New-Object System.IO.FileSystemWatcher
    $langWatcher.Path = Join-Path $WorkspaceRoot "lang"
    $langWatcher.Filter = "*.po"
    $langWatcher.IncludeSubdirectories = $true
    $langWatcher.EnableRaisingEvents = $true
    $watchers += $langWatcher
}

# Debounce timer
$timer = New-Object System.Timers.Timer
$timer.Interval = $DebounceMs
$timer.AutoReset = $false

# Pending compilation tasks
$script:pendingTasks = @{
    graphics = $false
    sounds = $false
    localization = $false
}

# Timer elapsed handler (executes builds)
$timerAction = {
    try {
        if ($script:pendingTasks.graphics) {
            Write-Host "[$(Get-Date -Format 'HH:mm:ss')] 🎨 Compiling graphics..." -ForegroundColor Magenta
            $startTime = Get-Date
            
            Push-Location $WorkspaceRoot
            docker compose -f "$WorkspaceRoot\build\docker\compose.yml" run --rm linux bash -c "make pkg-gfx"
            Pop-Location
            
            $duration = [math]::Round(((Get-Date) - $startTime).TotalSeconds, 2)
            Write-Host "[$(Get-Date -Format 'HH:mm:ss')] ✅ Graphics compiled (${duration}s)" -ForegroundColor Green
            $script:pendingTasks.graphics = $false
            
            if ($AutoDeploy) {
                Write-Host "[$(Get-Date -Format 'HH:mm:ss')] 📦 Auto-deploying graphics..." -ForegroundColor Cyan
                & "$WorkspaceRoot\.vscode\deploy_assets.ps1" -DeployGraphics
            }
        }
        
        if ($script:pendingTasks.sounds) {
            Write-Host "[$(Get-Date -Format 'HH:mm:ss')] 🔊 Compiling sounds..." -ForegroundColor Magenta
            $startTime = Get-Date
            
            Push-Location $WorkspaceRoot
            docker compose -f "$WorkspaceRoot\build\docker\compose.yml" run --rm linux bash -c "make pkg-sfx"
            Pop-Location
            
            $duration = [math]::Round(((Get-Date) - $startTime).TotalSeconds, 2)
            Write-Host "[$(Get-Date -Format 'HH:mm:ss')] ✅ Sounds compiled (${duration}s)" -ForegroundColor Green
            $script:pendingTasks.sounds = $false
            
            if ($AutoDeploy) {
                Write-Host "[$(Get-Date -Format 'HH:mm:ss')] 📦 Auto-deploying sounds..." -ForegroundColor Cyan
                & "$WorkspaceRoot\.vscode\deploy_assets.ps1" -DeploySounds
            }
        }
        
        if ($script:pendingTasks.localization) {
            Write-Host "[$(Get-Date -Format 'HH:mm:ss')] 🌍 Compiling localization..." -ForegroundColor Magenta
            $startTime = Get-Date
            
            Push-Location $WorkspaceRoot
            docker compose -f "$WorkspaceRoot\build\docker\compose.yml" run --rm linux bash -c "make pkg-lang"
            Pop-Location
            
            $duration = [math]::Round(((Get-Date) - $startTime).TotalSeconds, 2)
            Write-Host "[$(Get-Date -Format 'HH:mm:ss')] ✅ Localization compiled (${duration}s)" -ForegroundColor Green
            $script:pendingTasks.localization = $false
        }
        
    } catch {
        Write-Host "[$(Get-Date -Format 'HH:mm:ss')] ❌ Compilation error: $($_.Exception.Message)" -ForegroundColor Red
    }
    
    Write-Host ""
}

Register-ObjectEvent -InputObject $timer -EventName Elapsed -Action $timerAction | Out-Null

# Graphics change handler
$gfxAction = {
    $name = $Event.SourceEventArgs.Name
    $changeType = $Event.SourceEventArgs.ChangeType
    
    Write-Host "[$(Get-Date -Format 'HH:mm:ss')] 🖼️  Graphics changed: $name ($changeType)" -ForegroundColor Cyan
    $script:pendingTasks.graphics = $true
    $timer.Stop()
    $timer.Start()
}

# Sounds change handler
$sfxAction = {
    $name = $Event.SourceEventArgs.Name
    $changeType = $Event.SourceEventArgs.ChangeType
    
    Write-Host "[$(Get-Date -Format 'HH:mm:ss')] 🎵 Sound changed: $name ($changeType)" -ForegroundColor Cyan
    $script:pendingTasks.sounds = $true
    $timer.Stop()
    $timer.Start()
}

# Localization change handler
$langAction = {
    $name = $Event.SourceEventArgs.Name
    $changeType = $Event.SourceEventArgs.ChangeType
    
    Write-Host "[$(Get-Date -Format 'HH:mm:ss')] 📝 Localization changed: $name ($changeType)" -ForegroundColor Cyan
    $script:pendingTasks.localization = $true
    $timer.Stop()
    $timer.Start()
}

# Register event handlers
if ($gfxWatcher) {
    Register-ObjectEvent -InputObject $gfxWatcher -EventName Changed -Action $gfxAction | Out-Null
    Register-ObjectEvent -InputObject $gfxWatcher -EventName Created -Action $gfxAction | Out-Null
}

if ($sfxWatcher) {
    Register-ObjectEvent -InputObject $sfxWatcher -EventName Changed -Action $sfxAction | Out-Null
    Register-ObjectEvent -InputObject $sfxWatcher -EventName Created -Action $sfxAction | Out-Null
}

if ($langWatcher) {
    Register-ObjectEvent -InputObject $langWatcher -EventName Changed -Action $langAction | Out-Null
    Register-ObjectEvent -InputObject $langWatcher -EventName Created -Action $langAction | Out-Null
}

# Keep script running
try {
    Write-Host "✓ Watching for changes..." -ForegroundColor Green
    Write-Host ""
    
    while ($true) {
        Start-Sleep -Seconds 1
    }
}
finally {
    # Cleanup
    Write-Host ""
    Write-Host "🛑 Stopping file watcher..." -ForegroundColor Yellow
    
    foreach ($watcher in $watchers) {
        $watcher.EnableRaisingEvents = $false
        $watcher.Dispose()
    }
    
    $timer.Dispose()
    
    Get-EventSubscriber | Unregister-Event
    
    Write-Host "✓ Watcher stopped" -ForegroundColor Red
    Write-Host ""
}
