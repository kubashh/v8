// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/shared-isolate-utils.h"

namespace v8 {
namespace internal {

MultiClientIsolateTest::MultiClientIsolateTest() {
  std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(
      v8::ArrayBuffer::Allocator::NewDefaultAllocator());
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = allocator.get();
  shared_isolate_ =
      reinterpret_cast<v8::Isolate*>(Isolate::NewShared(create_params));
}

MultiClientIsolateTest::~MultiClientIsolateTest() {
  for (v8::Isolate* client_isolate : client_isolates_) {
    client_isolate->Dispose();
  }
  Isolate::Delete(i_shared_isolate());
}

v8::Isolate* MultiClientIsolateTest::NewClientIsolate(
    v8::StartupData* custom_blob) {
  CHECK_NOT_NULL(shared_isolate_);
  std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(
      v8::ArrayBuffer::Allocator::NewDefaultAllocator());
  v8::Isolate::CreateParams create_params;
  create_params.snapshot_blob = custom_blob;
  create_params.array_buffer_allocator = allocator.get();
  create_params.experimental_attach_to_shared_isolate = shared_isolate_;
  v8::Isolate* client = v8::Isolate::New(create_params);
  client_isolates_.push_back(client);
  return client;
}

}  // namespace internal
}  // namespace v8
