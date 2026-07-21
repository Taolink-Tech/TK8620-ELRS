param(
    [Parameter(Position = 0)]
    [string]$Mode = "help",

    [string]$Port,
    [int]$Baud = 921600,
    [string]$RxVersion,
    [int]$Freq = 900320000,
    [string]$TargetId,
    [switch]$NoReset,
    [switch]$SendReset,
    [switch]$KeepUserParams,
    [switch]$VerboseLog,
    [switch]$DryRun,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

$validModes = @("tx", "rx-stash", "help")
if ($Mode -notin $validModes) {
    Write-Host "Unknown mode: $Mode" -ForegroundColor Red
    Write-Host "Valid modes: tx, rx-stash, help"
    Write-Host ""
    Write-Host "Use rx-stash to stage Rx firmware into Tx flash before wireless update."
    exit 1
}

if ($null -ne $ExtraArgs -and $ExtraArgs.Count -gt 0) {
    Write-Host "Unexpected argument(s): $($ExtraArgs -join ' ')" -ForegroundColor Red
    Write-Host "This script uses fixed firmware paths under build\ and does not accept firmware file arguments."
    Write-Host "Run .\burn.cmd help for supported commands."
    exit 1
}

if ($Mode -eq "help") {
    Write-Host "Available targets:"
    Write-Host "  .\burn.cmd tx"
    Write-Host "      Flash TX bootloader and build\ELRS_Tx\TK8620_ELRS_TX_P.hex over UART"
    Write-Host "  .\burn.cmd rx-stash"
    Write-Host "      Stage build\ELRS_Rx\TK8620_ELRS_RX_P.hex into TX flash for wireless update"
    Write-Host ""
    Write-Host "Build firmware before flashing or staging:"
    Write-Host "  .\build.cmd tx"
    Write-Host "  .\build.cmd rx"
    exit 0
}

function Resolve-Python {
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        return @("py", "-3")
    }

    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        return @("python")
    }

    throw "Python was not found. Restore tools\burn\tk8620_bootrom.exe or install Python 3 with pyserial for the source fallback."
}

function Resolve-RequiredFile {
    param(
        [string]$Path,
        [string]$Label,
        [string]$Hint
    )

    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction SilentlyContinue

    if (-not $resolved) {
        Write-Host "$Label not found."
        Write-Host "  checked: $Path"
        if (-not [string]::IsNullOrWhiteSpace($Hint)) {
            Write-Host "  hint:    $Hint"
        }
        throw "Missing $Label file."
    }

    return $resolved.Path
}

function Get-SerialPorts {
    $portNames = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object {
        if ($_ -match '^COM(\d+)$') { [int]$Matches[1] } else { 9999 }
    }, { $_ }

    $pnpByPort = @{}
    Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '\((COM\d+)\)' } |
        ForEach-Object {
            $port = $Matches[1]
            if (-not $pnpByPort.ContainsKey($port)) {
                $pnpByPort[$port] = $_
            }
        }

    $items = @()
    foreach ($name in $portNames) {
        $desc = ""
        $vidpid = ""
        if ($pnpByPort.ContainsKey($name)) {
            $pnp = $pnpByPort[$name]
            $desc = $pnp.Name
            if ($pnp.PNPDeviceID -match 'VID_([0-9A-Fa-f]{4}).*PID_([0-9A-Fa-f]{4})') {
                $vidpid = "VID:$($Matches[1].ToUpper()) PID:$($Matches[2].ToUpper())"
            }
        }

        $items += [PSCustomObject]@{
            Port = $name
            Description = $desc
            VidPid = $vidpid
        }
    }

    return $items
}

function Select-SerialPort {
    param([string]$RequestedPort)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPort)) {
        return $RequestedPort
    }

    $ports = @(Get-SerialPorts)
    if ($ports.Count -eq 0) {
        throw "No serial ports found."
    }

    Write-Host "Serial ports:"
    for ($i = 0; $i -lt $ports.Count; $i++) {
        $p = $ports[$i]
        Write-Host ("  [{0}] {1,-8} {2} {3}" -f ($i + 1), $p.Port, $p.Description, $p.VidPid)
    }

    while ($true) {
        $choice = Read-Host "Select target port"
        $index = 0
        if ([int]::TryParse($choice, [ref]$index) -and $index -ge 1 -and $index -le $ports.Count) {
            return $ports[$index - 1].Port
        }
        Write-Host "Enter a number from 1 to $($ports.Count)."
    }
}

$defaultBootloader = Join-Path $Root "firmware\bootloader\TK8620_B_V2.0.2.hex"
$defaultTxFirmware = Join-Path $Root "build\ELRS_Tx\TK8620_ELRS_TX_P.hex"
$defaultRxFirmware = Join-Path $Root "build\ELRS_Rx\TK8620_ELRS_RX_P.hex"
$burnToolExe = Join-Path $Root "tools\burn\tk8620_bootrom.exe"
$burnToolScript = Join-Path $Root "tools\burn\tk8620_bootrom.py"

$isLocalBurn = $Mode -eq "tx"
$isRxStash = $Mode -eq "rx-stash"

if (-not $isRxStash) {
    if ($PSBoundParameters.ContainsKey("RxVersion")) {
        throw "-RxVersion is only valid with rx-stash mode."
    }
    if ($PSBoundParameters.ContainsKey("Freq")) {
        throw "-Freq is only valid with rx-stash mode."
    }
    if ($PSBoundParameters.ContainsKey("TargetId")) {
        throw "-TargetId is only valid with rx-stash mode."
    }
}

if ($isRxStash) {
    if ($PSBoundParameters.ContainsKey("KeepUserParams")) {
        throw "-KeepUserParams is only valid with tx/rx local burn modes."
    }
}

$bootloaderPath = $null
$firmwarePath = $null

if ($isLocalBurn) {
    $bootloaderPath = Resolve-RequiredFile -Path $defaultBootloader -Label "Bootloader" -Hint "Keep firmware\bootloader\TK8620_B_V2.0.2.hex in the repository."

    $firmwarePath = Resolve-RequiredFile -Path $defaultTxFirmware -Label "TX firmware" -Hint "Run .\build.cmd tx before flashing."
}
else {
    $firmwarePath = Resolve-RequiredFile -Path $defaultRxFirmware -Label "RX stash firmware" -Hint "Run .\build.cmd rx before staging an RX wireless update."
}

$targetPort = Select-SerialPort -RequestedPort $Port
if ($isLocalBurn) {
    Write-Host "Bootloader: $bootloaderPath"
    Write-Host ("{0} firmware: {1}" -f $Mode.ToUpper(), $firmwarePath)
}
else {
    Write-Host "Rx stash firmware: $firmwarePath"
}
Write-Host "Port: $targetPort"

$args = @($Mode, "--port", $targetPort, "--baud", "$Baud")

if ($isLocalBurn) {
    $args += @("--bootloader", $bootloaderPath, "--firmware", $firmwarePath)
}
else {
    $args += @(
        "--firmware", $firmwarePath
    )
    if (-not [string]::IsNullOrWhiteSpace($RxVersion)) {
        $args += @("--rx-version", $RxVersion)
    }
    $args += @("--freq", "$Freq")
    if (-not [string]::IsNullOrWhiteSpace($TargetId)) {
        $args += @("--target-id", $TargetId)
    }
}

if ($NoReset -or -not $SendReset) {
    $args += "--no-reset"
}
if ($KeepUserParams) {
    $args += "--keep-user-params"
}
if ($VerboseLog) {
    $args += "--verbose"
}
if ($DryRun) {
    $args += "--dry-run"
}

if (Test-Path -LiteralPath $burnToolExe) {
    & $burnToolExe @args
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    exit 0
}

if (-not (Test-Path -LiteralPath $burnToolScript)) {
    throw "Burn tool not found. Expected $burnToolExe or $burnToolScript."
}

Write-Host "Bundled burn executable not found; falling back to Python source."
$python = Resolve-Python
$pythonExe = $python[0]
$pythonArgs = @()
if ($python.Count -gt 1) {
    $pythonArgs += $python[1..($python.Count - 1)]
}
$pythonArgs += @($burnToolScript)
$pythonArgs += $args

& $pythonExe @pythonArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
