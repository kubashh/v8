// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as d3 from "d3";
import { PhaseView } from "./phase-view";

export abstract class MovableView<GraphType> extends PhaseView {
    panZoom: d3.ZoomBehavior<SVGElement, any>;
    divElement: d3.Selection<any, any, any, any>;
    graphElement: d3.Selection<any, any, any, any>;
    svg: d3.Selection<any, any, any, any>;

    protected abstract onKeyDown(event: any): boolean;
 
    constructor(htmlParentElement: HTMLElement) {
        super(htmlParentElement);

        this.divElement = d3.select(this.htmlElement);

        this.svg = this.divElement.append("svg")
      .attr("version", "2.0")
      .attr("width", "100%")
      .attr("height", "100%")
      .on("focus", () => { })
      .on("keydown", () => {
        let eventHandled = this.onKeyDown(d3.event);
        if(eventHandled) d3.event.preventDefault();
      });

    this.svg.append("svg:defs")
      .append("svg:marker")
      .attr("id", "end-arrow")
      .attr("viewBox", "0 -4 8 8")
      .attr("refX", 2)
      .attr("markerWidth", 2.5)
      .attr("markerHeight", 2.5)
      .attr("orient", "auto")
      .append("svg:path")
      .attr("d", "M0,-4L8,0L0,4");

    this.graphElement = this.svg.append("g");
 
        this.panZoom = d3.zoom<SVGElement, any>()
            .scaleExtent([0.2, 40])
            .on("zoom", () => {
                if(d3.event.shiftKey) return false;
                this.graphElement.attr("transform", d3.event.transform);
                return true;
            })
            .on("start", () => {
                if(d3.event.shiftKey) return;
                d3.select("body").style("cursor", "move");
            })
            .on("end", () => d3.select("body").style("cursor", "auto"));

        this.svg.call(this.panZoom).on("dblclick.zoom", null);
    }
}
