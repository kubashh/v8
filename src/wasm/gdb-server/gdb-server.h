// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_GDB_SERVER_GDB_SERVER_H_
#define V8_WASM_GDB_SERVER_GDB_SERVER_H_

#include <memory>
#include "src/wasm/gdb-server/gdb-server-thread.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

class GdbServer {
 public:
  GdbServer() {}

  bool Initialize();
  void Terminate();

 private:
  std::unique_ptr<GdbServerThread> thread_;

  DISALLOW_COPY_AND_ASSIGN(GdbServer);
};

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_GDB_SERVER_GDB_SERVER_H_
