param()

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Test-DependencyReady {
    param(
        [string]$ToolsRoot,
        [object]$Dependency
    )

    if ($Dependency.PSObject.Properties.Name -contains "target" -and -not [string]::IsNullOrWhiteSpace($Dependency.target)) {
        $targetPath = Join-Path $ToolsRoot $Dependency.target
        if (-not (Test-Path $targetPath)) {
            return $false
        }

        if ($Dependency.PSObject.Properties.Name -contains "check" -and -not [string]::IsNullOrWhiteSpace($Dependency.check)) {
            $checkPath = Join-Path $targetPath $Dependency.check
            return (Test-Path $checkPath)
        }

        return $true
    }

    if ($Dependency.PSObject.Properties.Name -contains "executable" -and -not [string]::IsNullOrWhiteSpace($Dependency.executable)) {
        if ($Dependency.PSObject.Properties.Name -contains "env") {
            foreach ($envName in $Dependency.env) {
                $envValue = [Environment]::GetEnvironmentVariable($envName)
                if (-not [string]::IsNullOrWhiteSpace($envValue)) {
                    if (Test-Path (Join-Path $envValue $Dependency.executable)) {
                        return $true
                    }
                }
            }
        }

        $cmd = Get-Command $Dependency.executable -ErrorAction SilentlyContinue
        return ($null -ne $cmd)
    }

    return $false
}

function Ensure-Dependency {
    param(
        [pscustomobject]$Dependency,
        [string]$ToolsRoot
    )

    if (Test-DependencyReady -ToolsRoot $ToolsRoot -Dependency $Dependency) {
        return
    }
}

$toolsRoot = $PSScriptRoot
$manifestPath = Join-Path $toolsRoot 'deps-manifest.json'
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json

foreach ($dependency in $manifest.dependencies) {
    Ensure-Dependency -Dependency $dependency -ToolsRoot $toolsRoot
}
