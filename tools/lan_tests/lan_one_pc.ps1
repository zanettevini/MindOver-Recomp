# tools/lan_tests/lan_one_pc.ps1 -- LAN co-op on ONE PC: this branch
# (built in <install>\mp\, README.md), two
# copies of the game in lockstep over 127.0.0.1. Host = player 1, guest =
# player 2. Each copy's own player is driven by a recording (replay): the
# host runs lan_host.txt, the guest lan_guest.txt (START joins, B leaves the
# mask). Both track both players every frame; the tracks are compared (both
# PCs must compute the same positions), and the session's own sync check
# (`net`: fingerprints compared every frame) is read on both.
param(
  [int]$Slot = 10,
  [int]$Frames = 3600,     # 60 fps: loading + 1.5 s lead + 40 s
  [string]$Tag = 'lan1',
  [string]$HostRec = 'lan_host.txt',
  [string]$GuestRec = 'lan_guest.txt',
  [int]$KillGuestAfterS = 0,   # > 0: the guest's game is killed this long after both drivers start (a lost PC)
  [int]$DropAfterS = 30,       # --net_drop_after_s on both
  [int]$ToMs = -1,             # replay only up to this ms of the recordings (-1 = all)
  [int]$WaitReplayMs = 900000, # how long the drivers wait for the recordings to end (a stuck load ends the run then)
  [switch]$FpLog,              # --debug_net_fingerprint_log on both (per-frame fingerprint parts)
  [string]$GameArgs = '',      # more game flags for both copies, separated by spaces (e.g. --simple_spinning=true)
  [string]$HostExtra = '',     # more console commands for the host after `track`, separated by ';' (e.g. wait ms 12000;key Q 400)
  [string]$Python = 'python'   # a Python 3 on PATH, or its full path
)
$ErrorActionPreference = 'Stop'
$tests   = $PSScriptRoot
$source  = Split-Path $tests -Parent
$install = Split-Path $source -Parent
$mp      = Join-Path $install 'mp'
$exe     = Join-Path $mp 'out\build\win-amd64-relwithdebinfo\crash_mom.exe'
$python  = $Python
$mom     = Join-Path $mp 'tools\mom.py'
$save    = Join-Path $install "user\saves\CrashMOM GameSlot $Slot.sav"   # read only: copied
$runs    = Join-Path $tests "runs\$Tag"
$cache   = Join-Path $tests 'cache-mp'
$report  = Join-Path $tests "$Tag-report.txt"
New-Item -ItemType Directory -Force $runs, $cache | Out-Null
"$Tag $(Get-Date -Format s): LAN on one PC, save $Slot, $Frames frames, $HostRec / $GuestRec, kill guest after $KillGuestAfterS s, drop after $DropAfterS s" | Set-Content $report

function Start-Copy($name, $port, $netArgs) {
  $dir = Join-Path $runs $name
  Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force (Join-Path $dir 'saves') | Out-Null
  Copy-Item $save (Join-Path $dir 'saves\CrashMOM GameSlot 1.sav')
  $args = @("--debug_console=tcp:$port", '--audio_mute=true', '--ground_grace_ms=0', '--load_save=1',
    "--game_data_root=`"$(Join-Path $install 'game')`"", "--user_data_root=`"$(Join-Path $dir 'saves')`"",
    "--cache_root=`"$cache`"", "--controls_file=`"$(Join-Path $dir 'controls.toml')`"",
    "--log_file=`"$(Join-Path $dir 'game.log')`"") + $netArgs
  if ($GameArgs) { $args += $GameArgs.Split(' ', [StringSplitOptions]::RemoveEmptyEntries) }
  if ($FpLog) { $args += "--debug_net_fingerprint_log=`"$(Join-Path $dir 'fingerprint.txt')`"" }
  $p = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory (Split-Path $exe) -PassThru
  "$name started: pid $($p.Id), console tcp:$port, $($netArgs -join ' ')" | Add-Content $report
  return $p
}

function Start-Driver($name, $port, $recording) {
  # Paths for the console are relative to the game's working folder (the
  # install path has spaces): <install>\mp\out\build\<preset> -> ..\..\..\..\
  $dir = Join-Path $runs $name
  $csv = "..\..\..\..\source\mp_tests\runs\$Tag\$name\track.csv"
  $rep = "..\..\..\..\source\mp_tests\$recording"
  $cmds = @('-I', "`"$mom`"", '-s', "tcp:$port", '--wait-socket', '600', 'state', '"wait ms 8000"', 'clock', 'net',
            $(if ($ToMs -gt 0) { "`"replay $rep 0 $ToMs onspawn`"" } else { "`"replay $rep onspawn`"" }), "`"track $csv $Frames`"") + $(if ($name -eq 'host' -and $HostExtra) { $HostExtra.Split(';') | ForEach-Object { "`"$_`"" } } else { @() }) + @("`"wait replay $WaitReplayMs`"",
            '"wait ms 4000"', 'track', 'net', 'players')
  $d = Start-Process -FilePath $python -ArgumentList $cmds -NoNewWindow -PassThru `
    -RedirectStandardOutput (Join-Path $dir 'console.txt') -RedirectStandardError (Join-Path $dir 'console-errors.txt')
  $null = $d.Handle
  return $d
}

$host_ = Start-Copy 'host' 47201 @('--net_host', '--net_port=37373', "--net_drop_after_s=$DropAfterS")
Start-Sleep 2
$guest = Start-Copy 'guest' 47202 @('--net_join=127.0.0.1', '--net_port=37373', "--net_drop_after_s=$DropAfterS")
$dh = Start-Driver 'host' 47201 $HostRec
$dg = Start-Driver 'guest' 47202 $GuestRec
if ($KillGuestAfterS -gt 0) {
  Start-Sleep $KillGuestAfterS
  "guest's game killed at $(Get-Date -Format s) (a PC that stops answering)" | Add-Content $report
  Stop-Process -Id $guest.Id -Force
}
$dh.WaitForExit(); $dg.WaitForExit()
"host console exit code: $($dh.ExitCode)" | Add-Content $report
"guest console exit code: $($dg.ExitCode)" | Add-Content $report
foreach ($p in @($host_, $guest)) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }

"--- host vs guest (both players, every frame)" | Add-Content $report
& $python -I (Join-Path $tests 'same_track.py') (Join-Path $runs 'host\track.csv') (Join-Path $runs 'guest\track.csv') 2>&1 | Add-Content $report
foreach ($n in @('host', 'guest')) {
  "--- ${n}: players at the end" | Add-Content $report
  Get-Content (Join-Path $runs "$n\console.txt") | Select-String -Pattern '^p[12]:' | ForEach-Object { $_.Line } | Add-Content $report
  "--- ${n}: net status at the end" | Add-Content $report
  Get-Content (Join-Path $runs "$n\console.txt") | Select-String -Pattern 'net|session|drift|sync|DRIFT' | ForEach-Object { $_.Line } | Add-Content $report
  "--- ${n}: log lines about the session" | Add-Content $report
  Get-Content (Join-Path $runs "$n\game.log") | Select-String -Pattern 'Network:|DRIFT|Co-op: states|Co-op: controller|drops out|Drop|KBM: Spin|for the helper threads|CRASH' | Select-Object -First 80 | ForEach-Object { $_.Line } | Add-Content $report
}
if ($FpLog) {
  "--- fingerprint logs: first difference" | Add-Content $report
  & $python -I (Join-Path $tests 'fp_first_diff.py') (Join-Path $runs 'host\fingerprint.txt') (Join-Path $runs 'guest\fingerprint.txt') 2>&1 | Add-Content $report
}
"done $(Get-Date -Format s)" | Add-Content $report
Write-Host "Pronto: $report"
