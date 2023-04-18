// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/address-reassociation.h"

#include "src/compiler/js-graph.h"
#include "src/compiler/machine-operator-reducer.h"
#include "src/compiler/node.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"
#include "testing/gmock-support.h"

using testing::_;
using testing::StrictMock;

namespace v8 {
namespace internal {
namespace compiler {

class AddressReassociationTest : public GraphTest {
 public:
  AddressReassociationTest()
      : GraphTest(3),
        machine_(zone()),
        javascript_(zone()),
        jsgraph_(isolate(), graph(), common(), &javascript_, nullptr,
                 &machine_),
        ar_(&jsgraph_, zone()) {}

  ~AddressReassociationTest() override = default;

 protected:
  MachineOperatorBuilder* machine() { return &machine_; }
  AddressReassociation* ar() { return &ar_; }
  Node* Int32Constant(int32_t value) {
    return graph()->NewNode(common()->Int32Constant(value));
  }
  Node* Int64Constant(int64_t value) {
    return graph()->NewNode(common()->Int64Constant(value));
  }
  Node* IntPtrConstant(intptr_t value) {
    return machine()->Is32() ? Int32Constant(static_cast<int32_t>(value))
                             : Int64Constant(static_cast<int64_t>(value));
  }
  Node* Int32Add(Node* lhs, Node* rhs) {
    return graph()->NewNode(machine()->Int32Add(), lhs, rhs);
  }
  Node* Int64Add(Node* lhs, Node* rhs) {
    return graph()->NewNode(machine()->Int64Add(), lhs, rhs);
  }
  Node* NativeAdd(Node* lhs, Node* rhs) {
    return machine()->Is32() ? Int32Add(lhs, rhs) : Int64Add(lhs, rhs);
  }
  Node* ExtendAdd(Node* lhs, Node* rhs) {
    Node* add = Int32Add(lhs, rhs);
    return graph()->NewNode(machine()->ChangeUint32ToUint64(), add);
  }
  bool IsNativeAdd(Node* node) {
    IrOpcode::Value opcode = node->opcode();
    if (machine()->Is32()) return opcode == IrOpcode::kInt32Add;
    return opcode == IrOpcode::kInt64Add;
  }

 private:
  MachineOperatorBuilder machine_;
  JSOperatorBuilder javascript_;
  JSGraph jsgraph_;
  AddressReassociation ar_;
};

namespace {

void TestAllEqual(std::vector<NodeId>& ids, unsigned long expected_length) {
  EXPECT_EQ(ids.size(), expected_length);
  EXPECT_TRUE(std::adjacent_find(ids.begin(), ids.end(),
                                 std::not_equal_to<>()) == ids.end());
}

}  // namespace

#define CHECK_LOAD(load_op)                              \
  Node* add = load_op->InputAt(0);                       \
  EXPECT_TRUE(IsNativeAdd(load_op->InputAt(0)));         \
  Node* base = add->InputAt(0);                          \
  base_addrs.push_back(base->id());                      \
  Node* reg_offset = add->InputAt(1);                    \
  EXPECT_EQ(base->opcode(), IrOpcode::kParameter);       \
  EXPECT_EQ(reg_offset->opcode(), IrOpcode::kParameter); \
  EXPECT_NE(base, reg_offset);                           \
  EXPECT_TRUE(NodeProperties::IsConstant(load_op->InputAt(1)));

TEST_F(AddressReassociationTest, ProtectedLoadBase) {
  Node* base = graph()->NewNode(common()->Parameter(0), graph()->start());
  Node* reg_offset = graph()->NewNode(common()->Parameter(1), graph()->start());
  Node* control = graph()->start();
  Node* effect = graph()->start();
  NodeId effect_id = effect->id();
  Node* load = nullptr;
  const Operator* protected_load_op =
      machine()->ProtectedLoad(MachineType::Int32());
  for (unsigned i = 0; i < 3; ++i) {
    Node* index = IntPtrConstant((i + 1) * 8);
    Node* object = NativeAdd(base, index);
    load = graph()->NewNode(protected_load_op, object, reg_offset, effect,
                            control);
    ar()->VisitLoad(load, effect_id);
    effect = load;
  }
  graph()->end()->InsertInput(zone(), 0, load);
  ar()->Optimize();

  std::vector<NodeId> base_addrs;
  while (load && load != graph()->start()) {
    CHECK_LOAD(load);
    load = NodeProperties::GetEffectInput(load, 0);
  }
  TestAllEqual(base_addrs, 3UL);
}

TEST_F(AddressReassociationTest, ProtectedLoadIndex) {
  Node* base = graph()->NewNode(common()->Parameter(0), graph()->start());
  Node* reg_offset = graph()->NewNode(common()->Parameter(1), graph()->start());
  Node* control = graph()->start();
  Node* effect = graph()->start();
  NodeId effect_id = effect->id();
  Node* load = nullptr;
  const Operator* protected_load_op =
      machine()->ProtectedLoad(MachineType::Int32());
  for (unsigned i = 0; i < 3; ++i) {
    Node* index = IntPtrConstant((i + 1) * 8);
    Node* add = NativeAdd(reg_offset, index);
    load = graph()->NewNode(protected_load_op, base, add, effect, control);
    ar()->VisitLoad(load, effect_id);
    effect = load;
  }
  graph()->end()->InsertInput(zone(), 0, load);
  ar()->Optimize();

  std::vector<NodeId> base_addrs;
  while (load && load != graph()->start()) {
    CHECK_LOAD(load);
    load = NodeProperties::GetEffectInput(load, 0);
  }
  TestAllEqual(base_addrs, 3UL);
}
#undef CHECK_LOAD

TEST_F(AddressReassociationTest, ProtectedLoadExtendIndex) {
  if (machine()->Is32()) return;
  Node* base = graph()->NewNode(common()->Parameter(0), graph()->start());
  Node* reg_offset = graph()->NewNode(common()->Parameter(1), graph()->start());
  Node* control = graph()->start();
  Node* effect = graph()->start();
  NodeId effect_id = effect->id();
  Node* load = nullptr;
  const Operator* protected_load_op =
      machine()->ProtectedLoad(MachineType::Int32());
  for (unsigned i = 0; i < 3; ++i) {
    Node* index = Int32Constant(8);
    Node* add = ExtendAdd(reg_offset, index);
    load = graph()->NewNode(protected_load_op, base, add, effect, control);
    ar()->VisitLoad(load, effect_id);
    effect = load;
  }
  graph()->end()->InsertInput(zone(), 0, load);
  ar()->Optimize();

  std::vector<NodeId> base_addrs;
  while (load && load != graph()->start()) {
    EXPECT_FALSE(NodeProperties::IsConstant(load->InputAt(1)));
    load = NodeProperties::GetEffectInput(load, 0);
  }
  TestAllEqual(base_addrs, 0UL);
}

#define INSERT_SEQUENTIAL_LOADS(N, effect_chain, control_in)                  \
  for (unsigned i = 0; i < N; ++i) {                                          \
    current_offset += 8 * (i + 1);                                            \
    Node* index = IntPtrConstant(current_offset);                             \
    Node* add = NativeAdd(base, index);                                       \
    load =                                                                    \
        graph()->NewNode(load_op, add, reg_offset, effect_chain, control_in); \
    ar()->VisitLoad(load, effect_id);                                         \
    effect_chain = load;                                                      \
  }                                                                           \
  effect = effect_chain;

#define CHECK_LOAD(load_op)                      \
  Node* add = load_op->InputAt(0);               \
  EXPECT_TRUE(IsNativeAdd(load_op->InputAt(0))); \
  Node* base = add->InputAt(0);                  \
  base_addrs.push_back(base->id());              \
  EXPECT_TRUE(NodeProperties::IsConstant(load_op->InputAt(1)));

TEST_F(AddressReassociationTest, Diamond) {
  // start
  //   3 loads
  //   branch
  // if_true
  //   3 loads
  // if_false
  //   3 loads
  // merge
  //   3 loads
  Node* control = graph()->start();
  Node* effect = graph()->start();
  NodeId effect_id = effect->id();
  Node* check = Parameter(Type::Boolean(), 1);
  Node* load = nullptr;
  unsigned current_offset = 0;
  auto load_op = machine()->ProtectedLoad(MachineType::Int32());

  Node* base = graph()->NewNode(common()->Parameter(0), graph()->start());
  Node* reg_offset = graph()->NewNode(common()->Parameter(1), graph()->start());

  INSERT_SEQUENTIAL_LOADS(3, effect, control);
  Node* branch = graph()->NewNode(common()->Branch(), check, control);
  Node* etrue = effect;
  Node* efalse = effect;

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  INSERT_SEQUENTIAL_LOADS(3, etrue, if_true);
  etrue = effect;

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  INSERT_SEQUENTIAL_LOADS(3, efalse, if_false);
  efalse = effect;

  control = graph()->NewNode(common()->Merge(2), if_true, if_false);
  Node* effect_phi =
      graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
  effect = effect_phi;
  effect_id = effect->id();
  INSERT_SEQUENTIAL_LOADS(3, effect, control);

  ar()->Optimize();

  // end to effect phi: 3 loads sharing base address.
  std::vector<NodeId> base_addrs;
  while (load && load != effect_phi) {
    CHECK_LOAD(load);
    load = NodeProperties::GetEffectInput(load, 0);
  }
  TestAllEqual(base_addrs, 3UL);

  // effect phi to start: 9 loads sharing base address.
  base_addrs.clear();
  std::vector<Node*> loads;
  std::set<NodeId> all_visited;
  loads.push_back(effect_phi->InputAt(0));
  loads.push_back(effect_phi->InputAt(1));
  while (!loads.empty()) {
    load = loads.back();
    loads.pop_back();
    if (load == graph()->start() || all_visited.count(load->id())) continue;
    CHECK_LOAD(load);
    all_visited.insert(load->id());
    loads.push_back(NodeProperties::GetEffectInput(load, 0));
  }
  TestAllEqual(base_addrs, 9UL);
}
#undef CHECK_LOAD
#undef INSERT_SEQUENTIAL_LOADS

}  // namespace compiler
}  // namespace internal
}  // namespace v8
