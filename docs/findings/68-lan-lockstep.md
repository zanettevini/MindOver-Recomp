# 68. LAN co-op, first step: two copies of the game in lockstep

Status: **experimental test tool** (online plan milestone M1). Two copies of the
game, one player each, on one PC or a LAN. Source: `src/net/`.

## 1. The idea

Each PC runs the whole game. Only the players' controller states travel over
the network: every game frame, each PC waits until it holds every player's
controller state for that frame, then runs the frame with them. Nothing about
the world (positions, enemies, objects) is ever sent.

This only works because the game is **deterministic** once the fixed step is on
(every frame exactly 1/60 s, helper threads and sound on game frames, the
random generators and the clock reset at the session's start; earlier work in
`fixed_step.h` and `system/helper_threads.h`): the same inputs on the same
frames give the same world, frame for frame, on every PC.

```
PC 1 (host)  = player 1            PC 2 (guest) = player 2
reads its keyboard / pad           reads its keyboard / pad
for frame N + delay                for frame N + delay
         \______ UDP: "my input for frames A..B" ______/
frame N runs when both inputs for N are in (the FRAME GATE)
```

**Input delay**: a press read on frame N plays on frame N + 3 (50 ms at 60 fps,
`--net_delay`). The other PC's packet has that long to arrive before anybody
waits.

## 2. Where it hooks in

| Place | What happens there |
|---|---|
| Main loop's pass hook (`fixed_step.cpp`, the frame's time delta) | Before the helper threads may run: this PC's input goes out for frame s + delay, and the frame that just ended is fingerprinted. After they may run: **the frame gate** (a wait here is "between frames": the helpers finish their work, their clock stands still, so a wait changes nothing in the world). |
| The game's controller read (`sub_824742F0`, wrapped in `save_library.cpp`) | Socket 0 = this PC's own player (keyboard + pads), noted; then every socket is answered from the session's table for this frame: sockets 0-1 = players 1-2, sockets 2-3 "not connected". |
| The game's controller capabilities call (`sub_824742E8`) | Sockets 0-1 answer "standard pad" on both PCs. Without it the other PC's player never joined: the game creates a controller object only for a socket whose capabilities answer, so player 2's START was never read on the host. |
| Who has a controller (`players.cpp` PresentMask) | Players 1 and 2, on both PCs, whatever devices each PC has. |
| Session start (`fixed_step::StartSession`) | The front end's "Load successful" exit (state 282 -> 283) or quick load: clock + random generators reset from the host's seed; session frame 0 = the next main-loop pass. At most once per frame (two callers in the same frame would otherwise restart the clock twice on one PC only). |

Also changed for a session, because they read this PC's devices or write into
the world directly:

* the keyboard reaches the game as controller state (its native input events
  would reach only one PC's game);
* mouse aiming is off (it writes the aim point into the world, not through the
  controller state);
* player 1's "controller unplugged" watch is off.

## 3. The frame gate's margin

First version: wait for the other PC's input for frame s. The PC that started
first then ran exactly `delay` frames ahead of the other, with no room left on
its side: it waited a few milliseconds on nearly every frame (6,300 waits in a
2-minute run), and any late packet would have stalled it.

Now: wait for the other PC's input for frame s + (delay - 1) / 2, i.e. one frame
of room both ways at delay 3. And after a wait, the paced clock moves its
schedule by the time waited, so the PC that was ahead takes the other's
rhythm instead of keeping its own phase. Result: 9 waits after the start in the
same kind of run.

## 4. Is it still the same world?

Each PC computes a small **fingerprint** at every frame's end
(`world_hash::QuickFingerprint`: the main random generator + every player's
position, velocity and titan flag; never a heap address, which may differ
between PCs) and sends its newest 16 with every packet. The first frame where
they differ is reported on both PCs ("DRIFTED at frame N": log error, console
`net`, Network window). **Dump both sides** asks both PCs to record the world's
data at the same session frame; `tools/world_hash_compare.py` then names the
differing words.

The **world settings fingerprint** (`system/session_settings.h`) is compared
when the PCs connect: a different value is a warning on both (those settings
change what the game decides).

## 5. Tests (one PC, two windows)

Both copies load the same save; a recorded script drives each copy's player 1
socket (player 1's part on the host, player 2's part relabelled on the guest).

| Test | Result |
|---|---|
| 70 s, player 2 joins, both move and fight | Session starts on the same frame on both; player 2 joins on both; players' tracks identical on every frame (16,812 rows); fingerprints equal on every frame |
| One copy with a different world setting | Warning on both at connect |
| Random generator poked on the guest (console `write`) | "DRIFTED at frame 972" on both PCs |
| Dump both sides | Both dumps at the same frame; the compare tool names exactly the poked word |

## 6. Security (the LAN part)

Packets carry a magic word, a protocol number and the session's random token;
every size and range is checked before use; after the handshake only the one
peer address is listened to. Only controller states, frame numbers and
fingerprints are accepted: nothing a packet says is ever used as a memory
address or a file name.

## 6b. A player disconnects

Nothing pauses. When the other PC goes quiet (2 s without a packet) or leaves,
the PC that's left **plays on alone**: the other player's input is "nothing
pressed" from then on, so the frame gate never waits again. Their character
stays in the level for `--net_drop_after_s` (30 s; at once when they left on
purpose) and then **drops out by itself** through the game's own Drop Out (the
"controller disconnected" question isn't asked). Only the player who lost the
session sees a message (the message bar card "Disconnected"): a guest whose
host went quiet, or a PC that pressed Leave. The host's view is always "the
guest left", with no message.

| Test (drop after 5 s) | Result |
|---|---|
| Guest's PC stops answering | Host plays on, no pause; player 2 stays 5 s, then drops out; no message on the host |
| Host's PC stops answering | Guest plays on as player 2; player 1 drops out after 5 s; the guest sees "Disconnected" |
| Guest presses Leave | Player 2 drops out at once on both PCs; the guest plays on alone and sees "Disconnected" |
| 30 minutes, 2 players (108,000 frames) | In sync on every frame, players' tracks identical, 24 frames waited at the gate |

## 6c. Nothing pauses online

A pause would have to stop every PC's world because one player opened a menu.
Online, nothing pauses: the menu opens and the world runs on under it.

**How the game pauses** (found with the debug console's `who` on the paused
byte): the pause menu's script calls a script method (`sub_82123778`) that
calls the game manager's pause, `sub_8227C8E8(manager, on)`. It sets game
`+168` bit `0x20` (the AI, physics, sound and others skip their updates while
it is set), bit `0x80` of `+104`'s `+24`, and resets the input manager
(`sub_82274538`). During a session that function returns at once.

**The menu's owner stands still.** With the world running, the buttons pressed
in the menu would also move the owner's Crash: the menu reads the MENU input
map, the character the GENERAL / PLAYER / titan maps of the same controller.
The game already has the tool: a **locked** root input controller (its byte
`+516` above 0, `sub_82272D68`) answers "no" to every play question, which is
how cutscenes take control (`DO_LockAllControllers`; `AreControllersLocked` =
`sub_82265A60`, which also shows where player p's root controller is: the
input manager's (`game +56`) controller array `+8`, at the index
`sub_82266130(front end, p)` gives). The menus still hear their MENU map
through the lock. So while player p's menu is up, 1 is added to p's lock and
taken back when it closes. "A menu is up" = the game's flag (front end `+8594`
bit `0x80`, which comes with its pause and so never comes on online) or, in a
level, the front end has left GameRunning (state 489). The owner comes from the
port's menu-owner rules (who paused, who used the totem, ...). Both PCs see the
same menus on the same frames, so the lock changes on the same frame everywhere.
Checked every frame from the front end's frame (`sub_82261C40`): the frame
function's own checks don't run while the pause menu is up.

| Test (host opens the pause menu) | Result |
|---|---|
| The paused byte while the menu is up | 0 (the world runs) |
| The host holds the stick 3 s in the menu | the host's Crash doesn't move |
| The guest walks meanwhile | the guest's Crash walks |
| The host presses B | the menu closes; the host's Crash walks again |
| Both PCs | in sync on every frame |

## 6d. The menus online (totem, map, everyone's view)

Three more findings while testing every in-game menu online:

* **The menus need to see a pause.** Every frame the co-op join check
  (`sub_82264988`) copies the paused bit into front end `+8594` bit `0x80`
  ("a menu is on screen in play"); the menus' D-pad handling and the port's
  menu-owner rules read that flag. And the question box (the save totem's "Do
  you want to save the game?", update `sub_8225DE68`) closes itself in a level
  unless the game is paused (its check at `0x8225DEB4`; found with the debug
  console's `who` on its "answered" flag, front end `+216` bit `0x80`).
  Online, while a menu is up in a level (front end out of GameRunning 489),
  the **front end's frame** (`sub_82261C40`) runs with the paused bit set and
  gets it put back afterwards; the box's own update always does. The world
  (AI, physics, players, sound) never sees a pause.
* **The map belongs to whoever opened it**: the original lets any player's
  buttons work it. Online the opener is the player whose session input has
  Back down on the frame of the exit into the map (489 -> 502); their Crash
  is locked and only their buttons work it. **But the map is more than a
  page**: the game moves its one camera to a bird's-eye map view, so every
  PC's picture turned into the map. Until each PC has its own camera, the
  map stays closed online (the exit into it is answered "stay").
* **Only the owner sees their menu.** In lockstep every PC runs every menu,
  so every PC drew it over a player who keeps playing. Online, on every PC but
  the owner's, the menu pages aren't drawn (the Parent draw `sub_82371270`
  skips top-level pages other than the HUD's: `InGame`, the co-op corners,
  counters, announcements, the message bar, tutorials, the letterbox). The
  menu still runs on every PC.
* The pause menu's title is the solo game's own "Paused" (the game's text, in
  its language) for whoever opened it: the menu shows only on its owner's PC,
  so "P1 Paused" / "P2 Paused" tells nobody anything.
* A lost window focus no longer opens the pause menu online (with
  `background_input` off it would open on that PC only).

![Online: the host's pause menu on the host's screen only](../images/online-menu-only-for-its-owner.jpg)

| Test (host opens each menu, the guest walks) | Result |
|---|---|
| Totem question | stays up on the host's screen only; the host's Crash still under a held stick; A = No (the default) back to play; the stick moves the box's cursor (left = Yes) as in the original |
| Pause menu | host's screen only; the guest plays on |
| Back (map) | stays closed online |
| Both PCs | in sync on every frame |

Open: while one player's menu is up, the other players' HUD disappears too
(the HUD hides itself when the front end reads "paused", and the front end
must read the same on every PC).

### The mouse in a menu (a desync found in play)

Two copies on one PC, player 1 paused with the keyboard (START, then A):
the host's copy went into Options, the guest's resumed the game, and the
worlds parted two seconds later (player 1 walked on one PC, stood in a menu
on the other). Both copies read the same START and A. The pointer resting
over the host's window had moved that copy's cursor: the mouse in menus
(hover and clicks, src/input/mouse_menus.cpp) acts on its own copy only and
is not part of the input the PCs share. Online the mouse now leaves menus
alone (hover, clicks, the wheel); keys and controllers still work, through
the shared input. A test with the host's pointer moved over its pause menu:
before, Options on one PC and Resume on the other; after, Resume on both and
identical worlds.

## 6e. Only the host saves

Online, only the host saves the session. A guest can't; whenever the host
saves, the guest's PC keeps a copy of that save in its own list.

Both PCs run the same game, so both run the save flow on the same frames. The
original flow opens the save list (totem "Do you want to save?" Yes = exit 837
-> AccessMemoryCard 359 -> ReadingCard 384 -> GameSlotScreen 386; pause menu
Save Game = 886 / 897, same way). That list comes from each PC's own save
files: a different number of saves or an "overwrite?" question on one PC only
would send the two front ends down different exits. So online the list never
shows:

* The storage steps run as usual (BeginAccess, the scan). At ReadingCard's
  decision (sub_8211DA80) the save library takes slot 1's "available slot"
  exit (408 -> AutoSaveGameScreen 326, script SaveGame(0)) instead of the list:
  the route New Game already uses for its first save. Slot 0 is "Create New
  Save" on every PC, so no overwrite step runs anywhere.
* SaveGame then writes "the game in progress's" file, chosen per PC: the host
  overwrites the save the session was loaded from; a guest writes its own copy
  (the first save of a game copies the loaded file to a new number, so both PCs
  overwrite an existing file; later saves overwrite that copy). The guest's own
  save is never written.
* A guest's own save is refused: the totem's Yes becomes No (838), the pause
  menu's Save Game becomes Resume. Because the same decision runs on both PCs,
  both show the message bar card "Only the host can save online". The menu's
  owner decides (the totem = who used it, the pause menu = who paused).

First try, dropped: the original's unused "ExitAutoSaveScreen" exits (totem
836, pause 885 / 896) go straight into 326. They skip BeginAccess, and the
EndAccess at the end freed a slot table that was never made (a write to
0x0000000C in sub_82360258).

| Test (save 2, two windows) | Result |
| --- | --- |
| Host: totem Yes | host's save written; the guest's copy byte-identical; same exits; in sync |
| Guest: pause -> Save Game | refused, card on both, no file written; in sync |
| Guest: totem Yes | refused (answered as No), card on both; in sync |
| Host: pause -> Save Game twice | copy made once, then overwritten; same exits; in sync |
| Offline: totem Yes | the save list opens as before |

The guest's copy can differ from the host's file in a few bytes: the save
buffer keeps leftover heap bytes after an empty string (the string itself is
empty on both). The heap's leftovers come from before the session started, when
the two PCs ran differently. The original game writes those bytes too; the game
data is the same.

## 6f. A stick is meant in its own player's camera (first step toward own cameras)

Every "which way does the player push?" question in the game (the fight
trees' walk, run, jump and aim, the script methods
GetSelfRelativeInputDirection and its siblings: about 20 callers) goes through
the input controller's sub_82272208 -> sub_82271E38. That function turns the
stick by the game camera's forward on the ground:

* normally the game camera, `*(*(0x8259B190) + 76) + 444` (a VectorCamera),
  virtual slot 14 -> a record whose +32/+36/+40 is its forward;
* while the controller's +520 top bit is set: a forward it holds (+464 /
  +472), so a camera cut doesn't swing what the stick means.

Both paths store x and z at r1+88 / r1+96 and meet at 0x82271F58. Today every
PC shows the same camera, so every PC turns every stick the same way. With a
camera per PC, player 2's stick would mean "left" on one PC and "back" on the
other, and the worlds would part.

The fix (src/net/online_sticks.cpp, a mid-function hook at 0x82271F58): each
PC reads its camera's facing on the ground at the start of every frame and
sends it with that frame's input as a 16-bit facing (65,536 steps a turn;
network protocol 2). Every PC then turns player P's stick by the facing that
P's PC sent for this frame. (Sent with every input, not only while the stick
is pushed: sub_82271E38 skips the camera while the stick is centred, and an
earlier version that noted the facing there sent a player's first steps
without one; see 6g.)
The cos / sin of the facing come from a fixed series in plain double
arithmetic (no fused multiply-add), so the result has the same bits on every
system (a C library's cos can differ in the last bit between Linux and
Windows). Until a PC's first facing arrives, the local camera is used (the same
on every PC as long as the cameras are).

| Test (save 2, two windows) | Result |
| --- | --- |
| Normal run | in sync on every frame; positions as before |
| Guest's sent facing turned 90 degrees (debug flag) | player 2 walks off turned, the same on both PCs; in sync |

Other game code reads the camera too: a scan for the
`*(*(game) + 76) + 444` pattern finds 48 functions. Among them are views
(markers over the players, the floating button icons, the sound listener, the
cinematic camera) but also world logic: an enemy task that circles the player
(CAITaskCombatMoveEncircle), the off-screen catch-up gate (sub_82235978), the
zones that run near the camera (sub_822F1F78, sub_822F4AA0), a Collectable
method. These decide whether each PC can show its own camera.

## 6g. Each PC's own camera: the experiment

The flag `net_own_camera` (with `coop_camera` on) makes each PC's camera
follow only its own player in a session: the co-op camera counts the other
players as absent. Two copies on save 2 (host stands, then walks left; guest
walks right, then up toward the water), with a per-frame log of the
fingerprint's parts (`--debug_net_fingerprint_log`: every player's raw
position and velocity bits, the random state) on both sides:

![Two PCs in one session, each camera following its own player](../images/online-own-cameras.jpg)

| Round | First difference | Cause | Fix |
| --- | --- | --- | --- |
| 1 | frame 1036: player 2's velocity, half a degree, the first frame they walk | the facing was only noted while the stick was pushed, so the first frames of a push went out without one and each PC used its own camera | send the camera's facing with every input (6f) |
| 1 | (found by reading, while chasing round 1; no visible change in this run) | sub_822718E8 decides "hold the old forward through a camera cut" from the local camera, writes the held forward (+464) and the hold bit (+520), sends the controller an event | mid-function hook at 0x8227192C: the sender's facing replaces the camera's direction there |
| 2 | frame 1885: player 2 deflected on the guest's PC only (velocity turned about 20 degrees, lifted) | zones RUN only when the camera is inside them (sub_822F4AA0, called by the zone check sub_822EFFD8): the guest's camera ran a zone around player 2 that the host's camera didn't | in a session sub_822F4AA0 answers "no" and leaves the zone alone: a zone runs when it is always on, the focus player's zone, or holds a player |
| 3 | none in that scene: in sync on every frame | | |
| 4 | a 30-minute random two-player script (jumps, attacks, B), frame 1276: player 1 parked on one PC only | B released 10 units from player 2: the game's off-screen CATCH-UP (off screen + B = forced into a mask) asks the camera whether the Crash is off screen (sub_82235978) | online plan 6.1, "no forced masks": in a session every Crash counts as on screen. A mask by choice doesn't ask the camera (coop_mask_from_anywhere) |
| 5 | frame 1307: the masked player 1 turned slightly differently on its host (forward row y 0.028 against -0.00006) | the mask turns toward what its player AIMS at; the aim is a ray from the camera through the player's 2D reticle spot (CReticleAimingBehaviour, sub_822AC668) | each input also carries its PC's whole camera pose (position, direction, up, field of view: the camera's own words; protocol 3). While the aim update runs for player P, the game camera holds P's sent pose (camera bytes +16..+251 saved, pose written, matrices marked stale, saved bytes restored after) |
| 6 | none: the 30-second replay of that script in sync on every frame | | |
| 7 | the full 30-minute script, frame 9430 (2.6 minutes): player 2 starts the same turn one frame earlier on the PC whose camera shows them; on the other PC they are off screen | a breakable prop player 2 targeted broke a frame later where it was off screen: props animate only when drawn (round 9 below) | three camera reads fixed on the way, none of them this one: the stick's direction sector for the fight trees (sub_8221DD70: camera direction -> the sender's, hook 0x8221DE00), the fight-tree condition "am I off screen" (sub_821BA090: "on screen" online), the held forward written at a controller UNLOCK (sub_82270FE8: camera direction -> the sender's facing) |

Round 7 in detail. Logging each player's controller state per frame (lock
word, hold bit, held forward, stored stick) shows the stick pipeline equal on
both PCs from frame 935 on, including every frame around 9430: the difference
is further along, in what the character does with the same input. Player 2 is
off screen on the host's PC at that moment (photos of both PCs) and the turn
comes about 15 frames after a light attack started: the lead is that characters
the camera doesn't see are updated differently (animation detail, an event
one frame late). The game's frustum test (sub_822ED0C8) is called only from
the render manager and zone drawing; what that leaves on a culled character
for game logic to read is the next question.

Round 8: player 2's behaviour objects, hashed per frame on both PCs, then
their raw words. Several differ long before the split, but mostly in harmless
ways: pointers to the SAME things at different addresses (each PC loads, draws
and sounds different things, so the heaps lay out differently), and
CSoundDialogueBehaviour's squared distance to the sound LISTENER (the global
at 0x82526324, set by the sound system = each PC's camera). The one that
mattered: CTargetingBehaviour (+0x3C, the candidate list) let go of its target
one frame apart. The write watch (`who`) on that list named the cause.

Round 9, the cause: the target was a breakable prop, and it BROKE one frame
later on the host. Logging every break with the game's call stack showed both
PCs breaking it from the same place: CRenderBehaviour's update (slot 4,
sub_82189D38) -> CRenderObject3D's update (slot 5, sub_822B6608) -> the
prop's StateProp animation (sub_82380B00 -> sub_8237F5C0) -> its "break"
event -> the breakable (sub_821959AC). sub_822B6608 has an ANIMATE ONLY WHEN
DRAWN rule: with flag +404 bit 0x40 set and the object not drawn lately (its
draw stamp against the render frame counter at **0x82571B88), it only adds the
time step to +400 and returns; the next time it is drawn it advances by all of
it at once. Player 2's prop was off the host's screen: its break event fired a
frame later there. Online the flag is cleared for the call (and put back): every
object advances every frame on every PC. The 3.5-minute replay is then in sync
on every frame, every break on the same frame on both PCs.

Round 10, harder scenes. The two halves of three earlier test scripts (each
PC plays one player's part) with own cameras and the per-frame log on both PCs:

| Scene | Frames | Result |
| --- | --- | --- |
| A titan ridden in a courtyard (50 s) | 3,640 | world in sync on every frame |
| N. Gin's lab cutscene and the fight after it (90 s) | 5,963 | in sync; the cutscene and the fight on the same frames |
| Walking into the next level (60 s) | 4,160 | in sync; both arrive at the same spot on the same frame |
| Five minutes of random jumps, attacks and jacks around the titan (two players) | 18,638 | world in sync; one controller state parted (below), fixed |

The log still showed two harmless differences. The dialogue sound's distance
to the listener (each PC's camera), which only the sound uses. And a
controller that NO player owns (sockets 3-4 in a two-player session) holding
each PC's camera direction as its "held forward": the unlock at a level's
fade-in (sub_82270FE8) copies the camera direction there, and the online
replacement skipped controllers without an owner. Player 2 is handed such a
controller for a while when they join, and the hold logic compares against
that value, so it now gets player 1's sent facing (the same on every PC).

The controller state that parted in the five-minute run: player 2's "hold
the old forward" started on the host 10 frames before the guest (frame
11361). Besides its angle test, the hold logic (sub_822718E8) starts a hold
at once when a question sent to the game object's +12 (event 9, no player
named in it) answers yes; it answered yes for about 40 frames on both PCs,
each at its own moment: the question is about the game camera. It now goes
the way of the facing: each PC sends its own camera's answer with every input
(protocol 4), and player P's controller gets the answer P's PC sent (a
controller nobody owns gets player 1's). After that the same run, the three
scenes and the titan run were identical on every frame, controllers included.

Every game function that asks the camera "is this point in view" (camera
slot 17) was listed: the fight-tree condition and the co-op catch-up (both
answered online already), the camera's own focus list (view side), and one
more: enemies circling a player (a step of CAITaskCombatMoveEncircle,
sub_820BE878) count one step more while they are OFF screen. Online every
enemy now counts as on screen, the original's answer while the single camera
sees it.

The pattern worth remembering: the engine saves work on what the camera
doesn't see, and with a camera per PC "what the camera sees" differs. Every
such saving that changes WHEN something happens in the world has to be turned
off online.

The random generator stayed identical in every round. The tool that named
each cause: `--debug_net_fingerprint_log=<file>` on both PCs (every player's
raw position / velocity bits, world matrix, zone state, and the random state,
per frame), then the first line where the two files differ.

The general rule this points to: code that works out something of ONE player
from the camera uses THAT player's PC camera (sent with the input); code that
asks the camera about the world as a whole (zones, catch-up) needs a rule that
doesn't involve a camera. Inputs got bigger for it, so a packet now carries at
most 20 inputs (about 1.3 KB). The zone rule has a cost:
a zone that only the camera looks into, with no player in it, stands still
until a player walks in.

## 6h. Windows: the first runs (one PC, two copies)

Built on Windows 11 with the project's Clang (23) and Visual Studio's
libraries. Two changes were needed to build: `system/helper_threads.cpp`
read Linux's `/proc` (its thread questions now answer neutrally elsewhere,
see the file's header), and the session's socket code needs `ws2_32`. The
debug console listens on `tcp:<port>` (127.0.0.1 only) so the tests can drive
both copies on Windows. Test tools: `tools/lan_tests/`.

| Test (save with a titan and enemies, two copies on 127.0.0.1) | Result |
|---|---|
| 40 s: player 1 circles; player 2 joins with START (mask), B (on foot), circles | connected, same settings, the session started on the same frame with the same seed; player 2 joined on both; in sync on every frame (3,043); both players' tracks identical; the same hitpoints at the end on both (titan 25.6, player 2 152.0) |
| 5 minutes of seeded random play, both players | **DRIFTED at session frame 1748** (29 s): the two copies parted for good |
| the same recordings, the guest's game killed after 60 s, `--net_drop_after_s=5` | in sync through frame 2731 (past 1748); the host played on without a pause; "player 2's PC stopped answering", player 2 dropped out 5 s later through the game's own Drop Out |
| the same recordings, 2 minutes, three times, fingerprint logs on | in sync on every frame, three times out of three (7,812 frames each) |

So one run in five parted, at a frame the other runs went through in sync:
on Windows the lockstep is not yet repeatable every time. The likely cause
is what `helper_threads.h` already names as Linux-only: the game's helper
threads (loading, audio streaming) are not held to game frames on Windows,
because the wait wrappers come from the Linux-only Xbox calls meter
(`--wrap`). In the drifting run the two copies' logs show the same audio
files loaded a few milliseconds apart, and the renderer's vertex byte
counts already differ before the drift (1,242,296 against 1,243,304 at game
frame 1800): something the fingerprint doesn't cover parted earlier. Not
proven: the fingerprint logs were off in that run.

**The fix: the wait wrappers on Windows** (built, `system/wait_imports_win.cpp`,
`system/thread_states.*`). The runtime
library is a DLL that exports every `__imp__<Name>` (`rexruntime`,
`WINDOWS_EXPORT_ALL_SYMBOLS`), and the generated code calls them by name
through the import library. A definition of `__imp__KeWaitForSingleObject`
and the four other waits in the game's own executable would be used instead
of the import (the import library only fills symbols nothing else defines),
and the original is still reachable with `GetProcAddress` on the runtime
DLL. The `/proc` questions have Windows answers too: a thread's run state
and wait state from `NtQuerySystemInformation` (SystemProcessInformation),
its name from `GetThreadDescription`. (The thread's end: gone from the
same snapshot, or Terminated.) `helper_threads.cpp` keeps its logic and asks
`thread_states` instead of `/proc` on Windows. lld-link uses the
executable's definitions with no duplicate-symbol complaint from the
import library; the log names each wait the first time it goes through the
rules (four of the five showed in the first second of a run).

| Test, with the wrappers (same save, same recordings) | Result |
|---|---|
| 40 s, player 2 joins | in sync; the end the same as the run without the wrappers (titan 25.6, player 2 152.0, the same positions) |
| the 5-minute session that drifted at frame 1748, three times | **in sync on every frame, three times out of three** (18,800 frames each, both players' tracks identical on all 18,038 replay frames); the three sessions end in the SAME state (player 1 74.9, player 2 58.1, the same positions), where the drifting run without the wrappers ended elsewhere (162.5 / 75.5) |
| frame rate (host, the 5-minute run) | 60.0 fps over 5 s windows, 58.8 average over the session including the start; frames waiting at the gate: 2-111 per session (about 2 s, almost all of it the first frame, while the guest's copy finished loading) |

Not covered yet: two PCs, a level change and a cutscene with the wrappers on
Windows, Linux with this branch's `thread_states`-free path (unchanged
code there).

## 6i. Played by hand (one PC, two copies): the Spin key

`tools/lan_tests/play_one_pc.ps1` opens both copies in windows (host = player
1 with sound, guest = player 2) on the newest save, for a person to play;
when both windows close, the session's log lines go to a report. The first
session played by hand (save 17, one person on the keyboard and a
controller, 8 minutes: fights, the pause menu, Options changed twice)
stayed in sync for 28,830 frames, then **DRIFTED at session frame 28831**,
20 s after Simple Spinning had been turned on in Options (on both copies, on
the same frame: the menu runs on the shared input). The input recordings of
the two copies (`game-inputs.txt`, what the game read per player) are the
same line for line: the controller states were alike, so something outside
them decided.

The likely cause (the hand-played session can't be replayed frame for frame: its recordings are stamped in real milliseconds): **the keyboard's Spin key**. With Simple Spinning on, the key
answers the fight trees' "a full circle of the stick done?"
(`sub_8221E200`, `input/keyboard_mouse.cpp`) straight from this PC's
keyboard driver, not through the controller state; `game_mods::FilterPad`
also read the key's "held" for Keep Spinning. In a session that answer
reaches only the copy whose window has the keyboard. Reproduced with the
console's `key Q` (the default Spin key) on the host only, save 17,
`--simple_spinning=true` on both:

| Run | Result |
|---|---|
| before the fix | "a full circle for player 1 (the Spin key)" in the HOST's log only; **DRIFTED at session frame 572**, a second after the second press |
| (the same on save 10, player 1 riding a titan) | in sync: the titan's state didn't take the circle there, so the presses changed nothing |
| after the fix | "a full circle for player 1 (simple spinning's button)" in BOTH logs on the same frame; in sync on every frame (1,394), tracks identical |

**The fix**: online (`net::Configured()`), the Spin key goes into the
controller state as LB, Simple Spinning's own button (`MergeInto`, only in
play, with Simple Spinning on), so it travels with the player's input and
`game_mods::FilterPad` turns it into circles on both PCs on the same frames
(its clock is already game time). The keyboard's own circles and its "held"
are not asked in a session. Off line nothing changes.

The same kind of thing, found while looking (not seen in a session yet; the
port's own code reading THIS PC's devices or mouse where the world listens):

* the chest's outfit picker read each player's controller and the keyboard
  itself (`players/outfit_picker.cpp` `ReadPad`), and its key repeat, its
  "just closed" second and its queue's wait used the real clock: online it
  now reads the session's input (`net::FrameInput`), the clock is game time,
  and the mouse plays no card;
* the save list's buttons (`saves/save_library.cpp` `MenuButtonsDown`):
  the session's input online;
* the mouse wheel in lists (the save list, the achievements):
  `TakeListWheel` gives nothing online, like the clicks (6d).

Built with all of these, the earlier sessions again: the 5-minute session in
sync on every frame (the fourth time; the same end, player 1 74.9 and player
2 58.1 at the same places), the 40 s session in sync (the same end, titan
25.6, player 2 152.0), the Spin key test in sync. The chest and the save list
online are not played yet: their changes are reasoned, not tested.

**15 minutes** (new recordings from `tools/lan_tests/make_random_play.py`,
seeds 33 and 44: runs, jumps, light / heavy attacks, B for both players):
in sync on every frame through session frame 53,400 (14.8 min), both
players' tracks identical on all 52,998 replay frames, 60.0 fps over the
last 5 s windows (59.7 average over the session with its start).

**A world setting changed inside a session** (the hand-played session
changed two in Options, alike on both PCs). The settings are compared once,
at connect (s.6, `session_settings`); one changed later through the F1 tools
or the console changes only that PC. Now each PC checks its settings'
fingerprint once a second in a session and names a change in the log
("a WORLD SETTING changed on this PC at session frame N", with the old and
new fingerprints; console `settings` lists them). Test: the host alone set
`friendly_fire` off through the console 7 s in: the warning on the host only,
at session frame 420; the session DRIFTED at frame 1944, when the players
first hit each other (both tracks part at that moment). The same change from
the game's Options menu shows the warning on both PCs on the same frame and
no drift (as in the hand-played session).

## 6j. A bad network, on one PC

To see how the frame gate and the input delay cope with internet-like links
without two PCs: `--debug_net_lag_ms=N` (every packet this PC sends leaves N
ms late), `--debug_net_jitter_ms=N` (plus a random 0..N ms each: packets can
overtake each other), `--debug_net_loss_percent=N` (N in 100 dropped).
Packets wait in a queue that their own thread sends; its random numbers are
its own. The 10 s status line counts them ("bad network: X packets sent
late, Y dropped"). Both copies got the same settings (so the round trip is
twice the lag), the 40 s recordings unless said:

| Link (each way) | Ping | Input delay | Result |
|---|---|---|---|
| 40 ms + 0..20, 5% lost | ~100 ms | 3 frames (50 ms, the default) | in sync, tracks identical (2 minutes); **43 fps**: 600 frames took 14 s |
| the same | ~100 ms | 4 (67 ms) | in sync, identical, three times out of three; **53 fps** |
| the same | ~100 ms | 6 (100 ms) | in sync, identical; **59 fps** (1,200 frames in 20.3 s) |
| 100 ms + 0..30, 10% lost | ~230 ms | 9 (150 ms) | in sync, identical; **43 fps** |
| the same | ~230 ms | 12 (200 ms) | in sync, identical; **55 fps** |

Lost and late packets never part the games (each packet carries every input
the other PC hasn't confirmed yet): a link too slow for the delay only slows
both games down, because a frame that waited at the gate is never caught up.
Why a 100 ms ping needs about 6 frames (100 ms) and not 3: the frame gate
(s.3) waits for the other PC's input of frame s + (delay - 1) / 2, so each PC
has only about HALF the delay as room; the delay wants about the whole round
trip.

**The host now picks the delay from the ping** (`--net_delay_auto`, on by
default; protocol 5). For the first 4 pings after the guest joins (~2 s) it
measures, then takes the slowest ping in frames, plus 1, at least
`--net_delay` (3 by default), and sends it in a new DELAY packet with every
ping; the guest's session may only start once it has it (else "NO
SESSION", like a level started before the host's seed: in practice the
save loads take 10 s or more after the join). A host whose level starts
before the 4 pings keeps the delay it has. `--net_delay_auto=false` = the
old fixed delay.

| Link (each way, both PCs) | Ping (slowest of 4) | Delay picked | Result (40 s) |
|---|---|---|---|
| none (one PC) | 0.1 ms | 3 (50 ms) | in sync, identical; waited only at the start (the guest's load) |
| 40 ms + 0..20, 5% lost | 121.5 ms | 8 (133 ms) | in sync, identical; **60 fps** (17 frames waited, 2.1 s, all at the start) |
| 100 ms + 0..30, 10% lost | 261.8 ms | 17 (283 ms) | in sync, identical; **60 fps** (31 frames waited, 2.2 s, at the start) |
| 40 ms + 0..20, 5% lost, `--net_delay_auto=false` | | 3 | in sync, identical; 23 s waited (the old way) |
| none, 5 minutes | | 3 | in sync on every frame, tracks and fingerprint logs identical |

(A first try took half the ping plus 3: 7 frames for the 40 ms link, 10 for
the 100 ms one; the 100 ms link then still waited 13.5 s in 50 s.)

The price is input lag: 8 frames = 133 ms between a press and its effect on a
100 ms link, 283 ms on a 230 ms one. Each PC also names a slow link in the
log when its frames waited more than a second in a 10 s window (past the
session's first 20 s): "the last 10 s waited 4.3 s for the other PC's input
(ping 95 ms, input delay 3 frames): both games run slower than 60 fps. An
input delay of about 8 on the host would fit this link" (the ping in frames
plus 2, at least the delay + 2): for a link that got worse after the start,
or with the automatic delay off.

**Spins online, 5 minutes**: new recordings with LB now and then
(`make_random_play.py ... --lb`, seeds 55 / 66), Simple Spinning and Keep
Spinning on in both copies: save 17 (another level, a cutscene at its
start) and save 10 both in sync on every frame (18,800+), both players'
tracks identical on all 18,039 replay frames, the fingerprint logs equal.

**A crash, once, not repeated.** In one of these runs (link 40 ms, delay 4)
the HOST's game stopped at the level's start, ~3 s after "new level" (right
after "Markers: player 3's own picture in use", a point every run passes):
`Unhandled guest access violation: read of guest 0x0000001C` in
`sub_822531E8`, from `sub_8217F808`: the physics set-up of an actor whose
collision shape wasn't there, the same null read as an actor created without
its assets (findings/26 s.22, the cheat menu's spawns). The guest's copy had
gone through the same frames (in sync through frame 327) and played on
alone. The same settings three more times: no crash, in sync. Over the ~25
sessions of this section and 6h/6i (50 copies) it happened once; maybe a
load that finished a frame late on that copy (the helper threads' rules on
Windows read thread states from snapshots, 6h), maybe not network related.
Open: it needs a catch with a debugger, or a run of many level starts.

**A load that never finished, once.** Then 22 level starts in a row (12 s of
play each; 6 with the bad network, 16 without): 21 in sync, tracks
identical. In one (no bad network) the HOST's save load never ended: from
the loading screen on, 6 frames a second for 15 minutes, 160 ms of each
frame spent waiting for the helper threads to go quiet (frame_rate's "game
160 ms"), "8,497 gaps of silence" a minute in the sound; no warning (no
helper let go at the gate, no 30 s timeout). The guest's copy had started
its session and waited for it. Not seen again in the next 10 starts. To
catch it next time a frame that waits more than 50 ms for the helper
threads now says what it saw not quiet last (at most every 2 s): "a frame
waited N ms for the helper threads (K such waits so far); last seen not
quiet: runnable game thread <name> / busy (not in a wait): <helper> / timed
wait due: <helper> / held at the gate / a wait ended (a wake)". In the 10
normal starts after it no frame waited that long. (The test script got
`-WaitReplayMs`: a stuck load now ends the run after that long instead of
15 minutes.)

## 7. Not yet

* Coming back after a disconnect (late join / reconnect: the plan's milestone M5).
* Saves: a guest at the totem still gets the "Do you want to save?" question
  (their Yes is refused afterwards).
* Each PC's own camera following its own player (the plan's milestone M4): an
  experiment flag for now (6g). The other camera readers in the game (6f) are
  not all checked yet: views, or world logic that a scene hasn't reached.
* A session starts at a save load: Host / Join first, then load the same save
  on both. The save itself isn't sent or checked (a different save shows as a
  drift at frame 0).
* Two players, one per PC; menus, pause and cutscene-skip rules; late join;
  the internet; a menu in the game's own look. Windows: the socket code is
  written but not built there yet.

## Controls

* `--net_host` (player 1) / `--net_join=<address>` (player 2), `--net_port`
  (default 37373), `--net_delay` (default 3; with `--net_delay_auto`, on by default, the least the host
  picks from the ping: s.6j), `--net_drop_after_s` (default 30). Both turn the fixed step on (60).
* Tests: `--debug_net_lag_ms`, `--debug_net_jitter_ms`, `--debug_net_loss_percent` (a bad network on
  this PC's sends, s.6j).
* F1 Tools -> **Network (LAN co-op test)**: Host / Join / Leave and the status.
* Debug console: `net` (status line), `net dump`, `net leave`.
