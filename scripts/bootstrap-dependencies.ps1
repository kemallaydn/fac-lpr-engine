$ErrorActionPreference = 'Stop'

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BaselineFile = Join-Path $RootDir 'cmake/vcpkg-baseline.txt'
$VcpkgCommit = (Get-Content $BaselineFile -Raw).Trim()
$VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $RootDir '.tools/vcpkg' }
$Triplet = if ($env:VCPKG_DEFAULT_TRIPLET) { $env:VCPKG_DEFAULT_TRIPLET } else { 'x64-windows' }

if (-not (Test-Path (Join-Path $VcpkgRoot '.git'))) {
    New-Item -ItemType Directory -Force -Path (Split-Path $VcpkgRoot -Parent) | Out-Null
    git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
}

git -C $VcpkgRoot fetch --depth 1 origin $VcpkgCommit
git -C $VcpkgRoot checkout --detach $VcpkgCommit

& (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
& (Join-Path $VcpkgRoot 'vcpkg.exe') install `
    "--x-manifest-root=$RootDir" `
    "--triplet=$Triplet" `
    "--x-install-root=$(Join-Path $RootDir 'build/vcpkg_installed')"

if ($LASTEXITCODE -ne 0) {
    throw "vcpkg dependency restore failed with exit code $LASTEXITCODE"
}
