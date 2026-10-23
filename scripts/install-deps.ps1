# Automated Dependency Installer Script for ka-hgis
# Run in Administrator PowerShell on Windows.

$ErrorActionPreference = "Continue"

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host " ka-hgis Development Setup Script" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

# 1. Check & Install CMake via Winget
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "[1/3] Installing CMake..." -ForegroundColor Yellow
    winget install --id Kitware.CMake -e --accept-source-agreements --accept-package-agreements
} else {
    Write-Host "[1/3] CMake is already installed." -ForegroundColor Green
}

# 2. Check & Install Visual Studio 2022 C++ Build Tools
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$hasVS = $false
if (Test-Path $vsWhere) {
    $inst = & $vsWhere -latest -property installationPath
    if ($inst) { $hasVS = $true }
}

if (-not $hasVS) {
    Write-Host "[2/3] Installing Visual Studio 2022 Build Tools (C++ Workload)..." -ForegroundColor Yellow
    winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" --accept-source-agreements --accept-package-agreements
} else {
    Write-Host "[2/3] Visual Studio 2022 is already installed." -ForegroundColor Green
}

# 3. OSGeo4W Setup
$OSGEO_DIR = if ($env:OSGEO4W_ROOT) { $env:OSGEO4W_ROOT } else {
    if (Test-Path "D:\") { "D:\OSGeo4W" } else { "C:\OSGeo4W" }
}
Write-Host "[3/3] Setting up OSGeo4W at $OSGEO_DIR..." -ForegroundColor Yellow

$setupExe = "$env:TEMP\osgeo4w-setup.exe"
Write-Host "Downloading OSGeo4W installer to $setupExe..." -ForegroundColor Gray
Invoke-WebRequest -Uri "https://download.osgeo.org/osgeo4w/v2/osgeo4w-setup.exe" -OutFile $setupExe

$packages = "qgis-dev,qt6-devel,gdal-dev-devel,sqlite3-devel,zlib-devel,pdal-dev"
Write-Host "Installing OSGeo4W packages ($packages)..." -ForegroundColor Yellow
Write-Host "Command: $setupExe -q -k -r -R $OSGEO_DIR -s https://download.osgeo.org/osgeo4w/v2/ -P $packages" -ForegroundColor Gray

Start-Process -FilePath $setupExe -ArgumentList "-q -k -r -R $OSGEO_DIR -s https://download.osgeo.org/osgeo4w/v2/ -P $packages" -Wait

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host " Setup script finished!" -ForegroundColor Green
Write-Host " Run '.\scripts\build-all.ps1' to test building the project." -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan
