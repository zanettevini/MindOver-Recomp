# tools/e1/e1_runs.ps1 -- multiplayer test E1 on one Windows PC (docs/multiplayer/architecture.md s.8,
# findings/68). Runs the test build (source\out\build\...\crash_mom.exe) with
# one save and one replay at a fixed step, tracks every player AND a
# fingerprint of every other damageable character (enemies...) on every game
# frame, and compares the tracks:
#   A then B one after the other, then C and D AT THE SAME TIME.
# -Scout: one run only (A), to see what a save has around it (enemies?).
# Where: copy this folder to <install>\source\mp_tests\ (it finds the
# install, the test build and tools\mom.py from there), build the game in
# source\out\build\win-amd64-relwithdebinfo first.
# Nothing in the player's own user\ folder is changed: every run gets its own
# copy of one save, its own controls file and log, inside mp_tests\runs\<tag>\.
param(
  [int]$Slot = 17,                  # which of user\saves\CrashMOM GameSlot N.sav (copied)
  [string]$Replay = 'circle.txt',   # in mp_tests\
  [int]$Frames = 1600,              # track length (loading frames in play + 1.5 s lead + the replay)
  [string]$Tag = 'e1',
  [switch]$Scout,
  [string]$Python = 'python'        # a Python 3 on PATH, or its full path
)
$ErrorActionPreference = 'Stop'
$tests   = $PSScriptRoot
$source  = Split-Path $tests -Parent
$install = Split-Path $source -Parent
$exe     = Join-Path $source 'out\build\win-amd64-relwithdebinfo\crash_mom.exe'
$python  = $Python
$mom     = Join-Path $source 'tools\mom.py'
$save    = Join-Path $install "user\saves\CrashMOM GameSlot $Slot.sav"   # read only: copied
$runs    = Join-Path $tests "runs\$Tag"
$cache   = Join-Path $tests 'cache'
$report  = Join-Path $tests "$Tag-report.txt"

New-Item -ItemType Directory -Force $runs, $cache | Out-Null
"$Tag $(Get-Date -Format s): save $Slot, replay $Replay, $Frames frames" | Set-Content $report

function Start-Run($name, $port) {
  $dir = Join-Path $runs $name
  Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force (Join-Path $dir 'saves') | Out-Null
  Copy-Item $save (Join-Path $dir 'saves\CrashMOM GameSlot 1.sav')
  $args = @(
    "--debug_console=tcp:$port", '--audio_mute=true', '--fixed_step=30', '--ground_grace_ms=0',
    '--load_save=1', "--game_data_root=`"$(Join-Path $install 'game')`"",
    "--user_data_root=`"$(Join-Path $dir 'saves')`"", "--cache_root=`"$cache`"",
    "--controls_file=`"$(Join-Path $dir 'controls.toml')`"", "--log_file=`"$(Join-Path $dir 'game.log')`"")
  $p = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory (Split-Path $exe) -PassThru
  "$name started: pid $($p.Id), console tcp:$port" | Add-Content $report
  return $p
}

function Start-Driver($name, $port) {
  # The console commands for one run, in a python process of its own (so two
  # runs can be driven at the same time). Wait for the fixed step, then the
  # replay (it waits for player 1's Crash), then track, then the replay's end.
  # Paths for the game's console are RELATIVE to the game's working folder
  # (the exe's): the install path has spaces and console commands split on them.
  $dir = Join-Path $runs $name
  $csv = "..\..\..\mp_tests\runs\$Tag\$name\track.csv"
  $rep = "..\..\..\mp_tests\$Replay"
  $cmds = @('-I', "`"$mom`"", '-s', "tcp:$port", '--wait-socket', '600', 'state', '"wait ms 6000"', 'clock',
            "`"replay $rep onspawn`"", "`"track $csv $Frames`"", '"wait replay 900000"',
            '"wait ms 3000"', 'track', 'players')
  $d = Start-Process -FilePath $python -ArgumentList $cmds -NoNewWindow -PassThru `
    -RedirectStandardOutput (Join-Path $dir 'console.txt') -RedirectStandardError (Join-Path $dir 'console-errors.txt')
  $null = $d.Handle
  return $d
}

function Stop-Run($p) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }

function Compare-Tracks($x, $y) {
  "--- $x vs $y" | Add-Content $report
  & $python -I (Join-Path $tests 'same_track.py') (Join-Path $runs "$x\track.csv") (Join-Path $runs "$y\track.csv") 2>&1 | Add-Content $report
}

function Summary($x) {
  "--- $x summary" | Add-Content $report
  & $python -I (Join-Path $tests 'same_track.py') --summary (Join-Path $runs "$x\track.csv") 2>&1 | Add-Content $report
}

$names = [System.Collections.ArrayList]::new()   # (an if-expression would flatten a one-run list)
[void]$names.Add(@('A', 47101))
if (-not $Scout) { [void]$names.Add(@('B', 47102)) }
foreach ($r in $names) {
  $p = Start-Run $r[0] $r[1]
  $d = Start-Driver $r[0] $r[1]; $d.WaitForExit()
  "$($r[0]) console exit code: $($d.ExitCode)" | Add-Content $report
  Stop-Run $p; Start-Sleep 3
}
if (-not $Scout) {
  $pc = Start-Run 'C' 47103; $pd = Start-Run 'D' 47104
  $dc = Start-Driver 'C' 47103; $dd = Start-Driver 'D' 47104
  $dc.WaitForExit(); $dd.WaitForExit()
  "C console exit code: $($dc.ExitCode)" | Add-Content $report
  "D console exit code: $($dd.ExitCode)" | Add-Content $report
  Stop-Run $pc; Stop-Run $pd
  Compare-Tracks 'A' 'B'; Compare-Tracks 'C' 'D'; Compare-Tracks 'A' 'C'; Compare-Tracks 'A' 'D'
}
Summary 'A'
"done $(Get-Date -Format s)" | Add-Content $report
Write-Host "Pronto: $report"
