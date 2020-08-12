// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class SelectionEvent extends CustomEvent {
  constructor(entries) {
    super(SelectionEvent.showEntries, { bubbles: true, composed: true });
    if (!Array.isArray(entries) || entries.length == 0) {
      throw new Error("No valid entries selected!");
    }
    this.entries = entries;
    this.name = "showentries";
  }
  static get showEntries() {
    return this.name;
  }
}

class FocusEvent extends CustomEvent {
  static showEntryDetail = "showentrydetail";
  constructor(entry) {
    super(FocusEvent.showEntryDetail, { bubbles: true, composed: true });
    this.entry = entry;
    this.name = "showentrydetail";
  }
  static get showEntryDetail() {
    return this.name;
  }
}

export { SelectionEvent, FocusEvent };
