// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api-inl.h"
#include "test/cctest/cctest.h"
#include "tools/debug_helper/debug-helper.h"

namespace v8 {
namespace internal {

namespace {

namespace d = v8::debug_helper;

// Implement the memory-reading callback. This one just fetches memory from the
// current process, but a real implementation for a debugging extension would
// fetch memory from the debuggee process or crash dump.
d::MemoryAccessResult ReadMemory(uintptr_t address, uint8_t* destination,
                                 size_t byte_count) {
  memcpy(destination, reinterpret_cast<void*>(address), byte_count);
  return d::MemoryAccessResult::kOk;
}

// Another memory-reading callback that simulates having no accessible memory in
// the dump.
d::MemoryAccessResult ReadMemoryFail(uintptr_t address, uint8_t* destination,
                                     size_t byte_count) {
  return d::MemoryAccessResult::kAddressValidButInaccessible;
}

void CheckProp(const d::ObjectProperty& property, const char* expected_type,
               const char* expected_name) {
  CHECK_EQ(property.num_values, 1);
  CHECK(property.values[0].validity == d::MemoryAccessResult::kOk);
  CHECK(property.type == std::string(expected_type));
  CHECK(property.kind == d::PropertyKind::kSingle);
  CHECK(property.name == std::string(expected_name));
}

void CheckProp(const d::ObjectProperty& property, const char* expected_type,
               const char* expected_name, const uint64_t expected_value) {
  CheckProp(property, expected_type, expected_name);
  CHECK(property.values[0].value == expected_value);
}

}  // namespace

TEST(GetObjectProperties) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());
  LocalContext context;
  d::Roots roots{0, 0, 0, 0};  // We don't know the heap roots.

  v8::Local<v8::Value> v = CompileRun("42");
  Handle<Object> o = v8::Utils::OpenHandle(*v);
  d::ObjectPropertiesResultPtr props =
      d::GetObjectProperties(o->ptr(), &ReadMemory, roots);
  CHECK(props->type_check_result == d::TypeCheckResult::kSmi);
  CHECK(props->brief == std::string("42"));
  CHECK(props->type == std::string("v8::internal::Smi"));
  CHECK_EQ(props->num_properties, 1);
  CheckProp(*props->properties[0], "int", "(untagged value)", 42);

  v = CompileRun("[\"a\", \"b\"]");
  o = v8::Utils::OpenHandle(*v);
  props = d::GetObjectProperties(o->ptr(), &ReadMemory, roots);
  CHECK(props->type_check_result == d::TypeCheckResult::kUsedMap);
  CHECK(props->type == std::string("v8::internal::JsArray"));
  CHECK_EQ(props->num_properties, 4);
  CheckProp(*props->properties[0], "v8::internal::Map", "map");
  CheckProp(*props->properties[1], "v8::internal::Object",
            "properties_or_hash");
  CheckProp(*props->properties[2], "v8::internal::FixedArrayBase", "elements");
  CheckProp(*props->properties[3], "v8::internal::Object", "length",
            IntToSmi(2));

  // The properties_or_hash_code field should be an empty fixed array. Since
  // that is at a known offset, we should be able to detect it even without
  // any ability to read memory.
  props = d::GetObjectProperties(
      static_cast<uintptr_t>(props->properties[1]->values[0].value),
      &ReadMemoryFail, roots);
  CHECK(props->type_check_result ==
        d::TypeCheckResult::kObjectPointerValidButInaccessible);
  CHECK(props->type == std::string("v8::internal::Object"));
  CHECK_EQ(props->num_properties, 0);
  CHECK(props->brief == std::string("maybe EmptyFixedArray"));
}

}  // namespace internal
}  // namespace v8
