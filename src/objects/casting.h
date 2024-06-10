// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_CASTING_H_
#define V8_OBJECTS_CASTING_H_

#include "include/v8-source-location.h"
#include "src/base/logging.h"
#include "src/objects/tagged.h"

namespace v8::internal {

// CastTraits<T> is a type trait that defines type checking behaviour for
// tagged object casting. The expected specialization is:
//
//     template<>
//     struct CastTraits<SomeObject> {
//       template<typename From>
//       static bool AllowFrom(Tagged<From> value) {
//         return IsSomeObject(value);
//       }
//     };
//
// or, likely, just specializations of AllowFrom for Object and HeapObject,
// under the assumption that the HeapObject implementation is the same for all
// HeapObjects and the Object implementation has additional overhead in Smi
// checks.
//
//     struct CastTraits<Object> {
//       static bool AllowFrom(Tagged<HeapObject> value) {
//         return IsSomeObject(value);
//       }
//       static bool AllowFrom(Tagged<Object> value) {
//         return IsSomeObject(value);
//       }
//     };
//
template <typename To>
struct CastTraits;

// `Is<T>(value)` checks whether `value` is a tagged object of type `T`.
template <typename T, typename U>
inline bool Is(Tagged<U> value) {
  return CastTraits<T>::AllowFrom(value);
}

// Only initialise the SourceLocation in debug mode.
#ifdef DEBUG
#define INIT_SOURCE_LOCATION_IN_DEBUG v8::SourceLocation::Current()
#else
#define INIT_SOURCE_LOCATION_IN_DEBUG v8::SourceLocation()
#endif

// `Cast<T>(value)` casts `value` to a tagged object of type `T`, with a debug
// check that `value` is a tagged object of type `T`.
template <typename To, typename From>
inline Tagged<To> Cast(Tagged<From> value,
                       v8::SourceLocation loc = INIT_SOURCE_LOCATION_IN_DEBUG) {
  DCHECK_WITH_MSG_AND_LOC(Is<To>(value),
                          V8_PRETTY_FUNCTION_VALUE_OR("Cast type check"), loc);
  return UncheckedCast<To>(value);
}
template <typename To, typename From>
inline Handle<To> Cast(Handle<From> value,
                       v8::SourceLocation loc = INIT_SOURCE_LOCATION_IN_DEBUG);
template <typename To, typename From>
inline MaybeHandle<To> Cast(
    MaybeHandle<From> value,
    v8::SourceLocation loc = INIT_SOURCE_LOCATION_IN_DEBUG);
#if V8_ENABLE_DIRECT_HANDLE_BOOL
template <typename To, typename From>
inline DirectHandle<To> Cast(
    DirectHandle<From> value,
    v8::SourceLocation loc = INIT_SOURCE_LOCATION_IN_DEBUG);
template <typename To, typename From>
inline MaybeDirectHandle<To> Cast(
    MaybeDirectHandle<From> value,
    v8::SourceLocation loc = INIT_SOURCE_LOCATION_IN_DEBUG);
#endif

// `UncheckedCast<T>(value)` casts `value` to a tagged object of type `T`,
// without checking the type of value.
template <typename To, typename From>
inline Tagged<To> UncheckedCast(Tagged<From> value) {
  return Tagged<To>(value.ptr());
}

// `Is<T>(maybe_weak_value)` specialization for possible weak values and strong
// target `T`, that additionally first checks whether `maybe_weak_value` is
// actually a strong value (or a Smi, which can't be weak).
template <typename T, typename U>
inline bool Is(Tagged<MaybeWeak<U>> value) {
  // TODO(leszeks): Add Is which supports weak conversion targets.
  static_assert(!is_maybe_weak_v<T>);
  // Cast from maybe weak to strong needs to be strong or smi.
  return value.IsStrongOrSmi() && Is<T>(Tagged<U>(value.ptr()));
}

}  // namespace v8::internal

#undef PRETTY_FUNCTION_VALUE
#undef INIT_SOURCE_LOCATION_IN_DEBUG

#endif  // V8_OBJECTS_CASTING_H_
