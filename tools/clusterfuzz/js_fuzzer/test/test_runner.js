// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Test runner implementation.
 */

'use strict';

const assert = require('assert');
const path = require('path');
const sinon = require('sinon');

const helpers = require('./helpers.js');
const runner = require('../runner.js');

const sandbox = sinon.createSandbox();


describe('Load tests', () => {
  afterEach(() => {
    sandbox.restore();
  });

  it('from test archive', () => {
    sandbox.stub(Math, 'random').callsFake(() => 0.5);
    const archivePath = path.join(helpers.BASE_DIR, 'input_archive');
    const testRunner = new runner.AllTestsRunner(archivePath, 'v8', 2, 2);
    var arr = Array.from(testRunner.enumerateInputs());
    assert.equal(2, arr.length);
    assert.deepEqual([0, 1], arr.map((x) => x[0]));
    assert.deepEqual(
        ["v8/test/mjsunit/v8_test.js", "v8/test/mjsunit/v8_test.js"],
        arr[0][1].map((x) => x.relPath));
    assert.deepEqual(
        ["spidermonkey/spidermonkey_test.js", "v8/test/mjsunit/v8_test.js"],
        arr[1][1].map((x) => x.relPath));
  });

  it('from test corpus', () => {
    sandbox.stub(Math, 'random').callsFake(() => 0.5);
    const archivePath = path.join(helpers.BASE_DIR, 'input_archive');
    const testRunner = new runner.CorpusRunner(archivePath, 'chakra', true);
    var arr = Array.from(testRunner.enumerateInputs());
    assert.equal(2, arr.length);
    assert.deepEqual([0, 1], arr.map((x) => x[0]));
    assert.deepEqual(
        ["chakra/chakra_test2.js"],
        arr[0][1].map((x) => x.relPath));
    assert.deepEqual(
        ["chakra/chakra_test1.js"],
        arr[1][1].map((x) => x.relPath));
  });
});
