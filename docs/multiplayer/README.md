# Online multiplayer (LAN, Direct IP, Steam): audit and plan

Status: **Phase 1 (audit) done, nothing built.** The game has no online
play; this directory is the plan for adding it as a native PC feature, on
top of the port's four local players. The Xbox's own networking is not
restored.

| Page | What it answers |
|---|---|
| [existing-systems.md](existing-systems.md) | What the port already has that an online mode builds on (players, input path, fixed step, replays, camera, saves), with files and findings; a review of open PR #24 |
| [feasibility.md](feasibility.md) | What is known and unknown: why lockstep and not snapshots, what threatens determinism, per-system verdicts, Steam's blockers, what needs reverse engineering |
| [architecture.md](architecture.md) | The proposal: host-led lockstep, the transport interface, the session layer, the protocol, LAN discovery, phases, the first milestone, expected code changes, regressions |
| [dependencies.md](dependencies.md) | GameNetworkingSockets, Steamworks, ENet, platform sockets: licences, build, limits |

## In short

* **The approach:** every computer runs the whole game; only the players'
  controller states travel, one per player per game frame (lockstep). The
  host is the authority over the session (who plays, which save, which
  settings, the frame clock, desync checks), not over each object.
* **Why:** the port can't read and rewrite enemies, scripts and objects of
  this game (snapshots), but it already proved that the same start and the
  same inputs at a fixed step give identical runs **on one machine**
  (findings/29 s.6), and its replay path already feeds whole sockets in game
  frames.
* **The gate:** cross-machine determinism is not measured yet. Experiment
  E1 measures it with the tools that exist, before any network code.
* **Steam Online is blocked** (no App ID; GPL-3.0 vs the Steamworks SDK
  licence). LAN and Direct IP need no Steam and no internet.
* **First milestone (M1):** two PCs on a LAN, Direct IP, one level, two
  controllers, both Crashes moving independently on both screens, equal
  state hashes every frame for 10 minutes, a pulled cable handled by the
  existing lost-controller box.

Findings: [68](../findings/68-online-multiplayer-audit.md) records the
audit itself.
