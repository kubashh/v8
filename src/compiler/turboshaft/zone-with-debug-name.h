// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_ZONE_WITH_NAME_H_
#define V8_COMPILER_TURBOSHAFT_ZONE_WITH_NAME_H_

#include "src/base/template-meta-programming/string-literal.h"
#include "src/zone/zone.h"

namespace v8::internal::compiler::turboshaft {

template<typename T, base::tmp::StringLiteral Name>
class ZoneWithNamePointerImpl final {
 public:
  using pointer_type = T*;

  pointer_type get() const { return ptr_; }
  operator pointer_type() const { return get(); } // NOLINT(runtime/explicit)

 private:
  pointer_type ptr_;
};

template<base::tmp::StringLiteral Name>
class ZoneWithNameImpl final : public Zone {
 public:

};

#if defined(DEBUG) && defined(__clang__)
template<base::tmp::StringLiteral Name>
using ZoneWithName = ZoneWithNameImpl<Name>;
#else
template<base::tmp::StringLiteral>
using ZoneWithName = Zone;
#endif

template<base::tmp::StringLiteral Name>
constexpr ZoneWithName<Name>* AttachDebugName(Zone* zone) {
  return reinterpret_cast<ZoneWithName<Name>*>(zone);
}

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_ZONE_WITH_NAME_H_
