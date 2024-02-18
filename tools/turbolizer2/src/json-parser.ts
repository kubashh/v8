// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// import { BasicBlock, Graph } from "./hierarchical-graph"
import { Content, Phase, PhaseType, TurbofanPhase, TurboshaftPhase } from "./content"
import { HierarchicalGraph, BasicBlock, Operation } from "./hierarchical-graph"

export class JsonParser {
  constructor() {}

  public Parse(json: any): Content {
    let content = new Content();
    this.ParsePhases(content, json.phases);
    return content;
  }

  private ParsePhases(content: Content, jsonPhases: any[]) {
    for(const jsonPhase of jsonPhases) {
      let phase: Phase;
      switch(jsonPhase.type) {
        case PhaseType.Turbofan:
          phase = this.ParseTurbofanPhase(jsonPhase);
          break;
        case PhaseType.TurboshaftGraph:
          phase = this.ParseTurboshaftGraphPhase(jsonPhase);
          break;
        default:
          console.log("UNIMPLEMENTED: PhaseType " + jsonPhase.type);
          continue;
      }
      content.phases.push(phase);
    }
  }

  private ParseTurbofanPhase(jsonPhase: any): TurbofanPhase {
    return new TurbofanPhase(jsonPhase.name);
  }

  private ParseTurboshaftGraphPhase(jsonPhase: any): TurboshaftPhase {
    const jsonPhaseData = jsonPhase.data;
    
    // Parse graph
    let graph = null;
    {
      // Construct all BasicBlocks first.
      let basicBlocks = new Array<BasicBlock>();
      for(const jsonBlock of jsonPhaseData.blocks) {
          const basicBlock = new BasicBlock(jsonBlock.id, jsonBlock.type);
          console.assert(basicBlock.id === basicBlocks.length);
          basicBlocks.push(basicBlock);
      }

      // Setup predecessors and successors.
      for(const jsonBlock of jsonPhaseData.blocks) {
          let block = basicBlocks[jsonBlock.id];
          console.assert(block.id === jsonBlock.id);
          for(const jsonPredecessorId of jsonBlock.predecessors) {
              let predecessor = basicBlocks[jsonPredecessorId];
              console.assert(predecessor.id === jsonPredecessorId);
              block.predecessors.push(predecessor);
              predecessor.successors.push(block);
          }
      }

      // Parse operations.
      for(const jsonNode of jsonPhaseData.nodes) {
        const operation = new Operation(jsonNode.id, jsonNode.title);
        basicBlocks[jsonNode.block_id].operations.push(operation);
      }
      graph = new HierarchicalGraph(basicBlocks);
    }

    return new TurboshaftPhase(jsonPhase.name, graph);
  }
}