param([switch]$Screenshots, [string]$OutputDirectory = 'out/package-validation')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path $PSScriptRoot -Parent
$executable = Join-Path $projectRoot 'dist/KantanLogoMaker/KantanLogoMaker.exe'
$cliExecutable = Join-Path $projectRoot 'dist/KantanLogoMaker/kantanlogo-cli.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Run package.ps1 first.' }
if (-not (Test-Path -LiteralPath $cliExecutable)) { throw 'Package is missing kantanlogo-cli.exe.' }
$cmakeLists = Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(\s*KantanLogoMaker\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
    throw 'Could not extract project version from CMakeLists.txt.'
}
$expectedVersion = $Matches[1]
$portableZip = Join-Path $projectRoot "dist/KantanLogoMaker-v$expectedVersion-Windows-Portable.zip"
$webglZip = Join-Path $projectRoot "dist/KantanLogoMaker-v$expectedVersion-WebGL.zip"
$webglLatestZip = Join-Path $projectRoot 'dist/KantanLogoMaker-WebGL.zip'
$webglIndex = Join-Path $projectRoot 'dist/KantanLogoMaker-WebGL/index.html'
$webglVersionJson = Join-Path $projectRoot 'dist/KantanLogoMaker-WebGL/version.json'
foreach ($artifact in @($portableZip, $webglZip, $webglLatestZip, $webglIndex, $webglVersionJson, (Join-Path $projectRoot 'dist/KantanLogoMaker-WebGL/favicon-32.png'))) {
    if (-not (Test-Path -LiteralPath $artifact)) { throw "Package is missing $artifact" }
}
$webglMeta = Get-Content -LiteralPath $webglVersionJson -Raw | ConvertFrom-Json
if ($webglMeta.version -ne $expectedVersion) { throw "WebGL version mismatch: expected $expectedVersion, got $($webglMeta.version)" }
$outputPath = [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
foreach ($library in @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll','platforms/qwindows.dll','app.ico','app.png')) {
    if (-not (Test-Path -LiteralPath (Join-Path (Split-Path $executable) $library))) { throw "Package is missing $library" }
}
$savedEnvironment = @{}
foreach ($key in @('PATH','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QT_QPA_PLATFORM','QT_SCALE_FACTOR','KANTANLOGO_DATA')) {
    $savedEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
}
$results = @()
try {
    # Verify deployment without inheriting a developer's Qt installation through PATH.
    $env:PATH = "$env:WINDIR/System32;$env:WINDIR"
    $env:QT_PLUGIN_PATH = ''
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = ''
    $env:QT_QPA_PLATFORM = 'windows'
    $env:KANTANLOGO_DATA = Join-Path $outputPath 'app-data'
    foreach ($scale in @('1','1.5','2')) {
        $env:QT_SCALE_FACTOR = $scale
        $readyPath = Join-Path $outputPath "scale-$scale.json"
        $imagePath = Join-Path $outputPath "scale-$scale.png"
        foreach ($previous in @($readyPath, $imagePath)) {
            if (Test-Path -LiteralPath $previous) { Remove-Item -LiteralPath $previous -Force }
        }
        $appArguments = @('--demo','--select','1','--quit-after','2000','--ready-file',('"' + $readyPath + '"'))
        if ($Screenshots) { $appArguments += @('--screenshot',('"' + $imagePath + '"')) }
        $process = Start-Process -FilePath $executable -ArgumentList $appArguments -WorkingDirectory (Split-Path $executable) -WindowStyle Hidden -PassThru
        if (-not $process.WaitForExit(20000)) {
            $process.Kill()
            throw "Packaged app timed out at scale $scale"
        }
        if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $readyPath)) { throw "Packaged app failed at scale $scale" }
        if ($Screenshots -and -not (Test-Path -LiteralPath $imagePath)) { throw "Screenshot missing at scale $scale" }
        $results += Get-Content -LiteralPath $readyPath -Raw | ConvertFrom-Json
    }
    $cliProject = Join-Path $outputPath 'cli-smoke.kantanlogo'
    $cliPreview = Join-Path $outputPath 'cli-smoke.png'
    $cliExportDir = Join-Path $outputPath 'cli-export'
    & $cliExecutable --cli new --template 0 --text 'KANTAN' -o $cliProject
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $cliProject)) { throw 'Packaged CLI failed on new.' }
    & $cliExecutable --cli render $cliProject -o $cliPreview --size 128x128 --padding 4
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $cliPreview)) { throw 'Packaged CLI failed on render.' }
    & $cliExecutable --cli export $cliProject -o $cliExportDir --name smoke --sizes 32,64 --ico | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $cliExportDir 'smoke.ico'))) { throw 'Packaged CLI failed on export.' }
    [ordered]@{
        executable_sha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant()
        checked_at_utc = [DateTime]::UtcNow.ToString('o')
        os = [Environment]::OSVersion.VersionString
        logical_processors = [Environment]::ProcessorCount
        runs = $results
        limitation = 'Local package smoke check; does not replace a clean-machine or physical mixed-monitor test.'
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputPath 'report.json') -Encoding UTF8
    Write-Output "Package passed at 100%, 150%, and 200%. Results: $outputPath"
} finally {
    foreach ($key in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($key, $savedEnvironment[$key], 'Process') }
}
