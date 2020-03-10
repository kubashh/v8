// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/assembler.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/pointer-authentication.h"
#include "src/execution/simulator.h"
#include "src/objects/js-regexp-inl.h"
#include "src/regexp/regexp-interpreter.h"
#include "src/regexp/regexp-macro-assembler.h"
#include "src/regexp/regexp-stack.h"

namespace v8 {
namespace internal {

// This method is called through an external reference from RegExpExecInternal
// builtin.
IrregexpInterpreter::Result IrregexpInterpreter::MatchForCallFromJs(
    Address subject, int32_t start_position, Address, Address, int* registers,
    int32_t registers_length, Address, RegExp::CallOrigin call_origin,
    Isolate* isolate, Address regexp) {
  DCHECK_NOT_NULL(isolate);
  DCHECK_NOT_NULL(registers);
  DCHECK(call_origin == RegExp::CallOrigin::kFromJs);

  DisallowHeapAllocation no_gc;
  DisallowJavascriptExecution no_js(isolate);

  String subject_string = String::cast(Object(subject));
  JSRegExp regexp_obj = JSRegExp::cast(Object(regexp));

  if (regexp_obj.MarkedForTierUp()) {
    // Returning RETRY will re-enter through runtime, where actual recompilation
    // for tier-up takes place.
    return IrregexpInterpreter::RETRY;
  }

  return Match(isolate, regexp_obj, subject_string, registers, registers_length,
               start_position, call_origin);
}

IrregexpInterpreter::Result IrregexpInterpreter::MatchForCallFromRuntime(
    Isolate* isolate, Handle<JSRegExp> regexp, Handle<String> subject_string,
    int* registers, int registers_length, int start_position) {
  return Match(isolate, *regexp, *subject_string, registers, registers_length,
               start_position, RegExp::CallOrigin::kFromRuntime);
}

const byte* NativeRegExpMacroAssembler::StringCharacterPosition(
    String subject, int start_index, const DisallowHeapAllocation& no_gc) {
  if (subject.IsConsString()) {
    subject = ConsString::cast(subject).first();
  } else if (subject.IsSlicedString()) {
    start_index += SlicedString::cast(subject).offset();
    subject = SlicedString::cast(subject).parent();
  }
  if (subject.IsThinString()) {
    subject = ThinString::cast(subject).actual();
  }
  DCHECK_LE(0, start_index);
  DCHECK_LE(start_index, subject.length());
  if (subject.IsSeqOneByteString()) {
    return reinterpret_cast<const byte*>(
        SeqOneByteString::cast(subject).GetChars(no_gc) + start_index);
  } else if (subject.IsSeqTwoByteString()) {
    return reinterpret_cast<const byte*>(
        SeqTwoByteString::cast(subject).GetChars(no_gc) + start_index);
  } else if (subject.IsExternalOneByteString()) {
    return reinterpret_cast<const byte*>(
        ExternalOneByteString::cast(subject).GetChars() + start_index);
  } else {
    DCHECK(subject.IsExternalTwoByteString());
    return reinterpret_cast<const byte*>(
        ExternalTwoByteString::cast(subject).GetChars() + start_index);
  }
}

// This method may only be called after an interrupt.
int NativeRegExpMacroAssembler::CheckStackGuardState(
    Isolate* isolate, int start_index, RegExp::CallOrigin call_origin,
    Address* return_address, Code re_code, Address* subject,
    const byte** input_start, const byte** input_end) {
  DisallowHeapAllocation no_gc;
  Address old_pc = PointerAuthentication::AuthenticatePC(return_address, 0);
  DCHECK_LE(re_code.raw_instruction_start(), old_pc);
  DCHECK_LE(old_pc, re_code.raw_instruction_end());

  StackLimitCheck check(isolate);
  bool js_has_overflowed = check.JsHasOverflowed();

  if (call_origin == RegExp::CallOrigin::kFromJs) {
    // Direct calls from JavaScript can be interrupted in two ways:
    // 1. A real stack overflow, in which case we let the caller throw the
    //    exception.
    // 2. The stack guard was used to interrupt execution for another purpose,
    //    forcing the call through the runtime system.

    // Bug(v8:9540) Investigate why this method is called from JS although no
    // stackoverflow or interrupt is pending on ARM64. We return 0 in this case
    // to continue execution normally.
    if (js_has_overflowed) {
      return EXCEPTION;
    } else if (check.InterruptRequested()) {
      return RETRY;
    } else {
      return 0;
    }
  }
  DCHECK(call_origin == RegExp::CallOrigin::kFromRuntime);

  // Prepare for possible GC.
  HandleScope handles(isolate);
  Handle<Code> code_handle(re_code, isolate);
  Handle<String> subject_handle(String::cast(Object(*subject)), isolate);
  bool is_one_byte = String::IsOneByteRepresentationUnderneath(*subject_handle);
  int return_value = 0;

  if (js_has_overflowed) {
    AllowHeapAllocation yes_gc;
    isolate->StackOverflow();
    return_value = EXCEPTION;
  } else if (check.InterruptRequested()) {
    AllowHeapAllocation yes_gc;
    Object result = isolate->stack_guard()->HandleInterrupts();
    if (result.IsException(isolate)) return_value = EXCEPTION;
  }

  if (*code_handle != re_code) {  // Return address no longer valid
    // Overwrite the return address on the stack.
    intptr_t delta = code_handle->address() - re_code.address();
    Address new_pc = old_pc + delta;
    // TODO(v8:10026): avoid replacing a signed pointer.
    PointerAuthentication::ReplacePC(return_address, new_pc, 0);
  }

  // If we continue, we need to update the subject string addresses.
  if (return_value == 0) {
    // String encoding might have changed.
    if (String::IsOneByteRepresentationUnderneath(*subject_handle) !=
        is_one_byte) {
      // If we changed between an LATIN1 and an UC16 string, the specialized
      // code cannot be used, and we need to restart regexp matching from
      // scratch (including, potentially, compiling a new version of the code).
      return_value = RETRY;
    } else {
      *subject = subject_handle->ptr();
      intptr_t byte_length = *input_end - *input_start;
      *input_start =
          StringCharacterPosition(*subject_handle, start_index, no_gc);
      *input_end = *input_start + byte_length;
    }
  }
  return return_value;
}

// Returns a {Result} sentinel, or the number of successful matches.
int NativeRegExpMacroAssembler::Match(Handle<JSRegExp> regexp,
                                      Handle<String> subject,
                                      int* offsets_vector,
                                      int offsets_vector_length,
                                      int previous_index, Isolate* isolate) {
  DCHECK(subject->IsFlat());
  DCHECK_LE(0, previous_index);
  DCHECK_LE(previous_index, subject->length());

  // No allocations before calling the regexp, but we can't use
  // DisallowHeapAllocation, since regexps might be preempted, and another
  // thread might do allocation anyway.

  String subject_ptr = *subject;
  // Character offsets into string.
  int start_offset = previous_index;
  int char_length = subject_ptr.length() - start_offset;
  int slice_offset = 0;

  // The string has been flattened, so if it is a cons string it contains the
  // full string in the first part.
  if (StringShape(subject_ptr).IsCons()) {
    DCHECK_EQ(0, ConsString::cast(subject_ptr).second().length());
    subject_ptr = ConsString::cast(subject_ptr).first();
  } else if (StringShape(subject_ptr).IsSliced()) {
    SlicedString slice = SlicedString::cast(subject_ptr);
    subject_ptr = slice.parent();
    slice_offset = slice.offset();
  }
  if (StringShape(subject_ptr).IsThin()) {
    subject_ptr = ThinString::cast(subject_ptr).actual();
  }
  // Ensure that an underlying string has the same representation.
  bool is_one_byte = subject_ptr.IsOneByteRepresentation();
  DCHECK(subject_ptr.IsExternalString() || subject_ptr.IsSeqString());
  // String is now either Sequential or External
  int char_size_shift = is_one_byte ? 0 : 1;

  DisallowHeapAllocation no_gc;
  const byte* input_start =
      StringCharacterPosition(subject_ptr, start_offset + slice_offset, no_gc);
  int byte_length = char_length << char_size_shift;
  const byte* input_end = input_start + byte_length;
  return Execute(*subject, start_offset, input_start, input_end, offsets_vector,
                 offsets_vector_length, isolate, *regexp);
}

// Returns a {Result} sentinel, or the number of successful matches.
// TODO(pthier): The JSRegExp object is passed to native irregexp code to match
// the signature of the interpreter. We should get rid of JS objects passed to
// internal methods.
int NativeRegExpMacroAssembler::Execute(
    String input,  // This needs to be the unpacked (sliced, cons) string.
    int start_offset, const byte* input_start, const byte* input_end,
    int* output, int output_size, Isolate* isolate, JSRegExp regexp) {
  // Ensure that the minimum stack has been allocated.
  RegExpStackScope stack_scope(isolate);
  Address stack_base = stack_scope.stack()->stack_base();

  bool is_one_byte = String::IsOneByteRepresentationUnderneath(input);
  Code code = Code::cast(regexp.Code(is_one_byte));
  RegExp::CallOrigin call_origin = RegExp::CallOrigin::kFromRuntime;

  using RegexpMatcherSig = int(
      Address input_string, int start_offset,  // NOLINT(readability/casting)
      const byte* input_start, const byte* input_end, int* output,
      int output_size, Address stack_base, int call_origin, Isolate* isolate,
      Address regexp);

  auto fn = GeneratedCode<RegexpMatcherSig>::FromCode(code);
  int result =
      fn.Call(input.ptr(), start_offset, input_start, input_end, output,
              output_size, stack_base, call_origin, isolate, regexp.ptr());
  DCHECK(result >= RETRY);

  if (result == EXCEPTION && !isolate->has_pending_exception()) {
    // We detected a stack overflow (on the backtrack stack) in RegExp code,
    // but haven't created the exception yet. Additionally, we allow heap
    // allocation because even though it invalidates {input_start} and
    // {input_end}, we are about to return anyway.
    AllowHeapAllocation allow_allocation;
    isolate->StackOverflow();
  }
  return result;
}

Address NativeRegExpMacroAssembler::GrowStack(Address stack_pointer,
                                              Address* stack_base,
                                              Isolate* isolate) {
  RegExpStack* regexp_stack = isolate->regexp_stack();
  size_t size = regexp_stack->stack_capacity();
  Address old_stack_base = regexp_stack->stack_base();
  DCHECK(old_stack_base == *stack_base);
  DCHECK(stack_pointer <= old_stack_base);
  DCHECK(static_cast<size_t>(old_stack_base - stack_pointer) <= size);
  Address new_stack_base = regexp_stack->EnsureCapacity(size * 2);
  if (new_stack_base == kNullAddress) {
    return kNullAddress;
  }
  *stack_base = new_stack_base;
  intptr_t stack_content_size = old_stack_base - stack_pointer;
  return new_stack_base - stack_content_size;
}

}  // namespace internal
}  // namespace v8
