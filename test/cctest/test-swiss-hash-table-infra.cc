// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/test-swiss-hash-table-infra.h"

namespace v8 {
namespace internal {
namespace test_swiss_hash_table {

namespace {
std::vector<PropertyDetails> make_details() {
  std::vector<PropertyDetails> result(32, PropertyDetails::Empty());

  int i = 0;
  for (PropertyKind kind : {PropertyKind::kAccessor, PropertyKind::kAccessor}) {
    for (PropertyConstness constness :
         {PropertyConstness::kConst, PropertyConstness::kMutable}) {
      for (bool writeable : {true, false}) {
        for (bool enumerable : {true, false}) {
          for (bool configurable : {true, false}) {
            uint8_t attrs = static_cast<uint8_t>(PropertyAttributes::NONE);
            if (!writeable) attrs |= PropertyAttributes::READ_ONLY;
            if (!enumerable) {
              attrs |= PropertyAttributes::DONT_ENUM;
            }
            if (!configurable) {
              attrs |= PropertyAttributes::DONT_DELETE;
            }
            PropertyAttributes attributes =
                static_cast<PropertyAttributes>(attrs);
            // FIXME: also deal with constness
            PropertyDetails details(kind, attributes,
                                    PropertyCellType::kNoCell);
            details = details.CopyWithConstness(constness);
            result[i++] = details;
          }
        }
      }
    }
  }
  return result;
}

}  // namespace

Handle<String> RuntimeTestRunner::create_key_with_hash(
    Isolate* isolate, KeysMap& keys, const std::string& key,
    base::Optional<uint32_t> override_h1, base::Optional<uint8_t> override_h2) {
  Handle<String> key_internalized;
  auto it = keys.find(key);

  if (override_h1 || override_h2) {
    if (it == keys.end()) {
      key_internalized = isolate->factory()->InternalizeUtf8String(key.c_str());

      // If this fails then the given key is in the builtin string table.
      DCHECK(!IsReadOnlyHeapObject(*key_internalized));

      keys[key] = key_internalized;

      uint32_t actual_hash = key_internalized->hash();
      int fake_hash = actual_hash;
      if (override_h1) {
        uint32_t override_with = override_h1.value();  // | (1 << 21);
        fake_hash = (override_with << SwissNameDictionary::kH2Bits) |
                    SwissNameDictionary::H2(actual_hash);
      }
      if (override_h2) {
        fake_hash &= 1 << SwissNameDictionary::kH2Bits;
        fake_hash |= SwissNameDictionary::H2(override_h2.value());
      }

      // Prepare what to put into the hash field.
      uint32_t hash_field = fake_hash << Name::kHashShift;

      key_internalized->set_raw_hash_field(hash_field);
      DCHECK_EQ(fake_hash, key_internalized->hash());
    } else {
      key_internalized = it->second;
    }

  } else {
    Handle<String> s = isolate->factory()->NewStringFromAsciiChecked(
        key.c_str(), AllocationType::kOld);
    // StringTable* string_table = isolate()->string_table();
    key_internalized = isolate->string_table()->LookupString(isolate, s);
    DCHECK_EQ(keys.end(), it);
  }
  return key_internalized;
}

template <typename TestRunner>
const std::vector<int>
    TestSequence<TestRunner>::last_capacity_with_representation = {
        1 << (sizeof(uint8_t) * 8), 1 << (sizeof(uint16_t) * 8)};

template <typename TestRunner>
const std::vector<int>
    TestSequence<TestRunner>::interesting_initial_capacities = {
        4,
        8,
        16,
        128,
        1 << (sizeof(uint16_t) * 8),
        1 << (sizeof(uint16_t) * 8 + 1)};

template <typename TestRunner>
const std::vector<PropertyDetails>
    TestSequence<TestRunner>::distinct_property_details = make_details();

template <typename TestRunner>
const KeyOpt TestSequence<TestRunner>::NoKey = base::Optional<Key>();

template <typename TestRunner>
const ValueOpt TestSequence<TestRunner>::NoValue = base::Optional<Key>();

template <typename TestRunner>
const PropertyDetailsOpt TestSequence<TestRunner>::NoDetails =
    base::Optional<PropertyDetails>();

template <typename TestRunner>
const base::Optional<int> TestSequence<TestRunner>::NoInt =
    base::Optional<int>();

template <typename TestRunner>
const base::Optional<InternalIndex> TestSequence<TestRunner>::NoIndex =
    base::Optional<InternalIndex>();

/*
  RuntimeTestRunner
  */

void RuntimeTestRunner::Add(Handle<Name> key, Handle<Object> value,
                            PropertyDetails details) {
  Handle<SwissNameDictionary> updated_table =
      SwissNameDictionary::Add(isolate, this->table, key, value, details);
  this->table = updated_table;
}

void RuntimeTestRunner::CheckData(Handle<Name> key, IndexOpt expected_index,
                                  Handle<Object> value,
                                  PropertyDetailsOpt expected_details) {
  InternalIndex actual_index = table->FindEntry(isolate, *key);
  // table->HeapObjectPrint(std::cout);
  if (expected_index) {
    CHECK_EQ(expected_index.value(), actual_index);
  }
  if (!expected_index || expected_index.value().is_found()) {
    if (!value.is_null()) {
      Handle<Object> act = handle(table->ValueAt(actual_index), isolate);
      // FIXME: right comparison?
      value->StrictEquals(*act);
    }

    if (expected_details) {
      CHECK_EQ(expected_details.value(), table->DetailsAt(actual_index));
    }
  }
}

void RuntimeTestRunner::Put(Handle<Name> key, Handle<Object> new_value,
                            PropertyDetails new_details) {
  InternalIndex index = table->FindEntry(isolate, *key);
  CHECK(index.is_found());

  table->ValueAtPut(index, *new_value);
  table->DetailsAtPut(index, new_details);
}

void RuntimeTestRunner::Delete(Handle<Name> key) {
  InternalIndex index = table->FindEntry(isolate, *key);
  CHECK(index.is_found());
  table = table->DeleteEntry(isolate, table, index);
}

void RuntimeTestRunner::CheckCounts(base::Optional<int> capacity,
                                    base::Optional<int> elements,
                                    base::Optional<int> deleted) {
  if (capacity.has_value()) CHECK_EQ(capacity.value(), table->Capacity());
  if (elements.has_value())
    CHECK_EQ(elements.value(), table->NumberOfElements());
  if (deleted.has_value())
    CHECK_EQ(deleted.value(), table->NumberOfDeletedElements());
  // table->HeapObjectPrint(std::cout);
}

void RuntimeTestRunner::CheckEnumerationOrder(
    std::vector<std::string> expected_keys) {
  ReadOnlyRoots roots(isolate);
  int i = 0;
  for (InternalIndex index : table->IterateEntriesOrdered()) {
    Object key;
    if (table->ToKey(roots, index, &key)) {
      CHECK_LT(i, expected_keys.size());
      Handle<String> expected_key = RuntimeTestRunner::create_key_with_hash(
          isolate, this->keys, expected_keys[i], base::Optional<uint32_t>(),
          base::Optional<uint8_t>());

      CHECK_EQ(key, *expected_key);
      ++i;
    }
  }
  CHECK_EQ(i, expected_keys.size());
}

void RuntimeTestRunner::VerifyHeap() {
#if VERIFY_HEAP
  table->SwissNameDictionaryVerify(isolate, true);
#endif
}

void RuntimeTestRunner::PrintTable() {
  table->SwissNameDictionaryPrint(std::cout);
}

// TODO(v8:11330): Currently, the CSATestRunner isn't doing much, except
// generating runtime calls. That will change once we have the CSA
// implementations ready.
CSATestRunner::CSATestRunner(Isolate* isolate, int initial_capacity,
                             KeysMap& keys)
    : isolate{isolate},
      rtt{isolate, initial_capacity, keys},
      asm_tester(isolate, 1),
      m(asm_tester.state()),
      table{
          m.HeapConstant(isolate->factory()->NewSwissNameDictionaryWithCapacity(
              initial_capacity, AllocationType::kYoung)),
          &m} {
  // TODO(v8:11330) allocate with CSA rather than factory
}

void RuntimeTestRunner::run() {
  // Nothing to do, everything is done immediately when calling functions like
  // Add.
}

void CSATestRunner::run() {
  m.Return(table.value());

  compiler::FunctionTester ft(asm_tester.GenerateCode(), 1);

  Handle<HeapObject>::cast(ft.Call().ToHandleChecked());
}

void CSATestRunner::Add(Handle<Name> key, Handle<Object> value,
                        PropertyDetails details) {
  rtt.Add(key, value, details);

  TNode<Object> v =
      value->IsHeapObject()
          ? m.HeapConstant(Handle<HeapObject>::cast(value))
          : m.UncheckedCast<Object>(m.SmiConstant(Smi::cast(*value)));

  table = m.CallRuntime<SwissNameDictionary>(
      Runtime::kSwissTableAdd, m.NoContextConstant(), table.value(),
      m.HeapConstant(key), v, m.SmiConstant(details.AsSmi()));
}

void CSATestRunner::CheckData(Handle<Name> key, IndexOpt expected_index,
                              Handle<Object> value,
                              PropertyDetailsOpt details) {
  // FIXME: do actual check here.
  CheckAgainstReference();
}

void CSATestRunner::CheckCounts(base::Optional<int> capacity,
                                base::Optional<int> elements,
                                base::Optional<int> deleted) {
  // FIXME: do actual check here.
  CheckAgainstReference();
}

void CSATestRunner::CheckEnumerationOrder(
    std::vector<std::string> expected_keys) {
  CheckAgainstReference();
}

void CSATestRunner::Put(Handle<Name> key, Handle<Object> new_value,
                        PropertyDetails new_details) {
  rtt.Put(key, new_value, new_details);

  TNode<Object> v =
      new_value->IsHeapObject()
          ? m.HeapConstant(Handle<HeapObject>::cast(new_value))
          : m.UncheckedCast<Object>(m.SmiConstant(Smi::cast(*new_value)));

  TNode<Smi> index =
      m.CallRuntime<Smi>(Runtime::kSwissTableFindEntry, m.NoContextConstant(),
                         table.value(), m.HeapConstant(key));
  m.CallRuntime<Smi>(Runtime::kSwissTableUpdate, m.NoContextConstant(),
                     table.value(), index, v,
                     m.SmiConstant(new_details.AsSmi()));
}

void CSATestRunner::Delete(Handle<Name> key) {
  rtt.Delete(key);

  TNode<Smi> index =
      m.CallRuntime<Smi>(Runtime::kSwissTableFindEntry, m.NoContextConstant(),
                         table.value(), m.HeapConstant(key));
  table = m.CallRuntime<SwissNameDictionary>(
      Runtime::kSwissTableDelete, m.NoContextConstant(), table.value(), index);
}

void CSATestRunner::VerifyHeap() {
  // FIXME: This is very expensive if verify-affter-each-step is enabled!
  CheckAgainstReference();
  rtt.VerifyHeap();
}

void CSATestRunner::PrintTable() { m.Print(table.value()); }

void CSATestRunner::CheckAgainstReference() {
  // We must copy the reference table because it may get modified by subsequent
  // test actions on it, but we want to compare against the version as of right
  // now.
  Handle<SwissNameDictionary> reference_table =
      SwissNameDictionary::DebugShallowCopy(isolate, rtt.table);

  TNode<Smi> is_equal =
      m.CallRuntime<Smi>(Runtime::kSwissTableEquals, m.NoContextConstant(),
                         table.value(), m.HeapConstant(reference_table));
  // FIXME: better conditions
  CSA_CHECK(&m, m.Word32Equal(m.SmiToInt32(is_equal), m.Int32Constant(1)));
}

void RuntimeTestRunner::RuntimeOnlyOperation(
    std::function<Handle<SwissNameDictionary>(Handle<SwissNameDictionary>)>
        op_on_map) {
  table = op_on_map(table);
}

template <typename T>
void CSATestRunner::RuntimeOnlyOperation(T ignored) {
  // use if (foo.IsRuntimeTest()) {...} to make sure we never do this for CSA
  // tests.
  CHECK(false);
}

template class TestSequence<RuntimeTestRunner>;
template class TestSequence<CSATestRunner>;

}  // namespace test_swiss_hash_table
}  // namespace internal
}  // namespace v8
