// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TAGGED_VALUE_INL_H_
#define V8_OBJECTS_TAGGED_VALUE_INL_H_

#include "src/objects/tagged-value.h"

#include "include/v8-internal.h"
#include "src/common/ptr-compr-inl.h"
#include "src/objects/maybe-object.h"
#include "src/objects/objects.h"
#include "src/objects/oddball.h"
#include "src/objects/tagged-impl-inl.h"
#include "src/roots/roots-inl.h"

namespace v8 {
namespace internal {

inline StrongTaggedValue::StrongTaggedValue(Object o)
    :
#if defined(V8_COMPRESS_POINTERS_IN_ISOLATE_CAGE) || \
    defined(V8_COMPRESS_POINTERS_IN_SHARED_CAGE)
      TaggedImpl(CompressTagged(o.ptr()))
#else
      TaggedImpl(o.ptr())
#endif
{
}

Object StrongTaggedValue::ToObject(Isolate* isolate, StrongTaggedValue object) {
#if defined(V8_COMPRESS_POINTERS_IN_ISOLATE_CAGE) || \
    defined(V8_COMPRESS_POINTERS_IN_SHARED_CAGE)
  return Object(DecompressTaggedAny(isolate, object.ptr()));
#else
  return Object(object.ptr());
#endif
}

inline TaggedValue::TaggedValue(MaybeObject o)
    :
#if defined(V8_COMPRESS_POINTERS_IN_ISOLATE_CAGE) || \
    defined(V8_COMPRESS_POINTERS_IN_SHARED_CAGE)
      TaggedImpl(CompressTagged(o.ptr()))
#else
      TaggedImpl(o.ptr())
#endif
{
}

MaybeObject TaggedValue::ToMaybeObject(Isolate* isolate, TaggedValue object) {
#if defined(V8_COMPRESS_POINTERS_IN_ISOLATE_CAGE) || \
    defined(V8_COMPRESS_POINTERS_IN_SHARED_CAGE)
  return MaybeObject(DecompressTaggedAny(isolate, object.ptr()));
#else
  return MaybeObject(object.ptr());
#endif
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_TAGGED_VALUE_INL_H_
