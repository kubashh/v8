// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export enum HierarchicalGraphKind {
    BasicBlock,
    Loop,
    AcyclicSubgraph,
    Subgraph,
}

export enum BasicBlockKind {
    Block = "BLOCK",
    Merge = "MERGE",
    Loop = "LOOP",
}

export class Operation {
    id: number;
    title: string;

    constructor(id: number, title: string) {
        this.id = id;
        this.title = title;
    }
}

export class BasicBlock {
  id: number;
  kind: BasicBlockKind;
  predecessors: Array<BasicBlock>;
  successors: Array<BasicBlock>;
  operations: Array<Operation>;
  hg: HGBasicBlock = null;

  constructor(id: number, kind: BasicBlockKind) {
    this.id = id;
    this.kind = kind;
    this.predecessors = new Array<BasicBlock>();
    this.successors = new Array<BasicBlock>();
    this.operations = new Array<Operation>();
  }
}

export abstract class HierarchicalGraphNode {
    graphKind: HierarchicalGraphKind;
    children: Array<HierarchicalGraphNode>;
    posX: number;
    posY: number;

    constructor(graphKind: HierarchicalGraphKind) {
        this.graphKind = graphKind;
    }
    
    public abstract successors(): Array<HierarchicalGraphNode>;
}

export class HGBasicBlock extends HierarchicalGraphNode {
    block: BasicBlock;

    constructor(block: BasicBlock) {
        super(HierarchicalGraphKind.BasicBlock);
        this.block = block;
        this.block.hg = this;
    }

    public override successors(): Array<HierarchicalGraphNode> {
        return this.block.successors.map(s => s.hg);
    }
}

export class HGSubgraph extends HierarchicalGraphNode {
    constructor() {
        super(HierarchicalGraphKind.Subgraph);
    }

    public override successors(): Array<HierarchicalGraphNode> {
        return null;
    }
}

export class HierarchicalGraph {
    basicBlocks: Array<BasicBlock>;
    root: HierarchicalGraphNode;

    constructor(basicBlocks: Array<BasicBlock>) {
        this.basicBlocks = basicBlocks;
        this.root = new HGSubgraph();
        this.root.children = basicBlocks.map(
            (basicBlock) => new HGBasicBlock(basicBlock));
        console.log("HierarchicalGraph.basicBlocks: ", this.basicBlocks);
    }
}


// class HierarchicalGraph_BasicBlock extends HierarchicalGraph {
//     constructor() {
//         super(HierarchicalGraphKind.BasicBlock);
//     }
// }
// 
// class HierarchicalGraph_Loop extends HierarchicalGraph {
//     constructor() {
//         super(HierarchicalGraphKind.Loop);
//     }
// }
// 
// class HierarchicalGraph_AcyclicSubgraph extends HierarchicalGraph {
//     constructor() {
//         super(HierarchicalGraphKind.AcyclicSubgraph);
//     }
// }
// 
// class HierarchicalGraph_Subgraph extends HierarchicalGraph {
//     constructor() {
//         super(HierarchicalGraphKind.Subgraph);
//     }
// }
