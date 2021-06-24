// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#ifndef V8_BASELINE_BASELINE_OSR_INL_H_
#define V8_BASELINE_BASELINE_OSR_INL_H_

#include "src/baseline/baseline-batch-compiler.h"
#include "src/execution/frames-inl.h"
#include "src/execution/isolate-inl.h"

namespace v8 {
namespace internal {

enum CompilationMode { kCompileImmediate, kCompileBatch };

inline void OSRInterpreterFrameToBaseline(Isolate* isolate,
                                          Handle<JSFunction> function,
                                          CompilationMode compilation_mode) {
  IsCompiledScope is_compiled_scope(
      function->shared().is_compiled_scope(isolate));
  bool is_compiled = false;
  switch (compilation_mode) {
    case kCompileBatch:
      is_compiled =
          isolate->baseline_batch_compiler()->EnqueueFunction(function);
      break;
    case kCompileImmediate:
      is_compiled = Compiler::CompileBaseline(
          isolate, function, Compiler::CLEAR_EXCEPTION, &is_compiled_scope);
      break;
  }
  if (is_compiled) {
    if (V8_LIKELY(FLAG_use_osr)) {
      if (FLAG_trace_osr) {
        JavaScriptFrameIterator it(isolate);
        DCHECK(it.frame()->is_unoptimized());
        UnoptimizedFrame* frame = UnoptimizedFrame::cast(it.frame());
        CodeTracer::Scope scope(isolate->GetCodeTracer());
        PrintF(scope.file(),
               "[OSR - Entry at OSR bytecode offset %d into baseline code]\n",
               frame->GetBytecodeOffset());
      }
      function->shared(isolate)
          .GetBytecodeArray(isolate)
          .set_osr_loop_nesting_level(AbstractCode::kMaxLoopNestingMarker);
    }
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_BASELINE_OSR_INL_H_
