
// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class SelectionLogEvent extends CustomEvent {
  constructor(entries){
    super('showentries', {bubbles: true, composed: true});
    if(!Array.isArray(entries) || entries.length == 0){
      throw new Error('No valid entries selected!')
    }
    this.entries = entries;
  }
}

class FocusLogEvent extends CustomEvent {
  constructor(entry){
    super('showentrydetail', {bubbles: true, composed: true});
    this.entry = entry;
  }
}

export {SelectionLogEvent, FocusLogEvent};