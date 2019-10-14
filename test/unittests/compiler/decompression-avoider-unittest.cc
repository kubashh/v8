// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/decompression-avoider.h"

#include "test/unittests/compiler/graph-unittest.h"

namespace v8 {
namespace internal {
namespace compiler {

class DecompressionAvoiderTest : public GraphTest {
 public:
  DecompressionAvoiderTest()
      : GraphTest(),
        machine_(zone(), MachineType::PointerRepresentation(),
                 MachineOperatorBuilder::kNoFlags) {}
  ~DecompressionAvoiderTest() override = default;

 protected:
  void ChangeLoads() {
    DecompressionAvoider decompression_avoider(zone(), graph(), machine());
    decompression_avoider.ChangeLoads();
  }

  MachineRepresentation CompressedMachRep(MachineRepresentation mach_rep) {
    if (mach_rep == MachineRepresentation::kTagged) {
      return MachineRepresentation::kCompressed;
    } else {
      DCHECK_EQ(mach_rep, MachineRepresentation::kTaggedPointer);
      return MachineRepresentation::kCompressedPointer;
    }
  }

  MachineRepresentation CompressedMachRep(MachineType type) {
    return CompressedMachRep(type.representation());
  }

  MachineRepresentation LoadMachRep(Node* node) {
    return LoadRepresentationOf(node->op()).representation();
  }

  const MachineType types[2] = {MachineType::AnyTagged(),
                                MachineType::TaggedPointer()};

  StoreRepresentation CreateStoreRep(MachineType type) {
    return StoreRepresentation(type.representation(),
                               WriteBarrierKind::kFullWriteBarrier);
  }

  MachineOperatorBuilder* machine() { return &machine_; }

 private:
  MachineOperatorBuilder machine_;
};

// -----------------------------------------------------------------------------
// Direct Load into Store.

TEST_F(DecompressionAvoiderTest, DirectLoadStore) {
  // Skip test if pointer compression is not enabled, or decompression
  // elimination disabled.
  if (!COMPRESS_POINTERS_BOOL || FLAG_turbo_decompression_elimination) {
    return;
  }

  // Define variables.
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);

  // Test for both AnyTagged and TaggedPointer.
  for (size_t i = 0; i < arraysize(types); ++i) {
    // Create the graph.
    Node* base_pointer = graph()->NewNode(machine()->Load(types[i]), object,
                                          index, effect, control);
    Node* value = graph()->NewNode(machine()->Load(types[i]), base_pointer,
                                   index, effect, control);
    graph()->SetEnd(graph()->NewNode(machine()->Store(CreateStoreRep(types[i])),
                                     object, index, value, effect, control));

    // Change the loads, and test the change.
    ChangeLoads();
    EXPECT_EQ(LoadMachRep(base_pointer), types[i].representation());
    EXPECT_EQ(LoadMachRep(value), CompressedMachRep(types[i]));
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
