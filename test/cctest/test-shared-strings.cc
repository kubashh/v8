// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-initialization.h"
#include "src/base/strings.h"
#include "src/heap/factory.h"
#include "src/heap/heap-inl.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"

namespace v8 {
namespace internal {
namespace test_shared_strings {

class MultiClientIsolateTest {
 public:
  MultiClientIsolateTest() {
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(
        v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = allocator.get();
    shared_isolate_ =
        reinterpret_cast<v8::Isolate*>(Isolate::NewShared(create_params));
  }

  ~MultiClientIsolateTest() {
    for (v8::Isolate* client_isolate : client_isolates_) {
      client_isolate->Dispose();
    }
    Isolate::Delete(i_shared_isolate());
  }

  v8::Isolate* shared_isolate() const { return shared_isolate_; }

  Isolate* i_shared_isolate() const {
    return reinterpret_cast<Isolate*>(shared_isolate_);
  }

  const std::vector<v8::Isolate*>& client_isolates() const {
    return client_isolates_;
  }

  v8::Isolate* NewClientIsolate() {
    CHECK_NOT_NULL(shared_isolate_);
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(
        v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = allocator.get();
    create_params.experimental_attach_to_shared_isolate = shared_isolate_;
    v8::Isolate* client = v8::Isolate::New(create_params);
    client_isolates_.push_back(client);
    return client;
  }

 private:
  v8::Isolate* shared_isolate_;
  std::vector<v8::Isolate*> client_isolates_;
};

UNINITIALIZED_TEST(InPlaceInternalizableStringsAreShared) {
  if (!ReadOnlyHeap::IsReadOnlySpaceShared()) return;
  if (!COMPRESS_POINTERS_IN_SHARED_CAGE_BOOL) return;

  FLAG_shared_string_table = true;

  MultiClientIsolateTest test;
  v8::Isolate* isolate1 = test.NewClientIsolate();
  Isolate* i_isolate1 = reinterpret_cast<Isolate*>(isolate1);
  Factory* factory1 = i_isolate1->factory();

  HandleScope handle_scope(i_isolate1);

  const char raw_one_byte[] = "foo";
  base::uc16 raw_two_byte[] = {2001, 2002, 2003};
  base::Vector<const base::uc16> two_byte(raw_two_byte, 3);

  // Old generation 1- and 2-byte seq strings are in-place internalizable.
  Handle<String> old_one_byte_seq =
      factory1->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
  CHECK(old_one_byte_seq->InSharedHeap());
  Handle<String> old_two_byte_seq =
      factory1->NewStringFromTwoByte(two_byte, AllocationType::kOld)
          .ToHandleChecked();
  CHECK(old_two_byte_seq->InSharedHeap());

  // Young generation are not internalizable and not shared when sharing the
  // string table.
  Handle<String> young_one_byte_seq =
      factory1->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kYoung);
  CHECK(!young_one_byte_seq->InSharedHeap());
  Handle<String> young_two_byte_seq =
      factory1->NewStringFromTwoByte(two_byte, AllocationType::kYoung)
          .ToHandleChecked();
  CHECK(!young_two_byte_seq->InSharedHeap());

  // Internalized strings are shared.
  Handle<String> one_byte_intern = factory1->NewOneByteInternalizedString(
      base::OneByteVector(raw_one_byte), 1);
  CHECK(one_byte_intern->InSharedHeap());
  Handle<String> two_byte_intern =
      factory1->NewTwoByteInternalizedString(two_byte, 1);
  CHECK(two_byte_intern->InSharedHeap());
}

UNINITIALIZED_TEST(InPlaceInternalization) {
  if (!ReadOnlyHeap::IsReadOnlySpaceShared()) return;
  if (!COMPRESS_POINTERS_IN_SHARED_CAGE_BOOL) return;

  FLAG_shared_string_table = true;

  MultiClientIsolateTest test;
  v8::Isolate* isolate1 = test.NewClientIsolate();
  v8::Isolate* isolate2 = test.NewClientIsolate();
  Isolate* i_isolate1 = reinterpret_cast<Isolate*>(isolate1);
  Factory* factory1 = i_isolate1->factory();
  Isolate* i_isolate2 = reinterpret_cast<Isolate*>(isolate2);
  Factory* factory2 = i_isolate2->factory();

  HandleScope scope1(i_isolate1);
  HandleScope scope2(i_isolate2);

  const char raw_one_byte[] = "foo";
  base::uc16 raw_two_byte[] = {2001, 2002, 2003};
  base::Vector<const base::uc16> two_byte(raw_two_byte, 3);

  // Allocate two in-place internalizable strings in isolate1 then intern
  // them.
  Handle<String> old_one_byte_seq1 =
      factory1->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
  Handle<String> old_two_byte_seq1 =
      factory1->NewStringFromTwoByte(two_byte, AllocationType::kOld)
          .ToHandleChecked();
  Handle<String> one_byte_intern1 =
      factory1->InternalizeString(old_one_byte_seq1);
  Handle<String> two_byte_intern1 =
      factory1->InternalizeString(old_two_byte_seq1);
  CHECK(old_one_byte_seq1.equals(one_byte_intern1));
  CHECK(old_two_byte_seq1.equals(two_byte_intern1));

  // Allocate two in-place internalizable strings with the same contents in
  // isolate2 then intern them. They should be the same as the interned strings
  // from isolate1.
  Handle<String> old_one_byte_seq2 =
      factory2->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
  Handle<String> old_two_byte_seq2 =
      factory2->NewStringFromTwoByte(two_byte, AllocationType::kOld)
          .ToHandleChecked();
  Handle<String> one_byte_intern2 =
      factory2->InternalizeString(old_one_byte_seq2);
  Handle<String> two_byte_intern2 =
      factory2->InternalizeString(old_two_byte_seq2);
  CHECK(!old_one_byte_seq2.equals(one_byte_intern2));
  CHECK(!old_two_byte_seq2.equals(two_byte_intern2));
  CHECK_NE(*old_one_byte_seq2, *one_byte_intern2);
  CHECK_NE(*old_two_byte_seq2, *two_byte_intern2);
  CHECK_EQ(*one_byte_intern1, *one_byte_intern2);
  CHECK_EQ(*two_byte_intern1, *two_byte_intern2);
}

UNINITIALIZED_TEST(YoungInternalization) {
  if (!ReadOnlyHeap::IsReadOnlySpaceShared()) return;
  if (!COMPRESS_POINTERS_IN_SHARED_CAGE_BOOL) return;

  FLAG_shared_string_table = true;

  MultiClientIsolateTest test;
  v8::Isolate* isolate1 = test.NewClientIsolate();
  v8::Isolate* isolate2 = test.NewClientIsolate();
  Isolate* i_isolate1 = reinterpret_cast<Isolate*>(isolate1);
  Factory* factory1 = i_isolate1->factory();
  Isolate* i_isolate2 = reinterpret_cast<Isolate*>(isolate2);
  Factory* factory2 = i_isolate2->factory();

  HandleScope scope1(i_isolate1);
  HandleScope scope2(i_isolate2);

  const char raw_one_byte[] = "foo";
  base::uc16 raw_two_byte[] = {2001, 2002, 2003};
  base::Vector<const base::uc16> two_byte(raw_two_byte, 3);

  // Allocate two young strings in isolate1 then intern them. Young strings
  // aren't in-place internalizable and are copied when internalized.
  Handle<String> young_one_byte_seq1 =
      factory1->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kYoung);
  Handle<String> young_two_byte_seq1 =
      factory1->NewStringFromTwoByte(two_byte, AllocationType::kYoung)
          .ToHandleChecked();
  Handle<String> one_byte_intern1 =
      factory1->InternalizeString(young_one_byte_seq1);
  Handle<String> two_byte_intern1 =
      factory1->InternalizeString(young_two_byte_seq1);
  CHECK(!young_one_byte_seq1.equals(one_byte_intern1));
  CHECK(!young_two_byte_seq1.equals(two_byte_intern1));
  CHECK_NE(*young_one_byte_seq1, *one_byte_intern1);
  CHECK_NE(*young_two_byte_seq1, *two_byte_intern1);

  // Allocate two young strings with the same contents in isolate2 then intern
  // them. They should be the same as the interned strings from isolate1.
  Handle<String> young_one_byte_seq2 =
      factory2->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
  Handle<String> young_two_byte_seq2 =
      factory2->NewStringFromTwoByte(two_byte, AllocationType::kOld)
          .ToHandleChecked();
  Handle<String> one_byte_intern2 =
      factory2->InternalizeString(young_one_byte_seq2);
  Handle<String> two_byte_intern2 =
      factory2->InternalizeString(young_two_byte_seq2);
  CHECK(!young_one_byte_seq2.equals(one_byte_intern2));
  CHECK(!young_two_byte_seq2.equals(two_byte_intern2));
  CHECK_NE(*young_one_byte_seq2, *one_byte_intern2);
  CHECK_NE(*young_two_byte_seq2, *two_byte_intern2);
  CHECK_EQ(*one_byte_intern1, *one_byte_intern2);
  CHECK_EQ(*two_byte_intern1, *two_byte_intern2);
}

}  // namespace test_shared_strings
}  // namespace internal
}  // namespace v8
