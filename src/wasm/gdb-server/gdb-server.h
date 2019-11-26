// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_GDB_SERVER_GDB_SERVER_H_
#define V8_WASM_GDB_SERVER_GDB_SERVER_H_

#include <map>
#include <memory>
#include "src/debug/debug.h"
#include "src/wasm/gdb-server/gdb-server-thread.h"

namespace v8 {
namespace internal {
namespace wasm {

class WasmEngine;

namespace gdb_server {

// class GdbServer acts as a manager for the GDB-remote stub. It is instantiated
// as soon as the first Wasm module is loaded in the Wasm engine and spawns a
// separate thread to accept connections and exchange messages with a debugger.
// It will contain the logic to serve debugger queries and access the state of
// the Wasm engine.
class GdbServer {
 public:
  GdbServer() {}

  // Spawns a "GDB-remote" thread that will be used to communicate with the
  // debugger. This should be called once, the first time a Wasm module is
  // loaded in the Wasm engine.
  bool Initialize();

  // Stops the "GDB-remote" thread and waits for it to complete. This should be
  // called once, when the Wasm engine shuts down.
  void Shutdown();

  // Manage the set of Isolates for this GdbServer.
  void AddIsolate(Isolate* isolate);
  void RemoveIsolate(Isolate* isolate);

 private:
  void AddWasmModule(Local<debug::WasmScript> wasm_script);

  // Class DebugDelegate implements the debug::DebugDelegate interface to
  // receive notifications when debug events happen in a given isolate, like a
  // script being loaded, a breakpoint being hit, an exception being thrown.
  class DebugDelegate : public debug::DebugDelegate {
   public:
    DebugDelegate(Isolate* isolate, GdbServer* gdb_server);

    // debug::DebugDelegate
    void ScriptCompiled(Local<debug::Script> script, bool is_live_edited,
                        bool has_compile_error) override;
    void BreakProgramRequested(Local<v8::Context> paused_context,
                               const std::vector<debug::BreakpointId>&
                                   inspector_break_points_hit) override;
    void ExceptionThrown(Local<v8::Context> paused_context,
                         Local<Value> exception, Local<Value> promise,
                         bool is_uncaught,
                         debug::ExceptionType exception_type) override;
    bool IsFunctionBlackboxed(Local<debug::Script> script,
                              const debug::Location& start,
                              const debug::Location& end) override;

   private:
    Isolate* isolate_;
    GdbServer* gdb_server_;
  };

  // Contains a global handle for a WasmScript
  struct WasmDebugScript {
    WasmDebugScript(v8::Isolate* isolate, Local<debug::WasmScript> script);

    v8::Isolate* isolate_;
    Global<debug::WasmScript> wasm_script_;
  };

  // The thread
  std::unique_ptr<GdbServerThread> thread_;

  base::RecursiveMutex mutex_;
  //////////////////////////////////////////////////////////////////////////////
  // Protected by {mutex_}:

  typedef std::map<Isolate*, std::unique_ptr<DebugDelegate>>
      IsolateDebugDelegateMap;
  IsolateDebugDelegateMap isolate_delegates_;

  typedef std::map<int, std::unique_ptr<WasmDebugScript>> ScriptsMap;
  ScriptsMap scripts_;

  // End of fields protected by {mutex_}.
  //////////////////////////////////////////////////////////////////////////////

  DISALLOW_COPY_AND_ASSIGN(GdbServer);
};

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_GDB_SERVER_GDB_SERVER_H_
