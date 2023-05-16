// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/witness.h"

#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace base {

namespace {

class MyResource {
 public:
  class MyWitness final : public Witness<MyResource> {
   public:
#ifdef DEBUG
    bool IsValidAndStillReservedFor(const MyResource* r) const {
      return IsValidFor(r) && resource()->IsReserved();
    }
#endif
   private:
    explicit MyWitness(const MyResource* r) : Witness(r) {}
    friend class MyResource;
  };

  MyWitness Reserve() {
    DCHECK(!is_reserved_);
    is_reserved_ = true;
    return MyWitness(this);
  }

  void Release() {
    DCHECK(is_reserved_);
    is_reserved_ = false;
  }

  bool IsReserved() const { return is_reserved_; }

  void Use(const MyWitness& reserved) {
    DCHECK(reserved.IsValidAndStillReservedFor(this));
  }

 private:
  bool is_reserved_ = false;
};

}  // namespace

TEST(WitnessTest, NoMemoryOverhead) {
#ifndef DEBUG
  EXPECT_TRUE(std::is_empty_v<MyResource::MyWitness>);
#endif
}

TEST(WitnessTest, ReserveUseRelease) {
  MyResource r;
  EXPECT_FALSE(r.IsReserved());
  auto witness = r.Reserve();
  EXPECT_TRUE(r.IsReserved());
  r.Use(witness);
  EXPECT_TRUE(r.IsReserved());
  r.Release();
  EXPECT_FALSE(r.IsReserved());
}

TEST(WitnessTest, UseAfterRelease) {
  MyResource r;
  auto witness = r.Reserve();
  r.Release();
#ifdef DEBUG
  EXPECT_DEATH_IF_SUPPORTED(r.Use(witness),
                            "Debug check failed:.*IsValidAndStillReservedFor");
#endif
}

TEST(WitnessTest, CopyWitness) {
  MyResource r;
  auto witness = r.Reserve();
  r.Use(witness);
  MyResource::MyWitness copy = witness;
  // We can use both the copy and the original.
  r.Use(copy);
  r.Use(witness);
}

TEST(WitnessTest, MoveWitness) {
  MyResource r;
  auto witness = r.Reserve();
  r.Use(witness);
  MyResource::MyWitness moved = std::move(witness);
  // We can use both the moved primary and the original (not primary anymore).
  r.Use(moved);
  r.Use(witness);
}

TEST(WitnessTest, UseInvalidCopy) {
  MyResource r;
  auto make_invalid_copy = [](MyResource* r) {
    auto witness = r->Reserve();
    r->Use(witness);
    MyResource::MyWitness copy = witness;
    r->Use(copy);
    r->Use(witness);
    // We're returning a copy that is not primary; the primary dies!
    return copy;
  };
  auto invalid_copy = make_invalid_copy(&r);
#ifdef DEBUG
  EXPECT_DEATH_IF_SUPPORTED(r.Use(invalid_copy),
                            "Debug check failed:.*IsValidAndStillReservedFor");
#endif
}

TEST(WitnessTest, UseValidMoved) {
  MyResource r;
  auto make_valid_moved = [](MyResource* r) {
    auto witness = r->Reserve();
    r->Use(witness);
    MyResource::MyWitness moved = std::move(witness);
    r->Use(moved);
    r->Use(witness);
    // We're returning the primary witness.
    return moved;
  };
  auto valid_moved = make_valid_moved(&r);
  r.Use(valid_moved);
}

TEST(WitnessTest, UseInvalidCopyAfterMove) {
  MyResource r;
  auto make_invalid_copy = [](MyResource* r) {
    auto witness = r->Reserve();
    r->Use(witness);
    MyResource::MyWitness moved = std::move(witness);
    r->Use(moved);
    r->Use(witness);
    // We're returning the original which is not a primary anymore.
    // The primary moved witness dies!
    return witness;
  };
  auto invalid_copy = make_invalid_copy(&r);
#ifdef DEBUG
  EXPECT_DEATH_IF_SUPPORTED(r.Use(invalid_copy),
                            "Debug check failed:.*IsValidAndStillReservedFor");
#endif
}

}  // namespace base
}  // namespace v8
