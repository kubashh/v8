// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/assert-scope.h"

#include "src/base/bit-field.h"
#include "src/base/lazy-instance.h"
#include "src/base/platform/platform.h"
#include "src/execution/isolate.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

namespace {

template <PerThreadAssertType kType>
using PerThreadDataBit = base::BitField<bool, kType, 1>;

// Thread-local storage for assert data. Default all asserts to "allow".
thread_local uint32_t current_per_thread_assert_data(~0);

}  // namespace

template <PerThreadAssertType kType, bool kAllow>
PerThreadAssertScope<kType, kAllow>::PerThreadAssertScope()
    : old_data_(current_per_thread_assert_data) {
  current_per_thread_assert_data =
      PerThreadDataBit<kType>::update(old_data_.value(), kAllow);
}

template <PerThreadAssertType kType, bool kAllow>
PerThreadAssertScope<kType, kAllow>::~PerThreadAssertScope() {
  if (!old_data_.has_value()) return;
  Release();
}

template <PerThreadAssertType kType, bool kAllow>
void PerThreadAssertScope<kType, kAllow>::Release() {
  current_per_thread_assert_data = old_data_.value();
  old_data_.reset();
}

// static
template <PerThreadAssertType kType, bool kAllow>
bool PerThreadAssertScope<kType, kAllow>::IsAllowed() {
  return PerThreadDataBit<kType>::decode(current_per_thread_assert_data);
}

#define PER_ISOLATE_ASSERT_ENABLE_SCOPE_DEFINITION(EnableType, DisableType, \
                                                   field)                   \
  EnableType::EnableType(Isolate* isolate)                                  \
      : isolate_(isolate), old_data_(isolate->field()) {                    \
    DCHECK_NOT_NULL(isolate);                                               \
    isolate_->set_##field(true);                                            \
  }                                                                         \
                                                                            \
  EnableType::~EnableType() { isolate_->set_##field(old_data_); }           \
                                                                            \
  /* static */                                                              \
  bool EnableType::IsAllowed(Isolate* isolate) { return isolate->field(); } \
                                                                            \
  /* static */                                                              \
  void EnableType::Open(Isolate* isolate, bool* was_execution_allowed) {    \
    DCHECK_NOT_NULL(isolate);                                               \
    DCHECK_NOT_NULL(was_execution_allowed);                                 \
    *was_execution_allowed = isolate->field();                              \
    isolate->set_##field(true);                                             \
  }                                                                         \
  /* static */                                                              \
  void EnableType::Close(Isolate* isolate, bool was_execution_allowed) {    \
    DCHECK_NOT_NULL(isolate);                                               \
    isolate->set_##field(was_execution_allowed);                            \
  }

#define PER_ISOLATE_ASSERT_DISABLE_SCOPE_DEFINITION(EnableType, DisableType, \
                                                    field)                   \
  DisableType::DisableType(Isolate* isolate)                                 \
      : isolate_(isolate), old_data_(isolate->field()) {                     \
    DCHECK_NOT_NULL(isolate);                                                \
    isolate_->set_##field(false);                                            \
  }                                                                          \
                                                                             \
  DisableType::~DisableType() { isolate_->set_##field(old_data_); }          \
                                                                             \
  /* static */                                                               \
  bool DisableType::IsAllowed(Isolate* isolate) { return isolate->field(); } \
                                                                             \
  /* static */                                                               \
  void DisableType::Open(Isolate* isolate, bool* was_execution_allowed) {    \
    DCHECK_NOT_NULL(isolate);                                                \
    DCHECK_NOT_NULL(was_execution_allowed);                                  \
    *was_execution_allowed = isolate->field();                               \
    isolate->set_##field(false);                                             \
  }                                                                          \
  /* static */                                                               \
  void DisableType::Close(Isolate* isolate, bool was_execution_allowed) {    \
    DCHECK_NOT_NULL(isolate);                                                \
    isolate->set_##field(was_execution_allowed);                             \
  }

PER_ISOLATE_ASSERT_TYPE(PER_ISOLATE_ASSERT_ENABLE_SCOPE_DEFINITION)
PER_ISOLATE_ASSERT_TYPE(PER_ISOLATE_ASSERT_DISABLE_SCOPE_DEFINITION)

// -----------------------------------------------------------------------------
// Instantiations.

template class PerThreadAssertScope<HEAP_ALLOCATION_ASSERT, false>;
template class PerThreadAssertScope<HEAP_ALLOCATION_ASSERT, true>;
template class PerThreadAssertScope<SAFEPOINTS_ASSERT, false>;
template class PerThreadAssertScope<SAFEPOINTS_ASSERT, true>;
template class PerThreadAssertScope<HANDLE_ALLOCATION_ASSERT, false>;
template class PerThreadAssertScope<HANDLE_ALLOCATION_ASSERT, true>;
template class PerThreadAssertScope<HANDLE_DEREFERENCE_ASSERT, false>;
template class PerThreadAssertScope<HANDLE_DEREFERENCE_ASSERT, true>;
template class PerThreadAssertScope<CODE_DEPENDENCY_CHANGE_ASSERT, false>;
template class PerThreadAssertScope<CODE_DEPENDENCY_CHANGE_ASSERT, true>;
template class PerThreadAssertScope<CODE_ALLOCATION_ASSERT, false>;
template class PerThreadAssertScope<CODE_ALLOCATION_ASSERT, true>;
template class PerThreadAssertScope<GC_MOLE, false>;

}  // namespace internal
}  // namespace v8
