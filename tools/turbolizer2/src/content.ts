// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { HierarchicalGraph } from "./hierarchical-graph"

export enum PhaseType {
  Turbofan = "graph",
  TurboshaftGraph = "turboshaft_graph",
  TurboshaftCustomData = "turboshaft_custom_data",
  Disassembly = "disassembly",
  Instructions = "instructions",
  Sequence = "sequence",
  Schedule = "schedule",
  Turboshaft = "turboshaft"
}

export class Phase {
  type: PhaseType;
  name: string;

  constructor(type: PhaseType, name: string) {
    this.type = type;
    this.name = name;
  }
}

export class TurboshaftPhase extends Phase {
  graph: HierarchicalGraph;

  constructor(name: string, graph: HierarchicalGraph) {
    super(PhaseType.TurboshaftGraph, name);
    this.graph = graph;
  }
}

export class TurbofanPhase extends Phase {
  constructor(name: string) {
    super(PhaseType.Turbofan, name);
  }
}

export class Content {
  phases: Array<Phase> = new Array<Phase>();
}
