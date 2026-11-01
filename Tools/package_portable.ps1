# "Llamasoft on the ST" (the project's title from 1.3.0; the exe stays llamatron.exe).
# Builds Release and assembles the portable folder dist\LlamasoftOnTheST\:
#   llamatron.exe (static runtime: no DLLs), README.txt, licenses\, game\ with every game (see below). The PDB
#   goes to dist\symbols\.
# -Release also writes dist\release\LlamasoftOnTheST-<version>-windows.zip (top folder "Llamasoft on the ST", with
# game\; no unpacked game files besides Trip-A-Tron's folder, no userdata or logs - checked) and
# LlamasoftOnTheST-<version>-symbols.zip.
#
# The games (owner 2026-11-01: Llamasoft only - the compilation disks' other games don't ship):
#   Llamatron ST.msa (Llamatron + Super Gridrunner), Trip-A-Tron\ (TRIP_A_T.RON's files), the Revenge 1 MB STE
#   .stx, and Andes Attack / Defender 2 / Hardcore .st built by Tools\make_release_disks.py from the downloads.
#
# Usage: powershell -ExecutionPolicy Bypass -File Tools\package_portable.ps1 [-SkipBuild] [-Release]
param(
    [switch]$SkipBuild,
    [switch]$Release
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $repo "dist\LlamasoftOnTheST"
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

# The game folder's contents, assembled once.
function Copy-Games([string]$to) {
    New-Item -ItemType Directory -Force $to | Out-Null
    $llamatron = Join-Path $repo "Private\Llamatron ST.msa"
    $trip = Join-Path $repo "Private\new\trip_a_t\TRIP_A_T.RON"
    $rmc = Get-ChildItem (Join-Path $repo "Private\new\rmc1mb") -Filter *.stx | Select-Object -First 1
    foreach ($p in $llamatron, $trip) { if (-not (Test-Path $p)) { throw "$p not found" } }
    if (-not $rmc) { throw "the Revenge 1 MB STE .stx (Private\new\rmc1mb) not found" }
    Copy-Item $llamatron $to
    New-Item -ItemType Directory -Force (Join-Path $to "Trip-A-Tron") | Out-Null
    Copy-Item (Join-Path $trip "*") (Join-Path $to "Trip-A-Tron")
    # -LiteralPath: the download's name has [brackets], which -Path takes as a wildcard (and copies nothing).
    Copy-Item -LiteralPath $rmc.FullName -Destination (Join-Path $to "Revenge of the Mutant Camels (1MB STE).stx")
    # The TT Megamix (owner 2026-11-01: include it, "a great historic artefact"): its disk holds only Revenge.
    $rmctt = Join-Path $repo "Private\Revenge_of_the_Mutant_Camels_demo-playable_1991_Llamasoft_TT_Megamix.st"
    if (-not (Test-Path -LiteralPath $rmctt)) { throw "$rmctt not found" }
    Copy-Item -LiteralPath $rmctt -Destination (Join-Path $to "Revenge of the Mutant Camels (TT Megamix).st")
    python -P (Join-Path $repo "Tools\make_release_disks.py") $to
    if ($LASTEXITCODE) { throw "make_release_disks.py failed" }
    foreach ($f in "Llamatron ST.msa", "Trip-A-Tron\TRIP1.TOS", "Revenge of the Mutant Camels (1MB STE).stx",
                   "Revenge of the Mutant Camels (TT Megamix).st",
                   "Andes Attack.st", "Defender 2.st", "Hardcore.st", "Photon Storm.st") {
        if (-not (Test-Path -LiteralPath (Join-Path $to $f))) { throw "game\$f missing" }
    }
}

# Fresh folder, but keep userdata\ if it's already there (the owner's scores and settings).
New-Item -ItemType Directory -Force $dist | Out-Null
Get-ChildItem $dist | Where-Object { $_.Name -ne "userdata" } | Remove-Item -Recurse -Force
Copy-Item $exe $dist
Copy-Item (Join-Path $repo "res\README.txt") $dist
New-Item -ItemType Directory -Force (Join-Path $dist "licenses") | Out-Null
Copy-Item (Join-Path $repo "licenses\THIRD-PARTY-NOTICES.txt") (Join-Path $dist "licenses")
Copy-Games (Join-Path $dist "game")
New-Item -ItemType Directory -Force $symbols | Out-Null
Copy-Item (Join-Path $repo "build\llamatron.pdb") $symbols
Write-Host "portable folder: $dist (version $version)"

if ($Release) {
    $out = Join-Path $repo "dist\release"
    New-Item -ItemType Directory -Force $out | Out-Null
    $stage = Join-Path $out "stage\Llamasoft on the ST"
    if (Test-Path (Split-Path $stage)) { Remove-Item -Recurse -Force (Split-Path $stage) }
    New-Item -ItemType Directory -Force $stage | Out-Null
    Copy-Item $exe, (Join-Path $repo "res\README.txt") $stage
    Copy-Item -Recurse (Join-Path $dist "licenses") $stage
    Copy-Games (Join-Path $stage "game")
    # Leak check: disk images only (plus Trip-A-Tron's own folder, which is how it ships); no unpacked game
    # files from the other disks, no user data, no logs.
    $bad = Get-ChildItem -Recurse $stage | Where-Object {
        ($_.Extension -in ".prg", ".tos", ".yak", ".cow", ".bin", ".log", ".boy" -and $_.FullName -notmatch "\\Trip-A-Tron\\") -or
        $_.FullName -match "\\userdata" }
    if ($bad) { throw "release would contain: $($bad.FullName -join ', ')" }
    $zip = Join-Path $out "LlamasoftOnTheST-$version-windows.zip"
    Compress-Archive -Path $stage -DestinationPath $zip -Force
    Compress-Archive -Path (Join-Path $symbols "llamatron.pdb") -DestinationPath (Join-Path $out "LlamasoftOnTheST-$version-symbols.zip") -Force
    Remove-Item -Recurse -Force (Split-Path $stage)
    Write-Host "release: $zip"
}
