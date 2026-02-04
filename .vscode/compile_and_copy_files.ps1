# receive params for script (must be first line in file, other than comments)
Param( $workspaceFolder, $launchJsonFile, $compileSettingsFile )

# show param inputs
Write-Host "workspaceFolder: '$workspaceFolder'" -ForegroundColor DarkGray
Write-Host "launchJsonFile: '$launchJsonFile'" -ForegroundColor DarkGray
Write-Host "compileSettingsFile: '$compileSettingsFile'" -ForegroundColor DarkGray

# validate param inputs
if( -not (Test-Path $workspaceFolder))
{
    Write-Host "Invalid workspaceFolder '$workspaceFolder'. Something went wrong." -ForegroundColor Red;
    exit;
}
if( -not (Test-Path $launchJsonFile))
{
    Write-Host "Invalid launchJsonFile '$launchJsonFile'. Something went wrong." -ForegroundColor Red;
    exit;
}
if( -not (Test-Path $compileSettingsFile))
{
    Write-Host "Invalid compileSettingsFile '$compileSettingsFile'. Something went wrong." -ForegroundColor Red;
    exit;
}

# inform user of relevant information
Write-Host ('Source Code directory: ' + "${workspaceFolder}".Replace('\\', '/')) -ForegroundColor White;

# grab game directory via regex
$regexPattern = '\"cwd\"\s*:\s*\"(.*?)\"';
Write-Host "regexPattern: '$regexPattern'" -ForegroundColor DarkGray;

$regexResult = (Get-Content -Raw "$launchJsonFile" | Select-String -Pattern $regexPattern);

if( -not $regexResult.Matches -or $regexResult.Matches.Length -le 0 -or -not $regexResult.Matches.Groups -or $regexResult.Matches.Groups.Length -le 1 )
{
    Write-Host "The current working directory `"cwd`" could not be found in '$launchJsonFile', please edit the file and update it." -ForegroundColor Red;
    Write-Host "Example: `"cwd`": `"D:/Games/DungeonKeeper/`" (be sure to use forward slashes)." -ForegroundColor Red;
    exit;
}

$gameDir = $regexResult.Matches[0].Groups[1].Value.Replace('\\', '/');
Write-Host "Found current working directory (cwd): '$gameDir' in '$launchJsonFile'" -ForegroundColor White;
if( -not (Test-Path $gameDir) )
{
    Write-Host "Directory '$gameDir' invalid, make sure it exists on-disk and the path is spelled correctly." -ForegroundColor Red;
    Write-Host "Example: `"cwd`": `"D:/Games/DungeonKeeper/`" (be sure to use forward slashes)." -ForegroundColor Red;
    exit;
}
else
{
    Write-Host "Directory '$gameDir' valid, exists on-disk" -ForegroundColor DarkGray;
}

$debugFlag      = 'DEBUG=0';
$debugFlagFTest = 'FTEST_DEBUG=0';

$compileSetting = (Get-Content "$compileSettingsFile" -Raw).Trim();
if ($compileSetting -like '*DEBUG=1*')
{
    $debugFlag = 'DEBUG=1';
}
if ($compileSetting -like 'FTEST_DEBUG=*')
{
    $debugFlagFTest = 'FTEST_DEBUG=1';
}

if ($debugFlag -eq 'DEBUG=1')
{
    Write-Host 'Compiling with DEBUG=1' -ForegroundColor Yellow;
}
else
{
    Write-Host 'Compiling with DEBUG=0' -ForegroundColor Green;
}

if ($debugFlagFTest -eq 'FTEST_DEBUG=1')
{
    Write-Host 'Compiling with FTEST_DEBUG=1' -ForegroundColor Magenta;
}
wsl make all -j`nproc` $debugFlag $debugFlagFTest;
if ($?) {
    Write-Host 'Compilation successful!' -ForegroundColor Green;
}
else
{
    Write-Host 'Compilation failed!' -ForegroundColor Red;
    exit 1;
}

# Copy compiled binary
Write-Host 'Copying binary files...' -ForegroundColor Cyan;
Copy-Item -Path "${workspaceFolder}\\bin\\*" -Destination $gameDir -Force;

# Copy only modified campaign/config/lang/level files using git to detect changes
Write-Host 'Detecting modified base game files...' -ForegroundColor Cyan;

# Get list of modified, added, and untracked files in relevant directories
$gitOutput = git -C $workspaceFolder status --porcelain campgns config lang levels 2>$null;

if ($gitOutput) {
    $modifiedFiles = @();
    foreach ($line in $gitOutput -split "`n") {
        # Match git status codes: M (modified), A (added), U (unmerged), ?? (untracked)
        if ($line -match '^\s*([MAU]|\?\?)\s+(.+)$') {
            $file = $matches[2].Trim();
            # Only include files from directories we care about (exclude .pot/.po source files)
            if ($file -match '^(campgns|config|levels)[\\/]' -or ($file -match '^lang[\\/]' -and $file -notmatch '\.(pot|po)$')) {
                $modifiedFiles += $file;
            }
        }
    }
    
    if ($modifiedFiles.Count -gt 0) {
        Write-Host "Found $($modifiedFiles.Count) modified file(s) to copy" -ForegroundColor Yellow;
        foreach ($file in $modifiedFiles) {
            $sourcePath = Join-Path $workspaceFolder $file;
            $destPath = Join-Path $gameDir $file;
            
            if (Test-Path $sourcePath) {
                # Ensure destination directory exists
                $destDir = Split-Path $destPath -Parent;
                if (-not (Test-Path $destDir)) {
                    New-Item -ItemType Directory -Path $destDir -Force | Out-Null;
                }
                Copy-Item -Path $sourcePath -Destination $destPath -Force;
                Write-Host "  Copied: $file" -ForegroundColor DarkGray;
            }
        }
    } else {
        Write-Host 'No modified base game files detected' -ForegroundColor DarkGray;
    }
} else {
    Write-Host 'Git not available or no repository - skipping incremental copy' -ForegroundColor DarkGray;
}

# Copy compiled pkg files (sprite atlases and language files)
Write-Host 'Copying compiled data files...' -ForegroundColor Cyan;
$pkgFiles = @(
    @{Source='pkg/data/gui2-32.dat'; Dest='data/gui2-32.dat'},
    @{Source='pkg/data/gui2-64.dat'; Dest='data/gui2-64.dat'},
    @{Source='pkg/fxdata/gtext_eng.dat'; Dest='fxdata/gtext_eng.dat'}
);
foreach ($pkgFile in $pkgFiles) {
    $sourcePath = Join-Path $workspaceFolder $pkgFile.Source;
    $destPath = Join-Path $gameDir $pkgFile.Dest;
    if (Test-Path $sourcePath) {
        $destDir = Split-Path $destPath -Parent;
        if (-not (Test-Path $destDir)) {
            New-Item -ItemType Directory -Path $destDir -Force | Out-Null;
        }
        Copy-Item -Path $sourcePath -Destination $destPath -Force;
        Write-Host "  Copied: $($pkgFile.Dest)" -ForegroundColor DarkGray;
    }
}

Write-Host 'All files copied successfully!' -ForegroundColor Green;
