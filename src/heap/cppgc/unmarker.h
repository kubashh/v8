// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_UNMARKER_H_
#define V8_HEAP_CPPGC_UNMARKER_H_

#include <cstdint>
#include <memory>

#include "include/cppgc/platform.h"
#include "src/base/macros.h"

namespace cppgc {
namespace internal {

class HeapBase;

// Unmarks heap before major collections.
class V8_EXPORT_PRIVATE Unmarker final {
 public:
  enum class Config : uint8_t {
    kAtomic,
    kConcurrent,
  };

  Unmarker(HeapBase*, Platform*);
  ~Unmarker();

  void Start(Config);
  void Finish();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_UNMARKER_H_
