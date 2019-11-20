// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/gdb-server/gdb-server.h"

#include "src/wasm/gdb-server/gdb-server-thread.h"
#include "src/wasm/wasm-engine.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

bool GdbServer::Initialize() {
  thread_ = std::make_unique<GdbServerThread>(this);
  if (!thread_->Start()) {
    TRACE_GDB_REMOTE(
        "Cannot initialize thread, GDB-remote debugging will be disabled.\n");
    thread_ = nullptr;
    return false;
  }
  return true;
}

void GdbServer::Terminate() {
  if (thread_) {
    thread_->Stop();
    thread_->Join();
    thread_ = nullptr;
  }
}

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8
