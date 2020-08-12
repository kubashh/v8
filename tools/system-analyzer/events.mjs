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
  }
  static get showEntries() {
    return "showentries";
  }
}

class FocusEvent extends CustomEvent {
  static showEntryDetail = "showentrydetail";
  constructor(entry) {
    super(FocusEvent.showEntryDetail, { bubbles: true, composed: true });
    this.entry = entry;
  }
  static get showEntryDetail() {
    return "showentrydetail";
  }
}

export { SelectionEvent, FocusEvent };
