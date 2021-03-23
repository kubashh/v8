// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/code-stub-assembler.h"
#include "src/init/v8.h"
#include "src/objects/objects-inl.h"
#include "src/objects/swiss-name-dictionary-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/code-assembler-tester.h"
#include "test/cctest/compiler/function-tester.h"
#include "test/cctest/test-swiss-name-dictionary-infra.h"
#include "test/cctest/test-swiss-name-dictionary-shared-tests.h"

namespace v8 {
namespace internal {
namespace test_swiss_hash_table {

// Executes tests by executing CSA/Torque versions of dictionary operations.
// See RuntimeTestRunner for description of public functions.
class CSATestRunner {
 public:
  CSATestRunner(Isolate* isolate, int initial_capacity, KeyCache& keys);

  void Add(Handle<Name> key, Handle<Object> value, PropertyDetails details);
  InternalIndex FindEntry(Handle<Name> key);
  void Put(InternalIndex entry, Handle<Object> new_value,
           PropertyDetails new_details);
  void Delete(InternalIndex entry);
  void RehashInplace();
  void Shrink();

  Handle<FixedArray> GetData(InternalIndex entry);
  void CheckCounts(base::Optional<int> capacity, base::Optional<int> elements,
                   base::Optional<int> deleted);
  void CheckEnumerationOrder(const std::vector<std::string>& expected_keys);
  void CheckCopy();
  void VerifyHeap();

  void PrintTable();

  Handle<SwissNameDictionary> table;

 private:
  using Label = compiler::CodeAssemblerLabel;
  template <class T>
  using TVariable = compiler::TypedCodeAssemblerVariable<T>;

  void CheckAgainstReference();

  void Allocate(Handle<Smi> capacity);

  Isolate* isolate_;

  // Used to mirror all operations using C++ versions of all operations,
  // yielding a reference to compare against.
  Handle<SwissNameDictionary> reference_;

  // CSA functions execute the corresponding dictionary operation.
  compiler::FunctionTester find_entry_ft_;
  compiler::FunctionTester get_data_ft_;
  compiler::FunctionTester put_ft_;
  compiler::FunctionTester delete_ft_;
  compiler::FunctionTester add_ft_;
  compiler::FunctionTester allocate_ft_;
  compiler::FunctionTester get_counts_ft_;
  compiler::FunctionTester copy_ft_;

  // Used to create the FunctionTesters above.
  static Handle<Code> create_get_data(Isolate* isolate);
  static Handle<Code> create_find_entry(Isolate* isolate);
  static Handle<Code> create_put(Isolate* isolate);
  static Handle<Code> create_delete(Isolate* isolate);
  static Handle<Code> create_add(Isolate* isolate);
  static Handle<Code> create_allocate(Isolate* isolate);
  static Handle<Code> create_get_counts(Isolate* isolate);
  static Handle<Code> create_copy(Isolate* isolate);

  // Number of parameters of each of the tester functions above.
  static constexpr int kFindEntryParams = 2;  // (table, key)
  static constexpr int kGetDataParams = 2;    // (table, entry)
  static constexpr int kPutParams = 4;        // (table, entry, value,  details)
  static constexpr int kDeleteParams = 2;     // (table, entry)
  static constexpr int kAddParams = 4;        // (table, key, value, details)
  static constexpr int kAllocateParams = 1;   // (capacity)
  static constexpr int kGetCountsParams = 1;  // (table)
  static constexpr int kCopyParams = 1;       // (table)
};

CSATestRunner::CSATestRunner(Isolate* isolate, int initial_capacity,
                             KeyCache& keys)
    : isolate_{isolate},
      reference_{isolate_->factory()->NewSwissNameDictionaryWithCapacity(
          initial_capacity, AllocationType::kYoung)},
      find_entry_ft_(create_find_entry(isolate), kFindEntryParams),
      get_data_ft_(create_get_data(isolate), kGetDataParams),
      put_ft_{create_put(isolate), kPutParams},
      delete_ft_{create_delete(isolate), kDeleteParams},
      add_ft_{create_add(isolate), kAddParams},
      allocate_ft_{create_allocate(isolate), kAllocateParams},
      get_counts_ft_{create_get_counts(isolate), kGetCountsParams},
      copy_ft_{create_copy(isolate), kCopyParams} {
  int at_least_space_for =
      SwissNameDictionary::MaxUsableCapacity(initial_capacity);
  Allocate(handle(Smi::FromInt(at_least_space_for), isolate));
}

void CSATestRunner::Add(Handle<Name> key, Handle<Object> value,
                        PropertyDetails details) {
  ReadOnlyRoots roots(isolate_);
  reference_ =
      SwissNameDictionary::Add(isolate_, reference_, key, value, details);

  Handle<Smi> details_smi = handle(details.AsSmi(), isolate_);
  Handle<Oddball> success =
      add_ft_.CallChecked<Oddball>(table, key, value, details_smi);

  if (*success == roots.false_value()) {
    // |add_ft_| does not resize and indicates the need to do so by returning
    // false.
    int capacity = table->Capacity();
    int used_capacity = table->UsedCapacity();
    CHECK_GT(used_capacity + 1,
             SwissNameDictionary::MaxUsableCapacity(capacity));

    table = SwissNameDictionary::Add(isolate_, table, key, value, details);
  }

  CheckAgainstReference();
}

void CSATestRunner::Allocate(Handle<Smi> capacity) {
  // FIXME: capacity 0 will return a non-zero dict at the moment

  if (capacity->value() == 0) {
    table = handle(ReadOnlyRoots(isolate_).empty_swiss_property_dictionary(),
                   isolate_);
  } else {
    table = allocate_ft_.CallChecked<SwissNameDictionary>(capacity);
  }

  CheckAgainstReference();
}

InternalIndex CSATestRunner::FindEntry(Handle<Name> key) {
  Handle<Smi> index = find_entry_ft_.CallChecked<Smi>(table, key);
  if (index->value() == SwissNameDictionary::kNotFoundSentinel) {
    return InternalIndex::NotFound();
  } else {
    return InternalIndex(index->value());
  }
}

Handle<FixedArray> CSATestRunner::GetData(InternalIndex entry) {
  DCHECK(entry.is_found());

  return get_data_ft_.CallChecked<FixedArray>(
      table, handle(Smi::FromInt(entry.as_int()), isolate_));
}

void CSATestRunner::CheckCounts(base::Optional<int> capacity,
                                base::Optional<int> elements,
                                base::Optional<int> deleted) {
  Handle<FixedArray> counts = get_counts_ft_.CallChecked<FixedArray>(table);

  if (capacity.has_value()) {
    CHECK_EQ(Smi::FromInt(capacity.value()), counts->get(0));
  }

  if (elements.has_value()) {
    CHECK_EQ(Smi::FromInt(elements.value()), counts->get(1));
  }

  if (deleted.has_value()) {
    CHECK_EQ(Smi::FromInt(deleted.value()), counts->get(2));
  }

  CheckAgainstReference();
}

void CSATestRunner::CheckEnumerationOrder(
    const std::vector<std::string>& expected_keys) {
  // TODO(v8:11330) Do actual check here once CSA/Torque version exists.
  CheckAgainstReference();
}

void CSATestRunner::Put(InternalIndex entry, Handle<Object> new_value,
                        PropertyDetails new_details) {
  DCHECK(entry.is_found());
  reference_->ValueAtPut(entry, *new_value);
  reference_->DetailsAtPut(entry, new_details);

  Handle<Smi> entry_smi = handle(Smi::FromInt(entry.as_int()), isolate_);
  Handle<Smi> details_smi = handle(new_details.AsSmi(), isolate_);

  put_ft_.Call(table, entry_smi, new_value, details_smi);

  CheckAgainstReference();
}

void CSATestRunner::Delete(InternalIndex entry) {
  DCHECK(entry.is_found());
  reference_ = SwissNameDictionary::DeleteEntry(isolate_, reference_, entry);

  Handle<Smi> entry_smi = handle(Smi::FromInt(entry.as_int()), isolate_);
  table = delete_ft_.CallChecked<SwissNameDictionary>(table, entry_smi);
  CheckAgainstReference();
}

void CSATestRunner::RehashInplace() {
  // There's no CSA version of this. Use IsRuntimeTest to ensure that we only
  // run a test using this if it's a runtime test.
  UNREACHABLE();
}

void CSATestRunner::Shrink() {
  // There's no CSA version of this. Use IsRuntimeTest to ensure that we only
  // run a test using this if it's a runtime test.
  UNREACHABLE();
}

void CSATestRunner::CheckCopy() {
  int capacity = table->Capacity();
  int size = SwissNameDictionary::SizeFor(capacity);
  if (size <= kMaxRegularHeapObjectSize) {
    // The CSA version can only handle none-large objects.

    Handle<SwissNameDictionary> copy =
        copy_ft_.CallChecked<SwissNameDictionary>(table);
    CHECK(table->EqualsForTesting(*copy));
  }
}

void CSATestRunner::VerifyHeap() {
#if VERIFY_HEAP
  table->SwissNameDictionaryVerify(isolate_, true);
#endif
}

void CSATestRunner::PrintTable() {
#ifdef OBJECT_PRINT
  table->SwissNameDictionaryPrint(std::cout);
#endif
}

Handle<Code> CSATestRunner::create_find_entry(Isolate* isolate) {
  STATIC_ASSERT(kFindEntryParams == 2);  // (table, key)
  compiler::CodeAssemblerTester asm_tester(isolate, kFindEntryParams + 1);
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);
    TNode<Name> key = m.Parameter<Name>(2);

    Label done(&m);
    TVariable<IntPtrT> entry_var(
        m.IntPtrConstant(SwissNameDictionary::kNotFoundSentinel), &m);

    m.SwissNameDictionaryFindEntry(table, key, &done, &entry_var, &done);

    m.Bind(&done);
    m.Return(m.SmiFromIntPtr(entry_var.value()));
  }

  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_get_data(Isolate* isolate) {
  STATIC_ASSERT(kGetDataParams == 2);  // (table, entry)
  compiler::CodeAssemblerTester asm_tester(isolate, kGetDataParams + 1);
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);
    TNode<Smi> index = m.Parameter<Smi>(2);
    TNode<IntPtrT> index_intptr = m.SmiToIntPtr(index);

    Label not_found(&m);

    m.GotoIf(m.SmiEqual(index,
                        m.SmiConstant(SwissNameDictionary::kNotFoundSentinel)),
             &not_found);

    TNode<FixedArray> data = m.AllocateZeroedFixedArray(m.IntPtrConstant(3));

    TNode<Object> key = m.LoadSwissNameDictionaryKey(table, index_intptr);
    TNode<Object> value = m.LoadValueByKeyIndex(table, index_intptr);
    TNode<Smi> details =
        m.SmiFromInt32(m.Signed(m.LoadDetailsByKeyIndex(table, index_intptr)));

    m.StoreFixedArrayElement(data, 0, key);
    m.StoreFixedArrayElement(data, 1, value);
    m.StoreFixedArrayElement(data, 2, details);

    m.Return(data);

    m.Bind(&not_found);

    m.Return(m.EmptyFixedArrayConstant());
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_put(Isolate* isolate) {
  STATIC_ASSERT(kPutParams == 4);  // (table, entry, value, details)
  compiler::CodeAssemblerTester asm_tester(isolate, kPutParams + 1);
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);
    TNode<Smi> entry = m.Parameter<Smi>(2);
    TNode<Object> value = m.Parameter<Object>(3);
    TNode<Smi> details = m.Parameter<Smi>(4);

    TNode<IntPtrT> entry_intptr = m.SmiToIntPtr(entry);

    m.StoreValueByKeyIndex(table, entry_intptr, value,
                           WriteBarrierMode::UPDATE_WRITE_BARRIER);
    m.StoreDetailsByKeyIndex(table, entry_intptr, details);

    m.Return(m.UndefinedConstant());
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_delete(Isolate* isolate) {
  STATIC_ASSERT(kDeleteParams == 2);  // (table, entry)
  compiler::CodeAssemblerTester asm_tester(isolate, kDeleteParams + 1);
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);
    TNode<IntPtrT> entry = m.SmiToIntPtr(m.Parameter<Smi>(2));

    TVariable<SwissNameDictionary> shrunk_table_var(table, &m);
    Label done(&m);

    m.SwissNameDictionaryDelete(table, entry, &done, &shrunk_table_var);
    m.Goto(&done);

    m.Bind(&done);
    m.Return(shrunk_table_var.value());
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_add(Isolate* isolate) {
  STATIC_ASSERT(kAddParams == 4);  // (table, key, value, details)
  compiler::CodeAssemblerTester asm_tester(isolate, kAddParams + 1);
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);
    TNode<Name> key = m.Parameter<Name>(2);
    TNode<Object> value = m.Parameter<Object>(3);
    TNode<Smi> details = m.Parameter<Smi>(4);

    Label needs_resize(&m);

    // We don't need the clamped part.
    TNode<Int32T> d32 = m.SmiToInt32(details);
    TNode<Uint8T> d = m.Int32ToUint8Clamped(d32);

    m.SwissNameDictionaryAdd(table, key, value, d, &needs_resize);
    m.Return(m.TrueConstant());

    m.Bind(&needs_resize);
    m.Return(m.FalseConstant());
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_allocate(Isolate* isolate) {
  STATIC_ASSERT(kAllocateParams == 1);  // (capacity)
  compiler::CodeAssemblerTester asm_tester(isolate, kAllocateParams + 1);
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<Smi> at_least_space_for_smi = m.Parameter<Smi>(1);
    TNode<IntPtrT> at_least_space_for = m.SmiToIntPtr(at_least_space_for_smi);

    TNode<SwissNameDictionary> table =
        m.AllocateSwissNameDictionary(at_least_space_for);

    m.Return(table);
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_get_counts(Isolate* isolate) {
  STATIC_ASSERT(kGetCountsParams == 1);  // (table)
  compiler::CodeAssemblerTester asm_tester(isolate, kGetCountsParams + 1);
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);

    TNode<IntPtrT> capacity =
        m.ChangeInt32ToIntPtr(m.LoadSwissNameDictionaryCapacity(table));
    TNode<IntPtrT> elements =
        m.LoadSwissNameDictionaryNumberOfElements(table, capacity);
    TNode<IntPtrT> deleted =
        m.LoadSwissNameDictionaryNumberOfDeletedElements(table, capacity);

    TNode<FixedArray> results = m.AllocateZeroedFixedArray(m.IntPtrConstant(3));

    auto check_and_add = [&](TNode<IntPtrT> value, int array_index) {
      CSA_ASSERT(&m, m.IntPtrLessThanOrEqual(m.IntPtrConstant(0), value));
      CSA_ASSERT(
          &m, m.IntPtrLessThanOrEqual(value, m.IntPtrConstant(Smi::kMaxValue)));
      TNode<Smi> smi = m.SmiFromIntPtr(value);
      m.StoreFixedArrayElement(results, array_index, smi);
    };

    check_and_add(capacity, 0);
    check_and_add(elements, 1);
    check_and_add(deleted, 2);

    m.Return(results);
  }
  // FIXME: Or just GenerateCode?
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_copy(Isolate* isolate) {
  STATIC_ASSERT(kCopyParams == 1);  // (table)
  compiler::CodeAssemblerTester asm_tester(isolate, kCopyParams + 1);
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);

    m.Return(m.CopySwissNameDictionary(table));
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

void CSATestRunner::CheckAgainstReference() {
  CHECK(table->EqualsForTesting(*reference_));
}

// The non-SIMD SwissNameDictionary implementation requires 64 bit integer
// operations, which CSA/Torque don't offer on 32 bit platforms. Therefore, we
// cannot run the CSA version of the tests on 32 bit platforms. The only
// exception is IA32, where we can use SSE and don't need 64 bit integers.
// TODO(v8:11330) The Torque SIMD implementation is not specific to SSE (like
// the C++ one), but works on other platforms. It should be possible to create a
// workaround where on 32 bit, non-IA32 platforms we use the "portable", non-SSE
// implementation on the C++ side (which uses a group size of 8) and create a
// special version of the SIMD Torque implementation that works for group size 8
// instead of 16.
#if defined(V8_TARGET_ARCH_64_BIT) || defined(V8_TARGET_ARCH_IA32)
// Executes the tests defined in test-swiss-name-dictionary-shared-tests.h as if
// they were defined in this file, using the CSATestRunner. See comments in
// test-swiss-name-dictionary-shared-tests.h and in
// swiss-name-dictionary-infra.h for details.
const char kCSATestFileName[] = __FILE__;
SharedSwissTableTests<CSATestRunner, kCSATestFileName> execute_shared_tests_csa;
#endif

}  // namespace test_swiss_hash_table
}  // namespace internal
}  // namespace v8
