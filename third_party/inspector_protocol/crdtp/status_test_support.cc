// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "status_test_support.h"

namespace v8_crdtp {
void PrintTo(const Status& status, std::ostream* os) {
  *os << status.ToASCIIString() << " (error: 0x" << std::hex
      << static_cast<int>(status.error) << ", "
      << "pos: " << std::dec << status.pos << ")";
}

namespace {
MATCHER_P(StatusIsMatcher, status, "") {
  return arg.error == status.error && arg.pos == status.pos;
}
MATCHER(StatusIsOKMatcher, "is ok") {
  return arg.ok();
}
}  // namespace

testing::Matcher<Status> StatusIsOk() {
  return StatusIsOKMatcher();
}

testing::Matcher<Status> StatusIs(Error error, size_t pos) {
  return StatusIsMatcher(Status(error, pos));
}
}  // namespace v8_crdtp
