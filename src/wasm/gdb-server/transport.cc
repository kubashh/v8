// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/gdb-server/transport.h"
#include <fcntl.h>

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

// Convert string in the form of [addr][:port] where addr is a
// IPv4 address or host name, and port is a 16b tcp/udp port.
// Both portions are optional, and only the portion of the address
// provided is updated.  Values are provided in network order.
static bool StringToIPv4(const std::string& instr, uint32_t* addr,
                         uint16_t* port) {
  // Make a copy so the are unchanged unless we succeed
  uint32_t outaddr = *addr;
  uint16_t outport = *port;

  // Substrings of the full ADDR:PORT
  std::string addrstr;
  std::string portstr;

  // We should either have one or two tokens in the form of:
  //  IP - IP, NUL
  //  IP: -  IP, NUL
  //  :PORT - NUL, PORT
  //  IP:PORT - IP, PORT

  // Search for the port marker
  size_t portoff = instr.find(':');

  // If we found a ":" before the end, get both substrings
  if ((portoff != std::string::npos) && (portoff + 1 < instr.size())) {
    addrstr = instr.substr(0, portoff);
    portstr = instr.substr(portoff + 1, std::string::npos);
  } else {
    // otherwise the entire string is the addr portion.
    addrstr = instr;
    portstr = "";
  }

  // If the address portion was provided, update it
  if (addrstr.size()) {
    // Special case 0.0.0.0 which means any IPv4 interface
    if (addrstr == "0.0.0.0") {
      outaddr = 0;
    } else {
      struct hostent* host = gethostbyname(addrstr.data());

      // Check that we found an IPv4 host
      if ((NULL == host) || (AF_INET != host->h_addrtype)) return false;

      // Make sure the IP list isn't empty.
      if (0 == host->h_addr_list[0]) return false;

      // Use the first address in the array of address pointers.
      uint32_t** addrarray = reinterpret_cast<uint32_t**>(host->h_addr_list);
      outaddr = *addrarray[0];
    }
  }

  // if the port portion was provided, then update it
  if (portstr.size()) {
    int val = atoi(portstr.data());
    if ((val < 0) || (val > 65535)) return false;
    outport = ntohs(static_cast<uint16_t>(val));
  }

  // We haven't failed, so set the values
  *addr = outaddr;
  *port = outport;
  return true;
}

static bool BuildSockAddr(const char* addr, struct sockaddr_in* sockaddr) {
  std::string addrstr = addr;
  uint32_t* pip = reinterpret_cast<uint32_t*>(&sockaddr->sin_addr.s_addr);
  uint16_t* pport = reinterpret_cast<uint16_t*>(&sockaddr->sin_port);

  sockaddr->sin_family = AF_INET;
  return StringToIPv4(addrstr, pip, pport);
}

SocketBinding::SocketBinding(SocketHandle socket_handle)
    : socket_handle_(socket_handle) {}

SocketBinding* SocketBinding::Bind(const char* addr) {
  SocketHandle socket_handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket_handle == InvalidSocket) {
    GdbRemoteLog(LOG_ERROR, "Failed to create socket.\n");
    return nullptr;
  }
  struct sockaddr_in saddr;
  // Clearing sockaddr_in first appears to be necessary on Mac OS X.
  memset(&saddr, 0, sizeof(saddr));
  socklen_t addrlen = static_cast<socklen_t>(sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = htonl(0x7F000001);
  saddr.sin_port = htons(4014);

  // Override portions address that are provided
  if (addr) BuildSockAddr(addr, &saddr);

#if _WIN32
  // On Windows, SO_REUSEADDR has a different meaning than on POSIX systems.
  // SO_REUSEADDR allows hijacking of an open socket by another process.
  // The SO_EXCLUSIVEADDRUSE flag prevents this behavior.
  // See:
  // http://msdn.microsoft.com/en-us/library/windows/desktop/ms740621(v=vs.85).aspx
  //
  // Additionally, unlike POSIX, TCP server sockets can be bound to
  // ports in the TIME_WAIT state, without setting SO_REUSEADDR.
  int exclusive_address = 1;
  if (setsockopt(socket_handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                 reinterpret_cast<char*>(&exclusive_address),
                 sizeof(exclusive_address))) {
    GdbRemoteLog(LOG_WARNING, "Failed to set SO_EXCLUSIVEADDRUSE option.\n");
  }
#else
  // On POSIX, this is necessary to ensure that the TCP port is released
  // promptly when sel_ldr exits.  Without this, the TCP port might
  // only be released after a timeout, and later processes can fail
  // to bind it.
  int reuse_address = 1;
  if (setsockopt(socket_handle, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<char*>(&reuse_address),
                 sizeof(reuse_address))) {
    GdbRemoteLog(LOG_WARNING, "Failed to set SO_REUSEADDR option.\n");
  }
#endif

  struct sockaddr* psaddr = reinterpret_cast<struct sockaddr*>(&saddr);
  if (bind(socket_handle, psaddr, addrlen)) {
    GdbRemoteLog(LOG_ERROR, "Failed to bind server.\n");
    return nullptr;
  }

  if (listen(socket_handle, 1)) {
    GdbRemoteLog(LOG_ERROR, "Failed to listen.\n");
    return nullptr;
  }
  return new SocketBinding(socket_handle);
}

Transport* SocketBinding::CreateTransport() {
  return new Transport(socket_handle_);
}

Transport::Transport(SocketHandle s)
    : buf_(new char[kBufSize]),
      pos_(0),
      size_(0),
      handle_bind_(s),
      handle_accept_(InvalidSocket) {
#if _WIN32
  socket_event_ = WSA_INVALID_EVENT;
  faulted_thread_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (faulted_thread_event_ == NULL) {
    GdbRemoteLog(
        LOG_FATAL,
        "Transport::Transport: Failed to create event object for faulted"
        "thread\n");
  }
#else
  int fds[2];
  int ret = pipe2(fds, O_CLOEXEC);
  if (ret < 0) {
    GdbRemoteLog(
        LOG_FATAL,
        "Transport::Transport: Failed to allocate pipe for faulted thread\n");
  }
  faulted_thread_fd_read_ = fds[0];
  faulted_thread_fd_write_ = fds[1];
#endif
}

Transport::~Transport() {
  if (handle_accept_ != InvalidSocket) {
    CloseSocket(handle_accept_);
  }

#if _WIN32
  if (!CloseHandle(faulted_thread_event_)) {
    GdbRemoteLog(LOG_FATAL, "Transport::~Transport: Failed to close event\n");
  }

  if (!::WSACloseEvent(socket_event_)) {
    GdbRemoteLog(LOG_FATAL,
                 "Transport::~Transport: Failed to close socket event\n");
  }
#else
  if (close(faulted_thread_fd_read_) != 0) {
    GdbRemoteLog(LOG_FATAL, "Transport::~Transport: Failed to close event\n");
  }
  if (close(faulted_thread_fd_write_) != 0) {
    GdbRemoteLog(LOG_FATAL, "Transport::~Transport: Failed to close event\n");
  }
#endif
}

void Transport::CopyFromBuffer(char** dst, int32_t* len) {
  int32_t copy_bytes = std::min(*len, size_ - pos_);
  memcpy(*dst, buf_.get() + pos_, copy_bytes);
  pos_ += copy_bytes;
  *len -= copy_bytes;
  *dst += copy_bytes;
}

bool Transport::ReadSomeData() {
  while (true) {
    ssize_t result =
        ::recv(handle_accept_, buf_.get() + size_, kBufSize - size_, 0);
    if (result > 0) {
      size_ += result;
      return true;
    }
    if (result == 0) return false;
#if _WIN32
    // WSAEventSelect sets socket to non-blocking mode. This is essential
    // for socket event notification to work, there is no workaround.
    // See remarks section at the page
    // http://msdn.microsoft.com/en-us/library/windows/desktop/ms741576(v=vs.85).aspx
    if (SocketGetLastError() == WSAEWOULDBLOCK) {
      if (WaitForSingleObject(socket_event_, INFINITE) == WAIT_FAILED) {
        GdbRemoteLog(
            LOG_FATAL,
            "Transport::ReadSomeData: Failed to wait on socket event\n");
      }
      if (!ResetEvent(socket_event_)) {
        GdbRemoteLog(LOG_FATAL,
                     "Transport::ReadSomeData: Failed to reset socket event\n");
      }
      continue;
    }
#endif
    if (SocketGetLastError() != EINTR) return false;
  }
}

bool Transport::Read(void* ptr, int32_t len) {
  char* dst = static_cast<char*>(ptr);
  if (pos_ < size_) {
    CopyFromBuffer(&dst, &len);
  }
  while (len > 0) {
    pos_ = 0;
    size_ = 0;
    if (!ReadSomeData()) {
      return false;
    }
    CopyFromBuffer(&dst, &len);
  }
  return true;
}

bool Transport::Write(const void* ptr, int32_t len) {
  const char* src = static_cast<const char*>(ptr);
  while (len > 0) {
    ssize_t result = ::send(handle_accept_, src, len, 0);
    if (result > 0) {
      src += result;
      len -= result;
      continue;
    }
    if (result == 0) {
      return false;
    }
    if (SocketGetLastError() != EINTR) {
      return false;
    }
  }
  return true;
}

void Transport::WaitForDebugStubEvent() {
  // Don't wait if we already have data to read.
  bool wait = !(pos_ < size_);

#if _WIN32
  HANDLE handles[2];
  handles[0] = faulted_thread_event_;
  handles[1] = socket_event_;
  int count = size_ < kBufSize ? 2 : 1;
  int result =
      WaitForMultipleObjects(count, handles, FALSE, wait ? INFINITE : 0);
  if (result == WAIT_OBJECT_0 + 1) {
    if (!ResetEvent(socket_event_)) {
      GdbRemoteLog(LOG_FATAL,
                   "Transport::WaitForDebugStubEvent: "
                   "Failed to reset socket event\n");
    }
    return;
  } else if (result == WAIT_OBJECT_0) {
    if (!ResetEvent(faulted_thread_event_)) {
      GdbRemoteLog(LOG_FATAL,
                   "Transport::WaitForDebugStubEvent: "
                   "Failed to reset event\n");
    }
    return;
  } else if (result == WAIT_TIMEOUT) {
    return;
  }
  GdbRemoteLog(LOG_FATAL,
               "Transport::WaitForDebugStubEvent: Wait for events failed\n");
#else
  fd_set fds;

  FD_ZERO(&fds);
  FD_SET(faulted_thread_fd_read_, &fds);
  int max_fd = faulted_thread_fd_read_;
  if (size_ < kBufSize) {
    FD_SET(handle_accept_, &fds);
    max_fd = std::max(max_fd, handle_accept_);
  }

  int ret;
  // We don't need sleep-polling on Linux now, so we set either zero or infinite
  // timeout.
  if (wait) {
    ret = select(max_fd + 1, &fds, NULL, NULL, NULL);
  } else {
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    ret = select(max_fd + 1, &fds, NULL, NULL, &timeout);
  }
  if (ret < 0) {
    GdbRemoteLog(LOG_FATAL,
                 "Transport::WaitForDebugStubEvent: Failed to wait for "
                 "debug stub event\n");
  }

  if (ret > 0) {
    if (FD_ISSET(faulted_thread_fd_read_, &fds)) {
      char buf[16];
      if (read(faulted_thread_fd_read_, &buf, sizeof(buf)) < 0) {
        GdbRemoteLog(LOG_FATAL,
                     "Transport::WaitForDebugStubEvent: Failed to read from "
                     "debug stub event pipe fd\n");
      }
    }
    if (FD_ISSET(handle_accept_, &fds)) ReadSomeData();
  }
#endif
}

bool Transport::SignalThreadEvent() {
#if _WIN32
  if (!SetEvent(faulted_thread_event_)) {
    return false;
  }
#else
  // Notify the debug stub by marking the thread as faulted.
  char buf = 0;
  if (write(faulted_thread_fd_write_, &buf, sizeof(buf)) != sizeof(buf)) {
    GdbRemoteLog(LOG_FATAL, "SignalThreadEvent: Can't send debug stub event\n");
    return false;
  }
#endif
  return true;
}

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8
