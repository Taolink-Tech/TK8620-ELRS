param(
    [switch]$SkipInstall
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$PythonScript = Join-Path $Root "tools\burn\tk8620_flasher.py"
$Bootpatch = Join-Path $Root "tools\burn\bootpatch.h"
$OutputExe = Join-Path $Root "tools\burn\tk8620_flasher.exe"
$PackageDir = Join-Path $Root "build\flasher-package"
$DistDir = Join-Path $PackageDir "dist"
$BuildDir = Join-Path $PackageDir "build"
$SpecDir = Join-Path $PackageDir "spec"
$SpecFile = Join-Path $SpecDir "tk8620_flasher.spec"

function Resolve-Python {
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        return @("py", "-3")
    }

    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        return @("python")
    }

    throw "Python 3 was not found. Install Python 3 to package the flasher."
}

function Invoke-Python {
    param(
        [string[]]$Python,
        [string[]]$Arguments
    )

    $exe = $Python[0]
    $baseArgs = @()
    if ($Python.Count -gt 1) {
        $baseArgs += $Python[1..($Python.Count - 1)]
    }
    & $exe @baseArgs @Arguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

function Remove-WorkspacePath {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    $rootResolved = (Resolve-Path -LiteralPath $Root).Path
    $pathResolved = (Resolve-Path -LiteralPath $Path).Path
    if (-not $pathResolved.StartsWith($rootResolved, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove outside workspace: $pathResolved"
    }

    Remove-Item -LiteralPath $pathResolved -Recurse -Force
}

if (-not (Test-Path -LiteralPath $PythonScript)) {
    throw "Flasher source not found: $PythonScript"
}
if (-not (Test-Path -LiteralPath $Bootpatch)) {
    throw "Boot patch header not found: $Bootpatch"
}

$python = Resolve-Python

if (-not $SkipInstall) {
    Write-Host "Ensuring Python packaging dependencies..."
    Invoke-Python -Python $python -Arguments @("-m", "pip", "install", "pyserial==3.5", "pyinstaller", "-q")
}

Write-Host "Building $OutputExe..."
Remove-WorkspacePath -Path $PackageDir
New-Item -ItemType Directory -Path $SpecDir -Force | Out-Null

$addData = "$Bootpatch;."
Invoke-Python -Python $python -Arguments @(
    "-m", "PyInstaller",
    "--clean",
    "--onefile",
    "--name", "tk8620_flasher",
    "--add-data", $addData,
    "--distpath", $DistDir,
    "--workpath", $BuildDir,
    "--specpath", $SpecDir,
    $PythonScript
)

$builtExe = Join-Path $DistDir "tk8620_flasher.exe"
if (-not (Test-Path -LiteralPath $builtExe)) {
    throw "PyInstaller did not produce $builtExe"
}

Copy-Item -LiteralPath $builtExe -Destination $OutputExe -Force
Remove-WorkspacePath -Path $PackageDir

Write-Host "Packaged flasher: $OutputExe"
