// =============================================================================
// debug/console_socket.h -- the debug console's connection, on Linux AND Windows
// =============================================================================
//
// WHAT: the listening side of the debug console (debug_console.h). It accepts
// connections, splits what arrives into lines, hands each line to the
// console and sends the reply back. It knows nothing about the game.
//
// WHY A FILE OF ITS OWN: the console was a Unix socket only, so on Windows
// `replay`, `track` and `clock` (the exact-run tools of findings/29 s.6)
// could not be used at all. Windows' socket header (winsock2.h) brings in
// windows.h and its macros (min, max, near, far...: AGENTS.md s.5); keeping it
// in this one file keeps them out of the console and the game code.
//
// TWO KINDS OF ADDRESS, chosen by --debug_console's value:
//   tcp:<port>        a TCP port on 127.0.0.1 only (this computer; nothing
//                     from the network can connect). Works on Linux and
//                     Windows. Example: --debug_console=tcp:47100
//   <path>            a Unix socket at that path (Linux, as before).
// The client is the same: tools/mom.py -s tcp:47100 state
// (findings/68 s.5).
//
// Threads: Serve() blocks; the caller runs it on a thread of its own. Each
// connection gets its own detached thread (a `wait` command may take long).
// =============================================================================
#pragma once

#include <functional>
#include <string>

namespace console_socket {

// One command line in, the whole reply out (ending with the "@end" line).
using LineHandler = std::function<std::string(const std::string& line)>;

// Listens on `spec` (see above) and serves connections until the process
// ends. Returns (after logging why) only when it couldn't listen.
void Serve(const std::string& spec, LineHandler handler);

}  // namespace console_socket
