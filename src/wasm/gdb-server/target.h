// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_GDB_SERVER_TARGET_H_
#define V8_INSPECTOR_GDB_SERVER_TARGET_H_

#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING

#include "src/base/platform/mutex.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

class GdbServer;
class Session;

class Target {
 public:
  // Contruct a Target object.
  explicit Target(GdbServer* gdb_server);

  // This function will spin on a session, until it closes.  If an
  // exception is caught, it will signal the exception thread by
  // setting sig_done_.
  void Run(Session* ses);

  void Terminate();
  bool IsTerminated() const { return status_ == Terminated; }

 private:
  void WaitForDebugEvent();
  void ProcessCommands();

  v8::base::Mutex mutex_;
  //////////////////////////////////////////////////////////////////////////////
  // Protected by {mutex_}:

  Session* session_;

  enum Status { Running, Terminated };
  Status status_;

  // End of fields protected by {mutex_}.
  //////////////////////////////////////////////////////////////////////////////
};

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
#endif  // V8_INSPECTOR_GDB_SERVER_TARGET_H_
