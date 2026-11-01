# Smoke test: every game headless with scripted pad input, from a fresh save folder each (Trip-A-Tron installs
# itself), to its play state; passes if play drew 50 frames a second and nothing crashed. The docs readers are
# started too. Screenshots and logs go to logs\smoke\. Run after changes to machine.cpp, the games' controls or
# the registry: powershell -ExecutionPolicy Bypass -File Tools\smoke_all.ps1 [-Exe <llamatron.exe>]
# (-Exe: e.g. the release zip unpacked somewhere else - then only its own game\ folder is found.)
param([string]$Exe = "")
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$exe = if ($Exe) { $Exe } else { Join-Path $repo "build\llamatron.exe" }
$out = Join-Path $repo "logs\smoke"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null

# name, program, scripted input, frames, what passing means (play = a "drawn 50.0 frames/s" line; runs = it
# ran to the end without crashing)
$tests = @(
    @("llamatron", "TRON1MEG.PRG", "--press=150:j1fire --press=3000:j0fire", 3600, "play"),
    @("gridrunner", "GRIDRUNR.PRG", "--press=600:mleft --press=1500:mleft --press=2000:mleft --press=2400:mleft", 3000, "play"),
    @("tripatron", "TRIP.PRG", "--press=1500:mleft:5 --press=1700:Escape --press=1800:grt:600 --press=1800:gl0@0.5:600", 2500, "runs"),
    @("rmc", "REVSTE1M.TOS", "--press=300:ga:5 --press=600:ga:5 --press=900:ga:5 --press=1200:ga:5 --press=1500:ga:5 --press=1800:ga:5 --press=2100:gl0@1:1000 --press=2100:grt:1000", 3200, "play"),
    @("andes", "ANDES.BOY", "--press=300:ga:5 --press=600:ga:1500 --press=600:gl0@1:1500", 2600, "play"),
    @("defender2", "DEFENDR2.PRG", "--press=1600:ga:3 --press=1900:ga:3 --press=1950:grt:600", 2700, "play"),
    @("hardcore", "HARDCORE.PRG", "--press=600:ga:5 --press=1500:ga:800 --press=1500:gl0@0.3:800", 2600, "play"),
    @("photonstorm", "PHOTON.YAK", "--press=1500:ga:5 --press=1700:gl0@0.9:600 --press=1700:ga:600", 2500, "play"),
    @("docs-rmc512", "DOX.TOS", "--press=300:gdown:40", 500, "runs"),
    @("docs-defender2", "READ_ME.PP", "--press=300:gdown:40", 500, "runs"),
    @("docs-hardcore", "DOC100E.PRG", "--press=400:B:5 --press=700:Down:4", 900, "runs")
)

$failed = 0
foreach ($t in $tests) {
    $name, $program, $input, $frames, $kind = $t
    $log = Join-Path $out "$name.log"
    $shot = Join-Path $out "$name.bmp"
    $args = "--headless --program=$program --userdata=`"$(Join-Path $out "userdata-$name")`" --log=`"$log`" $input --frames=$frames --shot=$($frames - 10):`"$shot`""
    if ($program -eq "DOX.TOS") { $args += " --disk=`"$(Join-Path $repo 'Private\new\rmc-st\DBUG028.MSA')`"" }
    $p = Start-Process -Wait -PassThru $exe -ArgumentList $args
    $text = if (Test-Path $log) { Get-Content $log -Raw } else { "" }
    $ok = $true
    $why = ""
    if ($text -match "CRASH|unhandled exception") { $ok = $false; $why = "crashed" }
    elseif ($text -notmatch "starting $([regex]::Escape($program))") { $ok = $false; $why = "didn't start (disk not found?)" }
    elseif ($text -match "ended after (\d+) frames" -and [int]$Matches[1] -lt $frames) { $ok = $false; $why = "ended early at frame $($Matches[1])" }
    elseif ($kind -eq "play") {
        $rates = [regex]::Matches($text, "drawn ([0-9.]+) frames/s while playing")
        if ($rates.Count -eq 0) { $ok = $false; $why = "never reached play" }
        elseif ([double]$rates[$rates.Count - 1].Groups[1].Value -lt 49.5) { $ok = $false; $why = "play drew $($rates[$rates.Count - 1].Groups[1].Value) frames/s" }
        else { $why = "play at $($rates[$rates.Count - 1].Groups[1].Value) frames/s" }
    } else { $why = "ran $frames frames" }
    if (-not $ok) { $failed++ }
    "{0,-16} {1,-5} {2}" -f $name, $(if ($ok) { "ok" } else { "FAIL" }), $why
}
if ($failed) { Write-Host "$failed failed (logs and screenshots in $out)"; exit 1 }
Write-Host "all passed (screenshots in $out)"
