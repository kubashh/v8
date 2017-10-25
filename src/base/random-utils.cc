// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/random-utils.h"

#include <algorithm>
#include <climits>
#include <unordered_set>
#include <vector>

#include "src/base/utils/random-number-generator.h"

namespace v8 {
namespace base {

std::vector<size_t> RandomSample(RandomNumberGenerator* gen, size_t max,
                                 size_t n) {
  DCHECK_GT(n, 0);
  DCHECK_LT(n, max);

  // Choose to select or exclude, whatever needs less generator calls.
  size_t smaller_part = std::min(max - n, n);

  std::unordered_set<size_t> selected;
  while (selected.size() != smaller_part) {
    int64_t x = gen->NextInt64();
    // Shift negative values. Simple abs() won't work since there is no
    // equivalent positive value for LLONG_MIN.
    if (x < 0) {
      x -= LLONG_MIN;
    }
    selected.insert(static_cast<size_t>(x % max));
  }

  if (smaller_part == n) {
    return std::vector<size_t>(selected.begin(), selected.end());
  } else {
    std::vector<size_t> result;
    for (size_t i = 0; i < max; i++) {
      if (!selected.count(i)) {
        result.push_back(i);
      }
    }
    return result;
  }
}

}  // namespace base
}  // namespace v8
