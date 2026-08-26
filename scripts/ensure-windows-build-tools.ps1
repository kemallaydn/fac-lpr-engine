$ErrorActionPreference = 'Stop'

if (-not $IsWindows -and $env:OS -ne 'Windows_NT') {
    return
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

function Get-VcToolsInstallation {
    if (-not (Test-Path $vswhere)) {
        return $null
    }

    $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0) {
        return $null
    }
    $path = ($path | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace($path)) {
        return $null
    }
    return $path.Trim()
}

$existing = Get-VcToolsInstallation
if ($existing) {
    Write-Host "Visual Studio C++ Build Tools already available: $existing"
    return
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Visual Studio C++ Build Tools are missing and the current account '$($identity.Name)' is not elevated."
}

$installer = Join-Path $env:TEMP 'vs_BuildTools.exe'
Write-Host 'Visual Studio 2022 C++ Build Tools are missing. Installing the VCTools workload...'
Invoke-WebRequest -UseBasicParsing -Uri 'https://aka.ms/vs/17/release/vs_BuildTools.exe' -OutFile $installer

$arguments = @(
    '--quiet',
    '--wait',
    '--norestart',
    '--nocache',
    '--installPath', 'C:\BuildTools',
    '--add', 'Microsoft.VisualStudio.Workload.VCTools',
    '--includeRecommended'
)

$process = Start-Process -FilePath $installer -ArgumentList $arguments -Wait -PassThru
if ($process.ExitCode -ne 0 -and $process.ExitCode -ne 3010) {
    throw "Visual Studio Build Tools installer failed with exit code $($process.ExitCode)."
}

# vswhere is installed alongside the VS installer after a successful bootstrap.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installed = Get-VcToolsInstallation
if (-not $installed) {
    throw 'Visual Studio C++ Build Tools installation completed but VC.Tools.x86.x64 could not be discovered by vswhere.'
}

Write-Host "Visual Studio C++ Build Tools ready: $installed"
if ($process.ExitCode -eq 3010) {
    Write-Warning 'Build Tools requested a reboot, but the toolchain is discoverable. Validation will continue.'
}
