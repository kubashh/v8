// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_VLQ_BASE64_H_
#define V8_BASE_VLQ_BASE64_H_

#include <string>

#include "src/base/base-export.h"

namespace v8 {
namespace base {
V8_BASE_EXPORT int charToDigitDecode(unsigned char c);
V8_BASE_EXPORT int VLQBase64Decode(const std::string& s, size_t& pos);
}  // namespace base
}  // namespace v8
#endif  // V8_BASE_VLQ_BASE64_H_
