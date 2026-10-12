[CmdletBinding()]
param([switch]$SkipLaunch)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$buildExe = Join-Path $root 'build/ninja-release/i2c_memorymap.exe'
$dist = [IO.Path]::GetFullPath((Join-Path $root 'dist'))
$package = [IO.Path]::GetFullPath((Join-Path $dist 'I2C_MemoryMap'))
$zip = [IO.Path]::GetFullPath((Join-Path $dist 'I2C_MemoryMap.zip'))

try {
    # Check before touching the previous package, including suspended processes
    # whose executable path may not be readable. Do not terminate the user's app.
    $running = @(Get-Process | Where-Object { $_.ProcessName -eq 'i2c_memorymap' })
    if ($running.Count -gt 0) {
        throw "i2c_memorymap.exe is still running (PID: $($running.Id -join ', ')). Close it before packaging. The existing package has not been changed."
    }
    if (-not (Test-Path -LiteralPath $buildExe -PathType Leaf)) {
        throw "Executable not found: $buildExe"
    }

    # Validate the absolute deletion targets; keep all filesystem operations in
    # PowerShell and never follow an existing distribution directory junction.
    foreach ($target in @($package, $zip)) {
        if ([IO.Path]::GetDirectoryName($target) -ne $dist) {
            throw "Output path is outside the distribution directory: $target"
        }
    }
    foreach ($target in @($dist, $package, $zip)) {
        if (Test-Path -LiteralPath $target) {
            $item = Get-Item -LiteralPath $target -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Output path must not be a symbolic link or junction: $target"
            }
        }
    }

    Write-Host '=== Packaging Started ==='
    if (Test-Path -LiteralPath $package) {
        Remove-Item -LiteralPath $package -Recurse -Force
    }
    if (Test-Path -LiteralPath $zip) {
        Remove-Item -LiteralPath $zip -Force
    }
    New-Item -ItemType Directory -Path $package -Force | Out-Null
    Copy-Item -LiteralPath $buildExe -Destination $package

    $resources = Join-Path $root 'resources'
    if (Test-Path -LiteralPath $resources -PathType Container) {
        Get-ChildItem -LiteralPath $resources -Force | Copy-Item -Destination $package -Recurse -Force
    }
    $bridge = Join-Path $root 'saleae-live'
    if (Test-Path -LiteralPath $bridge -PathType Container) {
        & robocopy $bridge (Join-Path $package 'saleae-live') /e /xd __pycache__ /r:0 /w:0 | Out-Null
        if ($LASTEXITCODE -ge 8) {
            throw "Live bridge copy failed (robocopy exit code: $LASTEXITCODE)."
        }
    }
    foreach ($name in @('README.md', 'LICENSE')) {
        Copy-Item -LiteralPath (Join-Path $root $name) -Destination $package
    }
    $archiveInputs = @(Get-ChildItem -LiteralPath $package | Select-Object -ExpandProperty FullName)
    Compress-Archive -LiteralPath $archiveInputs -DestinationPath $zip -Force

    Write-Host "=== Packaging Completed ===`nOutput: $zip"
    if (-not $SkipLaunch) {
        # Launch only after every copy and ZIP operation has succeeded.
        Start-Process -FilePath (Join-Path $package 'i2c_memorymap.exe') -WorkingDirectory $package
    }
    exit 0
}
catch {
    Write-Host "[ERROR] $($_.Exception.Message)" -ForegroundColor Red
    Write-Host '=== Packaging Failed ==='
    exit 1
}
