// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TEST_CCTEST_TEST_SWISS_HASH_TABLE_INFRA_H_
#define V8_TEST_CCTEST_TEST_SWISS_HASH_TABLE_INFRA_H_

#include <memory>
#include <utility>

#include "src/codegen/code-stub-assembler.h"
#include "src/init/v8.h"
#include "src/objects/objects-inl.h"
#include "src/objects/swiss-hash-table-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/code-assembler-tester.h"
#include "test/cctest/compiler/function-tester.h"

namespace v8 {
namespace internal {
namespace test_swiss_hash_table {

using Key = std::string;
using KeyOpt = base::Optional<std::string>;
using Value = std::string;
using ValueOpt = base::Optional<Value>;
using PropertyDetailsOpt = base::Optional<PropertyDetails>;
using IndexOpt = base::Optional<InternalIndex>;

using KeysMap = std::unordered_map<std::string, Handle<String>>;

class CSATestRunner;

// Executes test operations by calling the corresponding C++ functions.
class RuntimeTestRunner {
 public:
  RuntimeTestRunner(Isolate* isolate, int initial_capacity, KeysMap& keys)
      : isolate{isolate}, keys{keys} {
    table = isolate->factory()->NewSwissNameDictionaryWithCapacity(
        initial_capacity, AllocationType::kYoung);
  }

  void run();

  void Add(Handle<Name> key, Handle<Object> value, PropertyDetails details);

  void CheckData(Handle<Name> key, IndexOpt expected_index,
                 Handle<Object> value, PropertyDetailsOpt details);

  void CheckCounts(base::Optional<int> capacity, base::Optional<int> elements,
                   base::Optional<int> deleted);

  void CheckEnumerationOrder(std::vector<std::string> expected_keys);

  void Put(Handle<Name> key, Handle<Object> new_value,
           PropertyDetails new_details);

  void Delete(Handle<Name> key);

  void RuntimeOnlyOperation(
      std::function<Handle<SwissNameDictionary>(Handle<SwissNameDictionary>)>
          op_on_map);

  void VerifyHeap();

  void PrintTable();

  static Handle<String> create_key_with_hash(
      Isolate* isolate, KeysMap& keys, const std::string& key,
      base::Optional<uint32_t> override_h1,
      base::Optional<uint8_t> override_h2);

 private:
  Isolate* isolate;
  KeysMap& keys;

  Handle<SwissNameDictionary> table;

  friend CSATestRunner;
};

// Tests operations by generating code executing them once |run| is called.
class CSATestRunner {
 public:
  CSATestRunner(Isolate* isolate, int initial_capacity, KeysMap& keys);

  void run();

  void Add(Handle<Name> key, Handle<Object> value, PropertyDetails details);

  void CheckData(Handle<Name> key, IndexOpt expected_index,
                 Handle<Object> value, PropertyDetailsOpt details);

  void CheckCounts(base::Optional<int> capacity, base::Optional<int> elements,
                   base::Optional<int> deleted);

  void CheckEnumerationOrder(std::vector<std::string> expected_keys);

  void Put(Handle<Name> key, Handle<Object> new_value,
           PropertyDetails new_details);

  void Delete(Handle<Name> key);

  template <typename T>
  void RuntimeOnlyOperation(T ignored);

  void VerifyHeap();

  void PrintTable();

 private:
  void CheckAgainstReference();

  Isolate* isolate;
  RuntimeTestRunner rtt;

  compiler::CodeAssemblerTester asm_tester;
  CodeStubAssembler m;
  CodeStubAssembler::TVariable<SwissNameDictionary> table;
};

// Abstraction over a sequence of operations on a single hash table.
// Actually performing those operations is done by the TestRunner.
template <typename TestRunner>
class TestSequence {
 public:
  explicit TestSequence(Isolate* isolate, int initial_capacity)
      : keys{},
        isolate{isolate},
        factory{isolate->factory()},
        initial_capacity{initial_capacity},
        runner{isolate, initial_capacity, keys} {}

  // Can make debugging easier.
  static constexpr bool kVerifyAfterEachStep = false;

  void run() { runner.run(); }

  void Add(Handle<Name> key, Handle<Object> value, PropertyDetails details) {
    runner.Add(key, value, details);

    if (kVerifyAfterEachStep) {
      runner.VerifyHeap();
    }
  }

  void Add(Key key, ValueOpt value = NoValue,
           PropertyDetailsOpt details = NoDetails,
           base::Optional<uint32_t> override_h1 = base::Optional<uint32_t>(),
           base::Optional<uint8_t> override_h2 = base::Optional<uint8_t>()) {
    if (!value) {
      value = "dummy_value";
    }

    if (!details) {
      details = PropertyDetails::Empty();
    }

    Handle<Name> key_handle = RuntimeTestRunner::create_key_with_hash(
        isolate, keys, key, override_h1, override_h2);
    Handle<Object> value_handle = isolate->factory()->NewStringFromAsciiChecked(
        value.value().c_str(), AllocationType::kYoung);

    Add(key_handle, value_handle, details.value());
  }

  void UpdateByKey(Handle<Name> key, Handle<Object> new_value,
                   PropertyDetails new_details) {
    runner.Put(key, new_value, new_details);

    if (kVerifyAfterEachStep) {
      runner.VerifyHeap();
    }
  }

  void UpdateByKey(
      Key existing_key, ValueOpt new_value, PropertyDetailsOpt new_details,
      base::Optional<uint32_t> override_h1 = base::Optional<uint32_t>(),
      base::Optional<uint8_t> override_h2 = base::Optional<uint8_t>()) {
    Handle<Name> key_handle = RuntimeTestRunner::create_key_with_hash(
        isolate, keys, existing_key, override_h1, override_h2);
    Handle<Object> value_handle = isolate->factory()->NewStringFromAsciiChecked(
        new_value.value().c_str(), AllocationType::kYoung);

    UpdateByKey(key_handle, value_handle, new_details.value());
  }

  void DeleteByKey(Handle<Name> key) {
    runner.Delete(key);

    if (kVerifyAfterEachStep) {
      runner.VerifyHeap();
    }
  }

  void DeleteByKey(
      Key existing_key,
      base::Optional<uint32_t> override_h1 = base::Optional<uint32_t>(),
      base::Optional<uint8_t> override_h2 = base::Optional<uint8_t>()) {
    Handle<Name> key_handle = RuntimeTestRunner::create_key_with_hash(
        isolate, keys, existing_key, override_h1, override_h2);

    DeleteByKey(key_handle);
  }

  void CheckDataAtKey(Handle<Name> key, IndexOpt expected_index_opt,
                      Handle<Object> expected_value_opt,
                      PropertyDetailsOpt expected_details_opt) {
    runner.CheckData(key, expected_index_opt, expected_value_opt,
                     expected_details_opt);
  }
  void CheckDataAtKey(
      Key expected_key, IndexOpt expected_index,
      ValueOpt expected_value = NoValue,
      PropertyDetailsOpt expected_details = NoDetails,
      base::Optional<uint32_t> override_h1 = base::Optional<uint32_t>(),
      base::Optional<uint8_t> override_h2 = base::Optional<uint8_t>()) {
    Handle<Name> key_handle = RuntimeTestRunner::create_key_with_hash(
        isolate, keys, expected_key, override_h1, override_h2);
    Handle<Object> value_handle;
    if (expected_value) {
      value_handle = isolate->factory()->NewStringFromAsciiChecked(
          expected_value.value().c_str(), AllocationType::kYoung);
    }

    CheckDataAtKey(key_handle, expected_index, value_handle, expected_details);
  }

  void CheckKeyAbsent(Handle<Name> key) {
    runner.CheckData(key, InternalIndex::NotFound(), Handle<Object>::null(),
                     NoDetails);
  }
  void CheckKeyAbsent(
      Key expected_key,
      base::Optional<uint32_t> override_h1 = base::Optional<uint32_t>(),
      base::Optional<uint8_t> override_h2 = base::Optional<uint8_t>()) {
    Handle<Name> key_handle = RuntimeTestRunner::create_key_with_hash(
        isolate, keys, expected_key, override_h1, override_h2);
    CheckKeyAbsent(key_handle);
  }

  void CheckHasKey(
      Key expected_key,
      base::Optional<uint32_t> override_h1 = base::Optional<uint32_t>(),
      base::Optional<uint8_t> override_h2 = base::Optional<uint8_t>()) {
    Handle<Name> key_handle = RuntimeTestRunner::create_key_with_hash(
        isolate, keys, expected_key, override_h1, override_h2);

    runner.CheckData(key_handle, base::Optional<InternalIndex>(),
                     Handle<Object>::null(), NoDetails);
  }

  void CheckFreeAt(InternalIndex index) {}

  void CheckCounts(base::Optional<int> capacity, base::Optional<int> elements,
                   base::Optional<int> deleted) {
    runner.CheckCounts(capacity, elements, deleted);
  }

  void CheckEnumerationOrder(std::vector<std::string> keys) {
    runner.CheckEnumerationOrder(keys);
  }

  // Gives direct access to the SwissNameDictionary being tested. Thefore only
  // allowed in runtime-only tests.
  void RuntimeOnlyOperation(
      std::function<Handle<SwissNameDictionary>(Handle<SwissNameDictionary>)>
          op_on_map) {
    runner.RuntimeOnlyOperation(op_on_map);
  }

  static constexpr bool IsRuntimeTest() {
    return std::is_same<TestRunner, RuntimeTestRunner>::value;
  }

  void VerifyHeap() { runner.VerifyHeap(); }

  // Just for debugging
  void Print() { runner.PrintTable(); }

  /*
    Helpers that result in several of the more primitive operations being
    performed.
  */

  void AddMultiple(int count, std::string key_prefix = "key",
                   std::string value_prefix = "value", int details_offset = 0) {
    DCHECK_LT(count + details_offset, distinct_property_details.size());
    for (int i = 0; i < count; ++i) {
      std::string key = key_prefix + std::to_string(i);
      std::string value = value_prefix + std::to_string(i);
      PropertyDetails d = distinct_property_details[details_offset + i];
      Add(key, value, d);
    }
  }

  void CheckMultiple(int count, std::string key_prefix = "key",
                     std::string value_prefix = "value", int details_offset = 0,
                     bool check_counts = true) {
    DCHECK_LT(count + details_offset, distinct_property_details.size());
    DCHECK_LE(count, SwissNameDictionary::MaxUsableCapacity(initial_capacity));

    std::vector<std::string> expected_keys;
    for (int i = 0; i < count; ++i) {
      std::string key = key_prefix + std::to_string(i);
      expected_keys.push_back(key);
      std::string value = value_prefix + std::to_string(i);
      PropertyDetails d = distinct_property_details[details_offset + i];
      CheckDataAtKey(key, NoIndex, value, d);
    }
    if (check_counts) {
      CheckCounts(initial_capacity, count, 0);
    }
    CheckEnumerationOrder(expected_keys);
  }

  void AddAtBoundaries(bool check) {
    int capacity = initial_capacity;
    DCHECK_GE(capacity, 4);

    std::vector<int> interesting_indices = boundary_indices(capacity);
    int size = static_cast<int>(interesting_indices.size());
    if (check) CheckCounts(capacity, 0, 0);

    int count = 0;
    for (int index : interesting_indices) {
      std::string key = std::string("k") + std::to_string(index);
      std::string value = std::string("v") + std::to_string(index);
      PropertyDetails details =
          TestSequence::distinct_property_details.at(count++);
      Add(key, value, details, index);
    }
    if (check) {
      count = 0;
      for (int index : interesting_indices) {
        std::string key = std::string("k") + std::to_string(index);
        std::string value = std::string("v") + std::to_string(index);
        PropertyDetails details =
            TestSequence::distinct_property_details.at(count++);
        CheckDataAtKey(key, InternalIndex(index), value, details, index);
      }
      CheckCounts(capacity, size, 0);
    }
  }

  void UpdateAtBoundaries() {
    int capacity = initial_capacity;
    DCHECK_GE(capacity, 4);

    std::vector<int> interesting_indices = boundary_indices(capacity);
    int count = 0;
    for (int index : interesting_indices) {
      std::string key = std::string("k") + std::to_string(index);
      std::string value = std::string("newv") + std::to_string(index);
      PropertyDetails details = distinct_property_details.at(
          distinct_property_details.size() - 1 - count++);
      UpdateByKey(key, value, details, index);
    }
    count = 0;
    for (int index : interesting_indices) {
      std::string key = std::string("k") + std::to_string(index);
      std::string value = std::string("newv") + std::to_string(index);
      PropertyDetails details = distinct_property_details.at(
          distinct_property_details.size() - 1 - count++);
      CheckDataAtKey(key, InternalIndex(index), value, details, index);
    }
  }

  void DeleteAtBoundaries(bool check) {
    int capacity = initial_capacity;
    DCHECK_GE(capacity, 4);

    std::vector<int> interesting_indices = boundary_indices(capacity);
    int size = static_cast<int>(interesting_indices.size());
    if (check) {
      CheckCounts(capacity, size, 0);
    }
    for (int index : interesting_indices) {
      std::string key = std::string("k") + std::to_string(index);
      DeleteByKey(key, index);
    }
    if (check) {
      for (int index : interesting_indices) {
        std::string key = std::string("k") + std::to_string(index);
        CheckKeyAbsent(key, index);
      }
      CheckCounts(capacity, 0, size);
    }
  }

  void OverwriteAtBoundaries() {
    int capacity = initial_capacity;
    DCHECK_GE(capacity, 4);

    std::vector<int> interesting_indices = boundary_indices(capacity);

    std::vector<std::string> keys, values;
    std::vector<PropertyDetails> details;

    int count = 0;
    for (int index : interesting_indices) {
      std::string key = std::string("additional_k") + std::to_string(index);
      std::string value = std::string("additional_v") + std::to_string(index);

      // 12 is just some arbitrary offset into the property details list.
      PropertyDetails d =
          TestSequence::distinct_property_details.at(12 + count++);
      keys.push_back(key);
      values.push_back(value);
      details.push_back(d);
      Add(key, value, d, index);
    }

    count = 0;
    for (int index : interesting_indices) {
      std::string key = keys[count];
      std::string value = values[count];
      PropertyDetails d = details[count];
      // We don't know the indices where the new entries will land.
      CheckDataAtKey(key, base::Optional<InternalIndex>(), value, d, index);
      count++;
    }
  }

  std::unordered_map<std::string, Handle<String>> keys;
  Isolate* isolate;
  Factory* factory;

  std::vector<std::unique_ptr<Operation>> operations;

  int initial_capacity;

  TestRunner runner;

  static std::vector<int> boundary_indices(int capacity) {
    if (capacity == 4 && SwissNameDictionary::MaxUsableCapacity(4) < 4) {
      // If we cannot put 4 entries in a capacity 4 table without resizing, just
      // work with 3 boundary indices.
      return {0, capacity - 2, capacity - 1};
    }
    return {0, 1, capacity - 2, capacity - 1};
  }

  static const std::vector<int> last_capacity_with_representation;

  static const std::vector<int> interesting_initial_capacities;

  static const std::vector<PropertyDetails> distinct_property_details;

  static const KeyOpt NoKey;
  static const ValueOpt NoValue;
  static const PropertyDetailsOpt NoDetails;
  static const base::Optional<int> NoInt;
  static const base::Optional<InternalIndex> NoIndex;

  static Isolate* GetIsolateFrom(LocalContext* context) {
    return reinterpret_cast<Isolate*>((*context)->GetIsolate());
  }

  static void WithAllInterestingInitialCapacities(
      std::function<void(TestSequence&)> manipulate_sequence) {
    WithInitialCapacities(interesting_initial_capacities, manipulate_sequence);
  }

  static void WithInitialCapacity(
      int capacity, std::function<void(TestSequence&)> manipulate_sequence) {
    WithInitialCapacities({capacity}, manipulate_sequence);
  }

  static void WithInitialCapacities(
      std::vector<int> capacities,
      std::function<void(TestSequence&)> manipulate_sequence) {
    for (int capacity : capacities) {
      Isolate* isolate = CcTest::InitIsolateOnce();
      HandleScope scope{isolate};
      TestSequence<TestRunner> s(isolate, capacity);
      manipulate_sequence(s);
      s.run();
    }
  }
};

}  // namespace test_swiss_hash_table
}  // namespace internal
}  // namespace v8

#endif  // V8_TEST_CCTEST_TEST_SWISS_HASH_TABLE_INFRA_H_
