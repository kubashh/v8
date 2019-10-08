// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_GDB_SERVER_SESSION_H_
#define V8_INSPECTOR_GDB_SERVER_SESSION_H_

#include <map>
#include <string>

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

class Packet;
class Transport;

// Note that the Session object is not thread-safe.
class Session {
 public:
  explicit Session(Transport* transport);
  virtual ~Session();

  enum {
    IGNORE_ACK = 1,  // Do not emit or wait for '+' from RSP stream.
    USE_SEQ = 2,     // Automatically use a sequence number
    DEBUG_SEND = 4,  // Log all SENDs
    DEBUG_RECV = 8,  // Log all RECVs
    DEBUG_MASK = (DEBUG_SEND | DEBUG_RECV)
  };

 public:
  virtual void SetFlags(uint32_t flags);
  virtual void ClearFlags(uint32_t flags);
  virtual uint32_t GetFlags();

  virtual bool SendPacketOnly(Packet* packet);
  virtual bool SendPacket(Packet* packet);
  virtual bool GetPacket(Packet* packet);
  // Is there any data available right now.
  virtual bool IsDataAvailable();
  virtual bool Connected();
  virtual void Disconnect();

  void WaitForDebugStubEvent();
  bool SignalThreadEvent();

 private:
  Session(const Session&);
  Session& operator=(const Session&);

  virtual bool GetChar(char* ch);

  Transport* io_;   // Transport object not owned by the Session.
  uint32_t flags_;  // Session flags for Sequence/Ack generation.
  uint8_t seq_;     // Next sequence number to use or -1.
  bool connected_;  // Is the connection still valid.
};

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_INSPECTOR_GDB_SERVER_SESSION_H_
