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
  // Skip test if pointer compression is not enabled
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
  // Skip test if pointer compression is not enabled
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
  // Skip test if pointer compression is not enabled
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

// -----------------------------------------------------------------------------
// Direct Decompression & Compression - border cases

// For example, if we are lowering a CheckedCompressedToTaggedPointer in the
// effect linearization phase we will change that to
// ChangeCompressedPointerToTaggedPointer. Then, we might end up with a chain of
// Parent <- ChangeCompressedPointerToTaggedPointer <- ChangeTaggedToCompressed
// <- Child.
// Similarly, we have cases with Signed instead of pointer.
// The following border case tests will test that the functionality is robust
// enough to handle that.

TEST_F(DecompressionEliminationTest,
       BasicDecompressionCompressionBorderCaseSigned) {
  // Skip test if pointer compression is not enabled
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  ElementAccess const loadAccess = {kTaggedBase, kTaggedSize, Type::Any(),
                                    MachineType::AnyTagged(), kNoWriteBarrier};
  ElementAccess const storeAccess = {kTaggedBase, kTaggedSize, Type::Any(),
                                     MachineType::TaggedSigned(),
                                     kNoWriteBarrier};

  // Create the graph
  Node* load = graph()->NewNode(simplified()->LoadElement(loadAccess), object,
                                index, effect, control);
  Node* changeToTagged =
      graph()->NewNode(machine()->ChangeCompressedSignedToTaggedSigned(), load);
  Node* changeToCompressed =
      graph()->NewNode(machine()->ChangeTaggedToCompressed(), changeToTagged);
  effect = graph()->NewNode(simplified()->StoreElement(storeAccess), object,
                            index, changeToCompressed, effect, control);

  // Reduce
  Reduction r = Reduce(changeToCompressed);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

TEST_F(DecompressionEliminationTest,
       BasicDecompressionCompressionBorderCasePointer) {
  // Skip test if pointer compression is not enabled
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  ElementAccess const loadAccess = {kTaggedBase, kTaggedSize, Type::Any(),
                                    MachineType::AnyTagged(), kNoWriteBarrier};
  ElementAccess const storeAccess = {kTaggedBase, kTaggedSize, Type::Any(),
                                     MachineType::TaggedPointer(),
                                     kNoWriteBarrier};

  // Create the graph
  Node* load = graph()->NewNode(simplified()->LoadElement(loadAccess), object,
                                index, effect, control);
  Node* changeToTagged = graph()->NewNode(
      machine()->ChangeCompressedPointerToTaggedPointer(), load);
  Node* changeToCompressed =
      graph()->NewNode(machine()->ChangeTaggedToCompressed(), changeToTagged);
  effect = graph()->NewNode(simplified()->StoreElement(storeAccess), object,
                            index, changeToCompressed, effect, control);

  // Reduce
  Reduction r = Reduce(changeToCompressed);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

// We also have cases of ChangeCompressedToTagged <-
// ChangeTaggedPointerToCompressedPointer, where the
// ChangeTaggedPointerToCompressedPointer was introduced while lowering a
// NewConsString on effect control linearizer

TEST_F(DecompressionEliminationTest,
       BasicDecompressionCompressionBorderCasePointerDecompression) {
  // Skip test if pointer compression is not enabled
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  ElementAccess const loadAccess = {kTaggedBase, kTaggedSize, Type::Any(),
                                    MachineType::TaggedPointer(),
                                    kNoWriteBarrier};
  ElementAccess const storeAccess = {kTaggedBase, kTaggedSize, Type::Any(),
                                     MachineType::AnyTagged(), kNoWriteBarrier};

  // Create the graph
  Node* load = graph()->NewNode(simplified()->LoadElement(loadAccess), object,
                                index, effect, control);
  Node* changeToTagged = graph()->NewNode(
      machine()->ChangeCompressedPointerToTaggedPointer(), load);
  Node* changeToCompressed =
      graph()->NewNode(machine()->ChangeTaggedToCompressed(), changeToTagged);
  effect = graph()->NewNode(simplified()->StoreElement(storeAccess), object,
                            index, changeToCompressed, effect, control);

  // Reduce
  Reduction r = Reduce(changeToCompressed);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

// -----------------------------------------------------------------------------
// TypedStateValues

TEST_F(DecompressionEliminationTest, TypedStateValuesOneDecompress) {
  // Skip test if pointer compression is not enabled
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  const int numberOfInputs = 1;
  const ZoneVector<MachineType>* types =
      new (graph()->zone()->New(sizeof(ZoneVector<MachineType>)))
          ZoneVector<MachineType>(numberOfInputs, graph()->zone());
  SparseInputMask dense = SparseInputMask::Dense();

  const ElementAccess ElementAccesses[] = {
      {kTaggedBase, kTaggedSize, Type::Any(), MachineType::AnyTagged(),
       kNoWriteBarrier},
      {kTaggedBase, kTaggedSize, Type::Any(), MachineType::TaggedSigned(),
       kNoWriteBarrier},
      {kTaggedBase, kTaggedSize, Type::Any(), MachineType::TaggedPointer(),
       kNoWriteBarrier}};

  // For every access
  for (size_t i = 0; i < arraysize(ElementAccesses); ++i) {
    // Create the graph
    Node* load = graph()->NewNode(simplified()->LoadElement(ElementAccesses[i]),
                                  object, index, effect, control);
    Node* changeToTagged = graph()->NewNode(
        machine()->ChangeCompressedPointerToTaggedPointer(), load);
    Node* typedStateValuesOneDecompress = graph()->NewNode(
        common()->TypedStateValues(types, dense), changeToTagged);

    // Reduce
    StrictMock<MockAdvancedReducerEditor> editor;
    DecompressionElimination decompression_elimination(&editor);
    EXPECT_CALL(editor, Replace(changeToTagged, load));
    Reduction r =
        decompression_elimination.Reduce(typedStateValuesOneDecompress);
    ASSERT_TRUE(r.Changed());
  }
}

TEST_F(DecompressionEliminationTest, TypedStateValuesTwoDecompresses) {
  // Skip test if pointer compression is not enabled
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  const int numberOfInputs = 3;
  const ZoneVector<MachineType>* types =
      new (graph()->zone()->New(sizeof(ZoneVector<MachineType>)))
          ZoneVector<MachineType>(numberOfInputs, graph()->zone());
  SparseInputMask dense = SparseInputMask::Dense();
  const ElementAccess ElementAccesses[] = {
      {kTaggedBase, kTaggedSize, Type::Any(), MachineType::AnyTagged(),
       kNoWriteBarrier},
      {kTaggedBase, kTaggedSize, Type::Any(), MachineType::TaggedSigned(),
       kNoWriteBarrier},
      {kTaggedBase, kTaggedSize, Type::Any(), MachineType::TaggedPointer(),
       kNoWriteBarrier}};

  // For every access
  for (size_t i = 0; i < arraysize(ElementAccesses); ++i) {
    // Create the graph
    Node* load1 =
        graph()->NewNode(simplified()->LoadElement(ElementAccesses[i]), object,
                         index, effect, control);
    Node* changeToTagged1 = graph()->NewNode(
        machine()->ChangeCompressedPointerToTaggedPointer(), load1);
    Node* load2 =
        graph()->NewNode(simplified()->LoadElement(ElementAccesses[i]), object,
                         index, effect, control);
    Node* changeToTagged2 = graph()->NewNode(
        machine()->ChangeCompressedPointerToTaggedPointer(), load2);
    Node* typedStateValuesOneDecompress =
        graph()->NewNode(common()->TypedStateValues(types, dense),
                         changeToTagged1, load1, changeToTagged2);

    // Reduce
    StrictMock<MockAdvancedReducerEditor> editor;
    DecompressionElimination decompression_elimination(&editor);
    EXPECT_CALL(editor, Replace(changeToTagged1, load1));
    EXPECT_CALL(editor, Replace(changeToTagged2, load2));
    Reduction r =
        decompression_elimination.Reduce(typedStateValuesOneDecompress);
    ASSERT_TRUE(r.Changed());
  }
}

TEST_F(DecompressionEliminationTest, TypedStateValuesAllDecompresses) {
  // Skip test if pointer compression is not enabled
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  const int numberOfInputs = 3;
  const ZoneVector<MachineType>* types =
      new (graph()->zone()->New(sizeof(ZoneVector<MachineType>)))
          ZoneVector<MachineType>(numberOfInputs, graph()->zone());
  SparseInputMask dense = SparseInputMask::Dense();
  const ElementAccess ElementAccesses[] = {
      {kTaggedBase, kTaggedSize, Type::Any(), MachineType::AnyTagged(),
       kNoWriteBarrier},
      {kTaggedBase, kTaggedSize, Type::Any(), MachineType::TaggedSigned(),
       kNoWriteBarrier},
      {kTaggedBase, kTaggedSize, Type::Any(), MachineType::TaggedPointer(),
       kNoWriteBarrier}};

  // For every access
  for (size_t i = 0; i < arraysize(ElementAccesses); ++i) {
    // Create the graph
    Node* load1 =
        graph()->NewNode(simplified()->LoadElement(ElementAccesses[i]), object,
                         index, effect, control);
    Node* changeToTagged1 = graph()->NewNode(
        machine()->ChangeCompressedPointerToTaggedPointer(), load1);
    Node* load2 =
        graph()->NewNode(simplified()->LoadElement(ElementAccesses[i]), object,
                         index, effect, control);
    Node* changeToTagged2 = graph()->NewNode(
        machine()->ChangeCompressedPointerToTaggedPointer(), load2);
    Node* load3 =
        graph()->NewNode(simplified()->LoadElement(ElementAccesses[i]), object,
                         index, effect, control);
    Node* changeToTagged3 = graph()->NewNode(
        machine()->ChangeCompressedPointerToTaggedPointer(), load3);
    Node* typedStateValuesOneDecompress =
        graph()->NewNode(common()->TypedStateValues(types, dense),
                         changeToTagged1, changeToTagged2, changeToTagged3);

    // Reduce
    StrictMock<MockAdvancedReducerEditor> editor;
    DecompressionElimination decompression_elimination(&editor);
    EXPECT_CALL(editor, Replace(changeToTagged1, load1));
    EXPECT_CALL(editor, Replace(changeToTagged2, load2));
    EXPECT_CALL(editor, Replace(changeToTagged3, load3));
    Reduction r =
        decompression_elimination.Reduce(typedStateValuesOneDecompress);
    ASSERT_TRUE(r.Changed());
  }
}

TEST_F(DecompressionEliminationTest, TypedStateValuesNoDecompresses) {
  // Skip test if pointer compression is not enabled
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  const int numberOfInputs = 3;
  const ZoneVector<MachineType>* types =
      new (graph()->zone()->New(sizeof(ZoneVector<MachineType>)))
          ZoneVector<MachineType>(numberOfInputs, graph()->zone());
  SparseInputMask dense = SparseInputMask::Dense();
  const ElementAccess ElementAccesses[] = {
      {kTaggedBase, kTaggedSize, Type::Any(), MachineType::AnyTagged(),
       kNoWriteBarrier},
      {kTaggedBase, kTaggedSize, Type::Any(), MachineType::TaggedSigned(),
       kNoWriteBarrier},
      {kTaggedBase, kTaggedSize, Type::Any(), MachineType::TaggedPointer(),
       kNoWriteBarrier}};

  // For every access
  for (size_t i = 0; i < arraysize(ElementAccesses); ++i) {
    // Create the graph
    Node* load = graph()->NewNode(simplified()->LoadElement(ElementAccesses[i]),
                                  object, index, effect, control);
    Node* typedStateValuesOneDecompress = graph()->NewNode(
        common()->TypedStateValues(types, dense), load, load, load);

    // Reduce
    StrictMock<MockAdvancedReducerEditor> editor;
    DecompressionElimination decompression_elimination(&editor);
    Reduction r =
        decompression_elimination.Reduce(typedStateValuesOneDecompress);
    ASSERT_FALSE(r.Changed());
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
