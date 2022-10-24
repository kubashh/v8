// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>

#include "src/base/page-allocator.h"
#include "src/base/platform/platform.h"
#include "src/base/utils/random-number-generator.h"
#include "src/execution/isolate.h"
#include "src/heap/code-range.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/init/v8.h"
#include "src/tasks/task-utils.h"
#include "src/tracing/trace-event.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {
namespace ipc {

static int g_oopc_pid = -1;
static int g_sock_client = -1;
static int g_sock_server = -1;
static char g_name[256];
static struct sockaddr_un g_addr;
static bool g_ready = false;

bool HasOOPC() { return v8_flags.oopc != nullptr; }

namespace {
[[noreturn]] int ExecOOPC(void* arg) {
  std::string oopc(v8_flags.oopc);

  size_t libdir_end = oopc.rfind('/');
  if (libdir_end == std::string::npos) {
    FATAL("Could not find trailing '/' in %s\n", oopc.c_str());
  }
  std::string ld_library_path =
      "LD_LIBRARY_PATH=" + std::string(oopc, 0, libdir_end + 1);

  std::string socket = std::to_string(g_sock_server);

  std::shared_ptr<CodeRange> code_range = CodeRange::GetProcessWideCodeRange();
  std::string code_fd = std::to_string(code_range->shared_memory_handle());
  std::string code_offset = std::to_string(code_range->offset());
  std::string max_code_size =
      std::to_string(MemoryChunkLayout::MaxRegularCodeObjectSize());

  const char* envp[] = {ld_library_path.c_str(), NULL};
  const char* argv[] = {
      oopc.c_str(),        socket.c_str(),        g_name, code_fd.c_str(),
      code_offset.c_str(), max_code_size.c_str(), NULL};
  execve(oopc.c_str(), const_cast<char* const*>(argv),
         const_cast<char* const*>(envp));
  FATAL("exec: Oups, %s\n", strerror(errno));
}

void SpawnOOPC(Isolate* isolate) {
  TRACE_EVENT0("v8", "V8.IPC.Spawn");

  // Create an initial handshake socket pair.
  int sock[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sock) == -1) {
    FATAL("socketpair: Oups %s\n", strerror(errno));
  }
  g_sock_client = sock[0];
  g_sock_server = sock[1];

  // Make the client CLOEXEC
  {
    int flags = fcntl(g_sock_client, F_GETFD);
    if (flags == -1) {
      FATAL("fnctl: Oups, %s\n", strerror(errno));
    }
    if (fcntl(g_sock_client, F_SETFD, flags | FD_CLOEXEC) == -1) {
      FATAL("fcntl: Oups, %s\n", strerror(errno));
    }
  }

  base::RandomNumberGenerator rng;
  if (v8_flags.random_seed != 0) {
    rng.SetSeed(v8_flags.random_seed);
  }
  snprintf(g_name, sizeof(g_name), "v8-oopc.%u", rng.NextInt());

  memset(&g_addr, 0, sizeof(g_addr));
  g_addr.sun_family = AF_UNIX;
  snprintf(g_addr.sun_path, sizeof(g_addr.sun_path) - 1, "#%s", g_name);
  g_addr.sun_path[0] = '\0';

  base::PageAllocator page_allocator;

  size_t stack_size = page_allocator.AllocatePageSize() * 10;
  uintptr_t stack = reinterpret_cast<uintptr_t>(page_allocator.AllocatePages(
      nullptr, stack_size, page_allocator.AllocatePageSize(),
      PageAllocator::Permission::kReadWrite));
  int flags = CLONE_FILES | CLONE_FS;
  if (!v8_flags.oopc_copy_vm) {
    flags |= CLONE_VM;
  }
  g_oopc_pid = clone(ExecOOPC, reinterpret_cast<void*>(stack + stack_size),
                     flags, nullptr);
  if (g_oopc_pid == -1) {
    FATAL("clone: Oups, %s\n", strerror(errno));
  }

  // TODO: Wait for the OOPC process to be ready in a background thread by
  // default, this isn't stable on desktop sadly.
  if (v8_flags.oopc_background_wait) {
    auto task = MakeCancelableTask(isolate, [stack, stack_size] {
      TRACE_EVENT0("v8", "V8.IPC.BackgroundStart");
      uint8_t data = 0;
      if (recv(g_sock_client, &data, sizeof(data), 0) == -1) {
        FATAL("recv: Oups, %s\n", strerror(errno));
      }
      if (send(g_sock_client, &data, sizeof(data), 0) == -1) {
        FATAL("send: Oups, %s\n", strerror(errno));
      }
      base::PageAllocator page_allocator;
      close(g_sock_server);
      page_allocator.FreePages(reinterpret_cast<void*>(stack), stack_size);
      g_ready = true;
    });
    V8::GetCurrentPlatform()->CallOnWorkerThread(std::move(task));
    return;
  }

  uint8_t data = 0;
  if (recv(g_sock_client, &data, sizeof(data), 0) == -1) {
    FATAL("recv: Oups, %s\n", strerror(errno));
  }
  if (send(g_sock_client, &data, sizeof(data), 0) == -1) {
    FATAL("send: Oups, %s\n", strerror(errno));
  }
  close(g_sock_server);
  page_allocator.FreePages(reinterpret_cast<void*>(stack), stack_size);
  g_ready = true;
}
}  // namespace

void InitializeOncePerProcess() {}

V8_DECLARE_ONCE(spawn_oopc_once);

void Initialize(Isolate* isolate) {
  if (!HasOOPC()) return;
  base::CallOnce(&spawn_oopc_once, &SpawnOOPC, isolate);
}

void WriteCode(Address addr, byte* code, size_t size) {
  if (!HasOOPC()) return;
  CHECK(g_ready);
  TRACE_EVENT1("v8", "V8.IPC.WriteCode", "size", size);

  std::shared_ptr<CodeRange> code_range = CodeRange::GetProcessWideCodeRange();

  size_t offset = addr - code_range->base();

  int client = socket(AF_UNIX, SOCK_SEQPACKET, 0);

  if (connect(client, reinterpret_cast<const struct sockaddr*>(&g_addr),
              sizeof(g_addr)) == -1) {
    FATAL("send: Oups connect %s\n", strerror(errno));
  }

  if (send(client, code, size, 0) == -1) {
    FATAL("send: Oups, %s\n", strerror(errno));
  }

  if (send(client, &offset, sizeof(offset), 0) == -1) {
    FATAL("send: Oups, %s\n", strerror(errno));
  }

  int ret = 0;
  if (recv(client, &ret, sizeof(ret), 0) == -1) {
    FATAL("recv: Oups, %s\n", strerror(errno));
  }

  CHECK_EQ(ret, 42);
  close(client);
}

void DisposeOncePerProcess() {
  if (!HasOOPC()) return;
  kill(g_oopc_pid, SIGKILL);
  int status = 0;
  waitpid(g_oopc_pid, &status, WEXITED);
  g_oopc_pid = -1;
}

}  // namespace ipc
}  // namespace internal
}  // namespace v8
