
// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class MapProcessor extends LogReader {
  constructor() {
    super();
    this.dispatchTable_ = {
        'code-creation': {
            parsers: [null, parseInt, parseInt, parseInt, parseInt, null, 'var-args'],
            processor: this.processCodeCreation },
        'code-move': { parsers: [parseInt, parseInt],
        'sfi-move': { parsers: [parseInt, parseInt],
          processor: this.processCodeMove },
        'code-delete': { parsers: [parseInt],
          processor: this.processCodeDelete },
            processor: this.processFunctionMove },
        'map': {
          parsers : [null,parseInt, parseInt, parseInt, parseInt, parseInt, null, 'var-args'],
          processor: this.processMap
        },
        'map-details': {
          parsers: [parseInt, null],
          processor: this.processMapDetails
        }
    };
    this.deserializedEntriesNames_ = [];
    this.profile_ = new Profile();
  }

  printError(str) {
    print(str);
    throw str
  }

  processString(string) {
    var end = string.length;
    var current = 0;
    var next = 0;
    var line;
    var i = 0;
    var entry;
    while (current < end) {
      next = string.indexOf("\n", current);
      if (next === -1) break;
      i++;
      line = string.substring(current, next);
      current = next + 1;
      this.processLogLine(line);
    }
  }

  processLogFile(fileName) {
    this.collectEntries = true
    this.lastLogFileName_ = fileName;
    var line;
    while (line = readline()) {
      this.processLogLine(line);
    }
    print();
    print("=====================");
    print("Load: " + this.LoadIC);
    print("Store: " + this.StoreIC);
    print("KeyedLoad: " + this.KeyedLoadIC);
    print("KeyedStore: " + this.KeyedStoreIC);
  }

  addEntry(entry) {
    this.entries.push(entry);
  }

  /**
   * Parser for dynamic code optimization state.
   */
  parseState(s) {
    switch (s) {
    case "": return Profile.CodeState.COMPILED;
    case "~": return Profile.CodeState.OPTIMIZABLE;
    case "*": return Profile.CodeState.OPTIMIZED;
    }
    throw new Error("unknown code state: " + s);
  }

  processCodeCreation(
      type, kind, timestamp, start, size, name, maybe_func) {
    name = this.deserializedEntriesNames_[start] || name;
    if (name.startsWith("onComplete")) {
      console.log(name);
    }
    if (maybe_func.length) {
      var funcAddr = parseInt(maybe_func[0]);
      var state = this.parseState(maybe_func[1]);
      this.profile_.addFuncCode(type, name, timestamp, start, size, funcAddr, state);
    } else {
      this.profile_.addCode(type, name, timestamp, start, size);
    }
  }

  processCodeMove(from, to) {
    this.profile_.moveCode(from, to);
  }

  processCodeDelete(start) {
    this.profile_.deleteCode(start);
  }

  processFunctionMove(from, to) {
    this.profile_.moveFunc(from, to);
  }

  processMap(type, time, from, to, pc, line, column, reason, maybeName) {
    console.log(...arguments)

  }

  processMapDetails(id, string) {

  }

}


// ===========================================================================



// ===========================================================================

function ArgumentsProcessor(args) {
  this.args_ = args;
  this.result_ = ArgumentsProcessor.DEFAULTS;

  this.argsDispatch_ = {
    '--range': ['range', 'auto,auto',
        'Specify the range limit as [start],[end]'],
    '--source-map': ['sourceMap', null,
        'Specify the source map that should be used for output']
  };
};


ArgumentsProcessor.DEFAULTS = {
  logFileName: 'v8.log',
  range: 'auto,auto',
};


ArgumentsProcessor.prototype.parse = function() {
  while (this.args_.length) {
    var arg = this.args_.shift();
    if (arg.charAt(0) != '-') {
      this.result_.logFileName = arg;
      continue;
    }
    var userValue = null;
    var eqPos = arg.indexOf('=');
    if (eqPos != -1) {
      userValue = arg.substr(eqPos + 1);
      arg = arg.substr(0, eqPos);
    }
    if (arg in this.argsDispatch_) {
      var dispatch = this.argsDispatch_[arg];
      this.result_[dispatch[0]] = userValue == null ? dispatch[1] : userValue;
    } else {
      return false;
    }
  }
  return true;
};


ArgumentsProcessor.prototype.result = function() {
  return this.result_;
};


ArgumentsProcessor.prototype.printUsageAndExit = function() {

  function padRight(s, len) {
    s = s.toString();
    if (s.length < len) {
      s = s + (new Array(len - s.length + 1).join(' '));
    }
    return s;
  }

  print('Cmdline args: [options] [log-file-name]\n' +
        'Default log file name is "' +
        ArgumentsProcessor.DEFAULTS.logFileName + '".\n');
  print('Options:');
  for (var arg in this.argsDispatch_) {
    var synonyms = [arg];
    var dispatch = this.argsDispatch_[arg];
    for (var synArg in this.argsDispatch_) {
      if (arg !== synArg && dispatch === this.argsDispatch_[synArg]) {
        synonyms.push(synArg);
        delete this.argsDispatch_[synArg];
      }
    }
    print('  ' + synonyms.join(', ').padStart(20) + " " + dispatch[2]);
  }
  quit(2);
};
