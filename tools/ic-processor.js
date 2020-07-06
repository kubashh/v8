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

/**
 * Parser for dynamic code optimization state.
 */
function parseState(s) {
  switch (s) {
  case "": return Profile.CodeState.COMPILED;
  case "~": return Profile.CodeState.OPTIMIZABLE;
  case "*": return Profile.CodeState.OPTIMIZED;
  }
  throw new Error("unknown code state: " + s);
}


class IcProcessor extends LogReader {
  constructor() {
    super();
    // TODO(zc): Needs modification after addition of the IC event with time
    let propertyICParser = [
      parseInt, parseInt, parseInt, parseInt, parseString, parseString,
      parseInt, parseString, parseString, parseString
    ];
    LogReader.call(this, {
      'code-creation': {
        parsers: [
          parseString, parseInt, parseInt, parseInt, parseInt, parseString,
          parseVarArgs
        ],
        processor: this.processCodeCreation
      },
      'code-move':
          {parsers: [parseInt, parseInt], processor: this.processCodeMove},
      'code-delete': {parsers: [parseInt], processor: this.processCodeDelete},
      'sfi-move':
          {parsers: [parseInt, parseInt], processor: this.processFunctionMove},
      'LoadGlobalIC': {
        parsers: propertyICParser,
        processor: this.processPropertyIC.bind(this, 'LoadGlobalIC')
      },
      'StoreGlobalIC': {
        parsers: propertyICParser,
        processor: this.processPropertyIC.bind(this, 'StoreGlobalIC')
      },
      'LoadIC': {
        parsers: propertyICParser,
        processor: this.processPropertyIC.bind(this, 'LoadIC')
      },
      'StoreIC': {
        parsers: propertyICParser,
        processor: this.processPropertyIC.bind(this, 'StoreIC')
      },
      'KeyedLoadIC': {
        parsers: propertyICParser,
        processor: this.processPropertyIC.bind(this, 'KeyedLoadIC')
      },
      'KeyedStoreIC': {
        parsers: propertyICParser,
        processor: this.processPropertyIC.bind(this, 'KeyedStoreIC')
      },
      'StoreInArrayLiteralIC': {
        parsers: propertyICParser,
        processor: this.processPropertyIC.bind(this, 'StoreInArrayLiteralIC')
      },
    });
    this.profile_ = new Profile();

    this.LoadGlobalIC = 0;
    this.StoreGlobalIC = 0;
    this.LoadIC = 0;
    this.StoreIC = 0;
    this.KeyedLoadIC = 0;
    this.KeyedStoreIC = 0;
    this.StoreInArrayLiteralIC = 0;
  }
  /**
   * @override
   */
  printError(str) {
    print(str);
  }
  processString(string) {
    let end = string.length;
    let current = 0;
    let next = 0;
    let line;
    let i = 0;
    let entry;
    while (current < end) {
      next = string.indexOf('\n', current);
      if (next === -1) break;
      i++;
      line = string.substring(current, next);
      current = next + 1;
      this.processLogLine(line);
    }
  }
  processLogFile(fileName) {
    this.collectEntries = true;
    this.lastLogFileName_ = fileName;
    let line;
    while (line = readline()) {
      this.processLogLine(line);
    }
    print();
    print('=====================');
    print('LoadGlobal: ' + this.LoadGlobalIC);
    print('StoreGlobal: ' + this.StoreGlobalIC);
    print('Load: ' + this.LoadIC);
    print('Store: ' + this.StoreIC);
    print('KeyedLoad: ' + this.KeyedLoadIC);
    print('KeyedStore: ' + this.KeyedStoreIC);
    print('StoreInArrayLiteral: ' + this.StoreInArrayLiteralIC);
  }
  addEntry(entry) {
    this.entries.push(entry);
  }
  processCodeCreation(type, kind, timestamp, start, size, name, maybe_func) {
    if (maybe_func.length) {
      let funcAddr = parseInt(maybe_func[0]);
      let state = parseState(maybe_func[1]);
      this.profile_.addFuncCode(
          type, name, timestamp, start, size, funcAddr, state);
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
  formatName(entry) {
    if (!entry) return '<unknown>';
    let name = entry.func.getName();
    let re = /(.*):[0-9]+:[0-9]+$/;
    let array = re.exec(name);
    if (!array) return name;
    return entry.getState() + array[1];
  }

  processPropertyIC(
      type, pc, time, line, column, old_state, new_state, map, name, modifier,
      slow_reason) {
    this[type]++;
    let entry = this.profile_.findEntry(pc);
    print(
        type + ' (' + old_state + '->' + new_state + modifier + ') at ' +
        this.formatName(entry) + ':' + line + ':' + column + ' ' + name +
        ' (map 0x' + map.toString(16) + ')' +
        (slow_reason ? ' ' + slow_reason : '') + 'time: ' + time);
  }
}

class ArgumentsProcessor extends BaseArgumentsProcessor {
  getArgsDispatch() {
    return {
      '--range': ['range', 'auto,auto',
          'Specify the range limit as [start],[end]'],
      '--source-map': ['sourceMap', null,
          'Specify the source map that should be used for output']
    };
  }
  getDefaultResults() {
   return {
      logFileName: 'v8.log',
      range: 'auto,auto',
    };
  }
}
