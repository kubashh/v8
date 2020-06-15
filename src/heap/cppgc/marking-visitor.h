// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_MARKING_VISITOR_H_
#define V8_HEAP_CPPGC_MARKING_VISITOR_H_

#include "include/cppgc/source-location.h"
#include "include/cppgc/trace-trait.h"
#include "include/v8config.h"
#include "src/base/macros.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap.h"
#include "src/heap/cppgc/marker.h"
#include "src/heap/cppgc/stack.h"
#include "src/heap/cppgc/visitor.h"

namespace cppgc {
namespace internal {

class BasePage;
class HeapObjectHeader;
class StackMarkingVisitor;

class MarkingVisitor : public VisitorBase {
 public:
  MarkingVisitor(HeapBase&, Marker::MarkingWorklist*,
                 Marker::NotFullyConstructedWorklist*,
                 Marker::WeakCallbackWorklist*, int);
  virtual ~MarkingVisitor() = default;

  MarkingVisitor(const MarkingVisitor&) = delete;
  MarkingVisitor& operator=(const MarkingVisitor&) = delete;

  void FlushWorklists();

  void DynamicallyMarkAddress(ConstAddress);

  void AccountMarkedBytes(const HeapObjectHeader&);
  size_t marked_bytes() const { return marked_bytes_; }

  static bool IsInConstruction(const HeapObjectHeader&);

 protected:
  void Visit(const void*, TraceDescriptor) override;
  void VisitWeak(const void*, TraceDescriptor, WeakCallback,
                 const void*) override;
  void VisitRoot(const void*, TraceDescriptor) override;
  void VisitWeakRoot(const void*, TraceDescriptor, WeakCallback,
                     const void*) override;

 private:
  void MarkHeader(HeapObjectHeader*, TraceDescriptor);
  bool MarkHeaderNoTracing(HeapObjectHeader*);
  void RegisterWeakCallback(WeakCallback, const void*) override;

#ifdef DEBUG
  HeapBase& heap_;
#endif  // DEBUG
  Marker::MarkingWorklist::View marking_worklist_;
  Marker::NotFullyConstructedWorklist::View not_fully_constructed_worklist_;
  Marker::WeakCallbackWorklist::View weak_callback_worklist_;

  size_t marked_bytes_ = 0;

  friend class StackMarkingVisitor;
};

class V8_EXPORT_PRIVATE MutatorThreadMarkingVisitor : public MarkingVisitor {
 public:
  explicit MutatorThreadMarkingVisitor(Marker*);
};

class V8_EXPORT_PRIVATE StackMarkingVisitor final : public StackVisitor {
  CPPGC_STACK_ALLOCATED();

 public:
  StackMarkingVisitor(
      MarkingVisitor& marking_visitor,  // NOLINT(runtime/references)
      PageBackend& page_backend);       // NOLINT(runtime/references)

  StackMarkingVisitor(const StackMarkingVisitor&) = delete;
  StackMarkingVisitor& operator=(const StackMarkingVisitor&) = delete;

  void VisitPointer(const void*) final;

  void ConservativelyMarkAddress(const BasePage*, ConstAddress);

 private:
  MarkingVisitor& marking_visitor_;
  PageBackend& page_backend_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_MARKING_VISITOR_H_
