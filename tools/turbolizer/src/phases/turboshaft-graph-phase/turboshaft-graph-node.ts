// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { TurboshaftGraphEdge } from "./turboshaft-graph-edge";
import { TurboshaftGraphBlock } from "./turboshaft-graph-block";

export class TurboshaftGraphNode {
  id: number;
  title: string;
  block: TurboshaftGraphBlock;
  inputs: Array<TurboshaftGraphEdge>;
  outputs: Array<TurboshaftGraphEdge>;
  properties: string;

  constructor(id: number, title: string, block: TurboshaftGraphBlock, properties: string) {
    this.id = id;
    this.title = title;
    this.block = block;
    this.properties = properties;
    this.inputs = new Array<TurboshaftGraphEdge>();
    this.outputs = new Array<TurboshaftGraphEdge>();
  }
}
