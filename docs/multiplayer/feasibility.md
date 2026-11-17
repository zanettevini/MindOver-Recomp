# Online multiplayer, part 2: feasibility

Phase 1 of the online multiplayer audit (index: [README.md](README.md)).
What an online mode needs from the game, what the port already proves, and
what is unknown. Every row says whether it is **verified** (read in the
code or measured in a finding), an **assumption**, or **open** (needs an
experiment or reverse engineering).

## 1. The central question

Two computers, each running its own copy of the game, must show the same
world: the same Crashes in the same places, the same enemies, the same
pick-ups, the same story. There are three ways to get there:

| Way | What travels | What it needs from the game |
|---|---|---|
| **A. Lockstep** (input replication) | each player's controller state for each game frame | the game, given the same start and the same inputs on the same frames, computes the same world on both machines (**determinism**) |
| **B. State replication** (host-authoritative snapshots) | positions, animations, health, enemies, objects, story state | knowing every piece of world state well enough to read it on the host and overwrite it on the client, and stopping the client's own simulation of it |
| **C. Hybrid** | inputs, plus host corrections of a few known fields | determinism most of the time; corrections where it fails |

The prompt's preferred design is a host-authoritative listen server. In
this game that choice is decided by what the port knows:

* **For B**, the port knows a handful of fields: the players' physics
  position (+140) and velocity (+104) and ground contacts (findings/22,
  findings/29 s.6), the co-op tables (findings/26). It does not know the
  layout of enemies, their AI state, fight trees, projectiles, pick-ups,
  destructibles, the Lua scripts' state or the objectives. Each would be a
  reverse-engineering project, and the client's copy of each would have to
  be stopped from simulating. That is a decompilation-sized task (the
  roadmap's "Beyond") before the first enemy is shared. **Verdict: not
  feasible as a first approach.**
* **For A**, the port has already measured determinism on one machine
  (s.2) and has the tools to measure it across machines without new code.
  **Verdict: the only realistic first approach, gated by an experiment.**

The host stays the authority, but over the **session**, not over each
object: it admits peers, picks the save and the settings, owns the frame
clock, decides when players join, leave or drop, checks that every machine
computes the same state, and decides the recovery when they don't.
(architecture.md s.2.)

## 2. Determinism: what is known

| Fact | Status | Source |
|---|---|---|
| The game moves the world by the real time since the last frame | verified | findings/07, findings/29 s.6 |
| One place hands out that time (`0x8227B04C`); the fixed step answers it as 1/N s | verified | `src/fixed_step.*` |
| With the fixed step from launch, the three game clocks answering fixed time from a fixed start, `replay ... onspawn` and the pads' poll rhythm reset, **two runs of a 44 s recording at 180 fps were identical frame for frame** | verified, **same machine** | findings/29 s.6 |
| Two 30 fps runs of a 9.5 s walk identical in all 286 frames | verified, same machine | findings/29 s.6 |
| Recompiled floating point uses no CPU-specific estimates (`std::fma`, exact division / square root for the estimate instructions, SSE through simde) | verified in the code generator | existing-systems.md s.7 |
| The port's hand-written replacements (skinning, matrix helpers, memcpy, ...) are bit-identical to the originals | verified by their own checks | findings/57, findings/60 |
| Two different x86-64 machines, same build, same OS, give the same result | **open** (expected, not measured) | |
| Windows (UCRT `fma`, `sqrt`, MSVC libraries) and Linux (glibc) give the same result | **open**; `std::fma` and `sqrt` are required to be correctly rounded, so the same answer is expected, but the build differs (Windows' linker, CRT) | |
| arm64 builds match x86-64 | **open**, not expected to be checked first; simde maps SSE to NEON | |
| Longer runs: enemies, fights, titans, a level change, a cutscene | **open**: the 44 s run was a courtyard walk | |

### Known threats to determinism

| Threat | Why it matters | Status |
|---|---|---|
| **Other threads**: loading, streaming, the sound mixer | "Work on other threads (streaming, sound) doesn't follow the fixed step" (findings/29 s.6). The loading screen goes when the loading *thread* is done (10 or 11 frames after Crash appeared, even on one machine). Across machines with different disks the gap is larger | verified threat; a frame-agreed "load done" gate is needed (RE: the check the main loop makes) |
| **Speech timing** | Script events can wait for a voice to finish: the port's own `fix_speech_cut_off` holds `CActionTutorialMessage` while the speech bus plays, with a wall-clock limit (`src/game_bugs/speech_waits.cpp`). The voices are mixed on a guest thread paced by the PC's audio device (findings/54). Whether the original's scripts also wait on sound state elsewhere (cutscenes, dialogue) is unknown | verified for the port's fix; **open** for the original |
| **Wall-clock port settings** | `ground_grace_ms` counts wall-clock ms (findings/22); `speech_waits` caps a wait by wall clock | verified; both must be off or moved to game time in a session |
| **The shared random generator** (`0x825A14F4`) | Any extra or missing call shifts every later result. The camera shake setting already calls it (findings/66) | verified for camera shake; other callers open |
| **Settings that change the simulation** | see existing-systems.md s.6 (spinning, friendly fire, Reduce Motion pick-ups, camera reach and walls: the camera decides which zones run) | verified; the host must impose them |
| **Inputs that bypass the pad** | keyboard as events, mouse reticle, mouse in menus, name typing, Spin key, window focus (existing-systems.md s.3) | verified; each needs a session source or must be off in a session |
| **Guest heap order** | If two threads allocate in a different order on two machines, objects get different addresses; any logic ordered by address would differ | assumption; the same-machine identical runs are weak evidence against it |
| **The emulated GPU and the native renderer** | Whether game logic reads anything back from the GPU (queries, render results) | open; nothing found so far reads back, the native renderer is drawn from the game's calls |
| **Uninitialised host-side state in port code** | a port module reading a host variable that differs per machine | open; audit each port module that writes guest memory |

## 3. Per-system feasibility under lockstep

Under lockstep a system is "synchronized" if the game computes it from the
same inputs. The question per system is not "how do we send it" but "is
anything that feeds it local to one machine".

### Players

| System | Verdict | Notes |
|---|---|---|
| Movement, jumping, attacks, abilities, animations | feasible | Driven by input and physics; the 44 s identical run covers walking and jumping |
| Damage, health, death, respawn, masks, titans | feasible, untested | Game logic; the four-player paths exist (findings/26 s.12, s.22, s.23) |
| Joining and dropping out | feasible | The game's own START / Back, decided from input on the agreed frame |
| A peer's connection dropping | feasible with a change | Becomes "seat N has no input from frame F" on every machine; `lost_controller` then shows its box on every machine |
| Outfits, characters | feasible | Stored in the save; the chest picker reads pad state (`outfit_picker::FilterPad`) |

### World

| System | Verdict | Notes |
|---|---|---|
| Enemies, bosses, AI | feasible if deterministic | No state to send; risk = threads, random calls |
| Pick-ups, mojos, destructibles, hazards, physics | feasible if deterministic | Reduce Motion's "collect pick-ups at once" must match |
| Checkpoints, objectives, scripted events | feasible if deterministic | Scripts are Lua bytecode run on the main thread (assumption: no script runs on another thread) |
| Zones (which part of the level runs) | feasible | Decided by the camera's players; the co-op camera settings must match |
| Level changes and loading | **needs work** | The load-done moment must be agreed (s.2) |

### Game flow

| System | Verdict | Notes |
|---|---|---|
| Pause, menus in play | feasible | Pausing is input; menu ownership is decided from input (`menu_input.cpp`) |
| Cutscenes, dialogue | **open** | Depends on whether anything waits on the audio thread |
| Saving | needs a rule | Only the host writes a save; a client's copy is a scratch copy (players' saves are never touched, AGENTS.md s.6) |
| Title screen and main menus | out of scope for the first milestones | A session starts inside a level from the host's save |
| Port overlays (F1 tools, F5 cheats, F6 controls) | must be locked in a session | They write game state or change devices locally |

## 4. Systems that assume one player or one controller

From findings/26: the original assumed two players almost everywhere, and
the port has widened every site found (s.16 is an audit of every "for each
player" loop). Nothing in the online design needs more than the four the
port supports. What does assume a **local** device:

* the keyboard is always player 1 (`players.h`, findings/26 s.65): on a
  client the local player is not socket 1, so a client cannot use the
  keyboard until the keyboard routes take their input from the session;
* the Players screen, the lost-controller box and the "who plays" fields
  read local device presence (`PlayerAssignment`);
* outside play every device also answers on socket 1 (`players.cpp`).

## 5. Player configurations

The lockstep model distinguishes **machines** (peers), **connections** and
**seats** (sockets 1-4). Any split of the four seats over the machines is
the same problem for the game, because the game only ever sees four sockets.

| Configuration | Feasible | First milestone |
|---|---|---|
| 1 local + 1 remote | yes | **yes** |
| 2 local + 1 remote | yes | no |
| 1 local + 3 remote | yes | no |
| 2 local + 2 remote | yes | no |
| more than 4 in total | **no** (s.2 of existing-systems.md) | |

## 6. Network limits that are not the game's

| Topic | Fact |
|---|---|
| LAN discovery | Not provided by GameNetworkingSockets (its README does not list it); a small UDP broadcast / multicast announcer is needed. Broadcast does not cross subnets, VLANs, or Wi-Fi networks with client isolation; VPN overlays usually don't carry it either |
| Direct IP | Works on a LAN, over IPv6 when both sides have reachable addresses, over the internet only when the host's port is reachable (port forwarding, no carrier-grade NAT, firewall rule) |
| NAT traversal without Steam | GameNetworkingSockets' P2P needs a signaling service the project would run, STUN servers, and TURN relays for peers that can't traverse NAT (its README_P2P.md). Steam Datagram Relay is not in the open-source library |
| Lockstep cost | Every machine waits for the slowest one each frame; input delay grows with latency (architecture.md s.5) |

## 7. Steam

| Question | Answer |
|---|---|
| Does Steam networking need an App ID? | Yes. `SteamAPI_Init` needs an App ID; `480` (Spacewar) is Valve's test ID and is documented as not for shipping builds |
| Can this port get one? | Unlikely: the port is an unofficial build of a game whose rights it doesn't hold, built from each player's own disc |
| Licence | The project is GPL-3.0. The Steamworks SDK is proprietary and distributed under Valve's SDK agreement; linking it into a GPL-3.0 program needs the copyright holders' permission or a licence exception. This is a question for the maintainer, not settled here (not legal advice) |
| Verdict | **Steam Online is blocked** until those two questions have answers. The design keeps a place for a Steam transport (architecture.md s.3) so it can be added without touching the session layer |

## 8. What needs reverse engineering

1. The main loop's "loading done" check, so the frame play resumes on is
   agreed by every machine.
2. Everything that reads sound or voice state from game logic (cutscenes,
   dialogue, `CActionTutorialMessage`, `CActionPlayVillagerDialog`).
3. Every caller of the random generator at `0x825A14F4`, and which threads
   they run on.
4. Whether any script or game system runs on a thread other than the main
   thread.
5. More state for the desync checker: the enemies' list and positions
   (the zone lists, `src/world/zone_lists.cpp`, are a starting point),
   the objective state.

## 9. Technically infeasible (as of this audit)

* More than four players.
* Synchronising the world by snapshots in the near term (s.1, B).
* Steam networking without an App ID; SDR without Steamworks.
* LAN discovery across subnets or isolated Wi-Fi without extra setup.
* Client-side prediction / rollback: the port can't save and restore the
  whole game state within a frame (no save states).
