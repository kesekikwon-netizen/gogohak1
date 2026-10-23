# Build a self-contained Windows folder: USB copy, no OSGeo4W install on the target PC.
# Runs only when the user asks for portable output (AGENTS.md). It never blocks on
# verify-release; it records in PORTABLE-MANIFEST.json whether this exact EXE passed it.
#   -RefreshManifestOnly  rewrite PORTABLE-MANIFEST.json of an existing folder (publish-desktop.ps1)
param(
  [string]$OutDir = "",
  [switch]$IncludeLocalCredentials,
  [switch]$RefreshManifestOnly
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$out = if ($OutDir) { $OutDir } else { Join-Path $root "dist\ka-hgis-portable" }
$out = [System.IO.Path]::GetFullPath($out)

function Get-GitText([string[]]$gitArgs) {
  $old = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    $text = & git -C $root @gitArgs 2>$null
    if ($LASTEXITCODE -ne 0) { return '' }
    return (($text | Out-String).Trim())
  } catch {
    return ''
  } finally {
    $ErrorActionPreference = $old
  }
}

# F178: what was delivered, so the receiving PC can compare it. Verified means the EXE in
# the folder is byte-identical to the one scripts/verify-release.ps1 built, tested and
# smoke-started, and the sources have not changed since. It never blocks the package.
function Write-PortableManifest([string]$portableRoot) {
  $exePath = Join-Path $portableRoot 'ka-hgis.exe'
  $exeHash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
  $receiptPath = Join-Path $root 'build\release-verified.json'
  $verified = $false
  $reason = 'no verify-release receipt: scripts/verify-release.ps1 was not run for this build'
  $receiptUtc = $null
  if (Test-Path -LiteralPath $receiptPath) {
    try {
      $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
      $receiptUtc = $receipt.verifiedUtc
      if ($receipt.executableSha256 -ne $exeHash) {
        $reason = 'the verify-release receipt is for a different EXE'
      } else {
        try {
          & (Join-Path $PSScriptRoot 'verify-release.ps1') -CheckOnly | Out-Null
          $verified = $true
          $reason = 'verify-release passed for this EXE and the current sources'
        } catch {
          $reason = 'same EXE, but sources or tests changed since verify-release: ' + $_.Exception.Message
        }
      }
    } catch {
      $reason = 'unreadable verify-release receipt: ' + $_.Exception.Message
    }
  }
  # F180: qgis-dev is rebuilt daily, so record whether the bundled OSGeo4W packages still
  # match dev-env.lock.json (scripts/dev-env-lock.ps1: 0 same, 1 drifted, 2 no lock).
  $sdkLock = 'not checked'
  $lockScript = Join-Path $PSScriptRoot 'dev-env-lock.ps1'
  if (Test-Path -LiteralPath $lockScript) {
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
      & $lockScript -OsgeoOnly *> $null
      $lockCode = $LASTEXITCODE
    } catch {
      $lockCode = -1
    } finally {
      $ErrorActionPreference = $oldPreference
    }
    $sdkLock = switch ($lockCode) {
      0 { 'matches dev-env.lock.json' }
      1 { 'differs from dev-env.lock.json (OSGeo4W packages drifted)' }
      2 { 'no dev-env.lock.json' }
      default { 'lock check failed' }
    }
    if ($lockCode -eq 1) { Write-Warning "Bundled OSGeo4W packages differ from dev-env.lock.json." }
  }
  $status = if ($verified) { 'verified' } else { 'unverified' }
  $manifest = [ordered]@{
    schema            = 1
    createdUtc        = [DateTime]::UtcNow.ToString('o')
    executable        = 'ka-hgis.exe'
    executableSha256  = $exeHash
    releaseStatus     = $status
    releaseStatusNote = $reason
    releaseVerifiedUtc = $receiptUtc
    version           = ((Get-Content -LiteralPath (Join-Path $root 'VERSION') -TotalCount 1) -as [string]).Trim()
    gitCommit         = (Get-GitText @('rev-parse', 'HEAD'))
    gitDescribe       = (Get-GitText @('describe', '--tags', '--always', '--dirty'))
    qgisPin           = ((Get-Content -LiteralPath (Join-Path $root 'VERSION_QGIS_PIN.txt') -TotalCount 1) -as [string]).Trim()
    sdkLock           = $sdkLock
    credentialsIncluded = [bool](Get-ChildItem -Path (Join-Path $portableRoot 'config\*') -File -ErrorAction SilentlyContinue `
        -Include 'secrets.ini', '*-account.ini', '*-local.ini', 'ka-hgis-vworld.ini')
  }
  $json = $manifest | ConvertTo-Json
  [System.IO.File]::WriteAllText((Join-Path $portableRoot 'PORTABLE-MANIFEST.json'), $json, [System.Text.UTF8Encoding]::new($false))
  Write-Host ("Portable manifest: {0} (EXE SHA256 {1})" -f $status, $exeHash)
  if (-not $verified) { Write-Warning "Portable EXE is not release-verified: $reason" }
}

if ($RefreshManifestOnly) {
  if (-not (Test-Path -LiteralPath (Join-Path $out 'ka-hgis.exe'))) { throw "No portable EXE in $out" }
  Write-PortableManifest $out
  exit 0
}

# Never erase an existing delivery or a user's data through an arbitrary OutDir.
if (Test-Path -LiteralPath $out) {
  throw "Output already exists. Choose a new folder with -OutDir: $out"
}
$exe = Join-Path $root "build\Release\ka-hgis.exe"
if (-not (Test-Path $exe)) { throw "Build ka-hgis.exe first (Release)." }

# Same search order as scripts/dev-env.ps1 (the one documented order, F180):
# OSGEO4W_ROOT -> C:\OSGeo4W -> D:\OSGeo4W -> A:\OSGeo4W.
$OSGEO = $null
if ($env:OSGEO4W_ROOT -and (Test-Path -LiteralPath $env:OSGEO4W_ROOT)) {
  $OSGEO = $env:OSGEO4W_ROOT
} elseif (Test-Path "C:\OSGeo4W") { $OSGEO = "C:\OSGeo4W" }
elseif (Test-Path "D:\OSGeo4W") { $OSGEO = "D:\OSGeo4W" }
elseif (Test-Path "A:\OSGeo4W") { $OSGEO = "A:\OSGeo4W" }
else { throw "OSGEO4W_ROOT not found on this build PC." }

$qgis = Join-Path $OSGEO "apps\qgis-dev"
if (-not (Test-Path $qgis)) { throw "qgis-dev missing under $OSGEO" }
$caBundle = Join-Path $OSGEO "bin\curl-ca-bundle.crt"
if (-not (Test-Path -LiteralPath $caBundle -PathType Leaf)) {
  throw "OSGeo4W curl-ca-bundle.crt missing. HTTPS downloads require this runtime file."
}

Write-Host "Portable out: $out"
& (Join-Path $PSScriptRoot 'copy-webengine-runtime.ps1') -OsgeoRoot $OSGEO -Destination $out -CheckOnly
Write-Host "Runtime from: $OSGEO"
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Invoke-Robo([string]$src, [string]$dst, [string[]]$xd = @()) {
  if (-not (Test-Path $src)) { Write-Host "skip missing $src"; return }
  New-Item -ItemType Directory -Force -Path $dst | Out-Null
  $args = @($src, $dst, "/E", "/NFL", "/NDL", "/NJH", "/NJS", "/nc", "/ns", "/np", "/XO")
  if ($xd.Count -gt 0) { $args += @("/XD") + $xd }
  $args += @("/XF", "*.pdb", "*.lib", "*.exp", "*.a", "*.prl")
  & robocopy @args | Out-Null
  $code = $LASTEXITCODE
  if ($code -ge 8) { throw "robocopy failed $code : $src -> $dst" }
}

function Copy-Dlls([string]$src, [string]$dst) {
  if (-not (Test-Path $src)) { return }
  New-Item -ItemType Directory -Force -Path $dst | Out-Null
  Copy-Item (Join-Path $src "*.dll") $dst -Force -ErrorAction SilentlyContinue
}

Copy-Item $exe $out -Force
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $out
# Full GNU GPL v2 text (verbatim from the QGIS SDK) next to the short notice.
Copy-Item -LiteralPath (Join-Path $root 'COPYING') -Destination $out
Copy-Item -LiteralPath (Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination $out
# The receiving PC checks the folder with this (PORTABLE-MANIFEST.json + required files).
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'verify-portable-pack.ps1') -Destination $out
$noticeDir = Join-Path $out 'licenses'
New-Item -ItemType Directory -Force -Path $noticeDir | Out-Null
foreach ($notice in @('LICENSE', 'AUTHORS', 'CONTRIBUTORS')) {
  $noticeSource = Join-Path $qgis ('doc/' + $notice)
  if (Test-Path -LiteralPath $noticeSource -PathType Leaf) {
    Copy-Item -LiteralPath $noticeSource -Destination (Join-Path $noticeDir ('QGIS-' + $notice))
  }
}
# libcurl needs its trust store as well as DLLs on a standalone machine.
Copy-Item -LiteralPath $caBundle -Destination $out -Force
# PDB가 있으면 함께 배포 — 크래시 로그(KaCrashGuard)가 함수명·줄번호까지 심볼화한다.
$pdb = Join-Path $root "build\Release\ka-hgis.pdb"
if (Test-Path $pdb) { Copy-Item $pdb $out -Force }
if (Test-Path (Join-Path $root "data")) {
  Copy-Item (Join-Path $root "data") $out -Recurse -Force
}
# DWG converter (LibreDWG, a separate program). The ka-hgis build puts it in build\Release\tools\libredwg.
Invoke-Robo (Join-Path $root "build\Release\tools\libredwg") (Join-Path $out "tools\libredwg")
$docsUser = Join-Path $out "docs\user"
New-Item -ItemType Directory -Force -Path $docsUser | Out-Null
if (Test-Path (Join-Path $root "docs\user")) {
  Copy-Item (Join-Path $root "docs\user\*") $docsUser -Recurse -Force
}

Write-Host "Copying QGIS prefix..."
Invoke-Robo $qgis (Join-Path $out "apps\qgis-dev") @("python", "grass", "include", "lib", "doc", "server", "bin")

# DLL은 실행 파일 폴더(루트)에 한 벌만 둔다. 예전에는 apps\*\bin 과 bin\ 에도
# 같은 파일을 복사해 733 MB가 중복으로 쌓였다(2026-09-03 실측). start.bat 의
# PATH가 루트를 맨 앞에 두므로 하위 bin 사본은 쓰이지 않는다.
# 여기서 받는 것은 DLL이 아닌 것들 — Qt 플러그인, GDAL 데이터, PROJ 데이터.
Write-Host "Copying Qt6 plugins..."
Invoke-Robo (Join-Path $OSGEO "apps\Qt6\plugins") (Join-Path $out "apps\Qt6\plugins")
& (Join-Path $PSScriptRoot 'copy-webengine-runtime.ps1') -OsgeoRoot $OSGEO -Destination $out

Write-Host "Copying GDAL data..."
if (Test-Path (Join-Path $OSGEO "apps\gdal-dev\share")) {
  Invoke-Robo (Join-Path $OSGEO "apps\gdal-dev\share") (Join-Path $out "apps\gdal-dev\share")
}
if (Test-Path (Join-Path $OSGEO "share\proj")) {
  Invoke-Robo (Join-Path $OSGEO "share\proj") (Join-Path $out "share\proj")
}
# proj.dll 옆에도 두어 검색 경로가 깨져도 EPSG:5186/3857을 읽게 한다.
foreach ($projFile in @("proj.db", "proj.ini")) {
  $src = Join-Path $out "share\proj\$projFile"
  if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src -Destination $out -Force }
}

$qgisBin = Join-Path $qgis "bin"
# Windows loads DLLs from the exe folder first. Copy every runtime bin here
# so double-click / start.bat works without a pre-set PATH.
Copy-Dlls $qgisBin $out
Copy-Dlls (Join-Path $OSGEO "apps\Qt6\bin") $out
Copy-Dlls (Join-Path $OSGEO "apps\gdal-dev\bin") $out
Copy-Dlls (Join-Path $OSGEO "apps\pdal-dev\bin") $out
Copy-Dlls (Join-Path $OSGEO "bin") $out
Get-ChildItem (Join-Path $OSGEO "apps") -Directory -ErrorAction SilentlyContinue | ForEach-Object {
  $b = Join-Path $_.FullName "bin"
  if (Test-Path $b) { Copy-Dlls $b $out }
}

# 조사에 쓰이지 않는 DLL은 루트에서 뺀다. 실행 중 로드된 모듈 목록과 대조해
# "한 번도 로드되지 않고, 이 앱이 그 기능을 제공하지도 않는" 것만 골랐다
# (2026-09-03 실측). Qt6WebEngineCore·libpq·libmysql은 실제로 로드되므로 남긴다.
# Qt3D/Quick3D도 3D 지형 창이 열릴 때 쓰이므로 건드리지 않는다.
$skipDlls = @(
  "oraociicus.dll", "oci.dll",   # Oracle 클라이언트 — Oracle에 접속하지 않는다
  "msodbcsql18.dll",             # SQL Server ODBC — 쓰지 않는다
  "qgis_app.dll",                # QGIS 데스크톱 앱 라이브러리 — core/gui만 링크한다
  "python312.dll",               # QGIS Python — prefix에서 python을 이미 뺐다
  "Qt6Designer.dll", "Qt6DesignerComponents.dll", "Qt6QmlCompiler.dll"  # 개발 도구
)
$freed = 0
foreach ($n in $skipDlls) {
  $f = Join-Path $out $n
  if (Test-Path $f) { $freed += (Get-Item $f).Length; Remove-Item $f -Force }
}
Write-Host ("Skipped unused DLLs: {0:N1} MB" -f ($freed / 1MB))

$sys32 = Join-Path $env:WINDIR "System32"
foreach ($vc in @(
    "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll",
    "msvcp140_2.dll", "concrt140.dll", "vccorlib140.dll"
  )) {
  $src = Join-Path $sys32 $vc
  if (Test-Path $src) { Copy-Item $src $out -Force }
}

# qgis-dev\bin 사본은 두지 않는다 — 루트 한 벌로 충분하다(위 주석 참고).

# F138: every shipped DLL with the OSGeo4W package and version it came from, so GPL/LGPL
# recipients can find the matching upstream source (OSGeo4W etc/setup: installed.db, *.lst.gz).
function Write-BundledComponentList([string]$portableRoot, [string]$osgeoRoot) {
  $setup = Join-Path $osgeoRoot 'etc\setup'
  $versions = @{}
  $db = Join-Path $setup 'installed.db'
  if (Test-Path -LiteralPath $db) {
    foreach ($line in (Get-Content -LiteralPath $db | Select-Object -Skip 1)) {
      $parts = $line -split '\s+'
      if ($parts.Count -ge 2) { $versions[$parts[0]] = ($parts[1] -replace '\.tar\.bz2$', '') }
    }
  }
  $owner = @{}
  foreach ($list in (Get-ChildItem -LiteralPath $setup -Filter '*.lst.gz' -ErrorAction SilentlyContinue)) {
    $package = $list.Name -replace '\.lst\.gz$', ''
    $stream = [System.IO.File]::OpenRead($list.FullName)
    try {
      $gzip = New-Object System.IO.Compression.GZipStream($stream, [System.IO.Compression.CompressionMode]::Decompress)
      $reader = New-Object System.IO.StreamReader($gzip)
      while ($null -ne ($entry = $reader.ReadLine())) {
        if ($entry -match '\.dll$') {
          $name = [System.IO.Path]::GetFileName($entry).ToLowerInvariant()
          if (-not $owner.ContainsKey($name)) { $owner[$name] = $package }
        }
      }
      $reader.Dispose()
    } finally {
      $stream.Dispose()
    }
  }
  $vcRuntime = @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
                 'concrt140.dll', 'vccorlib140.dll')
  $rows = New-Object System.Collections.Generic.List[string]
  $perPackage = @{}
  foreach ($dll in (Get-ChildItem -LiteralPath $portableRoot -Filter '*.dll' | Sort-Object Name)) {
    $key = $dll.Name.ToLowerInvariant()
    $source = if ($owner.ContainsKey($key)) {
      $pkg = $owner[$key]
      if ($versions.ContainsKey($pkg)) { $versions[$pkg] } else { $pkg }
    } elseif ($vcRuntime -contains $key) {
      'Microsoft Visual C++ runtime (Windows System32, redistributable)'
    } else {
      'unknown (not listed by OSGeo4W setup)'
    }
    $perPackage[$source] = 1 + [int]$perPackage[$source]
    $rows.Add(("{0,-48} {1,10:N0}  {2}" -f $dll.Name, $dll.Length, $source))
  }
  $header = @(
    'ka-hgis portable - bundled components',
    ('Generated: {0:u}  OSGeo4W root: {1}' -f (Get-Date).ToUniversalTime(), $osgeoRoot),
    ('QGIS pin: {0}' -f ((Get-Content -LiteralPath (Join-Path $root 'VERSION_QGIS_PIN.txt') -TotalCount 1) -as [string])),
    '',
    'Licenses: see ../THIRD_PARTY_NOTICES.md, ../COPYING (GNU GPL v2), QGIS-LICENSE in this folder.',
    'Upstream binaries and their source packages (-src) are published by OSGeo4W:',
    '  https://download.osgeo.org/osgeo4w/v2/  (find each package by the name-version below)',
    'ka-hgis source for this build: ../source/ka-hgis-source.zip',
    '',
    'Packages (DLL count):'
  )
  $summary = $perPackage.Keys | Sort-Object | ForEach-Object { '  {0}  ({1})' -f $_, $perPackage[$_] }
  $body = @('', 'DLL                                                    bytes  package') + $rows
  $text = ($header + $summary + $body) -join "`r`n"
  [System.IO.File]::WriteAllText((Join-Path $portableRoot 'licenses\BUNDLED-COMPONENTS.txt'), $text,
    [System.Text.UTF8Encoding]::new($false))
  Write-Host ("Bundled component list: {0} DLLs" -f $rows.Count)
}

# F138: GPL corresponding source travels with the binaries, whatever the GitHub repository's
# visibility. Same file set verify-release fingerprints (tracked + untracked, .gitignore respected).
function Write-SourceArchive([string]$portableRoot) {
  $old = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    $paths = @(& git -C $root -c core.quotepath=false ls-files --cached --others --exclude-standard -- `
        src tests data cmake scripts templates third_party launch.ps1 CMakeLists.txt CMakePresets.json VERSION `
        VERSION_QGIS_PIN.txt dev-env.lock.json LICENSE COPYING README.md THIRD_PARTY_NOTICES.md 2>$null)
  } finally {
    $ErrorActionPreference = $old
  }
  if ($paths.Count -eq 0) {
    Write-Warning 'Source archive skipped: git could not list the sources. Ship the source separately.'
    return
  }
  Add-Type -AssemblyName System.IO.Compression
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $sourceDir = Join-Path $portableRoot 'source'
  New-Item -ItemType Directory -Force -Path $sourceDir | Out-Null
  $zipPath = Join-Path $sourceDir 'ka-hgis-source.zip'
  $zip = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
  try {
    foreach ($relative in ($paths | Sort-Object -Unique)) {
      $absolute = Join-Path $root $relative
      if (-not (Test-Path -LiteralPath $absolute -PathType Leaf)) { continue }
      [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $absolute, ($relative -replace '\\', '/'))
    }
  } finally {
    $zip.Dispose()
  }
  $readme = @(
    'ka-hgis source (GNU GPL v2 or later) for the ka-hgis.exe in this folder.',
    ('Revision: {0}' -f (Get-GitText @('describe', '--tags', '--always', '--dirty'))),
    'Build: see README.md in the archive (Windows, OSGeo4W qgis-dev, Visual Studio 2022, CMake).'
  ) -join "`r`n"
  [System.IO.File]::WriteAllText((Join-Path $sourceDir 'README.txt'), $readme, [System.Text.UTF8Encoding]::new($false))
  Write-Host ("Source archive: {0:N1} MB" -f ((Get-Item -LiteralPath $zipPath).Length / 1MB))
}

Write-BundledComponentList $out $OSGEO
Write-SourceArchive $out

$runPs1 = @'
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$env:OSGEO4W_ROOT = $here
$env:QGIS_PREFIX_PATH = Join-Path $here "apps\qgis-dev"
$bins = @(
  $here,
  (Join-Path $here "bin"),
  (Join-Path $here "apps\qgis-dev\bin"),
  (Join-Path $here "apps\Qt6\bin"),
  (Join-Path $here "apps\gdal-dev\bin"),
  (Join-Path $here "apps\pdal-dev\bin")
) | Where-Object { Test-Path $_ }
$env:PATH = ($bins + $env:PATH) -join ";"
$qtPlug = Join-Path $here "apps\Qt6\plugins"
if (Test-Path $qtPlug) { $env:QT_PLUGIN_PATH = $qtPlug }
$env:QGIS_PLUGIN_PATH = Join-Path $here "apps\qgis-dev\plugins"
$gdal = Join-Path $here "apps\gdal-dev\share\gdal"
if (Test-Path $gdal) { $env:GDAL_DATA = $gdal }
$proj = Join-Path $here "share\proj"
if (Test-Path $proj) { $env:PROJ_DATA = $proj; $env:PROJ_LIB = $proj }
$ca = Join-Path $here "curl-ca-bundle.crt"
if (Test-Path $ca) { $env:CURL_CA_BUNDLE = $ca; $env:SSL_CERT_FILE = $ca }
$exe = Join-Path $here "ka-hgis.exe"
if (-not (Test-Path $exe)) { throw "ka-hgis.exe missing in $here" }
& $exe @args
exit $LASTEXITCODE
'@
Set-Content -LiteralPath (Join-Path $out "run.ps1") -Value $runPs1 -Encoding UTF8
Set-Content -LiteralPath (Join-Path $out "run.bat") -Value @"
@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run.ps1" %*
"@ -Encoding ASCII
Set-Content -LiteralPath (Join-Path $out "start.bat") -Value @"
@echo off
cd /d "%~dp0"
set "OSGEO4W_ROOT=%~dp0"
set "QGIS_PREFIX_PATH=%~dp0apps\qgis-dev"
set "PATH=%~dp0;%~dp0bin;%~dp0apps\qgis-dev\bin;%~dp0apps\Qt6\bin;%~dp0apps\gdal-dev\bin;%~dp0apps\pdal-dev\bin;%PATH%"
set "QT_PLUGIN_PATH=%~dp0apps\Qt6\plugins"
set "QGIS_PLUGIN_PATH=%~dp0apps\qgis-dev\plugins"
set "GDAL_DATA=%~dp0apps\gdal-dev\share\gdal"
set "PROJ_DATA=%~dp0share\proj"
set "PROJ_LIB=%~dp0share\proj"
set "CURL_CA_BUNDLE=%~dp0curl-ca-bundle.crt"
set "SSL_CERT_FILE=%~dp0curl-ca-bundle.crt"
start "" "%~dp0ka-hgis.exe"
"@ -Encoding ASCII

$readmeKo = @"
필드고고학GIS  포터블 (Windows 10/11 64비트)

이 폴더 전체를 USB에 두면, QGIS/OSGeo4W를 설치하지 않은 다른 PC에서도 실행됩니다.
Visual Studio 설치도 필요 없습니다. Windows 화면 배율과 현재 모니터 작업 영역을 따릅니다.

실행:
  ka-hgis.exe   ← 이것을 더블클릭 (다른 PC·USB·한글 경로에서도)
  start.bat     ← 예전 방식. 없어도 EXE만으로 됩니다.

폴더 전체를 그대로 복사하세요. EXE만 옮기면 좌표계를 못 읽어 위성·지적이 안 뜹니다.

주의:
  - apps, bin, share 폴더를 지우면 실행되지 않습니다.
  - 폴더 이름에 한글이 있어도 되지만, 경로가 너무 길면 start.bat 을 쓰세요.
  - VWorld 지도·주소 검색은 더보기 → API 키 입력에서 유효한 키를 입력하세요.
  - 주변유적·수치지형도 다운로드 계정도 더보기 메뉴에서 입력하세요. 인터넷이 필요합니다.
  - 설정은 포터블 config 폴더에 저장됩니다. 계정을 입력한 폴더를 공유할 때 주의하세요.
  - GNU GPL v2 이상 (QGIS 라이브러리 링크). 자세한 공지는 앱 정보 창과 THIRD_PARTY_NOTICES.md.
    GPL 전문은 COPYING, 함께 넣은 DLL 과 그 출처 패키지는 licenses\BUNDLED-COMPONENTS.txt 에 있습니다.

이 판 확인:
  PORTABLE-MANIFEST.json 에 ka-hgis.exe 의 SHA256 과 검증 여부(verified/unverified)가 있습니다.
  다른 PC 에서 폴더를 받았으면 PowerShell 에서 이 폴더의 verify-portable-pack.ps1 을 실행해 대조하세요.

제작: 동국문화재연구원  ·  만든이: youngin kwon
소스: 이 폴더의 source\ka-hgis-source.zip (이 EXE 를 만든 소스). 저장소: https://github.com/kesekikwon-netizen/gogohak1
"@
Set-Content -LiteralPath (Join-Path $out "README.txt") -Value $readmeKo -Encoding UTF8
$guideName = (-join ([char]0xC0AC, [char]0xC6A9, [char]0xBC95)) + '.txt'
Set-Content -LiteralPath (Join-Path $out $guideName) -Value $readmeKo -Encoding UTF8

function Copy-VworldKeyToPortable([string]$portableRoot) {
  $dstDir = Join-Path $portableRoot "config"
  New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
  $dst = Join-Path $dstDir "secrets.ini"
  # QStandardPaths::AppConfigLocation on Windows: LocalAppData/org/app.
  # Copy Qt's serialized INI intact; legacy org-only settings may hold an expired key.
  $personal = Join-Path $env:LOCALAPPDATA 'ka-hgis/ka-hgis/ka-hgis-vworld.ini'
  if (Test-Path -LiteralPath $personal -PathType Leaf) {
    Copy-Item -LiteralPath $personal -Destination $dst -Force
    Copy-Item -LiteralPath $personal -Destination (Join-Path $dstDir 'ka-hgis-vworld.ini') -Force
    Write-Host 'VWorld key: current app settings included (value not printed)'
    return
  }
  $cands = @(
    (Join-Path $env:APPDATA "ka-hgis\ka-hgis-vworld.ini"),
    (Join-Path $env:LOCALAPPDATA "ka-hgis\ka-hgis-vworld.ini"),
    (Join-Path $env:APPDATA "ka-hgis\ka-hgis\ka-hgis-vworld.ini"),
    (Join-Path $env:LOCALAPPDATA "ka-hgis\ka-hgis\ka-hgis-vworld.ini")
  )
  $key = $null
  foreach ($p in $cands) {
    if (-not (Test-Path -LiteralPath $p)) { continue }
    foreach ($line in Get-Content -LiteralPath $p -ErrorAction SilentlyContinue) {
      if ($line -match '^\s*ApiKey\s*=\s*(.+)\s*$') {
        $cand = $Matches[1].Trim().Trim('"')
        if ($cand) { $key = $cand; break }
      }
    }
    if ($key) { break }
  }
  if (-not $key -and $env:VWORLD_API_KEY) { $key = $env:VWORLD_API_KEY.Trim() }
  if (-not $key) {
    Write-Host "VWorld key: not found on this PC (other PC will need 더보기 → API 키 입력)"
    return
  }
  $ini = "[VWorld]`r`nApiKey=$key`r`n"
  [System.IO.File]::WriteAllText($dst, $ini, [System.Text.UTF8Encoding]::new($false))
  Write-Host "VWorld key: copied into portable config/secrets.ini (value not printed)"
}

if ($IncludeLocalCredentials) { Copy-VworldKeyToPortable $out }

# DPAPI 암호문은 이 Windows 사용자만 푼다. 포터블에는 password_portable(앱과 같은 가림 형식,
# 암호화는 아님)로 바꿔 실어
# 다른 PC에서도 같은 폴더가 로그인된다. 값은 화면에 찍지 않는다.
function Convert-KaAccountIniToPortable([string]$path) {
  if (-not (Test-Path -LiteralPath $path)) { return $false }
  Add-Type -AssemblyName System.Security
  $entropy = [System.Text.Encoding]::UTF8.GetBytes('ka-hgis-account-v1')
  $lines = Get-Content -LiteralPath $path -Encoding UTF8
  $changed = $false
  $failed = $false
  $outLines = foreach ($line in $lines) {
    if ($line -match '^\s*password_dpapi\s*=\s*(.+)\s*$') {
      $b64 = $Matches[1].Trim().Trim('"')
      try {
        $blob = [Convert]::FromBase64String($b64)
        $plain = [System.Security.Cryptography.ProtectedData]::Unprotect($blob, $entropy, 'CurrentUser')
        # Same travelling form as KaSecretStore::writePortable (UTF-8 XOR 'ka-hgis-account-v1',
        # base64). No plain password= is ever written into the portable folder.
        $wrapped = New-Object byte[] $plain.Length
        for ($i = 0; $i -lt $plain.Length; $i++) { $wrapped[$i] = $plain[$i] -bxor $entropy[$i % $entropy.Length] }
        [Array]::Clear($plain, 0, $plain.Length)
        $changed = $true
        'password_portable="' + [Convert]::ToBase64String($wrapped) + '"'
      } catch {
        $failed = $true
        $line
      }
    } else {
      $line
    }
  }
  if ($changed) {
    [System.IO.File]::WriteAllLines($path, $outLines, [System.Text.UTF8Encoding]::new($false))
  }
  if ($failed) { return $false }
  $text = Get-Content -LiteralPath $path -Raw -Encoding UTF8
  return ($text -match '(?m)^\s*password\s*=') -or ($text -match '(?m)^\s*password_portable\s*=') -or -not ($text -match '(?m)^\s*password_dpapi\s*=')
}

function Copy-AccountIniToPortable([string]$portableRoot, [string]$destName, [string[]]$candidates, [string]$label) {
  $dstDir = Join-Path $portableRoot "config"
  New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
  $src = $null
  foreach ($p in $candidates) { if (Test-Path -LiteralPath $p) { $src = $p; break } }
  if (-not $src) {
    Write-Host "$label : not found on this PC"
    return
  }
  $dst = Join-Path $dstDir $destName
  Copy-Item -LiteralPath $src -Destination $dst -Force
  if (Convert-KaAccountIniToPortable $dst) {
    Write-Host "$label : copied for any PC (values not printed)"
  } else {
    Write-Host "$label : copied, but this Windows user could not unlock the password for another PC"
  }
}

if ($IncludeLocalCredentials) {
  # F179: personal-portable exception (docs/portable-desktop.md). The user asked for accounts to
  # travel so another PC needs no login; this is opt-in and never the default. Say plainly
  # what the folder now holds.
  Write-Warning ("-IncludeLocalCredentials: this folder's config\ will hold the VWorld key and account " +
    "passwords unencrypted (key in plain text, passwords only obfuscated as password_portable). " +
    "Keep it on your own USB/PC; do not share, upload or hand it over. " +
    "Build without the switch for anyone else.")
  $accountRoots = @(
    (Join-Path (Join-Path $env:LOCALAPPDATA "ka-hgis") "ka-hgis"),
    (Join-Path $env:APPDATA "ka-hgis"),
    (Join-Path $env:LOCALAPPDATA "ka-hgis"),
    (Join-Path (Join-Path $env:APPDATA "ka-hgis") "ka-hgis")
  )
  Copy-AccountIniToPortable $out "ngii-local.ini" (@(
      ($accountRoots | ForEach-Object { Join-Path $_ "ngii-account.ini" })
    ) + (Join-Path $root "config/ngii-local.ini")) "NGII account"
  Copy-Item -LiteralPath (Join-Path $out "config/ngii-local.ini") -Destination (Join-Path $out "config/ngii-account.ini") -Force -ErrorAction SilentlyContinue
  Copy-AccountIniToPortable $out "heritage-account.ini" (@(
      ($accountRoots | ForEach-Object { Join-Path $_ "heritage-account.ini" })
    ) + (Join-Path $root "config/heritage-local.ini")) "Heritage account"
  Copy-AccountIniToPortable $out "vworld-account.ini" @(
      $accountRoots | ForEach-Object { Join-Path $_ "vworld-account.ini" }
    ) "VWorld cadastral account"
  Add-Content -LiteralPath (Join-Path $out 'README.txt') -Encoding UTF8 -Value "`r`n개인용 패키지: 이 PC에 저장된 API 키와 계정 파일을 포함했습니다. 다른 PC에서도 같은 폴더로 로그인됩니다.`r`n주의: config 폴더에 키와 계정 비밀번호가 그대로(암호화 없이) 들어 있습니다. 본인 USB·PC 에만 두고 다른 사람에게 주거나 올리지 마세요."
  $guideName = (-join ([char]0xC0AC, [char]0xC6A9, [char]0xBC95)) + '.txt'
  Add-Content -LiteralPath (Join-Path $out $guideName) -Encoding UTF8 -Value "`r`n개인용 패키지: 계정은 이 폴더 config 에 있습니다. 다른 컴퓨터로 폴더를 통째로 복사하세요."
}

Write-PortableManifest $out
Write-Host "Portable folder ready: $out"
Get-ChildItem $out | Select-Object Name, Mode, @{n='MB';e={ if ($_.PSIsContainer) { '' } else { [math]::Round($_.Length/1MB,1) } }}
$qgisOut = Join-Path $out "apps\qgis-dev\plugins"
Write-Host ("qgis plugins dir exists=" + (Test-Path $qgisOut))
Write-Host ("proj.db exists=" + (Test-Path (Join-Path $out "share\proj\proj.db")))
Write-Host ("qwindows exists=" + (Test-Path (Join-Path $out "apps\Qt6\plugins\platforms\qwindows.dll")))
