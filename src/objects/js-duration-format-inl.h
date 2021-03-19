// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_DURATION_FORMAT_INL_H_
#define V8_OBJECTS_JS_DURATION_FORMAT_INL_H_

#include "src/objects/js-duration-format.h"
#include "src/objects/objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-duration-format-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSDurationFormat)

ACCESSORS(JSDurationFormat, icu_number_formatter,
          Managed<icu::number::LocalizedNumberFormatter>,
          kIcuNumberFormatterOffset)

inline void JSDurationFormat::set_largest_unit(Field largest_unit) {
  DCHECK_GE(LargestUnitBits::kMax, largest_unit);
  int hints = flags();
  hints = LargestUnitBits::update(hints, largest_unit);
  set_flags(hints);
}

inline JSDurationFormat::Field JSDurationFormat::largest_unit() const {
  return LargestUnitBits::decode(flags());
}

inline void JSDurationFormat::set_smallest_unit(Field smallest_unit) {
  DCHECK_GE(SmallestUnitBits::kMax, smallest_unit);
  int hints = flags();
  hints = SmallestUnitBits::update(hints, smallest_unit);
  set_flags(hints);
}

inline JSDurationFormat::Field JSDurationFormat::smallest_unit() const {
  return SmallestUnitBits::decode(flags());
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_DURATION_FORMAT_INL_H_
