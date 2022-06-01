// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-rab-gsab --allow-natives-syntax

'use strict';

d8.file.execute('test/mjsunit/typedarray-helpers.js');

(function ArrayConcat() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);

    const taWrite = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i + 1);
    }

    // Orig. array: [1, 2, 3, 4]
    //              [1, 2, 3, 4] << fixedLength
    //                    [3, 4] << fixedLengthWithOffset
    //              [1, 2, 3, 4, ...] << lengthTracking
    //                    [3, 4, ...] << lengthTrackingWithOffset

    function helper(receiver, ...params) {
      return ToNumbers(Array.prototype.concat.call(receiver, ...params));
    }

    // TypedArrays aren't concat spreadable.
    assertEquals([fixedLength, 5, 6, 7], helper(fixedLength, [5, 6], [7]));

    // OOBness doesn't matter since the TA is added as a single item.
    rab.resize(0);
    assertEquals([fixedLength, 5, 6, 7], helper(fixedLength, [5, 6], [7]));

    // The same for detached buffers.
    %ArrayBufferDetach(rab);
    assertEquals([fixedLength, 5, 6, 7], helper(fixedLength, [5, 6], [7]));
  }
})();

// Array.prototype.includes doesn't convert the search value to a Number, so
// there are no opportunities for resizing the buffer during parameter
// conversion of the search value.
(function ArrayIncludesNoEvilObjecParameter() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);

    let evil = { valueOf: () => {
      rab.resize(2 * ctor.BYTES_PER_ELEMENT);
      return 0;
    }};
    assertFalse(IncludesHelper(fixedLength, evil));
    // The buffer didn't get resized.
    assertEquals(4 * ctor.BYTES_PER_ELEMENT, rab.byteLength);
  }
})();

// FIXME: Converting the index is done though and can resize!
