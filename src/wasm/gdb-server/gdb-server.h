// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_GDB_SERVER_GDB_SERVER_H_
#define V8_INSPECTOR_GDB_SERVER_GDB_SERVER_H_

#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING

#include <memory>

namespace v8 {
namespace internal {

class Isolate;

namespace wasm {

class WasmEngine;

namespace gdb_server {

class GdbServerThread;
class TaskRunner;

class GdbServer {
 public:
  explicit GdbServer(Isolate* isolate, WasmEngine* wasm_engine);
  ~GdbServer();

  void Suspend() {
    // TODO(paolosev) - Not implemented
  }

 private:
  std::unique_ptr<GdbServerThread> thread_;
};

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
#endif  // V8_INSPECTOR_GDB_SERVER_GDB_SERVER_H_
