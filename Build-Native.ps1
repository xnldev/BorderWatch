param([string]$OutputName='BorderWatch-native.exe')
$ErrorActionPreference = 'Stop'

$workspace = $PSScriptRoot
$cppDir = Join-Path $workspace 'cpp'
$distDir = Join-Path $workspace 'dist'
$objDir = Join-Path $distDir 'obj'
$null = New-Item -Path $distDir -ItemType Directory -Force
$null = New-Item -Path $objDir -ItemType Directory -Force

# Stop running BorderWatch process if it locks the target executable
$procBase = [System.IO.Path]::GetFileNameWithoutExtension($OutputName)
Get-Process -Name $procBase -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

# Locate vcvars64.bat
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    throw "Visual Studio 2022 BuildTools vcvars64.bat not found at $vcvars"
}

Write-Host "Compiling native resources..." -ForegroundColor Cyan
$rcCmd = "`"$vcvars`" && cd `"$cppDir\res`" && rc.exe /fo `"$distDir\BorderWatch.res`" BorderWatch.rc"
cmd.exe /c $rcCmd
if ($LASTEXITCODE -ne 0) { throw "Resource compilation failed" }

Write-Host "Compiling native C++ application..." -ForegroundColor Cyan
$sources = @(
    "`"$cppDir\third_party\yyjson.c`"",
    "`"$cppDir\src\Config.cpp`"",
    "`"$cppDir\src\Watchlist.cpp`"",
    "`"$cppDir\src\AudioPlayer.cpp`"",
    "`"$cppDir\src\LogScanner.cpp`"",
    "`"$cppDir\src\ToastManager.cpp`"",
    "`"$cppDir\src\DetectedWindow.cpp`"",
    "`"$cppDir\src\WatchlistWindow.cpp`"",
    "`"$cppDir\src\StartupManager.cpp`"",
    "`"$cppDir\src\HotkeyManager.cpp`"",
    "`"$cppDir\src\TrayIcon.cpp`"",
    "`"$cppDir\src\ModernControls.cpp`"",
    "`"$cppDir\src\MainWindow.cpp`"",
    "`"$cppDir\src\Main.cpp`""
) -join " "

$libs = "user32.lib gdi32.lib shell32.lib ole32.lib comctl32.lib comdlg32.lib winhttp.lib gdiplus.lib advapi32.lib uxtheme.lib dwmapi.lib"
$outFile = "`"$distDir\$OutputName`""
$resFile = "`"$distDir\BorderWatch.res`""

$clCmd = "`"$vcvars`" && cd `"$objDir`" && cl.exe /nologo /O2 /W3 /std:c++17 /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /I`"$cppDir\include`" /I`"$cppDir\third_party`" $sources $resFile /Fe$outFile /link /SUBSYSTEM:WINDOWS $libs"

cmd.exe /c $clCmd
if ($LASTEXITCODE -ne 0) { throw "C++ compilation/link failed" }

$exe = Get-Item -LiteralPath (Join-Path $distDir $OutputName)
Write-Host "Build successful: $($exe.FullName)" -ForegroundColor Green
Write-Host "Binary size: $([math]::Round($exe.Length / 1KB, 2)) KB" -ForegroundColor Green
