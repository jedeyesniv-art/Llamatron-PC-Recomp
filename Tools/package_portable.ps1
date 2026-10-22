# Builds Release and assembles the portable folder dist\Llamatron\:
#   llamatron.exe (static runtime: no DLLs), README.txt, licenses\, an empty game\ folder (+ the disk image with
#   -GameData). The PDB goes to dist\symbols\.
# -Release also writes dist\release\Llamatron-<version>-windows.zip (no game files, no userdata - checked) and
# Llamatron-<version>-symbols.zip.
#
# Usage: powershell -ExecutionPolicy Bypass -File Tools\package_portable.ps1 [-GameData <.msa/.st file>] [-SkipBuild] [-Release]
param(
    [string]$GameData = "",
    [switch]$SkipBuild,
    [switch]$Release
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $repo "dist\Llamatron"
$symbols = Join-Path $repo "dist\symbols"

if (-not $SkipBuild) {
    $log = Join-Path $repo "build\package_build.log"
    New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
    cmd /c "`"$repo\build.bat`" > `"$log`" 2>&1"
    if (Select-String -Path $log -Pattern "FAILED|error:" -Quiet) {
        Get-Content $log | Select-String -Pattern "FAILED|error:" | Select-Object -First 20
        throw "Release build failed; see $log"
    }
    Write-Host "Release build OK"
}

$exe = Join-Path $repo "build\llamatron.exe"
if (-not (Test-Path $exe)) { throw "$exe not found - build first" }
$version = (Select-String -Path (Join-Path $repo "CMakeLists.txt") -Pattern 'project\(Llamatron VERSION ([0-9.]+)').Matches[0].Groups[1].Value

# Fresh folder, but keep game\ and userdata\ if they are already there.
New-Item -ItemType Directory -Force $dist | Out-Null
Get-ChildItem $dist | Where-Object { $_.Name -notin "game", "userdata" } | Remove-Item -Recurse -Force
Copy-Item $exe $dist
Copy-Item (Join-Path $repo "res\README.txt") $dist
New-Item -ItemType Directory -Force (Join-Path $dist "licenses") | Out-Null
Copy-Item (Join-Path $repo "licenses\THIRD-PARTY-NOTICES.txt") (Join-Path $dist "licenses")
$game = Join-Path $dist "game"
New-Item -ItemType Directory -Force $game | Out-Null
if ($GameData) {
    Copy-Item $GameData $game
    Write-Host "game data copied: $GameData"
}
New-Item -ItemType Directory -Force $symbols | Out-Null
Copy-Item (Join-Path $repo "build\llamatron.pdb") $symbols
Write-Host "portable folder: $dist (version $version)"

if ($Release) {
    $out = Join-Path $repo "dist\release"
    New-Item -ItemType Directory -Force $out | Out-Null
    $stage = Join-Path $out "stage\Llamatron"
    if (Test-Path (Split-Path $stage)) { Remove-Item -Recurse -Force (Split-Path $stage) }
    New-Item -ItemType Directory -Force $stage | Out-Null
    Copy-Item $exe, (Join-Path $repo "res\README.txt") $stage
    Copy-Item -Recurse (Join-Path $dist "licenses") $stage
    New-Item -ItemType Directory -Force (Join-Path $stage "game") | Out-Null
    Set-Content -Path (Join-Path $stage "game\PUT YOUR LLAMATRON DISK IMAGE HERE.txt") -Value "Copy your Llamatron disk image (.msa or .st) into this folder."
    # Leak check: no game files, no user data.
    $bad = Get-ChildItem -Recurse $stage | Where-Object { $_.Extension -in ".msa", ".st", ".prg", ".yak", ".cow", ".bin", ".log" -or $_.FullName -match "\\userdata" }
    if ($bad) { throw "release would contain: $($bad.FullName -join ', ')" }
    $zip = Join-Path $out "Llamatron-$version-windows.zip"
    Compress-Archive -Path $stage -DestinationPath $zip -Force
    Compress-Archive -Path (Join-Path $symbols "llamatron.pdb") -DestinationPath (Join-Path $out "Llamatron-$version-symbols.zip") -Force
    Remove-Item -Recurse -Force (Split-Path $stage)
    Write-Host "release: $zip"
}
