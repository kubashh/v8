// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_GDB_SERVER_GDB_SERVER_THREAD_H_
#define V8_WASM_GDB_SERVER_GDB_SERVER_THREAD_H_

#include "src/base/platform/platform.h"
#include "src/wasm/gdb-server/target.h"
#include "src/wasm/gdb-server/transport.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

class GdbServer;

class GdbServerThread : public v8::base::Thread {
 public:
  explicit GdbServerThread(GdbServer* gdb_server);

  // base::Thread
  void Run() override;

  // Stops the GDB-server thread when the V8 process shuts down; gracefully
  // closes any active debugging session.
  void Stop();

 private:
  void InitializeThread();
  void CleanupThread();

  GdbServer* gdb_server_;

  v8::base::Mutex mutex_;

  // Protected by {mutex_}:
  std::unique_ptr<Transport> transport_;
  std::unique_ptr<Target> target_;

  DISALLOW_COPY_AND_ASSIGN(GdbServerThread);
};

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_GDB_SERVER_GDB_SERVER_THREAD_H_
