// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/instrumentation/v8-provider.h"

#include <type_traits>

#if defined(V8_TARGET_OS_WIN)
#include "src/instrumentation/v8-provider-win.h"
#else
#include "src/instrumentation/v8-provider-default.h"
#endif

namespace v8 {
namespace internal {
namespace instrumentation {

// Create the global "tracing::v8Provider" that is the instance of the provider
static_assert(std::is_trivial<V8Provider>::value, "V8Provider is not trivial");
V8Provider v8Provider{};

}  // namespace instrumentation
}  // namespace internal
}  // namespace v8
