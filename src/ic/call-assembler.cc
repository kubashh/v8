// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ic/call-assembler.h"

#include "src/feedback-vector-inl.h"
#include "src/globals.h"

namespace v8 {
namespace internal {

using compiler::Node;

void CallAssembler::IncrementCallCount(Node* feedback_vector, Node* slot_id) {
  Comment("increment call count");
  Node* call_count =
      LoadFeedbackVectorSlot(feedback_vector, slot_id, kPointerSize);
  // The lowest {FeedbackNexus::CallCountField::kShift} bits of the call
  // count are used as flags. To increment the call count by 1 we hence
  // have to increment by 1 << {FeedbackNexus::CallCountField::kShift}.
  Node* new_count = SmiAdd(
      call_count, SmiConstant(1 << FeedbackNexus::CallCountField::kShift));
  // Count is Smi, so we don't need a write barrier.
  StoreFeedbackVectorSlot(feedback_vector, slot_id, new_count,
                          SKIP_WRITE_BARRIER, kPointerSize);
}

void CallAssembler::CollectCallableFeedback(Node* target, Node* context,
                                            Node* feedback_vector,
                                            Node* slot_id) {
  Label extra_checks(this, Label::kDeferred), done(this);

  // Check if we have monomorphic {target} feedback already.
  Node* feedback_element = LoadFeedbackVectorSlot(feedback_vector, slot_id);
  Node* feedback_value = LoadWeakCellValueUnchecked(feedback_element);
  Comment("check if monomorphic");
  Node* is_monomorphic = WordEqual(target, feedback_value);
  GotoIf(is_monomorphic, &done);

  // Check if it is a megamorphic {target}.
  Comment("check if megamorphic");
  Node* is_megamorphic =
      WordEqual(feedback_element,
                HeapConstant(FeedbackVector::MegamorphicSentinel(isolate())));
  Branch(is_megamorphic, &done, &extra_checks);

  BIND(&extra_checks);
  {
    Label initialize(this), mark_megamorphic(this);

    Comment("check if weak cell");
    Node* is_uninitialized = WordEqual(
        feedback_element,
        HeapConstant(FeedbackVector::UninitializedSentinel(isolate())));
    GotoIf(is_uninitialized, &initialize);
    CSA_ASSERT(this, IsWeakCell(feedback_element));

    // If the weak cell is cleared, we have a new chance to become monomorphic.
    Comment("check if weak cell is cleared");
    Node* is_smi = TaggedIsSmi(feedback_value);
    Branch(is_smi, &initialize, &mark_megamorphic);

    BIND(&initialize);
    {
      // Check if {target} is a JSFunction in the current native context.
      Comment("check if function in same native context");
      GotoIf(TaggedIsSmi(target), &mark_megamorphic);
      // Check if the {target} is a JSFunction or JSBoundFunction
      // in the current native context.
      VARIABLE(var_current, MachineRepresentation::kTagged, target);
      Label loop(this, &var_current), done_loop(this);
      Goto(&loop);
      BIND(&loop);
      {
        Label if_boundfunction(this), if_function(this);
        Node* current = var_current.value();
        CSA_ASSERT(this, TaggedIsNotSmi(current));
        Node* current_instance_type = LoadInstanceType(current);
        GotoIf(InstanceTypeEqual(current_instance_type, JS_BOUND_FUNCTION_TYPE),
               &if_boundfunction);
        Branch(InstanceTypeEqual(current_instance_type, JS_FUNCTION_TYPE),
               &if_function, &mark_megamorphic);

        BIND(&if_function);
        {
          // Check that the JSFunction {current} is in the current native
          // context.
          Node* current_context =
              LoadObjectField(current, JSFunction::kContextOffset);
          Node* current_native_context = LoadNativeContext(current_context);
          Branch(WordEqual(LoadNativeContext(context), current_native_context),
                 &done_loop, &mark_megamorphic);
        }

        BIND(&if_boundfunction);
        {
          // Continue with the [[BoundTargetFunction]] of {target}.
          var_current.Bind(LoadObjectField(
              current, JSBoundFunction::kBoundTargetFunctionOffset));
          Goto(&loop);
        }
      }
      BIND(&done_loop);
      CreateWeakCellInFeedbackVector(feedback_vector, slot_id, target);
      ReportFeedbackUpdate(feedback_vector, slot_id, "Call:Initialize");
      Goto(&done);
    }

    BIND(&mark_megamorphic);
    {
      // MegamorphicSentinel is an immortal immovable object so
      // write-barrier is not needed.
      Comment("transition to megamorphic");
      DCHECK(Heap::RootIsImmortalImmovable(Heap::kmegamorphic_symbolRootIndex));
      StoreFeedbackVectorSlot(
          feedback_vector, slot_id,
          HeapConstant(FeedbackVector::MegamorphicSentinel(isolate())),
          SKIP_WRITE_BARRIER);
      ReportFeedbackUpdate(feedback_vector, slot_id,
                           "Call:TransitionMegamorphic");
      Goto(&done);
    }
  }

  BIND(&done);
}

void CallAssembler::CollectCallFeedback(Node* target, Node* context,
                                        Node* feedback_vector, Node* slot_id) {
  // Increment the call count.
  IncrementCallCount(feedback_vector, slot_id);

  // Collect the callable {target} feedback.
  CollectCallableFeedback(target, context, feedback_vector, slot_id);
}

void CallAssembler::CollectConstructFeedback(Node* target, Node* context,
                                             Node* new_target, Node* slot_id,
                                             Node* feedback_vector,
                                             Variable* var_site,
                                             Label* construct_array,
                                             Label* construct) {
  Label extra_checks(this, Label::kDeferred);

  // Increment the call count.
  IncrementCallCount(feedback_vector, slot_id);

  // Check if we have monomorphic {new_target} feedback already.
  Node* feedback_element = LoadFeedbackVectorSlot(feedback_vector, slot_id);
  Node* feedback_value = LoadWeakCellValueUnchecked(feedback_element);
  Branch(WordEqual(new_target, feedback_value), construct, &extra_checks);

  BIND(&extra_checks);
  {
    Label check_allocation_site(this), check_initialized(this),
        initialize(this), mark_megamorphic(this);

    // Check if it is a megamorphic {new_target}..
    Comment("check if megamorphic");
    Node* is_megamorphic =
        WordEqual(feedback_element,
                  HeapConstant(FeedbackVector::MegamorphicSentinel(isolate())));
    GotoIf(is_megamorphic, construct);

    Comment("check if weak cell");
    Node* feedback_element_map = LoadMap(feedback_element);
    GotoIfNot(IsWeakCellMap(feedback_element_map), &check_allocation_site);

    // If the weak cell is cleared, we have a new chance to become monomorphic.
    Comment("check if weak cell is cleared");
    Node* is_smi = TaggedIsSmi(feedback_value);
    Branch(is_smi, &initialize, &mark_megamorphic);

    BIND(&check_allocation_site);
    {
      // Check if it is an AllocationSite.
      Comment("check if allocation site");
      GotoIfNot(IsAllocationSiteMap(feedback_element_map), &check_initialized);

      // Make sure that {target} and {new_target} are the Array constructor.
      Node* array_function = LoadContextElement(LoadNativeContext(context),
                                                Context::ARRAY_FUNCTION_INDEX);
      GotoIfNot(WordEqual(target, array_function), &mark_megamorphic);
      GotoIfNot(WordEqual(new_target, array_function), &mark_megamorphic);
      var_site->Bind(feedback_element);
      Goto(construct_array);
    }

    BIND(&check_initialized);
    {
      // Check if it is uninitialized.
      Comment("check if uninitialized");
      Node* is_uninitialized = WordEqual(
          feedback_element, LoadRoot(Heap::kuninitialized_symbolRootIndex));
      Branch(is_uninitialized, &initialize, &mark_megamorphic);
    }

    BIND(&initialize);
    {
      Comment("check if function in same native context");
      GotoIf(TaggedIsSmi(new_target), &mark_megamorphic);
      // Check if the {new_target} is a JSFunction or JSBoundFunction
      // in the current native context.
      VARIABLE(var_current, MachineRepresentation::kTagged, new_target);
      Label loop(this, &var_current), done_loop(this);
      Goto(&loop);
      BIND(&loop);
      {
        Label if_boundfunction(this), if_function(this);
        Node* current = var_current.value();
        CSA_ASSERT(this, TaggedIsNotSmi(current));
        Node* current_instance_type = LoadInstanceType(current);
        GotoIf(InstanceTypeEqual(current_instance_type, JS_BOUND_FUNCTION_TYPE),
               &if_boundfunction);
        Branch(InstanceTypeEqual(current_instance_type, JS_FUNCTION_TYPE),
               &if_function, &mark_megamorphic);

        BIND(&if_function);
        {
          // Check that the JSFunction {current} is in the current native
          // context.
          Node* current_context =
              LoadObjectField(current, JSFunction::kContextOffset);
          Node* current_native_context = LoadNativeContext(current_context);
          Branch(WordEqual(LoadNativeContext(context), current_native_context),
                 &done_loop, &mark_megamorphic);
        }

        BIND(&if_boundfunction);
        {
          // Continue with the [[BoundTargetFunction]] of {current}.
          var_current.Bind(LoadObjectField(
              current, JSBoundFunction::kBoundTargetFunctionOffset));
          Goto(&loop);
        }
      }
      BIND(&done_loop);

      // Create an AllocationSite if {target} and {new_target} refer
      // to the current native context's Array constructor.
      Label create_allocation_site(this), create_weak_cell(this);
      GotoIfNot(WordEqual(target, new_target), &create_weak_cell);
      Node* array_function = LoadContextElement(LoadNativeContext(context),
                                                Context::ARRAY_FUNCTION_INDEX);
      Branch(WordEqual(target, array_function), &create_allocation_site,
             &create_weak_cell);

      BIND(&create_allocation_site);
      {
        var_site->Bind(CreateAllocationSiteInFeedbackVector(feedback_vector,
                                                            SmiTag(slot_id)));
        ReportFeedbackUpdate(feedback_vector, slot_id,
                             "Construct:CreateAllocationSite");
        Goto(construct_array);
      }

      BIND(&create_weak_cell);
      {
        CreateWeakCellInFeedbackVector(feedback_vector, slot_id, new_target);
        ReportFeedbackUpdate(feedback_vector, slot_id,
                             "Construct:CreateWeakCell");
        Goto(construct);
      }
    }

    BIND(&mark_megamorphic);
    {
      // MegamorphicSentinel is an immortal immovable object so
      // write-barrier is not needed.
      Comment("transition to megamorphic");
      DCHECK(Heap::RootIsImmortalImmovable(Heap::kmegamorphic_symbolRootIndex));
      StoreFeedbackVectorSlot(
          feedback_vector, slot_id,
          HeapConstant(FeedbackVector::MegamorphicSentinel(isolate())),
          SKIP_WRITE_BARRIER);
      ReportFeedbackUpdate(feedback_vector, slot_id,
                           "Construct:TransitionMegamorphic");
      Goto(construct);
    }
  }
}

}  // namespace internal
}  // namespace v8
