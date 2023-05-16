// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_WITNESS_H_
#define V8_BASE_WITNESS_H_

#include <memory>

#include "src/base/compiler-specific.h"
#include "src/base/macros.h"

namespace v8::base {

template <typename Resource>
class Witness {
 public:
#ifdef DEBUG
  Witness(const Witness& w) V8_NOEXCEPT : resource_(w.resource_),
                                          primary_(false) {}
  Witness(Witness&& w) V8_NOEXCEPT
      : resource_(std::move(w.resource_)),
        primary_(std::exchange(w.primary_, false)) {}

  ~Witness() {
    if (primary_) *resource_ = nullptr;
  }
#else
  Witness(const Witness& w) V8_NOEXCEPT = default;
  Witness(Witness&& w) V8_NOEXCEPT = default;
  ~Witness() = default;
#endif

  Witness& operator=(const Witness&) = delete;
  Witness& operator=(Witness&&) = delete;

#ifdef DEBUG
  bool IsValid() const { return *resource_ != nullptr; }
#endif

 protected:
#ifdef DEBUG
  explicit Witness(const Resource* t)
      : resource_(new(const Resource*)(t)), primary_(true) {}
  const Resource* resource() const { return *resource_; }
#else
  explicit Witness(const Resource* t) {}
#endif

 private:
#ifdef DEBUG
  std::shared_ptr<const Resource*> resource_;
  bool primary_;
#endif
};

}  // namespace v8::base

#endif  // V8_BASE_WITNESS_H_
