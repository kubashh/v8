// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/baseline/jump-target-analysis.h"

#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecodes.h"

namespace v8 {
namespace internal {
namespace baseline {

JumpTargetAnalysis::JumpTargetAnalysis(Zone* zone,
                                       Handle<BytecodeArray> bytecode_array)
    : bytecode_array_(bytecode_array), labels_(zone) {}

void JumpTargetAnalysis::Analyse(
    CodeStubAssembler* assembler,
    const compiler::CodeAssemblerVariableList& merged_variables) {
  HandlerTable handler_table(*bytecode_array());
  std::map<int, int, std::less<int>> jump_targets;

  // Iterate through the bytecode to find all jump, switch and loop targets.
  interpreter::BytecodeArrayIterator iterator(bytecode_array());
  for (; !iterator.done(); iterator.Advance()) {
    if (interpreter::Bytecodes::IsJump(iterator.current_bytecode())) {
      auto it = jump_targets.emplace(iterator.GetJumpTargetOffset(), 0);
      it.first->second++;
    } else if (iterator.current_bytecode() ==
               interpreter::Bytecode::kSwitchOnSmiNoFeedback) {
      interpreter::JumpTableTargetOffsets offsets =
          iterator.GetJumpTableTargetOffsets();
      for (const auto& entry : offsets) {
        auto it = jump_targets.emplace(entry.target_offset, 0);
        it.first->second++;
      }
    }
  }
  // Add the locations of exception handlers, and explicitly add labels for them
  // since they are always reachable.
  int num_entries = handler_table.NumberOfRangeEntries();
  for (int i = 0; i < num_entries; i++) {
    int handler_offset = handler_table.GetRangeHandler(i);
    auto it = jump_targets.emplace(handler_offset, 0);
    it.first->second++;
    labels_.emplace(handler_offset, Label(assembler, merged_variables));
  }

  // Iterate again and create labels for all targets that aren't dead.
  iterator.SetOffset(0);
  std::set<int> dead_jump_targets;
  auto next_target_it = jump_targets.begin();
  bool exit_seen_in_block = false;
  for (; !iterator.done(); iterator.Advance()) {
    if (next_target_it != jump_targets.end() &&
        next_target_it->first == iterator.current_offset()) {
      if (next_target_it->second > 0) {
        exit_seen_in_block = false;
      }
      next_target_it++;
    }

    // Don't create labels that will never be jumped too.
    if (!exit_seen_in_block) {
      if (interpreter::Bytecodes::IsJump(iterator.current_bytecode())) {
        int jump_target = iterator.GetJumpTargetOffset();
        labels_.emplace(jump_target, Label(assembler, merged_variables));
      } else if (iterator.current_bytecode() ==
                 interpreter::Bytecode::kSwitchOnSmiNoFeedback) {
        interpreter::JumpTableTargetOffsets offsets =
            iterator.GetJumpTableTargetOffsets();
        for (const auto& entry : offsets) {
          labels_.emplace(entry.target_offset,
                          Label(assembler, merged_variables));
        }
      }
    } else {
      // Subtract jump target count to potentially mark as dead.
      if (interpreter::Bytecodes::IsJump(iterator.current_bytecode())) {
        jump_targets[iterator.GetJumpTargetOffset()]--;
        DCHECK_LE(0, jump_targets[iterator.GetJumpTargetOffset()]);
      } else if (iterator.current_bytecode() ==
                 interpreter::Bytecode::kSwitchOnSmiNoFeedback) {
        interpreter::JumpTableTargetOffsets offsets =
            iterator.GetJumpTableTargetOffsets();
        for (const auto& entry : offsets) {
          jump_targets[entry.target_offset]--;
          DCHECK_LE(0, jump_targets[entry.target_offset]);
        }
      }
    }

    if (interpreter::Bytecodes::UnconditionallyExitsBasicBlock(
            iterator.current_bytecode())) {
      exit_seen_in_block = true;
    }
  }
  DCHECK_EQ(next_target_it, jump_targets.end());
}

CodeStubAssembler::Label* JumpTargetAnalysis::LabelForTarget(int target) {
  auto it = labels_.find(target);
  DCHECK(it != labels_.end());
  return &it->second;
}

}  // namespace baseline
}  // namespace internal
}  // namespace v8
