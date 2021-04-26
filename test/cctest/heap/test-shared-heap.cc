// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8.h"
#include "src/common/globals.h"
#include "src/handles/handles-inl.h"
#include "src/heap/heap.h"
#include "src/objects/heap-object.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

namespace {
v8::Isolate* CreateSharedIsolate() {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator =
      v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);

  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  i_isolate->UseAsSharedIsolate();

  return isolate;
}

v8::Isolate* CreateClientIsolate() {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator =
      v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  return v8::Isolate::New(create_params);
}

const int kNumIterations = 2000;

class SharedSpaceAllocationThread final : public v8::base::Thread {
 public:
  explicit SharedSpaceAllocationThread(Isolate* shared)
      : v8::base::Thread(base::Thread::Options("SharedSpaceAllocationThread")),
        shared_(shared) {}

  void Run() override {
    v8::Isolate* client_isolate = CreateClientIsolate();
    Isolate* i_client_isolate = reinterpret_cast<Isolate*>(client_isolate);
    i_client_isolate->AttachToSharedIsolate(shared_);

    {
      HandleScope scope(i_client_isolate);

      for (int i = 0; i < kNumIterations; i++) {
        i_client_isolate->factory()->NewFixedArray(10,
                                                   AllocationType::kSharedOld);
      }

      CcTest::CollectGarbage(OLD_SPACE, i_client_isolate);

      v8::platform::PumpMessageLoop(i::V8::GetCurrentPlatform(),
                                    client_isolate);
    }

    client_isolate->Dispose();
  }

  Isolate* shared_;
};
}  // namespace

UNINITIALIZED_TEST(EmptySharedHeap) {
  v8::Isolate* shared_isolate = CreateSharedIsolate();
  Isolate* i_shared_isolate = reinterpret_cast<Isolate*>(shared_isolate);

  std::vector<std::unique_ptr<SharedSpaceAllocationThread>> threads;
  const int kThreads = 4;

  for (int i = 0; i < kThreads; i++) {
    auto thread =
        std::make_unique<SharedSpaceAllocationThread>(i_shared_isolate);
    CHECK(thread->Start());
    threads.push_back(std::move(thread));
  }

  for (auto& thread : threads) {
    thread->Join();
  }

  shared_isolate->Dispose();
}

}  // namespace internal
}  // namespace v8
