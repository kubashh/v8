// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/test-utils.h"

namespace v8 {
namespace {

using DirectHandlesTest = TestWithIsolate;

TEST_F(DirectHandlesTest, CreateDirectHandleFromLocal) {
  HandleScope scope(isolate());
  Local<String> foo = String::NewFromUtf8Literal(isolate(), "foo");

  i::DirectHandle<i::String> direct = Utils::OpenDirectHandle(*foo);
  i::Handle<i::String> handle = Utils::OpenHandle(*foo);

  CHECK_EQ(*direct, *handle);
}

TEST_F(DirectHandlesTest, CreateLocalFromDirectHandle) {
  i::DirectHandle<i::String> direct =
      i_isolate()->factory()->NewStringFromAsciiChecked("foo");
  i::Handle<i::String> handle(*direct, i_isolate());

  Local<String> l1 = Utils::ToLocal(direct, i_isolate());
  Local<String> l2 = Utils::ToLocal(handle);

  CHECK_EQ(l1, l2);
}

}  // namespace
}  // namespace v8
