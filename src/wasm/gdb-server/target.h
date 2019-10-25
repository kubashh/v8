// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_GDB_SERVER_TARGET_H_
#define V8_INSPECTOR_GDB_SERVER_TARGET_H_

#include <map>
#include <queue>
#include <string>
#include "src/base/platform/mutex.h"
#include "src/base/platform/semaphore.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

class GdbServer;
class Packet;
class Session;

class Target {
 public:
  enum ErrDef { NONE = 0, BAD_FORMAT = 1, BAD_ARGS = 2, FAILED = 3 };

 public:
  // Contruct a Target object.
  explicit Target(GdbServer* gdb_server);
  ~Target();

  // Init must be the first function called to correctlty
  // build the Target internal structures.
  bool Init();

  // This function will spin on a session, until it closes.  If an
  // exception is caught, it will signal the exception thread by
  // setting sig_done_.
  void Run(Session* ses);

  void OnSuspended(const std::vector<uint64_t>& call_frames);

  const std::vector<uint64_t>& GetCallStack() const { return call_frames_; }

 private:
  // This function always succeedes, since all errors
  // are reported as an error string of "E<##>" where
  // the two digit number.  The error codes are not
  // not documented, so this implementation uses
  // ErrDef as errors codes.  This function returns
  // true a request to continue (or step) is processed.
  bool ProcessPacket(Packet* pktIn, Packet* pktOut);

  void Destroy();
  void Detach();
  void Kill();

  void WaitForDebugEvent();
  void ProcessDebugEvent();
  void ProcessCommands();

  bool AddBreakpoint(uint64_t user_address);
  bool RemoveBreakpoint(uint64_t user_address);

  void Suspend();
  void Resume();

  void SetStopReply(Packet* pktOut) const;

  std::string GetThreadPcsString() const;

  uint64_t GetCurrentPc() const;

  GdbServer* gdb_server_;

  Session* session_;

  typedef std::map<std::string, std::string> PropertyMap_t;
  PropertyMap_t properties_;

  // Signal being processed.
  // Set to 0 when execution was interrupted by GDB and not by a signal.
  int8_t cur_signal_;

  // Whether we are about to detach.
  bool detaching_;

  // Whether we are about to exit (from kill).
  bool should_exit_;

  bool waiting_for_initial_suspension_;

  enum Status { Running, WaitingForSuspension, Suspended };
  Status status_;

  // Used to block waiting for suspension
  v8::base::Semaphore semaphore_;

  v8::base::Mutex mutex_;

  std::vector<uint64_t> call_frames_;
};

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_INSPECTOR_GDB_SERVER_TARGET_H_
