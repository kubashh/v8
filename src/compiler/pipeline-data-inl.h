
// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_PIPELINE_DATA_INL_H_
#define V8_COMPILER_PIPELINE_DATA_INL_H_

#include "src/builtins/profile-data-reader.h"
#include "src/codegen/assembler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/common/globals.h"
#include "src/compiler/backend/code-generator.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/backend/register-allocator.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/graph-visualizer.h"
#include "src/compiler/js-context-specialization.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/machine-graph.h"
#include "src/compiler/node-observer.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/phase.h"
#include "src/compiler/pipeline-statistics.h"
#include "src/compiler/schedule.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/pipelines.h"
#include "src/compiler/turboshaft/zone-with-name.h"
#include "src/compiler/typer.h"
#include "src/compiler/zone-stats.h"
#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/objects/objects-inl.h"
#include "src/zone/accounting-allocator.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-engine.h"
#endif

namespace v8::internal::compiler {

using turboshaft::ZoneWithName;
using turboshaft::ZoneWithNamePointer;

inline Maybe<OuterContext> GetModuleContext(OptimizedCompilationInfo* info) {
  Tagged<Context> current = info->closure()->context();
  size_t distance = 0;
  while (!IsNativeContext(*current)) {
    if (current->IsModuleContext()) {
      return Just(OuterContext(
          info->CanonicalHandle(current, current->GetIsolate()), distance));
    }
    current = current->previous();
    distance++;
  }
  return Nothing<OuterContext>();
}

inline std::unique_ptr<TurbofanPipelineStatistics> CreatePipelineStatistics(
    Handle<Script> script, OptimizedCompilationInfo* info, Isolate* isolate,
    ZoneStats* zone_stats) {
  std::unique_ptr<TurbofanPipelineStatistics> pipeline_statistics;

  bool tracing_enabled;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED(TRACE_DISABLED_BY_DEFAULT("v8.turbofan"),
                                     &tracing_enabled);
  if (tracing_enabled || v8_flags.turbo_stats || v8_flags.turbo_stats_nvp) {
    pipeline_statistics = std::make_unique<TurbofanPipelineStatistics>(
        info, isolate->GetTurboStatistics(), zone_stats);
    pipeline_statistics->BeginPhaseKind("V8.TFInitializing");
  }

  if (info->trace_turbo_json()) {
    TurboJsonFile json_of(info, std::ios_base::trunc);
    json_of << "{\"function\" : ";
    JsonPrintFunctionSource(json_of, -1, info->GetDebugName(), script, isolate,
                            info->shared_info());
    json_of << ",\n\"phases\":[";
  }

  return pipeline_statistics;
}

class PipelineData {
 public:
  struct WasmEnginePtr{
  #if V8_ENABLE_WEBASSEMBLY
    wasm::WasmEngine* wasm_engine_;
    WasmEnginePtr(wasm::WasmEngine* wasm_engine = nullptr)  // NOLINT(runtime/explicit)
      : wasm_engine_(wasm_engine) {}
    operator wasm::WasmEngine*() { return wasm_engine_; }
    wasm::WasmEngine* operator->() { return wasm_engine_; }
  #else
    std::nullptr_t wasm_engine_;
    WasmEnginePtr(std::nullptr_t wasm_engine = nullptr)  // NOLINT(runtime/explicit)
      : wasm_engine_(nullptr) {}
  #endif  // V8_ENABLE_WEBASSEMBLY
  };

  // Shared constructor.
  PipelineData(Isolate* isolate, WasmEnginePtr wasm_engine, AccountingAllocator* allocator, OptimizedCompilationInfo* info,
    std::unique_ptr<JSHeapBroker> broker, turboshaft::TurboshaftPipelineKind pipeline_kind, const AssemblerOptions& assembler_options)
    : isolate_(isolate),
#if V8_ENABLE_WEBASSEMBLY
    wasm_engine_(wasm_engine),
#endif  // V8_ENABLE_WEBASSEMBLY
    compilation_data_(std::make_unique<turboshaft::CompilationData>(info, std::move(broker), pipeline_kind, allocator)),
    debug_name_(info->GetDebugName()),
    assembler_options_(assembler_options)
   {
   }

  // For main entry point.
  static PipelineData ForJSMainEntryPoint(Isolate* isolate, OptimizedCompilationInfo* info, Handle<Script> script_for_statistics) {
    auto broker = std::make_unique<JSHeapBroker>(isolate, info->zone(), info->trace_heap_broker(), info->code_kind());
    PipelineData data(isolate, nullptr, isolate->allocator(), info, std::move(broker),
      turboshaft::TurboshaftPipelineKind::kJS, AssemblerOptions::Default(isolate));

    TurbofanPipelineStatistics* pipeline_statistics = data.InitializeStatistics(
      CreatePipelineStatistics(script_for_statistics, info, isolate, data.zone_stats()));
    
    PhaseScope scope(pipeline_statistics, "V8.TFInitPipelineData");
 
    data.graph_data_.emplace(data.zone_stats());
    data.graph_data_->InitializeWithNewGraphs(isolate);
    data.graph_data_->InitializeNodeObservation(info->node_observer());

    // TODO: Maybe put this somewhere.
    data.dependencies_ =
        info->zone()->New<CompilationDependencies>(data.broker(), info->zone());

    return data;
  }

#if 0
  PipelineData(/*ZoneStats* zone_stats, */ /* Isolate* isolate,*/
               /*OptimizedCompilationInfo* info,*/ //Handle<Script> script_for_statistics
               )
//               TurbofanPipelineStatistics* pipeline_statistics)
      //: //isolate_(isolate),
        //compilation_data_(new turboshaft::CompilationData(
        //    info,
        //    std::make_unique<JSHeapBroker>(isolate, info->zone(),
        //                                   info->trace_heap_broker(),
        //                                   info->code_kind()),
        //    turboshaft::TurboshaftPipelineKind::kJS, isolate->allocator())),
        //pipeline_statistics_(),
        // allocator_(isolate->allocator()),
        // [1]  info_(info),
        //debug_name_(info->GetDebugName()),
        // may_have_unverifiable_graph_(v8_flags.turboshaft),
        // [1] zone_stats_(zone_stats),
        // pipeline_statistics_(pipeline_statistics),
        //        graph_zone_scope_(zone_stats_, kGraphZoneName,
        //        kCompressGraphZone),
        //        graph_zone_(turboshaft::AttachDebugName<kGraphZoneName>(graph_zone_scope_.zone())),
        // graph_zone_(&compilation_data_->zone_stats, kGraphZoneName,
        //             kCompressGraphZone),
        //        instruction_zone_scope_(zone_stats_, kInstructionZoneName),
        //        instruction_zone_(instruction_zone_scope_.zone()),
        // instruction_zone_(&compilation_data_->zone_stats, kInstructionZoneName),
        // codegen_zone_scope_(zone_stats_, kCodegenZoneName),
        // codegen_zone_(codegen_zone_scope_.zone()),
        // codegen_zone_(&compilation_data_->zone_stats, kCodegenZoneName),
        //        broker_(new JSHeapBroker(isolate_, info->zone(),
        //                                 info->trace_heap_broker(),
        //                                 info->code_kind())),
        // register_allocation_zone_scope_(zone_stats_,
        //                                 kRegisterAllocationZoneName),
        // register_allocation_zone_(register_allocation_zone_scope_.zone()),
        // register_allocation_zone_(&compilation_data_->zone_stats,
        //                           kRegisterAllocationZoneName)//,
        /*assembler_options_(AssemblerOptions::Default(isolate))*/ {
// NOTE: We lose this. Is it required?
    //PhaseScope scope(pipeline_statistics_.get(), "V8.TFInitPipelineData");

    // ==> InitializeWithNewGraphs
    // graph_ = graph_zone_.New<Graph>(graph_zone_);
    // source_positions_ = graph_zone_.New<SourcePositionTable>(graph_);
    // if (info->trace_turbo_json()) {
    //   node_origins_ = graph_zone_.New<NodeOriginTable>(graph_);
    // }
    // simplified_ = graph_zone_->New<SimplifiedOperatorBuilder>(graph_zone_);
    // machine_ = graph_zone_->New<MachineOperatorBuilder>(
    //     graph_zone_, MachineType::PointerRepresentation(),
    //     InstructionSelector::SupportedMachineOperatorFlags(),
    //     InstructionSelector::AlignmentRequirements());
    // common_ = graph_zone_->New<CommonOperatorBuilder>(graph_zone_);
    // javascript_ = graph_zone_->New<JSOperatorBuilder>(graph_zone_);
    // jsgraph_ = graph_zone_->New<JSGraph>(isolate_, graph_, common_, javascript_,
    //                                      simplified_, machine_);
//    observe_node_manager_ =
//        info->node_observer()
//            ? graph_zone_->New<ObserveNodeManager>(graph_zone_)
//            : nullptr;
    // dependencies_ =
    //     info->zone()->New<CompilationDependencies>(broker(), info->zone());
  }
#endif

#if V8_ENABLE_WEBASSEMBLY
  // For WebAssembly compile entry point.
  static PipelineData ForWebAssemblyEntryPoint(wasm::WasmEngine* wasm_engine, OptimizedCompilationInfo* info,
    MachineGraph* mcgraph, SourcePositionTable* source_positions, NodeOriginTable* node_origins,
    const AssemblerOptions& assembler_options) {
    PipelineData data(nullptr, wasm_engine, wasm_engine->allocator(), info, {}, turboshaft::TurboshaftPipelineKind::kWasm,
      assembler_options);

    data.graph_data_.emplace(data.zone_stats());
    data.graph_data_->InitializeFromMachineGraph(nullptr, mcgraph, source_positions, node_origins);

    // TODO: Should put this somewhere.
    data.may_have_unverifiable_graph_ = v8_flags.turboshaft_wasm;

    return data;
  }

#if 0
  PipelineData(/*ZoneStats* zone_stats, */ /*wasm::WasmEngine* wasm_engine,*/
               /*OptimizedCompilationInfo* info,*/ //MachineGraph* mcgraph,
//               TurbofanPipelineStatistics* pipeline_statistics,
//               SourcePositionTable* source_positions,
//               NodeOriginTable* node_origins,
               /*const AssemblerOptions& assembler_options*/)
      // : //isolate_(nullptr),
        //wasm_engine_(wasm_engine),
        // compilation_data_(new turboshaft::CompilationData(
        //     info, nullptr, turboshaft::TurboshaftPipelineKind::kWasm,
        //     wasm_engine->allocator())),
        // allocator_(wasm_engine->allocator()),
        // // [1] info_(info),
        // debug_name_(info->GetDebugName()),
//        may_have_unverifiable_graph_(v8_flags.turboshaft_wasm) //,
        // [1] zone_stats_(zone_stats),
//        pipeline_statistics_(pipeline_statistics),
        //        graph_zone_scope_(zone_stats_, kGraphZoneName,
        //        kCompressGraphZone),
        //        graph_zone_(turboshaft::AttachDebugName<kGraphZoneName>(graph_zone_scope_.zone())),
        // graph_zone_(&compilation_data_->zone_stats, kGraphZoneName,
        //             kCompressGraphZone),
        // graph_(mcgraph->graph()),
        // source_positions_(source_positions),
        // node_origins_(node_origins),
        // machine_(mcgraph->machine()),
        // common_(mcgraph->common()),
        // mcgraph_(mcgraph),
        // //        instruction_zone_scope_(zone_stats_, kInstructionZoneName),
        // //        instruction_zone_(instruction_zone_scope_.zone()),
        // instruction_zone_(&compilation_data_->zone_stats, kInstructionZoneName),
        // codegen_zone_scope_(zone_stats_, kCodegenZoneName),
        // codegen_zone_(codegen_zone_scope_.zone()),
        // codegen_zone_(&compilation_data_->zone_stats, kCodegenZoneName),
        //        register_allocation_zone_scope_(zone_stats_,
        //                                        kRegisterAllocationZoneName),
        //        register_allocation_zone_(register_allocation_zone_scope_.zone()),
        // register_allocation_zone_(&compilation_data_->zone_stats,
        //                           kRegisterAllocationZoneName)//,
        /* assembler_options_(assembler_options)*/ {
    // simplified_ = graph_zone_->New<SimplifiedOperatorBuilder>(graph_zone_);
    // javascript_ = graph_zone_->New<JSOperatorBuilder>(graph_zone_);
    // jsgraph_ = graph_zone_->New<JSGraph>(isolate_, graph_, common_, javascript_,
    //                                      simplified_, machine_);
  }
#endif

#endif  // V8_ENABLE_WEBASSEMBLY

  // For CodeStubAssembler and machine graph testing entry point.
  static PipelineData ForCodeStubAssembler(Isolate* isolate, AccountingAllocator* allocator, OptimizedCompilationInfo* info, Graph* graph, JSGraph* jsgraph, Schedule* schedule,
  SourcePositionTable* source_positions, NodeOriginTable* node_origins, const AssemblerOptions& assembler_options,
  JumpOptimizationInfo* jump_opt, const ProfileDataFromFile* profile_data) {
    WasmEnginePtr wasm_engine{nullptr};
#if V8_ENABLE_WEBASSEMBLY
    wasm_engine = wasm::GetWasmEngine();
#endif  // V8_ENABLE_WEBASSEMBLY

    PipelineData data(isolate, wasm_engine, allocator, info, {}, turboshaft::TurboshaftPipelineKind::kCSA,
      assembler_options);
    
    data.graph_data_.emplace(data.zone_stats());
    if(jsgraph) {
      data.graph_data_->InitializeFromJSGraph(jsgraph, source_positions, node_origins);
    } else if (graph) {
      data.graph_data_->InitializeFromGraph(isolate, graph, source_positions, node_origins);
    } else {
      // TODO: The old implementation used to set `source_positions` and `node_origins` here. It's not clear if this is required.
      DCHECK_NULL(source_positions);
      DCHECK_NULL(node_origins);
    }
    data.set_schedule(schedule);

    // TODO: Find a place for those.
    data.jump_optimization_info_ = jump_opt;
    data.profile_data_ = profile_data;

    return data;
  }

  void ReplaceGraph(Graph* graph, Schedule* schedule, NodeOriginTable* node_origins = nullptr) {
    DCHECK(graph_data_.has_value());
    graph_data_->graph = ZoneWithNamePointer<Graph, kGraphZoneName>(graph);
    graph_data_->schedule = schedule;
    if(node_origins) {
      graph_data_->node_origins = ZoneWithNamePointer<NodeOriginTable, kGraphZoneName>(node_origins);
    } else {
      graph_data_->node_origins = graph_data_->zone.New<NodeOriginTable>(graph);
    }
    //graph_data_.emplace(zone_stats());
    // graph_data_->InitializeFromGraph(isolate_, graph, nullptr, node_origins);
    // graph_data_->schedule = schedule;
  }

#if 0
  PipelineData(/*ZoneStats* zone_stats, */ /*OptimizedCompilationInfo* info,*/
               /*Isolate* isolate, AccountingAllocator* allocator, */// Graph* graph,
               /*JSGraph* jsgraph,*/ //Schedule* schedule,
               //SourcePositionTable* source_positions,
               /*NodeOriginTable* node_origins,*/ JumpOptimizationInfo* jump_opt,
//               const AssemblerOptions& assembler_options,
               const ProfileDataFromFile* profile_data)
      : //isolate_(isolate),
// #if V8_ENABLE_WEBASSEMBLY
//         // TODO(clemensb): Remove this field, use GetWasmEngine directly
//         // instead.
//         //wasm_engine_(wasm::GetWasmEngine()),
// #endif  // V8_ENABLE_WEBASSEMBLY
        // compilation_data_(new turboshaft::CompilationData(
        //     info, nullptr, turboshaft::TurboshaftPipelineKind::kCSA,
        //     allocator)),
        // allocator_(allocator),
        // // [1] info_(info),
        // debug_name_(info->GetDebugName()),
        // [1] zone_stats_(zone_stats),
        //        graph_zone_scope_(zone_stats_, kGraphZoneName,
        //        kCompressGraphZone),
        //        graph_zone_(turboshaft::AttachDebugName<kGraphZoneName>(graph_zone_scope_.zone())),
//        graph_zone_(&compilation_data_->zone_stats, kGraphZoneName,
//                    kCompressGraphZone),
//        graph_(graph),
//        source_positions_(source_positions),
//        node_origins_(node_origins),
        // schedule_(schedule),
        //        instruction_zone_scope_(zone_stats_, kInstructionZoneName),
        //        instruction_zone_(instruction_zone_scope_.zone()),
        // instruction_zone_(&compilation_data_->zone_stats, kInstructionZoneName),
        //        codegen_zone_scope_(zone_stats_, kCodegenZoneName),
        //        codegen_zone_(codegen_zone_scope_.zone()),
        // codegen_zone_(&compilation_data_->zone_stats, kCodegenZoneName),
        //        register_allocation_zone_scope_(zone_stats_,
        //                                        kRegisterAllocationZoneName),
        //        register_allocation_zone_(register_allocation_zone_scope_.zone()),
        // register_allocation_zone_(&compilation_data_->zone_stats,
        //                           kRegisterAllocationZoneName),
        jump_optimization_info_(jump_opt),
//        assembler_options_(assembler_options),
        profile_data_(profile_data) {
//    if (jsgraph) {
//      jsgraph_ = jsgraph;
//      simplified_ = jsgraph->simplified();
//      machine_ = jsgraph->machine();
//      common_ = jsgraph->common();
//      javascript_ = jsgraph->javascript();
//    } else if (graph_) {
//      simplified_ = graph_zone_->New<SimplifiedOperatorBuilder>(graph_zone_);
//      machine_ = graph_zone_->New<MachineOperatorBuilder>(
//          graph_zone_, MachineType::PointerRepresentation(),
//          InstructionSelector::SupportedMachineOperatorFlags(),
//          InstructionSelector::AlignmentRequirements());
//      common_ = graph_zone_->New<CommonOperatorBuilder>(graph_zone_);
//      javascript_ = graph_zone_->New<JSOperatorBuilder>(graph_zone_);
//      jsgraph_ = graph_zone_->New<JSGraph>(isolate_, graph_, common_,
//                                           javascript_, simplified_, machine_);
//    }
  }
#endif

  // For register allocation testing entry point.
  static PipelineData ForRegisterAllocatorTesting(Isolate* isolate, OptimizedCompilationInfo* info,
    InstructionSequence* sequence) {
    PipelineData data(isolate, nullptr, isolate->allocator(), info, {}, turboshaft::TurboshaftPipelineKind::kCSA,
      AssemblerOptions::Default(isolate));

    // TODO: The old implementation used to initialize `graph_zone` but it's not clear if this is required.
    data.instruction_data_.emplace(data.zone_stats());
    data.instruction_data_->InitializeFromSequence(sequence);

    return data;
  }

#if 0
  PipelineData(/*ZoneStats* zone_stats, */ //OptimizedCompilationInfo* info,
               /*Isolate* isolate, */ /*InstructionSequence* sequence*/)
      : // isolate_(isolate),
        // compilation_data_(new turboshaft::CompilationData(
        //     info, nullptr, turboshaft::TurboshaftPipelineKind::kCSA,
        //     isolate->allocator())),
        // allocator_(isolate->allocator()),
        // // [1] info_(info),
        // debug_name_(info->GetDebugName()),
        // [1] zone_stats_(zone_stats),
        //        graph_zone_scope_(zone_stats_, kGraphZoneName,
        //        kCompressGraphZone),
//        graph_zone_(&compilation_data_->zone_stats, kGraphZoneName,
//                    kCompressGraphZone),
        //        instruction_zone_scope_(zone_stats_, kInstructionZoneName),
        //        instruction_zone_(sequence->zone()),
        // instruction_zone_(&compilation_data_->zone_stats,
        //                   kInstructionZoneName),  // TODO: Is this okay?
        // sequence_(sequence),
        //        codegen_zone_scope_(zone_stats_, kCodegenZoneName),
        //        codegen_zone_(codegen_zone_scope_.zone()),
        // codegen_zone_(&compilation_data_->zone_stats, kCodegenZoneName),
        //        register_allocation_zone_scope_(zone_stats_,
        //                                        kRegisterAllocationZoneName),
        //        register_allocation_zone_(register_allocation_zone_scope_.zone()),
        // register_allocation_zone_(&compilation_data_->zone_stats,
        //                           kRegisterAllocationZoneName),
        // assembler_options_(AssemblerOptions::Default(isolate))
        {}
#endif

  ~PipelineData() {
    // Must happen before zones are destroyed.
    code_generator_.reset();
    // delete code_generator_;
    // code_generator_ = nullptr;
    DeleteTyper();
    DeleteRegisterAllocationZone();
    DeleteInstructionZone();
    DeleteCodegenZone();
    DeleteGraphZone();
  }

  PipelineData(const PipelineData&) = delete;
  PipelineData(PipelineData&&) = default;
  PipelineData& operator=(const PipelineData&) = delete;
//  PipelineData& operator=(PipelineData&&) = default;

  Isolate* isolate() const { return isolate_; }
  AccountingAllocator* allocator() const { // return allocator_;
    return info()->zone()->allocator();
  }
  OptimizedCompilationInfo* info() const { return compilation_data_->info; }
  ZoneStats* zone_stats() const { return &compilation_data_->zone_stats; }
  //  OptimizedCompilationInfo* info() const { return info_; }
  //  ZoneStats* zone_stats() const { return zone_stats_; }
  CompilationDependencies* dependencies() const { return dependencies_; }
  TurbofanPipelineStatistics* pipeline_statistics() {
    return pipeline_statistics_.get();
  }
  OsrHelper* osr_helper() { return &(*osr_helper_); }

  bool verify_graph() const { return verify_graph_; }
  void set_verify_graph(bool value) { verify_graph_ = value; }

  MaybeHandle<Code> code() { return code_; }
  void set_code(MaybeHandle<Code> code) {
    DCHECK(code_.is_null());
    code_ = code;
  }

  CodeGenerator* code_generator() const { return code_generator_.get(); }

  // RawMachineAssembler generally produces graphs which cannot be verified.
  bool MayHaveUnverifiableGraph() const { return may_have_unverifiable_graph_; }

  Zone* graph_zone() {
    DCHECK(graph_data_.has_value());
    return graph_data_->zone;
    //return graph_zone_;
  }
  Graph* graph() const {
    DCHECK(graph_data_.has_value());
    return graph_data_->graph;
    //return graph_;
  }
  void set_graph(Graph* graph) {
    DCHECK(graph_data_.has_value());
    graph_data_->graph = ZoneWithNamePointer<Graph, kGraphZoneName>(graph);
  }
  turboshaft::PipelineData& GetTurboshaftPipelineData(
      turboshaft::TurboshaftPipelineKind kind,
      turboshaft::Graph* graph = nullptr) {
    if (!ts_data_.has_value()) {
      // turboshaft::TurboshaftPipelineKind _pipeline_kind = kind;
      // OptimizedCompilationInfo* const& _info = compilation_data_->info;
      // DCHECK(graph_data_.has_value());
      // ZoneWithName<kGraphZoneName>& _graph_zone = graph_data_->zone;
      // Zone* _shared_zone = info()->zone();
      // std::unique_ptr<JSHeapBroker>& _broker = compilation_data_->broker;
      // Isolate* const& _isolate = isolate_;
      // ZoneWithNamePointer<SourcePositionTable, kGraphZoneName>& _source_positions = graph_data_->source_positions;
      // ZoneWithNamePointer<NodeOriginTable, kGraphZoneName>& _node_origins = graph_data_->node_origins;
      // AssemblerOptions& _assembler_options = assembler_options_;
      // size_t* _address_of_max_unoptimized_frame_height = &max_unoptimized_frame_height_;
      // size_t* _address_of_max_pushed_argument_count = &max_pushed_argument_count_;
      // turboshaft::Graph* _graph = graph;
 
      ts_data_.emplace(this, kind, compilation_data_->info,
                        graph_data_->zone,
                        info()->zone(),
                       compilation_data_->broker, isolate_, graph_data_->source_positions,
                       graph_data_->node_origins,
                       assembler_options_, &max_unoptimized_frame_height_,
                       &max_pushed_argument_count_,
                       graph);
    }
    return ts_data_.value();
  }
  SourcePositionTable* source_positions() const {
    if(!graph_data_.has_value()) return nullptr;
    return graph_data_->source_positions;
    // return source_positions_;
  }
  NodeOriginTable* node_origins() const {
    if(!graph_data_.has_value()) return nullptr;
    return graph_data_->node_origins;
    //return node_origins_;
  }
  void set_node_origins(NodeOriginTable* node_origins) {
    UNIMPLEMENTED();
    //node_origins_ =
    //    ZoneWithNamePointer<NodeOriginTable, kGraphZoneName>(node_origins);
  }
  MachineOperatorBuilder* machine() const {
    DCHECK(graph_data_.has_value());
    return graph_data_->machine_builder;
    // return machine_;
  }
  SimplifiedOperatorBuilder* simplified() const {
     DCHECK(graph_data_.has_value());
    return graph_data_->simplified_builder;
    // return simplified_;
  }
  CommonOperatorBuilder* common() const {
      DCHECK(graph_data_.has_value());
    return graph_data_->common_builder;
   // return common_;
  }
  JSOperatorBuilder* javascript() const {
       DCHECK(graph_data_.has_value());
    return graph_data_->javascript_builder;
  // return javascript_;
  }
  JSGraph* jsgraph() const {
        DCHECK(graph_data_.has_value());
    return graph_data_->jsgraph;
 // return jsgraph_;
  }
  MachineGraph* mcgraph() const {
       DCHECK(graph_data_.has_value());
    return graph_data_->mcgraph;
  // return mcgraph_;
  }
  Handle<NativeContext> native_context() const {
    return handle(info()->native_context(), isolate());
  }
  Handle<JSGlobalObject> global_object() const {
    return handle(info()->global_object(), isolate());
  }

  JSHeapBroker* broker() const { return compilation_data_->broker.get(); }
  //  std::unique_ptr<JSHeapBroker> ReleaseBroker() { return std::move(broker_);
  //  }

  Schedule* schedule() const {
    //return schedule_;
    DCHECK(graph_data_.has_value());
    return graph_data_->schedule;
  }
  void set_schedule(Schedule* schedule) {
    // DCHECK(!schedule_);
    // schedule_ = schedule;
    DCHECK(graph_data_.has_value());
    DCHECK_NULL(graph_data_->schedule);
    graph_data_->schedule = schedule;
  }
  void reset_schedule() {
    // schedule_ = nullptr;
    DCHECK(graph_data_.has_value());
    graph_data_->schedule = nullptr;
  }

  ObserveNodeManager* observe_node_manager() const {
    DCHECK(graph_data_.has_value());
    return graph_data_->observe_node_manager;
    // return observe_node_manager_;
  }

  ZoneWithName<kInstructionZoneName>& instruction_zone() {
    DCHECK(instruction_data_.has_value());
    return instruction_data_->zone;
    // return instruction_zone_;
  }
  ZoneWithName<kCodegenZoneName>& codegen_zone() {
    DCHECK(codegen_data_.has_value());
    return codegen_data_->zone;
    // return codegen_zone_;
  }
  ZoneWithNamePointer<InstructionSequence, kInstructionZoneName> sequence()
      const {
    DCHECK(instruction_data_.has_value());
    return instruction_data_->sequence;
    // return sequence_;
  }
  ZoneWithNamePointer<Frame, kCodegenZoneName> frame() const {
    if(!codegen_data_.has_value()) return nullptr;
    return codegen_data_->frame;
    // return frame_;
  }

  ZoneWithName<kRegisterAllocationZoneName>& register_allocation_zone() {
    DCHECK(register_allocator_data_.has_value());
    return register_allocator_data_->zone;
    // return register_allocation_zone_;
  }

  RegisterAllocationData* register_allocation_data() const {
     DCHECK(register_allocator_data_.has_value());
    return register_allocator_data_->register_allocation_data;
    // return register_allocation_data_;
  }

  std::string const& source_position_output() const {
    return source_position_output_;
  }
  void set_source_position_output(std::string const& source_position_output) {
    source_position_output_ = source_position_output;
  }

  JumpOptimizationInfo* jump_optimization_info() const {
    return jump_optimization_info_;
  }

  const AssemblerOptions& assembler_options() const {
    return assembler_options_;
  }

  void ChooseSpecializationContext() {
    if (info()->function_context_specializing()) {
      DCHECK(info()->has_context());
      specialization_context_ = Just(OuterContext(
          info()->CanonicalHandle(info()->context(), isolate()), 0));
    } else {
      specialization_context_ = GetModuleContext(info());
    }
  }

  Maybe<OuterContext> specialization_context() const {
    return specialization_context_;
  }

  size_t* address_of_max_unoptimized_frame_height() {
    return &max_unoptimized_frame_height_;
  }
  size_t max_unoptimized_frame_height() const {
    return max_unoptimized_frame_height_;
  }
  size_t* address_of_max_pushed_argument_count() {
    return &max_pushed_argument_count_;
  }
  size_t max_pushed_argument_count() const {
    return max_pushed_argument_count_;
  }

  CodeTracer* GetCodeTracer() const {
#if V8_ENABLE_WEBASSEMBLY
    if (wasm_engine_) return wasm_engine_->GetCodeTracer();
#endif  // V8_ENABLE_WEBASSEMBLY
    // NOTE: We must not call `GetCodeTracer` if tracing is not enabled,
    // because it may not yet be initialized then and doing so from the
    // background thread is not threadsafe.
    DCHECK(info()->trace_turbo_graph() || info()->trace_turbo_json());
    return isolate_->GetCodeTracer();
  }

  Typer* CreateTyper() {
    DCHECK_NULL(typer_);
    typer_ =
        new Typer(broker(), typer_flags_, graph(), &info()->tick_counter());
    return typer_;
  }

  void AddTyperFlag(Typer::Flag flag) {
    DCHECK_NULL(typer_);
    typer_flags_ |= flag;
  }

  void DeleteTyper() {
    delete typer_;
    typer_ = nullptr;
  }

  void DeleteGraphZone() {
    // if (graph_zone_ == nullptr) return;
    // graph_ = nullptr;
    // source_positions_ = nullptr;
    // node_origins_ = nullptr;
    // simplified_ = nullptr;
    // machine_ = nullptr;
    // common_ = nullptr;
    // javascript_ = nullptr;
    // jsgraph_ = nullptr;
    // mcgraph_ = nullptr;
    graph_data_.reset();

    // schedule_ = nullptr;
    //    graph_zone_scope_.Destroy();
    // graph_zone_.Destroy();
  }

  void DeleteInstructionZone() {
    //instruction_zone_.Destroy();
    //    if (instruction_zone_ == nullptr) return;
    //    instruction_zone_scope_.Destroy();
    //    instruction_zone_ = nullptr;
    //sequence_ = nullptr;
    instruction_data_.reset();
  }

  void DeleteCodegenZone() {
    // if (codegen_zone_ == nullptr) return;
    // codegen_zone_scope_.Destroy();
    // codegen_zone_ = nullptr;
    // codegen_zone_.Destroy();
    codegen_data_.reset();

    dependencies_ = nullptr;
    //    broker_.reset();
    // broker_ = nullptr;
    // frame_ = nullptr;
  }

  void DeleteRegisterAllocationZone() {
    // if (register_allocation_zone_ == nullptr) return;
    // register_allocation_zone_scope_.Destroy();
    // register_allocation_zone_ = nullptr;
    // register_allocation_zone_.Destroy();
    // register_allocation_data_ = nullptr;
    register_allocator_data_.reset();
  }

  void InitializeInstructionSequence(const CallDescriptor* call_descriptor) {
    DCHECK(!instruction_data_.has_value());
    DCHECK_NOT_NULL(schedule());
    instruction_data_.emplace(zone_stats());
    instruction_data_->InitializeFromSchedule(isolate_, schedule(), call_descriptor);
  }

  void InitializeFrameData(const CallDescriptor* call_descriptor) {
    DCHECK(!codegen_data_.has_value());
    codegen_data_.emplace(zone_stats());
    codegen_data_->InitializeFrame(info(), call_descriptor, osr_helper_);
  }

  void InitializeRegisterAllocationData(const RegisterConfiguration* config) {
    DCHECK(!register_allocator_data_.has_value());
    register_allocator_data_.emplace(zone_stats());
    register_allocator_data_->Initialize(info(), config, frame(), sequence(), debug_name());
  }

  void InitializeOsrHelper() {
    DCHECK(!osr_helper_.has_value());
    osr_helper_.emplace(info());
  }

  void set_start_source_position(int position) {
    DCHECK_EQ(start_source_position_, kNoSourcePosition);
    start_source_position_ = position;
  }

  void InitializeCodeGenerator(Linkage* linkage) {
    DCHECK_NULL(code_generator_);
#if V8_ENABLE_WEBASSEMBLY
    assembler_options_.is_wasm =
        this->info()->IsWasm() || this->info()->IsWasmBuiltin();
#endif
    code_generator_ = std::make_unique<CodeGenerator>(
        codegen_zone(), frame(), linkage, sequence(), info(), isolate(),
        osr_helper_, start_source_position_, jump_optimization_info_,
        assembler_options(), info()->builtin(), max_unoptimized_frame_height(),
        max_pushed_argument_count(),
        v8_flags.trace_turbo_stack_accesses ? debug_name() : nullptr);
  }

  void BeginPhaseKind(const char* phase_kind_name) {
    if (pipeline_statistics() != nullptr) {
      pipeline_statistics()->BeginPhaseKind(phase_kind_name);
    }
  }

  void EndPhaseKind() {
    if (pipeline_statistics() != nullptr) {
      pipeline_statistics()->EndPhaseKind();
    }
  }

  const char* debug_name() const {
    // TODO: We need to cache this.
    return debug_name_.get();
  }

  const ProfileDataFromFile* profile_data() const { return profile_data_; }
  void set_profile_data(const ProfileDataFromFile* profile_data) {
    profile_data_ = profile_data;
  }

  // RuntimeCallStats that is only available during job execution but not
  // finalization.
  // TODO(delphick): Currently even during execution this can be nullptr, due to
  // JSToWasmWrapperCompilationUnit::Execute. Once a table can be extracted
  // there, this method can DCHECK that it is never nullptr.
  RuntimeCallStats* runtime_call_stats() const {
    //    if (turboshaft_pipeline_) {
    //      DCHECK_NULL(runtime_call_stats_);
    //      return turboshaft_pipeline_->runtime_call_stats();
    //    } else {
    //      return runtime_call_stats_;
    //    }
    return runtime_call_stats_;
  }
  void set_runtime_call_stats(RuntimeCallStats* stats) {
    //    if (turboshaft_pipeline_) {
    //      DCHECK_NULL(runtime_call_stats_);
    //      turboshaft_pipeline_->set_runtime_call_stats(stats);
    //    } else {
    //      runtime_call_stats_ = stats;
    //    }
    runtime_call_stats_ = stats;
  }

  // Used to skip the "wasm-inlining" phase when there are no JS-to-Wasm calls.
  bool has_js_wasm_calls() const { return has_js_wasm_calls_; }
  void set_has_js_wasm_calls(bool has_js_wasm_calls) {
    has_js_wasm_calls_ = has_js_wasm_calls;
  }

#if V8_ENABLE_WEBASSEMBLY
  const wasm::WasmModule* wasm_module_for_inlining() const {
    return wasm_module_for_inlining_;
  }
  void set_wasm_module_for_inlining(const wasm::WasmModule* module) {
    wasm_module_for_inlining_ = module;
  }
#endif

  std::unique_ptr<turboshaft::CompilationData> TakeCompilationData() {
    // We have to initialize `CompilationData::code_tracer` here if we have tracing enabled.
    if(compilation_data_->info->trace_turbo_graph()) {
      // NOTE: We must not call `GetCodeTracer` if tracing is not enabled,
      // because it may not yet be initialized then and doing so from the
      // background thread is not threadsafe.
      compilation_data_->code_tracer = GetCodeTracer();
    }
    return std::move(compilation_data_);
  }

  TurbofanPipelineStatistics* InitializeStatistics(std::unique_ptr<TurbofanPipelineStatistics> statistics) {
    DCHECK_NULL(pipeline_statistics_);
    pipeline_statistics_ = std::move(statistics);
    return pipeline_statistics_.get();
  }

//  TurbofanPipelineStatistics* CreateTurbofanPipelineStatistics(std::shared_ptr<CompilationStatistics> compilation_statistics) {
//    DCHECK_NULL(pipeline_statistics_);
//    pipeline_statistics_ = std::make_unique<TurbofanPipelineStatistics>(info(), compilation_statistics,
//      zone_stats());
//    return pipeline_statistics_.get();
//  }
//
//  TurbofanPipelineStatistics* CreateTurbofanPipelineStatistics(std::unique_ptr<TurbofanPipelineStatistics> use_these = {}) {
//    DCHECK_NULL(pipeline_statistics_);
//    if(use_these) {
//      pipeline_statistics_ = std::move(use_these);
//    } else {
//      UNIMPLEMENTED();
//    }
//    return pipeline_statistics_.get();
//  }

 private:
  friend class turboshaft::PipelineData; 
  //  turboshaft::PipelineBase* turboshaft_pipeline_ = nullptr;
  Isolate* const isolate_;
#if V8_ENABLE_WEBASSEMBLY
  wasm::WasmEngine* const wasm_engine_ = nullptr;
  // The wasm module to be used for inlining wasm functions into JS.
  // The first module wins and inlining of different modules into the same
  // JS function is not supported. This is necessary because the wasm
  // instructions use module-specific (non-canonicalized) type indices.
  const wasm::WasmModule* wasm_module_for_inlining_ = nullptr;
#endif  // V8_ENABLE_WEBASSEMBLY

  std::unique_ptr<turboshaft::CompilationData> compilation_data_;
//  AccountingAllocator* const allocator_ = nullptr;  //
  std::unique_ptr<char[]> debug_name_;

  std::unique_ptr<TurbofanPipelineStatistics> pipeline_statistics_;
  base::Optional<OsrHelper> osr_helper_;
  std::unique_ptr<CodeGenerator> code_generator_;
  AssemblerOptions assembler_options_;

  // [1]
  // [1] OptimizedCompilationInfo* const info_ = nullptr; //
  bool may_have_unverifiable_graph_ = true;
  // [1] ZoneStats* const zone_stats_ = nullptr; //

//  TurbofanPipelineStatistics* pipeline_statistics_ = nullptr;
  bool verify_graph_ = false;
  int start_source_position_ = kNoSourcePosition;
  MaybeHandle<Code> code_;
  Typer* typer_ = nullptr;
  Typer::Flags typer_flags_ = Typer::kNoFlags;

 //  ZoneStats::Scope graph_zone_scope_;
  struct GraphData {
    // All objects in the following group of fields are allocated in graph_zone_.
    // They are all set to nullptr when the graph_zone_ is destroyed.
    // Technically, in some instances of `PipelineData`, (some of) the following
    // pointers might not actually point into the graph zone, but may be provided
    // from outside. However, we consider these pointers valid only as long as the
    // graph zone is alive.
    template<typename T>
    using Pointer = ZoneWithNamePointer<T, kGraphZoneName>;

    ZoneWithName<kGraphZoneName> zone;
    Pointer<Graph> graph = nullptr;
    // Tables
    Pointer<SourcePositionTable> source_positions = nullptr;
    Pointer<NodeOriginTable> node_origins = nullptr;
    // Builders
    Pointer<MachineOperatorBuilder> machine_builder = nullptr;
    Pointer<CommonOperatorBuilder> common_builder = nullptr;
    Pointer<SimplifiedOperatorBuilder> simplified_builder = nullptr;
    Pointer<JSOperatorBuilder> javascript_builder = nullptr;
    // Graph facades
    Pointer<JSGraph> jsgraph = nullptr;
    Pointer<MachineGraph> mcgraph = nullptr;
    Pointer<ObserveNodeManager> observe_node_manager = nullptr;
    Schedule* schedule = nullptr;

    explicit GraphData(ZoneStats* zone_stats)
      : zone(zone_stats, kGraphZoneName, kCompressGraphZone) {}

    void InitializeWithNewGraphs(Isolate* isolate) {
      // Graph & Tables.
      graph = zone.New<Graph>(zone);
      source_positions = zone.New<SourcePositionTable>(graph);
      // if (info->trace_turbo_json()) {
      node_origins = zone.New<NodeOriginTable>(graph);
      // }
      // Builders
      machine_builder = zone.New<MachineOperatorBuilder>(
          zone, MachineType::PointerRepresentation(),
          InstructionSelector::SupportedMachineOperatorFlags(),
          InstructionSelector::AlignmentRequirements());
      common_builder = zone.New<CommonOperatorBuilder>(zone);
      simplified_builder = zone.New<SimplifiedOperatorBuilder>(zone);
      javascript_builder = zone.New<JSOperatorBuilder>(zone);
      // Graph facades.
      mcgraph = nullptr;
      jsgraph = zone.New<JSGraph>(isolate, graph, common_builder, javascript_builder,
                                         simplified_builder, machine_builder);
    }
    void InitializeFromGraph(Isolate* isolate, Graph* graph, SourcePositionTable* source_positions,
      NodeOriginTable* node_origins) {
        // Graph & Tables.
        this->graph = Pointer<Graph>(graph);
        this->source_positions = Pointer<SourcePositionTable>(source_positions);
        this->node_origins = Pointer<NodeOriginTable>(node_origins);
        // Builders
      machine_builder = zone.New<MachineOperatorBuilder>(
          zone, MachineType::PointerRepresentation(),
          InstructionSelector::SupportedMachineOperatorFlags(),
          InstructionSelector::AlignmentRequirements());
      common_builder = zone.New<CommonOperatorBuilder>(zone);
      simplified_builder = zone.New<SimplifiedOperatorBuilder>(zone);
      javascript_builder = zone.New<JSOperatorBuilder>(zone);
      // Graph facades.
      mcgraph = nullptr;
      jsgraph = zone.New<JSGraph>(isolate, graph, common_builder,
                                           javascript_builder, simplified_builder, machine_builder);
      }
    void InitializeFromMachineGraph(Isolate* isolate, MachineGraph* mcgraph, SourcePositionTable* source_positions,
      NodeOriginTable* node_origins) {
        // Graph & Tables.
        graph = Pointer<Graph>(mcgraph->graph());
        this->source_positions = Pointer<SourcePositionTable>(source_positions);
        this->node_origins = Pointer<NodeOriginTable>(node_origins);
        // Builders
        machine_builder = Pointer<MachineOperatorBuilder>(mcgraph->machine());
        common_builder = Pointer<CommonOperatorBuilder>(mcgraph->common());
        simplified_builder = zone.New<SimplifiedOperatorBuilder>(zone);
        javascript_builder = zone.New<JSOperatorBuilder>(zone);
        // Graph facades.
        this->mcgraph = Pointer<MachineGraph>(mcgraph);
        jsgraph = zone.New<JSGraph>(isolate, graph, common_builder, javascript_builder,
                                         simplified_builder, machine_builder);
    }
    void InitializeFromJSGraph(JSGraph* jsgraph, SourcePositionTable* source_positions, NodeOriginTable* node_origins) {
        // Graph & Tables.
        graph = Pointer<Graph>(jsgraph->graph());
        this->source_positions = Pointer<SourcePositionTable>(source_positions);
        this->node_origins = Pointer<NodeOriginTable>(node_origins);
        // Builders
        machine_builder = Pointer<MachineOperatorBuilder>(jsgraph->machine());
        common_builder = Pointer<CommonOperatorBuilder>(jsgraph->common());
        simplified_builder = Pointer<SimplifiedOperatorBuilder>(jsgraph->simplified());
        javascript_builder = Pointer<JSOperatorBuilder>(jsgraph->javascript());
        // Graph facades.
        mcgraph = nullptr;
        this->jsgraph = Pointer<JSGraph>(jsgraph);
    }
    void InitializeNodeObservation(NodeObserver* observer) {
      DCHECK_NULL(observe_node_manager);
      if(observer) {
        observe_node_manager = zone.New<ObserveNodeManager>(zone);
      }
    }
  };

  base::Optional<GraphData> graph_data_;


   base::Optional<turboshaft::PipelineData> ts_data_;

  struct InstructionData {
    // All objects in the following group of fields are allocated in
    // instruction_zone. They are all set to nullptr when the instruction_zone
    // is destroyed.
    template<typename T>
    using Pointer = ZoneWithNamePointer<T, kInstructionZoneName>;

    ZoneWithName<kInstructionZoneName> zone;
    Pointer<InstructionSequence> sequence = nullptr;

    InstructionData(ZoneStats* zone_stats)
      : zone(zone_stats, kInstructionZoneName) {}
    
    void InitializeFromSequence(InstructionSequence* sequence) {
      DCHECK_NULL(this->sequence);
      this->sequence = Pointer<InstructionSequence>(sequence);
    }
    void InitializeFromSchedule(Isolate* isolate, Schedule* schedule, const CallDescriptor* call_descriptor) {
      DCHECK_NULL(sequence);
      InstructionBlocks* instruction_blocks =
          InstructionSequence::InstructionBlocksFor(zone, schedule);
      sequence = zone.New<InstructionSequence>(
          isolate, zone, instruction_blocks);
      if (call_descriptor && call_descriptor->RequiresFrameAsIncoming()) {
        sequence->instruction_blocks()[0]->mark_needs_frame();
      } else {
        DCHECK(call_descriptor->CalleeSavedFPRegisters().is_empty());
      }
    }
  };

  base::Optional<InstructionData> instruction_data_;

 struct CodegenData {
    // All objects in the following group of fields are allocated in
    // codegen_zone. They are all set to nullptr when the codegen_zone
    // is destroyed.
    template<typename T>
    using Pointer = ZoneWithNamePointer<T, kCodegenZoneName>;

    ZoneWithName<kCodegenZoneName> zone;
    Pointer<Frame> frame = nullptr;

    CodegenData(ZoneStats* stats)
      : zone(stats, kCodegenZoneName) {}

    void InitializeFrame(OptimizedCompilationInfo* info, const CallDescriptor* call_descriptor,
          base::Optional<OsrHelper>& osr_helper) {
      DCHECK_NULL(frame);
      int fixed_frame_size = 0;
      if (call_descriptor != nullptr) {
        fixed_frame_size =
            call_descriptor->CalculateFixedFrameSize(info->code_kind());
      }
      frame = zone.New<Frame>(fixed_frame_size, zone);
      if (osr_helper.has_value()) osr_helper->SetupFrame(frame);
    }
  };

  base::Optional<CodegenData> codegen_data_;
  
  CompilationDependencies* dependencies_ = nullptr;

  struct RegisterAllocatorData {
    // All objects in the following group of fields are allocated in
    // register_allocation_zone. They are all set to nullptr when the zone is
    // destroyed.
    template<typename T>
    using Pointer = ZoneWithNamePointer<T, kRegisterAllocationZoneName>;

    ZoneWithName<kRegisterAllocationZoneName> zone;
    Pointer<RegisterAllocationData> register_allocation_data = nullptr;

    RegisterAllocatorData(ZoneStats* stats)
      : zone(stats, kRegisterAllocationZoneName) {}

    void Initialize(OptimizedCompilationInfo* info, const RegisterConfiguration* config, Frame* frame, InstructionSequence* sequence,
      const char* debug_name) {
      DCHECK_NULL(register_allocation_data);
      register_allocation_data = zone.New<RegisterAllocationData>(
              config, zone, frame, sequence,
              &info->tick_counter(), debug_name);
    }
  };

  base::Optional<RegisterAllocatorData> register_allocator_data_;

  // Source position output for --trace-turbo.
  std::string source_position_output_;

  JumpOptimizationInfo* jump_optimization_info_ = nullptr;
  Maybe<OuterContext> specialization_context_ = Nothing<OuterContext>();

  // The maximal combined height of all inlined frames in their unoptimized
  // state, and the maximal number of arguments pushed during function calls.
  // Calculated during instruction selection, applied during code generation.
  size_t max_unoptimized_frame_height_ = 0;
  size_t max_pushed_argument_count_ = 0;

  RuntimeCallStats* runtime_call_stats_ = nullptr;
  const ProfileDataFromFile* profile_data_ = nullptr;

  bool has_js_wasm_calls_ = false;
};

}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_PIPELINE_DATA_INL_H_
