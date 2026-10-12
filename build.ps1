# Configure and build FalloutBabylon with the Visual Studio toolchain and a local Qt 6.
# Usage: .\build.ps1 [-Config Release|Debug] [-Qt D:\Qt\6.8.3\msvc2022_64] [-NoApp] [-Test] [-Deploy]
#   -NoApp   core library and unit tests only (no GUI, no Scintilla)
#   -Test    run the unit tests after building
#   -Deploy  copy the executable and the Qt runtime it needs into dist\FalloutBabylon (windeployqt)
param(
    [string]$Config = "Release",
    [string]$Qt = "D:\Qt\6.8.3\msvc2022_64",
    [switch]$NoApp,
    [switch]$Test,
    [switch]$Deploy
)
$ErrorActionPreference = "Stop"
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { $vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" }
$build = Join-Path $PSScriptRoot "build"
$app = if ($NoApp) { "OFF" } else { "ON" }
$cmd = "`"$vcvars`" >nul && cmake -S `"$PSScriptRoot`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=$Config -DCMAKE_PREFIX_PATH=`"$Qt`" -DBABYLON_BUILD_APP=$app && cmake --build `"$build`""
if ($Test) { $cmd += " && set PATH=$Qt\bin;%PATH% && `"$build\babylon_tests.exe`"" }
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Deploy -and -not $NoApp) {
    $dist = Join-Path $PSScriptRoot "dist\FalloutBabylon"
    New-Item -ItemType Directory -Force $dist | Out-Null
    Copy-Item (Join-Path $build "FalloutBabylon.exe") $dist -Force
    & "$Qt\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $dist "FalloutBabylon.exe")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host "Deployed to $dist"
}
