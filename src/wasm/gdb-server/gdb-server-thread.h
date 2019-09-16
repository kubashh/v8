// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_GDB_SERVER_GDB_SERVER_THREAD_H_
#define V8_INSPECTOR_GDB_SERVER_GDB_SERVER_THREAD_H_

#include <queue>
#include "include/v8-inspector.h"
#include "src/base/platform/platform.h"
#include "src/wasm/gdb-server/target.h"
#include "src/wasm/gdb-server/transport.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

class GdbServer;

enum ProcessStatus { Running, WaitingForPause, Paused };

class GdbServerThread : public v8::base::Thread {
 public:
  enum DebugCommand { Pause };
  explicit GdbServerThread(GdbServer* gdb_server);

  static void ProcessDebugMessages(Isolate* isolate, void* data);
  void Run() override;

  void OnPaused(const std::vector<uint64_t>& callFrames);
  void OnPaused();

  const std::vector<uint64_t>& get_call_stack() const;

 private:
  void pause();
  void sendPauseRequest();
  void sendStepIntoRequest();

  GdbServer* gdb_server_;
  std::queue<DebugCommand> commands_queue_;
  SOCKET socket_;

  ProcessStatus process_status_;

  std::unique_ptr<Transport> transport_;
  std::unique_ptr<Target> target_;
};

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_INSPECTOR_GDB_SERVER_GDB_SERVER_THREAD_H_
