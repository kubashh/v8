// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "../../common/constants";
import { NodeLabel } from "../../node-label";
import { alignUp, measureText } from "../../common/util";
import { Node } from "../../node";
import { GraphEdge } from "./graph-edge";

export class GraphNode extends Node<GraphEdge> {
  nodeLabel: NodeLabel;
  rank: number;
  outputApproach: number;
  cfg: boolean;
  width: number;
  normalheight: number;

  constructor(nodeLabel: NodeLabel) {
    super(nodeLabel.id, nodeLabel.getDisplayLabel());
    this.nodeLabel = nodeLabel;
    this.rank = C.MAX_RANK_SENTINEL;
    this.outputApproach = C.MINIMUM_NODE_OUTPUT_APPROACH;
    // Every control node is a CFG node.
    this.cfg = nodeLabel.control;
    const typebbox = measureText(this.getDisplayType());
    const innerwidth = Math.max(this.labelbbox.width, typebbox.width);
    this.width = alignUp(innerwidth + C.NODE_INPUT_WIDTH * 2, C.NODE_INPUT_WIDTH);
    const innerheight = Math.max(this.labelbbox.height, typebbox.height);
    this.normalheight = innerheight + 20;
  }

  deepestInputRank() {
    let deepestRank = 0;
    this.inputs.forEach(function (e) {
      if (e.isVisible() && !e.isBackEdge()) {
        if (e.source.rank > deepestRank) {
          deepestRank = e.source.rank;
        }
      }
    });
    return deepestRank;
  }

  isControl() {
    return this.nodeLabel.control;
  }
  isInput() {
    return this.nodeLabel.opcode == 'Parameter' || this.nodeLabel.opcode.endsWith('Constant');
  }
  isLive() {
    return this.nodeLabel.live !== false;
  }
  isJavaScript() {
    return this.nodeLabel.opcode.startsWith('JS');
  }
  isSimplified() {
    if (this.isJavaScript()) return false;
    const opcode = this.nodeLabel.opcode;
    return opcode.endsWith('Phi') ||
      opcode.startsWith('Boolean') ||
      opcode.startsWith('Number') ||
      opcode.startsWith('String') ||
      opcode.startsWith('Change') ||
      opcode.startsWith('Object') ||
      opcode.startsWith('Reference') ||
      opcode.startsWith('Any') ||
      opcode.endsWith('ToNumber') ||
      (opcode == 'AnyToBoolean') ||
      (opcode.startsWith('Load') && opcode.length > 4) ||
      (opcode.startsWith('Store') && opcode.length > 5);
  }
  isMachine() {
    return !(this.isControl() || this.isInput() ||
      this.isJavaScript() || this.isSimplified());
  }
  getTotalNodeWidth() {
    const inputWidth = this.inputs.length * C.NODE_INPUT_WIDTH;
    return Math.max(inputWidth, this.width);
  }
  getTitle() {
    return this.nodeLabel.getTitle();
  }
  getDisplayLabel() {
    return this.nodeLabel.getDisplayLabel();
  }
  getType() {
    return this.nodeLabel.type;
  }
  getDisplayType() {
    let typeString = this.nodeLabel.type;
    if (typeString == undefined) return "";
    if (typeString.length > 24) {
      typeString = typeString.substr(0, 25) + "...";
    }
    return typeString;
  }
  getInputApproach(index) {
    return this.y - C.MINIMUM_NODE_INPUT_APPROACH -
      (index % 4) * C.MINIMUM_EDGE_SEPARATION - C.DEFAULT_NODE_BUBBLE_RADIUS;
  }
  getNodeHeight(showTypes: boolean): number {
    if (showTypes) {
      return this.normalheight + this.labelbbox.height;
    } else {
      return this.normalheight;
    }
  }
  getOutputApproach(showTypes: boolean) {
    return this.y + this.outputApproach + this.getNodeHeight(showTypes) +
      + C.DEFAULT_NODE_BUBBLE_RADIUS;
  }
  getInputX(index) {
    const result = this.getTotalNodeWidth() - (C.NODE_INPUT_WIDTH / 2) +
      (index - this.inputs.length + 1) * C.NODE_INPUT_WIDTH;
    return result;
  }
  getOutputX() {
    return this.getTotalNodeWidth() - (C.NODE_INPUT_WIDTH / 2);
  }
  hasBackEdges() {
    return (this.nodeLabel.opcode == "Loop") ||
      ((this.nodeLabel.opcode == "Phi" || this.nodeLabel.opcode == "EffectPhi" || this.nodeLabel.opcode == "InductionVariablePhi") &&
        this.inputs[this.inputs.length - 1].source.nodeLabel.opcode == "Loop");
  }
}
