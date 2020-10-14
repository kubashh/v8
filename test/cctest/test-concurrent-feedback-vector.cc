// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_set>

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

namespace {

// kCycles is large enough to ensure we see every state we are interested in.
const int kCycles = 1000;
volatile bool all_states_seen = false;
volatile int states_seen = 0;

class FeedbackVectorExplorationThread final : public v8::base::Thread {
 public:
  FeedbackVectorExplorationThread(Heap* heap, base::Semaphore* sema_started,
                                  std::unique_ptr<PersistentHandles> ph,
                                  Handle<JSFunction> function)
      : v8::base::Thread(base::Thread::Options("ThreadWithLocalHeap")),
        heap_(heap),
        function_(function),
        ph_(std::move(ph)),
        sema_started_(sema_started) {}

  bool AllRequiredStatesSeen(
      const std::unordered_set<InlineCacheState>& found) {
    auto end = found.end();
    return (found.find(UNINITIALIZED) != end &&
            found.find(MONOMORPHIC) != end && found.find(POLYMORPHIC) != end &&
            found.find(MEGAMORPHIC) != end);
  }

  void Run() override {
    LocalHeap local_heap(heap_, std::move(ph_));
    LocalHandleScope scope(&local_heap);
    // Get the feedback vector
    NexusConfig nexus_config =
        NexusConfig::FromBackgroundThread(function_->GetIsolate(), &local_heap);
    Handle<FeedbackVector> vector(function_->feedback_vector(), &local_heap);
    FeedbackSlot slot(0);

    sema_started_->Signal();
    std::unordered_set<InlineCacheState> found_states;
    for (int i = 0; i < (200 * kCycles); i++) {
      FeedbackNexus nexus(vector, slot, nexus_config);
      auto state = nexus.ic_state();
      if (state == MONOMORPHIC || state == POLYMORPHIC) {
        MapHandles maps;
        nexus.ExtractMaps(&maps);
        for (unsigned int i = 0; i < maps.size(); i++) {
          CHECK(maps[i]->IsMap());
        }
      }

      if (found_states.find(state) == found_states.end()) {
        found_states.insert(state);
        states_seen++;
        if (AllRequiredStatesSeen(found_states)) {
          // We are finished.
          break;
        }
      }
    }

    CHECK(AllRequiredStatesSeen(found_states));
    all_states_seen = true;
    CHECK(!ph_);
    ph_ = local_heap.DetachPersistentHandles();
  }

  Heap* heap_;
  Handle<JSFunction> function_;
  std::unique_ptr<PersistentHandles> ph_;
  base::Semaphore* sema_started_;
};

// Verify that a LoadIC can be cycled through different states and safely
// read on a background thread.
TEST(CheckLoadICStates) {
  CcTest::InitializeVM();
  FLAG_local_heaps = true;
  FLAG_lazy_feedback_allocation = false;
  Isolate* isolate = CcTest::i_isolate();

  std::unique_ptr<PersistentHandles> ph = isolate->NewPersistentHandles();
  HandleScope handle_scope(isolate);

  Handle<HeapObject> o1 = Handle<HeapObject>::cast(
      Utils::OpenHandle(*CompileRun("o1 = { bar: {} };")));
  Handle<HeapObject> o2 = Handle<HeapObject>::cast(
      Utils::OpenHandle(*CompileRun("o2 = { baz: 3, bar: 3 };")));
  Handle<HeapObject> o3 = Handle<HeapObject>::cast(
      Utils::OpenHandle(*CompileRun("o3 = { blu: 3, baz: 3, bar: 3 };")));
  Handle<HeapObject> o4 = Handle<HeapObject>::cast(Utils::OpenHandle(
      *CompileRun("o4 = { ble: 3, blu: 3, baz: 3, bar: 3 };")));
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

  // Now the basic environment is set up. Start the worker thread.
  base::Semaphore sema_started(0);
  Handle<JSFunction> persistent_function =
      Handle<JSFunction>::cast(ph->NewHandle(function));
  std::unique_ptr<FeedbackVectorExplorationThread> thread(
      new FeedbackVectorExplorationThread(isolate->heap(), &sema_started,
                                          std::move(ph), persistent_function));
  CHECK(thread->Start());
  sema_started.Wait();

  // Cycle the IC through all states repeatedly.

  // {dummy_handler} is just an arbitrary value to associate with a map in order
  // to fill in the feedback vector slots in a minimally acceptable way.
  MaybeObjectHandle dummy_handler(Smi::FromInt(10), isolate);
  for (int i = 0; i < (200 * kCycles); i++) {
    CHECK_EQ(UNINITIALIZED, nexus.ic_state());
    nexus.ConfigureMonomorphic(Handle<Name>(), Handle<Map>(o1->map(), isolate),
                               dummy_handler);
    CHECK_EQ(MONOMORPHIC, nexus.ic_state());

    if (all_states_seen) break;

    if (i > kCycles) {
      // To slow down the rate of change on the main thread and give the
      // background thread an opportunity to see all states.
      base::OS::Sleep(base::TimeDelta::FromMilliseconds(50));
    }

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

    // Go Megamorphic
    nexus.ConfigureMegamorphic();
    CHECK_EQ(MEGAMORPHIC, nexus.ic_state());

    nexus.ConfigureUninitialized();
  }

  if (!all_states_seen) {
    PrintF("states_seen = %d\n", states_seen);
    CHECK(false);
  }

  thread->Join();
}

}  // anonymous namespace

}  // namespace internal
}  // namespace v8
