// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_SCAVENGER_H_
#define V8_HEAP_SCAVENGER_H_

#include "src/heap/objects-visiting.h"
#include "src/heap/slot-set.h"

namespace v8 {
namespace internal {

class Scavenger {
 public:
  explicit Scavenger(Heap* heap) : heap_(heap) {}

  V8_INLINE void DispatchToVisitor(HeapObject** slot, Map* map,
                                   HeapObject* object);

  // Callback function passed to Heap::Iterate etc.  Copies an object if
  // necessary, the object might be promoted to an old space.  The caller must
  // ensure the precondition that the object is (a) a heap object and (b) in
  // the heap's from space.
  static inline void ScavengeObject(HeapObject** p, HeapObject* object);
  static inline SlotCallbackResult CheckAndScavengeObject(Heap* heap,
                                                          Address slot_address);

  // Slow part of {ScavengeObject} above.
  static inline void ScavengeObjectSlow(HeapObject** p, HeapObject* object);

  void UpdateConstraints();

  Isolate* isolate();
  Heap* heap() { return heap_; }

 private:
  Heap* heap_;
  bool logging_;
};

// Helper class for turning the scavenger into an object visitor that is also
// filtering out non-HeapObjects and objects which do not reside in new space.
class RootScavengeVisitor : public RootVisitor {
 public:
  explicit RootScavengeVisitor(Heap* heap) : heap_(heap) {}

  void VisitRootPointer(Root root, Object** p) override;
  void VisitRootPointers(Root root, Object** start, Object** end) override;

 private:
  inline void ScavengePointer(Object** p);

  Heap* heap_;
};

class ScavengeVisitor final : public NewSpaceVisitor<ScavengeVisitor> {
 public:
  explicit ScavengeVisitor(Heap* heap) : heap_(heap) {}
  V8_INLINE void VisitPointers(HeapObject* host, Object** start,
                               Object** end) final;

 private:
  Heap* heap_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_SCAVENGER_H_
