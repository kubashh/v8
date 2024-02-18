// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as d3 from "d3";
import { Dimensions, Position } from "./util";
import { MovableView } from "./movable-view";
import { HGBasicBlock, HierarchicalGraph, HierarchicalGraphKind, HierarchicalGraphNode, Operation } from "./hierarchical-graph";

type Svg = d3.Selection<SVGGElement, TurboshaftViewNode, null, undefined>;
const VERTICAL_NODE_SPACING = 50;

class TurboshaftViewEdge {
    source: TurboshaftViewNode;
    target: TurboshaftViewNode;

    constructor(source: TurboshaftViewNode, target: TurboshaftViewNode) {
        this.source = source;
        this.target = target;
    }
}

class TurboshaftViewNode {
    view: TurboshaftView;
    visualized: HierarchicalGraphNode;
    // Display properties
//    rank: number;
    posX: number;
    posY: number;

    dimensions: Dimensions;
    labelDimensions: Dimensions;
    operationDimensions: Array<Dimensions>;

    constructor(view: TurboshaftView, visualized: HierarchicalGraphNode) {
        this.view = view;
        this.visualized= visualized;
        this.posX = 0;
        this.posY = 0;
        this.ComputeDimensions();
    }

    private ComputeDimensions(): void {
        switch(this.visualized.graphKind) {
            case HierarchicalGraphKind.BasicBlock:
                const bb = (this.visualized as HGBasicBlock).block;
                this.labelDimensions = this.view.MeasureText(this.GetLabel());
                this.dimensions = new Dimensions(this.labelDimensions.width,
                        this.labelDimensions.height);
                this.operationDimensions = bb.operations.map((op: Operation) => {
                    const operationDimensions = this.view.MeasureText(op.title);
                    this.dimensions.GrowHeight(operationDimensions);
                    return operationDimensions;
                });
                break;
            default:
                console.log("UNIMPLEMENTED");
                break;
        }
    }

    public getWidth(): number {
        return this.dimensions.width;
        /*
        console.log("getWidth()");
        const TURBOSHAFT_COLLAPSE_ICON_X_INDENT = 20;
        const TURBOSHAFT_OPERATION_X_MARGIN = 25;
        if(!this.width) {
            switch(this.node.graphKind) {
                case HierarchicalGraphKind.BasicBlock:
                    const bb = (this.node as HGBasicBlock).block;
                    const labelBox = this.view.MeasureText("Block 100");
                    const labelWidth = labelBox.width + labelBox.height
                        + TURBOSHAFT_COLLAPSE_ICON_X_INDENT;
                    const maxOperationWidth = Math.max(...bb.operations.map(
                        (op: Operation) => this.view.ComputeOperationWidth(op)));
                    this.width = Math.max(maxOperationWidth, labelWidth)
                        + 2 * TURBOSHAFT_OPERATION_X_MARGIN;
                break;
                default:
                    console.log("UNIMPLEMENTED");
                    break;
            }
        }
        return this.width;
        */
    }

    public getHeight(): number {
        return this.dimensions.height;
    }

    public GetLabel(): string {
        switch(this.visualized.graphKind) {
           case HierarchicalGraphKind.BasicBlock:
                const bb = (this.visualized as HGBasicBlock).block;
                return `${bb.kind} ${bb.id}`;
            default:
                console.log("UNIMPLEMENTED");
                return "UNIMPLEMENTED";
        }
    }

//    public resetDisplayProperties() {
//        this.rank = -1;
//        this.posX = 0;
//        this.posY = 0;
//    }

    public successors(): Array<TurboshaftViewNode> {
        return this.view.mapNodes(this.visualized.successors());
    }

    public SetupNode(view: TurboshaftView, svg: Svg): void {
            // Outer frame
            svg
                .append("text")
                .classed("block-label", true)
                .attr("text-anchor", "middle")
                .attr("x", this.getWidth() / 2)
                .append("tspan")
                .text(this.GetLabel())
                .append("title")
                .text("myTitle");
            
            // Build content
            // (see appendInlineNodes)
            this.Render(view, svg);
    }

    public Render(view: TurboshaftView, svg: Svg): void {
        switch(this.visualized.graphKind) {
            case HierarchicalGraphKind.BasicBlock:
                const operations = (this.visualized as HGBasicBlock).block.operations;
                const allOperations = svg.selectAll<SVGGElement, TurboshaftViewNode>(".operation");
                const visibleOperations = allOperations.data(operations);

                // remove old operations
                visibleOperations.exit().remove();
                // add new operations
                const newOperations = visibleOperations 
                    .enter()
                    .append("g")
                    .classed("operation", true);

                let node = this;
                let nodeY = this.labelDimensions.height;
                console.log("nodeY: ", nodeY);
                newOperations.each(function (op: Operation, index: number) {
                    const operationSvg = d3.select(this);
                    console.log("operation-title: ", op.title);
                    operationSvg
                        .attr("id", op.id)
                        .append("text")
                        .attr("dx", 25)
                        .classed("inline-node-label", true)
                        .attr("dy", nodeY)
                        .append("tspan")
                        .text(op.title)
                        .append("title")
                        .text("Tooltip (Todo)");
                    nodeY += node.operationDimensions[index].height;
                });
                break;
            default:
                // TODO
                break;
        }
    }
}

export class TurboshaftView extends MovableView<HierarchicalGraph> {
    graph: HierarchicalGraph;
    nodes: Array<TurboshaftViewNode>;
    edges: Array<TurboshaftViewEdge>;
    rootNode: TurboshaftViewNode;
    visibleBlocks: d3.Selection<any, TurboshaftViewNode, any, any>;
    visibleEdges: d3.Selection<any, TurboshaftViewEdge, any, any>; 
    viewMap: Map<HierarchicalGraphNode, TurboshaftViewNode>;
    
    constructor(htmlParentElement: HTMLElement) {
        super(htmlParentElement);

        this.visibleEdges = this.graphElement.append("g");
        this.visibleBlocks = this.graphElement.append("g");

    }

    public override onKeyDown(event: any): boolean {
        // TODO(nicohartmann@): Handle some keys.
        return false;
    }

    public override createViewElement(): HTMLDivElement {
        const div = document.createElement("div");
        div.setAttribute("style", "background-color:ltgray;height:100vh");
        return div;
    }

    public MeasureText(text: string, coefficient: number = 1): Dimensions {
        const textMeasure = document.getElementById("text-measure");
        if(textMeasure instanceof SVGTSpanElement) {
            textMeasure.textContent = text;
            return new Dimensions(
                textMeasure.getBBox().width * coefficient,
                textMeasure.getBBox().height * coefficient
            );
        }
        console.assert(false, "No text-measure found");
        return new Dimensions(0, 0);
    }

    public ComputeOperationWidth(op: Operation): number {
        const box = this.MeasureText(op.title);
        return box.width;
    }

    // Temp
    public displayGraph(graph: HierarchicalGraph) {
        console.log("displayGraph");
        const view = this;
        this.graph = graph;
        this.nodes = graph.basicBlocks.map((b, i) =>
            new TurboshaftViewNode(this, b.hg)
        );
        this.viewMap = new Map<HierarchicalGraphNode, TurboshaftViewNode>();
        this.edges = new Array<TurboshaftViewEdge>;
        this.nodes.forEach(function(node: TurboshaftViewNode) {
            view.viewMap.set(node.visualized, node);
            view.edges.push(...node.successors().map(s => new TurboshaftViewEdge(node, s)));
        });
        this.rootNode = this.nodes[0];

        const allBlocks = this.visibleBlocks.selectAll<SVGGElement, TurboshaftViewNode>(".turboshaft-block");
        const selBlocks = allBlocks.data(this.nodes, (block,i) => i.toString());

        // remove old blocks
        selBlocks.exit().remove();

        // add new blocks
        const newBlocks = selBlocks
            .enter()
            .append("g")
            .classed("turboshaft-block", true)
            .classed("block", true);

        newBlocks.append("rect")
            .attr("rx", 35)
            .attr("ry", 35)
            .attr("width", d => d.getWidth())
            .attr("height", d => d.getHeight());

        newBlocks.each(function(node: TurboshaftViewNode) {
            const svg = d3.select<SVGGElement, TurboshaftViewNode>(this);
            node.SetupNode(view, svg);
        });

        const newAndOldBlocks = newBlocks.merge(selBlocks);

        this.layoutGraph();

        newAndOldBlocks.classed("selected", true)
            .attr("transform", node => `translate(${node.posX},${node.posY})`)
            .select("rect");


        // select existing edges
        const filteredEdges = 
    }

    public mapNode(node: HierarchicalGraphNode): TurboshaftViewNode {
        return this.viewMap.get(node);
    }

    public mapNodes(nodes: Array<HierarchicalGraphNode>): Array<TurboshaftViewNode> {
        return nodes.map(n => this.mapNode(n));
    }

    private visitNodeForLayout(node: TurboshaftViewNode, topCenter: Position): Position {
        const visualized = node.visualized;
        switch(visualized.graphKind) {
            case HierarchicalGraphKind.BasicBlock:
//                const bb = (visualized as HGBasicBlock);
                node.posX = topCenter.x - node.getWidth() / 2;
                node.posY = topCenter.y;
                return new Position(topCenter.x, node.posY + node.getHeight());
            default:
                console.log("UNIMPLEMENTED");
                return new Position();
        }
    }

    private layoutGraph(): void {
        if(this.rootNode == null) return;
        let nextNodeCenterPosition = new Position(0, 0);
        let queue = new Array<TurboshaftViewNode>();
        queue.push(this.rootNode);
        while(queue.length > 0) {
            const nextNode = queue.shift();
            nextNodeCenterPosition = this.visitNodeForLayout(nextNode, nextNodeCenterPosition);
            nextNodeCenterPosition.y += VERTICAL_NODE_SPACING;
            const successors = nextNode.successors();
            if(successors) queue.push(...successors);
        }
    }

//    public getViewNode(hgNode: HierarchicalGraphNode): TurboshaftViewNode {
//        console.log("viewMap: ", this.viewMap);
//        return this.viewMap.get(hgNode);
//    }
//
//    private layoutNodes() {
//        this.nodes.forEach(n => n.resetDisplayProperties());
//        let nodesPerRank = new Array<Array<TurboshaftViewNode>>(this.nodes.length);
//        
//        function assignRank(node: TurboshaftViewNode, rank: number) {
//            node.rank = rank;
//            if(!nodesPerRank[rank]) nodesPerRank[rank] = new Array();
//            nodesPerRank[rank].push(node);
//            node.successors().forEach(s => assignRank(s, rank + 1));
//        }
//        assignRank(this.getViewNode(this.graph.root), 0);
//
//        for(let rank = 0; rank < nodesPerRank.length; ++rank) {
//            if(nodesPerRank[rank].length == 0) break;
//            let x = 0;
//            nodesPerRank[rank].forEach(n => {
//                n.posY = rank * RANK_HEIGHT;
//                n.posX = x * NODE_WIDTH;
//                ++x;
//            });
//        }
//
//        this.graphElement.selectAll("rect").data(this.nodes)
//        .join(null, update => update.attr("x", n => n.posX).attr("y", n => n.posY));
//        
//        
//        //.append("rect").attr("x", n => n.posX).attr("y", n => n.posY);
//
//        // .attr("cx", (d) => 100).attr("cy", (d) => 100).attr("r", 50);
//    }
}
