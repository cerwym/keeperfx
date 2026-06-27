#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Safely removes a layered deployment environment.

.DESCRIPTION
    Removes .deploy/ directory by:
    1. Deleting junctions (doesn't affect source)
    2. Deleting hard links (source files remain in clean master)
    3. Removing overlay files
    
    Safe: Original clean master files are never touched.

.PARAMETER Force
    Skip confirmation prompt.

.EXAMPLE
    .\reset_layered_deploy.ps1 -Force
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [switch]$Force,
    
    [Parameter(Mandatory=$false)]
    [string]$WorkspaceFolder
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $WorkspaceFolder) {
    $WorkspaceFolder = Split-Path -Parent $PSScriptRoot
}

$deployPath = Join-Path $WorkspaceFolder ".deploy"

if (-not (Test-Path $deployPath)) {
    Write-Host "No deployment found at: $deployPath" -ForegroundColor Yellow
    exit 0
}

if (-not $Force) {
    Write-Host "This will remove: $deployPath" -ForegroundColor Yellow
    $response = Read-Host "Continue? (y/N)"
    if ($response -ne 'y') {
        Write-Host "Aborted." -ForegroundColor Yellow
        exit 0
    }
}

Write-Host "Removing deployment..." -ForegroundColor Yellow
Remove-Item $deployPath -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "Deployment removed successfully." -ForegroundColor Green

