// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Runner class.
 */

'use strict';

const path = require('path');

const corpus = require('./corpus.js');
const random = require('./random.js');

// Maximum number of test inputs to use for one fuzz test.
const MAX_TEST_INPUTS_PER_TEST = 10;

function getRandomInputs(primaryCorpus, secondaryCorpora, count) {
  count = random.randInt(2, count);

  // Choose 40%-80% of inputs from primary corpus.
  const primaryCount = Math.floor(random.uniform(0.4, 0.8) * count);
  count -= primaryCount;

  let inputs = primaryCorpus.getRandomTestcases(primaryCount);

  // Split remainder equally between the secondary corpora.
  const secondaryCount = Math.floor(count / secondaryCorpora.length);

  for (let i = 0; i < secondaryCorpora.length; i++) {
    let currentCount = secondaryCount;
    if (i == secondaryCorpora.length - 1) {
      // Last one takes the remainder.
      currentCount = count;
    }

    count -= currentCount;
    if (currentCount) {
      inputs = inputs.concat(
          secondaryCorpora[i].getRandomTestcases(currentCount));
    }
  }

  return random.shuffle(inputs);
}

class Runner {
  
}

class AllTestsRunner extends Runner {
  constructor(inputDir, primary, numFiles,
              maxTestInputs=MAX_TEST_INPUTS_PER_TEST) {
    super();
    this.primary = primary;
    this.numFiles = numFiles;
    this.maxTestInputs = maxTestInputs;
    this.corpi = {
      'v8': new corpus.Corpus(inputDir, 'v8'),
      'chakra': new corpus.Corpus(inputDir, 'chakra'),
      'spidermonkey': new corpus.Corpus(inputDir, 'spidermonkey'),
      'jsc': new corpus.Corpus(inputDir, 'WebKit/JSTests'),
      'crash': new corpus.Corpus(inputDir, 'CrashTests'),
    };
  }

  *randomInputGen() {
    const primary = this.corpi[this.primary];
    const secondary = Object.values(this.corpi);

    for (let i = 0; i < this.numFiles; i++) {
      const inputs = getRandomInputs(
          primary,
          random.shuffle(secondary),
          this.maxTestInputs);

      if (inputs.length > 0) {
        yield inputs;
      }
    }
  }
}

function* enumerate(iterable) {
  let i = 0;
  for (const value of iterable) {
    yield [i, value];
    i++;
  }
}

module.exports = {
  AllTestsRunner: AllTestsRunner,
  enumerate: enumerate,
};
