// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_WASM_CODE_POINTER_TABLE_INL_H_
#define V8_SANDBOX_WASM_CODE_POINTER_TABLE_INL_H_

#include "src/common/code-memory-access-inl.h"
#include "src/sandbox/external-entity-table-inl.h"
#include "src/sandbox/wasm-code-pointer-table.h"

#ifdef V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

void WasmCodePointerTableEntry::MakeCodePointerEntry(Address entrypoint) {
  entrypoint_.store(entrypoint, std::memory_order_relaxed);
}

Address WasmCodePointerTableEntry::GetEntrypoint() const {
  return entrypoint_.load(std::memory_order_relaxed);
}

void WasmCodePointerTableEntry::SetEntrypoint(Address value) {
  entrypoint_.store(value, std::memory_order_relaxed);
}

void WasmCodePointerTableEntry::MakeFreelistEntry(uint32_t next_entry_index) {
  Address value = next_entry_index;
  entrypoint_.store(value, std::memory_order_relaxed);
}

uint32_t WasmCodePointerTableEntry::GetNextFreelistEntryIndex() const {
  return static_cast<uint32_t>(entrypoint_.load(std::memory_order_relaxed));
}

Address WasmCodePointerTable::GetEntrypoint(
    WasmCodePointerTable::Handle handle) const {
  uint32_t index = HandleToIndex(handle);
  return at(index).GetEntrypoint();
}

void WasmCodePointerTable::SetEntrypoint(WasmCodePointerTable::Handle handle,
                                         Address value) {
  WriteScope write_scope("WasmCodePointerTable write");
  SetEntrypointUnlocked(handle, value);
}

void WasmCodePointerTable::SetEntrypointUnlocked(
    WasmCodePointerTable::Handle handle, Address value) {
  uint32_t index = HandleToIndex(handle);
  at(index).SetEntrypoint(value);
}

WasmCodePointerTable::Handle WasmCodePointerTable::AllocateAndInitializeEntry(
    Address entrypoint) {
  uint32_t index = AllocateEntry(&space_);
  WriteScope write_scope("WasmCodePointerTable write");
  at(index).MakeCodePointerEntry(entrypoint);
  return IndexToHandle(index);
}

WasmCodePointerTable::Handle
WasmCodePointerTable::AllocateUninitializedEntry() {
  uint32_t index = AllocateEntry(&space_);
  return IndexToHandle(index);
}

void WasmCodePointerTable::FreeEntryUnlocked(
    WasmCodePointerTable::Handle handle) {
  uint32_t index = HandleToIndex(handle);
  FreelistHead current_head, new_head;
  do {
    current_head = space_.freelist_head_.load();
    { at(index).MakeFreelistEntry(current_head.next()); }
    new_head = FreelistHead(index, current_head.length() + 1);
  } while (
      !space_.freelist_head_.compare_exchange_strong(current_head, new_head));
}

void WasmCodePointerTable::FreeEntry(WasmCodePointerTable::Handle handle) {
  uint32_t index = HandleToIndex(handle);
  FreelistHead current_head, new_head;
  do {
    current_head = space_.freelist_head_.load();
    {
      WriteScope write_scope("WasmCodePointerTable write");
      at(index).MakeFreelistEntry(current_head.next());
    }
    new_head = FreelistHead(index, current_head.length() + 1);
  } while (
      !space_.freelist_head_.compare_exchange_strong(current_head, new_head));
}

uint32_t WasmCodePointerTable::HandleToIndex(
    WasmCodePointerTable::Handle handle) const {
  // TODO(sroettger): we might want to shift the handle like in the JS code
  // pointer table.
  return handle;
}

WasmCodePointerTable::Handle WasmCodePointerTable::IndexToHandle(
    uint32_t index) const {
  return index;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_WEBASSEMBLY

#endif  // V8_SANDBOX_WASM_CODE_POINTER_TABLE_INL_H_
