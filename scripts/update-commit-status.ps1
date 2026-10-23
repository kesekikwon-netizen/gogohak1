# Regenerates docs/COMMIT_STATUS.md from git state.
# Run manually, or via .githooks/pre-commit (auto on every commit).
param(
  [switch]$IncludeStagedSummary,
  [string]$Root = ""
)

$ErrorActionPreference = "Stop"
if (-not $Root) {
  $Root = Split-Path -Parent $PSScriptRoot
}
Set-Location $Root

function ExecGit([string[]]$GitArgs) {
  $out = & git @GitArgs 2>&1
  if ($LASTEXITCODE -ne 0) {
    throw "git $($GitArgs -join ' ') failed: $out"
  }
  return ($out | Out-String).TrimEnd()
}

if (-not (Test-Path (Join-Path $Root ".git"))) {
  throw "Not a git repository: $Root"
}

$branch = ExecGit @("rev-parse", "--abbrev-ref", "HEAD")
$headFull = ExecGit @("rev-parse", "HEAD")
$headShort = ExecGit @("rev-parse", "--short", "HEAD")
$headSubject = ExecGit @("log", "-1", "--format=%s")
$headDate = ExecGit @("log", "-1", "--format=%ci")
$headAuthor = ExecGit @("log", "-1", "--format=%an <%ae>")
$version = if (Test-Path "VERSION") { (Get-Content "VERSION" -Raw).Trim() } else { "unknown" }

$upstream = ""
$sync = "no upstream"
try {
  $upstream = ExecGit @("rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}")
  $counts = ExecGit @("rev-list", "--left-right", "--count", "${upstream}...HEAD")
  $parts = ($counts -split "\s+") | Where-Object { $_ -ne "" }
  $behind = [int]$parts[0]
  $ahead = [int]$parts[1]
  if ($ahead -eq 0 -and $behind -eq 0) { $sync = "up to date with $upstream" }
  elseif ($ahead -gt 0 -and $behind -eq 0) { $sync = "ahead $ahead of $upstream (push pending)" }
  elseif ($ahead -eq 0 -and $behind -gt 0) { $sync = "behind $behind from $upstream (pull needed)" }
  else { $sync = "diverged: ahead $ahead, behind $behind vs $upstream" }
} catch {
  $upstream = "(none)"
  $sync = "no upstream configured"
}

$porcelain = & git status --porcelain 2>&1
$dirty = if ($porcelain) { "yes" } else { "no" }
$dirtyCount = if ($porcelain) { @($porcelain).Count } else { 0 }

$remoteUrl = ""
try { $remoteUrl = ExecGit @("remote", "get-url", "origin") } catch { $remoteUrl = "(no origin)" }

$now = Get-Date -Format "yyyy-MM-dd HH:mm:ss K"
$totalCommits = ExecGit @("rev-list", "--count", "HEAD")

# Recent history table
$logLines = & git log -30 --date=iso-strict --format="%h|%ad|%s" 2>&1
$rows = @()
foreach ($line in $logLines) {
  if (-not $line) { continue }
  $bits = $line -split "\|", 3
  if ($bits.Count -lt 3) { continue }
  $msg = ($bits[2] -replace "\|", "/").Trim()
  $rows += "| ``$($bits[0])`` | $($bits[1]) | $msg |"
}

$stagedSummary = ""
if ($IncludeStagedSummary) {
  $staged = & git diff --cached --name-status 2>&1
  if ($staged) {
    $stagedSummary = @"

## Staged in this commit

``````
$($staged -join "`n")
``````
"@
  }
}

# Milestone anchors (best-effort from subjects)
$milestones = @()
$allLog = & git log --reverse --format="%h%x09%s" 2>&1
foreach ($line in $allLog) {
  if (-not $line) { continue }
  $tab = $line.IndexOf([char]9)
  if ($tab -lt 1) { continue }
  $h = $line.Substring(0, $tab).Trim()
  $s = $line.Substring($tab + 1).Trim()
  if ($s -match "project foundation|first import") {
    $milestones += "- ``$h`` foundation / first import — $s"
  } elseif ($s -match "version to 0\.3|bump version to 0\.3") {
    $milestones += "- ``$h`` version 0.3.0 — $s"
  } elseif ($s -match "Track portable dist|portable dist package") {
    $milestones += "- ``$h`` portable exe in repo — $s"
  } elseif ($s -match "GitHub Actions CI|Actions CI with") {
    $milestones += "- ``$h`` CI workflow — $s"
  } elseif ($s -match "KaIcons") {
    $milestones += "- ``$h`` UI icons — $s"
  } elseif ($s -match "COMMIT_STATUS|commit status") {
    $milestones += "- ``$h`` commit progress ledger — $s"
  }
}
if (-not $milestones) { $milestones = @("- (auto milestones not matched yet)") }

$content = @"
# Git Commit Status

> **Auto-generated.** Do not hand-edit.  
> Refresh: ``.\scripts\update-commit-status.ps1``  
> Auto on commit: ``.githooks/pre-commit`` (after ``.\scripts\install-git-hooks.ps1``)

## Snapshot

| Field | Value |
|------|--------|
| Updated (local) | $now |
| VERSION file | $version |
| Branch | ``$branch`` |
| HEAD short | ``$headShort`` |
| HEAD full | ``$headFull`` |
| HEAD date | $headDate |
| HEAD author | $headAuthor |
| HEAD subject | $headSubject |
| Total commits | $totalCommits |
| Upstream | ``$upstream`` |
| Sync | $sync |
| Origin | $remoteUrl |
| Working tree dirty | **$dirty** ($dirtyCount paths) |

## How far we are

- **Recorded tip when this file was written:** $headSubject (``$headShort`` @ $headDate)
- **Remote sync at write time:** $sync
- **Uncommitted local changes at write time:** $dirty ($dirtyCount paths)

### Read this correctly

1. After ``git pull``, run ``git log -1 --oneline`` for the absolute tip (this file may lag by one commit if it was refreshed in ``post-commit`` and not yet re-committed).
2. The **Recent commits** table is the durable history of what landed.
3. If dirty = yes on a dev machine, another computer will **not** see those paths until commit + push.
4. Always-on update: ``.\scripts\install-git-hooks.ps1`` once per clone.

## Milestone markers

$($milestones -join "`n")

## Recent commits (newest first, max 30)

| Hash | Date | Subject |
|------|------|---------|
$($rows -join "`n")
$stagedSummary
## Other PC checklist

``````powershell
git clone https://github.com/kesekikwon-netizen/gogohak1.git
cd hgis
git pull
Get-Content docs\COMMIT_STATUS.md -Head 40
# optional hooks on that PC:
.\scripts\install-git-hooks.ps1
``````

## Maintainer notes

- This file is updated by ``scripts/update-commit-status.ps1``.
- ``pre-commit`` hook rewrites it and ``git add``s it so **every commit records progress**.
- Field survey files (``*.gpkg``, ``*.qgz``) stay local (gitignored).
- Portable binary: ``dist/ka-hgis-portable/`` (needs OSGeo4W on target PC).
"@

$outPath = Join-Path $Root "docs\COMMIT_STATUS.md"
$outDir = Split-Path $outPath -Parent
if (-not (Test-Path $outDir)) {
  New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

# Normalize to UTF-8 without BOM for git friendliness
$utf8 = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($outPath, $content, $utf8)
Write-Host "Updated $outPath (HEAD=$headShort dirty=$dirty)"
