// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const MB = 1024 * 1024,
      block_size = 32 * MB;
let array = Array(block_size).fill(1.1);
array.prop = 1;
let args = Array(2048 * MB / block_size - 2).fill(array);
args.push(Array(block_size));
array.concat.apply(array, args);
