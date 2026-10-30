$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$appPath = Join-Path $projectRoot 'dist/KantanLogoMaker/KantanLogoMaker.exe'
if (-not (Test-Path $appPath)) { throw 'Run build.ps1 and package.ps1 first.' }
Start-Process -FilePath $appPath -WorkingDirectory $projectRoot -WindowStyle Hidden
