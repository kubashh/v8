// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ISOLATE_DATA_H_
#define V8_ISOLATE_DATA_H_

#include "src/builtins/builtins.h"
#include "src/constants-arch.h"
#include "src/external-reference-table.h"
#include "src/roots.h"
#include "src/utils.h"

namespace v8 {
namespace internal {

class Isolate;

// This class contains a collection of data accessible from both C++ runtime
// and compiled code (including assembly stubs, builtins, interpreter bytecode
// handlers and optimized code).
// In particular, it contains pointer to the V8 heap roots table, external
// reference table and builtins array.
// The compiled code accesses the isolate data fields indirectly via the root
// register.
class IsolateData final {
 public:
  IsolateData() = default;

  static constexpr size_t kRootsTableSize =
      RootsTable::kEntriesCount * kPointerSize;

  static constexpr size_t kBuiltinsTableSize =
      Builtins::builtin_count * kPointerSize;

  static constexpr intptr_t kBaseOffset = kRootRegisterBias;

  // The value of the root register.
  Address isolate_base_address() const {
    return reinterpret_cast<Address>(this) + kBaseOffset;
  }

  static constexpr int base_to_roots_table_offset() {
    return kRootsTableOffset - kBaseOffset;
  }

  static constexpr int base_to_root_slot_offset(RootIndex root_index) {
    return base_to_roots_table_offset() + RootsTable::offset_of(root_index);
  }

  static constexpr int base_to_external_reference_table_offset() {
    return kExternalReferenceTableOffset - kBaseOffset;
  }

  static constexpr int base_to_builtins_table_offset() {
    return kBuiltinsTableOffset - kBaseOffset;
  }

  // TODO(ishell): remove in favour of typified id version.
  static int base_to_builtin_slot_offset(int builtin_index) {
    DCHECK(Builtins::IsBuiltinId(builtin_index));
    return base_to_builtins_table_offset() + builtin_index * kPointerSize;
  }

  static int base_to_builtin_slot_offset(Builtins::Name id) {
    return base_to_builtins_table_offset() + id * kPointerSize;
  }

  static constexpr int base_to_magic_number_offset() {
    return kMagicNumberOffset - kBaseOffset;
  }

  static constexpr int base_to_virtual_call_target_register_offset() {
    return kVirtualCallTargetRegisterOffset - kBaseOffset;
  }

  // Returns true if this address points to data stored in this instance.
  // If it's the case then the value can be accessed indirectly through the
  // root register.
  bool contains(Address address) const {
    STATIC_ASSERT(std::is_unsigned<Address>::value);
    Address start = reinterpret_cast<Address>(this);
    return (address - start) < sizeof(*this);
  }

  RootsTable& roots() { return roots_; }
  const RootsTable& roots() const { return roots_; }

  ExternalReferenceTable* external_reference_table() {
    return &external_reference_table_;
  }

  Object** builtins() { return &builtins_[0]; }

  // For root register verification.
  // TODO(v8:6666): Remove once the root register is fully supported on ia32.
  static constexpr intptr_t kRootRegisterSentinel = 0xcafeca11;

 private:
// Static layout definition.
#define FIELDS(V)                                                         \
  V(kRootsTableOffset, kRootsTableSize)                                   \
  V(kExternalReferenceTableOffset, ExternalReferenceTable::SizeInBytes()) \
  V(kBuiltinsTableOffset, kBuiltinsTableSize)                             \
  V(kMagicNumberOffset, kIntptrSize)                                      \
  V(kVirtualCallTargetRegisterOffset, kPointerSize)                       \
  /* Total size. */                                                       \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(0, FIELDS)
#undef FIELDS

  RootsTable roots_;

  ExternalReferenceTable external_reference_table_;

  Object* builtins_[Builtins::builtin_count];

  // For root register verification.
  // TODO(v8:6666): Remove once the root register is fully supported on ia32.
  const intptr_t magic_number_ = kRootRegisterSentinel;

  // For isolate-independent calls on ia32.
  // TODO(v8:6666): Remove once wasm supports pc-relative jumps to builtins on
  // ia32 (otherwise the arguments adaptor call runs out of registers).
  void* virtual_call_target_register_ = nullptr;

  V8_INLINE static void AssertPredictableLayout();

  friend class Isolate;

  DISALLOW_COPY_AND_ASSIGN(IsolateData);
};

// IsolateData object must have "predictable" layout which does not change when
// cross-compiling to another platform. Otherwise there may be compatibility
// issues because of different compilers used for snapshot generator and
// actual V8 code.
void IsolateData::AssertPredictableLayout() {
  STATIC_ASSERT(offsetof(IsolateData, roots_) ==
                IsolateData::kRootsTableOffset);
  STATIC_ASSERT(offsetof(IsolateData, external_reference_table_) ==
                IsolateData::kExternalReferenceTableOffset);
  STATIC_ASSERT(offsetof(IsolateData, builtins_) ==
                IsolateData::kBuiltinsTableOffset);
  STATIC_ASSERT(sizeof(IsolateData) == IsolateData::kSize);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ISOLATE_DATA_H_
