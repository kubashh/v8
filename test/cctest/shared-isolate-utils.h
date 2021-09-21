// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-initialization.h"
#include "src/base/strings.h"
#include "src/heap/factory.h"
#include "src/heap/heap-inl.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"

#ifndef V8_CCTEST_SHARED_ISOLATE_UTILS_H_
#define V8_CCTEST_SHARED_ISOLATE_UTILS_H_

namespace v8 {
namespace internal {

class MultiClientIsolateTest {
 public:
  MultiClientIsolateTest();
  ~MultiClientIsolateTest();

  v8::Isolate* shared_isolate() const { return shared_isolate_; }

  Isolate* i_shared_isolate() const {
    return reinterpret_cast<Isolate*>(shared_isolate_);
  }

  const std::vector<v8::Isolate*>& client_isolates() const {
    return client_isolates_;
  }

  v8::Isolate* NewClientIsolate(v8::StartupData* custom_blob = nullptr);

 private:
  v8::Isolate* shared_isolate_;
  std::vector<v8::Isolate*> client_isolates_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CCTEST_SHARED_ISOLATE_UTILS_H_
