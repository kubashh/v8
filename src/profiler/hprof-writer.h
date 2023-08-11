// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PROFILER_HPROF_WRITER_H_
#define V8_PROFILER_HPROF_WRITER_H_

#include <memory>

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-objects.h"
#endif

namespace v8::internal {

class HprofWriterImpl;

class HprofWriter {
 public:
  HprofWriter();
  ~HprofWriter();

  void Start();
  void Finish();

#if V8_ENABLE_WEBASSEMBLY
  void AddWasmStruct(WasmStruct obj);
  void AddWasmArray(WasmArray obj);
  void AddWasmMap(Map map);
#endif

 private:
  std::unique_ptr<HprofWriterImpl> impl_;
};

}  // namespace v8::internal

#endif  // V8_PROFILER_HPROF_WRITER_H_
