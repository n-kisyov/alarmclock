param (
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$ProjRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$SrcDir   = Join-Path $ProjRoot "src"
$ResDir   = Join-Path $ProjRoot "resources"
$ObjDir   = Join-Path $ProjRoot "obj"
$OutExe   = Join-Path $ProjRoot "alarmclock.exe"

$MSysRoot = "C:\msys64\ucrt64"
$GccPath  = Join-Path $MSysRoot "bin\gcc.exe"
$GppPath  = Join-Path $MSysRoot "bin\g++.exe"
$WrPath   = Join-Path $MSysRoot "bin\windres.exe"

if ($Clean) {
    Write-Host "Cleaning build artifacts..."
    if (Test-Path $ObjDir) { Remove-Item -Recurse -Force $ObjDir }
    if (Test-Path $OutExe) { Remove-Item -Force $OutExe }
    Write-Host "Clean complete."
    exit 0
}

if (-not (Test-Path $GccPath)) {
    Write-Error "gcc.exe not found at $GccPath"
    exit 1
}
if (-not (Test-Path $WrPath)) {
    Write-Error "windres.exe not found at $WrPath"
    exit 1
}

$env:PATH = (Join-Path $MSysRoot "bin") + ";" + $env:PATH

if (-not (Test-Path $ObjDir)) {
    New-Item -ItemType Directory -Path $ObjDir -Force | Out-Null
}

$IconFile = Join-Path $ResDir "alarmclock.ico"
if (-not (Test-Path $IconFile)) {
    Write-Host "Generating app icon..." -ForegroundColor Cyan
    $GenScript = Join-Path $ProjRoot "generate_icon.ps1"
    powershell -ExecutionPolicy Bypass -File $GenScript
}

$FontFile = Join-Path $ResDir "digital-7.ttf"
if (-not (Test-Path $FontFile)) {
    Write-Host "[WARN] digital-7.ttf not found -- app will use Consolas fallback" -ForegroundColor Yellow
}

Write-Host "Compiling resources..." -ForegroundColor Cyan
$ResObj = Join-Path $ObjDir "app_res.o"
$RcFile = Join-Path $ResDir "app.rc"
$ArgsWr = @("-i", $RcFile, "-o", $ResObj, "-I$ResDir", "-I$SrcDir")
& $WrPath @ArgsWr
if ($LASTEXITCODE -ne 0) { Write-Error "windres failed"; exit 1 }

Write-Host "Compiling source files..." -ForegroundColor Cyan

$Sources = @(
    "main.c",
    "main_window.c",
    "theme.c",
    "settings_dialog.c",
    "alarm_dialog.c",
    "alarms.c",
    "clock_renderer.c",
    "tray.c",
    "settings_data.c",
    "json_utils.c",
    "sound.c"
)

$Objs = @()
foreach ($src in $Sources) {
    $obj = Join-Path $ObjDir ($src -replace '\.c$', '.o')
    $srcFull = Join-Path $SrcDir $src
    Write-Host "  $src" -ForegroundColor Gray
    $ArgsGcc = @(
        "-c", $srcFull, "-o", $obj,
        "-mwindows", "-O2", "-Wall",
        "-DUNICODE", "-D_UNICODE",
        "-I$SrcDir", "-I$ResDir"
    )
    & $GccPath @ArgsGcc
    if ($LASTEXITCODE -ne 0) { Write-Error "Compilation failed: $src"; exit 1 }
    $Objs += $obj
}

Write-Host "Linking..." -ForegroundColor Cyan
$LinkObjs = $Objs + $ResObj
$ArgsLink = @(
    "-o", $OutExe
) + $LinkObjs + @(
    "-mwindows", "-O2",
    "-lcomctl32", "-lgdi32", "-lshell32", "-ldwmapi", "-lwinmm", "-luxtheme", "-lmsimg32", "-lgdiplus"
)
& $GccPath @ArgsLink
if ($LASTEXITCODE -ne 0) { Write-Error "Linking failed"; exit 1 }

Write-Host "Build successful: $OutExe" -ForegroundColor Green
