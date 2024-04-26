// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_ZONE_WITH_NAME_H_
#define V8_COMPILER_TURBOSHAFT_ZONE_WITH_NAME_H_

#include "src/base/template-meta-programming/string-literal.h"
#include "src/compiler/zone-stats.h"

namespace v8::internal::compiler::turboshaft {

template <typename T, base::tmp::StringLiteral Name>
class ZoneWithNamePointerImpl final {
 public:
  using pointer_type = T*;

  ZoneWithNamePointerImpl() = default;
  ZoneWithNamePointerImpl(std::nullptr_t)  // NOLINT(runtime/explicit)
      : ptr_(nullptr) {}
  explicit ZoneWithNamePointerImpl(pointer_type ptr) : ptr_(ptr) {}

  ZoneWithNamePointerImpl(const ZoneWithNamePointerImpl&) = default;
  ZoneWithNamePointerImpl(ZoneWithNamePointerImpl&&) noexcept = default;
  ZoneWithNamePointerImpl& operator=(const ZoneWithNamePointerImpl&) = default;
  ZoneWithNamePointerImpl& operator=(ZoneWithNamePointerImpl&&) = default;

  operator pointer_type() const { return get(); }  // NOLINT(runtime/explicit)
  T& operator*() const { return *get(); }
  pointer_type operator->() { return get(); }

  //  bool operator==(std::nullptr_t) const {
  //    return ptr_ == nullptr;
  //  }

 private:
  pointer_type get() const { return ptr_; }

  pointer_type ptr_ = pointer_type{};
};

#ifdef DEBUG
template <typename T, base::tmp::StringLiteral Name>
using ZoneWithNamePointer = ZoneWithNamePointerImpl<T, Name>;
#else
template <typename T, auto>
using ZoneWithNamePointer = T*;
#endif

template <base::tmp::StringLiteral Name>
class ZoneWithNameImpl final {
 public:
  ZoneWithNameImpl(ZoneStats* pool, const char* name,
                   bool support_zone_compression = false)
      : scope_(pool, name, support_zone_compression) {
    DCHECK_EQ(std::strcmp(name, Name.c_str()), 0);
  }

  ZoneWithNameImpl(Zone* non_owned_zone)  // NOLINT(runtime/explicit)
      : scope_(nullptr, Name.c_str()), non_owned_zone_(non_owned_zone) {}
    
  ZoneWithNameImpl(const ZoneWithNameImpl&) = delete;
  ZoneWithNameImpl(ZoneWithNameImpl&& other)
    : scope_(std::move(other.scope_)), non_owned_zone_(nullptr) {
      std::swap(non_owned_zone_, other.non_owned_zone_);
    }
  ZoneWithNameImpl& operator=(const ZoneWithNameImpl&) = delete;
  ZoneWithNameImpl& operator=(ZoneWithNameImpl&& other) {
    if(non_owned_zone_ == nullptr) {
      scope_.Destroy();
    }
    scope_ = std::move(other.scope_);
    non_owned_zone_ = other.non_owned_zone_;
    other.non_owned_zone_ = nullptr;
  }

  template <typename T, typename... Args>
  ZoneWithNamePointer<T, Name> New(Args&&... args) {
    return ZoneWithNamePointer<T, Name>{
        get()->template New<T>(std::forward<Args>(args)...)};
  }

  Zone* get() {
    if (non_owned_zone_) return non_owned_zone_;
    return scope_.zone();
  }
  operator Zone*() { return get(); }  // NOLINT(runtime/explicit)
  Zone* operator->() { return get(); }

  // TODO(nicohartmann@): Remove this. Don't use!
  void Destroy() {
    if (non_owned_zone_) return;
    scope_.Destroy();
  }

 private:
  // NOTE: `ZoneStats::Scope` actually allocates a new zone.
  ZoneStats::Scope scope_;
  // TODO(nicohartmann@): Remove this hack once we have proper ownership of
  // zones.
  Zone* non_owned_zone_ = nullptr;
};

// #if defined(DEBUG) && defined(__clang__)
template <base::tmp::StringLiteral Name>
using ZoneWithName = ZoneWithNameImpl<Name>;
// #else
// template<base::tmp::StringLiteral>
// using ZoneWithName = Zone;
// #endif

template <base::tmp::StringLiteral Name>
constexpr ZoneWithName<Name>* AttachDebugName(Zone* zone) {
  return reinterpret_cast<ZoneWithName<Name>*>(zone);
}

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_ZONE_WITH_NAME_H_
