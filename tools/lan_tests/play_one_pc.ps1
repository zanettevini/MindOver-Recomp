# tools/lan_tests/play_one_pc.ps1 -- LAN co-op on ONE PC, played by hand: two
# copies of this branch (built in <install>\mp\, README.md), host = player 1, guest =
# player 2, over 127.0.0.1. Both load the same copy of a save (the newest one
# in user\saves unless -Slot says which); the originals are only read.
# Each copy keeps its own controls file in mp_tests\jogar\ (F6 -> Players
# there is remembered for next time). When both windows are closed, the
# session's lines from both logs go to mp_tests\jogar-report.txt.
param([int]$Slot = 0)
$ErrorActionPreference = 'Stop'
$tests   = $PSScriptRoot
$install = Split-Path (Split-Path $tests -Parent) -Parent
$exe     = Join-Path $install 'mp\out\build\win-amd64-relwithdebinfo\crash_mom.exe'
$saves   = Join-Path $install 'user\saves'
$play    = Join-Path $tests 'jogar'
$cache   = Join-Path $tests 'cache-mp'
$report  = Join-Path $tests 'jogar-report.txt'
if (-not (Test-Path $exe)) { throw "Nao achei o jogo compilado: $exe (rode 7_rebuild_mp.bat)" }
if ($Slot -gt 0) { $save = Get-Item (Join-Path $saves "CrashMOM GameSlot $Slot.sav") }
else { $save = Get-ChildItem $saves -Filter 'CrashMOM GameSlot *.sav' | Sort-Object LastWriteTime -Descending | Select-Object -First 1 }
New-Item -ItemType Directory -Force $play, $cache | Out-Null
"jogar $(Get-Date -Format s): save '$($save.Name)'" | Set-Content $report

function Start-Copy($name, $port, $extra) {
  $dir = Join-Path $play $name
  $savesDir = Join-Path $dir 'saves'
  Remove-Item -Recurse -Force $savesDir -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force $savesDir | Out-Null
  Copy-Item $save.FullName (Join-Path $savesDir 'CrashMOM GameSlot 1.sav')
  $args = @("--debug_console=tcp:$port", '--load_save=1', '--fullscreen=false', '--window_width=960', '--window_height=540',
    "--game_data_root=`"$(Join-Path $install 'game')`"", "--user_data_root=`"$savesDir`"",
    "--cache_root=`"$cache`"", "--controls_file=`"$(Join-Path $play "$name-controls.toml")`"",
    "--log_file=`"$(Join-Path $dir 'game.log')`"") + $extra
  return Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory (Split-Path $exe) -PassThru
}

$h = Start-Copy 'host' 47201 @('--net_host', '--net_port=37373')
Start-Sleep 2
$g = Start-Copy 'guest' 47202 @('--net_join=127.0.0.1', '--net_port=37373', '--audio_mute=true')
Write-Host ''
Write-Host 'Duas janelas: HOST (player 1, com som) e GUEST (player 2, sem som).'
Write-Host 'Coloque uma ao lado da outra e NAO minimize nenhuma.'
Write-Host 'Teclado: vale para a janela em que voce clicou por ultimo.'
Write-Host 'Controle: vale para as DUAS janelas ao mesmo tempo, ate voce desligar'
Write-Host '          ele numa delas (F6 -> Players -> o controle -> Off).'
Write-Host 'Status da sessao: F1 -> Tools -> Network.'
Write-Host 'Quando terminar, feche as duas janelas do jogo; este aviso some sozinho.'
$h.WaitForExit(); $g.WaitForExit()
foreach ($n in 'host', 'guest') {
  "--- ${n}: linhas da sessao" | Add-Content $report
  Select-String -Path (Join-Path $play "$n\game.log") -Pattern 'Network:|Players:|Controls:' |
    ForEach-Object { $_.Line } | Select-Object -Last 60 | Add-Content $report
}
$drift = Select-String -Path (Join-Path $play '*\game.log') -Pattern 'DRIFTED' -List
if ($drift) { "RESULTADO: DESSINCRONIZOU ($($drift[0].Line))" | Add-Content $report }
else { 'RESULTADO: nenhuma dessincronizacao nos logs' | Add-Content $report }
Get-Content $report | Select-Object -Last 2
