// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_HEAP_BROKER_H_
#define V8_COMPILER_JS_HEAP_BROKER_H_

#include "src/base/compiler-specific.h"
#include "src/base/optional.h"
#include "src/common/globals.h"
#include "src/compiler/access-info.h"
#include "src/compiler/feedback-source.h"
#include "src/compiler/globals.h"
#include "src/compiler/processed-feedback.h"
#include "src/compiler/refs-map.h"
#include "src/compiler/serializer-hints.h"
#include "src/execution/local-isolate.h"
#include "src/handles/handles.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/local-heap.h"
#include "src/heap/parked-scope.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/objects/code-kind.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/function-kind.h"
#include "src/objects/objects.h"
#include "src/utils/address-map.h"
#include "src/utils/identity-map.h"
#include "src/utils/ostreams.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

class Node;
class ObjectRef;

std::ostream& operator<<(std::ostream& os, const ObjectRef& ref);

#define TRACE_BROKER(broker, x)                                      \
  do {                                                               \
    if (broker->tracing_enabled() && FLAG_trace_heap_broker_verbose) \
      StdoutStream{} << broker->Trace() << x << '\n';                \
  } while (false)

#define TRACE_BROKER_MEMORY(broker, x)                              \
  do {                                                              \
    if (broker->tracing_enabled() && FLAG_trace_heap_broker_memory) \
      StdoutStream{} << broker->Trace() << x << std::endl;          \
  } while (false)

#define TRACE_BROKER_MISSING(broker, x)                                        \
  do {                                                                         \
    if (broker->tracing_enabled())                                             \
      StdoutStream{} << broker->Trace() << "Missing " << x << " (" << __FILE__ \
                     << ":" << __LINE__ << ")" << std::endl;                   \
  } while (false)

struct PropertyAccessTarget {
  MapRef map;
  NameRef name;
  AccessMode mode;

  struct Hash {
    size_t operator()(const PropertyAccessTarget& pair) const {
      return base::hash_combine(
          base::hash_combine(pair.map.object().address(),
                             pair.name.object().address()),
          static_cast<int>(pair.mode));
    }
  };
  struct Equal {
    bool operator()(const PropertyAccessTarget& lhs,
                    const PropertyAccessTarget& rhs) const {
      return lhs.map.equals(rhs.map) && lhs.name.equals(rhs.name) &&
             lhs.mode == rhs.mode;
    }
  };
};

class TFTask {
 private:
  enum Kind {
    // Dummy kinds.
    kNone,
    kSome,
    // Real ops.
    kJSObjectGetOwnConstantElement,
    kGetPropertyAccessInfo,
  };

 public:
  TFTask() {}

  static TFTask None(Node* node) {
    return {node, kNone, {}, 0, {}, nullptr, nullptr};
  }
  static TFTask Some(Node* node, ObjectRef o) {
    return {node, kSome, o, 0, {}, nullptr, nullptr};
  }
  static TFTask JSObjectGetOwnConstantElement(Node* node, JSObjectRef o,
                                              uint32_t index) {
    return {node,   kJSObjectGetOwnConstantElement, o, index, {}, nullptr,
            nullptr};
  }
  static TFTask GetPropertyAccessInfo(Node* node, MapRef map, NameRef name,
                                      AccessMode access_mode,
                                      CompilationDependencies* dependencies,
                                      JSHeapBroker* broker) {
    return {
        node, kGetPropertyAccessInfo, map,   static_cast<uint32_t>(access_mode),
        name, dependencies,           broker};
  }

  Node* node() const { return node_; }

  void ProcessOnMainThread(Isolate* isolate);
  void PostprocessOnBackgroundThread(JSHeapBroker* broker);

 private:
  TFTask(Node* node, Kind kind, base::Optional<ObjectRef> data0, uint32_t data1,
         base::Optional<ObjectRef> data2, CompilationDependencies* data3,
         JSHeapBroker* broker)
      : node_(node),
        kind_(kind),
        data0_(data0),
        data1_(data1),
        data2_(data2),
        data3_(data3),
        broker_(broker) {}

  Node* node_ = nullptr;
  Kind kind_ = kNone;
  base::Optional<ObjectRef> data0_;
  uint32_t data1_ = 0;
  base::Optional<ObjectRef> data2_;
  CompilationDependencies* data3_ = nullptr;
  Handle<Object> result0_;
  base::Optional<PropertyAccessInfo> result1_;
  // For the mt_zone and isolate.
  JSHeapBroker* broker_ = nullptr;
};

template <class Task, int kLength>
class CircularTaskQueue {
 public:
  CircularTaskQueue()
      : first_processed_task_(0), first_pending_task_(0), end_(0) {
    DCHECK(base::bits::IsPowerOfTwo(kLength));
  }

  // BT-only.
  bool TryPush(const Task& task) {
    const int last_end = end_.load(std::memory_order_relaxed);
    const int next_end = Index(last_end + 1);
    DCHECK_LT(last_end, kLength);
    DCHECK_LT(next_end, kLength);
    if (next_end == first_processed_task_.load(std::memory_order_relaxed)) {
      return false;  // Full.
    }
    tasks_[last_end] = task;
    end_.store(next_end, std::memory_order_release);
    return true;
  }

  // BT-only.
  bool IsEmpty() {
    return first_processed_task_.load(std::memory_order_relaxed) ==
           end_.load(std::memory_order_relaxed);
  }

  // MT-only.
  Task* TryGetNextPendingTask() {
    const int first_pending_task =
        first_pending_task_.load(std::memory_order_relaxed);
    if (first_pending_task == end_.load(std::memory_order_acquire)) {
      return nullptr;  // No pending tasks.
    }
    DCHECK_NE(first_pending_task_, end_);
    return &tasks_[first_pending_task_];
  }

  // MT-only.
  void MarkNextPendingTaskAsProcessed(const Task* task) {
    DCHECK_EQ(task, TryGetNextPendingTask());
    DCHECK_NOT_NULL(task);
    USE(task);
    first_pending_task_.store(Index(first_pending_task_ + 1),
                              std::memory_order_release);
  }

  // BT-only.
  bool HasNextProcessedTask() {
    return first_pending_task_.load(std::memory_order_relaxed) !=
           first_processed_task_.load(std::memory_order_relaxed);
  }

  // BT-only.
  Task* TryPopNextProcessedTask() {
    const int first_pending_task =
        first_pending_task_.load(std::memory_order_acquire);
    const int first_processed_task =
        first_processed_task_.load(std::memory_order_relaxed);
    if (first_processed_task == first_pending_task) {
      return nullptr;  // No processed tasks.
    }
    DCHECK_NE(first_processed_task_, end_);
    Task* result = &tasks_[first_processed_task];
    first_processed_task_.store(Index(first_processed_task + 1),
                                std::memory_order_relaxed);
    return result;
  }

 private:
  static constexpr int Index(int i) { return i % kLength; }

  Task tasks_[kLength];

  // All indices are modulo kLength. Conceptually:
  //
  //  first_processed_task_ <= first_pending_task_ <= end_
  //  end_ < first_processed_task_
  //
  // end_ is only mutated by the owning background thread (BT), when pushing a
  // new task. first_pending_task_ is only mutated by the main thread (MT), when
  // marking a task as processed. first_processed_task_ is only mutated by the
  // BT, when popping a processed task.
  std::atomic<int> first_processed_task_;
  std::atomic<int> first_pending_task_;
  std::atomic<int> end_;
};

// Avg tasks per compilation: 20 on WTB.
using BrokerTaskQueue = CircularTaskQueue<TFTask, 64>;

class V8_EXPORT_PRIVATE JSHeapBroker {
 public:
  JSHeapBroker(Isolate* isolate, Zone* broker_zone, bool tracing_enabled,
               bool is_concurrent_inlining, CodeKind code_kind,
               Zone* mt_broker_zone = nullptr);

  // For use only in tests, sets default values for some arguments. Avoids
  // churn when new flags are added.
  JSHeapBroker(Isolate* isolate, Zone* broker_zone)
      : JSHeapBroker(isolate, broker_zone, FLAG_trace_heap_broker, false,
                     CodeKind::TURBOFAN) {}

  ~JSHeapBroker();

  void ClearCachedPropertyAccessInfosAfterSerialization();

  BrokerTaskQueue broker_task_queue_;

  Zone* mt_zone() { return mt_broker_zone_; }
  ZoneVector<TFTask> broker_task_queue_backlog_;

  void PushTask(const TFTask& task) {
    if (broker_task_queue_.TryPush(task)) return;
    broker_task_queue_backlog_.push_back(task);
  }
  bool TaskQueueIsEmpty() { return broker_task_queue_.IsEmpty(); }
  TFTask* TryPopNextProcessedTask() {
    return broker_task_queue_.TryPopNextProcessedTask();
  }
  BrokerTaskQueue* broker_task_queue_ptr() { return &broker_task_queue_; }

  // The compilation target's native context. We need the setter because at
  // broker construction time we don't yet have the canonical handle.
  NativeContextRef target_native_context() const {
    return target_native_context_.value();
  }
  void SetTargetNativeContextRef(Handle<NativeContext> native_context);

  void InitializeAndStartSerializing(Handle<NativeContext> native_context);

  Isolate* isolate() const { return isolate_; }
  Zone* zone() const { return zone_; }
  bool tracing_enabled() const { return tracing_enabled_; }
  bool is_concurrent_inlining() const { return is_concurrent_inlining_; }
  bool is_isolate_bootstrapping() const { return is_isolate_bootstrapping_; }
  bool is_native_context_independent() const {
    return code_kind_ == CodeKind::NATIVE_CONTEXT_INDEPENDENT;
  }
  bool generate_full_feedback_collection() const {
    // NCI code currently collects full feedback.
    DCHECK_IMPLIES(is_native_context_independent(),
                   CollectFeedbackInGenericLowering());
    return is_native_context_independent();
  }
  bool is_turboprop() const { return code_kind_ == CodeKind::TURBOPROP; }

  NexusConfig feedback_nexus_config() const {
    // TODO(mvstanton): when the broker gathers feedback on the background
    // thread, this should return a local NexusConfig object which points
    // to the associated LocalHeap.
    return NexusConfig::FromMainThread(isolate());
  }

  enum BrokerMode { kDisabled, kSerializing, kSerialized, kRetired };
  BrokerMode mode() const { return mode_; }

  void StopSerializing();
  void Retire();
  bool SerializingAllowed() const;

  // Remember the local isolate and initialize its local heap with the
  // persistent and canonical handles provided by {info}.
  void AttachLocalIsolate(OptimizedCompilationInfo* info,
                          LocalIsolate* local_isolate);
  // Forget about the local isolate and pass the persistent and canonical
  // handles provided back to {info}. {info} is responsible for disposing of
  // them.
  void DetachLocalIsolate(OptimizedCompilationInfo* info);

  bool StackHasOverflowed() const;

#ifdef DEBUG
  void PrintRefsAnalysis() const;
#endif  // DEBUG

  // Retruns the handle from root index table for read only heap objects.
  Handle<Object> GetRootHandle(Object object);

  // Never returns nullptr.
  ObjectData* GetOrCreateData(
      Handle<Object>,
      ObjectRef::BackgroundSerialization background_serialization =
          ObjectRef::BackgroundSerialization::kDisallowed);
  // Like the previous but wraps argument in handle first (for convenience).
  ObjectData* GetOrCreateData(
      Object, ObjectRef::BackgroundSerialization background_serialization =
                  ObjectRef::BackgroundSerialization::kDisallowed);

  // Gets data only if we have it. However, thin wrappers will be created for
  // smis, read-only objects and never-serialized objects.
  ObjectData* TryGetOrCreateData(
      Handle<Object>, bool crash_on_error = false,
      ObjectRef::BackgroundSerialization background_serialization =
          ObjectRef::BackgroundSerialization::kDisallowed);

  // Check if {object} is any native context's %ArrayPrototype% or
  // %ObjectPrototype%.
  bool IsArrayOrObjectPrototype(const JSObjectRef& object) const;

  bool HasFeedback(FeedbackSource const& source) const;
  void SetFeedback(FeedbackSource const& source,
                   ProcessedFeedback const* feedback);
  ProcessedFeedback const& GetFeedback(FeedbackSource const& source) const;
  FeedbackSlotKind GetFeedbackSlotKind(FeedbackSource const& source) const;

  // TODO(neis): Move these into serializer when we're always in the background.
  ElementAccessFeedback const& ProcessFeedbackMapsForElementAccess(
      MapHandles const& maps, KeyedAccessMode const& keyed_mode,
      FeedbackSlotKind slot_kind);

  // Binary, comparison and for-in hints can be fully expressed via
  // an enum. Insufficient feedback is signaled by <Hint enum>::kNone.
  BinaryOperationHint GetFeedbackForBinaryOperation(
      FeedbackSource const& source);
  CompareOperationHint GetFeedbackForCompareOperation(
      FeedbackSource const& source);
  ForInHint GetFeedbackForForIn(FeedbackSource const& source);

  ProcessedFeedback const& GetFeedbackForCall(FeedbackSource const& source);
  ProcessedFeedback const& GetFeedbackForGlobalAccess(
      FeedbackSource const& source);
  ProcessedFeedback const& GetFeedbackForInstanceOf(
      FeedbackSource const& source);
  ProcessedFeedback const& GetFeedbackForArrayOrObjectLiteral(
      FeedbackSource const& source);
  ProcessedFeedback const& GetFeedbackForRegExpLiteral(
      FeedbackSource const& source);
  ProcessedFeedback const& GetFeedbackForTemplateObject(
      FeedbackSource const& source);
  ProcessedFeedback const& GetFeedbackForPropertyAccess(
      FeedbackSource const& source, AccessMode mode,
      base::Optional<NameRef> static_name);

  ProcessedFeedback const& ProcessFeedbackForBinaryOperation(
      FeedbackSource const& source);
  ProcessedFeedback const& ProcessFeedbackForCall(FeedbackSource const& source);
  ProcessedFeedback const& ProcessFeedbackForCompareOperation(
      FeedbackSource const& source);
  ProcessedFeedback const& ProcessFeedbackForForIn(
      FeedbackSource const& source);
  ProcessedFeedback const& ProcessFeedbackForGlobalAccess(
      FeedbackSource const& source);
  ProcessedFeedback const& ProcessFeedbackForInstanceOf(
      FeedbackSource const& source);
  ProcessedFeedback const& ProcessFeedbackForPropertyAccess(
      FeedbackSource const& source, AccessMode mode,
      base::Optional<NameRef> static_name);
  ProcessedFeedback const& ProcessFeedbackForArrayOrObjectLiteral(
      FeedbackSource const& source);
  ProcessedFeedback const& ProcessFeedbackForRegExpLiteral(
      FeedbackSource const& source);
  ProcessedFeedback const& ProcessFeedbackForTemplateObject(
      FeedbackSource const& source);

  bool FeedbackIsInsufficient(FeedbackSource const& source) const;

  base::Optional<NameRef> GetNameFeedback(FeedbackNexus const& nexus);

  // If {policy} is {kAssumeSerialized} and the broker doesn't know about the
  // combination of {map}, {name}, and {access_mode}, returns Invalid.
  RefResult<PropertyAccessInfo> GetPropertyAccessInfo(
      MapRef map, NameRef name, AccessMode access_mode,
      CompilationDependencies* dependencies = nullptr,
      SerializationPolicy policy = SerializationPolicy::kAssumeSerialized);

  MinimorphicLoadPropertyAccessInfo GetPropertyAccessInfo(
      MinimorphicLoadPropertyAccessFeedback const& feedback,
      FeedbackSource const& source,
      SerializationPolicy policy = SerializationPolicy::kAssumeSerialized);

  StringRef GetTypedArrayStringTag(ElementsKind kind);

  bool ShouldBeSerializedForCompilation(const SharedFunctionInfoRef& shared,
                                        const FeedbackVectorRef& feedback,
                                        const HintsVector& arguments) const;
  void SetSerializedForCompilation(const SharedFunctionInfoRef& shared,
                                   const FeedbackVectorRef& feedback,
                                   const HintsVector& arguments);
  bool IsSerializedForCompilation(const SharedFunctionInfoRef& shared,
                                  const FeedbackVectorRef& feedback) const;

  LocalIsolate* local_isolate() const { return local_isolate_; }

  // Return the corresponding canonical persistent handle for {object}. Create
  // one if it does not exist.
  // If we have the canonical map, we can create the canonical & persistent
  // handle through it. This commonly happens during the Execute phase.
  // If we don't, that means we are calling this method from serialization. If
  // that happens, we should be inside a canonical and a persistent handle
  // scope. Then, we would just use the regular handle creation.
  template <typename T>
  Handle<T> CanonicalPersistentHandle(T object) {
    if (canonical_handles_) {
      Address address = object.ptr();
      if (Internals::HasHeapObjectTag(address)) {
        RootIndex root_index;
        if (root_index_map_.Lookup(address, &root_index)) {
          return Handle<T>(isolate_->root_handle(root_index).location());
        }
      }

      Object obj(address);
      auto find_result = canonical_handles_->FindOrInsert(obj);
      if (!find_result.already_exists) {
        // Allocate new PersistentHandle if one wasn't created before.
        DCHECK_NOT_NULL(local_isolate());
        *find_result.entry =
            local_isolate()->heap()->NewPersistentHandle(obj).location();
      }
      return Handle<T>(*find_result.entry);
    } else {
      return Handle<T>(object, isolate());
    }
  }

  template <typename T>
  Handle<T> CanonicalPersistentHandle(Handle<T> object) {
    return CanonicalPersistentHandle<T>(*object);
  }

  // Find the corresponding handle in the CanonicalHandlesMap. The entry must be
  // found.
  template <typename T>
  Handle<T> FindCanonicalPersistentHandleForTesting(Object object) {
    Address** entry = canonical_handles_->Find(object);
    return Handle<T>(*entry);
  }

  // Set the persistent handles and copy the canonical handles over to the
  // JSHeapBroker.
  void SetPersistentAndCopyCanonicalHandlesForTesting(
      std::unique_ptr<PersistentHandles> persistent_handles,
      std::unique_ptr<CanonicalHandlesMap> canonical_handles);
  std::string Trace() const;
  void IncrementTracingIndentation();
  void DecrementTracingIndentation();

  RootIndexMap const& root_index_map() { return root_index_map_; }

  // TODO.
  bool IsMainThread() const {
    return local_isolate() == nullptr || local_isolate()->is_main_thread();
  }

 private:
  friend class HeapObjectRef;
  friend class ObjectRef;
  friend class ObjectData;
  friend class PropertyCellData;

  // If this returns false, the object is guaranteed to be fully initialized and
  // thus safe to read from a memory safety perspective. The converse does not
  // necessarily hold.
  bool ObjectMayBeUninitialized(Handle<Object> object) const;

  bool CanUseFeedback(const FeedbackNexus& nexus) const;
  const ProcessedFeedback& NewInsufficientFeedback(FeedbackSlotKind kind) const;

  // Bottleneck FeedbackNexus access here, for storage in the broker
  // or on-the-fly usage elsewhere in the compiler.
  ProcessedFeedback const& ReadFeedbackForArrayOrObjectLiteral(
      FeedbackSource const& source);
  ProcessedFeedback const& ReadFeedbackForBinaryOperation(
      FeedbackSource const& source) const;
  ProcessedFeedback const& ReadFeedbackForCall(FeedbackSource const& source);
  ProcessedFeedback const& ReadFeedbackForCompareOperation(
      FeedbackSource const& source) const;
  ProcessedFeedback const& ReadFeedbackForForIn(
      FeedbackSource const& source) const;
  ProcessedFeedback const& ReadFeedbackForGlobalAccess(
      FeedbackSource const& source);
  ProcessedFeedback const& ReadFeedbackForInstanceOf(
      FeedbackSource const& source);
  ProcessedFeedback const& ReadFeedbackForPropertyAccess(
      FeedbackSource const& source, AccessMode mode,
      base::Optional<NameRef> static_name);
  ProcessedFeedback const& ReadFeedbackForRegExpLiteral(
      FeedbackSource const& source);
  ProcessedFeedback const& ReadFeedbackForTemplateObject(
      FeedbackSource const& source);

  void CollectArrayAndObjectPrototypes();

  PerIsolateCompilerCache* compiler_cache() const { return compiler_cache_; }

  void set_persistent_handles(
      std::unique_ptr<PersistentHandles> persistent_handles) {
    DCHECK_NULL(ph_);
    ph_ = std::move(persistent_handles);
    DCHECK_NOT_NULL(ph_);
  }
  std::unique_ptr<PersistentHandles> DetachPersistentHandles() {
    DCHECK_NOT_NULL(ph_);
    return std::move(ph_);
  }

  void set_canonical_handles(
      std::unique_ptr<CanonicalHandlesMap> canonical_handles) {
    DCHECK_NULL(canonical_handles_);
    canonical_handles_ = std::move(canonical_handles);
    DCHECK_NOT_NULL(canonical_handles_);
  }

  std::unique_ptr<CanonicalHandlesMap> DetachCanonicalHandles() {
    DCHECK_NOT_NULL(canonical_handles_);
    return std::move(canonical_handles_);
  }

  // Copy the canonical handles over to the JSHeapBroker.
  void CopyCanonicalHandlesForTesting(
      std::unique_ptr<CanonicalHandlesMap> canonical_handles);

  Isolate* const isolate_;
  Zone* const zone_ = nullptr;
  Zone* const mt_broker_zone_ = nullptr;
  base::Optional<NativeContextRef> target_native_context_;
  RefsMap* refs_;
  RootIndexMap root_index_map_;
  ZoneUnorderedSet<Handle<JSObject>, Handle<JSObject>::hash,
                   Handle<JSObject>::equal_to>
      array_and_object_prototypes_;
  BrokerMode mode_ = kDisabled;
  bool const tracing_enabled_;
  bool const is_concurrent_inlining_;
  bool const is_isolate_bootstrapping_;
  CodeKind const code_kind_;
  std::unique_ptr<PersistentHandles> ph_;
  LocalIsolate* local_isolate_ = nullptr;
  std::unique_ptr<CanonicalHandlesMap> canonical_handles_;
  unsigned trace_indentation_ = 0;
  PerIsolateCompilerCache* compiler_cache_ = nullptr;
  ZoneUnorderedMap<FeedbackSource, ProcessedFeedback const*,
                   FeedbackSource::Hash, FeedbackSource::Equal>
      feedback_;
  ZoneUnorderedMap<PropertyAccessTarget, PropertyAccessInfo,
                   PropertyAccessTarget::Hash, PropertyAccessTarget::Equal>
      property_access_infos_;
  friend class TFTask;
  ZoneUnorderedMap<FeedbackSource, MinimorphicLoadPropertyAccessInfo,
                   FeedbackSource::Hash, FeedbackSource::Equal>
      minimorphic_property_access_infos_;

  ZoneVector<ObjectData*> typed_array_string_tags_;

  struct SerializedFunction {
    SharedFunctionInfoRef shared;
    FeedbackVectorRef feedback;

    bool operator<(const SerializedFunction& other) const {
      if (shared.object().address() < other.shared.object().address()) {
        return true;
      }
      if (shared.object().address() == other.shared.object().address()) {
        return feedback.object().address() < other.feedback.object().address();
      }
      return false;
    }
  };
  ZoneMultimap<SerializedFunction, HintsVector> serialized_functions_;

  static const size_t kMaxSerializedFunctionsCacheSize = 200;
  static const uint32_t kMinimalRefsBucketCount = 8;     // must be power of 2
  static const uint32_t kInitialRefsBucketCount = 1024;  // must be power of 2
};

class V8_NODISCARD TraceScope {
 public:
  TraceScope(JSHeapBroker* broker, const char* label)
      : TraceScope(broker, static_cast<void*>(broker), label) {}

  TraceScope(JSHeapBroker* broker, ObjectData* data, const char* label)
      : TraceScope(broker, static_cast<void*>(data), label) {}

  TraceScope(JSHeapBroker* broker, void* subject, const char* label)
      : broker_(broker) {
    TRACE_BROKER(broker_, "Running " << label << " on " << subject);
    broker_->IncrementTracingIndentation();
  }

  ~TraceScope() { broker_->DecrementTracingIndentation(); }

 private:
  JSHeapBroker* const broker_;
};

#define ASSIGN_RETURN_NO_CHANGE_IF_DATA_MISSING(something_var,             \
                                                optionally_something)      \
  auto optionally_something_ = optionally_something;                       \
  if (!optionally_something_)                                              \
    return NoChangeBecauseOfMissingData(broker(), __FUNCTION__, __LINE__); \
  something_var = *optionally_something_;

class Reduction;
Reduction NoChangeBecauseOfMissingData(JSHeapBroker* broker,
                                       const char* function, int line);

// Miscellaneous definitions that should be moved elsewhere once concurrent
// compilation is finished.
bool CanInlineElementAccess(MapRef const& map);

// Scope that unparks the LocalHeap, if:
//   a) We have a JSHeapBroker,
//   b) Said JSHeapBroker has a LocalIsolate and thus a LocalHeap,
//   c) Said LocalHeap has been parked and
//   d) The given condition evaluates to true.
// Used, for example, when printing the graph with --trace-turbo with a
// previously parked LocalHeap.
class V8_NODISCARD UnparkedScopeIfNeeded {
 public:
  explicit UnparkedScopeIfNeeded(JSHeapBroker* broker,
                                 bool extra_condition = true) {
    if (broker != nullptr && extra_condition) {
      LocalIsolate* local_isolate = broker->local_isolate();
      if (local_isolate != nullptr && local_isolate->heap()->IsParked()) {
        unparked_scope.emplace(local_isolate->heap());
      }
    }
  }

 private:
  base::Optional<UnparkedScope> unparked_scope;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_HEAP_BROKER_H_
