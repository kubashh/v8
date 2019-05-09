// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/decompression-elimination.h"
#include "src/compiler/simplified-operator.h"
#include "test/unittests/compiler/graph-reducer-unittest.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"

using testing::StrictMock;

namespace v8 {
namespace internal {
namespace compiler {

class DecompressionEliminationTest : public GraphTest {
 public:
  DecompressionEliminationTest()
      : GraphTest(),
        machine_(zone(), MachineType::PointerRepresentation(),
                 MachineOperatorBuilder::kNoFlags),
        simplified_(zone()) {}
  ~DecompressionEliminationTest() override = default;

 protected:
  Reduction Reduce(Node* node) {
    StrictMock<MockAdvancedReducerEditor> editor;
    DecompressionElimination decompression_elimination(&editor);
    return decompression_elimination.Reduce(node);
  }
  MachineOperatorBuilder* machine() { return &machine_; }
  SimplifiedOperatorBuilder* simplified() { return &simplified_; }

 private:
  MachineOperatorBuilder machine_;
  SimplifiedOperatorBuilder simplified_;
};

// -----------------------------------------------------------------------------
// Direct Decompression & Compression

TEST_F(DecompressionEliminationTest, BasicDecompressionCompression) {
  // Skip test if we are not using pointer compression
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  ElementAccess const access = {kTaggedBase, kTaggedSize, Type::Any(),
                                MachineType::AnyTagged(), kNoWriteBarrier};

  // Create the graph
  Node* load = graph()->NewNode(simplified()->LoadElement(access), object,
                                index, effect, control);
  Node* changeToTagged =
      graph()->NewNode(machine()->ChangeCompressedToTagged(), load);
  Node* changeToCompressed =
      graph()->NewNode(machine()->ChangeTaggedToCompressed(), changeToTagged);
  effect = graph()->NewNode(simplified()->StoreElement(access), object, index,
                            changeToCompressed, effect, control);

  // Reduce
  Reduction r = Reduce(changeToCompressed);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

TEST_F(DecompressionEliminationTest, BasicDecompressionCompressionSigned) {
  // Skip test if we are not using pointer compression
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  ElementAccess const access = {kTaggedBase, kTaggedSize, Type::Any(),
                                MachineType::TaggedSigned(), kNoWriteBarrier};

  // Create the graph
  Node* load = graph()->NewNode(simplified()->LoadElement(access), object,
                                index, effect, control);
  Node* changeToTagged =
      graph()->NewNode(machine()->ChangeCompressedSignedToTaggedSigned(), load);
  Node* changeToCompressed = graph()->NewNode(
      machine()->ChangeTaggedSignedToCompressedSigned(), changeToTagged);
  effect = graph()->NewNode(simplified()->StoreElement(access), object, index,
                            changeToCompressed, effect, control);

  // Reduce
  Reduction r = Reduce(changeToCompressed);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

TEST_F(DecompressionEliminationTest, BasicDecompressionCompressionPointer) {
  // Skip test if we are not using pointer compression
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  ElementAccess const access = {kTaggedBase, kTaggedSize, Type::Any(),
                                MachineType::TaggedPointer(), kNoWriteBarrier};

  // Create the graph
  Node* load = graph()->NewNode(simplified()->LoadElement(access), object,
                                index, effect, control);
  Node* changeToTagged = graph()->NewNode(
      machine()->ChangeCompressedPointerToTaggedPointer(), load);
  Node* changeToCompressed = graph()->NewNode(
      machine()->ChangeTaggedPointerToCompressedPointer(), changeToTagged);
  effect = graph()->NewNode(simplified()->StoreElement(access), object, index,
                            changeToCompressed, effect, control);

  // Reduce
  Reduction r = Reduce(changeToCompressed);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
