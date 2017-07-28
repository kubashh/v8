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
    console.log(fileName + ': ' + (e.message || e));
    throw e;
  }
}

// ===========================================================================

numberFormat = new Intl.NumberFormat('de-CH', {
  maximumFractionDigits: 2,
  minimumFractionDigits: 2,
});

function KB(bytes, total) {
  let kb = bytes / 1024;
  let result = numberFormat.format(kb).padStart(10) + "KB";
  if (total !== undefined && total != 0) {
    result += (Math.round(bytes / total * 100) + "%").padStart(5);
  }
  return result;
}

// ===========================================================================
class Script {
  constructor(file, id) {
    this.file = file;
    this.id = id;
    this.funktions = [];
    this.metrics = new Map();
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
    this.funktions.forEach(fn => {
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

  printMetrics() {
    console.log("SCRIPT: " + this.id);
    let totalFunktions = this.funktions.filter(each => true);

    if (totalFunktions.length == 0) return;

    let nofFunktions = totalFunktions.length;
    let toplevel = totalFunktions.filter(each => each.isToplevel());
    let preparsed = totalFunktions.filter(each => each.preparseTime > 0);
    let parsed = totalFunktions.filter(each => each.parseTime > 0);
    let parsed2 = totalFunktions.filter(each => each.parse2Time > 0);
    let resolved = totalFunktions.filter(each => each.reslutionTime > 0);
    let executed = this.funktions.filter(each => each.executionTimestamp > 0);

    let ownBytesSum = list => {
      return list.reduce((bytes, each) => bytes + each.getOwnBytes(), 0)
    };
    let bytesTotal = toplevel.reduce(
      (bytes, each) => bytes + each.getBytes(), 0);

    let info = (name, funktions) => {
      let ownBytes = ownBytesSum(funktions);
      let nofPercent = Math.round(funktions.length / nofFunktions * 100);
      let value = (funktions.length + "").padStart(6) +
        (nofPercent + "%").padStart(5) +
        KB(ownBytes, bytesTotal).padStart(10);
      console.log(("  - " + name).padEnd(20) + value);
      this.metrics.set(name + "-bytes", ownBytes);
      this.metrics.set(name + "-count", funktions.length);
      this.metrics.set(name + "-count-percent", nofPercent);
      this.metrics.set(name + "-bytes-percent",
        Math.round(ownBytes / bytesTotal * 100));
    };

    console.log("  - file:         " + this.file);
    info('total', totalFunktions)
    info("functions", totalFunktions);
    info("toplevel", toplevel);
    info("preparsed", preparsed);
    info("fully parsed", parsed);
    info("fn parsed", parsed2);
    info("resolved", resolved);
    info("executed", executed);


    let metric = totalFunktions.reduce((sum, each) => sum + each.metric(), 0);
    let negativeMetric = totalFunktions.reduce(
      (sum, each) => sum + each.negativeMetric(), 0);
    console.log("  - bytes-parse-exec-time: positive=" +
      numberFormat.format(metric) + " negative=" +
      numberFormat.format(negativeMetric));

    this.metrics.set('parseMetric', metric);
    this.metrics.set('parseMetricNegative', metric);
  }
}



// ===========================================================================
class Funktion {
  constructor(name, start, end, script) {
    if (start < 0) throw new Error("invalid start position: " + start);
    if (end <= 0) throw new Error("invalid end position: " + end);
    this.name = name;
    this.start = start;
    this.end = end;
    this.script = script;
    this.parent = undefined;
    this.nested = [];

    this.preparseTimestamp = -1;
    this.parseTimestamp = -1;
    this.parse2Timestamp = -1;
    this.resolutionTimestamp = -1;
    this.executionTimestamp = -1;

    this.preparseTime = 0;
    this.parseTime = 0;
    this.parse2Time;
    this.resolutionTime = 0;

    this.scopeResolutionTime = 0;
    if (script) this.script.addFunktion(this);
  }

  isNestedIn(funktion) {
    if (this.script != funktion.script) throw new Error("Incompatible script");
    return funktion.start < this.start && this.end < funktion.end;
  }

  isToplevel() {
    return this.parent === undefined
  }

  hasBeenExecuted() {
    return this.executionTimestamp > 0
  }

  addNestedFunktion(funktion) {
    if (this.script != funktion.script) throw new Error("Incompatible script");
    if (funktion == undefined) throw new Error("Nesting non funktion");
    this.nested.push(funktion);
  }

  getBytes() {
    return this.end - this.start;
  }

  getOwnBytes() {
    return this.nested.reduce(
      (bytes, each) => bytes - each.getBytes(),
      this.getBytes());
  }

  metric() {
    let delta = (this.executionTimestamp - this.parseTimestamp) / 1000;
    if (delta <= 0) return 0;
    // unit: kB ms
    return this.getOwnBytes() * delta / 1024;
  }

  negativeMetric() {
    if (this.hasBeenExecuted()) return 0;
    return this.getOwnBytes() / 1024;
  }

  print() {
    console.log(`function ${this.name}() range=${this.start}-${this.end}`,
      `script=${this.script ? this.script.id : 'X'}`);
  }
}




// ===========================================================================
function functionEventProcessor(processor) {
  // {script file},{script id},{start position},{end position},
  // {time},{timestamp},{function name}
  return {
    parsers: [null, parseInt, parseInt, parseInt, parseFloat, parseInt, null],
    processor: processor
  };
}


function ParseProcessor() {
  LogReader.call(this, {
    'parse-full': functionEventProcessor(this.processFull),
    'parse-function': functionEventProcessor(this.processFunction),
    'parse-script': functionEventProcessor(this.processScript),
    'parse-eval': functionEventProcessor(this.processEval),
    'preparse-no-resolution': functionEventProcessor(this.processPreparseNoResolution),
    'preparse-resolution': functionEventProcessor(this.processPreparseResolution),
    'first-execution': functionEventProcessor(this.processFirstExecution)
  });
  this.idToScript = new Map();
  this.fileToScript = new Map();
  this.nameToFunction = new Map();
  this.rangeToFunction = [];
  this.danglingFunktions = [];
}
inherits(ParseProcessor, LogReader);

ParseProcessor.prototype.print = function() {
  console.log("scripts:");
  this.idToScript.forEach(script => script.print());
}
/**
 * @override
 */
ParseProcessor.prototype.printError = function(str) {
  console.log(str);
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
  this.postProcess();
}

ParseProcessor.prototype.processLogFile = function(fileName) {
  this.collectEntries = true
  this.lastLogFileName_ = fileName;
  var line;
  while (line = readline()) {
    this.processLogLine(line);
  }
  this.postProcess();
}

ParseProcessor.prototype.postProcess = function() {
  this.idToScript.forEach(script => script.inferFunktionParents());
  this.idToScript.forEach(script => script.printMetrics());
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

ParseProcessor.prototype.newFunktion = function(file, scriptId,
  startPosition, endPosition, time, timestamp, functionName) {
  let script = this.lookupScript(file, scriptId);
  let fn = new Funktion(functionName, startPosition, endPosition, script);
  if (script === undefined) {
    if (this.danglingFunktions.length > 0) {
      let lastFn = this.danglingFunktions[this.danglingFunktions.length - 1];
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
  if (script === undefined) return script;
  return script.funktionAtPosition(startPosition);
}

ParseProcessor.prototype.addScriptAndFunction = function(file, scriptId,
  startPosition, endPosition, time, timestamp, functionName) {
  // All preparse events have no script attached and thus we can only detect
  // functions via range or name.
  let endToFunction = this.rangeToFunction[startPosition];
  if (endToFunction !== undefined) {
    let funktion = endToFunction[endPosition];
    if (funktion !== undefined) return funktion;
  } else {
    this.rangeToFunction[startPosition] = endToFunction = [];
  }
  let funktion = this.newFunktion(...arguments);
  return endToFunction[endPosition] = funktion;
}

ParseProcessor.prototype.updateDanglingFunctions = function(file, scriptId) {
  let script = this.lookupScript(file, scriptId);
  // console.log("updated " + this.danglingFunktions.length + " functions");
  script.addMissingFunktions(this.danglingFunktions);
  this.danglingFunktions = [];
  this.rangeToFunktion = [];
}


ParseProcessor.prototype.processFull = function(file, scriptId, startPosition,
  endPosition, time, timestamp, functionName) {
  let funktion = this.addScriptAndFunction(...arguments);
  funktion.parseTimestamp = timestamp;
  funktion.parseTime = time;
}

ParseProcessor.prototype.processEval = function(file, scriptId, startPosition,
  endPosition, time, timestamp, functionName) {
  //this.addScriptAndFunction(...arguments);
}

ParseProcessor.prototype.processFunction = function(file, scriptId, startPosition,
  endPosition, time, timestamp, functionName) {
  let funktion = this.addScriptAndFunction(...arguments);
  funktion.parse2Timestamp = timestamp;
  funktion.parse2Time = time;
  this.updateDanglingFunctions(file, scriptId);
}

ParseProcessor.prototype.processScript = function(file, scriptId, startPosition,
  endPosition, time, timestamp, functionName) {
  // TODO timestamp and time
  this.updateDanglingFunctions(file, scriptId);
}

ParseProcessor.prototype.processPreparseResolution = function(file, scriptId,
  startPosition, endPosition, time, timestamp, functionName) {
  // console.log('preparse resolution');
  let funktion = this.addScriptAndFunction(...arguments);
  funktion.resolutionTimestamp = timestamp;
  funktion.resolutionTime = time;
}

ParseProcessor.prototype.processPreparseNoResolution = function(file, scriptId,
  startPosition, endPosition, time, timestamp, functionName) {
  // console.log('preparse no resolution');
  let funktion = this.addScriptAndFunction(...arguments);
  funktion.preparseTimestamp = timestamp;
  funktion.preparseTime = time;
}

ParseProcessor.prototype.processFirstExecution = function(file, scriptId,
  startPosition, endPosition, time, timestamp, functionName) {
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
    } else if (functionName.length > 0) {
      // throw new Error("Could not find function: " + functionName);
    }
  }
}


function ArgumentsProcessor(args) {
  this.args_ = args;
  this.result_ = ArgumentsProcessor.DEFAULTS;

  this.argsDispatch_ = {};
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

  console.log('Cmdline args: [options] [log-file-name]\n' +
    'Default log file name is "' +
    ArgumentsProcessor.DEFAULTS.logFileName + '".\n');
  console.log('Options:');
  for (var arg in this.argsDispatch_) {
    var synonyms = [arg];
    var dispatch = this.argsDispatch_[arg];
    for (var synArg in this.argsDispatch_) {
      if (arg !== synArg && dispatch === this.argsDispatch_[synArg]) {
        synonyms.push(synArg);
        delete this.argsDispatch_[synArg];
      }
    }
    console.log('  ' + padRight(synonyms.join(', '), 20) + " " + dispatch[2]);
  }
  quit(2);
};
