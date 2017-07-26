// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function inherits(childCtor, parentCtor) {
  childCtor.prototype.__proto__ = parentCtor.prototype;
};

/**
 * A thin wrapper around shell's 'read' function showing a file name on error.
 */
function readFile(fileName) {
  try {
    return read(fileName);
  } catch (e) {
    print(fileName + ': ' + (e.message || e));
    throw e;
  }
}

class Script {
  constructor(file, id) {
    this.file = file;
    this.id = id;
    this.funktions = [];
    this.print();
  }
  
  funktionAtPosition(start) {
    return this.funktions[start];
  }

  addMissingFunktions(list) {
    list.forEach(fn => {
      if (this.funktions[fn.start] === undefined) {
        this.addFunktion(fn);
      }
    });

  }

  addFunktion(fn) {
    if (fn.start == undefined) {
      throw new Error("function has no start position");
    }
    if (this.funktions[fn.start] !== undefined) {
      fn.print();
      throw new Error("adding same function twice to script");
    }
    this.funktions[fn.start] = fn;
  }

  inferFunktionParents() {
    let parent;
    this.funktions.forEach( fn => {
      if (!parent) {
        parent = fn;
      } else {
        // Walk up the nested chain of Funktions to find the parent.
        while (parent != undefined && !fn.isNestedIn(parent)) {
          parent = parent.parent;
        }
        fn.parent = parent;
        if (parent) parent.addNestedFunktion(fn);
        parent = fn;
      }
    });
  }

  print() {
    console.log(`SCRIPT id=${this.id} file=${this.file}\nfunctions[${this.funktions.length}]:`);
    this.funktions.forEach(fn => fn.print());
  }
}




class Funktion {
  constructor(name, start, end, script, timestamp) {
    if (start < 0) throw new Error("invalid start position: " + start);
    if (end <= 0) throw new Error("invalid end position: " + end);
    this.name = name;
    this.start = start;
    this.end = end;
    this.script = script;
    this.parent = undefined;
    this.nested = [];
    this.parseTimestamp = timestamp;
    this.executionTimestamp = -1;
    this.preparseTime = 0;
    this.parseTime = 0;
    this.scopeResolutionTime = 0;
    if (script) this.script.addFunktion(this);
  }

  isNestedIn(funktion) {
    if (this.script != funktion.script) throw new Error("Incompatible script");
    return funktion.start < this.start && this.end < funktion.end;
  }

  addNestedFunktion(funktion) {
    if (this.script != funktion.script) throw new Error("Incompatible script");
    this.nested.push(funktion);
  }

  print() {
    console.log(`function ${this.name}() range=${this.start}-${this.end}`,
        `script=${this.script ? this.script.id : 'X'}`);
  }
}




function functionEventProcessor(processor) {
  // {script file},{script id},{start position},{end position},
  // {time},{timestamp},{function name}
  return {
    parsers: [null, parseInt, parseInt, parseInt, parseInt, parseInt, null],
    processor: processor
  };
}


function ParseProcessor() {
  LogReader.call(this, {
    'parse-full': functionEventProcessor(this.processFull),
    'parse-function': functionEventProcessor(this.processFunction),
    'parse-script': functionEventProcessor(this.processScript),
    'parse-eval': functionEventProcessor(this.processEval),
    'preparse-no-resolution':
        functionEventProcessor(this.processPreparseNoResolution),
    'preparse-resolution':
        functionEventProcessor(this.processPreparseResolution),
    'first-execution': functionEventProcessor(this.processFirstExecution)
  });
  this.idToScript = new Map();
  this.fileToScript = new Map();
  this.nameToFunction = new Map();
  this.rangeToFunction = [];
  this.danglingFunktions = [];
}
inherits(ParseProcessor, LogReader);

ParseProcessor.prototype.print= function() {
  console.log("scripts:");
  this.idToScript.forEach(script => script.print());
}
/**
 * @override
 */
ParseProcessor.prototype.printError = function(str) {
  print(str);
};

ParseProcessor.prototype.processString = function(string) {
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

ParseProcessor.prototype.processLogFile = function(fileName) {
  this.collectEntries = true
  this.lastLogFileName_ = fileName;
  var line;
  while (line = readline()) {
    this.processLogLine(line);
  }
  this.idToScript.forEach(script => script.inferFunktionParents());
  print();
};

ParseProcessor.prototype.addEntry = function(entry) {
  this.entries.push(entry);
}

ParseProcessor.prototype.lookupScript = function(file, id) {
  // During preparsing we only have the temporary ranges and no script yet.
  if (file.length == 0 && id == 0) {
    return undefined;
  }
  if (id <= 0) throw new Error("Invalid script_id: " + id);
  let script;
  if (this.idToScript.has(id)) {
    script = this.idToScript.get(id);
  } else {
    script = new Script(file, id);
    this.idToScript.set(id, script);
  }
  if (file.length > 0 && script.file === undefined) {
    script.file = file;
    this.fileToScript.set(file, script);
  }
  return script;
}

// Used to deal with dangling functions.
ParseProcessor.prototype.lookupFunktionsByRange = function(start, end, adder) {
  let endToFunction = this.rangeToFunction[start];
  if (endToFunction !== undefined) {
    let funktions = endToFunction[end];
    if (funktions !== undefined) return funktions;
  } else {
    this.rangeToFunction[start] = endToFunction = [];
  }
  let funktion = adder();
  return endToFunction[end] = [funktion];
}

ParseProcessor.prototype.newFunktion = function(file, scriptId, 
    startPosition, endPosition, time, timestamp, functionName) {
  let script = this.lookupScript(file, scriptId);
  let fn = new Funktion(functionName, startPosition, endPosition, script,
                        timestamp);
  if (script === undefined) {
    if (this.danglingFunktions.length > 0) {
      let lastFn = this.danglingFunktions[this.danglingFunktions.length-1];
      if (startPosition < lastFn.start && endPosition < lastFn.end) {
        console.log("===============", startPosition, endPosition);
        lastFn.print();
        throw new Error("Dangling function ranges are invalid");
      }
    }
    this.danglingFunktions.push(fn);
  }
  return fn;
}

ParseProcessor.prototype.lookupFunktion = function(scriptId, startPosition) {
  let script = this.idToScript.get(scriptId);
  return script.funktionAtPosition(startPosition);
}

ParseProcessor.prototype.addScriptAndFunction = function(file, scriptId, 
    startPosition, endPosition, time, timestamp, functionName) {
  // All preparse events have no script attached and thus we can only detect
  // functions via range or name.
  let funktions = this.lookupFunktionsByRange(startPosition, endPosition,
      ()=> this.newFunktion(...arguments));
}

ParseProcessor.prototype.processFull = function(file, scriptId, startPosition,
      endPosition, time, timestamp, functionName) {
  this.addScriptAndFunction(...arguments);
}

ParseProcessor.prototype.processEval = function(file, scriptId, startPosition,
      endPosition, time, timestamp, functionName) {
  //this.addScriptAndFunction(...arguments);
}

ParseProcessor.prototype.processFunction = function(file, scriptId, startPosition,
      endPosition, time, timestamp, functionName) {
  this.addScriptAndFunction(...arguments);
  this.updateDanglingFunctions(file, scriptId);
}

ParseProcessor.prototype.processScript = function(file, scriptId, startPosition,
      endPosition, time, timestamp, functionName) {
  // TODO timestamp and time
  this.updateDanglingFunctions(file, scriptId);
}

ParseProcessor.prototype.updateDanglingFunctions = function(file, scriptId) {
  let script = this.lookupScript(file, scriptId);
  // console.log("updated " + this.danglingFunktions.length + " functions");
  script.addMissingFunktions(this.danglingFunktions);
  this.danglingFunktions = [];
  this.rangeToFunktion = [];
}

ParseProcessor.prototype.processPreparseResolution = function(file, scriptId,
      startPosition, endPosition, time, timestamp, functionName) {
  // console.log('preparse resolution');
  this.addScriptAndFunction(...arguments);
}

ParseProcessor.prototype.processPreparseNoResolution = function(file, scriptId,
      startPosition, endPosition, time, timestamp, functionName) {
  // console.log('preparse no resolution');
  this.addScriptAndFunction(...arguments);
}

ParseProcessor.prototype.processFirstExecution = function(file, scriptId,
      startPosition, endPosition, time, timestamp, functionName) {
   console.log('first execution ' + timestamp);
  if (startPosition == 0) {
    let script = this.idToScript.get(scriptId);
    // undefined = eval fn execution
    if (script) {
      script.executionTimestamp = timestamp;
    }
  } else {
    let funktion = this.lookupFunktion(scriptId, startPosition);
    if (funktion) {
      funktion.executionTimestamp = timestamp;
      console.log((timestamp - funktion.parseTimestamp)/1000);
    }
  }
}


function ArgumentsProcessor(args) {
  this.args_ = args;
  this.result_ = ArgumentsProcessor.DEFAULTS;

  this.argsDispatch_ = { };
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
    print('  ' + padRight(synonyms.join(', '), 20) + " " + dispatch[2]);
  }
  quit(2);
};
