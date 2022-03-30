// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/decompression-optimization.h"

#include "src/codegen/machine-type.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/reducer.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

namespace {

struct DecompressionAnalyzer : AnalyzerBase {
  using Base = AnalyzerBase;
  std::vector<uint8_t> needs_decompression;

  DecompressionAnalyzer(const Graph& graph, Zone* zone)
      : AnalyzerBase(graph, zone), needs_decompression(graph.op_id_count()) {}

  void Run() {
    for (size_t next_block_id = graph.block_count() - 1; next_block_id > 0;) {
      BlockIndex block_index = static_cast<BlockIndex>(next_block_id);
      --next_block_id;
      if (!BlockReachable(block_index)) continue;
      const Block& block = graph.Get(block_index);
      if (block.IsLoop()) {
        ProcessBlock<true>(block, &next_block_id);
      } else {
        ProcessBlock<false>(block, &next_block_id);
      }
    }
  }

  bool NeedsDecompression(OpIndex i) { return needs_decompression[i.id()]; }
  bool NeedsDecompression(const Operation& op) {
    return NeedsDecompression(graph.Index(op));
  }
  bool MarkAsNeedsDecompression(OpIndex i) {
    return needs_decompression[i.id()] = true;
  }

  template <bool is_loop>
  void ProcessBlock(const Block& block, size_t* next_block_id) {
    for (const Operation& op : base::Reversed(graph.operations(block))) {
      if (is_loop && op.Is<PhiOp>() && NeedsDecompression(op)) {
        const PhiOp& phi = op.Cast<PhiOp>();
        if (!NeedsDecompression(phi.inputs()[1])) {
          Block* backedge = block.predecessors[1];
          *next_block_id = std::max<size_t>(*next_block_id,
                                            ToUnderlyingType(backedge->index));
        }
      }
      ProcessOperation(op);
    }
  }
  void ProcessOperation(const Operation& op);
};

void DecompressionAnalyzer::ProcessOperation(const Operation& op) {
  switch (op.opcode) {
    case Opcode::kStore: {
      auto& store = op.Cast<StoreOp>();
      MarkAsNeedsDecompression(store.base());
      if (!IsAnyTagged(store.stored_rep))
        MarkAsNeedsDecompression(store.value());
      break;
    }
    case Opcode::kIndexedStore: {
      auto& store = op.Cast<IndexedStoreOp>();
      MarkAsNeedsDecompression(store.base());
      MarkAsNeedsDecompression(store.index());
      if (!IsAnyTagged(store.stored_rep))
        MarkAsNeedsDecompression(store.value());
      break;
    }
    case Opcode::kFrameState:
      // The deopt code knows how to handle Compressed inputs, both
      // MachineRepresentation kCompressed values and CompressedHeapConstants.
      break;
    case Opcode::kPhi: {
      // Replicate the phi's state for its inputs.
      auto& phi = op.Cast<PhiOp>();
      if (NeedsDecompression(op)) {
        for (OpIndex input : phi.inputs()) {
          MarkAsNeedsDecompression(input);
        }
      }
      break;
    }
    case Opcode::kEqual: {
      auto& equal = op.Cast<EqualOp>();
      if (equal.rep == MachineRepresentation::kWord64) {
        MarkAsNeedsDecompression(equal.left());
        MarkAsNeedsDecompression(equal.right());
      }
      break;
    }
    case Opcode::kComparison: {
      auto& comp = op.Cast<ComparisonOp>();
      if (comp.rep == MachineRepresentation::kWord64) {
        MarkAsNeedsDecompression(comp.left());
        MarkAsNeedsDecompression(comp.right());
      }
      break;
    }
    case Opcode::kBinary: {
      auto& binary_op = op.Cast<BinaryOp>();
      if (binary_op.rep == MachineRepresentation::kWord64) {
        MarkAsNeedsDecompression(binary_op.left());
        MarkAsNeedsDecompression(binary_op.right());
      }
      break;
    }
    case Opcode::kShift: {
      auto& shift_op = op.Cast<ShiftOp>();
      if (shift_op.rep == MachineRepresentation::kWord64) {
        MarkAsNeedsDecompression(shift_op.left());
      }
      break;
    }
    case Opcode::kChange: {
      auto& change = op.Cast<ChangeOp>();
      if (change.to == MachineRepresentation::kWord64 &&
          NeedsDecompression(op)) {
        MarkAsNeedsDecompression(change.input());
      }
      break;
    }
    case Opcode::kTaggedBitcast: {
      auto& bitcast = op.Cast<TaggedBitcastOp>();
      if (NeedsDecompression(op)) {
        MarkAsNeedsDecompression(bitcast.input());
      }
      break;
    }
    default:
      for (OpIndex input : op.inputs()) {
        MarkAsNeedsDecompression(input);
      }
      break;
  }
}

}  // namespace

void RunDecompressionOptimization(Graph& graph, Zone* phase_zone) {
  DecompressionAnalyzer analyzer(graph, phase_zone);
  analyzer.Run();
  for (Operation& op : graph.AllOperations()) {
    if (analyzer.NeedsDecompression(op)) continue;
    switch (op.opcode) {
      case Opcode::kConstant: {
        auto& constant = op.Cast<ConstantOp>();
        if (constant.kind == ConstantOp::Kind::kHeapObject) {
          constant.kind = ConstantOp::Kind::kCompressedHeapObject;
        }
        break;
      }
      case Opcode::kPhi: {
        auto& phi = op.Cast<PhiOp>();
        if (phi.rep == MachineRepresentation::kTagged) {
          phi.rep = MachineRepresentation::kCompressed;
        } else if (phi.rep == MachineRepresentation::kTaggedPointer) {
          phi.rep = MachineRepresentation::kCompressedPointer;
        }
        break;
      }
      case Opcode::kLoad: {
        auto& load = op.Cast<LoadOp>();
        if (load.loaded_rep == MachineType::AnyTagged()) {
          load.loaded_rep = MachineType::AnyCompressed();
        } else if (load.loaded_rep == MachineType::TaggedPointer()) {
          load.loaded_rep = MachineType::CompressedPointer();
        }
        break;
      }
      case Opcode::kIndexedLoad: {
        auto& load = op.Cast<IndexedLoadOp>();
        if (load.loaded_rep == MachineType::AnyTagged()) {
          load.loaded_rep = MachineType::AnyCompressed();
        } else if (load.loaded_rep == MachineType::TaggedPointer()) {
          load.loaded_rep = MachineType::CompressedPointer();
        }
        break;
      }
      default:
        break;
    }
  }
}

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8
