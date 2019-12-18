// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8.h"
#include "src/common/globals.h"
#include "src/execution/frame-constants.h"

namespace v8 {

namespace {

bool PCIsInCodeRange(const v8::MemoryRange& code_range, void* pc) {
  // Given that the length of the memory range is in bytes and it is not
  // necessarily aligned, we need to do the pointer arithmetic in byte* here.
  const i::byte* pc_as_byte = reinterpret_cast<i::byte*>(pc);
  const i::byte* start = reinterpret_cast<const i::byte*>(code_range.start);
  const i::byte* end = start + code_range.length_in_bytes;
  return pc_as_byte >= start && pc_as_byte < end;
}

bool PCIsInCodePages(size_t code_pages_length, const MemoryRange* code_pages,
                     void* pc) {
  MemoryRange fake_range{pc, 1};
  // This relies on the fact that the code pages are ordered, and that they
  // don't overlap.
  return std::binary_search(
      code_pages, code_pages + code_pages_length, fake_range,
      [](const MemoryRange& a, const MemoryRange& b) {
        const i::byte* a_start = reinterpret_cast<const i::byte*>(a.start);
        const i::byte* a_end = a_start + a.length_in_bytes;
        const i::byte* b_start = reinterpret_cast<const i::byte*>(b.start);
        const i::byte* b_end = b_start + b.length_in_bytes;

        bool a_contains_b = a_start <= b.start && b.start < a_end;
        bool b_contains_a = b_start <= a.start && a.start < b_end;
        bool overlap = a_contains_b || b_contains_a;
        if (overlap) return false;
        return a.start < b.start;
      });
}

bool IsInJSEntryRange(const UnwindState& unwind_state, void* pc) {
  return PCIsInCodeRange(unwind_state.js_entry_stub.code, pc) ||
         PCIsInCodeRange(unwind_state.js_construct_entry_stub.code, pc) ||
         PCIsInCodeRange(unwind_state.js_run_microtasks_entry_stub.code, pc);
}

bool IsInUnsafeJSEntryRange(const UnwindState& unwind_state, void* pc) {
  return IsInJSEntryRange(unwind_state, pc);

  // TODO(petermarshall): We can be more precise by checking whether we are
  // in JSEntry but after frame setup and before frame teardown, in which case
  // we are safe to unwind the stack. For now, we bail out if the PC is anywhere
  // within JSEntry.
}

bool IsInJSEntryRange(const JSEntryStubs& entry_stubs, void* pc) {
  return PCIsInCodeRange(entry_stubs.js_entry_stub.code, pc) ||
         PCIsInCodeRange(entry_stubs.js_construct_entry_stub.code, pc) ||
         PCIsInCodeRange(entry_stubs.js_run_microtasks_entry_stub.code, pc);
}

bool IsInUnsafeJSEntryRange(const JSEntryStubs& entry_stubs, void* pc) {
  return IsInJSEntryRange(entry_stubs, pc);

  // TODO(petermarshall): We can be more precise by checking whether we are
  // in JSEntry but after frame setup and before frame teardown, in which case
  // we are safe to unwind the stack. For now, we bail out if the PC is anywhere
  // within JSEntry.
}

i::Address Load(i::Address address) {
  return *reinterpret_cast<i::Address*>(address);
}

void* GetReturnAddressFromFP(void* fp, void* pc,
                             const v8::UnwindState& unwind_state) {
  int caller_pc_offset = i::CommonFrameConstants::kCallerPCOffset;
#ifdef V8_TARGET_ARCH_ARM64
  if (IsInJSEntryRange(unwind_state, pc)) {
    caller_pc_offset = i::EntryFrameConstants::kDirectCallerPCOffset;
  }
#endif
  return reinterpret_cast<void*>(
      Load(reinterpret_cast<i::Address>(fp) + caller_pc_offset));
}

void* GetReturnAddressFromFP(void* fp, void* pc,
                             const JSEntryStubs& entry_stubs) {
  int caller_pc_offset = i::CommonFrameConstants::kCallerPCOffset;
#ifdef V8_TARGET_ARCH_ARM64
  if (IsInJSEntryRange(entry_stubs, pc)) {
    caller_pc_offset = i::EntryFrameConstants::kDirectCallerPCOffset;
  }
#endif
  return reinterpret_cast<void*>(
      Load(reinterpret_cast<i::Address>(fp) + caller_pc_offset));
}

void* GetCallerFPFromFP(void* fp, void* pc,
                        const v8::UnwindState& unwind_state) {
  int caller_fp_offset = i::CommonFrameConstants::kCallerFPOffset;
#ifdef V8_TARGET_ARCH_ARM64
  if (IsInJSEntryRange(unwind_state, pc)) {
    caller_fp_offset = i::EntryFrameConstants::kDirectCallerFPOffset;
  }
#endif
  return reinterpret_cast<void*>(
      Load(reinterpret_cast<i::Address>(fp) + caller_fp_offset));
}

void* GetCallerFPFromFP(void* fp, void* pc, const JSEntryStubs& entry_stubs) {
  int caller_fp_offset = i::CommonFrameConstants::kCallerFPOffset;
#ifdef V8_TARGET_ARCH_ARM64
  if (IsInJSEntryRange(entry_stubs, pc)) {
    caller_fp_offset = i::EntryFrameConstants::kDirectCallerFPOffset;
  }
#endif
  return reinterpret_cast<void*>(
      Load(reinterpret_cast<i::Address>(fp) + caller_fp_offset));
}

void* GetCallerSPFromFP(void* fp, void* pc,
                        const v8::UnwindState& unwind_state) {
  int caller_sp_offset = i::CommonFrameConstants::kCallerSPOffset;
#ifdef V8_TARGET_ARCH_ARM64
  if (IsInJSEntryRange(unwind_state, pc)) {
    caller_sp_offset = i::EntryFrameConstants::kDirectCallerSPOffset;
  }
#endif
  return reinterpret_cast<void*>(reinterpret_cast<i::Address>(fp) +
                                 caller_sp_offset);
}

void* GetCallerSPFromFP(void* fp, void* pc, const JSEntryStubs& entry_stubs) {
  int caller_sp_offset = i::CommonFrameConstants::kCallerSPOffset;
#ifdef V8_TARGET_ARCH_ARM64
  if (IsInJSEntryRange(entry_stubs, pc)) {
    caller_sp_offset = i::EntryFrameConstants::kDirectCallerSPOffset;
  }
#endif
  return reinterpret_cast<void*>(reinterpret_cast<i::Address>(fp) +
                                 caller_sp_offset);
}

bool AddressIsInStack(const void* address, const void* stack_base,
                      const void* stack_top) {
  return address <= stack_base && address >= stack_top;
}

}  // namespace

bool Unwinder::TryUnwindV8Frames(const UnwindState& unwind_state,
                                 RegisterState* register_state,
                                 const void* stack_base) {
  const void* stack_top = register_state->sp;

  void* pc = register_state->pc;
  if (PCIsInV8(unwind_state, pc) && !IsInUnsafeJSEntryRange(unwind_state, pc)) {
    void* current_fp = register_state->fp;
    if (!AddressIsInStack(current_fp, stack_base, stack_top)) return false;

    // Peek at the return address that the caller pushed. If it's in V8, then we
    // assume the caller frame is a JS frame and continue to unwind.
    void* next_pc = GetReturnAddressFromFP(current_fp, pc, unwind_state);
    while (PCIsInV8(unwind_state, next_pc)) {
      current_fp = GetCallerFPFromFP(current_fp, pc, unwind_state);
      if (!AddressIsInStack(current_fp, stack_base, stack_top)) return false;
      pc = next_pc;
      next_pc = GetReturnAddressFromFP(current_fp, pc, unwind_state);
    }

    void* final_sp = GetCallerSPFromFP(current_fp, pc, unwind_state);
    if (!AddressIsInStack(final_sp, stack_base, stack_top)) return false;
    register_state->sp = final_sp;

    // We don't check that the final FP value is within the stack bounds because
    // this is just the rbp value that JSEntryStub pushed. On platforms like
    // Win64 this is not used as a dedicated FP register, and could contain
    // anything.
    void* final_fp = GetCallerFPFromFP(current_fp, pc, unwind_state);
    register_state->fp = final_fp;

    register_state->pc = next_pc;

    // Link register no longer valid after unwinding.
    register_state->lr = nullptr;
    return true;
  }
  return false;
}

bool Unwinder::TryUnwindV8Frames(const JSEntryStubs& entry_stubs,
                                 size_t code_pages_length,
                                 const MemoryRange* code_pages,
                                 RegisterState* register_state,
                                 const void* stack_base) {
  const void* stack_top = register_state->sp;

  void* pc = register_state->pc;
  if (PCIsInV8(code_pages_length, code_pages, pc) &&
      !IsInUnsafeJSEntryRange(entry_stubs, pc)) {
    void* current_fp = register_state->fp;
    if (!AddressIsInStack(current_fp, stack_base, stack_top)) return false;

    // Peek at the return address that the caller pushed. If it's in V8, then we
    // assume the caller frame is a JS frame and continue to unwind.
    void* next_pc = GetReturnAddressFromFP(current_fp, pc, entry_stubs);
    while (PCIsInV8(code_pages_length, code_pages, next_pc)) {
      current_fp = GetCallerFPFromFP(current_fp, pc, entry_stubs);
      if (!AddressIsInStack(current_fp, stack_base, stack_top)) return false;
      pc = next_pc;
      next_pc = GetReturnAddressFromFP(current_fp, pc, entry_stubs);
    }

    void* final_sp = GetCallerSPFromFP(current_fp, pc, entry_stubs);
    if (!AddressIsInStack(final_sp, stack_base, stack_top)) return false;
    register_state->sp = final_sp;

    // We don't check that the final FP value is within the stack bounds because
    // this is just the rbp value that JSEntryStub pushed. On platforms like
    // Win64 this is not used as a dedicated FP register, and could contain
    // anything.
    void* final_fp = GetCallerFPFromFP(current_fp, pc, entry_stubs);
    register_state->fp = final_fp;

    register_state->pc = next_pc;

    // Link register no longer valid after unwinding.
    register_state->lr = nullptr;
    return true;
  }
  return false;
}

bool Unwinder::PCIsInV8(const UnwindState& unwind_state, void* pc) {
  return pc && (PCIsInCodeRange(unwind_state.code_range, pc) ||
                PCIsInCodeRange(unwind_state.embedded_code_range, pc));
}

bool Unwinder::PCIsInV8(size_t code_pages_length, const MemoryRange* code_pages,
                        void* pc) {
  return pc && PCIsInCodePages(code_pages_length, code_pages, pc);
}

}  // namespace v8
