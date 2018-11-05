// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var fs = require('fs'),
    path = require('path'),
    readline = require('readline'),
    process = require('process');

function main() {
  if (process.argv.length === 2)  {
    throw new Error('File name required as first arg.');
  }

  var fileName = process.argv[2],
      fullPath = path.join(process.cwd(), fileName);

  const rl = readline.createInterface({
    input: fs.createReadStream(fullPath),
    crlfDelay: Infinity
  });

  rl.on('line', readLine);
  rl.on('close', onClose);
}

var method_count = new Map();

function readLine(line) {
  if (line.startsWith('caller,')) {
    line = line.split('\\n');
    method = line[0].split(',')[1];
    var count = method_count.get(method) || 0;
    method_count.set(method, ++count);
  }
}

function onClose() {
  for (const [method, count] of method_count) {
    console.log(`${method} is called ${count} times.`);
  }
}

main();
