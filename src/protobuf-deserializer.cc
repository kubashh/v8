// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8.h"

namespace v8 {
// namespace internal {

// static
Local<Object> ProtobufDeserializer::Deserialize(v8::Isolate* isolate,
                                                const uint8_t*, size_t length) {
  return Object::New(isolate);
}

// }  // namespace internal
}  // namespace v8
