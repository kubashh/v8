// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/oopc/oopc.h"

#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"
#include "src/common/globals.h"

namespace v8 {
namespace oopc {

namespace {
template <typename T>
base::Optional<T> Recv(int sock) {
  T data = 0;
  if (recv(sock, &data, sizeof(data), 0) == -1) {
    base::OS::Print("recv: Oups %s\n", strerror(errno));
    return base::nullopt;
  }
  return data;
}
bool Send(int sock, uint8_t data) {
  if (send(sock, &data, sizeof(data), 0) == -1) {
    base::OS::Print("send: Oups, %s\n", strerror(errno));
    return false;
  }
  return true;
}

int Accept(int sock) {
  struct sockaddr_un peer;
  socklen_t peer_size = sizeof(peer);
  int client =
      accept(sock, reinterpret_cast<struct sockaddr*>(&peer), &peer_size);
  if (client == -1) {
    base::OS::Print("oopc %d: Oups accept %s\n",
                    base::OS::GetCurrentProcessId(), strerror(errno));
    return -1;
  }
  return client;
}
}  // namespace

void Main(int argc, char* argv[]) {
  base::OS::Print("oopc %d: Hello world!\n", base::OS::GetCurrentProcessId());

  int sock = stoi(std::string(argv[1]));
  std::string id = std::string(argv[2]);
  int code_fd = stoi(std::string(argv[3]));
  int code_offset = stoi(std::string(argv[4]));
  int max_code_size = stoi(std::string(argv[5]));

  base::OS::Print("oopc %d: Listen on %s, code %d:%d, max %d\n",
                  base::OS::GetCurrentProcessId(), id.c_str(), code_fd,
                  code_offset, max_code_size);

  int server = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (server == -1) {
    base::OS::Print("oopc %d: Oups socket %s\n",
                    base::OS::GetCurrentProcessId(), strerror(errno));
    return;
  }

  struct sockaddr_un addr;

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path) - 1, "#%s", id.c_str());
  addr.sun_path[0] = '\0';

  if (bind(server, reinterpret_cast<const struct sockaddr*>(&addr),
           sizeof(addr)) == -1) {
    base::OS::Print("oopc %d: Oups bind %s\n", base::OS::GetCurrentProcessId(),
                    strerror(errno));
    return;
  }

  if (listen(server, 50) == -1) {
    base::OS::Print("oopc %d: Oups listen %s\n",
                    base::OS::GetCurrentProcessId(), strerror(errno));
    return;
  }

  void* code_space =
      mmap(nullptr, v8::internal::kMaximalCodeRangeSize, PROT_READ | PROT_WRITE,
           MAP_SHARED, code_fd, code_offset);
  if (code_space == MAP_FAILED) {
    base::OS::Print("oopc %d: Oups %s\n", base::OS::GetCurrentProcessId(),
                    strerror(errno));
    return;
  }
  if (madvise(code_space, v8::internal::kMaximalCodeRangeSize, MADV_DONTNEED) ==
      -1) {
    base::OS::Print("oopc %d: Oups %s\n", base::OS::GetCurrentProcessId(),
                    strerror(errno));
    return;
  }

  uint8_t* buffer = new uint8_t[max_code_size];

  // Signal that we're ready
  if (!Send(sock, 42)) {
    base::OS::Print("oopc %d: Could not send\n",
                    base::OS::GetCurrentProcessId());
    return;
  }
  auto ok = Recv<uint8_t>(sock);
  if (!ok.has_value()) return;
  if (*ok != 42) {
    base::OS::Print("oopc %d: Unexpected data %d\n",
                    base::OS::GetCurrentProcessId(), *ok);
    return;
  }

  base::OS::Print("oopc %d: Let's goooo\n", base::OS::GetCurrentProcessId());

  while (1) {
    int connection = Accept(server);
    if (connection == -1) return;
    ssize_t size = recv(connection, buffer, max_code_size, 0);
    if (size == -1) {
      base::OS::Print("oopc %d: Oups %s\n", base::OS::GetCurrentProcessId(),
                      strerror(errno));
      return;
    }
    auto offset = Recv<size_t>(connection);
    if (!offset.has_value()) return;

    void* start = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(code_space) + *offset);
    memcpy(start, buffer, size);

    if (!Send(connection, 42)) {
      base::OS::Print("oopc %d: Could not send\n",
                      base::OS::GetCurrentProcessId());
      return;
    }
    close(connection);
  }
}

}  // namespace oopc
}  // namespace v8
