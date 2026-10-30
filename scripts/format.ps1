param([switch]$Check,[string]$Formatter)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    if (-not $Formatter) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $Formatter = Join-Path $vsRoot 'VC/Tools/Llvm/x64/bin/clang-format.exe'
    }
    if (-not (Test-Path $Formatter)) { throw 'Install clang-format 17 or pass -Formatter.' }
    $files = Get-ChildItem core,io,app,tests,benchmarks -Recurse -File | Where-Object { $_.Extension -in @('.cpp','.h') } | ForEach-Object { $_.FullName }
    if ($Check) { & $Formatter --dry-run --Werror $files } else { & $Formatter -i $files }
    if ($LASTEXITCODE -ne 0) { throw 'Formatting check failed.' }
} finally { Pop-Location }
