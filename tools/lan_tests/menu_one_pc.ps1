# tools/lan_tests/menu_one_pc.ps1 -- the Multiplayer menu test (findings/68
# s.6l): two copies booted normally (no --net_* flags); each one's menus
# driven by menu_drive.py (host / guest) one checked press at a time, as a
# player would: Extras -> Multiplayer -> Host a Game / the game found -> Join,
# then Load Game on both, player 2 drops in with START, the session's sync.
param([string]$Tag = 'menu1', [int]$Slot = 10, [string]$GameArgs = '')
$ErrorActionPreference = 'Stop'
$tests   = $PSScriptRoot
$install = Split-Path (Split-Path $tests -Parent) -Parent
$exe     = Join-Path $install 'mp\out\build\win-amd64-relwithdebinfo\crash_mom.exe'
$python  = (Get-Command python).Source  # or the full path of python.exe
$save    = Join-Path $install "user\saves\CrashMOM GameSlot $Slot.sav"
$runs    = Join-Path $tests "runs\$Tag"
$cache   = Join-Path $tests 'cache-mp'
$report  = Join-Path $tests "$Tag-report.txt"
New-Item -ItemType Directory -Force $runs, $cache | Out-Null
"$Tag $(Get-Date -Format s): the Multiplayer menu, save $Slot" | Set-Content $report
function Start-Copy($name, $port, $extra) {
  $dir = Join-Path $runs $name
  Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force (Join-Path $dir 'saves') | Out-Null
  Copy-Item $save (Join-Path $dir 'saves\CrashMOM GameSlot 1.sav')
  $args = @("--debug_console=tcp:$port", '--fullscreen=false', '--window_width=960', '--window_height=540',
    "--game_data_root=`"$(Join-Path $install 'game')`"", "--user_data_root=`"$(Join-Path $dir 'saves')`"",
    "--cache_root=`"$cache`"", "--controls_file=`"$(Join-Path $dir 'controls.toml')`"",
    "--log_file=`"$(Join-Path $dir 'game.log')`"") + $extra
  if ($GameArgs) { $args += $GameArgs.Split(' ', [StringSplitOptions]::RemoveEmptyEntries) }
  return Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory (Split-Path $exe) -PassThru
}
$h = Start-Copy 'host' 47201 @()
Start-Sleep 2
$g = Start-Copy 'guest' 47202 @('--audio_mute=true')
$dh = Start-Process -FilePath $python -ArgumentList @('-I', "`"$(Join-Path $tests 'menu_drive.py')`"", '47201', 'host', "`"$(Join-Path $runs 'host\drive.txt')`"", "`"$(Join-Path $runs 'host\game.log')`"") -NoNewWindow -PassThru -RedirectStandardError (Join-Path $runs 'host\drive-errors.txt')
$dg = Start-Process -FilePath $python -ArgumentList @('-I', "`"$(Join-Path $tests 'menu_drive.py')`"", '47202', 'guest', "`"$(Join-Path $runs 'guest\drive.txt')`"", "`"$(Join-Path $runs 'guest\game.log')`"") -NoNewWindow -PassThru -RedirectStandardError (Join-Path $runs 'guest\drive-errors.txt')
$null = $dh.Handle; $null = $dg.Handle
$dh.WaitForExit(); $dg.WaitForExit()
Start-Sleep 2
foreach ($p in @($h, $g)) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
foreach ($n in @('host', 'guest')) {
  "--- ${n}: the drive" | Add-Content $report
  Get-Content (Join-Path $runs "$n\drive.txt") | Add-Content $report
  Get-Content (Join-Path $runs "$n\drive-errors.txt") | Add-Content $report
  "--- ${n}: log" | Add-Content $report
  Get-Content (Join-Path $runs "$n\game.log") | Select-String -Pattern 'Network:|Online:|Options: main menu|Main menu:|Options: screen|DRIFT|CRASH|found a game|looking for|SESSION' | Select-Object -Last 80 | ForEach-Object { $_.Line } | Add-Content $report
}
"done $(Get-Date -Format s)" | Add-Content $report
