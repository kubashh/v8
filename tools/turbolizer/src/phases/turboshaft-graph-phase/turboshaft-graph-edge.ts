// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { TurboshaftGraphNode } from "./turboshaft-graph-node";

export class TurboshaftGraphEdge {
  target: TurboshaftGraphNode;
  source: TurboshaftGraphNode;

  constructor(target: TurboshaftGraphNode, source: TurboshaftGraphNode) {
    this.target = target;
    this.source = source;
  }
}
