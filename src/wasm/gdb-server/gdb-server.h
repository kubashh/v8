// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_GDB_SERVER_GDB_SERVER_H_
#define V8_INSPECTOR_GDB_SERVER_GDB_SERVER_H_

#include <map>
#include <string>
#include "src/debug/debug.h"

namespace v8 {
namespace internal {

class Isolate;

namespace wasm {

class WasmEngine;

namespace gdb_server {

class GdbServerThread;
class TaskRunner;

class GdbServer : public debug::DebugDelegate {
 public:
  explicit GdbServer(Isolate* isolate, WasmEngine* wasm_engine);
  ~GdbServer();

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

  v8::Isolate* isolate() const;

  void suspend();
  void onPaused(const std::vector<uint64_t>& callFrames);
  void prepareStep();

  std::string getWasmModuleString() const;

  bool getWasmGlobal(uint32_t wasm_module_id, uint32_t index, uint64_t* value);
  bool getWasmLocal(uint32_t wasm_module_id, uint32_t frame_index,
                    uint32_t index, uint64_t* value);
  bool getWasmStackValue(uint32_t wasm_module_id, uint32_t index,
                         uint64_t* value);
  uint32_t getWasmMemory(uint32_t offset, uint8_t* buffer, uint32_t size);
  bool getWasmCallStack(std::vector<uint64_t>* callStackPCs);
  uint32_t getWasmModuleBytes(uint64_t address, uint8_t* buffer, uint32_t size);
  bool addBreakpoint(uint64_t address);
  bool removeBreakpoint(uint64_t address);

  void runMessageLoopOnPause();
  void quitMessageLoopOnPause();

  static int getSessionMessageId();

 private:
  int CreateContextGroup();

  const uint64_t code_offset_ = 0;
  std::unique_ptr<GdbServerThread> thread_;
  v8::Isolate* isolate_;

  WasmEngine* wasm_engine_;
  std::unique_ptr<TaskRunner> task_runner_;

  struct WasmDebugScript {
    WasmDebugScript(v8::Isolate* isolate, Local<debug::WasmScript> script);

    Global<debug::WasmScript> wasm_script_;
  };
  typedef std::map<int, std::unique_ptr<WasmDebugScript>> ScriptsMap;
  ScriptsMap scripts_;

  typedef std::map<uint64_t, int> BreakpointsMap;
  BreakpointsMap breakpoints_;
};

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_INSPECTOR_GDB_SERVER_GDB_SERVER_H_
