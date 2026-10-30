param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    & $Python -c 'import sys; assert sys.version_info[:2] == (3,12), "Dependency installer requires Python 3.12"'
    if ($LASTEXITCODE -ne 0) { throw 'Install Python 3.12 or pass its executable with -Python.' }
    $dependencies = Get-Content dependencies.json -Raw | ConvertFrom-Json
    New-Item -ItemType Directory -Path .tools/aqt,.deps/qt -Force | Out-Null
    & $Python -m pip install --disable-pip-version-check --target .tools/aqt --upgrade -r scripts/installer-requirements.lock
    if ($LASTEXITCODE -ne 0) { throw 'Dependency installer setup failed.' }
    $env:PYTHONPATH = Join-Path $projectRoot '.tools/aqt'
    $qtDirectory = Join-Path $projectRoot ('.deps/qt/' + $dependencies.qt.version + '/msvc2022_64')
    if (-not (Test-Path (Join-Path $qtDirectory 'lib/cmake/Qt6/Qt6Config.cmake'))) {
        & $Python -m aqt install-qt windows desktop $dependencies.qt.version $dependencies.qt.architecture --archives qtbase -O .deps/qt
        if ($LASTEXITCODE -ne 0) { throw 'Qt acquisition failed.' }
    }
    $archivePath = Join-Path $projectRoot ('.deps/miniz-' + $dependencies.miniz.version + '.tar.gz')
    if (-not (Test-Path $archivePath)) {
        Invoke-WebRequest -Uri $dependencies.miniz.url -OutFile $archivePath
    }
    if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $dependencies.miniz.sha256) { throw 'Miniz archive checksum mismatch.' }
    if (-not (Test-Path (Join-Path $projectRoot '.deps/miniz-3.1.0/miniz.c'))) {
        tar -xzf $archivePath -C .deps
        if ($LASTEXITCODE -ne 0) { throw 'Miniz extraction failed.' }
    }
    Write-Output 'Pinned dependencies are ready.'
} finally { Pop-Location }
