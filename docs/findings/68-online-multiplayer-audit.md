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

1. **Experiment E1**: the same recording replayed on two PCs, then Linux vs
   Windows, then with enemies, a cutscene, 10 minutes (architecture.md s.8).
2. The main loop's "loading done" check (a frame-agreed level change).
3. Which game logic reads sound state.
4. Every caller of the random generator and its thread.
5. Whether GameNetworkingSockets links with the project's Clang on both
   systems (dependencies.md s.1).
