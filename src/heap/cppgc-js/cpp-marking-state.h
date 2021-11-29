// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_JS_CPP_MARKING_STATE_H_
#define V8_HEAP_CPPGC_JS_CPP_MARKING_STATE_H_

#include "src/heap/cppgc-js/cpp-heap.h"
#include "src/heap/cppgc/marking-state.h"
#include "src/heap/cppgc/marking-worklists.h"
#include "src/heap/embedder-tracing.h"
#include "src/objects/js-objects.h"

namespace v8 {

namespace internal {

class CppMarkingState {
 public:
  CppMarkingState(CppHeap& cpp_heap,
                  const WrapperDescriptor& wrapper_descriptor,
                  cppgc::internal::MarkingWorklists& marking_worklists)
      : isolate_(reinterpret_cast<Isolate*>(cpp_heap.isolate())),
        wrapper_descriptor_(wrapper_descriptor),
        marking_state_(cpp_heap.AsBase(), marking_worklists) {}

  CppMarkingState(const CppMarkingState&) = delete;
  CppMarkingState& operator=(const CppMarkingState&) = delete;

  void Publish() { marking_state_.Publish(); }

  void MarkAndPush(const JSObject& js_object) {
    DCHECK(CheckJSObject(js_object));
    LocalEmbedderHeapTracer::WrapperInfo info;
    if (LocalEmbedderHeapTracer::ExtractWrappableInfo(
            isolate_, js_object, wrapper_descriptor_, &info)) {
      marking_state_.MarkAndPush(
          cppgc::internal::HeapObjectHeader::FromObject(info.second));
    }
  }

  bool IsLocalEmpty() {
    return marking_state_.marking_worklist().IsLocalEmpty();
  }

 private:
  V8_EXPORT_PRIVATE bool CheckJSObject(const JSObject&);

  Isolate* const isolate_;
  const WrapperDescriptor& wrapper_descriptor_;

  cppgc::internal::MarkingStateBase marking_state_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CPPGC_JS_CPP_MARKING_STATE_H_
