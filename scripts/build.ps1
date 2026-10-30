param([switch]$CoreOnly,[switch]$Sanitize)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $preset = if ($Sanitize) { 'core-asan' } elseif ($CoreOnly) { 'core' } else { 'desktop' }
    cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw 'Configuration failed.' }
    cmake --build --preset $preset --parallel 1 -- /nr:false
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    ctest --preset $preset
    if ($LASTEXITCODE -ne 0) { throw 'Contract checks failed.' }
} finally { Pop-Location }
