// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/assembler-buffer-cache.h"

#include "src/codegen/assembler.h"

namespace v8::internal::wasm {

class CachedAssemblerBuffer final : public AssemblerBuffer {
 public:
  CachedAssemblerBuffer(AssemblerBufferCache* cache, base::AddressRegion region)
      : cache_(cache), region_(region) {}

  ~CachedAssemblerBuffer() override { cache_->Return(region_); }

  uint8_t* start() const override {
    return reinterpret_cast<uint8_t*>(region_.begin());
  }

  int size() const override { return static_cast<int>(region_.size()); }

  std::unique_ptr<AssemblerBuffer> Grow(int new_size) override {
    return cache_->GetAssemblerBuffer(new_size);
  }

 private:
  AssemblerBufferCache* const cache_;
  const base::AddressRegion region_;
};

std::unique_ptr<AssemblerBuffer> AssemblerBufferCache::GetAssemblerBuffer(
    int size) {
  // TODO(12809): Return PKU-protected buffers, and cache them.
  return NewAssemblerBuffer(size);
}

void AssemblerBufferCache::Return(base::AddressRegion region) {
  // TODO(12809): Actually cache the assembler buffers.
}

}  // namespace v8::internal::wasm
