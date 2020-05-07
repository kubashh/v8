// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_CUSTOM_SPACE_H_
#define INCLUDE_CPPGC_CUSTOM_SPACE_H_

#include <stddef.h>

namespace cppgc {

/**
 * Top-level base class for custom spaces. Users must inherit from CustomSpace
 * below.
 */
class CustomSpaceBase {
 public:
  virtual ~CustomSpaceBase() = default;
  virtual size_t GetCustomSpaceIndex() const = 0;
};

/**
 * Base class custom spaces should directly inherit from.
 */
template <typename ConcreteCustomSpace>
class CustomSpace : public CustomSpaceBase {
 public:
  size_t GetCustomSpaceIndex() const final {
    return ConcreteCustomSpace::kSpaceIndex;
  }
};

/**
 * User-overridable trait that allows pinning types to custom spaces.
 */
template <typename T, typename = void>
struct SpaceTrait {
  using Space = void;
};

}  // namespace cppgc

#endif  // INCLUDE_CPPGC_CUSTOM_SPACE_H_
