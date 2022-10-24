// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_IPC_H_
#define V8_EXECUTION_IPC_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

class Isolate;

namespace ipc {

bool HasOOPC();

void InitializeOncePerProcess();
void Initialize(Isolate* isolate);

void WriteCode(Address addr, byte* code, size_t size);

void DisposeOncePerProcess();

}  // namespace ipc
}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_IPC_H_
