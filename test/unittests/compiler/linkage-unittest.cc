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
    size_t stack_arguments = 0;
    for (size_t i = 0; i < locations->parameter_count(); ++i) {
      if (locations->GetParam(i).IsCallerFrameSlot()) stack_arguments++;
    }
    size_t stack_returns = 0;
    for (size_t i = 0; i < locations->return_count(); ++i) {
      if (locations->GetReturn(i).IsCallerFrameSlot()) stack_returns++;
    }
    return zone()->New<CallDescriptor>(
        CallDescriptor::kCallCodeObject, MachineType::AnyTagged(),
        LinkageLocation::ForAnyRegister(MachineType::Pointer()),
        locations,  // location_sig
        stack_arguments,
        Operator::kNoProperties,   // properties
        0,                         // callee-saved
        0,                         // callee-saved fp
        CallDescriptor::kNoFlags,  // flags,
        "", StackArgumentOrder::kDefault,
        0,  // allocatable_registers
        stack_returns);
  }

  LinkageLocation StackLocation(int loc) {
    return LinkageLocation::ForCallerFrameSlot(-loc, MachineType::Pointer());
  }
};

TEST_F(LinkageTest, NoStackParamsOrReturns) {
  LocationSignature signature(0, 0, nullptr);
  CallDescriptor* desc = NewStandardCallDescriptor(&signature);
  EXPECT_EQ(0UL, desc->StackParameterCount());
  EXPECT_EQ(0UL, desc->StackReturnCount());
  EXPECT_EQ(0, desc->GetFirstUnusedStackSlot());
  EXPECT_EQ(0, desc->GetOffsetToReturns());
}

TEST_F(LinkageTest, GetFirstUnusedStackSlot) {
  const int kLastStackParam = 4;
  LinkageLocation locations[] = {StackLocation(1),
                                 StackLocation(kLastStackParam)};
  LocationSignature signature(0, 2, locations);
  CallDescriptor* desc = NewStandardCallDescriptor(&signature);
  EXPECT_EQ(2UL, desc->StackParameterCount());
  EXPECT_EQ(0UL, desc->StackReturnCount());
  EXPECT_EQ(kLastStackParam, desc->GetFirstUnusedStackSlot());
}

TEST_F(LinkageTest, GetOffsetToReturns_NoReturns) {
  const int kLastStackParam = 3;
  LinkageLocation locations[] = {StackLocation(1),
                                 StackLocation(kLastStackParam)};
  LocationSignature signature(0, 2, locations);
  CallDescriptor* desc = NewStandardCallDescriptor(&signature);
  EXPECT_EQ(0UL, desc->StackReturnCount());
  // Arguments may require a padding slot, which we count.
  int expected = AddArgumentPaddingSlots(kLastStackParam);
  EXPECT_EQ(expected, desc->GetOffsetToReturns());
}

TEST_F(LinkageTest, GetOffsetToReturns_Returns) {
  const int kLastStackParam = 3;  // odd location, which may require padding.
  const int kFirstStackReturn = kLastStackParam + 3;
  LinkageLocation locations[] = {
      StackLocation(kFirstStackReturn), StackLocation(kFirstStackReturn + 2),
      StackLocation(1), StackLocation(kLastStackParam)};
  LocationSignature signature(2, 2, locations);
  CallDescriptor* desc = NewStandardCallDescriptor(&signature);
  EXPECT_EQ(2UL, desc->StackParameterCount());
  EXPECT_EQ(2UL, desc->StackReturnCount());
  EXPECT_EQ(kFirstStackReturn - 1, desc->GetOffsetToReturns());
}

TEST_F(LinkageTest, GetStackParameterDelta_NoReturns) {
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

TEST_F(LinkageTest, GetStackParameterDelta_Returns) {
  const int kCallerFirstStackReturn = 2;
  const int kCalleeFirstStackReturn = 5;
  LinkageLocation caller_locations[] = {
      StackLocation(kCallerFirstStackReturn),
      StackLocation(kCallerFirstStackReturn + 2), StackLocation(1)};
  LocationSignature caller_signature(2, 1, caller_locations);
  CallDescriptor* caller = NewStandardCallDescriptor(&caller_signature);
  LinkageLocation callee_locations[] = {
      StackLocation(kCalleeFirstStackReturn),
      StackLocation(kCalleeFirstStackReturn + 2), StackLocation(2)};
  LocationSignature callee_signature(2, 1, callee_locations);
  CallDescriptor* callee = NewStandardCallDescriptor(&callee_signature);
  EXPECT_TRUE(caller->CanTailCall(callee));
  int expected = AddArgumentPaddingSlots(kCalleeFirstStackReturn - 1) -
                 AddArgumentPaddingSlots(kCallerFirstStackReturn - 1);
  EXPECT_EQ(expected, callee->GetStackParameterDelta(caller));

  // Check the other way around.
  EXPECT_TRUE(callee->CanTailCall(caller));
  EXPECT_EQ(-expected, caller->GetStackParameterDelta(callee));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
