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

std::vector<int64_t> RandomSample(RandomNumberGenerator* gen, int64_t max,
                                  size_t n) {
  DCHECK_GT(n, 0);
  DCHECK_LT(n, max);

  // Choose to select or exclude, whatever needs less generator calls.
  size_t smaller_part = std::min(max - n, n);

  std::unordered_set<int64_t> selected;
  while (selected.size() != smaller_part) {
    int64_t x = gen->NextInt64();
    // Shift negative values. Simple abs() won't work since there is no
    // equivalent positive value for LLONG_MIN.
    if (x < 0) {
      x -= LLONG_MIN;
    }
    selected.insert(x % max);
  }

  if (smaller_part == n) {
    return std::vector<int64_t>(selected.begin(), selected.end());
  } else {
    std::vector<int64_t> result;
    for (int64_t i = 0; i < max; i++) {
      if (!selected.count(i)) {
        result.push_back(i);
      }
    }
    return result;
  }
}

}  // namespace base
}  // namespace v8
