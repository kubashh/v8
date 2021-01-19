// FIXME: licensing info for V8 and abseil

// Dummy temporary copyright header to make presubmit check happy:
// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
A property backing store based on Swiss Tables/Abseil's flat_hash_map.

Memory layout:
  Prefix:             4 bytes, raw uint32_t.
  Capacity:           4 bytes, raw int32_t.
  Meta table pointer: kTaggedSize bytes.
    See below for explanation of the meta table.
    For capacity 0, this contains the Smi |kNoMetaTableSentinel| instead.
  Data table:         2 * |capacity| * |kTaggedSize| bytes.
    For each logical bucket of the hash table, contains the corresponding key
    and value.
  Ctrl table:         |capacity| + |kGroupWidth| uint8_t entries.
    The control table is used to implement a Swiss Table: Each byte is either
    Ctrl::kEmpty, Ctrl::kEmpty, or in case of a bucket denoting a present
    entry in the hash table, the 7 lowest bits of the key's hash. The first
    |capacity| entries are the actual control table. The additional
    |kGroupWidth| bytes contain a copy of the first min(capacity, kGroupWidth)
    bytes of the table.

Note that because of |kInitialCapacity| ==  4 there is no need for padding.

Meta table:
  The meta table (not to be confused with the control table used in any
  Swiss Table design!) is a separate ByteArray. Here, the "X" in "uintX_t"
  depends on the capacity of the swiss table. For capacities <= 256 we have X =
  8, for 256 < |capacity| <= 2^16 we have X = 16, and otherwise X = 32 (see
  MetaTableSizePerEntryFor). It contais the following data:
    Number of Entries: uintX_t.
    Number of Deleted Entries: uintX_t.
    Enumeration table: max_load_factor * capacity entries of type uintX_t:
      The i-th entry in the enumeration table
      contains the number of the bucket representing the i-th entry of the table
      in enumeration order.
*/

// Fixme: document main differences to Abseil:
// - capacity is not a power of two minus one, but a power of two directly
// - there is no sentinel at the end

#ifndef V8_OBJECTS_SWISS_HASH_TABLE_H_
#define V8_OBJECTS_SWISS_HASH_TABLE_H_

#include "src/base/export-template.h"
#include "src/common/globals.h"
#include "src/objects/fixed-array.h"
#include "src/objects/internal-index.h"
#include "src/objects/js-objects.h"
#include "src/roots/roots.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "src/objects/swiss-hash-table-helpers.h"

#define V8_SWISS_TABLES_USE_SSE_IMPL ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSE2
// #define V8_SWISS_TABLES_USE_SSE_IMPL 0 // For debugging.

#if V8_SWISS_TABLES_USE_SSE_IMPL
using Group = GroupSse2Impl;
#else
using Group = GroupPortableImpl;
#endif

class SwissNameDictionary : public HeapObject {
 public:
  template <typename LocalIsolate>
  inline static Handle<SwissNameDictionary> Add(
      LocalIsolate* isolate, Handle<SwissNameDictionary> table,
      Handle<Name> key, Handle<Object> value, PropertyDetails details,
      InternalIndex* index_out = nullptr);

  static Handle<SwissNameDictionary> Shrink(Isolate* isolate,
                                            Handle<SwissNameDictionary> table);

  static Handle<SwissNameDictionary> DeleteEntry(
      Isolate* isolate, Handle<SwissNameDictionary> table, InternalIndex entry);

  template <typename LocalIsolate>
  inline InternalIndex FindEntry(LocalIsolate* isolate, Object key);

  // This is to make the interfaces of NameDictionary::FindEntry and
  // OrderedNameDictionary::FindEntry compatible.
  // TODO(emrich) clean this up: NameDictionary uses Handle<Object>
  // for FindEntry keys due to its Key typedef, but that's also used
  // for adding, where we do need handles.
  template <typename LocalIsolate>
  inline InternalIndex FindEntry(LocalIsolate* isolate, Handle<Object> key);

  static inline bool IsKey(ReadOnlyRoots roots, Object key_candidate);
  inline bool ToKey(ReadOnlyRoots roots, InternalIndex index, Object* out_key);

  inline Object KeyAt(InternalIndex index);
  inline Name NameAt(InternalIndex index);
  inline Object ValueAt(InternalIndex index);
  inline PropertyDetails DetailsAt(InternalIndex index);

  inline void ValueAtPut(InternalIndex entry, Object value);
  inline void DetailsAtPut(InternalIndex entry, PropertyDetails value);

  inline int NumberOfElements();
  inline int NumberOfDeletedElements();

  inline int Capacity();
  inline int UsedCapacity();

  // "Strict" in the sense that it guarantees that all used/initialized memory
  // in the old table is copied as-is to the new table. In particular, no kind
  // of tidying up is performed.
  bool DebugEquals(SwissNameDictionary other);

  // Copy operation for debugging purposes. Guarantees that DebugEquals equals
  // holds for the old table and its copy.
  static Handle<SwissNameDictionary> DebugShallowCopy(
      Isolate* isolate, Handle<SwissNameDictionary> table);

  template <typename LocalIsolate>
  void Initialize(LocalIsolate* isolate, ByteArray meta_table, int capacity);

  template <typename LocalIsolate>
  static Handle<SwissNameDictionary> Rehash(LocalIsolate* isolate,
                                            Handle<SwissNameDictionary> table,
                                            int new_capacity);

  void RehashInplace(Isolate* isolate);

  inline void SetHash(int hash);
  inline int Hash();

  class index_iterator {
   public:
    inline index_iterator(Handle<SwissNameDictionary>& dict, int start);

    inline index_iterator& operator++();

    inline bool operator==(const index_iterator& b) const;

    inline bool operator!=(const index_iterator& b) const;

    inline InternalIndex operator*();

   private:
    inline int used_capacity() const;

    int enum_index;

    // This may be nullptr, in which case the capacity of the table is 0.
    Handle<SwissNameDictionary> const& dict;
  };

  class index_iterable {
   public:
    inline explicit index_iterable(Handle<SwissNameDictionary> dict);

    inline index_iterator begin();
    inline index_iterator end();

   private:
    // This may be nullptr, in which case the capacity of the table must be  0.
    Handle<SwissNameDictionary> dict;
  };

  inline index_iterable IterateEntriesOrdered();
  inline index_iterable IterateEntries();

  inline int BucketForEnumerationIndex(int enumeration_index);

  static uint32_t H1(uint32_t hash) { return (hash >> kH2Bits); }
  static ctrl_t H2(uint32_t hash) { return hash & ((1 << kH2Bits) - 1); }

  inline static bool IsValidCapacity(int capacity);
  inline static int CapacityFor(int at_least_space_for);

  // Given a capacity, how much of it can we fill before resizing?
  inline static int MaxUsableCapacity(int capacity);

  // The maximum allowed capacity for any SwissNameDictionary.
  inline static constexpr int MaxCapacity();

  // Returns total size in bytes required for a table of given
  // capacity.
  inline static constexpr int SizeFor(int capacity);

  inline static constexpr int MetaTableSizePerEntryFor(int capacity);
  inline static constexpr int MetaTableSizeFor(int capacity);

  // Indicates that IterateEntries() returns entries ordered.
  static constexpr bool kIsOrderedDictionaryType = true;

  // Only used in CSA/Torque, where indices are actual integers. In C++,
  // InternalIndex::NotFound() is always used instead.
  static constexpr int kNotFoundSentinel = -1;

  static const int kGroupWidth = Group::kWidth;

  class BodyDescriptor;

  // Just for documentation purposes, the implementation relies on this being 7.
  static constexpr int kH2Bits = 7;

  // Note that 0 is also a valid capacity. Changing this value to a smaller one
  // may make some padding necessary in the data layout.
  static constexpr int kInitialCapacity = 4;

  // How many kTaggedSize sized entries are associcated which each entry in the
  // data table?
  static constexpr int kDataTableEntryCount = 2;
  static constexpr int kDataTableKeyEntryIndex = 0;
  static constexpr int kDataTableValueEntryIndex = kDataTableKeyEntryIndex + 1;

  static constexpr int kNoMetaTableSentinel = -1;

  static constexpr int kMetaTableElementCountOffset = 0;
  static constexpr int kMetaTableDeletedElementCountOffset = 1;
  static constexpr int kMetaTableEnumerationTableStartOffset = 2;

  // Offset into the overall table, starting after HeapObject standard fields,
  // in bytes.
  using Offset = int;
  inline static constexpr Offset PrefixOffset();
  inline static constexpr Offset CapacityOffset();
  inline static constexpr Offset MetaTablePointerOffset();
  inline static constexpr Offset DataTableStartOffset();
  inline static constexpr Offset DataTableEndOffset(int capacity);
  inline static constexpr Offset CtrlTableStartOffset(int capacity);
  inline static constexpr Offset PropertyDetailsTableStartOffset(int capacity);

#if VERIFY_HEAP
  void SwissNameDictionaryVerify(Isolate* isolate, bool slow_checks);
  DECL_VERIFIER(SwissNameDictionary)
#endif

  DECL_PRINTER(SwissNameDictionary)
  DECL_CAST(SwissNameDictionary)
  OBJECT_CONSTRUCTORS(SwissNameDictionary, HeapObject);

 private:
  template <typename LocalIsolate>
  inline static Handle<SwissNameDictionary> EnsureGrowable(
      LocalIsolate* isolate, Handle<SwissNameDictionary> table);

  // Returns table of byte-encoded PropertyDetails (without enumeration index
  // stored)
  inline uint8_t* PropertyDetailsTable();

  inline void SetKey(int index, Object key);
  inline void SetValue(int index, Object value);

  inline void DetailsAtPut(int entry, PropertyDetails value);
  inline void ValueAtPut(int entry, Object value);

  inline PropertyDetails DetailsAtRaw(int index);
  inline Object ValueAtRaw(int index);
  inline Object KeyAt(int index);

  inline bool ToKey(ReadOnlyRoots roots, int index, Object* out_key);

  inline int FindFirstEmpty(uint32_t hash);

  // Not intended for modification, use set_ctrl instead to get correct copying
  // of first group.
  inline const ctrl_t* CtrlTable();

  inline static bool IsEmpty(ctrl_t c);
  inline static bool IsFull(ctrl_t c);
  inline static bool IsDeleted(ctrl_t c);
  inline static bool IsEmptyOrDeleted(ctrl_t c);

  // Sets the a control byte, taking the necessary copying of the first group
  // into account.
  inline void SetCtrl(int index, ctrl_t h);
  inline ctrl_t GetCtrl(int index);

  inline Object LoadFromDataTable(int index, int data_offset);
  inline void StoreToDataTable(int index, int data_offset, Object data);

  inline void SetCapacity(int capacity);
  inline void SetNumberOfElements(int elements);
  inline void SetNumberOfDeletedElements(int deleted_elements);

  template <typename LocalIsolate>
  static inline bool CanBePresentKey(LocalIsolate* isolate, Object key,
                                     uint32_t* hash_out);

  static inline probe_seq<Group::kWidth> probe(uint32_t hash, int capacity);

  inline void SetEnumerationTableMapping(int enumeration_index,
                                         int bucket_index);

  // |meta_table| must be either a ByteArray or the Smi kNoMetaTableSentinel.
  inline void SetMetaTable(Object meta_table);
  inline ByteArray GetMetaTable();

  inline void SetMetaTableField(int field_index, int value);
  inline int GetMetaTableField(int field_index);

  template <typename T>
  inline static void SetMetaTableField(ByteArray meta_table, int field_index,
                                       int value);
  template <typename T>
  inline static int GetMetaTableField(ByteArray meta_table, int field_index);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_SWISS_HASH_TABLE_H_
