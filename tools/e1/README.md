# E1: is the game repeatable? (tools for findings/68 s.6)

Online play by lockstep needs two computers to compute the same game from
the same save and the same inputs ([docs/multiplayer/](../../docs/multiplayer/README.md)).
These files measure it with the debug console (`--debug_console=tcp:<port>`)
and the fixed step, on Windows.

| File | What |
|---|---|
| `e1_runs.ps1` | runs the game 4 times (A, B one after the other; C, D at the same time) with one save and one replay, tracks every frame, compares; `-Scout` = one run, to see what a save has around it |
| `same_track.py` | `a.csv b.csv`: identical row by row? `--summary a.csv`: what one track saw |
| `walk.txt`, `circle.txt` | synthetic recordings (input_record format): a 14 s walk, 40 s of circles with jumps and attacks |

Use: copy the folder to `<install>\source\mp_tests\`, build the game there
(`cmake --build --preset win-amd64-relwithdebinfo` in Visual Studio's x64
environment), then

```
powershell -ExecutionPolicy Bypass -File e1_runs.ps1 -Slot 10 -Tag e1-save10 -Python C:\path\to\python.exe
```

The player's own saves are only read (each run gets a copy). The report is
`<tag>-report.txt`; tracks and logs are under `runs\<tag>\`.
