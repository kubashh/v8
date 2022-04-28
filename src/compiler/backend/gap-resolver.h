// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_GAP_RESOLVER_H_
#define V8_COMPILER_BACKEND_GAP_RESOLVER_H_

#include "src/compiler/backend/instruction.h"

namespace v8 {
namespace internal {
namespace compiler {

class GapResolver final {
 public:
  // Interface used by the gap resolver to emit moves and swaps.
  class Assembler {
   public:
    virtual ~Assembler() = default;

    // Move an operand to a (unique) temporary location to break a move cycle.
    virtual void MoveToTempLocation(InstructionOperand* src) = 0;
    // Resolve the cycle by moving the temporary location to its destination.
    virtual void MoveTempLocationTo(InstructionOperand* dst,
                                    MachineRepresentation rep) = 0;
    // On platforms where a scratch register is available, we want to use that
    // as the temporary location. However, one of the pending moves might also
    // require the temp register (e.g. stack-to-stack move).
    // Detect such conflict with the following function, and choose the temp
    // location appropriately later.
    virtual void SetPendingMove(MoveOperands* move) = 0;
    // Reset the scratch register state after a move cycle.
    virtual void ResetPendingMoves() = 0;

    // Assemble move.
    virtual void AssembleMove(InstructionOperand* source,
                              InstructionOperand* destination) = 0;
  };

  explicit GapResolver(Assembler* assembler)
      : assembler_(assembler), split_rep_(MachineRepresentation::kSimd128) {}

  // Resolve a set of parallel moves, emitting assembler instructions.
  V8_EXPORT_PRIVATE void Resolve(ParallelMove* parallel_move);

 private:
  // Performs the given move, possibly performing other moves to unblock the
  // destination operand.
  void PerformMove(ParallelMove* moves, MoveOperands* move);
  void PerformMoveHelper(ParallelMove* moves, MoveOperands* move,
                         MoveOperands** deferred_out);

  // Assembler used to emit moves and save registers.
  Assembler* const assembler_;

  // While resolving moves, the largest FP representation that can be moved.
  // Any larger moves must be split into an equivalent series of moves of this
  // representation.
  MachineRepresentation split_rep_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_GAP_RESOLVER_H_
