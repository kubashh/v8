// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/allocation.h"
#include "include/v8-context.h"
#include "include/v8-internal.h"
#include "include/v8-local-handle.h"
#include "include/v8-persistent-handle.h"
#include "include/v8-sandbox.h"
#include "include/v8-template.h"
#include "src/api/api-inl.h"
#include "src/base/macros.h"
#include "src/execution/isolate-utils-inl.h"
#include "src/objects/js-objects-inl.h"
#include "test/benchmarks/cpp/benchmark-utils.h"
#include "third_party/google_benchmark_chrome/src/include/benchmark/benchmark.h"

class IsolateGetter : public v8::benchmarking::BenchmarkWithIsolate {
 public:
  void SetUp(::benchmark::State& state) override {
    auto* isolate = v8_isolate();
    v8::HandleScope handle_scope(isolate);

    v8::Local<v8::Context> context = v8::Context::New(isolate);

    context_.Reset(isolate, context);
    context->Enter();
  }

  void TearDown(::benchmark::State& state) override {
    auto* isolate = v8_isolate();
    v8::HandleScope handle_scope(isolate);
    auto context = context_.Get(isolate);
    context->Exit();
    context_.Reset();
  }

 protected:
  v8::Local<v8::Context> v8_context() { return context_.Get(v8_isolate()); }

  v8::Global<v8::Context> context_;
};

BENCHMARK_F(IsolateGetter, FromPageMetadata)(benchmark::State& st) {
  v8::HandleScope handle_scope(v8_isolate());
  i::Tagged<i::Context> context = *v8::Utils::OpenHandle(*v8_context());

  for (auto _ : st) {
    USE(_);
    i::Isolate* isolate = i::GetIsolateFromWritableObject(context);
    benchmark::DoNotOptimize(isolate);
  }
}

BENCHMARK_F(IsolateGetter, FromThreadLocal)(benchmark::State& st) {
  for (auto _ : st) {
    USE(_);
    i::Isolate* isolate = i::Isolate::Current();
    benchmark::DoNotOptimize(isolate);
  }
}
