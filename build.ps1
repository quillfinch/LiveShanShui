# LivePaper build script -- native C++ using only Windows SDK components.
# Toolchain: MSYS2 UCRT64 g++ (no CMake, no external packages, no runtime deps).
[CmdletBinding()]
param(
    [string]$Config = 'Release',
    [switch]$Clean,
    [switch]$Run,
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$src  = Join-Path $root 'src'
$out  = Join-Path $root 'build'
$GXX  = 'C:\Program Files (x86)\MSYS2\ucrt64\bin\g++.exe'
$WINDRES = 'C:\Program Files (x86)\MSYS2\ucrt64\bin\windres.exe'

if (-not (Test-Path $GXX)) { throw "g++ not found at $GXX" }
# g++ needs its own bin dir on PATH to locate cc1plus and the mingw runtime.
$env:Path = (Split-Path $GXX) + ';' + $env:Path

if ($Clean -and (Test-Path $out)) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $out | Out-Null

$optFlags = switch ($Config) {
    'Debug'   { @('-O0', '-g', '-DLIVEPAPER_DEBUG') }
    default   { @('-O2', '-ffunction-sections', '-fdata-sections', '-DLIVEPAPER_RELEASE') }
}

# Everything is static-linked against the UCRT already present on Windows 10+,
# so the result is a single self-contained exe with no redistributables.
$common = @(
    '-std=c++20', '-municode', '-mwindows',
    '-DUNICODE', '-D_UNICODE', '-DWIN32_LEAN_AND_MEAN',
    '-D_WIN32_WINNT=0x0A00', '-DNOMINMAX', '-D_CRT_SECURE_NO_WARNINGS'
) + $optFlags + @(
    '-fno-exceptions', '-fno-rtti',
    '-Wall', '-Wextra',
    '-Wno-unused-parameter', '-Wno-cast-function-type',
    # The ABI mirror interfaces intentionally redeclare overloads with different
    # by-value parameter types; that is the whole point of the file.
    '-Wno-overloaded-virtual', '-Wno-unused-function'
)

# Windows system libraries only -- D3D11/DXGI/D2D1/DWrite/DComp/WIC/Shell/GDI.
$libs = @(
    '-ld2d1', '-ld3d11', '-ldxgi', '-ldwrite', '-ldcomp',
    '-lwindowscodecs', '-lole32', '-loleaut32', '-luuid', '-lshlwapi',
    '-lshell32', '-luser32', '-lgdi32', '-ladvapi32', '-lcomctl32',
    '-lpropsys', '-lpowrprof', '-lversion', '-ldwmapi', '-lmsimg32',
    '-lbcrypt', '-lcfgmgr32', '-lsetupapi'
)

# Resource + manifest (DPI awareness, icon, version info).
$resDir = Join-Path $root 'res'
$resFile = Join-Path $out 'livepaper.res'
if (Test-Path $WINDRES) {
    $prev = Get-Location
    Set-Location $resDir
    & $WINDRES 'liveshanshui.rc' -O coff -o $resFile 2>&1 | Out-Null
    $rc = $LASTEXITCODE
    Set-Location $prev
    if ($rc -ne 0 -or -not (Test-Path $resFile)) {
        Write-Warning "windres failed (exit $rc); building without resources"
        $resFile = $null
    }
} else {
    Write-Warning "windres not found; building without manifest/icon resources"
    $resFile = $null
}

$sources = Get-ChildItem -Path $src -Recurse -Filter *.cpp | ForEach-Object { $_.FullName }
if ($sources.Count -eq 0) { throw "no sources found under $src" }

$objects = @()
foreach ($s in $sources) {
    $rel = $s.Substring($src.Length + 1) -replace '[\\/]', '_'
    $obj = Join-Path $out ($rel -replace '\.cpp$', '.o')
    Write-Host "  CC  $($s.Substring($root.Length + 1))" -ForegroundColor DarkGray
    & $GXX @common -c $s -o $obj
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $s" }
    $objects += $obj
}

$exe = Join-Path $out 'LiveShanShui.exe'
# A running engine holds its own image open, which makes the link fail with an opaque
# "ld returned 1 exit status". Stop it first.
$running = Get-Process -Name 'LiveShanShui' -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "  --  stopping running LivePaper ($($running.Id -join ', '))" -ForegroundColor DarkYellow
    $running | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 400
}
Write-Host "  LD  build/LiveShanShui.exe" -ForegroundColor DarkGray
$linkInputs = $objects + @($resFile) | Where-Object { $_ }
& $GXX @common $linkInputs -o $exe @libs '-Wl,--gc-sections' '-static-libgcc' '-static-libstdc++' '-s'
if ($LASTEXITCODE -ne 0) { throw "link failed" }

$info = Get-Item $exe
Write-Host ("`nBuilt {0} ({1:N0} bytes)" -f $info.Name, $info.Length) -ForegroundColor Green

if ($Test) {
    Write-Host "`nRunning self-test..." -ForegroundColor Cyan
    & $exe --selftest
    exit $LASTEXITCODE
}
if ($Run) {
    Write-Host "`nLaunching..." -ForegroundColor Cyan
    & $exe @args
}
