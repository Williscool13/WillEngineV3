# Run wrapper: optionally regenerate a scripted scene, launch the engine with a .wplay armed,
# collect the capture PNGs into a timestamped directory, print their paths.
#
#   .\scripts\playtest_run.ps1 -Play ballance_roll_held
#   .\scripts\playtest_run.ps1 -Play military_sandbox -Generator scripts\build_military_sandbox.py
#
# Requires a WILL_EDITOR build (editor camera + texture stub generation live there).
param(
    [Parameter(Mandatory = $true)][string]$Play,
    [string]$Generator = "",
    [string]$BuildDir = "cmake-build-debug-visual-studio"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

if ($Generator) {
    python (Join-Path $repo $Generator)
    if ($LASTEXITCODE -ne 0) { throw "generator failed" }
}

$exe = Join-Path $repo "$BuildDir\will-engine.exe"
if (-not (Test-Path $exe)) { throw "engine exe not found: $exe" }
$playFile = Join-Path $repo "scenes\$Play.wplay"
if (-not (Test-Path $playFile)) { throw "run script not found: $playFile" }

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path $repo "captures\$Play\$stamp"

$argList = @("--play", $playFile, "--out", $outDir, "--exit")

Write-Host "run: $Play -> $outDir"
# game.dll and the crash/shader-watch paths resolve relative to the working directory
Push-Location (Split-Path -Parent $exe)
try { & $exe @argList } finally { Pop-Location }

$pngs = @(Get-ChildItem $outDir -Filter "*.png" -ErrorAction SilentlyContinue | Sort-Object Name)
Write-Host "$($pngs.Count) capture(s):"
foreach ($p in $pngs) { Write-Host "  $($p.FullName)" }
