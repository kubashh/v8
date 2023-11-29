// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_TRACED_HANDLES_H_
#define V8_HANDLES_TRACED_HANDLES_H_

#include "include/v8-embedder-heap.h"
#include "include/v8-traced-handle.h"
#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/objects/objects.h"
#include "src/objects/visitors.h"

namespace v8::internal {

class Isolate;
class TracedHandlesImpl;

// TracedHandles hold handles that must go through cppgc's tracing methods. The
// handles do otherwise not keep their pointees alive.
class V8_EXPORT_PRIVATE TracedHandles final {
 public:
  enum class MarkMode : uint8_t { kOnlyYoung, kAll };
  enum class WeaknessCompuationMode : uint8_t { kAtomic, kConcurrent };

  static void Destroy(Address* location);
  static void Copy(const Address* const* from, Address** to);
  static void Move(Address** from, Address** to);

  // Returns the object referenced by the relevant node and whether the node was
  // marked.
  static std::pair<Tagged<Object>, bool> TryMark(Address* location,
                                                 MarkMode mark_mode);
  static Tagged<Object> MarkConservatively(Address* inner_location,
                                           Address* traced_node_block_base,
                                           MarkMode mark_mode);

  // Update a node's IsWeak bit and returns the new value.
  static bool UpdateIsWeak(Address* location,
                           EmbedderRootsHandler* embedder_root_handler,
                           WeaknessCompuationMode mode);

  static bool IsValidInUseNode(Address* location);

  explicit TracedHandles(Isolate*);
  ~TracedHandles();

  TracedHandles(const TracedHandles&) = delete;
  TracedHandles& operator=(const TracedHandles&) = delete;

  FullObjectSlot Create(Address value, Address* slot,
                        GlobalHandleStoreMode store_mode);

  using NodeBounds = std::vector<std::pair<const void*, const void*>>;
  const NodeBounds GetNodeBounds() const;

  void SetIsMarking(bool);
  void SetIsSweepingOnMutatorThread(bool);

  // Updates the list of young nodes that is maintained separately.
  void UpdateListOfYoungNodes();
  // Clears the list of young nodes, assuming that the young generation is
  // empty.
  void ClearListOfYoungNodes();

  // Deletes empty blocks. Sweeping must not be running.
  void DeleteEmptyBlocks();

  void ResetDeadNodes(WeakSlotCallbackWithHeap should_reset_handle);
  void ResetYoungDeadNodes(WeakSlotCallbackWithHeap should_reset_handle);

  void Iterate(RootVisitor*);
  void IterateYoungRoots(RootVisitor*);
  void IterateAndMarkYoungRootsWithOldHosts(RootVisitor*);
  void IterateYoungRootsWithOldHostsForTesting(RootVisitor*);

  size_t used_node_count() const;
  size_t total_size_bytes() const;
  size_t used_size_bytes() const;

  bool HasYoung() const;

 private:
  std::unique_ptr<TracedHandlesImpl> impl_;
};

}  // namespace v8::internal

#endif  // V8_HANDLES_TRACED_HANDLES_H_
