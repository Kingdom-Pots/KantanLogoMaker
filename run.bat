@echo off
rem Build and package with the same Qt installation selected by CMake.
setlocal
cd /d "%~dp0"
title Kantan LogoMaker
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
cmake --preset desktop > "out\run-build.log" 2>&1
if errorlevel 1 goto buildfailed
echo Building Kantan LogoMaker...
cmake --build --preset desktop --target kantan_app --parallel 1 -- /nr:false >> "out\run-build.log" 2>&1
if errorlevel 1 goto buildfailed
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package.ps1 >> "out\run-build.log" 2>&1
if errorlevel 1 goto buildfailed
start "" "%~dp0dist\KantanLogoMaker\KantanLogoMaker.exe" %*
exit /b 0
:buildfailed
echo The build or packaging failed. See out\run-build.log for details.
:failed
pause
exit /b 1
