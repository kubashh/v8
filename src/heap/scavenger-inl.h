// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_SCAVENGER_INL_H_
#define V8_HEAP_SCAVENGER_INL_H_

#include "src/heap/scavenger.h"

namespace v8 {
namespace internal {

class ScavengingVisitor final : public HeapVisitor<int, ScavengingVisitor> {
 public:
  typedef HeapVisitor<int, ScavengingVisitor> Parent;

  void VisitPointers(HeapObject* host, Object** start, Object** end) final {
    // The visitor uses the dispatch mechanism to invoke the proper evacuation
    // logic and never reaches the dispatch for individual pointers.
    UNREACHABLE();
  }

  explicit ScavengingVisitor(Heap* heap, bool logging)
      : heap_(heap),
        is_in_incremental_marking_(heap_->incremental_marking()->IsMarking()),
        logging_(logging) {}

  V8_INLINE bool ShouldVisitMapPointer() { return false; }

  V8_INLINE void Scavenge(HeapObject** slot, Map* map, HeapObject* object) {
    current_slot_ = slot;
    Parent::Visit(map, object);
  }

#define FOR_SIMPLE_OBJECTS(V)            \
  V(SeqOneByteString, DATA_OBJECT)       \
  V(SeqTwoByteString, DATA_OBJECT)       \
  V(Cell, POINTER_OBJECT)                \
  V(ByteArray, DATA_OBJECT)              \
  V(FixedArray, POINTER_OBJECT)          \
  V(FixedDoubleArray, DATA_OBJECT)       \
  V(FixedTypedArrayBase, POINTER_OBJECT) \
  V(FixedFloat64Array, POINTER_OBJECT)   \
  V(JSArrayBuffer, POINTER_OBJECT)       \
  V(ConsString, POINTER_OBJECT)          \
  V(SlicedString, POINTER_OBJECT)        \
  V(Symbol, POINTER_OBJECT)              \
  V(SharedFunctionInfo, POINTER_OBJECT)  \
  V(JSWeakCollection, POINTER_OBJECT)    \
  V(JSRegExp, POINTER_OBJECT)            \
  V(JSObject, POINTER_OBJECT)

#define SIMPLE_OBJECTS_HANDLER(TypeName, Contents)             \
  V8_INLINE int Visit##TypeName(Map* map, TypeName* object) {  \
    int size = TypeName::BodyDescriptor::SizeOf(map, object);  \
    EvacuateObject<Contents, kWordAligned>(map, object, size); \
    return size;                                               \
  }

  FOR_SIMPLE_OBJECTS(SIMPLE_OBJECTS_HANDLER)
#undef SIMPLE_OBJECTS_HANDLER
#undef FOR_SIMPLE_OBJECTS

  V8_INLINE int VisitNativeContext(Map* map, Context* object) {
    int size = Context::BodyDescriptor::SizeOf(map, object);
    EvacuateObject<POINTER_OBJECT, kWordAligned>(map, object, size);
    return size;
  }

  V8_INLINE int VisitDataObject(Map* map, HeapObject* object) {
    int size = map->instance_size();
    EvacuateObject<DATA_OBJECT, kWordAligned>(map, object, size);
    return size;
  }

  V8_INLINE int VisitJSObjectFast(Map* map, JSObject* object) {
    return VisitJSObject(map, object);
  }

  V8_INLINE int VisitJSApiObject(Map* map, JSObject* object) {
    return VisitJSObject(map, object);
  }

  V8_INLINE int VisitStruct(Map* map, HeapObject* object) {
    int size = map->instance_size();
    EvacuateObject<POINTER_OBJECT, kWordAligned>(map, object, size);
    return size;
  }

  V8_INLINE int VisitJSFunction(Map* map, JSFunction* object) {
    int size = JSFunction::BodyDescriptor::SizeOf(map, object);
    EvacuateObject<POINTER_OBJECT, kWordAligned>(map, object, size);

    if (!is_in_incremental_marking_) return size;

    MapWord map_word = object->map_word();
    DCHECK(map_word.IsForwardingAddress());
    HeapObject* target = map_word.ToForwardingAddress();

    // TODO(mlippautz): Notify collector of this object so we don't have to
    // retrieve the state our of thin air.
    if (ObjectMarking::IsBlack(target, MarkingState::Internal(target))) {
      // This object is black and it might not be rescanned by marker.
      // We should explicitly record code entry slot for compaction because
      // promotion queue processing (IteratePromotedObjectPointers) will
      // miss it as it is not HeapObject-tagged.
      Address code_entry_slot =
          target->address() + JSFunction::kCodeEntryOffset;
      Code* code = Code::cast(Code::GetObjectFromEntryAddress(code_entry_slot));
      heap_->mark_compact_collector()->RecordCodeEntrySlot(
          target, code_entry_slot, code);
    }
    return size;
  }

  V8_INLINE int VisitShortcutCandidate(Map* map, ConsString* object) {
    DCHECK(IsShortcutCandidate(map->instance_type()));
    if (!is_in_incremental_marking_ &&
        object->unchecked_second() == heap_->empty_string()) {
      HeapObject* first = HeapObject::cast(object->unchecked_first());

      *current_slot_ = first;

      if (!heap_->InNewSpace(first)) {
        object->set_map_word(MapWord::FromForwardingAddress(first));
        return ConsString::BodyDescriptor::SizeOf(map, object);
      }

      MapWord first_word = first->map_word();
      if (first_word.IsForwardingAddress()) {
        HeapObject* target = first_word.ToForwardingAddress();

        *current_slot_ = target;
        object->set_map_word(MapWord::FromForwardingAddress(target));
        return ConsString::BodyDescriptor::SizeOf(map, object);
      }

      Scavenger::ScavengeObjectSlow(current_slot_, first);
      object->set_map_word(MapWord::FromForwardingAddress(*current_slot_));
      return ConsString::BodyDescriptor::SizeOf(map, object);
    }

    int object_size = ConsString::BodyDescriptor::SizeOf(map, object);
    EvacuateObject<POINTER_OBJECT, kWordAligned>(map, object, object_size);
    return object_size;
  }

  V8_INLINE int VisitThinString(Map* map, ThinString* object) {
    if (!is_in_incremental_marking_) {
      HeapObject* actual = object->actual();
      *current_slot_ = actual;
      // ThinStrings always refer to internalized strings, which are
      // always in old space.
      DCHECK(!heap_->InNewSpace(actual));
      object->set_map_word(MapWord::FromForwardingAddress(actual));
      return ThinString::BodyDescriptor::SizeOf(map, object);
    }

    int size = ThinString::BodyDescriptor::SizeOf(map, object);
    EvacuateObject<POINTER_OBJECT, kWordAligned>(map, object, size);
    return size;
  }

 private:
  enum ObjectContents { DATA_OBJECT, POINTER_OBJECT };

  void RecordCopiedObject(HeapObject* obj) {
    bool should_record = FLAG_log_gc;
#ifdef DEBUG
    should_record = FLAG_heap_stats;
#endif
    if (should_record) {
      if (heap_->new_space()->Contains(obj)) {
        heap_->new_space()->RecordAllocation(obj);
      } else {
        heap_->new_space()->RecordPromotion(obj);
      }
    }
  }

  // Helper function used by CopyObject to copy a source object to an
  // allocated target object and update the forwarding pointer in the source
  // object.
  V8_INLINE void MigrateObject(HeapObject* source, HeapObject* target,
                               int size) {
    // If we migrate into to-space, then the to-space top pointer should be
    // right after the target object. Incorporate double alignment
    // over-allocation.
    DCHECK(!heap_->InToSpace(target) ||
           target->address() + size == heap_->new_space()->top() ||
           target->address() + size + kPointerSize ==
               heap_->new_space()->top());

    // Make sure that we do not overwrite the promotion queue which is at
    // the end of to-space.
    DCHECK(!heap_->InToSpace(target) ||
           heap_->promotion_queue()->IsBelowPromotionQueue(
               heap_->new_space()->top()));

    // Copy the content of source to target.
    heap_->CopyBlock(target->address(), source->address(), size);

    // Set the forwarding address.
    source->set_map_word(MapWord::FromForwardingAddress(target));

    if (logging_) {
      // Update NewSpace stats if necessary.
      RecordCopiedObject(target);
      heap_->OnMoveEvent(target, source, size);
    }

    if (is_in_incremental_marking_) {
      heap_->incremental_marking()->TransferColor(source, target);
    }
  }

  template <AllocationAlignment alignment>
  inline bool SemiSpaceCopyObject(Map* map, HeapObject* object,
                                  int object_size) {
    DCHECK(heap_->AllowedToBeMigrated(object, NEW_SPACE));
    AllocationResult allocation =
        heap_->new_space()->AllocateRaw(object_size, alignment);

    HeapObject* target = NULL;  // Initialization to please compiler.
    if (allocation.To(&target)) {
      // Order is important here: Set the promotion limit before storing a
      // filler for double alignment or migrating the object. Otherwise we
      // may end up overwriting promotion queue entries when we migrate the
      // object.
      heap_->promotion_queue()->SetNewLimit(heap_->new_space()->top());

      MigrateObject(object, target, object_size);

      // Update slot to new target.
      *current_slot_ = target;

      heap_->IncrementSemiSpaceCopiedObjectSize(object_size);
      return true;
    }
    return false;
  }

  template <ObjectContents object_contents, AllocationAlignment alignment>
  inline bool PromoteObject(Map* map, HeapObject* object, int object_size) {
    AllocationResult allocation =
        heap_->old_space()->AllocateRaw(object_size, alignment);

    HeapObject* target = nullptr;
    if (allocation.To(&target)) {
      DCHECK(ObjectMarking::IsWhite(
          target, heap_->mark_compact_collector()->marking_state(target)));
      MigrateObject(object, target, object_size);

      // Update slot to new target using CAS. A concurrent sweeper thread my
      // filter the slot concurrently.
      HeapObject* old = *current_slot_;
      base::Release_CompareAndSwap(
          reinterpret_cast<base::AtomicWord*>(current_slot_),
          reinterpret_cast<base::AtomicWord>(old),
          reinterpret_cast<base::AtomicWord>(target));

      if (object_contents == POINTER_OBJECT) {
        heap_->promotion_queue()->insert(target, object_size);
      }
      heap_->IncrementPromotedObjectsSize(object_size);
      return true;
    }
    return false;
  }

  template <ObjectContents object_contents, AllocationAlignment alignment>
  inline void EvacuateObject(Map* map, HeapObject* object, int object_size) {
    SLOW_DCHECK(object_size <= Page::kAllocatableMemory);
    SLOW_DCHECK(object->Size() == object_size);
    if (!heap_->ShouldBePromoted(object->address())) {
      // A semi-space copy may fail due to fragmentation. In that case, we
      // try to promote the object.
      if (SemiSpaceCopyObject<alignment>(map, object, object_size)) {
        return;
      }
    }

    if (PromoteObject<object_contents, alignment>(map, object, object_size)) {
      return;
    }

    // If promotion failed, we try to copy the object to the other semi-space
    if (SemiSpaceCopyObject<alignment>(map, object, object_size)) return;

    FatalProcessOutOfMemory("Scavenger: semi-space copy\n");
  }

  Heap* heap_;
  HeapObject** current_slot_;
  bool is_in_incremental_marking_;
  bool logging_;
};

void Scavenger::ScavengeObject(HeapObject** p, HeapObject* object) {
  DCHECK(object->GetIsolate()->heap()->InFromSpace(object));

  // We use the first word (where the map pointer usually is) of a heap
  // object to record the forwarding pointer.  A forwarding pointer can
  // point to an old space, the code space, or the to space of the new
  // generation.
  MapWord first_word = object->map_word();

  // If the first word is a forwarding address, the object has already been
  // copied.
  if (first_word.IsForwardingAddress()) {
    HeapObject* dest = first_word.ToForwardingAddress();
    DCHECK(object->GetIsolate()->heap()->InFromSpace(*p));
    *p = dest;
    return;
  }

  object->GetHeap()->UpdateAllocationSite<Heap::kGlobal>(
      object, object->GetHeap()->global_pretenuring_feedback_);

  // AllocationMementos are unrooted and shouldn't survive a scavenge
  DCHECK(object->map() != object->GetHeap()->allocation_memento_map());
  // Call the slow part of scavenge object.
  return ScavengeObjectSlow(p, object);
}

void Scavenger::DispatchToVisitor(HeapObject** slot, Map* map,
                                  HeapObject* object) {
  ScavengingVisitor visitor(heap(), logging_);
  visitor.Scavenge(slot, map, object);
}

void Scavenger::ScavengeObjectSlow(HeapObject** p, HeapObject* object) {
  SLOW_DCHECK(object->GetIsolate()->heap()->InFromSpace(object));
  MapWord first_word = object->map_word();
  SLOW_DCHECK(!first_word.IsForwardingAddress());
  Map* map = first_word.ToMap();

  Scavenger* scavenger = map->GetHeap()->scavenge_collector_;
  scavenger->DispatchToVisitor(p, map, object);
}

SlotCallbackResult Scavenger::CheckAndScavengeObject(Heap* heap,
                                                     Address slot_address) {
  Object** slot = reinterpret_cast<Object**>(slot_address);
  Object* object = *slot;
  if (heap->InFromSpace(object)) {
    HeapObject* heap_object = reinterpret_cast<HeapObject*>(object);
    DCHECK(heap_object->IsHeapObject());

    ScavengeObject(reinterpret_cast<HeapObject**>(slot), heap_object);

    object = *slot;
    // If the object was in from space before and is after executing the
    // callback in to space, the object is still live.
    // Unfortunately, we do not know about the slot. It could be in a
    // just freed free space object.
    if (heap->InToSpace(object)) {
      return KEEP_SLOT;
    }
  }
  // Slots can point to "to" space if the slot has been recorded multiple
  // times in the remembered set. We remove the redundant slot now.
  return REMOVE_SLOT;
}

void ScavengeVisitor::VisitPointers(HeapObject* host, Object** start,
                                    Object** end) {
  for (Object** p = start; p < end; p++) {
    Object* object = *p;
    if (!heap_->InNewSpace(object)) continue;
    Scavenger::ScavengeObject(reinterpret_cast<HeapObject**>(p),
                              reinterpret_cast<HeapObject*>(object));
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_SCAVENGER_INL_H_
