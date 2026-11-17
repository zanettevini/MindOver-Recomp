// =============================================================================
// debug/console_socket.cpp -- the debug console's connection (see the header)
// =============================================================================
//
// Linux: a Unix socket (the original) or TCP on 127.0.0.1.
// Windows: TCP on 127.0.0.1 through Winsock (ws2_32, linked in CMakeLists.txt).
//
// The protocol is the console's own, unchanged: one command per line ("\n",
// a trailing "\r" ignored), each reply ends with "@end ok" / "@end error ..."
// (written by the handler, debug_console.cpp).
// =============================================================================
#include "console_socket.h"

#include <cerrno>
#include <charconv>
#include <cstring>
#include <string_view>
#include <thread>

#include <rex/logging.h>

#if defined(_WIN32)
// Only what the sockets need from windows.h (no min/max macros).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#elif defined(__unix__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace console_socket {
namespace {

#if defined(_WIN32)
using Handle = SOCKET;
constexpr Handle kNoSocket = INVALID_SOCKET;
void DropSocket(Handle s) { closesocket(s); }
int LastError() { return WSAGetLastError(); }
#else
using Handle = int;
constexpr Handle kNoSocket = -1;
void DropSocket(Handle s) { close(s); }
int LastError() { return errno; }
#endif

bool SendAll(Handle s, std::string_view text) {
  while (!text.empty()) {
#if defined(_WIN32)
    const int n = send(s, text.data(), int(text.size()), 0);
#else
    const ssize_t n = send(s, text.data(), text.size(), MSG_NOSIGNAL);
#endif
    if (n <= 0) return false;
    text.remove_prefix(size_t(n));
  }
  return true;
}

// One connection: lines in, replies out, until the client closes it.
void ServeClient(Handle s, LineHandler handler) {
  std::string pending;
  char buf[512];
  for (;;) {
    const auto n = recv(s, buf, int(sizeof(buf)), 0);
    if (n <= 0) break;
    pending.append(buf, size_t(n));
    size_t eol;
    while ((eol = pending.find('\n')) != std::string::npos) {
      std::string line = pending.substr(0, eol);
      pending.erase(0, eol + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!SendAll(s, handler(line))) {
        DropSocket(s);
        return;
      }
    }
  }
  DropSocket(s);
}

void AcceptLoop(Handle server, const std::string& spec, LineHandler handler) {
  REXLOG_INFO("Debug console: listening on {} (client: tools/mom.py -s {})", spec, spec);
  for (;;) {
    const Handle client = accept(server, nullptr, nullptr);
    if (client == kNoSocket) {
#if !defined(_WIN32)
      if (errno == EINTR) continue;
#endif
      REXLOG_ERROR("Debug console: accept failed (error {})", LastError());
      break;
    }
    // Detached: the process ends with _Exit anyway (ReXApp::OnClosing).
    std::thread(ServeClient, client, handler).detach();
  }
  DropSocket(server);
}

// "tcp:47100" -> 47100; 0 = not a tcp: address or a bad port.
uint16_t TcpPort(std::string_view spec) {
  constexpr std::string_view kPrefix = "tcp:";
  if (spec.substr(0, kPrefix.size()) != kPrefix) return 0;
  spec.remove_prefix(kPrefix.size());
  unsigned port = 0;
  const auto [end, ec] = std::from_chars(spec.data(), spec.data() + spec.size(), port);
  if (ec != std::errc() || end != spec.data() + spec.size() || port == 0 || port > 65535) return 0;
  return uint16_t(port);
}

void ServeTcp(const std::string& spec, uint16_t port, LineHandler handler) {
#if defined(_WIN32)
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    REXLOG_ERROR("Debug console: Winsock didn't start");
    return;
  }
#endif
  const Handle server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (server == kNoSocket) {
    REXLOG_ERROR("Debug console: no socket (error {})", LastError());
    return;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  // LOOPBACK ONLY: the console can read and write the game's memory, so it
  // must never be reachable from another computer.
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      listen(server, 8) != 0) {
    REXLOG_ERROR("Debug console: can't listen on 127.0.0.1:{} (error {}; port in use?)", port,
                 LastError());
    DropSocket(server);
    return;
  }
  AcceptLoop(server, spec, std::move(handler));
}

#if defined(__unix__)
void ServeUnix(const std::string& path, LineHandler handler) {
  const int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (server < 0 || path.size() >= sizeof(address.sun_path)) {
    REXLOG_ERROR("Debug console: can't use {} (path too long, or no socket)", path);
    if (server >= 0) close(server);
    return;
  }
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  unlink(path.c_str());  // a stale socket from an earlier run
  if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      listen(server, 8) != 0) {
    REXLOG_ERROR("Debug console: can't listen on {}: {}", path, std::strerror(errno));
    close(server);
    return;
  }
  AcceptLoop(server, path, std::move(handler));
}
#endif

}  // namespace

void Serve(const std::string& spec, LineHandler handler) {
  if (const uint16_t port = TcpPort(spec)) {
    ServeTcp(spec, port, std::move(handler));
    return;
  }
  if (spec.rfind("tcp:", 0) == 0) {
    REXLOG_ERROR("Debug console: bad address {} (tcp:<port>, port 1-65535)", spec);
    return;
  }
#if defined(__unix__)
  ServeUnix(spec, std::move(handler));
#else
  REXLOG_ERROR("Debug console: {} looks like a socket path; on Windows use tcp:<port> "
               "(e.g. --debug_console=tcp:47100)", spec);
#endif
}

}  // namespace console_socket
