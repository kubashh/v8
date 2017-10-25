// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_RANDOM_UTILS_H_
#define V8_RANDOM_UTILS_H_

#include <vector>

#include "src/base/base-export.h"

namespace v8 {
namespace base {

class RandomNumberGenerator;

// Selects n numbers from range [0; max).
V8_BASE_EXPORT std::vector<int64_t> RandomSample(RandomNumberGenerator* gen,
                                                 int64_t max, size_t n);

}  // namespace base
}  // namespace v8

#endif  // V8_RANDOM_UTILS_H_
