// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/backend/gap-resolver.h"

#include <algorithm>
#include <set>

#include "src/base/enum-set.h"
#include "src/codegen/register-configuration.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

// Splits a FP move between two location operands into the equivalent series of
// moves between smaller sub-operands, e.g. a double move to two single moves.
// This helps reduce the number of cycles that would normally occur under FP
// aliasing, and makes swaps much easier to implement.
MoveOperands* Split(MoveOperands* move, MachineRepresentation smaller_rep,
                    ParallelMove* moves) {
  DCHECK(kFPAliasing == AliasingKind::kCombine);
  // Splitting is only possible when the slot size is the same as float size.
  DCHECK_EQ(kSystemPointerSize, kFloatSize);
  const LocationOperand& src_loc = LocationOperand::cast(move->source());
  const LocationOperand& dst_loc = LocationOperand::cast(move->destination());
  MachineRepresentation dst_rep = dst_loc.representation();
  DCHECK_NE(smaller_rep, dst_rep);
  auto src_kind = src_loc.location_kind();
  auto dst_kind = dst_loc.location_kind();

  int aliases =
      1 << (ElementSizeLog2Of(dst_rep) - ElementSizeLog2Of(smaller_rep));
  int base = -1;
  USE(base);
  DCHECK_EQ(aliases, RegisterConfiguration::Default()->GetAliases(
                         dst_rep, 0, smaller_rep, &base));

  int src_index = -1;
  int slot_size = (1 << ElementSizeLog2Of(smaller_rep)) / kSystemPointerSize;
  int src_step = 1;
  if (src_kind == LocationOperand::REGISTER) {
    src_index = src_loc.register_code() * aliases;
  } else {
    src_index = src_loc.index();
    // For operands that occupy multiple slots, the index refers to the last
    // slot. On little-endian architectures, we start at the high slot and use a
    // negative step so that register-to-slot moves are in the correct order.
    src_step = -slot_size;
  }
  int dst_index = -1;
  int dst_step = 1;
  if (dst_kind == LocationOperand::REGISTER) {
    dst_index = dst_loc.register_code() * aliases;
  } else {
    dst_index = dst_loc.index();
    dst_step = -slot_size;
  }

  // Reuse 'move' for the first fragment. It is not pending.
  move->set_source(AllocatedOperand(src_kind, smaller_rep, src_index));
  move->set_destination(AllocatedOperand(dst_kind, smaller_rep, dst_index));
  // Add the remaining fragment moves.
  for (int i = 1; i < aliases; ++i) {
    src_index += src_step;
    dst_index += dst_step;
    moves->AddMove(AllocatedOperand(src_kind, smaller_rep, src_index),
                   AllocatedOperand(dst_kind, smaller_rep, dst_index));
  }
  // Return the first fragment.
  return move;
}

enum MoveOperandKind : uint8_t { kConstant, kGpReg, kFpReg, kStack };

MoveOperandKind GetKind(const InstructionOperand& move) {
  if (move.IsConstant()) return kConstant;
  LocationOperand loc_op = LocationOperand::cast(move);
  if (loc_op.location_kind() != LocationOperand::REGISTER) return kStack;
  return IsFloatingPoint(loc_op.representation()) ? kFpReg : kGpReg;
}

}  // namespace

void GapResolver::Resolve(ParallelMove* moves) {
  base::EnumSet<MoveOperandKind, uint8_t> source_kinds;
  base::EnumSet<MoveOperandKind, uint8_t> destination_kinds;

  // Remove redundant moves, collect source kinds and destination kinds to
  // detect simple non-overlapping moves, and collect FP move representations if
  // aliasing is non-simple.
  int fp_reps = 0;
  size_t nmoves = moves->size();
  for (size_t i = 0; i < nmoves;) {
    MoveOperands* move = (*moves)[i];
    if (move->IsRedundant()) {
      nmoves--;
      if (i < nmoves) (*moves)[i] = (*moves)[nmoves];
      continue;
    }
    i++;
    source_kinds.Add(GetKind(move->source()));
    destination_kinds.Add(GetKind(move->destination()));
    if (kFPAliasing == AliasingKind::kCombine &&
        move->destination().IsFPRegister()) {
      fp_reps |= RepresentationBit(
          LocationOperand::cast(move->destination()).representation());
    }
  }
  if (nmoves != moves->size()) moves->resize(nmoves);

  if ((source_kinds & destination_kinds).empty() || moves->size() < 2) {
    // Fast path for non-conflicting parallel moves.
    for (MoveOperands* move : *moves) {
      assembler_->AssembleMove(&move->source(), &move->destination());
    }
    return;
  }

  if (kFPAliasing == AliasingKind::kCombine) {
    if (fp_reps && !base::bits::IsPowerOfTwo(fp_reps)) {
      // Start with the smallest FP moves, so we never encounter smaller moves
      // in the middle of a cycle of larger moves.
      if ((fp_reps & RepresentationBit(MachineRepresentation::kFloat32)) != 0) {
        split_rep_ = MachineRepresentation::kFloat32;
        for (size_t i = 0; i < moves->size(); ++i) {
          auto move = (*moves)[i];
          if (!move->IsEliminated() && move->destination().IsFloatRegister())
            PerformMove(moves, move);
        }
      }
      if ((fp_reps & RepresentationBit(MachineRepresentation::kFloat64)) != 0) {
        split_rep_ = MachineRepresentation::kFloat64;
        for (size_t i = 0; i < moves->size(); ++i) {
          auto move = (*moves)[i];
          if (!move->IsEliminated() && move->destination().IsDoubleRegister())
            PerformMove(moves, move);
        }
      }
    }
    split_rep_ = MachineRepresentation::kSimd128;
  }

  for (size_t i = 0; i < moves->size(); ++i) {
    auto move = (*moves)[i];
    if (!move->IsEliminated()) PerformMove(moves, move);
  }
}

void GapResolver::PerformMove(ParallelMove* moves, MoveOperands* move) {
  auto cycle = PerformMoveHelper(moves, move);
  if (!cycle.has_value()) return;
  DCHECK_EQ(cycle->back(), move);
  if (cycle->size() == 2) {
    MoveOperands* move2 = cycle->front();
    MachineRepresentation rep =
        LocationOperand::cast(move->source()).representation();
    MachineRepresentation rep2 =
        LocationOperand::cast(move2->source()).representation();
    if (rep == rep2) {
      InstructionOperand* source = &move->source();
      InstructionOperand* destination = &move->destination();
      // Ensure source is a register or both are stack slots, to limit swap
      // cases.
      if (move->source().IsAnyStackSlot()) {
        std::swap(source, destination);
      }
      assembler_->AssembleSwap(source, destination);
      move->Eliminate();
      move2->Eliminate();
      return;
    }
  }
  MachineRepresentation rep =
      LocationOperand::cast(move->source()).representation();
  cycle->pop_back();
  for (auto* move : *cycle) {
    assembler_->SetPendingMove(move);
  }
  assembler_->MoveToTempLocation(&move->source());
  InstructionOperand destination = move->destination();
  move->Eliminate();
  for (auto* move : *cycle) {
    assembler_->AssembleMove(&move->source(), &move->destination());
    move->Eliminate();
  }
  assembler_->MoveTempLocationTo(&destination, rep);
}

base::Optional<std::vector<MoveOperands*>> GapResolver::PerformMoveHelper(
    ParallelMove* moves, MoveOperands* move) {
  // We first recursively perform any move blocking this one.  We mark a move as
  // "pending" on entry to PerformMove in order to detect cycles in the move
  // graph. If there is a cycle, we move one of the operands to a temporary
  // location to break the dependency and resolve the cycle. When the move and
  // all of its dependencies have been assembled, place the temporary location
  // back into its destination.
  DCHECK(!move->IsPending());
  DCHECK(!move->IsRedundant());

  // Clear this move's destination to indicate a pending move.  The actual
  // destination is saved on the side.
  InstructionOperand source = move->source();
  DCHECK(!source.IsInvalid());  // Or else it will look eliminated.
  InstructionOperand destination = move->destination();
  move->SetPending();
  base::Optional<std::vector<MoveOperands*>> cycle;

  // We may need to split moves between FP locations differently.
  const bool is_fp_loc_move = kFPAliasing == AliasingKind::kCombine &&
                              destination.IsFPLocationOperand();

  // Perform a depth-first traversal of the move graph to resolve dependencies.
  // Any unperformed, unpending move with a source the same as this one's
  // destination blocks this one so recursively perform all such moves.
  for (size_t i = 0; i < moves->size(); ++i) {
    auto other = (*moves)[i];
    if (other->IsEliminated()) continue;
    if (other == move) continue;
    if (other->source().InterferesWith(destination)) {
      if (other->IsPending()) {
        // The conflicting move is pending, i.e. we found a cycle. Break it by
        // moving the source to a platform-dependent temporary location.
        // Check that we have at most one blocker. This assumption will have to
        // be revisited for tail-calls, which create more complex interferences.
        // DCHECK_NULL(*deferred_move_out);
        // assembler_->MoveToTempLocation(&other->source());
        cycle = std::vector<MoveOperands*>();
      } else {
        // Recursively perform the conflicting move.
        if (is_fp_loc_move &&
            LocationOperand::cast(other->source()).representation() >
                split_rep_) {
          // 'other' must also be an FP location move. Break it into fragments
          // of the same size as 'move'. 'other' is set to one of the fragments,
          // and the rest are appended to 'moves'.
          other = Split(other, split_rep_, moves);
          // 'other' may not block destination now.
          if (!other->source().InterferesWith(destination)) continue;
        }
        auto cycle_rec = PerformMoveHelper(moves, other);
        if (cycle_rec.has_value()) {
          DCHECK(!cycle.has_value());
          cycle = cycle_rec;
        }
      }
    }
  }

  // We are about to resolve this move and don't need it marked as pending, so
  // restore its destination.
  move->set_destination(destination);

  if (cycle.has_value()) {
    cycle->push_back(move);
    return cycle;
  }

  assembler_->AssembleMove(&source, &destination);
  move->Eliminate();
  return {};
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
