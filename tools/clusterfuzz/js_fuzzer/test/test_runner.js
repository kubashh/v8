// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Test input file loading.
 */

'use strict';

const assert = require('assert');
const path = require('path');
const sinon = require('sinon');

const helpers = require('./helpers.js');
const runner = require('../runner.js');

const sandbox = sinon.createSandbox();


describe('Execute runner', () => {
  afterEach(() => {
    sandbox.restore();
  });

  it('with test archive', () => {
    sandbox.stub(Math, 'random').callsFake(() => 0.5);
    const archivePath = path.join(helpers.BASE_DIR, 'input_archive');
    const testRunner = new runner.AllTestsRunner(archivePath, 'v8', 2, 2);
    var arr = Array.from(testRunner.randomInputGen());
    assert.equal(2, arr.length);
  });
});
