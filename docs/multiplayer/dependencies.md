# Online multiplayer, part 4: networking dependencies

Phase 1 of the online multiplayer audit (index: [README.md](README.md)).
The project has **no networking dependency today** (existing-systems.md
s.1). This page compares the candidates against the project's build: Clang
20+ only (C++23), CMake 3.25+, Ninja, Linux and Windows x86-64 (Windows
with Visual Studio's libraries), arm64 presets, GPL-3.0 licence, a
ready-built Windows release. Nothing has been built for this audit: the
"verify" items are experiment E2 (architecture.md s.8).

## 1. GameNetworkingSockets, standalone

Source: <https://github.com/ValveSoftware/GameNetworkingSockets>
(README, BUILDING.md, README_P2P.md, read for this audit).

| Topic | What its documentation says | For this project |
|---|---|---|
| Licence | BSD-3-Clause | compatible with GPL-3.0 |
| What it is | Valve's connection-oriented, message-based transport (reliable and unreliable messages, fragmentation, per-packet AES-GCM-256 encryption, Curve25519 key exchange), the same API as Steam's `ISteamNetworkingSockets` | the reliable / unreliable split the protocol needs (architecture.md s.6) |
| Platforms | Linux, Windows, macOS, iOS in CI; has shipped on consoles and other stores; IPv6 | Linux + Windows covered |
| Compiler | C++17 compiler; the library's own code needs C++11; protobuf 22+ needs C++17 for code including its headers | fine with C++23 |
| Known issue | BUILDING.md mentions link errors with LLVM 10+ (LLVM bug #46313), with GCC as a workaround | **the project can't use GCC**: verify the link with Clang 20+ on both systems (E2) |
| Crypto | one of OpenSSL 1.1.1+, libsodium (issues on Intel CPUs with AES-NI disabled), bcrypt (Windows only) | bcrypt on Windows avoids shipping OpenSSL there; Linux needs OpenSSL or libsodium |
| Protobuf | 2.6.1+; protobuf 22+ also pulls in Abseil; `protoc` and `libprotobuf` must be the same version | the heaviest part; must be built for the Windows release |
| Windows build | vcpkg in manifest mode; the vcpkg package defaults to OpenSSL with P2P off | fits the release pipeline only if vcpkg is acceptable there (PRs #4 / #19 discuss Windows dependencies) |
| P2P / NAT traversal | optional ICE: a built-in native client (`ENABLE_ICE`) or Google WebRTC (`USE_STEAMWEBRTC`), interoperable; needs **a signaling service you run**, STUN servers, and TURN relays for peers that can't traverse NAT; an example signaling server (`trivial_signaling_server.py`) | internet play without port forwarding means running servers: out of scope for the first phases |
| Not in the open-source version | Steam's authentication service, signaling service and Steam Datagram Relay | standalone connections are encrypted but not authenticated as a Steam user: say so in the UI and docs |
| LAN discovery | not provided | the port's own announcer (architecture.md s.7) |
| Next to Steamworks | both define the same API; whether one binary can hold both is to be checked if a Steam transport is ever added | verify only with Phase 7 |

## 2. Steamworks SDK

Source: <https://partner.steamgames.com/doc/api/ISteamNetworkingSockets>,
<https://partner.steamgames.com/doc/sdk/api>.

| Topic | Fact | For this project |
|---|---|---|
| Interfaces | `ISteamNetworkingSockets`, `ISteamNetworkingUtils`, `ISteamNetworkingMessages`, `ISteamMatchmaking` (lobbies, invites), `ISteamFriends` | the Steam transport and invites |
| App ID | `SteamAPI_Init` needs one; `steam_appid.txt` with `480` (Spacewar) is for development and is not to be shipped | **no App ID for this port**: blocker |
| Steam Datagram Relay | through Steam only | not available standalone |
| Distribution | `steam_api` library shipped next to the program; the SDK is under Valve's SDK agreement | **GPL-3.0 compatibility is a question for the maintainer** (feasibility.md s.7) |
| Requirement | Steam installed, running, logged in | must never be required by standalone builds: a separate CMake option, off by default, loaded only when chosen |

Verdict: designed for (the optional `SteamTransport`), not added.

## 3. ENet (fallback candidate)

| Topic | Fact | For this project |
|---|---|---|
| Licence | MIT | compatible with GPL-3.0 |
| What it is | a small C library over UDP: connections, reliable and unreliable sequenced channels, fragmentation; no encryption | enough for lockstep's tiny messages |
| Dependencies | none beyond the system's sockets | trivial to build with Clang on both systems |
| IPv6 | the original `lsalzman/enet` is IPv4-centred; forks (e.g. `zpl-c/enet`, MIT, single header) add IPv6 | pick and verify a version in E2 |
| NAT traversal | none | same as GNS without ICE |

## 4. Comparison for the first phases

| Criterion | GNS standalone | ENet |
|---|---|---|
| Reliable + unreliable messages | yes | yes |
| Encryption | yes | no |
| IPv6 | yes | fork-dependent |
| Build weight | protobuf (+ Abseil), crypto library, vcpkg on Windows | one C file / header |
| Known Clang issue | LLVM link errors mentioned | none known |
| Same API as Steam | yes (matters only if Steam is unblocked) | no |
| NAT traversal later | ICE with own signaling / STUN / TURN | none |

**Recommendation:** GNS standalone if E2 builds and links it with the
project's Clang on Linux and Windows inside the release pipeline (bcrypt on
Windows, OpenSSL or libsodium on Linux); otherwise ENet, behind the same
`Transport` interface (architecture.md s.3) so the choice can be revisited.
Lockstep sends a few KB/s, so performance doesn't decide it.

## 5. Platform sockets for LAN discovery

The announcer needs plain UDP sockets: Winsock on Windows (link
`ws2_32`), BSD sockets on Linux, in one `#ifdef`'d platform file
(AGENTS.md s.5: no POSIX calls outside platform code, no Win32 macro names
like `near`, `far`, `min`, `max` in our identifiers). Options: IPv4 subnet
broadcast and `255.255.255.255`, IPv6 multicast `ff02::1`. A firewall
may ask the first time the game listens: the docs must say so.

## 6. Requirements players will meet (to document with the feature)

| Mode | Needs |
|---|---|
| LAN | same subnet; the firewall allowing the game (Windows asks on first listen); not across isolated guest Wi-Fi |
| Direct IP on a LAN | the host's local address and port |
| Direct IP over the internet | the host's public address, the port forwarded on the host's router (UDP), no carrier-grade NAT on the host's line; IPv6 avoids NAT where both sides have it, but firewalls still apply |
| Over a VPN overlay (a player's own choice: Tailscale, ZeroTier and similar) | works as Direct IP on the overlay's addresses; LAN discovery usually doesn't cross it |
| Steam | blocked (s.2) |
