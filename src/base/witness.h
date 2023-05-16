// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_WITNESS_H_
#define V8_BASE_WITNESS_H_

#include <memory>

#include "src/base/compiler-specific.h"
#include "src/base/macros.h"

namespace v8::base {

/**
 * This is a base class template for creating objects that witness that some
 * resource is in some desired state. Witness objects are empty and should incur
 * no time or memory overhead, except in DEBUG builds.
 *
 * Example usage; Say we want to witness that objects of type MyResource have
 * been properly reserved when they are used. We will use class MyWitness for
 * this purpose.
 *
 *   class MyResource {
 *    public:
 *     MyWitness Reserve();
 *     void Release();
 *     bool IsReserved() const { return is_reserved_; }
 *     void Use(const MyWitness& reserved);
 *
 *    private:
 *     bool is_reserved_ = false;
 *   };
 *
 * Define MyWitness as a subclass of Witness<MyResource> and implement witness
 * validation appropriately.
 *
 *   class MyWitness final : public Witness<MyResource> {
 *    public:
 * #ifdef DEBUG
 *     bool IsValidAndStillReservedFor(const MyResource* r) const {
 *       return IsValidFor(r) && resource()->IsReserved();
 *     }
 * #endif
 *
 *    private:
 *     explicit MyWitness(const MyResource* r) : Witness(r) {}
 *     friend class MyResource;
 *   };
 *
 * Implement how resources are reserved and released.
 *
 *   MyWitness MyResource::reserve() {
 *     DCHECK(!is_reserved_);
 *     is_reserved_ = true;
 *     return MyWitness(this);
 *   }
 *
 *   void Release() {
 *     DCHECK(is_reserved_);
 *     is_reserved_ = false;
 *   }
 *
 * When using the resource, a witness is provided and its validity can be
 * checked.
 *
 *   void MyResource::Use(const MyWitness& reserved) {
 *     DCHECK(reserved.IsValidAndStillReservedFor(this));
 *     ...
 *   }
 *
 * Only privileged methods (MyResource::Reserve, in this example) should be able
 * to construct such objects. Witness objects constructed in this way are
 * "primary". Witness objects can be copied an moved freely but the copies are
 * When a "primary" witness object is destroyed, all copies are immediately
 * invalidated.
 *
 * This class template is not thread-safe.
 */
template <typename Resource>
class Witness {
 public:
#ifdef DEBUG
  Witness(const Witness& w) V8_NOEXCEPT : resource_(w.resource_),
                                          primary_(false) {}
  Witness(Witness&& w) V8_NOEXCEPT
      : resource_(w.resource_),
        primary_(std::exchange(w.primary_, false)) {}

  ~Witness() {
    if (primary_) {
      DCHECK(resource_);
      *resource_ = nullptr;
    }
  }
#else   // !DEBUG
  Witness(const Witness& w) V8_NOEXCEPT = default;
  Witness(Witness&& w) V8_NOEXCEPT = default;
  ~Witness() = default;
#endif  // DEBUG

  Witness& operator=(const Witness&) = delete;
  Witness& operator=(Witness&&) = delete;

#ifdef DEBUG
  bool IsValid() const { return resource_ && *resource_ != nullptr; }
  bool IsValidFor(const Resource* r) const {
    return resource_ && *resource_ == r;
  }
#endif

 protected:
#ifdef DEBUG
  explicit Witness(const Resource* r)
      : resource_(new(const Resource*)(r)), primary_(true) {}
  const Resource* resource() const {
    DCHECK(resource_);
    return *resource_;
  }
#else
  explicit Witness(const Resource* t) {}
#endif

 private:
#ifdef DEBUG
  std::shared_ptr<const Resource*> resource_;
  bool primary_;
#endif
};

// Witness objects should be empty on non DEBUG builds.
#ifndef DEBUG
static_assert(sizeof(Witness<int>) == 0);
#endif

}  // namespace v8::base

#endif  // V8_BASE_WITNESS_H_
