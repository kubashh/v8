// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_STACK_FRAME_INFO_INL_H_
#define V8_OBJECTS_STACK_FRAME_INFO_INL_H_

#include "src/objects/stack-frame-info.h"

#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/frame-array-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

OBJECT_CONSTRUCTORS_IMPL(StackFrameInfo, Struct)

NEVER_READ_ONLY_SPACE_IMPL(StackFrameInfo)

CAST_ACCESSOR(StackFrameInfo)

SMI_ACCESSORS(StackFrameInfo, line_number, kLineNumberIndex)
SMI_ACCESSORS(StackFrameInfo, column_number, kColumnNumberIndex)
SMI_ACCESSORS(StackFrameInfo, script_id, kScriptIdIndex)
ACCESSORS(StackFrameInfo, script_name, Object, kScriptNameIndex)
ACCESSORS(StackFrameInfo, script_name_or_source_url, Object,
          kScriptNameOrSourceUrlIndex)
ACCESSORS(StackFrameInfo, function_name, Object, kFunctionNameIndex)
SMI_ACCESSORS(StackFrameInfo, flag, kFlagIndex)
BOOL_ACCESSORS(StackFrameInfo, flag, is_eval, kIsEvalBit)
BOOL_ACCESSORS(StackFrameInfo, flag, is_constructor, kIsConstructorBit)
BOOL_ACCESSORS(StackFrameInfo, flag, is_wasm, kIsWasmBit)
SMI_ACCESSORS(StackFrameInfo, id, kIdIndex)

OBJECT_CONSTRUCTORS_IMPL(StackTraceFrame, Struct)
NEVER_READ_ONLY_SPACE_IMPL(StackTraceFrame)
CAST_ACCESSOR(StackTraceFrame)

ACCESSORS(StackTraceFrame, frame_array, Object, kFrameArrayIndex)
SMI_ACCESSORS(StackTraceFrame, frame_index, kFrameIndexIndex)
ACCESSORS(StackTraceFrame, frame_info, Object, kFrameInfoIndex)
SMI_ACCESSORS(StackTraceFrame, id, kIdIndex)

int StackTraceFrame::GetLineNumber() {
  if (frame_info()->IsUndefined()) InitializeFrameInfo();
  int line = StackFrameInfo::cast(frame_info())->line_number();
  return line != -1 ? line : Message::kNoLineNumberInfo;
}

int StackTraceFrame::GetColumnNumber() {
  if (frame_info()->IsUndefined()) InitializeFrameInfo();
  int column = StackFrameInfo::cast(frame_info())->column_number();
  return column != -1 ? column : Message::kNoColumnInfo;
}

int StackTraceFrame::GetScriptId() {
  if (frame_info()->IsUndefined()) InitializeFrameInfo();
  int id = StackFrameInfo::cast(frame_info())->script_id();
  return id != -1 ? id : Message::kNoScriptIdInfo;
}

Handle<Object> StackTraceFrame::GetFileName() {
  if (frame_info()->IsUndefined()) InitializeFrameInfo();
  auto name = StackFrameInfo::cast(frame_info())->script_name();
  return handle(name, GetIsolate());
}

Handle<Object> StackTraceFrame::GetScriptNameOrSourceUrl() {
  if (frame_info()->IsUndefined()) InitializeFrameInfo();
  auto name = StackFrameInfo::cast(frame_info())->script_name_or_source_url();
  return handle(name, GetIsolate());
}

Handle<Object> StackTraceFrame::GetFunctionName() {
  if (frame_info()->IsUndefined()) InitializeFrameInfo();
  auto name = StackFrameInfo::cast(frame_info())->function_name();
  return handle(name, GetIsolate());
}

bool StackTraceFrame::IsEval() {
  if (frame_info()->IsUndefined()) InitializeFrameInfo();
  return StackFrameInfo::cast(frame_info())->is_eval();
}

bool StackTraceFrame::IsConstructor() {
  if (frame_info()->IsUndefined()) InitializeFrameInfo();
  return StackFrameInfo::cast(frame_info())->is_constructor();
}

bool StackTraceFrame::IsWasm() {
  if (frame_info()->IsUndefined()) InitializeFrameInfo();
  return StackFrameInfo::cast(frame_info())->is_wasm();
}

void StackTraceFrame::InitializeFrameInfo() {
  Isolate* isolate = GetIsolate();
  set_frame_info(*isolate->factory()->NewStackFrameInfo(
      Handle<FrameArray>(FrameArray::cast(frame_array()), isolate),
      frame_index()));

  // After initializing, we no longer need to keep a reference
  // to the frame_array.
  set_frame_array(*isolate->factory()->undefined_value());
  set_frame_index(-1);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_STACK_FRAME_INFO_INL_H_
