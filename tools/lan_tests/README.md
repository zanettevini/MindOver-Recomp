# LAN co-op on one Windows PC (findings/68 s.6h)

Two copies of the game in one LAN session over 127.0.0.1: the host plays
player 1, the guest player 2, each one's own player driven by a recording
(console `replay`), both tracking both players every frame (`track`).

| File | What |
|---|---|
| `lan_one_pc.ps1` | starts host and guest (`--net_host` / `--net_join=127.0.0.1`, each with its own copy of one save, controls file, log), drives them through the debug console (`--debug_console=tcp:<port>`), compares the two tracks, collects `net` status and the session's log lines; `-KillGuestAfterS N` kills the guest's game (a lost PC), `-FpLog` turns on `--debug_net_fingerprint_log` on both and names the first differing frame; `-GameArgs "--flag=x ..."` adds flags to both copies; `-HostExtra "wait ms 12000;key Q 400"` sends more console commands to the host after `track` (e.g. keyboard keys: the Spin key test of findings/68 s.6i) |
| `play_one_pc.ps1` | the same two copies in windows for a person to PLAY (no recordings): host = player 1 with sound, guest = player 2, the newest save (or `-Slot N`), each copy with its own controls file (F6 -> Players: give each window its own device); when both windows close, the session's log lines and the verdict (drifted or not) go to `jogar-report.txt` |
| `same_track.py` | two `track` files identical row by row? |
| `fp_first_diff.py` | the first frame where two fingerprint logs differ |
| `lan_host.txt`, `lan_guest.txt` | 40 s: the host runs circles; the guest presses START at 3 s (joins as the mask), B at 5 s (on foot), then circles the other way |
| `lan_host_5min.txt`, `lan_guest_5min.txt` | 5 minutes of seeded random play (runs, jumps, light / heavy attacks, B), the guest joining the same way |

Layout it expects (the Windows install of 0.2.2): the game built from this
branch in `<install>\mp\` (`cmake --build --preset win-amd64-relwithdebinfo`),
these files in `<install>\source\mp_tests\`, the player's saves in
`<install>\user\saves\` (only read: each copy gets its own copy). Paths the
game's console receives are relative to the game's working folder, because
console commands split on spaces and the install path has them.

```
powershell -ExecutionPolicy Bypass -File lan_one_pc.ps1 -Slot 10 -Tag lan4 -Frames 8500 `
  -HostRec lan_host_5min.txt -GuestRec lan_guest_5min.txt -ToMs 120000 -FpLog -Python C:\path\to\python.exe
```
