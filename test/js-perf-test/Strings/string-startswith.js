// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function createSuite(name, count, fn) {
  new BenchmarkSuite(name, [count], [new Benchmark(name, true, false, 0, fn)]);
}

const inputs = [
  'I\xF1t\xEBrn\xE2ti\xF4n\xE0liz\xE6ti\xF8n\u2603\uD83D\uDCA9\uFFFD',
  'Lorem ipsum dolor sit amet, consectetur adipiscing elit.',
  'Integer eu augue suscipit, accumsan ipsum nec, sagittis sem.',
  'In vitae pellentesque dolor. Curabitur leo nunc, luctus vitae',
  'risus eget, fermentum hendrerit justo.',
];
const firsts = ['I', 'Integer', 'Lorem', 'risus', 'hello'];

function simpleHelper() {
  let sum = 0;
  for (let i = 0; i < inputs.length; i++) {
    for (let j = 0; j < firsts.length; j++) {
      sum += inputs[i].startsWith(firsts[j]);
    }
  }
  return sum;
}

function consInputHelper() {
  let sum = 0;
  for (let i = 0; i < inputs.length; i++) {
    for (let j = 0; j < inputs.length; j++) {
      for (let k = 0; k < firsts.length; k++) {
        sum += %ConstructConsString(inputs[i], inputs[j]).startsWith(firsts[k]);
      }
    }
  }
  return sum;
}

function consFirstHelper() {
  let sum = 0;
  for (let i = 0; i < inputs.length; i++) {
    for (let j = 0; j < firsts.length; j++) {
      for (let k = 0; k < firsts.length; k++) {
        sum += inputs[i].startsWith(%ConstructConsString(firsts[j], firsts[k]));
      }
    }
  }
  return sum;
}

function doubleConsHelper() {
  let sum = 0;
  for (let i = 0; i < inputs.length; i++) {
    for (let j = 0; j < inputs.length; j++) {
      for (let k = 0; k < firsts.length; k++) {
        for (let l = 0; l < firsts.length; l++) {
          sum += %ConstructConsString(inputs[i], inputs[j]).startsWith(
            %ConstructConsString(firsts[k], firsts[l])
          );
        }
      }
    }
  }
}

createSuite('direct strings and direct search', 1000, simpleHelper);
createSuite('cons strings and direct search', 1000, consInputHelper);
createSuite('direct strings and cons search', 1000, consFirstHelper);
createSuite('cons strings and cons search', 1000, doubleConsHelper);
