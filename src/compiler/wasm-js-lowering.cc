// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/wasm-js-lowering.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/wasm/wasm-code-manager.h"

namespace v8::internal::compiler {

WasmJSLowering::WasmJSLowering(Editor* editor, MachineGraph* mcgraph,
                               SourcePositionTable* source_position_table)
    : AdvancedReducer(editor),
      gasm_(mcgraph, mcgraph->zone()),
      mcgraph_(mcgraph),
      source_position_table_(source_position_table) {}

Reduction WasmJSLowering::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kTrapIf:
    case IrOpcode::kTrapUnless: {
      Node* effect = NodeProperties::GetEffectInput(node);
      Node* control = NodeProperties::GetControlInput(node);
      Node* trap_condition = NodeProperties::GetValueInput(node, 0);
      auto ool_trap = gasm_.MakeDeferredLabel();
      gasm_.InitializeEffectControl(effect, control);
      if (node->opcode() == IrOpcode::kTrapIf) {
        gasm_.GotoIf(trap_condition, &ool_trap);
      } else {
        DCHECK_EQ(node->opcode(), IrOpcode::kTrapUnless);
        gasm_.GotoIfNot(trap_condition, &ool_trap);
      }
      effect = gasm_.effect();
      control = gasm_.control();
      Node* goto_node = control;

      // Generate out of line code.
      gasm_.InitializeEffectControl(nullptr, nullptr);
      gasm_.Bind(&ool_trap);
      TrapId trap_id = TrapIdOf(node->op());
      Builtin trap = wasm::RuntimeStubIdToBuiltinName(
          static_cast<wasm::WasmCode::RuntimeStubId>(trap_id));

      Node* frame_state = NodeProperties::GetValueInput(node, 1);
      Node* call = gasm_.CallBuiltinWithFrameState(
          trap, Operator::kNoProperties, frame_state);
      UpdateSourcePosition(call, node);
      Node* terminate = mcgraph_->graph()->NewNode(
          mcgraph_->common()->Throw(), gasm_.effect(), gasm_.control());
      NodeProperties::MergeControlToEnd(mcgraph_->graph(), mcgraph_->common(),
                                        terminate);

      // Replace the trap node with the conditional branch.
      gasm_.InitializeEffectControl(effect, control);
      ReplaceWithValue(node, goto_node, gasm_.effect(), gasm_.control());
      node->Kill();
      return Replace(goto_node);
    }
    default:
      return NoChange();
  }
}

void WasmJSLowering::UpdateSourcePosition(Node* new_node, Node* old_node) {
  if (source_position_table_) {
    SourcePosition position =
        source_position_table_->GetSourcePosition(old_node);
    DCHECK(position.ScriptOffset() != kNoSourcePosition);
    source_position_table_->SetSourcePosition(new_node, position);
  }
}

}  // namespace v8::internal::compiler
