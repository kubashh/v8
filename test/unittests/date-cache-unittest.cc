// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/base/platform/platform.h"
#include "src/date.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "unicode/strenum.h"
#include "unicode/timezone.h"

namespace v8 {
namespace internal {

static const size_t kGetLocalOffsetFromOSIteration = 1987;
static const size_t kLocalTimezoneIteration = 2697;
static const int64_t kStartTime = 1557288964845;

class AdoptDefaultThread final : public base::Thread {
 public:
  AdoptDefaultThread() : base::Thread(Options("AdoptDefault")) {}

  void Run() override {
    std::unique_ptr<icu::StringEnumeration> s(
        icu::TimeZone::createEnumeration());
    const icu::UnicodeString* name = nullptr;
    UErrorCode status = U_ZERO_ERROR;
    while ((name = s->snext(status)) != nullptr) {
      icu::TimeZone::adoptDefault(icu::TimeZone::createTimeZone(*name));
    }
  }
};

class GetLocalOffsetFromOSThread final : public base::Thread {
 public:
  explicit GetLocalOffsetFromOSThread(bool utc)
      : base::Thread(Options("GetLocalOffsetFromOS")), utc_(utc) {}

  void Run() override {
    int64_t time = kStartTime;
    for (size_t n = 0; n < kGetLocalOffsetFromOSIteration; ++n) {
      DateCache date_cache;
      time += 6000 * n;
      date_cache.GetLocalOffsetFromOS(time, utc_);
    }
  }

 private:
  bool utc_;
};

class LocalTimezoneThread final : public base::Thread {
 public:
  LocalTimezoneThread() : base::Thread(Options("LocalTimezone")) {}

  void Run() override {
    int64_t time = kStartTime;
    for (size_t n = 0; n < kLocalTimezoneIteration; ++n) {
      DateCache date_cache;
      time += 7001 * n;
      date_cache.LocalTimezone(time);
    }
  }
};

TEST(DateCache, ResetDateCache) {
  AdoptDefaultThread t1;
  GetLocalOffsetFromOSThread t2(true);
  GetLocalOffsetFromOSThread t3(false);
  LocalTimezoneThread t4;

  t1.Start();
  t2.Start();
  t3.Start();
  t4.Start();

  t1.Join();
  t2.Join();
  t3.Join();
  t4.Join();
  printf("ResetDateCache Finished\n");
}

}  // namespace internal
}  // namespace v8
