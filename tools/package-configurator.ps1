param(
    [switch]$SkipInstall
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$PythonScript = Join-Path $Root "tools\config\tk8620_configurator.py"
$OutputExe = Join-Path $Root "tools\config\tk8620_configurator.exe"
$PackageDir = Join-Path $Root "build\configurator-package"
$DistDir = Join-Path $PackageDir "dist"
$BuildDir = Join-Path $PackageDir "build"
$SpecDir = Join-Path $PackageDir "spec"
$PySerialVersion = "3.5"
$PyInstallerVersion = "6.21.0"
$PyInstallerHooksVersion = "2026.6"
$FallbackSourceDateEpoch = "1767225600"

function Resolve-Python {
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) { return @("py", "-3") }
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) { return @("python") }
    throw "Python 3 was not found. Install Python 3 to package the configurator."
}

function Invoke-Python {
    param([string[]]$Python, [string[]]$Arguments)
    $exe = $Python[0]
    $baseArgs = @()
    if ($Python.Count -gt 1) { $baseArgs += $Python[1..($Python.Count - 1)] }
    & $exe @baseArgs @Arguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Remove-WorkspacePath {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $rootResolved = (Resolve-Path -LiteralPath $Root).Path
    $pathResolved = (Resolve-Path -LiteralPath $Path).Path
    if (-not $pathResolved.StartsWith($rootResolved, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove outside workspace: $pathResolved"
    }
    Remove-Item -LiteralPath $pathResolved -Recurse -Force
}

function Set-ReproducibleBuildTimestamp {
    if ($env:SOURCE_DATE_EPOCH -match '^\d+$') { return }
    $git = Get-Command git -ErrorAction SilentlyContinue
    if ($git) {
        $epoch = (& $git.Source -C $Root log -1 --format=%ct 2>$null)
        if ($LASTEXITCODE -eq 0 -and $epoch -match '^\d+$') {
            $env:SOURCE_DATE_EPOCH = $epoch
            return
        }
    }
    $env:SOURCE_DATE_EPOCH = $FallbackSourceDateEpoch
}

if (-not (Test-Path -LiteralPath $PythonScript)) {
    throw "Configurator source not found: $PythonScript"
}

$python = Resolve-Python
Set-ReproducibleBuildTimestamp
$env:PYTHONHASHSEED = "0"
Write-Host "Packaging with SOURCE_DATE_EPOCH=$env:SOURCE_DATE_EPOCH and PYTHONHASHSEED=0"

if (-not $SkipInstall) {
    Invoke-Python -Python $python -Arguments @(
        "-m", "pip", "install",
        "pyserial==$PySerialVersion",
        "pyinstaller==$PyInstallerVersion",
        "pyinstaller-hooks-contrib==$PyInstallerHooksVersion",
        "-q"
    )
}

Remove-WorkspacePath -Path $PackageDir
New-Item -ItemType Directory -Path $SpecDir -Force | Out-Null
Invoke-Python -Python $python -Arguments @(
    "-m", "PyInstaller", "--clean", "--onefile",
    "--name", "tk8620_configurator",
    "--distpath", $DistDir,
    "--workpath", $BuildDir,
    "--specpath", $SpecDir,
    $PythonScript
)

$builtExe = Join-Path $DistDir "tk8620_configurator.exe"
if (-not (Test-Path -LiteralPath $builtExe)) {
    throw "PyInstaller did not produce $builtExe"
}
Copy-Item -LiteralPath $builtExe -Destination $OutputExe -Force
Remove-WorkspacePath -Path $PackageDir
Write-Host "Packaged configurator: $OutputExe"
