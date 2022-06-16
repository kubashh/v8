// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { measureText } from "./common/util";
import { GraphEdge } from "./phases/graph-phase/graph-edge";
import { TurboshaftGraphEdge } from "./phases/turboshaft-graph-phase/turboshaft-graph-edge";

export abstract class Node<EdgeType extends GraphEdge | TurboshaftGraphEdge> {
  id: number;
  displayLabel: string;
  inputs: Array<EdgeType>;
  outputs: Array<EdgeType>;
  visible: boolean;
  x: number;
  y: number;
  labelbbox: { width: number, height: number };
  visitOrderWithinRank: number;

  constructor(id: number, displayLabel?: string) {
    this.id = id;
    this.displayLabel = displayLabel;
    this.inputs = new Array<EdgeType>();
    this.outputs = new Array<EdgeType>();
    this.visible = false;
    this.x = 0;
    this.y = 0;
    this.labelbbox = measureText(this.displayLabel);
    this.visitOrderWithinRank = 0;
  }

  areAnyOutputsVisible() {
    let visibleCount = 0;
    this.outputs.forEach(function (e) { if (e.isVisible())++visibleCount; });
    if (this.outputs.length == visibleCount) return 2;
    if (visibleCount != 0) return 1;
    return 0;
  }

  setOutputVisibility(v) {
    let result = false;
    this.outputs.forEach(function (e) {
      e.visible = v;
      if (v) {
        if (!e.target.visible) {
          e.target.visible = true;
          result = true;
        }
      }
    });
    return result;
  }

  setInputVisibility(i, v) {
    const edge = this.inputs[i];
    edge.visible = v;
    if (v) {
      if (!edge.source.visible) {
        edge.source.visible = true;
        return true;
      }
    }
    return false;
  }

  public identifier = (): string => `${this.id}`;
  public toString = (): string => `N${this.id}`;
}
