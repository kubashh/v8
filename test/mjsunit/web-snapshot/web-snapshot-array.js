// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-d8-web-snapshot-api --allow-natives-syntax

'use strict';

d8.file.execute('test/mjsunit/web-snapshot/web-snapshot-helpers.js');

(function TestArray() {
  function createObjects() {
    globalThis.foo = {
      array: [5, 6, 7]
    };
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals([5, 6, 7], foo.array);
})();

(function TestHoleySmiElementsArray() {
  function createObjects() {
    globalThis.foo = [1,,2];
  }
  const {foo} = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals([1,,2], foo);
})();

(function TestHoleyElementsArray() {
  function createObjects() {
    globalThis.foo = [1,,"123"];
  }
  const {foo} = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals([1,,"123"], foo);
})();

(function TestPackedDoubleElementsArray() {
  function createObjects() {
    globalThis.foo = [1.2, 2.3];
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals([1.2, 2.3], foo);
})();

(function TestArrayContainingDoubleAndSmi() {
  function createObjects() {
    globalThis.foo = [1.2, 1];
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals([1.2, 1], foo);
})();

(function TestHoleyArrayContainingDoubleAndSmi() {
  function createObjects() {
    globalThis.foo = [1.2, , 1];
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals([1.2, , 1], foo);
})();

(function TestArrayContainingDoubleAndObject() {
  function createObjects() {
    globalThis.foo = [1.2, {'key': 'value'}];
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals([1.2, {'key': 'value'}], foo);
})();

(function TestHoleyArrayContainingDoubleAndObject() {
  function createObjects() {
    globalThis.foo = [1.2, , {'key': 'value'}];
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals([1.2, , {'key': 'value'}], foo);
})();

(function TestHoleyDoubleElementsArray() {
  function createObjects() {
    globalThis.foo = [1.2, , 2.3];
  }
  const {foo} = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals([1.2, , 2.3], foo);
})();

(function TestEmptyArray() {
  function createObjects() {
    globalThis.foo = {
      array: []
    };
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals(0, foo.array.length);
  assertEquals([], foo.array);
})();

(function TestArrayContainingArray() {
  function createObjects() {
    globalThis.foo = {
      array: [[2, 3], [4, 5]]
    };
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals([[2, 3], [4, 5]], foo.array);
})();

(function TestArrayContainingObject() {
  function createObjects() {
    globalThis.foo = {
      array: [{ a: 1 }, { b: 2 }]
    };
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals(1, foo.array[0].a);
  assertEquals(2, foo.array[1].b);
})();

(function TestArrayContainingFunction() {
  function createObjects() {
    globalThis.foo = {
      array: [function () { return 5; }]
    };
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals(5, foo.array[0]());
})();

(function TestInPlaceStringsInArray() {
  function createObjects() {
    globalThis.foo = {
      array: ['foo', 'bar', 'baz']
    };
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  // We cannot test that the strings are really in-place; that's covered by
  // cctests.
  assertEquals('foobarbaz', foo.array.join(''));
})();

(function TestRepeatedInPlaceStringsInArray() {
  function createObjects() {
    globalThis.foo = {
      array: ['foo', 'bar', 'foo']
    };
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  // We cannot test that the strings are really in-place; that's covered by
  // cctests.
  assertEquals('foobarfoo', foo.array.join(''));
})();

(function TestDictionaryElementsArray() {
  function createObjects() {
    const array = [];
    // Add a large index to force dictionary elements.
    array[2 ** 30] = 10;
    for (let i = 0; i < 10; i++) {
      array[i * 101] = i;
    }
    globalThis.foo = array;
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertTrue(%HasDictionaryElements(foo));
  assertEquals(2 ** 30 + 1, foo.length);
  for (let i = 0; i < 10; i++) {
    assertEquals(i,foo[i * 101]);
  }
})();

(function TestDictionaryElementsArrayContainingArray() {
  function createObjects() {
    const array = [];
    // Add a large index to force dictionary elements.
    array[2 ** 30] = 10;
    for (let i = 0; i < 10; i++) {
      array[i * 101] = [i];
    }
    globalThis.foo = array;
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertTrue(%HasDictionaryElements(foo));
  assertEquals(2 ** 30 + 1, foo.length);
  for (let i = 0; i < 10; i++) {
    assertEquals([i],foo[i * 101]);
  }
})();

(function TestDictionaryElementsArrayContainingObject() {
  function createObjects() {
    const array = [];
    // Add a large index to force dictionary elements.
    array[2 ** 30] = 10;
    for (let i = 0; i < 10; i++) {
      array[i * 101] = {i:i};
    }
    globalThis.foo = array;
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertTrue(%HasDictionaryElements(foo));
  assertEquals(2 ** 30 + 1, foo.length);
  for (let i = 0; i < 10; i++) {
    assertEquals({i:i}, foo[i * 101]);
  }
})();

(function TestDictionaryElementsArrayContainingFunction() {
  function createObjects() {
    const array = [];
    // Add a large index to force dictionary elements.
    array[2 ** 30] = 10;
    for (let i = 0; i < 10; i++) {
      array[i * 101] = function() { return i; };
    }
    globalThis.foo = array;
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertTrue(%HasDictionaryElements(foo));
  assertEquals(2 ** 30 + 1, foo.length);
  for (let i = 0; i < 10; i++) {
    assertEquals(i, foo[i * 101]());
  }
})();

(function TestDictionaryElementsArrayContainingString() {
  function createObjects() {
    const array = [];
    // Add a large index to force dictionary elements.
    array[2 ** 30] = 10;
    for (let i = 0; i < 10; i++) {
      array[i * 101] = `${i}`;
    }
    globalThis.foo = array;
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertTrue(%HasDictionaryElements(foo));
  assertEquals(2 ** 30 + 1, foo.length);
  for (let i = 0; i < 10; i++) {
    assertEquals(`${i}`, foo[i * 101]);
  }
})();
