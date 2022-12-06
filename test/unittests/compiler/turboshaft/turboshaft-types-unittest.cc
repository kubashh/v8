// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/types.h"
#include "src/handles/handles.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8::internal::compiler::turboshaft {

class TurboshaftTypesTest : public TestWithNativeContextAndZone {
 public:
  using Kind = Type::Kind;
  CanonicalHandleScope canonical;

  TurboshaftTypesTest()
      : TestWithNativeContextAndZone(), canonical(isolate()) {}

  void CheckIs(const Type& t, Type::Kind kind) const {
    CHECK_EQ(t.kind(), kind);
    CHECK_EQ(kind == Kind::kNone, t.IsNone());
    CHECK_EQ(kind == Kind::kInvalid, t.IsInvalid());
    CHECK_EQ(kind == Kind::kWord32, t.IsWord32());
    CHECK_EQ(kind == Kind::kWord64, t.IsWord64());
    CHECK_EQ(kind == Kind::kFloat32, t.IsFloat32());
    CHECK_EQ(kind == Kind::kFloat64, t.IsFloat64());
  }
};

TEST_F(TurboshaftTypesTest, None) {
  Type t = Type::None();
  CheckIs(t, Kind::kNone);
}

#if 0
TEST_F(TurboshaftTypesTest, Word32) {
  // Complete range
  {
    Type t = Word32Type::Any();
    CheckIs(t, Kind::kWord32);
    const auto& w32 = t.AsWord32();
    CHECK_EQ(0, w32.range_from());
    CHECK_EQ(std::numeric_limits<uint32_t>::max(), w32.range_to());
    CHECK(w32.is_any());
    CHECK(!w32.is_constant());
    CHECK(!w32.is_wrapping());
  }

  // Non-wrapping range
  {
    Type t = Word32Type::Range(100, 400, zone());
    CheckIs(t, Kind::kWord32);
    const auto& w32 = t.AsWord32();
    CHECK_EQ(100, w32.range_from());
    CHECK_EQ(400, w32.range_to());
    CHECK(!w32.is_any());
    CHECK(!w32.is_constant());
    CHECK(!w32.is_wrapping());
  }

  // Wrapping range
  {
    Type t = Word32Type::Range(static_cast<uint32_t>(-1), 1, zone());
    CheckIs(t, Kind::kWord32);
    const auto& w32 = t.AsWord32();
    CHECK_EQ(static_cast<uint32_t>(-1), w32.range_from());
    CHECK_EQ(1, w32.range_to());
    CHECK(!w32.is_any());
    CHECK(!w32.is_constant());
    CHECK(w32.is_wrapping());
  }

  // Constant
  {
    Type t = Word32Type::Constant(42);
    CheckIs(t, Kind::kWord32);
    const auto& w32 = t.AsWord32();
    CHECK_EQ(42, w32.range_from());
    CHECK_EQ(42, w32.range_to());
    CHECK(!w32.is_any());
    CHECK(w32.is_constant());
    CHECK(!w32.is_wrapping());
  }
}

TEST_F(TurboshaftTypesTest, Word64) {
  // Complete range
  {
    Type t = Word64Type::Any();
    CheckIs(t, Kind::kWord64);
    const auto& w64 = t.AsWord64();
    CHECK_EQ(0, w64.range_from());
    CHECK_EQ(std::numeric_limits<uint64_t>::max(), w64.range_to());
    CHECK(w64.is_any());
    CHECK(!w64.is_constant());
    CHECK(!w64.is_wrapping());
  }

  // Non-wrapping range
  {
    Type t = Word64Type::Range(100ULL * (2ULL << 32ULL),
                               400ULL * (2ULL << 36ULL), zone());
    CheckIs(t, Kind::kWord64);
    const auto& w64 = t.AsWord64();
    CHECK_EQ(100ULL * (2ULL << 32ULL), w64.range_from());
    CHECK_EQ(400ULL * (2ULL << 36ULL), w64.range_to());
    CHECK(!w64.is_any());
    CHECK(!w64.is_constant());
    CHECK(!w64.is_wrapping());
  }

  // Wrapping range
  {
    Type t = Word64Type::Range(static_cast<uint64_t>(-1), 1, zone());
    CheckIs(t, Kind::kWord64);
    const auto& w64 = t.AsWord64();
    CHECK_EQ(static_cast<uint64_t>(-1), w64.range_from());
    CHECK_EQ(1, w64.range_to());
    CHECK(!w64.is_any());
    CHECK(!w64.is_constant());
    CHECK(w64.is_wrapping());
  }

  // Constant
  {
    Type t = Word64Type::Constant(42ULL * (2ULL << 50ULL));
    CheckIs(t, Kind::kWord64);
    const auto& w64 = t.AsWord64();
    CHECK_EQ(42ULL * (2ULL << 50ULL), w64.range_from());
    CHECK_EQ(42ULL * (2ULL << 50ULL), w64.range_to());
    CHECK(!w64.is_any());
    CHECK(w64.is_constant());
    CHECK(!w64.is_wrapping());
  }
}

TEST_F(TurboshaftTypesTest, Float32) {
  // Complete range (including NaN)
  {
    Type t = Type::Float32();
    CheckIs(t, Kind::kFloat32);
    const auto& f32 = t.AsFloat32();
    CHECK(f32.has_range());
    CHECK_EQ(-std::numeric_limits<float>::infinity(), f32.range_min());
    CHECK_EQ(std::numeric_limits<float>::infinity(), f32.range_max());
    CHECK(f32.is_any());
    CHECK(!f32.is_constant());
    CHECK(f32.has_nan());
  }

  // Complete range (excluding NaN)
  {
    Type t = Type::Float32(false);
    CheckIs(t, Kind::kFloat32);
    const auto& f32 = t.AsFloat32();
    CHECK(f32.has_range());
    CHECK_EQ(-std::numeric_limits<float>::infinity(), f32.range_min());
    CHECK_EQ(std::numeric_limits<float>::infinity(), f32.range_max());
    CHECK(f32.is_any());
    CHECK(!f32.is_constant());
    CHECK(!f32.has_nan());
  }

  // Range (including NaN)
  {
    Type t = Type::Float32(-3.14159f, 3.14159f, true);
    CheckIs(t, Kind::kFloat32);
    const auto& f32 = t.AsFloat32();
    CHECK(f32.has_range());
    CHECK_EQ(-3.14159f, f32.range_min());
    CHECK_EQ(3.14159f, f32.range_max());
    CHECK(!f32.is_any());
    CHECK(!f32.is_constant());
    CHECK(f32.has_nan());
  }

  // Range (excluding NaN)
  {
    Type t = Type::Float32(-3.14159f, 3.14159f);
    CheckIs(t, Kind::kFloat32);
    const auto& f32 = t.AsFloat32();
    CHECK(f32.has_range());
    CHECK_EQ(-3.14159f, f32.range_min());
    CHECK_EQ(3.14159f, f32.range_max());
    CHECK(!f32.is_any());
    CHECK(!f32.is_constant());
    CHECK(!f32.has_nan());
  }

  // Constant (including NaN)
  {
    Type t = Type::Float32(42.42f, true);
    CheckIs(t, Kind::kFloat32);
    const auto& f32 = t.AsFloat32();
    CHECK(f32.has_range());
    CHECK_EQ(42.42f, f32.range_min());
    CHECK_EQ(42.42f, f32.range_max());
    CHECK(!f32.is_any());
    CHECK(f32.is_constant());
    CHECK(f32.has_nan());
  }

  // Range (excluding NaN)
  {
    Type t = Type::Float32(42.42f);
    CheckIs(t, Kind::kFloat32);
    const auto& f32 = t.AsFloat32();
    CHECK(f32.has_range());
    CHECK_EQ(42.42f, f32.range_min());
    CHECK_EQ(42.42f, f32.range_max());
    CHECK(!f32.is_any());
    CHECK(f32.is_constant());
    CHECK(!f32.has_nan());
  }

  // NaN
  {
    Type t = Type::Float32NaN();
    CheckIs(t, Kind::kFloat32);
    const auto& f32 = t.AsFloat32();
    CHECK(!f32.has_range());
    CHECK(!f32.is_any());
    CHECK(!f32.is_constant());
    CHECK(f32.has_nan());
  }
}

TEST_F(TurboshaftTypesTest, Float64) {
  // Complete range (including NaN)
  {
    Type t = Type::Float64();
    CheckIs(t, Kind::kFloat64);
    const auto& f64 = t.AsFloat64();
    CHECK(f64.has_range());
    CHECK_EQ(-std::numeric_limits<double>::infinity(), f64.range_min());
    CHECK_EQ(std::numeric_limits<double>::infinity(), f64.range_max());
    CHECK(f64.is_any());
    CHECK(!f64.is_constant());
    CHECK(f64.has_nan());
  }

  // Complete range (excluding NaN)
  {
    Type t = Type::Float64(false);
    CheckIs(t, Kind::kFloat64);
    const auto& f64 = t.AsFloat64();
    CHECK(f64.has_range());
    CHECK_EQ(-std::numeric_limits<double>::infinity(), f64.range_min());
    CHECK_EQ(std::numeric_limits<double>::infinity(), f64.range_max());
    CHECK(f64.is_any());
    CHECK(!f64.is_constant());
    CHECK(!f64.has_nan());
  }

  // Range (including NaN)
  {
    Type t = Type::Float64(-3.14159f, 3.14159f, true);
    CheckIs(t, Kind::kFloat64);
    const auto& f64 = t.AsFloat64();
    CHECK(f64.has_range());
    CHECK_EQ(-3.14159f, f64.range_min());
    CHECK_EQ(3.14159f, f64.range_max());
    CHECK(!f64.is_any());
    CHECK(!f64.is_constant());
    CHECK(f64.has_nan());
  }

  // Range (excluding NaN)
  {
    Type t = Type::Float64(-3.14159f, 3.14159f);
    CheckIs(t, Kind::kFloat64);
    const auto& f64 = t.AsFloat64();
    CHECK(f64.has_range());
    CHECK_EQ(-3.14159f, f64.range_min());
    CHECK_EQ(3.14159f, f64.range_max());
    CHECK(!f64.is_any());
    CHECK(!f64.is_constant());
    CHECK(!f64.has_nan());
  }

  // Constant (including NaN)
  {
    Type t = Type::Float64(42.42f, true);
    CheckIs(t, Kind::kFloat64);
    const auto& f64 = t.AsFloat64();
    CHECK(f64.has_range());
    CHECK_EQ(42.42f, f64.range_min());
    CHECK_EQ(42.42f, f64.range_max());
    CHECK(!f64.is_any());
    CHECK(f64.is_constant());
    CHECK(f64.has_nan());
  }

  // Range (excluding NaN)
  {
    Type t = Type::Float64(42.42f);
    CheckIs(t, Kind::kFloat64);
    const auto& f64 = t.AsFloat64();
    CHECK(f64.has_range());
    CHECK_EQ(42.42f, f64.range_min());
    CHECK_EQ(42.42f, f64.range_max());
    CHECK(!f64.is_any());
    CHECK(f64.is_constant());
    CHECK(!f64.has_nan());
  }

  // NaN
  {
    Type t = Type::Float64NaN();
    CheckIs(t, Kind::kFloat64);
    const auto& f64 = t.AsFloat64();
    CHECK(!f64.has_range());
    CHECK(!f64.is_any());
    CHECK(!f64.is_constant());
    CHECK(f64.has_nan());
  }
}

TEST_F(TurboshaftTypesTest, Equals) {
  Type none = Type::None();
  Type any = Type::Any();
  Type w32 = Word32Type::Any();
  Type w32c = Word32Type::Constant(1084);
  Type w32r = Word32Type::Range(1000, 0, zone());
  Type w64 = Word64Type::Any();
  Type w64c = Word64Type::Constant(1084);
  Type w64r = Word64Type::Range(1000, 0, zone());
  Type f32 = Float32Type::Any(0);
  Type f32n = Float32Type::Any();
  Type f32c = Float32Type::Constant(0.133f);
  Type f32cn = Float32Type::Set({0.133f}, Float32Type::kNaN, zone());
  Type f32r = Float32Type::Range(-13.99f, 18.02f, zone());
  Type f32rn = Float32Type::Range(-13.99f, 18.02f, Float32Type::kNaN, zone());
  Type f64 = Float64Type::Any(0);
  Type f64n = Float64Type::Any(Float64Type::kNaN);
  Type f64c = Float64Type::Constant(0.133);
  Type f64cn = Float64Type::Set({0.133}, Float64Type::kNaN, zone());
  Type f64r = Float64Type::Range(-13.99, 18.02, zone());
  Type f64rn = Float64Type::Range(-13.99, 18.02, Float64Type::kNaN, zone());
  Type all_types[] = {
      none, any,   w32,  w32c,  w32r, w64,  w64c, w64r,  f32,  f32n,
      f32c, f32cn, f32r, f32rn, f64,  f64n, f64c, f64cn, f64r, f64rn,
  };

  for (size_t i = 0; i < std::size(all_types); ++i) {
    for (size_t j = 0; j < std::size(all_types); ++j) {
      const Type& t1 = all_types[i];
      const Type& t2 = all_types[j];
      if (i == j) {
        CHECK(t1.Equals(t2));
        CHECK(t2.Equals(t1));
      } else {
        CHECK(!t1.Equals(t2));
        CHECK(!t2.Equals(t1));
      }
    }
  }

  // Word32
  {
    CHECK(w32.Equals(Word32Type::Any()));
    CHECK(w32.Equals(Word32Type::Range(18, 17)));
    CHECK(!w32.Equals(Word32Type::Range(18, 16)));

    CHECK(w32c.Equals(Word32Type::Constant(1084)));
    CHECK(!w32c.Equals(Word32Type::Range(1084, 1085)));

    CHECK(w32r.Equals(Word32Type::Range(1000, 0)));
    CHECK(!w32r.Equals(Word32Type::Range(0, 1000)));
    CHECK(!w32r.Equals(Word32Type::Range(1000, 1)));
    CHECK(!w32r.Equals(Word32Type::Range(999, 0)));
  }

  // Word64
  {
    CHECK(w64.Equals(Word64Type::Any()));
    CHECK(w64.Equals(Word64Type::Range(18, 17)));
    CHECK(!w64.Equals(Word64Type::Range(18, 16)));

    CHECK(w64c.Equals(Word64Type::Constant(1084)));
    CHECK(!w64c.Equals(Word64Type::Range(1084, 1085)));
    CHECK(!w64c.Equals(Word64Type::Constant(1084ULL + (2ULL << 40ULL))));

    CHECK(w64r.Equals(Word64Type::Range(1000, 0)));
    CHECK(!w64r.Equals(Word64Type::Range(0, 1000)));
    CHECK(!w64r.Equals(Word64Type::Range(1000, 1)));
    CHECK(!w64r.Equals(Word64Type::Range(999, 0)));
  }

  // Float32
  {
    CHECK(f32.Equals(Type::Float32(false)));
    CHECK(!f32.Equals(Type::Float32(std::numeric_limits<float>::lowest(),
                                    std::numeric_limits<float>::max(), false)));

    CHECK(f32n.Equals(Type::Float32()));
    CHECK(!f32n.Equals(Type::Float32(std::numeric_limits<float>::lowest(),
                                     std::numeric_limits<float>::max())));

    CHECK(f32c.Equals(Type::Float32(0.133f)));
    CHECK(!f32c.Equals(Type::Float32(0.133f, 0.134f)));
    CHECK(!f32c.Equals(Type::Float32(-0.133f, 0.133f)));

    CHECK(f32cn.Equals(Type::Float32(0.133f, true)));
    CHECK(!f32cn.Equals(Type::Float32(0.133f, 0.134f, true)));
    CHECK(!f32cn.Equals(Type::Float32(-0.133f, 0.133f, true)));

    CHECK(f32r.Equals(Type::Float32(-13.99f, 18.02f)));
    CHECK(!f32r.Equals(Type::Float32(-13.99f, 18.03f)));
    CHECK(!f32r.Equals(Type::Float32(13.99f, 18.02f)));

    CHECK(f32rn.Equals(Type::Float32(-13.99f, 18.02f, true)));
    CHECK(!f32rn.Equals(Type::Float32(-13.99f, 18.03f, true)));
    CHECK(!f32rn.Equals(Type::Float32(13.99f, 18.02f, true)));
  }

  // Float64
  {
    CHECK(f64.Equals(Type::Float64(false)));
    CHECK(
        !f64.Equals(Type::Float64(std::numeric_limits<double>::lowest(),
                                  std::numeric_limits<double>::max(), false)));

    CHECK(f64n.Equals(Type::Float64()));
    CHECK(!f64n.Equals(Type::Float64(std::numeric_limits<double>::lowest(),
                                     std::numeric_limits<double>::max())));

    CHECK(f64c.Equals(Type::Float64(0.133)));
    CHECK(!f64c.Equals(Type::Float64(0.133, 0.134)));
    CHECK(!f64c.Equals(Type::Float64(-0.133, 0.133)));

    CHECK(f64cn.Equals(Type::Float64(0.133, true)));
    CHECK(!f64cn.Equals(Type::Float64(0.133, 0.134, true)));
    CHECK(!f64cn.Equals(Type::Float64(-0.133, 0.133, true)));

    CHECK(f64r.Equals(Type::Float64(-13.99, 18.02)));
    CHECK(!f64r.Equals(Type::Float64(-13.99, 18.03)));
    CHECK(!f64r.Equals(Type::Float64(13.99, 18.02)));

    CHECK(f64rn.Equals(Type::Float64(-13.99, 18.02, true)));
    CHECK(!f64rn.Equals(Type::Float64(-13.99, 18.03, true)));
    CHECK(!f64rn.Equals(Type::Float64(13.99, 18.02, true)));
  }
}

TEST_F(TurboshaftTypesTest, Word32LeastUpperBound) {
  auto CheckLubIs = [&](const Word32Type& lhs, const Word32Type& rhs,
                        const Word32Type& expected) {
    CHECK(Word32Type::LeastUpperBound(lhs, rhs, zone()).Equals(expected));
  };

  {
    const auto lhs = Word32Type::Range(100, 400, zone());
    CheckLubIs(lhs, lhs, lhs);
    CheckLubIs(lhs, Word32Type::Range(50, 350, zone()),
               Word32Type::Range(50, 400, zone()));
    CheckLubIs(lhs, Word32Type::Range(150, 600, zone()),
               Word32Type::Range(100, 600, zone()));
    CheckLubIs(lhs, Word32Type::Range(150, 350, zone()), lhs);
    CheckLubIs(lhs, Word32Type::Range(350, 0, zone()),
               Word32Type::Range(100, 0, zone()));
    CheckLubIs(lhs, Word32Type::Range(400, 100, zone()), Word32Type::Any());
    CheckLubIs(lhs, Word32Type::Range(600, 0, zone()),
               Word32Type::Range(600, 400, zone()));
    CheckLubIs(lhs, Word32Type::Range(300, 150, zone()), Word32Type::Any());
  }

  {
    const auto lhs = Word32Type::Constant(18);
    CheckLubIs(lhs, lhs, lhs);
    CheckLubIs(lhs, Word32Type::Constant(1119),
               Word32Type::Range(18, 1119, zone()));
    CheckLubIs(lhs, Word32Type::Constant(0), Word32Type::Range(0, 18, zone()));
    CheckLubIs(lhs, Word32Type::Range(40, 100, zone()),
               Word32Type::Range(18, 100, zone()));
    CheckLubIs(lhs, Word32Type::Range(4, 90, zone()),
               Word32Type::Range(4, 90, zone()));
    CheckLubIs(lhs, Word32Type::Range(0, 3, zone()),
               Word32Type::Range(0, 18, zone()));
    CheckLubIs(
        lhs, Word32Type::Constant(std::numeric_limits<uint32_t>::max()),
        Word32Type::Range(18, std::numeric_limits<uint32_t>::max(), zone()));
  }
}

TEST_F(TurboshaftTypesTest, Word64LeastUpperBound) {
  auto CheckLubIs = [&](const Word64Type& lhs, const Word64Type& rhs,
                        const Word64Type& expected) {
    CHECK(Word64Type::LeastUpperBound(lhs, rhs, zone()).Equals(expected));
  };

  {
    const auto lhs = Word64Type::Range(100, 400, zone());
    CheckLubIs(lhs, lhs, lhs);
    CheckLubIs(lhs, Word64Type::Range(50, 350, zone()),
               Word64Type::Range(50, 400, zone()));
    CheckLubIs(lhs, Word64Type::Range(150, 600, zone()),
               Word64Type::Range(100, 600, zone()));
    CheckLubIs(lhs, Word64Type::Range(150, 350, zone()), lhs);
    CheckLubIs(lhs, Word64Type::Range(350, 0, zone()),
               Word64Type::Range(100, 0, zone()));
    CheckLubIs(lhs, Word64Type::Range(400, 100, zone()), Word64Type::Any());
    CheckLubIs(lhs, Word64Type::Range(600, 0, zone()),
               Word64Type::Range(600, 400, zone()));
    CheckLubIs(lhs, Word64Type::Range(300, 150, zone()), Word64Type::Any());
  }

  {
    const auto lhs = Word64Type::Constant(18);
    CheckLubIs(lhs, lhs, lhs);
    CheckLubIs(lhs, Word64Type::Constant(1119),
               Word64Type::Set({18, 1119}, zone()));
    CheckLubIs(lhs, Word64Type::Constant(0), Word64Type::Set({0, 18}, zone()));
    CheckLubIs(lhs, Word64Type::Range(40, 100, zone()),
               Word64Type::Range(18, 100, zone()));
    CheckLubIs(lhs, Word64Type::Range(4, 90, zone()),
               Word64Type::Range(4, 90, zone()));
    CheckLubIs(lhs, Word64Type::Range(0, 3, zone()),
               Word64Type::Set({0, 1, 2, 3, 18}, zone()));
    CheckLubIs(
        lhs, Word64Type::Constant(std::numeric_limits<uint64_t>::max()),
        Word64Type::Set({18, std::numeric_limits<uint64_t>::max()}, zone()));
  }
}

TEST_F(TurboshaftTypesTest, Float32LeastUpperBound) {
  auto CheckLubIs = [&](const Float32Type& lhs, const Float32Type& rhs,
                        const Float32Type& expected) {
    CHECK(Float32Type::LeastUpperBound(lhs, rhs, zone()).Equals(expected));
  };

  {
    const auto lhs = Type::Float32(-32.19f, 94.07f);
    CheckLubIs(lhs, lhs, lhs);
    CheckLubIs(lhs, Type::Float32(-32.19f, 94.07f, true),
               Type::Float32(-32.19f, 94.07f, true));
    CheckLubIs(lhs, Type::Float32NaN(), Type::Float32(-32.19f, 94.07f, true));
    CheckLubIs(lhs, Type::Float32(0.0f), lhs);
    CheckLubIs(lhs, Type::Float32(-19.9f, 31.29f), lhs);
    CheckLubIs(lhs, Type::Float32(-91.22f, -40.0f),
               Type::Float32(-91.22f, 94.07f));
    CheckLubIs(lhs, Type::Float32(0.0f, 1993.0f),
               Type::Float32(-32.19f, 1993.0f));
    CheckLubIs(lhs, Type::Float32(-100.0f, 100.0f, true),
               Type::Float32(-100.0f, 100.0f, true));
  }

  {
    const auto lhs = Type::Float32(-0.04f);
    CheckLubIs(lhs, lhs, lhs);
    CheckLubIs(lhs, Type::Float32NaN(), Type::Float32(-0.04f, true));
    CheckLubIs(lhs, Type::Float32(17.14f), Type::Float32(-0.04f, 17.14f));
    CheckLubIs(lhs, Type::Float32(-75.4f, -12.7f),
               Type::Float32(-75.4f, -0.04f));
    CheckLubIs(lhs, Type::Float32(0.04f, true),
               Type::Float32(-0.04f, 0.04f, true));
  }
}

TEST_F(TurboshaftTypesTest, Float64LeastUpperBound) {
  auto CheckLubIs = [&](const Float64Type& lhs, const Float64Type& rhs,
                        const Float64Type& expected) {
    CHECK(Float64Type::LeastUpperBound(lhs, rhs, zone()).Equals(expected));
  };

  {
    const auto lhs = Type::Float64(-32.19, 94.07);
    CheckLubIs(lhs, lhs, lhs);
    CheckLubIs(lhs, Type::Float64(-32.19, 94.07, true),
               Type::Float64(-32.19, 94.07, true));
    CheckLubIs(lhs, Type::Float64NaN(), Type::Float64(-32.19, 94.07, true));
    CheckLubIs(lhs, Type::Float64(0.0), lhs);
    CheckLubIs(lhs, Type::Float64(-19.9, 31.29), lhs);
    CheckLubIs(lhs, Type::Float64(-91.22, -40.0), Type::Float64(-91.22, 94.07));
    CheckLubIs(lhs, Type::Float64(0.0, 1993.0), Type::Float64(-32.19, 1993.0));
    CheckLubIs(lhs, Type::Float64(-100.0, 100.0, true),
               Type::Float64(-100.0, 100.0, true));
  }

  {
    const auto lhs = Type::Float64(-0.04);
    CheckLubIs(lhs, lhs, lhs);
    CheckLubIs(lhs, Type::Float64NaN(), Type::Float64(-0.04, true));
    CheckLubIs(lhs, Type::Float64(17.14), Type::Float64(-0.04, 17.14));
    CheckLubIs(lhs, Type::Float64(-75.4, -12.7), Type::Float64(-75.4, -0.04));
    CheckLubIs(lhs, Type::Float64(0.04, true),
               Type::Float64(-0.04, 0.04, true));
  }
}
#endif
}  // namespace v8::internal::compiler::turboshaft
