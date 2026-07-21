param(
    [ValidateSet('all', 'rx', 'tx', 'normal', 'sg', 'pair', 'clean', 'help')]
    [string]$Target = 'all',

    [ValidateSet('normal', 'signal-generator', 'module-pair')]
    [string]$BuildProfile = 'normal',

    [int]$RssiCompDb = -17
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Write-Step {
    param([string]$Message)
    Write-Host $Message
}

function Show-MissingToolchainMessage {
    Write-Host ""
    Write-Host "TK8620 ELRS build setup" -ForegroundColor Cyan
    Write-Host "-----------------------" -ForegroundColor Cyan
    Write-Host "Nuclei RISC-V GCC was not found." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Download the verified Windows toolchain:"
    Write-Host "  https://download.nucleisys.com/upload/files/toolchain/gcc/nuclei_riscv_newlibc_prebuilt_win32_2020.08.zip" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Official download page:"
    Write-Host "  https://www.nucleisys.com/download.php" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Add the folder containing this file to PATH or NUCLEI_GCC_BIN:"
    Write-Host "  riscv-nuclei-elf-gcc.exe" -ForegroundColor Green
    Write-Host ""
    Write-Host "To verify the setup, open a new PowerShell window and run:"
    Write-Host "  riscv-nuclei-elf-gcc.exe --version" -ForegroundColor Green
    Write-Host ""
    Write-Host "Note: the current binary SDK library is verified with GCC 9.x / Nuclei 2020.08."
    Write-Host "      Newer GCC 14.x toolchains are not compatible with this SDK library." -ForegroundColor Yellow
    Write-Host ""
}

function Show-IncompatibleToolchainMessage {
    param([pscustomobject]$Toolchain)

    Write-Host ""
    Write-Host "TK8620 ELRS build setup" -ForegroundColor Cyan
    Write-Host "-----------------------" -ForegroundColor Cyan
    Write-Host "An incompatible RISC-V GCC toolchain was found." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Found:"
    Write-Host "  $($Toolchain.Gcc)"
    if ($Toolchain.Version) {
        Write-Host "  $($Toolchain.Version)"
    }
    Write-Host ""
    Write-Host "The current open-sdk/lib/libtk86xx.a binary SDK is built with an older GCC LTO format."
    Write-Host "Use the verified Nuclei GCC 9.x / 2020.08 toolchain, or rebuild libtk86xx.a with the same newer toolchain."
    Write-Host ""
    Write-Host "Set PATH or NUCLEI_GCC_BIN to the folder containing:"
    Write-Host "  riscv-nuclei-elf-gcc.exe" -ForegroundColor Green
    Write-Host ""
    Write-Host "To verify the setup, open a new PowerShell window and run:"
    Write-Host "  riscv-nuclei-elf-gcc.exe --version" -ForegroundColor Green
    Write-Host ""
}

function Ensure-BundledDependencies {
    $ensureScript = Join-Path $PSScriptRoot 'ensure-deps.ps1'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $ensureScript
    if ($LASTEXITCODE -ne 0) {
        throw "Dependency bootstrap failed: $ensureScript"
    }
}

function New-ToolchainInfo {
    param(
        [string]$BinDir,
        [string]$Prefix,
        [bool]$Compatible,
        [string]$Version
    )

    [pscustomobject]@{
        BinDir     = (Resolve-Path $BinDir).Path
        Prefix     = $Prefix
        Compatible = $Compatible
        Version    = $Version
        Gcc        = Join-Path $BinDir "$Prefix-gcc.exe"
        Objcopy    = Join-Path $BinDir "$Prefix-objcopy.exe"
        Objdump    = Join-Path $BinDir "$Prefix-objdump.exe"
        Size       = Join-Path $BinDir "$Prefix-size.exe"
        Readelf    = Join-Path $BinDir "$Prefix-readelf.exe"
    }
}

function Find-ToolchainInBinDir {
    param([string]$BinDir)

    if ([string]::IsNullOrWhiteSpace($BinDir) -or -not (Test-Path $BinDir)) {
        return $null
    }

    foreach ($prefix in @('riscv-nuclei-elf', 'riscv64-unknown-elf')) {
        $gcc = Join-Path $BinDir "$prefix-gcc.exe"
        $objcopy = Join-Path $BinDir "$prefix-objcopy.exe"
        $objdump = Join-Path $BinDir "$prefix-objdump.exe"
        $size = Join-Path $BinDir "$prefix-size.exe"
        $readelf = Join-Path $BinDir "$prefix-readelf.exe"

        if ((Test-Path $gcc) -and (Test-Path $objcopy) -and (Test-Path $objdump) -and (Test-Path $size) -and (Test-Path $readelf)) {
            $version = (& $gcc --version 2>$null | Select-Object -First 1)
            $compatible = ($prefix -eq 'riscv-nuclei-elf')
            return (New-ToolchainInfo -BinDir $BinDir -Prefix $prefix -Compatible $compatible -Version $version)
        }
    }

    return $null
}

function Get-ToolchainInfo {
    $candidates = @(
        $env:NUCLEI_GCC_BIN,
        $env:RISCV_NUCLEI_GCC_BIN,
        $env:RISCV_TOOLCHAIN_BIN
    ) | Where-Object { $_ }

    foreach ($candidate in $candidates) {
        $toolchain = Find-ToolchainInBinDir -BinDir $candidate
        if ($toolchain) {
            return $toolchain
        }
    }

    foreach ($gccName in @('riscv-nuclei-elf-gcc.exe', 'riscv64-unknown-elf-gcc.exe')) {
        $gcc = Get-Command $gccName -ErrorAction SilentlyContinue
        if ($gcc) {
            $toolchain = Find-ToolchainInBinDir -BinDir (Split-Path -Parent $gcc.Source)
            if ($toolchain) {
                return $toolchain
            }
        }
    }

    return $null
}

function Get-ProjectConfig {
    param(
        [string]$Root,
        [ValidateSet('rx', 'tx')]
        [string]$Kind
    )

    switch ($Kind) {
        'rx' {
            $dir = Join-Path $Root 'applications\ELRS_Rx'
            $artifact = 'TK8620_ELRS_RX_P'
            $mainSource = Join-Path $dir 'rx_main.c'
            $buildSubdir = 'ELRS_Rx'
        }
        'tx' {
            $dir = Join-Path $Root 'applications\ELRS_Tx'
            $artifact = 'TK8620_ELRS_TX_P'
            $mainSource = Join-Path $dir 'tx_main.c'
            $buildSubdir = 'ELRS_Tx'
        }
    }

    [pscustomobject]@{
        Kind       = $Kind
        Name       = $buildSubdir
        Dir        = $dir
        CommonDir  = Join-Path $Root 'applications\ELRS_Common'
        Artifact   = $artifact
        MainSource = $mainSource
        BuildDir   = Join-Path $Root "build\$buildSubdir"
        Linker     = Join-Path $dir 'tk8620_flashxip.ld'
    }
}

function ConvertTo-CStringLiteral {
    param([string]$Value)

    if ($null -eq $Value) {
        $Value = ''
    }

    $escaped = $Value.Replace('\', '\\').Replace('"', '\"')
    '"' + $escaped + '"'
}

function Get-GitValue {
    param(
        [string]$Root,
        [string[]]$Arguments,
        [string]$Fallback
    )

    $git = Get-Command 'git.exe' -ErrorAction SilentlyContinue
    if (-not $git) {
        return $Fallback
    }

    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $git.Source -C $Root @Arguments 2>$null
        if ($LASTEXITCODE -ne 0 -or -not $output) {
            return $Fallback
        }
    } catch {
        return $Fallback
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    ($output | Select-Object -First 1).Trim()
}

function Get-ReleaseVersion {
    param([string]$Root)

    $versionFile = Join-Path $Root 'VERSION'
    if (-not (Test-Path $versionFile)) {
        throw "Missing release version file: $versionFile"
    }

    $version = (Get-Content -Path $versionFile -Encoding UTF8 | Select-Object -First 1).Trim()
    if ($version -notmatch '^\d+\.\d+\.\d+([-.][0-9A-Za-z][0-9A-Za-z.-]*)?$') {
        throw "Invalid VERSION value '$version'. Expected a semantic version such as 1.0.0 or 1.0.1-rc1."
    }

    $version
}

function New-VersionHeader {
    param(
        [string]$Root,
        [string]$BuildRoot
    )

    $version = Get-ReleaseVersion -Root $Root
    $commit = Get-GitValue -Root $Root -Arguments @('rev-parse', '--short=7', 'HEAD') -Fallback ''
    $dirty = Get-GitValue -Root $Root -Arguments @('status', '--porcelain', '--untracked-files=no') -Fallback ''
    $source = Get-GitValue -Root $Root -Arguments @('describe', '--tags', '--exact-match', 'HEAD') -Fallback ''
    if (-not $source) {
        $source = Get-GitValue -Root $Root -Arguments @('rev-parse', '--abbrev-ref', 'HEAD') -Fallback 'unknown'
    }

    $generatedDir = Join-Path $BuildRoot 'generated'
    $header = Join-Path $generatedDir 'version.h'
    New-Item -ItemType Directory -Path $generatedDir -Force | Out-Null

    $lines = @(
        '#pragma once',
        '',
        "#define TK8620_ELRS_VERSION $(ConvertTo-CStringLiteral $version)",
        "#define TK8620_ELRS_VERSION_SOURCE $(ConvertTo-CStringLiteral $source)",
        "#define TK8620_ELRS_GIT_COMMIT $(ConvertTo-CStringLiteral $commit)",
        "#define TK8620_ELRS_HAS_GIT_COMMIT $(if ($commit) { '1' } else { '0' })",
        "#define TK8620_ELRS_GIT_DIRTY $(if ($dirty) { '1' } else { '0' })",
        '#define ELRS_UPSTREAM_VERSION "3.5.6"',
        '#define ELRS_UPSTREAM_COMMIT "ee188b4efb9a707f682e8b2d966cd670de92ab50"',
        '',
        '#if TK8620_ELRS_GIT_DIRTY',
        '#define TK8620_ELRS_DIRTY_SUFFIX "-dirty"',
        '#else',
        '#define TK8620_ELRS_DIRTY_SUFFIX ""',
        '#endif',
        '',
        '#if TK8620_ELRS_HAS_GIT_COMMIT',
        '#define TK8620_ELRS_COMMIT_SUFFIX " " TK8620_ELRS_GIT_COMMIT TK8620_ELRS_DIRTY_SUFFIX',
        '#else',
        '#define TK8620_ELRS_COMMIT_SUFFIX ""',
        '#endif',
        '',
        '#define TK8620_ELRS_VERSION_STRING \',
        '    TK8620_ELRS_VERSION "-tk8620 elrs-" ELRS_UPSTREAM_VERSION TK8620_ELRS_COMMIT_SUFFIX',
        '',
        '#define TK8620_ELRS_MENU_VERSION \',
        '    TK8620_ELRS_VERSION TK8620_ELRS_COMMIT_SUFFIX',
        '',
        '#define TK8620_ELRS_BUILD_ID TK8620_ELRS_VERSION_STRING'
    )

    Set-Content -Path $header -Value $lines -Encoding ASCII

    [pscustomobject]@{
        Version   = $version
        Source    = $source
        Commit    = $commit
        Dirty     = [bool]$dirty
        Header    = $header
    }
}

function Get-SourceFiles {
    param([pscustomobject]$Project)

    $sources = @($Project.MainSource)
    $sources += Get-ChildItem -Path (Join-Path $Project.Dir 'ELRS\src\lib') -Recurse -File -Filter '*.c' | ForEach-Object FullName
    $sources += Get-ChildItem -Path (Join-Path $Project.Dir 'ELRS\src\src') -Recurse -File -Filter '*.c' | ForEach-Object FullName
    $sources += Get-ChildItem -Path (Join-Path $Project.CommonDir 'ELRS\src\lib') -Recurse -File -Filter '*.c' | ForEach-Object FullName
    $sources += Get-ChildItem -Path (Join-Path $Project.CommonDir 'ELRS\src\src') -Recurse -File -Filter '*.c' | ForEach-Object FullName
    $sources | Sort-Object -Unique
}

function Get-IncludeDirs {
    param(
        [pscustomobject]$Project,
        [string]$SdkRoot
    )

    $dirs = @(
        $Project.Dir,
        (Join-Path $Project.Dir 'ELRS'),
        (Join-Path $Project.Dir 'ELRS\src\include'),
        $Project.CommonDir,
        (Join-Path $Project.CommonDir 'ELRS'),
        (Join-Path $Project.CommonDir 'ELRS\src\include'),
        (Join-Path $SdkRoot 'include\Peripheral\StdPeriphDriver\Include'),
        (Join-Path $SdkRoot 'include\Peripheral\Phy'),
        (Join-Path $SdkRoot 'include\Common\tk_printf'),
        (Join-Path (Split-Path -Parent $SdkRoot) 'build\generated')
    )

    $dirs += Get-ChildItem -Path (Join-Path $Project.Dir 'ELRS\src\lib') -Recurse -Directory | ForEach-Object FullName
    $dirs += Get-ChildItem -Path (Join-Path $Project.Dir 'ELRS\src\src') -Recurse -Directory | ForEach-Object FullName
    $dirs += Get-ChildItem -Path (Join-Path $Project.CommonDir 'ELRS\src\lib') -Recurse -Directory | ForEach-Object FullName
    $dirs += Get-ChildItem -Path (Join-Path $Project.CommonDir 'ELRS\src\src') -Recurse -Directory | ForEach-Object FullName

    $dirs | Sort-Object -Unique
}

function Invoke-Checked {
    param(
        [string]$Exe,
        [string[]]$Arguments
    )

    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $Exe $($Arguments -join ' ')"
    }
}

function New-ParentDirectory {
    param([string]$Path)

    $parent = Split-Path -Parent $Path
    if ($parent -and -not (Test-Path $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
}

function Get-RelativePathCompat {
    param(
        [string]$BasePath,
        [string]$TargetPath
    )

    $baseUri = New-Object System.Uri(([System.IO.Path]::GetFullPath($BasePath.TrimEnd('\') + '\')))
    $targetUri = New-Object System.Uri([System.IO.Path]::GetFullPath($TargetPath))
    $relativeUri = $baseUri.MakeRelativeUri($targetUri)
    [System.Uri]::UnescapeDataString($relativeUri.ToString()).Replace('/', '\')
}

function Build-Project {
    param(
        [string]$Root,
        [pscustomobject]$Project,
        [pscustomobject]$Toolchain,
        [string]$SdkRoot,
        [string]$ToolsRoot,
        [string]$BuildProfile,
        [int]$RssiCompDb
    )

    $gcc = $Toolchain.Gcc
    $objcopy = $Toolchain.Objcopy
    $objdump = $Toolchain.Objdump
    $size = $Toolchain.Size
    $readelf = $Toolchain.Readelf

    $intelhex2strhex = Join-Path $ToolsRoot 'intelhex2strhex.exe'
    $codespace = Join-Path $ToolsRoot 'codespace.exe'

    New-Item -ItemType Directory -Path $Project.BuildDir -Force | Out-Null

    Write-Step "[PREP] $($Project.Name)"

    $sources = Get-SourceFiles -Project $Project
    $includeDirs = Get-IncludeDirs -Project $Project -SdkRoot $SdkRoot

    $commonArgs = @(
        '-march=rv32imac',
        '-mabi=ilp32',
        '-mcmodel=medany',
        '-msmall-data-limit=8',
        '-msave-restore',
        '-Os',
        '-ffunction-sections',
        '-fdata-sections',
        '-flto',
        '-Werror',
        '-Wall',
        '-g',
        '-DDOWNLOAD_MODE=DOWNLOAD_MODE_FLASHXIP',
        '-D__RSIC_V',
        "-DRSSI_COMP_DB=$RssiCompDb"
    )

    switch ($BuildProfile) {
        'signal-generator' {
            $commonArgs += '-DSENSI_TEST=1'
            $commonArgs += '-DSENSI_TEST_PROFILE=0'
            $commonArgs += '-DSENSI_SLOT_NUM=3'
        }
        'module-pair' {
            $commonArgs += '-DSENSI_TEST=1'
            $commonArgs += '-DSENSI_TEST_PROFILE=1'
            $commonArgs += '-DSENSI_SLOT_NUM=129'
        }
        default {
            $commonArgs += '-DSENSI_TEST=0'
            $commonArgs += '-DSENSI_TEST_PROFILE=0'
        }
    }

    $objects = New-Object System.Collections.Generic.List[string]

    foreach ($source in $sources) {
        $relativeSource = Get-RelativePathCompat -BasePath $Root -TargetPath $source
        $object = Join-Path $Project.BuildDir ($relativeSource -replace '\.c$', '.o')
        $listing = "$object.lst"

        New-ParentDirectory -Path $object

        $compileArgs = @()
        $compileArgs += $commonArgs
        foreach ($includeDir in $includeDirs) {
            $compileArgs += "-I$includeDir"
        }
        $compileArgs += @(
            '-std=gnu11',
            "-Wa,-adhlns=$listing",
            '-c',
            '-o',
            $object,
            $source
        )

        Write-Step "[CC] $relativeSource"
        Invoke-Checked $gcc $compileArgs
        $objects.Add($object)
    }

    $elf = Join-Path $Project.BuildDir "$($Project.Artifact).elf"
    $hex = Join-Path $Project.BuildDir "$($Project.Artifact).hex"
    $bin = Join-Path $Project.BuildDir "$($Project.Artifact).bin"
    $lst = Join-Path $Project.BuildDir "$($Project.Artifact).lst"
    $map = Join-Path $Project.BuildDir "$($Project.Artifact).map"
    $ram = Join-Path $Project.BuildDir "$($Project.Artifact).ram"

    $linkArgs = @(
        '-march=rv32imac',
        '-mabi=ilp32',
        '-mcmodel=medany',
        '-msmall-data-limit=8',
        '-msave-restore',
        '-Os',
        '-ffunction-sections',
        '-fdata-sections',
        '-flto',
        '-Werror',
        '-Wall',
        '-g',
        '-T',
        $Project.Linker,
        '-nostartfiles',
        '-Xlinker',
        '--gc-sections',
        "-L$(Join-Path $SdkRoot 'lib')",
        "-Wl,-Map,$map",
        '--specs=nano.specs',
        '--specs=nosys.specs',
        '-o',
        $elf
    )
    $linkArgs += $objects
    $linkArgs += @('-ltk86xx', '-lm')

    Write-Step "[LD] $($Project.Artifact).elf"
    Invoke-Checked $gcc $linkArgs

    Write-Step "[HEX] $($Project.Artifact).hex"
    Invoke-Checked $objcopy @('-O', 'ihex', $elf, $hex)
    Invoke-Checked $intelhex2strhex @($hex, $Project.Linker)

    Write-Step "[BIN] $($Project.Artifact).bin"
    Invoke-Checked $objcopy @('-O', 'binary', $elf, $bin)

    Write-Step "[LST] $($Project.Artifact).lst"
    & $objdump '--all-headers' '--disassemble' '--wide' $elf | Set-Content -Path $lst
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $objdump --all-headers --disassemble --wide $elf"
    }

    Write-Step "[SIZE] $($Project.Artifact).elf"
    Invoke-Checked $size @('--format=berkeley', '--totals', $elf)

    Write-Step "[RAM] $($Project.Artifact).ram"
    & $readelf '-SW' $elf | Set-Content -Path $ram
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $readelf -SW $elf"
    }
    Invoke-Checked $codespace @($ram, $Project.Linker)

    Write-Step "[DONE] $hex"
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sdkRoot = Join-Path $root 'open-sdk'
$toolsRoot = Join-Path $root 'tools'
$buildRoot = Join-Path $root 'build'

switch ($Target) {
    'normal' {
        $Target = 'all'
        $BuildProfile = 'normal'
    }
    'sg' {
        $Target = 'all'
        $BuildProfile = 'signal-generator'
    }
    'pair' {
        $Target = 'all'
        $BuildProfile = 'module-pair'
    }
}

switch ($Target) {
    'help' {
        Write-Host 'Available targets:'
        Write-Host '  .\build.cmd        Build RX and TX'
        Write-Host '  .\build.cmd rx     Build RX only'
        Write-Host '  .\build.cmd tx     Build TX only'
        Write-Host '  .\build.cmd clean  Remove build output'
        exit 0
    }
    'clean' {
        if (Test-Path $buildRoot) {
            Remove-Item -Recurse -Force $buildRoot
            Write-Host "Removed $buildRoot"
        } else {
            Write-Host 'Nothing to clean.'
        }
        exit 0
    }
}

Ensure-BundledDependencies
$toolchain = Get-ToolchainInfo
if (-not $toolchain) {
    Show-MissingToolchainMessage
    exit 2
}
if (-not $toolchain.Compatible) {
    Show-IncompatibleToolchainMessage -Toolchain $toolchain
    exit 3
}

$versionInfo = New-VersionHeader -Root $root -BuildRoot $buildRoot
$commitDisplay = if ($versionInfo.Commit) {
    " $($versionInfo.Commit)$(if ($versionInfo.Dirty) { '-dirty' } else { '' })"
} else {
    ''
}
Write-Step "[VERSION] $($versionInfo.Version)-tk8620 elrs-3.5.6$commitDisplay"
Write-Step "[PROFILE] $BuildProfile"

if ($Target -in @('all', 'rx')) {
    Build-Project -Root $root -Project (Get-ProjectConfig -Root $root -Kind 'rx') -Toolchain $toolchain -SdkRoot $sdkRoot -ToolsRoot $toolsRoot -BuildProfile $BuildProfile -RssiCompDb $RssiCompDb
}

if ($Target -in @('all', 'tx')) {
    Build-Project -Root $root -Project (Get-ProjectConfig -Root $root -Kind 'tx') -Toolchain $toolchain -SdkRoot $sdkRoot -ToolsRoot $toolsRoot -BuildProfile $BuildProfile -RssiCompDb $RssiCompDb
}

