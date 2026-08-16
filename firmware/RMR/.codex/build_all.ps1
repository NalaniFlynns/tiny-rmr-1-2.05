$ErrorActionPreference = 'Continue'
$enc = New-Object System.Text.UTF8Encoding($false)
$rootDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$cfg = Join-Path $rootDir 'app_config.h'
$debug = Join-Path $rootDir 'Debug'
$gmake = 'C:\ti\ccs2100\ccs\utils\bin\gmake.exe'

$orig = [System.IO.File]::ReadAllText($cfg, $enc)
function Set-Defines([string]$src, [int]$direct, [int]$save, [int]$dbg, [int]$dbl) {
    $t = [regex]::Replace($src, '#define POWER_SOURCE_DIRECT \d+', "#define POWER_SOURCE_DIRECT $direct")
    $t = [regex]::Replace($t, '#define POWER_SAVE_BUILD \d+', "#define POWER_SAVE_BUILD $save")
    $t = [regex]::Replace($t, '#define DEBUG_BUILD \d+', "#define DEBUG_BUILD $dbg")
    $t = [regex]::Replace($t, '#define DEBUG_LP_BUILD \d+', "#define DEBUG_LP_BUILD $dbl")
    return $t
}
function Build-Variant([string]$name, [int]$direct, [int]$save, [int]$dbg, [int]$dbl) {
    Write-Host "===== BUILD $name ====="
    [System.IO.File]::WriteAllText($cfg, (Set-Defines $orig $direct $save $dbg $dbl), $enc)
    Push-Location $debug
    & $gmake clean 2>&1 | Out-Null
    $out = & $gmake all 2>&1
    Pop-Location
    if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED $name"; $out | Select-Object -Last 25; exit 1 }
    Copy-Item "$debug\RMR.hex" "$debug\RMR_$name.hex" -Force
    Write-Host "OK: RMR_$name.hex"
}

Build-Variant 'DBG' 1 0 1 0
Build-Variant 'DBGL' 1 0 0 1
Build-Variant 'DIRECT' 1 0 0 0
Build-Variant 'BATT' 0 0 0 0
Build-Variant 'ECO_D' 1 1 0 0
Build-Variant 'ECO_B' 0 1 0 0

# restore original defaults and rebuild so Debug/ is consistent
[System.IO.File]::WriteAllText($cfg, $orig, $enc)
Write-Host "===== RESTORE + REBUILD (DBGL default) ====="
Push-Location $debug
& $gmake clean 2>&1 | Out-Null
$out = & $gmake all 2>&1
Pop-Location
if ($LASTEXITCODE -ne 0) { Write-Host "RESTORE BUILD FAILED"; $out | Select-Object -Last 25; exit 1 }
Copy-Item "$debug\RMR.hex" "$debug\RMR_DBGL.hex" -Force
Write-Host "ALL DONE"

