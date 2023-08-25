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
  explicit HprofWriter(Isolate* isolate);
  ~HprofWriter();

  void Start();
  void Finish();

  void AddRoot(HeapObject obj, Root root);
  void AddHeapObject(HeapObject obj, InstanceType instance_type);

 private:
  std::unique_ptr<HprofWriterImpl> impl_;
};

}  // namespace v8::internal

#endif  // V8_PROFILER_HPROF_WRITER_H_
