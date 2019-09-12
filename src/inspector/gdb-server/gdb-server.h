// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_GDB_SERVER_GDB_SERVER_H_
#define V8_INSPECTOR_GDB_SERVER_GDB_SERVER_H_

#include <map>
#include "include/v8-inspector.h"
#include "src/inspector/protocol/Debugger.h"
#include "src/inspector/protocol/Forward.h"

namespace v8_inspector {

class InspectorClient;
class GdbServerThread;

class GdbServer {
 public:
  explicit GdbServer(v8::Isolate* isolate);
  ~GdbServer();

  v8::Isolate* isolate() const;

  void onWasmModuleAdded(int moduleId, uint32_t codeOffset,
                         const std::string& moduleName,
                         const std::string& sourceMappingURL);
  void sendPauseRequest();
  void onPaused(const std::vector<uint64_t>& callFrames);

  std::string getWasmModuleString() const;
  void quitMessageLoopOnPause();

  bool getWasmGlobal(uint32_t wasmModuleId, uint32_t index, uint64_t* value);
  bool getWasmLocal(uint32_t wasmModuleId, uint32_t index, uint64_t* value);
  bool getWasmStackValue(uint32_t wasmModuleId, uint32_t index,
                         uint64_t* value);
  bool getWasmMemory(uint32_t offset, uint8_t* buffer, uint32_t size);
  bool getWasmCallStack(std::vector<uint64_t>* callStackPCs);
  bool addBreakpoint(uint64_t address);
  bool removeBreakpoint(uint64_t address);
  void step();

  void addInitialBreakpoints();
  void removeInitialBreakpoints();

  static int getSessionMessageId();

 private:
  int CreateContextGroup();

  const uint64_t code_offset_ = 0;
  std::unique_ptr<GdbServerThread> thread_;
  v8::Isolate* isolate_;
  std::unique_ptr<V8InspectorSession> session_;
  InspectorClient* inspector_client_;

  struct Module {
    int module_id_;
    uint32_t code_offset_;
    std::string module_name_;
    std::string source_mapping_url_;
  };
  std::map<int, Module> modules_;
};

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_GDB_SERVER_GDB_SERVER_H_
