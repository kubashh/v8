// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DIAGNOSTICS_UNWINDER_H_
#define V8_DIAGNOSTICS_UNWINDER_H_

#include <algorithm>

#include "include/v8.h"
#include "src/common/globals.h"
#include "src/execution/frame-constants.h"
#include "src/execution/pointer-authentication.h"

namespace v8 {

// Helper methods, common to all archs
const i::byte* CalculateEnd(const void* start, size_t length_in_bytes);

bool PCIsInCodeRange(const v8::MemoryRange& code_range, void* pc);
bool PCIsInCodePages(size_t code_pages_length, const MemoryRange* code_pages,
                     void* pc);
bool IsInJSEntryRange(const JSEntryStubs& entry_stubs, void* pc);
bool IsInUnsafeJSEntryRange(const JSEntryStubs& entry_stubs, void* pc);

bool AddressIsInStack(const void* address, const void* stack_base,
                      const void* stack_top);

i::Address Load(i::Address address);

void* GetReturnAddressFromFP(void* fp, void* pc,
                             const JSEntryStubs& entry_stubs);
void* GetCallerFPFromFP(void* fp, void* pc, const JSEntryStubs& entry_stubs);
void* GetCallerSPFromFP(void* fp, void* pc, const JSEntryStubs& entry_stubs);

// Architecture specific. Implemented in unwinder-<arch>.cc.
void RestoreCalleeSavedRegisters(void* fp, RegisterState* register_state);

}  // namespace v8

#endif  // V8_DIAGNOSTICS_UNWINDER_H_
