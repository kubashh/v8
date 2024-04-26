// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/phase.h"

#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/graph-visualizer.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/turboshaft/graph-visualizer.h"
#include "src/compiler/turboshaft/pipelines.h"
#include "src/compiler/pipeline-data-inl.h"
#include "src/diagnostics/code-tracer.h"
#include "src/utils/ostreams.h"

namespace v8::internal::compiler::turboshaft {

compiler::Schedule* PipelineData::schedule() const  {
  return turbofan_data_->schedule();
}

void PipelineData::reset_schedule()  {
  turbofan_data_->reset_schedule();
}

ZoneWithNamePointer<Frame, kCodegenZoneName> PipelineData::frame() const {
  return turbofan_data_->frame();
}

ZoneWithName<kInstructionZoneName>& PipelineData::instruction_zone() {
  return turbofan_data_->instruction_zone();
}

ZoneWithNamePointer<InstructionSequence, kInstructionZoneName> PipelineData::sequence()
      const {
  return turbofan_data_->sequence();
}

void PipelineData::InitializeInstructionSequence(const CallDescriptor* call_descriptor) {
  DCHECK(!turbofan_data_->instruction_data_.has_value());
  turbofan_data_->instruction_data_.emplace(turbofan_data_->zone_stats());
  InstructionBlocks* instruction_blocks =
      InstructionSequence::InstructionBlocksFor(instruction_zone(), *graph_);
  InstructionSequence* sequence = instruction_zone().New<InstructionSequence>(
        isolate(), instruction_zone(), instruction_blocks);
  if (call_descriptor && call_descriptor->RequiresFrameAsIncoming()) {
      sequence->instruction_blocks()[0]->mark_needs_frame();
  } else {
      DCHECK(call_descriptor->CalleeSavedFPRegisters().is_empty());
  }
  turbofan_data_->instruction_data_->InitializeFromSequence(sequence);
}

void PrintTurboshaftGraph(Zone* temp_zone, OptimizedCompilationInfo* info,
                          JSHeapBroker* broker, const Graph& graph,
                          const NodeOriginTable* node_origins,
                          CodeTracer* code_tracer, const char* phase_name) {
  if (info->trace_turbo_json()) {
    UnparkedScopeIfNeeded scope(broker);
    AllowHandleDereference allow_deref;

    TurboJsonFile json_of(info, std::ios_base::app);
    // TODO(nicohartmann): Should propagate constness.
    PrintTurboshaftGraphForTurbolizer(
        json_of, graph, phase_name, const_cast<NodeOriginTable*>(node_origins),
        temp_zone);
  }

  if (info->trace_turbo_graph()) {
    DCHECK(code_tracer);
    UnparkedScopeIfNeeded scope(broker);
    AllowHandleDereference allow_deref;

    CodeTracer::StreamScope tracing_scope(code_tracer);
    tracing_scope.stream() << "\n----- " << phase_name << " -----\n" << graph;
  }
}

void PrintTurboshaftGraph(Zone* temp_zone, CodeTracer* code_tracer,
                          const char* phase_name) {
  const PipelineData& data = PipelineData::Get();
  PrintTurboshaftGraph(temp_zone, data.info(), data.broker(), data.graph(),
                       data.node_origins(), code_tracer, phase_name);
}

void PrintTurboshaftGraph(DataComponentProvider* data_provider, Zone* temp_zone,
                          CodeTracer* code_tracer, const char* phase_name) {
  CompilationData& compilation_data =
      data_provider->GetDataComponent<CompilationData>();
  GraphData& graph_data = data_provider->GetDataComponent<GraphData>();
  PrintTurboshaftGraph(temp_zone, compilation_data.info, nullptr,
                       *graph_data.graph, nullptr, code_tracer, phase_name);
}

void PrintTurboshaftGraphForTurbolizer(std::ofstream& stream,
                                       const Graph& graph,
                                       const char* phase_name,
                                       NodeOriginTable* node_origins,
                                       Zone* temp_zone) {
  stream << "{\"name\":\"" << phase_name
         << "\",\"type\":\"turboshaft_graph\",\"data\":"
         << AsJSON(graph, node_origins, temp_zone) << "},\n";

  PrintTurboshaftCustomDataPerOperation(
      stream, "Properties", graph,
      [](std::ostream& stream, const turboshaft::Graph& graph,
         turboshaft::OpIndex index) -> bool {
        const auto& op = graph.Get(index);
        op.PrintOptions(stream);
        return true;
      });
  PrintTurboshaftCustomDataPerOperation(
      stream, "Types", graph,
      [](std::ostream& stream, const turboshaft::Graph& graph,
         turboshaft::OpIndex index) -> bool {
        turboshaft::Type type = graph.operation_types()[index];
        if (!type.IsInvalid() && !type.IsNone()) {
          type.PrintTo(stream);
          return true;
        }
        return false;
      });
  PrintTurboshaftCustomDataPerOperation(
      stream, "Representations", graph,
      [](std::ostream& stream, const turboshaft::Graph& graph,
         turboshaft::OpIndex index) -> bool {
        const Operation& op = graph.Get(index);
        stream << PrintCollection(op.outputs_rep());
        return true;
      });
  PrintTurboshaftCustomDataPerOperation(
      stream, "Use Count (saturated)", graph,
      [](std::ostream& stream, const turboshaft::Graph& graph,
         turboshaft::OpIndex index) -> bool {
        stream << static_cast<int>(graph.Get(index).saturated_use_count.Get());
        return true;
      });
#ifdef DEBUG
  PrintTurboshaftCustomDataPerBlock(
      stream, "Type Refinements", graph,
      [](std::ostream& stream, const turboshaft::Graph& graph,
         turboshaft::BlockIndex index) -> bool {
        const std::vector<std::pair<turboshaft::OpIndex, turboshaft::Type>>&
            refinements = graph.block_type_refinement()[index];
        if (refinements.empty()) return false;
        stream << "\\n";
        for (const auto& [op, type] : refinements) {
          stream << op << " : " << type << "\\n";
        }
        return true;
      });
#endif  // DEBUG
}

}  // namespace v8::internal::compiler::turboshaft

EXPORT_CONTEXTUAL_VARIABLE(v8::internal::compiler::turboshaft::PipelineData)
