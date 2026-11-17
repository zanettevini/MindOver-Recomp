# Online multiplayer, part 1: what already exists

Phase 1 of the online multiplayer audit (index: [README.md](README.md)).
This page lists every existing system an online mode would build on, where
it lives, what it does today and whether it can be reused. Each claim
points to the source file or finding it was read from. Nothing here was
run on the game for this audit: it is a reading of `main` at merge
#36 (`6ccc967`) and of the open pull requests.

Legend for the "Reuse" column:

| Mark | Meaning |
|---|---|
| **complete** | does what the online mode needs as it is |
| **extend** | the right place, needs a session-aware branch |
| **partial** | covers part of the need |
| **experimental** | exists, documented as needing more testing |
| **none** | nothing exists yet |

## 1. Repository state

| Item | State |
|---|---|
| Branch audited | `main` at `6ccc967` (merge of PR #36), 306 commits |
| Networking code in `main` | **none**. No socket, Steam, ENet or GameNetworkingSockets code or dependency (grep of `src/`, `CMakeLists.txt`, `launcher/`) |
| Xbox networking | The game's one network import is answered by the port: README "Going native", row *Network 1/1, not needed (no online features)*. Nothing to restore |
| Open PR #24 *Add initial co-op network foundations* | Not merged. Adds `src/network/protocol.*` (protocol v1 codec, 22-byte header, payload up to 64 bytes), `src/network/remote_input.*` (four remote pad slots applied in the XInput wrapper), a remote-owned bit in `PlayerAssignment::PresentMask`, two unit tests behind `CRASHMOM_BUILD_TESTS`, `docs/multiplayer/README.md` and **finding 59**, a number `main` has since given to `59-native-only-gpu-thread.md`. Section 9 below reviews it |
| Other open PRs | #4 and #19 (Windows build dependencies), #12 (Discord Rich Presence), #26 (a crash report finding), #28 (subtitle editor on Windows). #4 / #19 matter for adding a dependency to the Windows build |
| Merged PRs relevant here | #15 (controllers read by the port through SDL, `src/input/native_controllers.*`), #21 (native quit and launch) |
| Agent rules | [AGENTS.md](../../AGENTS.md): findings for every investigation, heavy comments, no edits in `generated/` or the SDK, `GuestPtr` for guest memory, keep the original's defaults, game-look UI with `tr()` text, up to four players, saves never moved or deleted |

## 2. Players and slots

| Need | Where | What it does | Reuse |
|---|---|---|---|
| Up to four players | `src/players/more_players.*`, [findings/26](../findings/26-more-local-players.md) s.8-10, s.15 | The game's two-entry per-player tables grown to four (state `0x8259B11C` in place, starting state moved, starting sub-state `0x824F3C38` in place, controllers at front end `+8524`, characters `0x8259B1C0`, game object `+16..+31` actors and jacked titans); players 3-4 spawned from a copy of player 2's `CActionSpawnPlayer`. Tables always moved; spawns depend on `--local_players` (2-4) | **complete** for slots |
| The limit of four | same, plus [findings/26](../findings/26-more-local-players.md) s.64 | Four comes from the 360's four XInput sockets (users 0-3), the tables sized four, the front end's four-controller loop, the HUD and markers built for four, and the underground renderer's two slots widened to cover more Crashes. No part is sized for five. | **hard limit 4** |
| Player identity | `src/input/players.h` | "Socket N = the game's player N, always" (findings/26 s.21). Devices are keyed by SDL GUID (`pad:<guid>[:n]`) or `keyboard`. There is no player identity apart from the socket number | **extend**: a network seat must map to a socket |
| Join, drop out | `MorePlayersJoinPick` hook on `sub_82264988`; `lost_controller::DropOut` (= `sub_82266040` with the menu owner set); `src/players/pause_drop_out.*` | The game's own drop-in START join; socket N joins as player N; Back in the pause menu drops a player out; the last player in game can't drop out | **complete**, driven by input |
| A player without a physical controller | `src/debug_input_script.*` (`ScriptedInputDriver`), `src/players/dummy_players.*`, findings/26 s.7, s.49 | Fake controllers are a `rex::input::InputDriver` of synthetic devices, assigned to sockets like real pads; dummy players drive them each frame with analog state (`SetLiveState`), including *copy player N* from `input_record::LastRead`. Tested (findings/26 s.49): a dummy copying player 1 matched his run | **complete** as proof that a socket can be fed by a non-device source |
| A device lost | `src/players/lost_controller.*`, findings/26 s.33, s.58, s.62 | Every frame, a player in game with no device pauses the game with the game's own question box: reconnect, or any player drops them out | **extend**: "no device" must mean "the session says this seat has no input" |
| The Players screen | `src/players/players_setup.*` (rules), `src/options/options_menu.cpp` (drawing), findings/26 s.53-54, s.59 | Four cards, press any button to join, X = move, Y = remove, button pictures per player. Global (controls.toml) | **extend**: show local / remote / host, lock device moves for remote seats |
| "Who plays" without profiles | `src/players/who_plays.*`, findings/32 | Game fields `+352/+360/+364` hold "which players have a controller"; changes come from `PlayersChanged()` | **extend** (PR #24 already ORs remote seats into the present mask) |
| Menus owned by a player | `src/players/menu_input.cpp`, findings/26 s.21 | Pause menu owner `+8536`, the totem's flow owned by who used it, the level-up screen answers everyone | **complete**: it decides from input, so it behaves the same on every machine that gets the same input |
| Characters and outfits | `src/players/outfits.*`, `outfit_picker.*`, `outfit_previews.*`, findings/46 | Character number per player (`0x8259B1C0`: 0 Crash, 9 Coco), outfits chosen at the chest per player, stored per save | **complete**; outfit choice is gameplay state and travels with the save |
| HUD, markers, paws | `more_players_hud.cpp`, `more_players_markers.cpp`, `more_players_loading.cpp` | Portraits, health, combo meters, P3/P4 markers, loading paws for four | **complete** (display only) |

## 3. Input path, in the order the game sees it

The game asks for each socket's controller through its XInputGetState
wrapper `sub_824742F0` (r3 = socket, r4 = `XINPUT_STATE`), called only by
Radical's input manager poll `sub_8235D188`. The port overrides it in
`src/saves/save_library.cpp` (around line 1787). In order:

1. `native_controllers::Read(user)` (SDL pads and the synthetic drivers'
   devices merged per socket by `PlayerAssignment::DevicesForUser`), or the
   SDK's path when `native_controller_state` is off.
2. Keyboard and mouse merged in for the socket the keyboard answers on
   (`KeyboardMouseDriver::MergeInto`; the keyboard is always player 1,
   `players.h`).
3. `input_record::BeforeFilters`: **a running replay replaces the state of
   every player it has lines for**; real and fake devices for that player are
   ignored ([findings/29](../findings/29-debug-console.md) s.3, s.6).
4. Game input blocked while the rename box has it.
5. `lost_controller::FilterPad`, `outfit_picker::FilterPad`,
   `title_any_button::FilterPad`.
6. `input_record::AfterFilters`: the session recording (what the game gets).
7. `options_menu::SeePad` / `FilterPad`, `prompt_icons::SeePad`.
8. `game_mods::FilterPad` (simple / keep spinning add LB and attack presses).
9. The keyboard's merged buttons, triggers and sticks taken back out again
   when `keyboard_events` is on ([findings/62](../findings/62-input-events.md) s.7-8).

Then Radical's input objects turn the state into value channels, and once a
frame the input maps turn channels into EVENTS ("did the player jump?",
findings/62 s.1).

**Inputs that do not travel through the pad state** (each one reads a local
device or a local clock and writes game memory, so each one is a source of
difference between two machines):

| Route | File | Reads |
|---|---|---|
| Keyboard as events (shadow input maps OR'ed into the real ones) | `src/input/key_events.*` | local keyboard |
| Mouse aiming reticle | `src/input/mouse_reticle.*` | local mouse |
| Mouse in menus (selection set directly) | `src/input/mouse_menus.*`, `mouse_screens.*`, `mouse_minigame.cpp` | local mouse |
| Typing a name | `src/input/name_typing.*` | local keyboard |
| Spin key answering the "stick circle done" question | `src/input/keyboard_mouse.*`, findings/38 s.4 | local keyboard |
| A controller that plays as nobody (Players screen) | `PlayerAssignment::ProbeState` | local pad |
| Losing the window focus | `src/input/background_input.*` | local window state |
| Movie skip, title "any button" | `movie_skip.*`, `title_any_button.*` | local pads |

## 4. Timing, repeatability and test tools

| Need | Where | What it does | Reuse |
|---|---|---|---|
| A fixed simulation step | `src/fixed_step.*`, findings/29 s.6 | `--fixed_step=N`: the main loop's delta (`CrashMomPassDelta`, `0x8227B04C`) answered as 1/N s, every pass runs one game frame, the game's three clocks (`sub_8235AAE0`, `sub_8235AB58`, `sub_8235ABC8`) answer fixed-step time from a fixed start value; `Frame()` = the game frame; `SetPassListener` runs at the start of every pass | **complete**: the frame counter and the place to wait for a frame's inputs |
| Recording what the game reads | `src/input_record.*` | One line per change of each socket's state; `LastRead(player)` | **complete** |
| Feeding recorded states back | `input_record::StartReplay` | Replaces whole sockets' states, in game frames when the fixed step is on, aligned to a 30 Hz grid; `onspawn` starts 1.5 s of game time after player 1's Crash is created; the pads' poll rhythm (`sub_822744F0`, a 16 ms test) restarted at the first frame | **extend**: a live network-fed replay is the lockstep input path (architecture.md s.4) |
| Proof of repeatability | findings/29 s.6 | **Same machine:** two 30 fps runs of a 9.5 s walk identical in all 286 frames; two 180 fps runs of a 44 s recording identical frame for frame, once the clocks, the start frame and the poll rhythm were fixed | **evidence**, see feasibility.md s.2 |
| Per-frame state dump and comparison | console `track`, `tools/compare_runs.py` | One CSV row per player per frame: physics position (+140), velocity (+104), ground contacts; the compare tool lines two runs up on game time | **extend** into a desync checker |
| Debug console | `src/debug_console.*`, `tools/mom.py`, findings/29 | Game state, players, memory read / eval, `who` (writers of an address), `goto`, `wait level`, `photo`, `profile` | **complete** for testing two instances |
| Jump into a save | `--load_save=<n|name|last>`, `src/saves/` | Starts straight in a save | **complete**: the same start state on two machines |
| Restart with other arguments | `src/app_restart.*` | Re-launches the process with the same command line (drops `--load_save`) | **extend**: restart into a session with the host's save and the fixed step |

## 5. Camera

| Need | Where | What it does | Reuse |
|---|---|---|---|
| A shared camera for all players | `src/players/coop_camera.*`, [findings/28](../findings/28-coop-camera.md) | Keeps the game's camera volumes, changes where it looks (centre of players in game, group focus) and how far back it sits (`CoopCameraDistance` midasm hook). Experimental, on by default, `--coop_camera=false` = original | **complete**: one screen, one camera for every player; it reads positions from the game, so it needs nothing from the network if the game state is the same |
| The camera's player | findings/26 s.38, s.43 | Focus at game object `+40`; checks widened to players 3-4 | **complete** |
| Zones that run | findings/26 s.42 | A zone's actors update only while a camera-followed player is in it (`sub_822EFFD8`); the co-op camera adds every player in game | note: **the camera's settings change what the world simulates** (feasibility.md s.3) |

## 6. World, saves, settings

| Item | Where | Note |
|---|---|---|
| Saves | `src/saves/save_library.cpp`, findings/24, 32 | Plain `.sav` files, the 20,174-byte block (format word `0x4ECE`). A save is the obvious "same start state" to send from host to client |
| The game's random generator | `0x825A14F4` (findings/66) | Used by at least the camera shake's starting phase: settings that call it change the sequence every later caller gets |
| Gameplay-changing port settings | `src/gameplay/game_mods.cpp` (`friendly_fire`, `friendly_hits_break_combo`, `simple_spinning`, `keep_spinning`), `src/gameplay/reduce_motion.cpp` (`reduce_motion`: pick-ups collected at once), `src/gameplay/camera_shake.cpp` (`camera_shake`), `src/ground_physics.cpp` (`ground_grace_ms` counts **wall-clock** ms, `platform_move_fix`), `src/game_bugs/speech_waits.cpp` (`fix_speech_cut_off`: script events wait while a voice plays, bounded by a wall-clock limit), `--local_players`, the co-op camera's settings, `fps_cap` | All must be the same on every machine of a session, or fixed by the session |
| Cheats | `src/cheats/` (F5) | Write game state directly |

## 7. Platform and build

| Item | State |
|---|---|
| Compilers | Clang 20+ on Linux and Windows (Windows uses Visual Studio's libraries), C++23, CMake 3.25+, Ninja ([docs/01-building.md](../01-building.md)). GCC not supported |
| x86-64 flags | `-msse4.1` (SDK `rexglue_apply_target_settings`); arm64 presets use `-march=armv8-a` |
| Floating point in the recompiled code | Checked in the SDK's code generator (`src/codegen/builders/floating_point.cpp`, `vector.cpp`): `fmadd(s)` -> `std::fma` (rounded once by definition; `fmadds` rounds again to float), `fres` / `frsqrte` / `vrefp` / `vrsqrtefp` -> exact divisions and square roots instead of hardware estimates, `vmaddfp` -> multiply then add (simde). No instruction depends on a CPU-specific estimate |
| Windows release | Ready-built by the installer pipeline (`launcher/`, `tools/`); a new library must be built for it (open PRs #4 / #19 touch these dependencies) |
| Threads | The input system calls in from game threads; `PlayerAssignment` has one mutex. Loading, streaming and the sound mixer run on other guest threads, outside the fixed step |

## 8. Debug and diagnostics

`--debug_coop_trace` (state changes, joins, controller wiring),
`--debug_menu_input_trace`, `--debug_coop_camera_trace`, crash reports in
`user/logs/crash-*.txt`, the session log and its `-inputs.txt`, the Xbox
calls meter (Linux). All usable unchanged on each instance of a test.

## 9. Open PR #24, reviewed against this audit

What it gets right and keeps: no guest pointers on the wire, explicit
big-endian fields, network threads never touching guest memory, the
remote seat counted in the present mask, slot ownership taken from the
connection rather than the packet.

What does not fit the architecture proposed here (architecture.md):

1. **Where the state lands.** `remote_input::Apply` writes a remote socket's
   state into the wrapper *after* the device read. For lockstep every socket,
   local ones included, must get the same per-frame value on every machine,
   before every filter: that is exactly what `input_record::BeforeFilters`
   does for a replay (s.3, step 3).
2. **`Input` has no seat.** `Welcome` can give a peer several seats, but an
   `Input` message cannot say which one it is for.
3. **"Newest state wins"** is the right rule for snapshots, not for lockstep,
   where every frame's input must arrive and be applied on that frame.
4. **Payload cap of 64 bytes** is too small for settings, the save file (20,174
   bytes) and redundant input frames.
5. **Finding 59** is taken in `main`; a revived PR needs the next free number.
6. Tests: the codec tests are worth keeping as they are.
