param (
    [switch]$Clean,
    [switch]$Test,
    [switch]$Rebuild
)

$ErrorActionPreference = "Stop"

$ProjRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$SrcDir   = Join-Path $ProjRoot "src"
$ResDir   = Join-Path $ProjRoot "resources"
$TestDir  = Join-Path $ProjRoot "tests"
$ObjDir   = Join-Path $ProjRoot "obj"
$OutExe   = Join-Path $ProjRoot "alarmclock.exe"

$MSysRoot = "C:\msys64\ucrt64"
$GccPath  = Join-Path $MSysRoot "bin\gcc.exe"
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

$CFlags = @(
    "-O2", "-Wall", "-Wextra",
    "-DUNICODE", "-D_UNICODE",
    "-I$SrcDir", "-I$ResDir"
)

# Any header change invalidates every object - the dependency graph is small
# enough that tracking it per-file would cost more than it saves.
$NewestHeader = (Get-ChildItem -Path $SrcDir -Filter *.h |
                 Sort-Object LastWriteTimeUtc | Select-Object -Last 1).LastWriteTimeUtc

function Test-Stale {
    param([string]$Target, [string[]]$Sources)
    if ($Rebuild) { return $true }
    if (-not (Test-Path $Target)) { return $true }
    $targetTime = (Get-Item $Target).LastWriteTimeUtc
    foreach ($s in $Sources) {
        if (-not (Test-Path $s)) { return $true }
        if ((Get-Item $s).LastWriteTimeUtc -gt $targetTime) { return $true }
    }
    return $false
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

$ResObj = Join-Path $ObjDir "app_res.o"
$RcFile = Join-Path $ResDir "app.rc"
$ResDeps = @($RcFile, (Join-Path $SrcDir "resource.h"), $IconFile, $FontFile) |
           Where-Object { Test-Path $_ }

if (Test-Stale $ResObj $ResDeps) {
    Write-Host "Compiling resources..." -ForegroundColor Cyan
    & $WrPath @("-i", $RcFile, "-o", $ResObj, "-I$ResDir", "-I$SrcDir")
    if ($LASTEXITCODE -ne 0) { Write-Error "windres failed"; exit 1 }
} else {
    Write-Host "Resources up to date." -ForegroundColor DarkGray
}

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
    "sound.c",
    "audio.c"
)

Write-Host "Compiling source files..." -ForegroundColor Cyan

$Objs = @()
$Compiled = 0
foreach ($src in $Sources) {
    $obj     = Join-Path $ObjDir ($src -replace '\.c$', '.o')
    $srcFull = Join-Path $SrcDir $src
    $Objs += $obj

    $deps = @($srcFull)
    if ($NewestHeader) { $deps += (Get-ChildItem -Path $SrcDir -Filter *.h | ForEach-Object { $_.FullName }) }

    if (-not (Test-Stale $obj $deps)) {
        Write-Host "  $src (up to date)" -ForegroundColor DarkGray
        continue
    }

    Write-Host "  $src" -ForegroundColor Gray
    & $GccPath @("-c", $srcFull, "-o", $obj) @CFlags
    if ($LASTEXITCODE -ne 0) { Write-Error "Compilation failed: $src"; exit 1 }
    $Compiled++
}

$LinkObjs = $Objs + $ResObj
if ($Compiled -gt 0 -or (Test-Stale $OutExe $LinkObjs)) {
    Write-Host "Linking..." -ForegroundColor Cyan
    $ArgsLink = @("-o", $OutExe) + $LinkObjs + @(
        "-mwindows", "-O2",
        "-lcomctl32", "-lgdi32", "-lshell32", "-ldwmapi", "-lwinmm", "-luxtheme", "-lgdiplus",
        "-lmfplat", "-lmfreadwrite", "-lmfuuid", "-lole32", "-lavrt", "-lcomdlg32"
    )
    & $GccPath @ArgsLink
    if ($LASTEXITCODE -ne 0) { Write-Error "Linking failed"; exit 1 }
    Write-Host "Build successful: $OutExe" -ForegroundColor Green
} else {
    Write-Host "Up to date: $OutExe" -ForegroundColor Green
}

if ($Test) {
    # The settings load/save path and the alarm schedule are pure enough to
    # exercise without a window.
    Write-Host ""
    Write-Host "Building tests..." -ForegroundColor Cyan
    $TestExe = Join-Path $ObjDir "test_settings.exe"
    $TestSrc = @(
        (Join-Path $TestDir "test_settings.c"),
        (Join-Path $SrcDir  "json_utils.c"),
        (Join-Path $SrcDir  "settings_data.c"),
        (Join-Path $SrcDir  "alarms.c")
    )
    & $GccPath (@("-o", $TestExe) + $TestSrc + $CFlags)
    if ($LASTEXITCODE -ne 0) { Write-Error "Test build failed"; exit 1 }

    & $TestExe
    if ($LASTEXITCODE -ne 0) { Write-Error "Tests failed"; exit 1 }

    # The audio harness drives the real device at zero gain: it proves the
    # pipeline works end to end without making a sound.
    $AudioExe = Join-Path $ObjDir "test_audio.exe"
    & $GccPath (@("-o", $AudioExe,
                  (Join-Path $TestDir "test_audio.c"),
                  (Join-Path $SrcDir  "audio.c"),
                  (Join-Path $SrcDir  "sound.c")) + $CFlags +
                @("-lmfplat", "-lmfreadwrite", "-lmfuuid", "-lole32", "-lavrt"))
    if ($LASTEXITCODE -ne 0) { Write-Error "Audio test build failed"; exit 1 }

    & $AudioExe
    if ($LASTEXITCODE -ne 0) { Write-Error "Audio tests failed"; exit 1 }
}
