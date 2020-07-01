// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api.h"
#include "src/base/platform/semaphore.h"
#include "src/handles/handles-inl.h"
#include "src/handles/local-handles-inl.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"

namespace v8 {
namespace internal {

static constexpr int kNumHandles = kHandleBlockSize * 2 + kHandleBlockSize / 2;

namespace {

class PersistentHandlesThread final : public v8::base::Thread {
 public:
  PersistentHandlesThread(Heap* heap, std::vector<Handle<JSObject>> handles,
                          std::unique_ptr<PersistentHandles> ph,
                          Handle<Name> name, base::Semaphore* sema_started)
      : v8::base::Thread(base::Thread::Options("ThreadWithLocalHeap")),
        heap_(heap),
        handles_(std::move(handles)),
        ph_(std::move(ph)),
        name_(name),
        sema_started_(sema_started) {}

  void Run() override {
    LocalHeap local_heap(heap_, std::move(ph_));
    LocalHandleScope scope(&local_heap);
    Address object = handles_[0]->ptr();

    for (int i = 0; i < kNumHandles; i++) {
      handles_.push_back(
          Handle<JSObject>::cast(local_heap.NewPersistentHandle(object)));
    }

    sema_started_->Signal();

    for (Handle<JSObject> handle : handles_) {
      // Lookup the named property on the {map}.
      CHECK(name_->IsUniqueName());
      Handle<Map> map(handle->map(), &local_heap);

      Handle<DescriptorArray> descriptors(
          map->synchronized_instance_descriptors(), &local_heap);
      bool is_background_thread = true;
      InternalIndex const number =
          descriptors->Search(*name_, *map, is_background_thread);
      CHECK(number.is_found());
    }

    CHECK_EQ(handles_.size(), kNumHandles * 2);

    CHECK(!ph_);
    ph_ = local_heap.DetachPersistentHandles();
  }

  Heap* heap_;
  std::vector<Handle<JSObject>> handles_;
  std::unique_ptr<PersistentHandles> ph_;
  Handle<Name> name_;
  base::Semaphore* sema_started_;
};

// Uses linear search on a flat object, with up to 8 elements.
TEST(CheckICStates) {
  CcTest::InitializeVM();
  FLAG_local_heaps = true;
  FLAG_lazy_feedback_allocation = false;
  Isolate* isolate = CcTest::i_isolate();

  std::unique_ptr<PersistentHandles> ph = isolate->NewPersistentHandles();
  std::vector<Handle<JSObject>> handles;

  // auto factory = isolate->factory();
  HandleScope handle_scope(isolate);

  Handle<HeapObject> o1 = Handle<HeapObject>::cast(
      Utils::OpenHandle(*CompileRun("o1 = { bar: {} };")));
  Handle<HeapObject> o2 = Handle<HeapObject>::cast(
      Utils::OpenHandle(*CompileRun("o2 = { baz: 3, bar: 3 };")));
  Handle<HeapObject> o3 = Handle<HeapObject>::cast(
      Utils::OpenHandle(*CompileRun("o3 = { blu: 3, baz: 3, bar: 3 };")));
  Handle<HeapObject> o4 = Handle<HeapObject>::cast(Utils::OpenHandle(
      *CompileRun("o4 = { ble: 3, blu: 3, baz: 3, bar: 3 };")));
  // Handle<HeapObject> o5 =
  // Handle<HeapObject>::cast(Utils::OpenHandle(*CompileRun(
  //  "o5 = { karst: 3, bar: {} };")));
  auto result = CompileRun(
      "function foo(o) {"
      "  let a = o.bar;"
      "  return a;"
      "}"
      "foo(o1);"
      "foo;");
  Handle<JSFunction> function =
      Handle<JSFunction>::cast(Utils::OpenHandle(*result));
  Handle<FeedbackVector> vector(function->feedback_vector(), isolate);
  FeedbackSlot slot(0);
  FeedbackNexus nexus(vector, slot);
  CHECK(IsLoadICKind(nexus.kind()));
  CHECK_EQ(MONOMORPHIC, nexus.ic_state());
  nexus.ConfigureUninitialized();

  // Cycle the IC through all states repeatedly.
  MaybeObjectHandle dummy_handler(Smi::FromInt(10), isolate);
  const int kCycles = 5;
  for (int i = 0; i < kCycles; i++) {
    CHECK_EQ(UNINITIALIZED, nexus.ic_state());
    nexus.ConfigureMonomorphic(Handle<Name>(), Handle<Map>(o1->map(), isolate),
                               dummy_handler);
    CHECK_EQ(MONOMORPHIC, nexus.ic_state());
    base::OS::Sleep(base::TimeDelta::FromMilliseconds(50));

    // Go polymorphic.
    std::vector<MapAndHandler> map_and_handlers;
    map_and_handlers.push_back(
        MapAndHandler(Handle<Map>(o1->map(), isolate), dummy_handler));
    map_and_handlers.push_back(
        MapAndHandler(Handle<Map>(o2->map(), isolate), dummy_handler));
    map_and_handlers.push_back(
        MapAndHandler(Handle<Map>(o3->map(), isolate), dummy_handler));
    map_and_handlers.push_back(
        MapAndHandler(Handle<Map>(o4->map(), isolate), dummy_handler));
    nexus.ConfigurePolymorphic(Handle<Name>(), map_and_handlers);
    CHECK_EQ(POLYMORPHIC, nexus.ic_state());
    base::OS::Sleep(base::TimeDelta::FromMilliseconds(50));

    // Go Megamorphic
    nexus.ConfigureMegamorphic();
    CHECK_EQ(MEGAMORPHIC, nexus.ic_state());
    base::OS::Sleep(base::TimeDelta::FromMilliseconds(50));

    nexus.ConfigureUninitialized();
  }

  /*
    Address object = js_object->ptr();
    for (int i = 0; i < kNumHandles; i++) {
      handles.push_back(Handle<JSObject>::cast(ph->NewHandle(object)));
    }

    Handle<Name> persistent_name =
    Handle<Name>::cast(ph->NewHandle(name->ptr()));

    base::Semaphore sema_started(0);

    // Pass persistent handles to background thread.
    std::unique_ptr<PersistentHandlesThread> thread(new PersistentHandlesThread(
        isolate->heap(), std::move(handles), std::move(ph), persistent_name,
        &sema_started));
    CHECK(thread->Start());

    sema_started.Wait();

    // Exercise descriptor in main thread too.
    for (int i = 0; i < 7; ++i) {
      Handle<String> filler_name = CcTest::MakeName("filler_property_", i);
      Handle<Object> filler_value = CcTest::MakeString("dummy_value");
      JSObject::DefinePropertyOrElementIgnoreAttributes(js_object, filler_name,
                                                        filler_value, NONE)
          .Check();
    }
    CHECK_EQ(js_object->map().NumberOfOwnDescriptors(), 8);

    thread->Join();
  */
}

}  // anonymous namespace

}  // namespace internal
}  // namespace v8
