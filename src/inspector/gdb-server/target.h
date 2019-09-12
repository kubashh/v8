// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_GDB_SERVER_TARGET_H_
#define V8_INSPECTOR_GDB_SERVER_TARGET_H_

#include <map>
#include <queue>
#include <string>
#include "include/v8-inspector.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/semaphore.h"

namespace v8_inspector {

class GdbServer;
class Packet;
class Session;

class WasmThread {
 public:
  void SetStep(bool on) {}
  void ResumeThread() {}
};

class Target {
 public:
  enum ErrDef { NONE = 0, BAD_FORMAT = 1, BAD_ARGS = 2, FAILED = 3 };

  typedef std::map<uint32_t, WasmThread*> ThreadMap_t;
  typedef std::map<std::string, std::string> PropertyMap_t;
  // typedef std::map<uint32_t, uint8_t*> BreakpointMap_t;

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

  void OnPaused(const std::vector<uint64_t>& call_frames);

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

  bool GetFirstThreadId(uint32_t* id);
  bool GetNextThreadId(uint32_t* id);
  WasmThread* GetRegThread();
  WasmThread* GetThread(uint32_t id);

  bool AddBreakpoint(uint64_t user_address);
  bool RemoveBreakpoint(uint64_t user_address);

  bool AreInitialBreakpointsActive();
  void RemoveInitialBreakpoints();

  void SuspendAllThreads();
  void ResumeAllThreads();
  void Resume();

  void SetStopReply(Packet* pktOut) const;

  std::string GetThreadPcsString() const;

  static void ProcessDebugCommands(v8::Isolate* isolate, void* data);
  void ProcessDebugCommands();

  uint64_t GetCurrentPc() const;

  GdbServer* gdb_server_;

  Session* session_;

  ThreadMap_t threads_;
  ThreadMap_t::const_iterator threadItr_;

  bool initial_breakpoints_active_;

  PropertyMap_t properties_;

  // Signal being processed.
  // Set to 0 when execution was interrupted by GDB and not by a signal.
  int8_t cur_signal_;

  // Signaled thread id.
  // Set to 0 when execution was interrupted by GDB and not by a signal.
  uint32_t sig_thread_;

  // Thread for subsequent registers access operations.
  uint32_t reg_thread_;

  // Thread that is stepping over a breakpoint while other threads remain
  // suspended.
  uint32_t step_over_breakpoint_thread_;

  // Whether all threads are currently suspended.
  bool all_threads_suspended_;

  // Whether we are about to detach.
  bool detaching_;

  // Whether we are about to exit (from kill).
  bool should_exit_;

  enum ProcessStatus { Running, WaitingForPause, Paused };
  ProcessStatus process_status_;

  enum DebugCommand { Pause };
  std::queue<DebugCommand> commands_queue_;

  v8::base::Mutex mutex_;

  v8::base::Semaphore semaphore_;

  std::vector<uint64_t> call_frames_;
};

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_GDB_SERVER_TARGET_H_
