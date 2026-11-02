$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $cmakeLists = Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw
    if ($cmakeLists -notmatch 'project\(\s*KantanLogoMaker\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
        throw 'Could not extract project version from CMakeLists.txt.'
    }
    $version = $Matches[1]
    $producerCredit = 'Produced by Kingdom Pots using Claude Opus, GPT 6.1 Sol and Google Gemini 4'

    $cachePath = Join-Path $projectRoot 'out/desktop-msvc1938/CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath)) { throw 'Run build.ps1 first.' }
    $qtEntry = Select-String -LiteralPath $cachePath -Pattern '^Qt6_DIR:PATH=(.+)$'
    if (-not $qtEntry) { throw 'The desktop build has no configured Qt installation. Run build.ps1 first.' }
    $qtRoot = [IO.Path]::GetFullPath((Join-Path $qtEntry.Matches[0].Groups[1].Value '../../..'))
    if (-not (Test-Path -LiteralPath (Join-Path $qtRoot 'bin/windeployqt.exe'))) { throw 'The configured Qt deployment tool is missing.' }

    $destination = Join-Path $projectRoot 'dist/KantanLogoMaker'
    Get-Process -Name 'KantanLogoMaker','kantanlogo-cli' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
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
    Copy-Item -LiteralPath 'app/assets/app.ico' -Destination $destination
    Copy-Item -LiteralPath 'app/assets/app.png' -Destination $destination

    $desktopVersionInfo = [ordered]@{
        name = 'Kantan Logo Maker'
        version = $version
        platform = 'Windows-x64-Portable'
        producer = $producerCredit
        built_at_utc = [DateTime]::UtcNow.ToString('o')
    }
    $desktopVersionInfo | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $destination 'version.json') -Encoding UTF8

    $portableZip = Join-Path $projectRoot "dist/KantanLogoMaker-v$version-Windows-Portable.zip"
    if (Test-Path -LiteralPath $portableZip) { Remove-Item -LiteralPath $portableZip -Force }
    Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $portableZip -Force

    # Package WebGL distribution and create versioned WebGL zip archive.
    $webglDir = Join-Path $projectRoot 'dist/KantanLogoMaker-WebGL'
    if (Test-Path -LiteralPath $webglDir) { Remove-Item -LiteralPath $webglDir -Recurse -Force }
    New-Item -ItemType Directory -Path $webglDir -Force | Out-Null

    $webTemplatePath = Join-Path $projectRoot 'web/index.html'
    if (-not (Test-Path -LiteralPath $webTemplatePath)) { throw 'WebGL template web/index.html is missing.' }
    $webHtml = (Get-Content -LiteralPath $webTemplatePath -Raw).Replace('{{VERSION}}', $version)
    Set-Content -LiteralPath (Join-Path $webglDir 'index.html') -Value $webHtml -Encoding UTF8
    foreach ($webIcon in @('favicon-32.png', 'icon-192.png', 'icon-512.png')) {
        $srcIcon = Join-Path $projectRoot "web/$webIcon"
        if (Test-Path -LiteralPath $srcIcon) {
            Copy-Item -LiteralPath $srcIcon -Destination $webglDir
        }
    }

    $webglPresetsDir = Join-Path $webglDir 'presets'
    New-Item -ItemType Directory -Path $webglPresetsDir -Force | Out-Null
    if (Test-Path -LiteralPath (Join-Path $projectRoot 'app/presets')) {
        Copy-Item -Path (Join-Path $projectRoot 'app/presets/*.svg') -Destination $webglPresetsDir -Force
    }

    $webglNoticesDir = Join-Path $webglDir 'notices'
    New-Item -ItemType Directory -Path $webglNoticesDir -Force | Out-Null
    Copy-Item -LiteralPath '.deps/miniz-3.1.0/LICENSE' -Destination (Join-Path $webglNoticesDir 'Miniz-MIT.txt')
    Copy-Item -Path 'third_party/*.txt','third_party/NOTICE.md' -Destination $webglNoticesDir
    Copy-Item -LiteralPath 'third_party/clipper2/LICENSE' -Destination (Join-Path $webglNoticesDir 'Clipper2-BSL-1.0.txt')
    Copy-Item -LiteralPath 'third_party/nanosvg/LICENSE.txt' -Destination (Join-Path $webglNoticesDir 'nanosvg-zlib.txt')

    $webglVersionInfo = [ordered]@{
        name = 'Kantan Logo Maker'
        version = $version
        platform = 'WebGL'
        renderer = 'WebGL2 / WebGL1'
        producer = $producerCredit
        frameworks = [ordered]@{
            'Qt 6 (LGPL)' = 'Cross-platform desktop UI framework powering windows, menus, dialogs, high-DPI display scaling, and keyboard/mouse input.'
            'Clipper2 (BSL-1.0)' = 'Polygon clipping and offsetting geometry library used for boolean shape operations (union, subtract, intersect, xor) and path stroke/outline expansion.'
            'nanosvg (zlib)' = 'Lightweight SVG vector parser used to import external SVG paths and built-in symbol presets.'
            'Miniz (MIT)' = 'Fast lossless data compression library used for reading/writing .kantanlogo project archives and encoding PNG/ICO exports.'
            'Fluent System Icons (MIT)' = 'Crisp vector UI icon set used across the toolbar, layer panel, and menus.'
        }
        built_at_utc = [DateTime]::UtcNow.ToString('o')
    }
    $webglVersionInfo | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $webglDir 'version.json') -Encoding UTF8

    $webglVersionedZip = Join-Path $projectRoot "dist/KantanLogoMaker-v$version-WebGL.zip"
    $webglLatestZip = Join-Path $projectRoot 'dist/KantanLogoMaker-WebGL.zip'
    foreach ($zipPath in @($webglVersionedZip, $webglLatestZip)) {
        if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    }
    Compress-Archive -Path (Join-Path $webglDir '*') -DestinationPath $webglVersionedZip -Force
    Copy-Item -LiteralPath $webglVersionedZip -Destination $webglLatestZip -Force

    Write-Output ('Standalone portable .exe: ' + (Join-Path $destination 'KantanLogoMaker.exe') + " (v$version)")
    Write-Output ('Standalone portable .zip: ' + $portableZip)
    Write-Output ('WebGL zipped build:       ' + $webglVersionedZip)
} finally { Pop-Location }
