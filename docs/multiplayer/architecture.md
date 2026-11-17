# Online multiplayer, part 3: proposed architecture

Phase 1 of the online multiplayer audit (index: [README.md](README.md)).
A proposal for review, not a decision: nothing here is built. It follows
from [existing-systems.md](existing-systems.md) and
[feasibility.md](feasibility.md). Whether it can work at all is decided by
experiment E1 (s.8) before any invasive change.

## 1. The shape, in one picture

```
 machine A (host)                               machine B (client)
 ───────────────────                            ───────────────────
 local pads ─> capture seat 1 ─┐          ┌─ capture seat 2 <─ local pads
                               v          v
                     ┌──────────────── session layer ────────────────┐
                     │ roster (peers, seats), settings, save, frame   │
                     │ clock, input buffer per seat per frame,        │
                     │ desync hashes, join / leave / drop decisions   │
                     └────────────── transport (interface) ───────────┘
                         standalone (GNS or ENet)  │  Steam (blocked)
                               │                          │
 game frame k, socket N:  input_record's replay path: every socket gets
 the session's input for frame k, before every port filter (both machines)
                               v                          v
              the SAME recompiled game, fixed step, same save, same settings
                               v                          v
               same co-op camera, same HUD, same world on both screens
```

Each machine runs the whole game. Only the players' inputs travel, one
small message per frame. The host is the authority over the session.

## 2. Authority

| Decision | Who | How |
|---|---|---|
| Admission, build and protocol check | host | `Hello` / `Welcome` / `Reject` (s.6) |
| Which save, which settings | host | sent before start; client restarts into them |
| The frame clock and the start frame | host | `Start(frame)` after every peer reports "loaded" |
| Which peer owns which seat | host | seat table in the roster; inputs for a seat are accepted only from its owner's connection |
| Join, drop out, a peer lost | host | roster changes carry the frame they apply on |
| "Load done" after a level change | host | the frame every machine resumes on (s.4.4) |
| Is everyone still in sync | host | compares the state hashes every peer reports |
| Recovery from a desync | host | resend the save, restart everyone from it (s.4.6) |
| The gameplay itself | nobody sends it | every machine computes it from the same inputs |

A client can't change another seat's input, the roster or the start
state. It can lie about its own seat's input, which is the same power a
local player has; host-led lockstep is not cheat-proof and this design
doesn't claim it is.

## 3. Transport interface

The session layer talks to one small interface. Steam features that the
standalone library can't offer are a separate, optional interface.

```cpp
// network/transport.h (proposed)
struct ConnectionId { uint32_t value; };
enum class Delivery { kReliableOrdered, kUnreliable };

class Transport {
 public:
  virtual ~Transport() = default;
  virtual bool Start() = 0;                            // no game thread
  virtual void Stop() = 0;
  virtual bool Listen(const Address& bind) = 0;        // host
  virtual ConnectionId Connect(const Address& to) = 0; // client
  virtual void Close(ConnectionId, int reason) = 0;
  virtual bool Send(ConnectionId, Delivery, std::span<const uint8_t>) = 0;
  // Drains received messages and connection events (network thread).
  virtual void Poll(std::vector<Event>& out) = 0;
  virtual ConnectionStats Stats(ConnectionId) const = 0; // ping, loss, bytes
};

// Optional (Steam only): friends' identities, invites, relay addresses.
class SteamPeers { ... };
```

* `StandaloneTransport`: GameNetworkingSockets in standalone mode, or ENet
  (dependencies.md s.4 compares them). IPv4 and IPv6 addresses.
* `SteamTransport`: `ISteamNetworkingSockets` / `ISteamNetworkingMessages`,
  only in builds that opt in, only once feasibility.md s.7 is answered.
* `LanDiscovery` is not a transport: a separate UDP announcer / listener
  (s.7), so LAN works with either transport and without Steam.

Threads: the transport and the session's receive side run on one network
thread. The game thread only reads the session's input buffer (a mutex or a
lock-free ring per seat) and posts its own outgoing input. No network call
on the game thread, no guest memory on the network thread (as PR #24
already required).

## 4. The session layer

### 4.1 Peers, seats, players

```
Session
  peers[]   : {peer_id, connection, name, build, state (joining / ready / playing / gone)}
  seats[4]  : {owner peer_id or none, local_device_key on the owner}
  frame     : the game frame both machines are on (fixed_step::Frame())
  delay     : input delay in frames (s.5)
```

A seat is a socket (= the game's player N, existing-systems.md s.2). A peer
can own several seats (local co-op on that machine). The host is a peer
like the others and normally owns seat 1. On each machine the seats it
owns are read from its own devices through the existing
`PlayerAssignment` (so the Players screen still says which local pad is
which local seat); every other seat's device read is ignored.

### 4.2 Where inputs enter the game

In a session, on every machine, for every socket, every read of frame k
returns **the session's input for that seat and frame**, at the same point
the replay already uses: `input_record::BeforeFilters` in the XInput
wrapper (`src/saves/save_library.cpp`). Everything after it (lost
controller, outfit picker, game mods, the recording) then runs the same
way on both machines.

This is the replay mechanism with a live source: the replay already
replaces whole sockets' states in game frames, ignores the real devices of
those sockets and restarts the pads' poll rhythm on its first frame.

What each machine sends for its own seats: the state its own devices gave
for that seat *at capture time*, captured once per frame on the game thread
(the same 12-byte gamepad `input_record` writes), tagged with the frame it
is for (k + delay).

### 4.3 The frame gate

`fixed_step::SetPassListener` runs at the start of every main-loop pass.
In a session it:

1. captures the local seats' input and hands it to the network thread for
   frame `k + delay`;
2. checks that the inputs of frame `k` for every seat are present;
3. if not, waits on a condition variable, bounded (a few ms at a time);
   after a short time the port shows a "waiting for <player>" notice
   (game-look overlay); after a long time the host declares the peer lost
   (s.4.5).

This is the one place the game thread waits. It is unavoidable in lockstep
(every machine must have frame k's inputs before computing frame k) and
only happens when a packet is late. The network itself never runs on the
game thread.

### 4.4 Starting and level changes

**Start** (the recipe findings/29 s.6 found for identical runs):

1. Host picks a save; host and client compare builds and settings.
2. The host sends the save file and the session settings.
3. Every machine restarts (`app_restart`) with `--fixed_step=<N>` from
   launch, the session settings, and the received save as `--load_save`
   (the client's copy lands in a scratch folder: the client's own saves are
   never touched).
4. Each machine reports "spawned" when player 1's Crash exists (the replay's
   `onspawn` test); the host answers `Start(frame)`; inputs start on that
   frame, with the poll rhythm reset as the replay does.

**Level changes**: the load-done moment must be one frame on every machine
(feasibility.md s.2). Proposed: every machine reports "load done"; the
main loop's check is answered "not yet" until the host's `Resume(frame)`
arrives. Finding that check is reverse-engineering item 1 of
feasibility.md s.8. Until then, the first milestone stays in one level.

### 4.5 Joining, leaving, losing a peer

| Event | What happens |
|---|---|
| A seat's player presses START / Back | ordinary input: the game joins / drops them on the same frame everywhere |
| A peer joins mid-session | first version: not allowed; the host restarts the session with the new roster |
| A peer leaves or times out | the host broadcasts `SeatLost(seat, frame)`; from that frame the seat has no device on every machine, so `lost_controller` opens its box everywhere and any player can drop the seat out, as with a pulled controller |
| The host leaves | the session ends on every client; each client keeps its scratch copy only |

### 4.6 Desync detection and recovery

Every machine hashes, each frame, a fixed list of state: the frame number,
the random generator's word at `0x825A14F4`, each player's co-op state,
physics position and velocity (the fields `track` already writes),
later the enemies' positions. Every M frames each client sends its hashes
for the last M frames; the host compares. On a mismatch:

1. log both machines' `track` dumps around the frame (for a finding);
2. pause the game on both and say so;
3. recovery, first version: the host saves, the session restarts from that
   save (s.4.4).

## 5. Latency and input delay

| Link | Round trip | Delay at 60 Hz (frames) | Added feel |
|---|---|---|---|
| Same computer / LAN | < 2 ms | 2 | 33 ms |
| Same city | ~20-40 ms | 3-4 | 50-67 ms |
| Far | ~100 ms | 6-7 | 100-117 ms |

The delay is chosen by the host from measured ping at start (later:
adjusted at agreed frames). Each input message repeats the last several
frames' inputs so a lost packet rarely stalls the gate. Bandwidth: 12 bytes
per seat per frame plus headers, a few KB/s per peer.

The fixed step's rate (30 or 60) is a session setting; at 60 the game
plays as at the 60 fps default.

## 6. Protocol (v2, building on PR #24)

Header as PR #24's v1 (magic `MOMN`, version, type, flags, session id,
sequence, payload size), with a 16-bit payload size up to the transport's
message size and these messages:

| Message | Delivery | Payload |
|---|---|---|
| `Hello` | reliable | protocol version, build fingerprint (port version + commit + a hash of the generated game code), peer name |
| `Welcome` / `Reject` | reliable | assigned peer id and seats / reason code (version, build, full, settings) |
| `SessionSettings` | reliable | every simulation-relevant setting (existing-systems.md s.6), fixed-step rate, input delay |
| `SaveChunk` | reliable | the host's save file, in chunks, with its hash |
| `Ready` / `Spawned` / `Start(frame)` | reliable | start barrier |
| `Inputs` | unreliable | seat, first frame, count, the 12-byte gamepad per frame (redundant: the last several frames) |
| `InputAck` | unreliable | newest frame received per seat (trims redundancy) |
| `LoadDone` / `Resume(frame)` | reliable | level-change barrier |
| `SeatLost(seat, frame)` | reliable | a peer gone |
| `StateHashes` | reliable | frame range + hashes |
| `Desync(frame)` | reliable | host to all |
| `Ping` / `Pong` | unreliable | latency |
| `Leave` | reliable | reason |

Every message is checked: size, version, type, session id, seat range, a
seat owned by the sender, frame within the window (not too old, not too far
ahead), rate per connection. A failing message is dropped and counted;
repeated failures close the connection.

## 7. LAN discovery

A separate UDP socket (IPv4 broadcast to the local subnet's broadcast
address and 255.255.255.255, IPv6 multicast to `ff02::1`), on a fixed port,
two messages: `Query` and `Announce {session id, name, host name, players,
max players, port version, protocol version, game port}`. The host answers
queries and announces every couple of seconds; the browser lists answers
younger than a few seconds. No Steam, no internet. It won't cross subnets
or isolated Wi-Fi (feasibility.md s.6); the screen says so and offers
Direct IP.

## 8. Phases and the first milestone

Each phase ends with a finding (or a section in this one) saying what was
built, what was tested and what wasn't, per AGENTS.md.

| Phase | Content | Changes code? |
|---|---|---|
| **E1 (gate)** | **Cross-machine determinism, with the tools that exist**: record a two-player session with the fixed step on machine A (player 2 on a real or fake pad), replay it with `replay ... onspawn` on machine B, `track` both, `tools/compare_runs.py`. In order: (a) two Linux PCs, (b) Linux vs Windows, (c) a run with enemies and a fight, (d) a cutscene with dialogue, (e) 10 minutes. Result recorded in a finding | none (at most a debug-only addition to `track`) |
| E2 | Build GameNetworkingSockets (and ENet as the fallback) with the project's clang on Linux and Windows, inside the release pipeline; link a 20-line host / client test | build only |
| 2 | Technical design approved (this page, revised by E1 / E2) | none |
| 3 | `network/` transport + LAN discovery + Direct IP, a console-driven host / join, no game effect: two instances exchange reliable and unreliable messages, LAN without internet, disconnects | new files only |
| 4-5 | **Milestone M1** (below) | session hooks |
| 6 | Level changes (load gate), cutscenes, the desync checker grown to enemies, keyboard and mouse as session inputs | yes |
| 7 | Steam transport, only if feasibility.md s.7 is answered | optional build |
| 8 | Game-look UI: Multiplayer in the main menu, LAN browser, Direct IP, session panel on the Players screen | UI |
| 9 | Test matrix (s.10), known limitations | |

**Milestone M1**, small and measurable:

* two computers on one LAN, Direct IP, no Steam, no internet;
* one level (Wumpa Island), started from the host's save on both;
* controllers only; seat 1 on the host, seat 2 on the client;
* both players move, jump and fight independently, each machine shows
  both Crashes, the co-op camera frames both on both screens;
* the state hash is equal on both machines on every frame for 10 minutes
  of play, enemies included;
* pulling the client's network cable opens the lost-controller box for
  player 2 on the host within the timeout, and the host can drop player 2
  out and play on.

Out of M1 on purpose: level changes, cutscenes, keyboard and mouse, the
UI, more than two seats, internet play.

## 9. What changes where (expected)

| Where | Change | Risk |
|---|---|---|
| `src/network/` (new) | transport, LAN discovery, session, protocol | none to existing play (unused without a session) |
| `src/input_record.*` | a live input source next to the file replay | the replay path is shared with debugging: keep both |
| `src/fixed_step.*` | the frame gate in the pass listener | the fixed step is debug-only today; in a session it is the clock |
| `src/input/players.*`, `who_plays.*`, `lost_controller.*` | device presence from the session for non-local seats | must be a no-op outside a session |
| `src/input/key_events.*`, `mouse_*`, `name_typing.*`, `background_input.*` | off in a session (M1), session-fed later | |
| `src/players/players_setup.*`, `src/options/` | local / remote marks, locked rows | UI only |
| `crash_mom_manifest.toml` | likely one midasm hook for the load-done gate (after RE) | the usual manifest rules |
| `generated/`, `thirdparty/rexglue-sdk` | **no changes** | |
| `CMakeLists.txt`, release tooling | one networking library | Windows packaging (PRs #4 / #19) |

## 10. Regressions to guard against

* Single player and local co-op with no session: every new branch must be
  inert; check with the same `replay` + `track` runs before and after
  (identical tracks = nothing changed).
* The fixed step must not become the default outside sessions.
* Saves: the client never writes into its own save folder during a
  session; the host's saves are written as usual.
* Settings: a session's settings are applied for the session and the
  player's own settings file is left as it was.
* Windows build and packaging: the new library built and shipped by the
  installer pipeline.
* Windows names rule (AGENTS.md s.5): socket code must not use Win32 macro
  names; POSIX calls only in `#ifdef`'d platform code.
