$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $cachePath = Join-Path $projectRoot 'out/desktop-msvc1938/CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath)) { throw 'Run build.ps1 first.' }
    $qtEntry = Select-String -LiteralPath $cachePath -Pattern '^Qt6_DIR:PATH=(.+)$'
    if (-not $qtEntry) { throw 'The desktop build has no configured Qt installation. Run build.ps1 first.' }
    $qtRoot = [IO.Path]::GetFullPath((Join-Path $qtEntry.Matches[0].Groups[1].Value '../../..'))
    if (-not (Test-Path -LiteralPath (Join-Path $qtRoot 'bin/windeployqt.exe'))) { throw 'The configured Qt deployment tool is missing.' }
    $destination = Join-Path $projectRoot 'dist/KantanLogoMaker'
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    cmake --install out/desktop-msvc1938 --config Release --prefix $destination
    if ($LASTEXITCODE -ne 0) { throw 'App installation failed.' }
    & (Join-Path $qtRoot 'bin/windeployqt.exe') --release --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler --no-compiler-runtime --no-network --skip-plugin-types generic,networkinformation,tls (Join-Path $destination 'KantanLogoMaker.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
    $noticeDestination = Join-Path $destination 'notices'
    New-Item -ItemType Directory -Path $noticeDestination -Force | Out-Null
    Copy-Item -LiteralPath '.deps/miniz-3.1.0/LICENSE' -Destination (Join-Path $noticeDestination 'Miniz-MIT.txt')
    Copy-Item -Path 'third_party/*.txt','third_party/NOTICE.md' -Destination $noticeDestination
    Copy-Item -LiteralPath 'third_party/clipper2/LICENSE' -Destination (Join-Path $noticeDestination 'Clipper2-BSL-1.0.txt')
    Copy-Item -LiteralPath 'third_party/nanosvg/LICENSE.txt' -Destination (Join-Path $noticeDestination 'nanosvg-zlib.txt')
    $qtNotices = Join-Path $noticeDestination 'qt-sbom'
    New-Item -ItemType Directory -Path $qtNotices -Force | Out-Null
    Copy-Item -Path (Join-Path $qtRoot 'sbom/*') -Destination $qtNotices -Recurse -Force
    # Remove only obsolete, optional deployment items from earlier local package runs.
    $packagePrefix = [IO.Path]::GetFullPath($destination) + [IO.Path]::DirectorySeparatorChar
    foreach ($name in @('generic','networkinformation','tls','Qt6Network.dll')) {
        $obsolete = [IO.Path]::GetFullPath((Join-Path $destination $name))
        if (-not $obsolete.StartsWith($packagePrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid package cleanup path.' }
        if (Test-Path -LiteralPath $obsolete) { Remove-Item -LiteralPath $obsolete -Recurse -Force }
    }
    Copy-Item -LiteralPath dependencies.json -Destination $destination
    Copy-Item -LiteralPath docs/USER_GUIDE.md -Destination (Join-Path $destination 'Readme.md')
    Write-Output ('Local test build: ' + (Join-Path $destination 'KantanLogoMaker.exe'))
} finally { Pop-Location }
