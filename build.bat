@echo off
rem Automatically build the standalone portable .exe distribution and the zipped WebGL build with version info.
setlocal
cd /d "%~dp0"
title Kantan Logo Maker Release Builder
where cmake >nul 2>nul
if errorlevel 1 (
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`) do set "PATH=%%i;%PATH%"
)
where cmake >nul 2>nul
if errorlevel 1 (
  echo Could not find CMake. Install Visual Studio 2022 with Desktop development with C++.
  goto failed
)
if not exist out mkdir out
echo Configuring Kantan Logo Maker...
cmake --preset desktop > "out\build-release.log" 2>&1
if errorlevel 1 goto buildfailed
echo Building Kantan Logo Maker desktop targets...
cmake --build --preset desktop --parallel 1 -- /nr:false >> "out\build-release.log" 2>&1
if errorlevel 1 goto buildfailed
echo Packaging standalone portable .exe and zipped WebGL build...
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package.ps1
if errorlevel 1 goto buildfailed
echo.
echo Build and packaging completed successfully!
exit /b 0
:buildfailed
echo The build or packaging failed. See out\build-release.log for details.
:failed
exit /b 1
