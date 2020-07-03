// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {Group, properties} from './ic-model.js';

defineCustomElement('ic-panel', (templateText) =>
 class ICPanel extends HTMLElement {
  constructor() {
    super();
    const shadowRoot = this.attachShadow({mode: 'open'});
    shadowRoot.innerHTML = templateText;
    this.groupKeySelect.addEventListener(
        'change', e => this.updateTable(e));
    this.$('#filterICTimeBtn').addEventListener(
      'click', e => this.handleICTimeFilter(e));
    this._noOfItems = 100;
    this._startTime = 0;
    this._endTime = 0;
  }

  // DOM item selector
  $(id) {
    return this.shadowRoot.querySelector(id);
  }

  querySelectorAll(query) {
    return this.shadowRoot.querySelectorAll(query);
  }

  get groupKeySelect() {
    return this.$('#group-key');
  }

  get tableSelect() {
    return this.$('#table');
  }

  get tableBodySelect() {
    return this.$('#table-body');
  }

  get countSelect() {
    return this.$('#count');
  }

  get spanSelectAll(){
    return this.querySelectorAll("span");
  }

  // init
  receiveData(entries) {
    this.fileEntries = entries;
    this.entries = entries;
    this.updateTable();
  }

  // file entries
  set fileEntries(value){
    this._fileEntries = value;
  }

  get fileEntries(){
    return this._fileEntries;
  }

  set entries(value){
    this._entries = value;
  }

  get entries(){
    return this._entries;
  }

   filterEntriesByTime() {
    /*
    let newEntries = [];
    for (let entry of this.fileEntries) {
      if(entry.time >= this._startTime && entry.time <= this._endTime){
        newEntries.push(entry);
      }
    }
    */
    this.entries =  this.fileEntries.filter(e => e.time >= this._startTime && e.time <= this._endTime);
  }

  updateTable(event) {
    let select = this.groupKeySelect;
    let key = select.options[select.selectedIndex].text;
    let tableBody = this.tableBodySelect;
    this.removeAllChildren(tableBody);
    //this.processTimelineIC(entries);
    let groups = Group.groupBy(this.entries, key, true);
    console.log(groups);
    this.display(groups, tableBody);
  }

  escapeHtml(unsafe) {
    if (!unsafe) return "";
    return unsafe.toString()
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#039;");
  }
  processValue(unsafe) {
    if (!unsafe) return "";
    if (!unsafe.startsWith("http")) return this.escapeHtml(unsafe);
    let a = document.createElement("a");
    a.href = unsafe;
    a.textContent = unsafe;
    return a;
  }

  removeAllChildren(node) {
    while (node.firstChild) {
      node.removeChild(node.firstChild);
    }
  }

  td(tr, content, className) {
    let node = document.createElement("td");
    if (typeof content == "object") {
      node.appendChild(content);
    } else {
      node.innerHTML = content;
    }
    node.className = className;
    tr.appendChild(node);
    return node
  }

  set noOfItems(value){
    this._noOfItems = value;
  }

  get noOfItems(){
    return this._noOfItems;
  }

  display(entries, parent) {
    let fragment = document.createDocumentFragment();
    //let max = Math.min(100, entries.length);
    let max = entries.length;
    for (let i = 0; i < max; i++) {
      let entry = entries[i];
      let tr = document.createElement("tr");
      tr.entry = entry;
      let details = this.td(tr,'<span>&#8505;</a>', 'details');
      details.onclick = _ => this.toggleDetails(details);
      this.td(tr, entry.percentage + "%", 'percentage');
      this.td(tr, entry.count, 'count');
      this.td(tr, this.processValue(entry.key), 'key');
      fragment.appendChild(tr);
    }
    let omitted = entries.length - max;
    if (omitted > 0) {
      let tr = document.createElement("tr");
      let tdNode = td(tr, 'Omitted ' + omitted + " entries.");
      tdNode.colSpan = 4;
      fragment.appendChild(tr);
    }
    parent.appendChild(fragment);
  }


  displayDrilldown(entry, previousSibling) {
    let tr = document.createElement('tr');
    tr.className = "entry-details";
    tr.style.display = "none";
    // indent by one td.
    tr.appendChild(document.createElement("td"));
    let td = document.createElement("td");
    td.colSpan = 3;
    for (let key in entry.groups) {
      td.appendChild(this.displayDrilldownGroup(entry, key));
    }
    tr.appendChild(td);
    // Append the new TR after previousSibling.
    previousSibling.parentNode.insertBefore(tr, previousSibling.nextSibling)
  }

  displayDrilldownGroup(entry, key) {
    let max = 20;
    let group = entry.groups[key];
    let div = document.createElement("div")
    div.className = 'drilldown-group-title'
    div.textContent = key + ' [top ' + max + ' out of ' + group.length + ']';
    let table = document.createElement("table");
    this.display(group.slice(0, max), table, false)
    div.appendChild(table);
    return div;
  }

 toggleDetails(node) {
  let tr = node.parentNode;
  let entry = tr.entry;
  // Create subgroup in-place if the don't exist yet.
  if (entry.groups === undefined) {
    entry.createSubGroups();
    this.displayDrilldown(entry, tr);
  }
  let details = tr.nextSibling;
  let display = details.style.display;
  if (display != "none") {
    display = "none";
  } else {
    display = "table-row"
  };
  details.style.display = display;
  }

  initGroupKeySelect() {
    let select = this.groupKeySelect;
    select.options.length = 0;
    for (let i in properties) {
      let option = document.createElement("option");
      option.text = properties[i];
      select.add(option);
    }
  }

  //TODO(ZC): Emit event if the address has a valid V8-Map state
  sendMapClickedEvent(entry){
    let dataModel = Object.create(null);
    let selectedMap = V8Map.get("0x" + entry.map);
    if(selectedMap){
      dataModel.map = selectedMap;
    }
    this.dispatchEvent(new CustomEvent(
      'map-click', {bubbles: true, composed: true, detail: dataModel}));
  }


  //TODO(zc): Function processing the timestamps of ICEvents
  // Processes the IC Events which have V8Map's in the map-processor
  /*
  processICEventTime(){
    let ICTimeToEvent = new Map();
    let eventTimes = []
    console.log("Num of entries: " + entries.length);
    entries.forEach(element => {
      let v8Map = V8Map.get("0x" + element.map);
      if(!v8Map){
        ICTimeToEvent.set(-1, element);
      } else {
        ICTimeToEvent.set(v8Map.time, element);
        eventTimes.push(v8Map.time);
      }
    });
    eventTimes.sort();
    // save the IC events which have Map states
    let eventsList = [];
    for(let i = 0;  i < eventTimes.length; i++){
      eventsList.push(ICTimeToEvent.get(eventTimes[i]));
    }
    return eventList;
  }
  */

  /*
  handleICFilter(e){
    let noOfItemsInput = parseInt(this.$('#filter-input').value);
    this.noOfItems = noOfItemsInput;
    this.updateTable(e);
  }
  */

   handleICTimeFilter(e) {
    this._startTime = parseInt(this.$('#filter-time-start').value);
    console.log(this._startTime);
    console.assert(this._startTime >= 0, { errorMsg: "start time must be a non-negative integer!" });
    this._endTime = parseInt(this.$('#filter-time-end').value);
    console.assert(this._endTime <= this._fileEntries[this._fileEntries.length - 1].time,
      { errorMsg: "end time must be smaller or equal to the the time of the last event!" });
    console.assert(this._startTime < this._endTime,
      { errorMsg: "end time must be smaller than the start time!" });
    this.filterEntriesByTime();
    this.updateTable(e);
  }

  // process IC events and display in a Timeline view
  //TODO(zc): new functionality
  /*
  processTimelineIC(entries){
    let parent = this.$('#details')
    let marginLeft = 0;
    for (let index = 0; index < entries.length; index++) {
      const entry = entries[index];
      let div = document.createElement("div");
      div.className = 'TL-cell';
      div.style = "width:150px; margin-left:0px;";
      let entryDetails = "type: " + entry.type + " category: " + entry.category + ' (' + entry.oldState + '->' + entry.newState + ') ' +
      ' (map: 0x' + entry.map.toString(16) + ')' + " key: " + entry.key;
      //console.log(entry);
      div.innerHTML = entryDetails;
      marginLeft += 100;
      parent.append(div);
    }
  }
  */
});
