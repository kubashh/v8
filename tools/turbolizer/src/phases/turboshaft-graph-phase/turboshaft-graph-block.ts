// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export class TurboshaftGraphBlock {
  id: string;
  type: TurboshaftGraphBlockType;
  deferred: boolean;
  predecessors: Array<string>;

  constructor(id: string, type: TurboshaftGraphBlockType, deferred: boolean, predecessors: Array<string>) {
    this.id = id;
    this.type = type;
    this.deferred = deferred;
    this.predecessors = predecessors ?? new Array<string>();
  }
}

export enum TurboshaftGraphBlockType {
  Loop = "LOOP",
  Merge = "MERGE",
  Block = "BLOCK"
}
