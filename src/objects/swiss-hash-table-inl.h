// Dummy temporary copyright header to make presubmit check happy:
// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SWISS_HASH_TABLE_INL_H_
#define V8_OBJECTS_SWISS_HASH_TABLE_INL_H_

#include "src/objects/swiss-hash-table.h"
// ---
#include "src/base/macros.h"
#include "src/execution/isolate-utils-inl.h"
#include "src/heap/heap.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/js-collection-iterator.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/swiss-hash-table-tq-inl.inc"

CAST_ACCESSOR(SwissNameDictionary)
OBJECT_CONSTRUCTORS_IMPL(SwissNameDictionary, HeapObject)

const ctrl_t* SwissNameDictionary::CtrlTable() {
  return reinterpret_cast<ctrl_t*>(
      field_address(CtrlTableStartOffset(Capacity())));
}

uint8_t* SwissNameDictionary::PropertyDetailsTable() {
  return reinterpret_cast<uint8_t*>(
      field_address(PropertyDetailsTableStartOffset(Capacity())));
}

int SwissNameDictionary::Capacity() {
  return ReadField<int32_t>(CapacityOffset());
}

void SwissNameDictionary::SetCapacity(int capacity) {
  DCHECK(IsValidCapacity(capacity));

  WriteField(CapacityOffset(), capacity);
}

int SwissNameDictionary::NumberOfElements() {
  if (Capacity() > 0)
    return GetMetaTableField(kMetaTableElementCountOffset);
  else
    return 0;
}

int SwissNameDictionary::NumberOfDeletedElements() {
  if (Capacity() > 0)
    return GetMetaTableField(kMetaTableDeletedElementCountOffset);
  else
    return 0;
}

void SwissNameDictionary::SetNumberOfElements(int elements) {
  SetMetaTableField(kMetaTableElementCountOffset, elements);
}

void SwissNameDictionary::SetNumberOfDeletedElements(int deleted_elements) {
  SetMetaTableField(kMetaTableDeletedElementCountOffset, deleted_elements);
}

int SwissNameDictionary::UsedCapacity() {
  return NumberOfElements() + NumberOfDeletedElements();
}

// static
bool SwissNameDictionary::IsValidCapacity(int capacity) {
  return capacity == 0 || (capacity >= kInitialCapacity &&
                           // Must be power of 2.
                           ((capacity & (capacity - 1)) == 0));
}

// static
constexpr int SwissNameDictionary::SizeFor(int capacity) {
  DCHECK(IsValidCapacity(capacity));

  return (PropertyDetailsTableStartOffset(capacity) + capacity);
}

// We use 7/8th as maximum load factor.
// For 16-wide groups, that gives an average of two empty slots per group.
// Similar to Abseils CapacityToGrowth.
// static
int SwissNameDictionary::MaxUsableCapacity(int capacity) {
  DCHECK(IsValidCapacity(capacity));

  if (Group::kWidth == 8 && capacity == 4) {
    // If the group size is 16 we can fully utilize capacity 4: There will be
    // enough kEmpty entries in the ctrl table.
    return 3;
  }
  return capacity - capacity / 8;
}

// Returns |at_least_space_for| * 8/7 for non-special cases. Similar to Abseil's
// GrowthToLowerboundCapacity.
// static
int SwissNameDictionary::CapacityFor(int at_least_space_for) {
  if (at_least_space_for == 0) {
    return 0;
  } else if (at_least_space_for < 4) {
    return 4;
  } else if (at_least_space_for == 4 && kGroupWidth == 16) {
    return 4;
  } else if (at_least_space_for == 4 && kGroupWidth == 8) {
    return 8;
  }

  int non_normalized = at_least_space_for + at_least_space_for / 7;
  return base::bits::RoundUpToPowerOfTwo32(non_normalized);
}

int SwissNameDictionary::BucketForEnumerationIndex(int enumeration_index) {
  DCHECK_LT(enumeration_index, UsedCapacity());
  return GetMetaTableField(kMetaTableEnumerationTableStartOffset +
                           enumeration_index);
}

void SwissNameDictionary::SetEnumerationTableMapping(int enumeration_index,
                                                     int bucket_index) {
  DCHECK_LT(enumeration_index, UsedCapacity());
  DCHECK_LT(bucket_index, Capacity());
  DCHECK(IsFull(GetCtrl(bucket_index)));

  SetMetaTableField(kMetaTableEnumerationTableStartOffset + enumeration_index,
                    bucket_index);
}

template <typename LocalIsolate>
InternalIndex SwissNameDictionary::FindEntry(LocalIsolate* isolate,
                                             Object key) {
  uint32_t hash;
  if (!CanBePresentKey(isolate, key, &hash)) return InternalIndex::NotFound();

  const ctrl_t* ctrl = CtrlTable();
  auto seq = probe(hash, Capacity());
  while (true) {
    Group g{ctrl + seq.offset()};
    for (int i : g.Match(H2(hash))) {
      int candidate_index = seq.offset(i);
      Object candidate_key = KeyAt(candidate_index);
      // FIXME: This is name dictionary specific!
      if (candidate_key == key) return InternalIndex(candidate_index);
    }
    if (g.MatchEmpty()) return InternalIndex::NotFound();

    seq.next();

    // FIXME: comment
    DCHECK_LT(seq.index(), Capacity());
  }
}

template <typename LocalIsolate>
InternalIndex SwissNameDictionary::FindEntry(LocalIsolate* isolate,
                                             Handle<Object> key) {
  return FindEntry(isolate, *key);
}

// static
template <typename LocalIsolate>
inline bool SwissNameDictionary::CanBePresentKey(LocalIsolate* isolate,
                                                 Object key,
                                                 uint32_t* hash_out) {
  // This special cases for Smi, so that we avoid the HandleScope
  // creation below.
  if (key.IsSmi()) {
    uint32_t hash = ComputeUnseededHash(Smi::ToInt(key));
    *hash_out = hash & Smi::kMaxValue;
    return true;
  } else {
    // FIXME: The creation of the HandleScope is taken from
    // OrderedNameDictionary. What is it good for, I don't see any handles being
    // created? Remove for now because HandleScope doesn't take a LocalIsolate
    // HandleScope scope(isolate);
    Object hash = key.GetHash();
    // If the object does not have an identity hash, it was never used as a key
    if (hash.IsUndefined(isolate)) return false;
    *hash_out = static_cast<uint32_t>(Smi::ToInt(hash));
    return true;
  }
}

Object SwissNameDictionary::LoadFromDataTable(int index, int data_offset) {
  IsolateRoot isolate = GetIsolateForPtrCompr(*this);
  int overall_offset =
      DataTableStartOffset() +
      (index * kDataTableEntryCount + data_offset) * kTaggedSize;
  // Fixme: Right loading function?
  return TaggedField<Object>::load(isolate, *this, overall_offset);
}

void SwissNameDictionary::StoreToDataTable(int index, int data_offset,
                                           Object data) {
  DCHECK_LT(index, Capacity());

  int offset = DataTableStartOffset() +
               (index * kDataTableEntryCount + data_offset) * kTaggedSize;

  // FIXME: taken from FixedArray::set, right storing function?
  // TaggedField<Object>::store doesn't seem to set the write barrier
  RELAXED_WRITE_FIELD(*this, offset, data);
  WRITE_BARRIER(*this, offset, data);
}

void SwissNameDictionary::ValueAtPut(int index, Object value) {
  DCHECK(!value.IsTheHole());
  SetValue(index, value);
}

void SwissNameDictionary::ValueAtPut(InternalIndex index, Object value) {
  SetValue(index.as_int(), value);
}

void SwissNameDictionary::SetValue(int index, Object value) {
  // FIXME: this is also used for deleting, thus may be the hole value
  StoreToDataTable(index, kDataTableValueEntryIndex, value);
}

void SwissNameDictionary::SetKey(int index, Object key) {
  // The ctrl table must have been updated already.
  // FIXME: this is also used for deleting, thus may be the hole value
  // DCHECK(CtrlTable()[index] == H2(key.hash()));
  StoreToDataTable(index, kDataTableKeyEntryIndex, key);
}

void SwissNameDictionary::DetailsAtPut(int index, PropertyDetails details) {
  uint8_t encoded_details = details.ToByte();
  PropertyDetailsTable()[index] = encoded_details;
}

void SwissNameDictionary::DetailsAtPut(InternalIndex index,
                                       PropertyDetails details) {
  DetailsAtPut(index.as_int(), details);
}

Object SwissNameDictionary::KeyAt(int index) {
  return LoadFromDataTable(index, kDataTableKeyEntryIndex);
}

Object SwissNameDictionary::KeyAt(InternalIndex index) {
  return KeyAt(index.as_int());
}

Name SwissNameDictionary::NameAt(InternalIndex index) {
  return Name::cast(KeyAt(index));
}

// This version does allow being called on empty buckets
Object SwissNameDictionary::ValueAtRaw(int index) {
  return LoadFromDataTable(index, kDataTableValueEntryIndex);
}

Object SwissNameDictionary::ValueAt(InternalIndex index) {
  DCHECK(IsFull(GetCtrl(index.as_int())));
  return ValueAtRaw(index.as_int());
}

PropertyDetails SwissNameDictionary::DetailsAtRaw(int index) {
  uint8_t encoded_details = PropertyDetailsTable()[index];
  return PropertyDetails::FromByte(encoded_details);
}

PropertyDetails SwissNameDictionary::DetailsAt(InternalIndex index) {
  DCHECK(IsFull(GetCtrl(index.as_int())));
  return DetailsAtRaw(index.as_int());
}

// static
template <typename LocalIsolate>
Handle<SwissNameDictionary> SwissNameDictionary::EnsureGrowable(
    LocalIsolate* isolate, Handle<SwissNameDictionary> table) {
  int capacity = table->Capacity();
  int max_usable = MaxUsableCapacity(capacity);

  if (table->UsedCapacity() + 1 <= max_usable) {
    return table;
  }

  int new_capacity = capacity == 0 ? kInitialCapacity : capacity * 2;
  return Rehash(isolate, table, new_capacity);
}

ctrl_t SwissNameDictionary::GetCtrl(int index) {
  DCHECK_LT(index, Capacity());
  return CtrlTable()[index];
}

void SwissNameDictionary::SetCtrl(int index, ctrl_t h) {
  int capacity = Capacity();
  DCHECK_LT(index, capacity);

  ctrl_t* ctrl =
      reinterpret_cast<ctrl_t*>(field_address(CtrlTableStartOffset(capacity)));

  ctrl[index] = h;

  // We mirror at the group starting at bucket 0 at the end of the ctrl table,
  // using some bit magic to avoid a branch.
  int mask = capacity - 1;
  int copy_index =
      ((index - Group::kWidth) & mask) + 1 + ((Group::kWidth - 1) & mask);
  DCHECK_IMPLIES(index < static_cast<int>(Group::kWidth),
                 copy_index == capacity + index);
  DCHECK_IMPLIES(index >= static_cast<int>(Group::kWidth), copy_index == index);
  ctrl[copy_index] = h;
}

// static
inline int SwissNameDictionary::FindFirstEmpty(uint32_t hash) {
  auto seq = probe(hash, Capacity());
  while (true) {
    Group g{CtrlTable() + seq.offset()};
    auto mask = g.MatchEmpty();
    if (mask) {
      return seq.offset(mask.LowestBitSet());
    }
    seq.next();
    // FIXME: Add Same comment as in find.
    DCHECK_LT(seq.index(), Capacity());
  }
}

ByteArray SwissNameDictionary::GetMetaTable() {
  DCHECK_NE(Capacity(), 0);

  // fixme: double check getting isolate;
  IsolateRoot isolate = GetIsolateForPtrCompr(*this);
  // Fixme: Right function to load?
  Object obj = TaggedField<Object>::Relaxed_Load(isolate, *this,
                                                 MetaTablePointerOffset());
  return ByteArray::cast(obj);
}

void SwissNameDictionary::SetMetaTable(Object meta_table) {
  DCHECK(
      (meta_table.IsSmi() && Smi::ToInt(meta_table) == kNoMetaTableSentinel) ||
      meta_table.IsByteArray());

  // FIXME: right kind of store?
  TaggedField<Object>::store(*this, MetaTablePointerOffset(), meta_table);
}

void SwissNameDictionary::SetMetaTableField(int field_index, int value) {
  // If Capacity() == 0, we don't allocate a meta table.
  DCHECK_GT(Capacity(), 0);

  unsigned int max_value = Capacity() - 1;
  ByteArray meta_table = GetMetaTable();
  if (max_value <= std::numeric_limits<uint8_t>::max()) {
    SetMetaTableField<uint8_t>(meta_table, field_index, value);
  } else if (max_value <= std::numeric_limits<uint16_t>::max()) {
    SetMetaTableField<uint16_t>(meta_table, field_index, value);
  } else {
    SetMetaTableField<uint32_t>(meta_table, field_index, value);
  }
}

int SwissNameDictionary::GetMetaTableField(int field_index) {
  // If Capacity() == 0, we don't allocate a meta table.
  DCHECK_GT(Capacity(), 0);

  unsigned int max_value = Capacity() - 1;
  ByteArray meta_table = GetMetaTable();
  if (max_value <= std::numeric_limits<uint8_t>::max()) {
    return GetMetaTableField<uint8_t>(meta_table, field_index);
  } else if (max_value <= std::numeric_limits<uint16_t>::max()) {
    return GetMetaTableField<uint16_t>(meta_table, field_index);
  } else {
    return GetMetaTableField<uint32_t>(meta_table, field_index);
  }
}

// static
template <typename T>
void SwissNameDictionary::SetMetaTableField(ByteArray meta_table,
                                            int field_index, int value) {
  STATIC_ASSERT((std::is_same<T, uint8_t>::value) ||
                (std::is_same<T, uint16_t>::value) ||
                (std::is_same<T, uint32_t>::value));
  DCHECK_LE(value, std::numeric_limits<T>::max());
  DCHECK_LT(meta_table.GetDataStartAddress() + field_index * sizeof(T),
            meta_table.GetDataEndAddress());
  T* raw_data = reinterpret_cast<T*>(meta_table.GetDataStartAddress());
  raw_data[field_index] = value;
}

// static
template <typename T>
int SwissNameDictionary::GetMetaTableField(ByteArray meta_table,
                                           int field_index) {
  STATIC_ASSERT((std::is_same<T, uint8_t>::value) ||
                (std::is_same<T, uint16_t>::value) ||
                (std::is_same<T, uint32_t>::value));
  DCHECK_LT(meta_table.GetDataStartAddress() + field_index * sizeof(T),
            meta_table.GetDataEndAddress());
  T* raw_data = reinterpret_cast<T*>(meta_table.GetDataStartAddress());
  return raw_data[field_index];
}

constexpr int SwissNameDictionary::MetaTableSizePerEntryFor(int capacity) {
  DCHECK_NE(capacity, 0);
  DCHECK(IsValidCapacity(capacity));

  int max_value = capacity - 1;
  if (max_value <= std::numeric_limits<uint8_t>::max()) {
    return sizeof(uint8_t);
  } else if (max_value <= std::numeric_limits<uint16_t>::max()) {
    return sizeof(uint16_t);
  } else {
    return sizeof(uint32_t);
  }
}

constexpr int SwissNameDictionary::MetaTableSizeFor(int capacity) {
  DCHECK_NE(capacity, 0);
  DCHECK(IsValidCapacity(capacity));

  int per_entry_size = MetaTableSizePerEntryFor(capacity);

  // The enumeration table only needs to have as many slots as there can be
  // present + deleted entries in the hash table (= maximum load factor *
  // capactiy). Two more slots to store the number of present and deleted
  // entries.
  return per_entry_size * (MaxUsableCapacity(capacity) + 2);
}

bool SwissNameDictionary::IsKey(ReadOnlyRoots roots, Object key_candidate) {
  return key_candidate != roots.the_hole_value();
}

bool SwissNameDictionary::ToKey(ReadOnlyRoots roots, int index,
                                Object* out_key) {
  Object k = KeyAt(index);
  if (!IsKey(roots, k)) return false;
  *out_key = k;
  return true;
}

bool SwissNameDictionary::ToKey(ReadOnlyRoots roots, InternalIndex index,
                                Object* out_key) {
  return ToKey(roots, index.as_int(), out_key);
}

// static
template <typename LocalIsolate>
Handle<SwissNameDictionary> SwissNameDictionary::Add(
    LocalIsolate* isolate, Handle<SwissNameDictionary> original_table,
    Handle<Name> key, Handle<Object> value, PropertyDetails details,
    InternalIndex* index_out) {
  DCHECK(key->IsUniqueName());
  DCHECK(original_table->FindEntry(isolate, *key).is_not_found());
  DCHECK(!value->IsTheHole());

  Handle<SwissNameDictionary> table = EnsureGrowable(isolate, original_table);

  uint32_t hash = key->hash();

  // For now we don't re-use deleted buckets (due to enumeration table
  // complications).
  int target = table->FindFirstEmpty(hash);

  table->SetCtrl(target, H2(hash));
  table->SetKey(target, *key);
  table->ValueAtPut(target, *value);
  table->DetailsAtPut(target, details);

  int nof = table->NumberOfElements();
  int nod = table->NumberOfDeletedElements();
  int new_enum_index = nof + nod;
  table->SetNumberOfElements(nof + 1);
  table->SetEnumerationTableMapping(new_enum_index, target);

  if (index_out) {
    *index_out = InternalIndex(target);
  }

  return table;
}

template <typename LocalIsolate>
void SwissNameDictionary::Initialize(LocalIsolate* isolate,
                                     ByteArray meta_table, int capacity) {
  DCHECK(IsValidCapacity(capacity));
  DisallowHeapAllocation no_gc;
  ReadOnlyRoots roots(isolate);

  SetCapacity(capacity);
  SetHash(PropertyArray::kNoHashSentinel);

  ctrl_t* ctrl_table = const_cast<ctrl_t*>(CtrlTable());
  memset(ctrl_table, Ctrl::kEmpty, capacity + Group::kWidth);

  MemsetTagged(RawField(DataTableStartOffset()), roots.the_hole_value(),
               capacity * kDataTableEntryCount);

  if (capacity == 0) {
    // This branch is only supposed to be used to create the canonical empty
    // version  and should not be used afterwards.
    DCHECK_EQ(kNullAddress, ReadOnlyRoots(isolate).at(
                                RootIndex::kEmptySwissPropertyDictionary));

    DCHECK(meta_table.is_null());
    SetMetaTable(Smi::FromInt(kNoMetaTableSentinel));
  } else {
    SetMetaTable(meta_table);

    SetNumberOfElements(0);
    SetNumberOfDeletedElements(0);

    // We leave the enumeration table uninitialized.
  }

  // We leave the PropertyDetails table uninitalized.
}

SwissNameDictionary::index_iterator::index_iterator(
    Handle<SwissNameDictionary>& dict, int start)
    : enum_index{start}, dict{dict} {}

SwissNameDictionary::index_iterator&
SwissNameDictionary::index_iterator::operator++() {
  DCHECK_LT(enum_index, used_capacity());
  ++enum_index;
  return *this;
}

bool SwissNameDictionary::index_iterator::operator==(
    const SwissNameDictionary::index_iterator& b) const {
  DCHECK_LE(enum_index, used_capacity());
  DCHECK_LE(b.enum_index, used_capacity());
  // Must be iterators over the same table:
  DCHECK(dict.equals(b.dict));

  return this->enum_index == b.enum_index;
}

bool SwissNameDictionary::index_iterator::operator!=(
    const index_iterator& b) const {
  return !(*this == b);
}

InternalIndex SwissNameDictionary::index_iterator::operator*() {
  DCHECK_LE(enum_index, used_capacity());

  if (enum_index == used_capacity()) return InternalIndex::NotFound();

  return InternalIndex(dict->BucketForEnumerationIndex(enum_index));
}

int SwissNameDictionary::index_iterator::used_capacity() const {
  if (dict.is_null()) {
    return 0;
  }
  return dict->UsedCapacity();
}

SwissNameDictionary::index_iterable::index_iterable(
    Handle<SwissNameDictionary> dict)
    : dict{dict} {}

SwissNameDictionary::index_iterator
SwissNameDictionary::index_iterable::begin() {
  return index_iterator(dict, 0);
}

SwissNameDictionary::index_iterator SwissNameDictionary::index_iterable::end() {
  if (dict.is_null()) {
    return index_iterator(dict, 0);
  } else {
    return index_iterator(dict, dict->UsedCapacity());
  }
}

SwissNameDictionary::index_iterable
SwissNameDictionary::IterateEntriesOrdered() {
  Isolate* isolate;
  if (GetIsolateFromHeapObject(*this, &isolate)) {
    return index_iterable(handle(*this, isolate));
  } else {
    DCHECK_EQ(this->Capacity(), 0);
    return index_iterable(Handle<SwissNameDictionary>::null());
  }
}

SwissNameDictionary::index_iterable SwissNameDictionary::IterateEntries() {
  return IterateEntriesOrdered();
}

void SwissNameDictionary::SetHash(int hash) {
  WriteField(PrefixOffset(), hash);
}

int SwissNameDictionary::Hash() { return ReadField<int>(PrefixOffset()); }

// static
constexpr int SwissNameDictionary::MaxCapacity() {
  // FIXME insert proper  calculation.
  constexpr long result = 1000000;
  STATIC_ASSERT(result <= std::numeric_limits<int>::max());
  return result;
}

// static
constexpr int SwissNameDictionary::PrefixOffset() {
  return HeapObject::kHeaderSize;
}

// static
constexpr int SwissNameDictionary::CapacityOffset() {
  return PrefixOffset() + sizeof(uint32_t);
}

// static
constexpr int SwissNameDictionary::MetaTablePointerOffset() {
  return CapacityOffset() + sizeof(int32_t);
}

// static
constexpr int SwissNameDictionary::DataTableStartOffset() {
  return MetaTablePointerOffset() + kTaggedSize;
}
// static
constexpr int SwissNameDictionary::DataTableEndOffset(int capacity) {
  return CtrlTableStartOffset(capacity);
}

// static
constexpr int SwissNameDictionary::CtrlTableStartOffset(int capacity) {
  return DataTableStartOffset() + capacity * kDataTableEntryCount * kTaggedSize;
}

// static
constexpr int SwissNameDictionary::PropertyDetailsTableStartOffset(
    int capacity) {
  return CtrlTableStartOffset(capacity) +
         // +  kGroupWidth due to the copy of first group at the end of
         // control table
         (capacity + kGroupWidth) * kOneByteSize;
}

// static
bool SwissNameDictionary::IsEmpty(ctrl_t c) { return c == kEmpty; }

// static
bool SwissNameDictionary::IsFull(ctrl_t c) {
  STATIC_ASSERT(Ctrl::kEmpty < 0 && Ctrl::kDeleted < 0 && Ctrl::kSentinel < 0);
  return c >= 0;
}

// static
bool SwissNameDictionary::IsDeleted(ctrl_t c) { return c == kDeleted; }

// static
bool SwissNameDictionary::IsEmptyOrDeleted(ctrl_t c) {
  STATIC_ASSERT(Ctrl::kDeleted < Ctrl::kSentinel &&
                Ctrl::kEmpty < Ctrl::kSentinel && Ctrl::kSentinel < 0);
  return c < kSentinel;
}

// static
probe_seq<SwissNameDictionary::kGroupWidth> SwissNameDictionary::probe(
    uint32_t hash, int capacity) {
  // If capacity is 0, we must produce 1 here, such that the - 1 below
  // yields a valid modulo mask.
  int non_zero_capacity = capacity | (capacity == 0);
  return probe_seq<SwissNameDictionary::kGroupWidth>(
      H1(hash), static_cast<uint32_t>(non_zero_capacity - 1));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_SWISS_HASH_TABLE_INL_H_
