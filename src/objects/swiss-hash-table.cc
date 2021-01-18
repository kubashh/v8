// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/swiss-hash-table-inl.h"

namespace v8 {
namespace internal {

#if VERIFY_HEAP
void SwissNameDictionary::SwissNameDictionaryVerify(Isolate* isolate) {
  this->SwissNameDictionaryVerify(isolate, false);
}

void SwissNameDictionary::SwissNameDictionaryVerify(Isolate* isolate,
                                                    bool slow_checks) {
  DisallowHeapAllocation no_gc;

  CHECK(IsValidCapacity(Capacity()));

  // FIXME: Nothing to check about the hash?

  if (Capacity() > 0) {
    ByteArray meta_table = GetMetaTable();
    meta_table.ByteArrayVerify(isolate);
  } else {
    // FIXME: consolidate?
    Object obj = TaggedField<Object>::Relaxed_Load(isolate, *this,
                                                   MetaTablePointerOffset());
    CHECK_EQ(kNoMetaTableSentinel, Smi::ToInt(obj));
  }

  int seen_deleted = 0;
  int seen_present = 0;

  for (int i = 0; i < Capacity(); i++) {
    ctrl_t ctrl = GetCtrl(i);

    if (IsFull(ctrl) || slow_checks) {
      Object key = KeyAt(i);
      Object value = ValueAtRaw(i);

      if (IsFull(ctrl)) {
        ++seen_present;

        Name name = Name::cast(key);
        if (slow_checks) {
          CHECK_EQ(H2(name.hash()), ctrl);
        }

        CHECK(!key.IsTheHole());
        CHECK(!value.IsTheHole());
        name.NameVerify(isolate);
        key.ObjectVerify(isolate);
      } else if (IsDeleted(ctrl)) {
        ++seen_deleted;
        CHECK(key.IsTheHole());
        CHECK(value.IsTheHole());
      } else if (IsEmpty(ctrl)) {
        CHECK(key.IsTheHole());
        CHECK(value.IsTheHole());
      } else {
        // Something unexpected. Note that we don't use kSentinel at the moment.
        CHECK(false);
      }
    }
  }
  if (slow_checks) {
    CHECK_EQ(seen_present, NumberOfElements());
    CHECK_EQ(seen_deleted, NumberOfDeletedElements());

    // Verify copy of first group at end (= after Capacity() slots) of control
    // table.
    for (int i = 0; i < std::min(static_cast<int>(Group::kWidth), Capacity());
         ++i) {
      CHECK(CtrlTable()[i] == CtrlTable()[Capacity() + i]);
    }
    // If 2 * capacity is smaller than the group width, the slots after that
    // must be empty.
    for (int i = 2 * Capacity(); i < Capacity() + kGroupWidth; ++i) {
      CHECK_EQ(Ctrl::kEmpty, CtrlTable()[i]);
    }
  }

  for (int enum_index = 0; slow_checks && enum_index < UsedCapacity();
       ++enum_index) {
    int index = BucketForEnumerationIndex(enum_index);
    CHECK_LT(index, Capacity());
    ctrl_t ctrl = GetCtrl(index);

    // Enum table must not point to empty slots.
    CHECK(IsFull(ctrl) || IsDeleted(ctrl));
  }
}
#endif

// static
Handle<SwissNameDictionary> SwissNameDictionary::DeleteEntry(
    Isolate* isolate, Handle<SwissNameDictionary> table, InternalIndex index) {
  DCHECK(index.is_found());
  DCHECK_LT(index.as_int(), table->Capacity());
  DCHECK(IsFull(table->GetCtrl(index.as_int())));

  int i = index.as_int();

  Object hole = ReadOnlyRoots(isolate).the_hole_value();

  table->SetCtrl(i, Ctrl::kDeleted);
  table->SetKey(i, hole);
  table->SetValue(i, hole);
  // We leave the PropertyDetails unchanged because they are not relevant for
  // GC.

  int nof = table->NumberOfElements();
  table->SetNumberOfElements(nof - 1);
  int nod = table->NumberOfDeletedElements();
  table->SetNumberOfDeletedElements(nod + 1);

  // FIXME: abseil does not shrink on deletion, but OrderedNameDictioanry does.
  // Abseil may remove deleted elements on insertion, though
  return table;
  // return Shrink(isolate, table);
}

// static
template <typename LocalIsolate>
Handle<SwissNameDictionary> SwissNameDictionary::Rehash(
    LocalIsolate* isolate, Handle<SwissNameDictionary> table,
    int new_capacity) {
  // FIXME: No support for in-place rehashing yet. See Abseils
  // rehash_and_grow_if_necessary

  DCHECK(IsValidCapacity(new_capacity));
  ReadOnlyRoots roots(isolate);

  Handle<SwissNameDictionary> new_table =
      isolate->factory()->NewSwissNameDictionaryWithCapacity(
          new_capacity, AllocationType::kYoung);

  DisallowHeapAllocation no_gc;

  for (int enum_index = 0; enum_index < table->UsedCapacity(); ++enum_index) {
    int index = table->BucketForEnumerationIndex(enum_index);

    Object key;

    if (table->ToKey(roots, index, &key)) {
      Object value = table->ValueAtRaw(index);
      PropertyDetails details = table->DetailsAtRaw(index);

      // FIXME: inline necessary parts of Add here? for example, no need to set
      // number of elements n times
      new_table = SwissNameDictionary::Add(isolate, new_table,
                                           handle(Name::cast(key), isolate),
                                           handle(value, isolate), details);
    }
  }

  new_table->SetHash(table->Hash());
  return new_table;
}

bool SwissNameDictionary::DebugEquals(SwissNameDictionary other) {
  if (Capacity() != other.Capacity() ||
      NumberOfElements() != other.NumberOfElements() ||
      NumberOfDeletedElements() != other.NumberOfDeletedElements())
    return false;

  for (int i = 0; i < Capacity() + kGroupWidth; i++) {
    if (CtrlTable()[i] != other.CtrlTable()[i]) return false;

    if (i < Capacity()) {
      if (ValueAtRaw(i).ptr() != other.ValueAtRaw(i).ptr()) return false;
    }

    if (i < UsedCapacity()) {
      if (BucketForEnumerationIndex(i) != other.BucketForEnumerationIndex(i))
        return false;
    }
  }

  return true;
}

// static
Handle<SwissNameDictionary> SwissNameDictionary::DebugShallowCopy(
    Isolate* isolate, Handle<SwissNameDictionary> table) {
  if (table->Capacity() == 0) {
    return table;
  }

  Handle<SwissNameDictionary> copy =
      isolate->factory()->NewSwissNameDictionaryWithCapacity(
          table->Capacity(), AllocationType::kYoung);
  ByteArray original_meta_table = table->GetMetaTable();
  ByteArray copy_meta_table = copy->GetMetaTable();

  void* original_start =
      reinterpret_cast<void*>(table->field_address(PrefixOffset()));
  void* copy_start =
      reinterpret_cast<void*>(copy->field_address(PrefixOffset()));
  size_t size = SizeFor(table->Capacity());
  MemCopy(copy_start, original_start, size);

  copy->SetMetaTable(copy_meta_table);
  copy_meta_table.copy_in(0, original_meta_table.GetDataStartAddress(),
                          original_meta_table.length());

  return copy;
}

// static
Handle<SwissNameDictionary> SwissNameDictionary::Shrink(
    Isolate* isolate, Handle<SwissNameDictionary> table) {
  // FIXME: right algorithm for deciding when to shrink? This is the one from
  // ordered hash table, not abseil.
  int nof = table->NumberOfElements();
  int capacity = table->Capacity();
  if (nof >= (capacity >> 2)) return table;
  return Rehash(isolate, table, capacity / 2);
}

// FIXME. This is horrible.
void SwissNameDictionary::RehashInplace(Isolate* isolate) {
  DisallowHeapAllocation no_gc;

  if (Capacity() == 0) return;

  using D = std::tuple<Address, Address, uint8_t>;
  std::vector<D> data(NumberOfElements());
  for (int enum_index = 0; enum_index < UsedCapacity(); ++enum_index) {
    int index = BucketForEnumerationIndex(enum_index);

    data[enum_index] =
        std::make_tuple(KeyAt(index).ptr(), ValueAtRaw(index).ptr(),
                        DetailsAtRaw(index).ToByte());
  }

  Initialize(isolate, GetMetaTable(), Capacity());

  // fixme: use new Handle-less version of Add for this?
  int elements = 0;
  for (D& triple : data) {
    Name name = Name::cast(Object(std::get<0>(triple)));
    Object value = Object::cast(Object(std::get<1>(triple)));
    PropertyDetails details = PropertyDetails::FromByte(std::get<2>(triple));

    uint32_t hash = name.hash();

    int target = FindFirstEmpty(hash);

    SetCtrl(target, H2(hash));
    SetKey(target, name);
    ValueAtPut(target, value);
    DetailsAtPut(target, details);

    int new_enum_index = elements++;
    SetNumberOfElements(elements);
    SetEnumerationTableMapping(new_enum_index, target);
  }
}

template Handle<SwissNameDictionary> SwissNameDictionary::Rehash(
    LocalIsolate* isolate, Handle<SwissNameDictionary> table, int new_capacity);

template Handle<SwissNameDictionary> SwissNameDictionary::Rehash(
    Isolate* isolate, Handle<SwissNameDictionary> table, int new_capacity);

}  // namespace internal
}  // namespace v8
