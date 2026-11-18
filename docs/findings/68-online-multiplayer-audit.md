# 68. Online multiplayer: an audit of what exists and what it would take

**The question.** Can the port's four local players become players on
different computers (LAN, Direct IP, Steam), and on what? This finding
records the audit; the detail and the proposal are in
[docs/multiplayer/](../multiplayer/README.md). No code was changed and
nothing was run on the game for it.

## 1. How it was looked at

* `main` at merge #36 read for every system an online mode touches:
  `src/players/`, `src/input/`, `src/saves/save_library.cpp` (the XInput
  wrapper `sub_824742F0`), `src/input_record.*`, `src/fixed_step.*`,
  `src/debug_input_script.*`, `src/gameplay/`, `src/game_bugs/`, and
  findings 07, 22, 26, 28, 29, 32, 54, 62, 66.
* The open pull requests' branches fetched (`refs/pull/*/head`): #24 adds a
  protocol codec and a remote-pad handoff (not merged; its finding number 59
  has since been used in `main`).
* The SDK's code generator read for how floating point is translated
  (`thirdparty/rexglue-sdk/src/codegen/builders/floating_point.cpp`,
  `vector.cpp`).
* GameNetworkingSockets' README / BUILDING / README_P2P and the Steamworks
  API pages read.

**The wrong turn worth writing down:** the obvious design for a
"host-authoritative" game (the host simulates, clients get snapshots of
players, enemies and objects) needs the layout and the ownership of every
piece of world state. The port knows the players' physics position (+140),
velocity (+104) and the co-op tables, and nothing comparable about enemies,
scripts or objectives. That design is a decompilation project before the
first shared enemy.

## 2. What was found

* **Input enters in one place.** Every controller state the game reads goes
  through `sub_824742F0`; the replay (`input_record::BeforeFilters`)
  already replaces whole sockets' states there, in game frames when the
  fixed step is on.
* **The same machine repeats itself.** With the fixed step from launch and
  the clocks fixed, two runs of a 44 s recording were identical frame for
  frame (findings/29 s.6).
* **The recompiled floating point has no CPU-dependent step**: `fmadd` is
  `std::fma`, the estimate instructions (`fres`, `frsqrte`, `vrefp`,
  `vrsqrtefp`) are exact divisions / square roots, vector multiply-add is a
  multiply then an add. Cross-machine determinism is plausible; it is not
  measured.
* **Known threats**: loading and sound on other threads, script waits on
  voices (the port's own `fix_speech_cut_off` and possibly the original's),
  wall-clock settings (`ground_grace_ms`), the shared random generator
  `0x825A14F4`, settings that change the simulation, and the port's input
  routes that bypass the pad (keyboard as events, mouse reticle and menus,
  name typing, window focus).
* **Steam** needs an App ID this port doesn't have, and the Steamworks SDK's
  licence next to GPL-3.0 is unresolved.

## 3. What it leads to

A host-led **lockstep** design (architecture.md): every machine runs the
game from the host's save with the same settings at a fixed step; each
machine sends its own seats' controller states per frame; every socket of
frame k gets the session's state at the replay's place; the host owns the
roster, the frame clock and the desync check.

## 4. Still open

1. **Experiment E1**: on one Windows PC done for walking, jumping,
   attacks, pick-ups and a titan fight (s.6), with one unexplained
   difference in 12 runs on one save; still to do: a level change, a
   cutscene, 10 minutes, two players, another CPU, Linux vs Windows
   (architecture.md s.8).
2. The main loop's "loading done" check (a frame-agreed level change).
3. Which game logic reads sound state.
4. Every caller of the random generator and its thread.
5. Whether GameNetworkingSockets links with the project's Clang on both
   systems (dependencies.md s.1).

## 5. The debug console on Windows

The exact-run tools (`replay`, `track`, `clock`: findings/29 s.6) live in
the debug console, which was a Unix socket only: nothing of E1 could run on
Windows. `--debug_console=tcp:<port>` now listens on **127.0.0.1 only** (the
console reads and writes game memory: nothing from the network may reach
it), on Linux and Windows; a path still means a Unix socket on Linux.

* `src/debug/console_socket.*`: the listening side alone (accept, lines in,
  replies out), so `winsock2.h` and its macros stay out of the console and
  the game code (AGENTS.md s.5); `ws2_32` linked on Windows.
* `tools/mom.py -s tcp:<port>`.
* Console commands split on spaces: a file path with a space in it (the
  default install path has several) can't be passed. Relative paths from
  the game's working folder work.

Tested: a stand-alone echo handler with `mom.py` on Linux; a mingw build of
the same file for Windows; the game built by the project's Clang on Windows
answering `state`, `clock`, `replay`, `track` and `players` over TCP.

## 6. E1 on one Windows computer

**Set-up.** The game built from the install's own source tree (the player's
`program\` and `user\` untouched); every run gets its own copy of one save
(`--user_data_root`), its own controls file and log, a shared test cache,
`--fixed_step=30 --ground_grace_ms=0 --audio_mute=true --load_save=1`, the
real disc files. A PowerShell script starts the game, waits until the
fixed step is on (a `replay` sent before it is a real-time replay: the
first try did that), sends `replay <file> onspawn` and `track <file> N`,
waits for the end, stops the process. Runs A then B one after the other,
then C and D **at the same time** (two game processes loading and playing
side by side, competing for the disc, the CPU and the GPU). `same.py`
compares two tracks row by row on the replay frame (the absolute frame
counts from launch and differs with the load time): every printed value
must be the same text, and the guest addresses are reported apart.

**Wrong turns** (each cost a run): the replay sent before the fixed step
was on counted real time; a track length counted from the first frame in
play (loading frames included) ended before the replay did; PowerShell
resolves an alias before a function (`Compare` ran `Compare-Object`) and an
`if` expression flattens a one-element list of lists.

**Results**, a save at the start of the game (Wumpa Island):

| Recording | Frames compared | A vs B | C vs D (same time) | A vs C, A vs D |
|---|---|---|---|---|
| a walk: run, jump, turn (`walk.txt`) | 131 x 4 players | identical | identical | identical |
| 40 s in circles: runs, jumps, light and heavy attacks, two mojos collected, ground lost and found many times (`circle.txt`) | 1,216 x 4 players | identical | identical | identical |

Identical means every column: positions, velocities and the drawn matrix
to 5 decimals, ground and steep contact objects, and the bodies' guest
addresses: even two processes loading at the same time built the same
heap.

These two runs had no enemies near the save. The world row of `track`
(below) was added for the enemies, then saves were scouted for one with
fighting near its start (`-Scout`: one run, its summary): a later save,
player 1 riding a titan (Roller) with enemies around, 21-23 other
damageable characters, was used for:

| Recording | Frames compared | A vs B | C vs D (same time) | A vs C, A vs D |
|---|---|---|---|---|
| `circle.txt` on the titan save | 1,216 x (4 players + the world row) | identical | identical | identical |

In that run the titan lost hitpoints (40 -> 37 of 100), two of the other
characters went (23 -> 21) and their hitpoints changed six times: the world
fingerprint (positions and hitpoints of every one of them) was the same on
every frame of all four runs.

A third save (another area, 14-22 other damageable characters coming and
going, one of them with a hitpoint pointer that reads as garbage) was run
three times, four runs each:

| Batch | A vs B | C vs D (same time) | A vs C | A vs D |
|---|---|---|---|---|
| 1 | identical | **world hash differs from replay frame 562 on** | identical | **differs from 562 on** |
| 2 | identical | identical | identical | identical |
| 3 | identical | identical | identical | identical |

**The one difference.** In batch 1, run D (one of the two loading at the
same time) parts from the other three at replay frame 562, the moment the
count of other characters goes from 14 to 15 (a new one appears), and stays
different to the end. The players' rows stay identical, and so do the
world row's count and its position and hitpoint sums. Only the hash that
includes each character's guest address and exact bits differs. Two
explanations fit, and that run can't tell them apart (the address-free
hash was added after it):

1. the character created at frame 562 got another heap address in run D
   (a different heap layout: the "guest heap order" threat of
   feasibility.md s.2); its position and health the same;
2. the character whose hitpoint pointer reads as garbage: in run D that
   memory held other bits (not game state, a stale read), and the sum of
   hitpoints, dominated by the garbage value, hides it.

11 of 12 runs on this save matched in every column. What decides it: run
the batch until it happens again with the address-free hash (vy:vz), and
dump each character (address, position bits, hitpoint pointer) at the
first differing frame.

Still not covered: a level change, a cutscene with dialogue, 10 minutes, a
second player, another CPU, Linux.

**The world row.** `track` now writes, every frame, a row with player 0:
how many other damageable characters the game updated last frame
(`cheats::ForEachDamageable`, the list the cheats already keep), the sums
of their positions and hitpoints, and a 64-bit FNV-1a hash of each one's
address, exact position bits and hitpoint bits.
