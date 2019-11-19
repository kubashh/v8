// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_GDB_SERVER_SESSION_H_
#define V8_INSPECTOR_GDB_SERVER_SESSION_H_

#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

class Transport;

// Note that the Session object is not thread-safe.
class Session {
 public:
  explicit Session(Transport* transport);
  virtual ~Session();

  bool GetPacket();

  bool IsDataAvailable() const;
  bool IsConnected() const;
  void Disconnect();

  void WaitForDebugStubEvent();
  bool SignalThreadEvent();

 private:
  Session(const Session&);
  Session& operator=(const Session&);

  bool GetChar(char* ch);

  Transport* io_;   // Transport object not owned by the Session.
  bool connected_;  // Is the connection still valid.
};

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
#endif  // V8_INSPECTOR_GDB_SERVER_SESSION_H_
