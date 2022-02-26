// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/arguments.h"

#include "src/execution/simulator.h"
#include "src/objects/code-inl.h"

namespace v8 {
namespace internal {

void ClobberDoubleRegisters(Isolate* isolate) {
  Handle<CodeT> code = BUILTIN_CODE(isolate, ClobberDoubleRegisters);
  using ClobberDoubleRegistersFunction = GeneratedCode<void()>;
  ClobberDoubleRegistersFunction stub_entry =
      ClobberDoubleRegistersFunction::FromAddress(isolate,
                                                  code->InstructionStart());
  stub_entry.Call();
}

}  // namespace internal
}  // namespace v8
