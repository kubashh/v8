// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_WASM_CODE_POINTER_TABLE_H_
#define V8_SANDBOX_WASM_CODE_POINTER_TABLE_H_

#include "src/sandbox/external-entity-table.h"

#ifdef V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

struct WasmCodePointerTableEntry {
  // We write-protect the WasmCodePointerTable on platforms that support it for
  // forward-edge CFI.
  static constexpr bool IsWriteProtected = true;

  inline void MakeCodePointerEntry(Address entrypoint);

  // Make this entry a freelist entry, containing the index of the next entry
  // on the freelist.
  inline void MakeFreelistEntry(uint32_t next_entry_index);

  // Load code entrypoint pointer stored in this entry.
  // This entry must be a code pointer entry.
  inline Address GetEntrypoint() const;

  // Store the given code entrypoint pointer in this entry.
  // This entry must be a code pointer entry.
  inline void SetEntrypoint(Address value);

  // Get the index of the next entry on the freelist. This method may be
  // called even when the entry is not a freelist entry. However, the result
  // is only valid if this is a freelist entry. This behaviour is required
  // for efficient entry allocation, see TryAllocateEntryFromFreelist.
  inline uint32_t GetNextFreelistEntryIndex() const;

 private:
  friend class WasmCodePointerTable;

  // Freelist entries contain the index of the next free entry in their lower 32
  // bits and are tagged with the kFreeWasmCodePointerTableEntryTag.
  // static constexpr Address kFreeEntryTag = sizeof(Address) == 64 ?
  // kFreeCodePointerTableEntryTag : 0;

  std::atomic<Address> entrypoint_;
};

class V8_EXPORT_PRIVATE WasmCodePointerTable
    : public ExternalEntityTable<WasmCodePointerTableEntry,
                                 kCodePointerTableReservationSize> {
  using Base = ExternalEntityTable<WasmCodePointerTableEntry,
                                   kCodePointerTableReservationSize>;

 public:
  using Handle = uint32_t;
  using WriteScope = CFIMetadataWriteScope;
  static constexpr Handle kInvalidHandle = -1;

  WasmCodePointerTable() = default;
  WasmCodePointerTable(const WasmCodePointerTable&) = delete;
  WasmCodePointerTable& operator=(const WasmCodePointerTable&) = delete;

  // This method is atomic and can be called from background threads.
  inline Address GetEntrypoint(Handle handle) const;

  // Sets the entrypoint of the entry referenced by the given handle.
  // The Unlocked version can be used in loops, but you need to hold a
  // `WriteScope` while calling it.
  //
  // This method is atomic and can be called from background threads.
  inline void SetEntrypoint(Handle handle, Address value);
  inline void SetEntrypointUnlocked(Handle handle, Address value);

  // Allocates a new entry in the table and optionally initialize it.
  //
  // This method is atomic and can be called from background threads.
  inline Handle AllocateAndInitializeEntry(Address entrypoint);
  inline Handle AllocateUninitializedEntry();

  // Free an entry, which will add it  to the free list.
  // The Unlocked version can be used in loops, but you need to hold a
  // `WriteScope` while calling it.
  //
  // This method is atomic and can be called from background threads.
  inline void FreeEntry(Handle handle);
  inline void FreeEntryUnlocked(Handle handle);

  // The base address of this table, for use in JIT compilers.
  Address base_address() const { return base(); }

  void Initialize();

 private:
  class Space : public Base::Space {
    friend class WasmCodePointerTable;
  };

  inline uint32_t HandleToIndex(Handle handle) const;
  inline Handle IndexToHandle(uint32_t index) const;

  Space space_;
};

V8_EXPORT_PRIVATE WasmCodePointerTable* GetProcessWideWasmCodePointerTable();

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_WEBASSEMBLY

#endif  // V8_SANDBOX_WASM_CODE_POINTER_TABLE_H_
