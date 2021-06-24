// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#ifndef V8_BASELINE_BASELINE_OSR_INL_H_
#define V8_BASELINE_BASELINE_OSR_INL_H_

#include "src/baseline/baseline-batch-compiler.h"
#include "src/execution/isolate-inl.h"

namespace v8 {
namespace internal {

enum CompilationMode { kCompileImmediate, kCompileBatch };

inline void OSRInterpreterFrameToBaseline(Isolate* isolate,
                                          Handle<JSFunction> function,
                                          CompilationMode compilation_mode) {
  IsCompiledScope is_compiled_scope(
      function->shared().is_compiled_scope(isolate));
  switch (compilation_mode) {
    case kCompileBatch:
      isolate->baseline_batch_compiler()->EnqueueFunction(function);
      break;
    case kCompileImmediate:
      Compiler::CompileBaseline(isolate, function, Compiler::CLEAR_EXCEPTION,
                                &is_compiled_scope);
      break;
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_BASELINE_OSR_INL_H_
