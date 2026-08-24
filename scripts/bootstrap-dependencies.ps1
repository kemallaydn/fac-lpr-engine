$ErrorActionPreference = 'Stop'

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BaselineFile = Join-Path $RootDir 'cmake/vcpkg-baseline.txt'
$VcpkgCommit = (Get-Content $BaselineFile -Raw).Trim()
$VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $RootDir '.tools/vcpkg' }
$Triplet = if ($env:VCPKG_DEFAULT_TRIPLET) { $env:VCPKG_DEFAULT_TRIPLET } else { 'x64-windows' }

$OrtVersion = (Get-Content (Join-Path $RootDir 'cmake/onnxruntime-version.txt') -Raw).Trim()
$OrtSha256 = (Get-Content (Join-Path $RootDir 'cmake/onnxruntime-win-x64.sha256') -Raw).Trim().ToLowerInvariant()
$OrtAsset = "onnxruntime-win-x64-$OrtVersion.zip"
$OrtUrl = "https://github.com/microsoft/onnxruntime/releases/download/v$OrtVersion/$OrtAsset"
$OrtCacheDir = Join-Path $RootDir '.cache/onnxruntime'
$OrtArchive = Join-Path $OrtCacheDir $OrtAsset
$OrtRoot = Join-Path $RootDir 'build/deps/onnxruntime'

if ($Triplet -ne 'x64-windows') {
    throw "Unsupported Windows triplet for prebuilt ONNX Runtime: $Triplet"
}

if (-not (Test-Path (Join-Path $VcpkgRoot '.git'))) {
    New-Item -ItemType Directory -Force -Path (Split-Path $VcpkgRoot -Parent) | Out-Null
    git clone --filter=blob:none https://github.com/microsoft/vcpkg.git $VcpkgRoot
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

$OrtHeader = Join-Path $OrtRoot 'include/onnxruntime_cxx_api.h'
$OrtImportLib = Join-Path $OrtRoot 'lib/onnxruntime.lib'
if (-not (Test-Path $OrtHeader) -or -not (Test-Path $OrtImportLib)) {
    New-Item -ItemType Directory -Force -Path $OrtCacheDir | Out-Null
    if (Test-Path $OrtArchive) {
        $ActualSha256 = (Get-FileHash -Algorithm SHA256 $OrtArchive).Hash.ToLowerInvariant()
        if ($ActualSha256 -ne $OrtSha256) {
            Remove-Item -Force $OrtArchive
        }
    }

    if (-not (Test-Path $OrtArchive)) {
        $Partial = "$OrtArchive.part"
        Remove-Item -Force -ErrorAction SilentlyContinue $Partial
        & curl.exe --fail --location --retry 3 --retry-delay 2 --output $Partial $OrtUrl
        if ($LASTEXITCODE -ne 0) {
            throw "ONNX Runtime download failed with exit code $LASTEXITCODE"
        }
        Move-Item -Force $Partial $OrtArchive
    }

    $ActualSha256 = (Get-FileHash -Algorithm SHA256 $OrtArchive).Hash.ToLowerInvariant()
    if ($ActualSha256 -ne $OrtSha256) {
        throw "ONNX Runtime checksum mismatch. Expected $OrtSha256, got $ActualSha256"
    }

    $TempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("fac-lpr-ort-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $TempDir | Out-Null
    try {
        Expand-Archive -Path $OrtArchive -DestinationPath $TempDir -Force
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $OrtRoot
        New-Item -ItemType Directory -Force -Path $OrtRoot | Out-Null
        Copy-Item -Recurse -Force (Join-Path $TempDir "onnxruntime-win-x64-$OrtVersion/*") $OrtRoot
    }
    finally {
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $TempDir
    }
}

Write-Host "Dependencies restored. FAC_LPR_ONNXRUNTIME_ROOT=$OrtRoot"
