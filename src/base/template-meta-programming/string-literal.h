// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_TEMPLATE_META_PROGRAMMING_STRING_LITERAL_H_
#define V8_BASE_TEMPLATE_META_PROGRAMMING_STRING_LITERAL_H_

#include <algorithm>

#include "src/base/logging.h"

namespace v8::base::tmp {

// This class provides a way to pass compile time string literals to templates.
template <size_t N>
class StringLiteral {
 public:
  constexpr StringLiteral(const char (&s)[N]) {  // NOLINT(runtime/explicit)
    // We assume '\0' terminated strings.
    DCHECK_EQ(s[N - 1], '\0');
    std::copy(s, s + N, data_);
  }

  size_t size() const {
    DCHECK_EQ(data_[N - 1], '\0');
    // `size` does not include the terminating '\0'.
    return N - 1;
  }

  const char* c_str() const { return data_; }

  // `data_` cannot be private to satisify requirements of a structural type.
  char data_[N];
};

// Deduction guide for `StringLiteral`.
template <size_t N>
StringLiteral(const char (&)[N]) -> StringLiteral<N>;

}  // namespace v8::base::tmp

#endif  // V8_BASE_TEMPLATE_META_PROGRAMMING_STRING_LITERAL_H_
