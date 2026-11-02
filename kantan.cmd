@echo off
setlocal
set "ROOT=%~dp0"
if "%~1"=="" goto usage
set "SUB=%~1"
shift
set "REST="
:collect
if "%~1"=="" goto dispatch
set "REST=%REST% "%~1""
shift
goto collect

:dispatch
if /I "%SUB%"=="build" goto do_build
if /I "%SUB%"=="test" goto do_test
if /I "%SUB%"=="package" goto do_package
if /I "%SUB%"=="validate" goto do_validate
if /I "%SUB%"=="bench" goto do_bench
if /I "%SUB%"=="format" goto do_format
if /I "%SUB%"=="ship" goto do_ship
if /I "%SUB%"=="run" goto do_run

set "EXE=%ROOT%dist\KantanLogoMaker\kantanlogo-cli.exe"
if not exist "%EXE%" set "EXE=%ROOT%out\desktop-msvc1938\Release\kantanlogo-cli.exe"
if not exist "%EXE%" set "EXE=%ROOT%dist\KantanLogoMaker\KantanLogoMaker.exe"
if not exist "%EXE%" set "EXE=%ROOT%out\desktop-msvc1938\Release\KantanLogoMaker.exe"
if not exist "%EXE%" (
  echo KantanLogoMaker binary not found. Run: kantan.cmd build
  exit /b 1
)
"%EXE%" --cli "%SUB%" %REST%
exit /b %ERRORLEVEL%

:do_build
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\build.ps1" %REST%
exit /b %ERRORLEVEL%

:do_test
ctest --preset desktop %REST%
exit /b %ERRORLEVEL%

:do_package
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\package.ps1"
exit /b %ERRORLEVEL%

:do_validate
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\validate-package.ps1" %REST%
exit /b %ERRORLEVEL%

:do_bench
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\benchmark.ps1" %REST%
exit /b %ERRORLEVEL%

:do_format
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\format.ps1" %REST%
exit /b %ERRORLEVEL%

:do_ship
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\build.ps1" && powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\package.ps1" && powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\validate-package.ps1" -Screenshots
exit /b %ERRORLEVEL%

:do_run
call "%ROOT%run.bat" %REST%
exit /b %ERRORLEVEL%

:usage
echo Kantan LogoMaker CLI Bridge
echo.
echo Dev / production pipeline:
echo   kantan build [-CoreOnly] [-Sanitize]   Configure, build and run contract tests
echo   kantan test                            Run CTest suites
echo   kantan package                         Deploy standalone Windows folder to dist\KantanLogoMaker
echo   kantan validate [-Screenshots]         Verify packaged app at 100%%, 150%% and 200%% DPI
echo   kantan bench                           Run render benchmarks
echo   kantan format                          Format C++ sources with clang-format
echo   kantan ship                            Full release gate: build + test + package + validate
echo   kantan run                             Build, package and launch GUI
echo.
echo Headless asset production:
echo   kantan presets                         List templates, export presets, styles and shapes (JSON)
echo   kantan inspect ^<file.kantanlogo^>       Inspect document structure and fonts (JSON)
echo   kantan new --template ^<i^|name^> -o ^<out.kantanlogo^> [--text "Brand"]
echo   kantan render ^<in.kantanlogo^|template:0^> -o ^<out.png^> [--size WxH] [--padding P] [--bg #RRGGBB^|transparent]
echo   kantan export ^<in.kantanlogo^|template:0^> -o ^<folder^> [--name N] [--preset P] [--sizes 16,32,64,256] [--ico]
exit /b 0
