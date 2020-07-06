// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

defineCustomElement('timeline-panel', (templateText) =>
 class TimelinePanel extends HTMLElement {
  constructor() {
    super();
    const shadowRoot = this.attachShadow({mode: 'open'});
    shadowRoot.innerHTML = templateText;
    this.timelineOverviewSelect.addEventListener(
      'mousemove', e => this.handleTimelineIndicatorMove(e));
    //TODO(zc) trigger resize event
    this.$('div.leftMask').addEventListener(
      'mousedown', e => this.handleTimelineIndicatorResizeMouseDown(e));
    this.$('div.rightMask').addEventListener(
      'mousedown', e => this.handleTimelineIndicatorResizeMouseDown(e));
    window.addEventListener(
      'mousemove', e => this.handleTimelineIndicatorResizeMouseMove(e));
    window.addEventListener(
      'mouseup', e => this.handleTimelineIndicatorResizeMouseUp(e));
  }

  $(id) {
    return this.shadowRoot.querySelector(id);
  }

  querySelectorAll(query) {
    return this.shadowRoot.querySelectorAll(query);
  }

  get timelineOverviewSelect() {
    return this.$('#timelineOverview');
  }

  get timelineOverviewIndicatorSelect() {
    return this.$('#timelineOverviewIndicator');
  }

  get timelineCanvasSelect() {
    return this.$('#timelineCanvas');
  }

  get timelineChunksSelect() {
    return this.$('#timelineChunks');
  }

  get timelineSelect() {
    return this.$('#timeline');
  }


  handleTimelineIndicatorMove(event) {
    if (event.buttons == 0) return;
    let timelineTotalWidth = this.timelineCanvasSelect.offsetWidth;
    let factor = this.timelineOverviewSelect.offsetWidth / timelineTotalWidth;
    this.timelineSelect.scrollLeft += event.movementX / factor;
  }

  handleTimelineIndicatorResizeMouseDown(e) {
    const timelineOverviewIndicator = this.timelineOverviewIndicatorSelect;
    if (e.buttons == 0) return;
    console.log("resize initiated");
    this.resize = true;
    let original_width = parseFloat(timelineOverviewIndicator.clientWidth);
    let original_mouse_x = e.pageX;
    let original_x = timelineOverviewIndicator.getBoundingClientRect().left;
    let width = original_width + (e.pageX - original_mouse_x);
    console.log("original_width: ", original_width, " original_mouse_x: ", original_mouse_x, " original_x: ", original_x, " width: ", width);
  }

  handleTimelineIndicatorResizeMouseMove(e) {
    const timelineOverviewIndicator = this.timelineOverviewIndicatorSelect;
    if (this.resize === true) {
      console.log("resize continue");
    }
  }

  handleTimelineIndicatorResizeMouseUp(e) {
    const timelineOverviewIndicator = this.timelineOverviewIndicatorSelect;
    let original_width = parseFloat(timelineOverviewIndicator.clientWidth);
    let original_mouse_x = e.pageX;
    let original_x = timelineOverviewIndicator.getBoundingClientRect().left;
    const width = original_width + (e.pageX - original_mouse_x);
    if (this.resize === true) {
      console.log("end resize");
      //TODO(zc) It automatically resets itself...
      timelineOverviewIndicator.style.width = width + 100 + 'px';
      this.resize = false;
    }
  }

});







