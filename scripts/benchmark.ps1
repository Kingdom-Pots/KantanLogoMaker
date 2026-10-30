param([string]$OutputDirectory = 'out/measurements',[switch]$DesktopOnly)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path $PSScriptRoot -Parent
$process = $null
Push-Location $projectRoot
try {
    $outputPath = [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
    New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
    $desktop = Join-Path $projectRoot 'dist/KantanLogoMaker/KantanLogoMaker.exe'
    if (-not (Test-Path -LiteralPath $desktop)) { throw 'Package the local build first.' }
    if (-not $DesktopOnly) {
        $benchmark = Join-Path $projectRoot 'out/desktop-msvc1938/Release/kantan_bench.exe'
        for ($run = 1; $run -le 3; $run++) {
            & $benchmark | Set-Content -LiteralPath (Join-Path $outputPath ('render-' + $run + '.csv')) -Encoding UTF8
            if ($LASTEXITCODE -ne 0) { throw 'Render benchmark failed.' }
        }
    }
    $token = [Guid]::NewGuid().ToString('N')
    $readyPath = Join-Path $outputPath ($token + '-ready.json')
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $desktop -ArgumentList @('--demo','--quit-after','40000','--ready-file',('"' + $readyPath + '"')) -WindowStyle Hidden -PassThru
    while (-not (Test-Path -LiteralPath $readyPath)) {
        if ($process.HasExited -or $timer.Elapsed.TotalSeconds -gt 10) { throw 'Workspace did not report readiness.' }
        Start-Sleep -Milliseconds 20
    }
    $readyObserved = $timer.Elapsed.TotalMilliseconds
    $launch = Get-Content -LiteralPath $readyPath -Raw | ConvertFrom-Json
    Start-Sleep -Seconds 2
    $process.Refresh()
    $workingSet = $process.WorkingSet64
    $privateBytes = $process.PrivateMemorySize64
    $cpuBefore = $process.TotalProcessorTime.TotalSeconds
    $idleTimer = [Diagnostics.Stopwatch]::StartNew()
    Start-Sleep -Seconds 30
    $process.Refresh()
    $cpuSeconds = $process.TotalProcessorTime.TotalSeconds - $cpuBefore
    $idleSeconds = $idleTimer.Elapsed.TotalSeconds
    $metadataSource = 'CIM'
    try {
        $processorName = (Get-CimInstance Win32_Processor -ErrorAction Stop | Select-Object -First 1).Name
        $system = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
        $osCaption = $system.Caption
        $osVersion = $system.Version
    } catch {
        # Restricted accounts can still measure the application without CIM access.
        $metadataSource = 'environment'
        $processorName = $env:PROCESSOR_IDENTIFIER
        $osCaption = [Environment]::OSVersion.VersionString
        $osVersion = [Environment]::OSVersion.Version.ToString()
    }
    [ordered]@{
        date_utc = [DateTime]::UtcNow.ToString('o')
        processor = $processorName
        metadata_source = $metadataSource
        logical_processors = [Environment]::ProcessorCount
        os = $osCaption
        os_version = $osVersion
        build = 'Release / MSVC 19.38.33141 / Qt 6.8.3'
        app_sha256 = (Get-FileHash -LiteralPath $desktop -Algorithm SHA256).Hash.ToLowerInvariant()
        app_bytes = (Get-Item -LiteralPath $desktop).Length
        scene = 'Round badge starter template; native Windows platform; mouse idle'
        launch_ms = $launch.launch_ms
        ready_observed_ms = $readyObserved
        device_pixel_ratio = $launch.device_pixel_ratio
        settled_working_set_bytes = $workingSet
        settled_private_bytes = $privateBytes
        idle_cpu_percent_normalized = 100 * $cpuSeconds / $idleSeconds / [Environment]::ProcessorCount
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputPath 'desktop.json') -Encoding UTF8
    Write-Output (Join-Path $outputPath 'desktop.json')
    $process.WaitForExit(10000) | Out-Null
    if (-not $process.HasExited) { throw 'Measurement process did not exit on its timer.' }
} finally {
    if ($null -ne $process -and -not $process.HasExited) { $process.Kill() }
    Pop-Location
}
