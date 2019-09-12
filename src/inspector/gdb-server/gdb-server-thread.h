// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_GDB_SERVER_GDB_SERVER_THREAD_H_
#define V8_INSPECTOR_GDB_SERVER_GDB_SERVER_THREAD_H_

#include <queue>
#include "include/v8-inspector.h"
#include "src/base/platform/platform.h"
#include "src/inspector/gdb-server/target.h"
#include "src/inspector/gdb-server/transport.h"
#include "src/inspector/protocol/Debugger.h"
#include "src/inspector/protocol/Forward.h"

namespace v8_inspector {

class GdbServer;

enum ProcessStatus { Running, WaitingForPause, Paused };

class GdbServerThread : public v8::base::Thread {
 public:
  enum DebugCommand { Pause };
  explicit GdbServerThread(GdbServer* gdb_server,
                           V8InspectorClient* inspector_client,
                           V8InspectorSession* session);

  static void ProcessDebugMessages(v8::Isolate* isolate, void* data);
  void Run() override;

  void OnPaused(const std::vector<uint64_t>& callFrames);

 private:
  void pause();
  void sendPauseRequest();
  void sendStepIntoRequest();

  uint64_t get_current_pc() const;
  std::string get_thread_pcs_string() const;

  GdbServer* gdb_server_;
  V8InspectorSession* session_ = nullptr;
  std::queue<DebugCommand> commands_queue_;
  SOCKET socket_;

  ProcessStatus process_status_;
  std::unique_ptr<V8Inspector> inspector_;

  std::unique_ptr<Transport> transport_;
  std::unique_ptr<Target> target_;
};

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_GDB_SERVER_GDB_SERVER_THREAD_H_
