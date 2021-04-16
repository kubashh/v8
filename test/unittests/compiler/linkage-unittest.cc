// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/linkage.h"

#include "src/compiler/node.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

class LinkageTest : public TestWithZone {
 protected:
  LinkageTest() : TestWithZone(kCompressGraphZone) {}

  CallDescriptor* NewStandardCallDescriptor(LocationSignature* locations) {
    int param_slots = 0;
    for (size_t i = 0; i < locations->parameter_count(); ++i) {
      auto location = locations->GetParam(i);
      if (location.IsCallerFrameSlot()) {
        param_slots = std::max(param_slots, -location.GetLocation());
      }
    }
    param_slots = AddArgumentPaddingSlots(param_slots);

    int total_slots = param_slots;
    for (size_t i = 0; i < locations->return_count(); ++i) {
      auto location = locations->GetReturn(i);
      if (location.IsCallerFrameSlot()) {
        total_slots = std::max(total_slots, -location.GetLocation());
      }
    }
    int return_slots = total_slots -= param_slots;

    return zone()->New<CallDescriptor>(
        CallDescriptor::kCallCodeObject, MachineType::AnyTagged(),
        LinkageLocation::ForAnyRegister(MachineType::Pointer()),
        locations,  // location_sig
        static_cast<size_t>(param_slots),
        Operator::kNoProperties,   // properties
        0,                         // callee-saved
        0,                         // callee-saved fp
        CallDescriptor::kNoFlags,  // flags,
        "", StackArgumentOrder::kDefault,
        0,  // allocatable_registers
        static_cast<size_t>(return_slots));
  }

  LinkageLocation StackLocation(int loc) {
    return LinkageLocation::ForCallerFrameSlot(-loc, MachineType::Pointer());
  }
};

TEST_F(LinkageTest, NoParamOrReturnSlots) {
  LocationSignature signature(0, 0, nullptr);
  CallDescriptor* desc = NewStandardCallDescriptor(&signature);
  EXPECT_EQ(0UL, desc->ParameterSlotCount());
  EXPECT_EQ(0UL, desc->ReturnSlotCount());
  EXPECT_EQ(1, desc->GetOffsetToFirstUnusedStackSlot());
  EXPECT_EQ(0, desc->GetOffsetToReturns());
}

TEST_F(LinkageTest, GetOffsetToFirstUnusedStackSlot) {
  const int kLastParamSlot = 4;
  LinkageLocation locations[] = {StackLocation(1),
                                 StackLocation(kLastParamSlot)};
  LocationSignature signature(0, 2, locations);
  CallDescriptor* desc = NewStandardCallDescriptor(&signature);
  EXPECT_EQ(kLastParamSlot, static_cast<int>(desc->ParameterSlotCount()));
  EXPECT_EQ(0UL, desc->ReturnSlotCount());
  EXPECT_EQ(kLastParamSlot + 1, desc->GetOffsetToFirstUnusedStackSlot());
}

TEST_F(LinkageTest, GetOffsetToReturnsWithOnlyParams) {
  const int kLastParamSlot = 3;
  const int kFirstReturnSlot = AddArgumentPaddingSlots(kLastParamSlot);
  LinkageLocation locations[] = {StackLocation(1),
                                 StackLocation(kLastParamSlot)};
  LocationSignature signature(0, 2, locations);
  CallDescriptor* desc = NewStandardCallDescriptor(&signature);
  EXPECT_EQ(0UL, desc->ReturnSlotCount());
  // Arguments may require a padding slot, which we count.
  EXPECT_EQ(kFirstReturnSlot, desc->GetOffsetToReturns());
}

TEST_F(LinkageTest, GetOffsetToReturnsWithParamsAndReturns) {
  const int kLastParamSlot = 3;  // odd location, which may require padding.
  const int kOptionalPaddingSlot = AddArgumentPaddingSlots(kLastParamSlot);
  const int kFirstReturnSlot = kOptionalPaddingSlot + 1;
  const int kLastReturnSlot = kFirstReturnSlot + 2;
  LinkageLocation locations[] = {
      StackLocation(kFirstReturnSlot), StackLocation(kLastReturnSlot),
      StackLocation(1), StackLocation(kLastParamSlot)};
  LocationSignature signature(2, 2, locations);
  CallDescriptor* desc = NewStandardCallDescriptor(&signature);
  EXPECT_EQ(kOptionalPaddingSlot, static_cast<int>(desc->ParameterSlotCount()));
  EXPECT_EQ(kLastReturnSlot - kOptionalPaddingSlot,
            static_cast<int>(desc->ReturnSlotCount()));
  EXPECT_EQ(kFirstReturnSlot - 1, desc->GetOffsetToReturns());
}

TEST_F(LinkageTest, GetStackParameterDeltaNoReturns) {
  const int kCallerLastStackParam = 2;
  const int kCalleeLastStackParam = 5;
  LinkageLocation caller_locations[] = {StackLocation(kCallerLastStackParam)};
  LocationSignature caller_signature(0, 1, caller_locations);
  CallDescriptor* caller = NewStandardCallDescriptor(&caller_signature);
  LinkageLocation callee_locations[] = {StackLocation(kCalleeLastStackParam)};
  LocationSignature callee_signature(0, 1, callee_locations);
  CallDescriptor* callee = NewStandardCallDescriptor(&callee_signature);
  EXPECT_TRUE(caller->CanTailCall(callee));
  int expected = AddArgumentPaddingSlots(kCalleeLastStackParam) -
                 AddArgumentPaddingSlots(kCallerLastStackParam);
  EXPECT_EQ(expected, callee->GetStackParameterDelta(caller));

  // Check the other way around.
  EXPECT_TRUE(callee->CanTailCall(caller));
  EXPECT_EQ(-expected, caller->GetStackParameterDelta(callee));
}

TEST_F(LinkageTest, GetStackParameterDeltaReturns) {
  const int kCallerFirstReturnSlot = AddArgumentPaddingSlots(2);
  const int kCalleeFirstReturnSlot = AddArgumentPaddingSlots(5);
  LinkageLocation caller_locations[] = {
      StackLocation(kCallerFirstReturnSlot),
      StackLocation(kCallerFirstReturnSlot + 2),
      StackLocation(kCallerFirstReturnSlot - 1)};
  LocationSignature caller_signature(2, 1, caller_locations);
  CallDescriptor* caller = NewStandardCallDescriptor(&caller_signature);
  LinkageLocation callee_locations[] = {
      StackLocation(kCalleeFirstReturnSlot),
      StackLocation(kCalleeFirstReturnSlot + 2),
      StackLocation(kCalleeFirstReturnSlot - 1)};
  LocationSignature callee_signature(2, 1, callee_locations);
  CallDescriptor* callee = NewStandardCallDescriptor(&callee_signature);
  EXPECT_TRUE(caller->CanTailCall(callee));
  int expected = AddArgumentPaddingSlots(kCalleeFirstReturnSlot - 1) -
                 AddArgumentPaddingSlots(kCallerFirstReturnSlot - 1);
  EXPECT_EQ(expected, callee->GetStackParameterDelta(caller));

  // Check the other way around.
  EXPECT_TRUE(callee->CanTailCall(caller));
  EXPECT_EQ(-expected, caller->GetStackParameterDelta(callee));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
