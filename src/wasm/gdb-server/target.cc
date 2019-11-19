// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING

#include "src/wasm/gdb-server/target.h"

#include "src/base/platform/time.h"
#include "src/wasm/gdb-server/gdb-server.h"
#include "src/wasm/gdb-server/session.h"
#include "src/wasm/gdb-server/transport.h"
#include "src/wasm/gdb-server/util.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

Target::Target(GdbServer* gdb_server)
    : session_(nullptr), status_(Status::Running) {}

void Target::Terminate() {
  // Executed in the Isolate thread
  v8::base::MutexGuard guard(&mutex_);

  status_ = Status::Terminated;
}

void Target::Run(Session* session) {
  // Executed in the GdbServer thread
  v8::base::MutexGuard guard(&mutex_);

  session_ = session;
  do {
    WaitForDebugEvent();
    ProcessCommands();
  } while (!IsTerminated() && session_->IsConnected());
  session_ = nullptr;
}

void Target::WaitForDebugEvent() {
  // Executed in the GdbServer thread

  if (status_ != Status::Terminated) {
    // Wait for either:
    //   * the thread to fault (or single-step)
    //   * an interrupt from LLDB
    session_->WaitForDebugStubEvent();
  }
}

void Target::ProcessCommands() {
  // GDB-remote messages are processed in the GDBServer thread.
  // Here the wasm engine should already be suspended.

  if (IsTerminated()) {
    return;
  }

  // TODO(paolosev)
  // For the moment just discard any packet we receive from the debugger.
  do {
    if (!session_->GetPacket()) continue;
  } while (session_->IsConnected());
}

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
