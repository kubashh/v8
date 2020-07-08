// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef V8_ENABLE_PRECISE_ZONE_STATS

#if defined(__clang__) || defined(__GLIBCXX__)
#include <cxxabi.h>
#endif  // __GLIBCXX__

#include "src/utils/utils.h"
#include "src/zone/type-stats.h"

namespace v8 {
namespace internal {

void TypeStats::MergeWith(const TypeStats& other) {
  for (auto const& item : other.map_) {
    Add(item.first, item.second);
  }
}

class Demangler {
 public:
  Demangler() = default;
  ~Demangler() {
    if (buffer_) free(buffer_);
    // USE(buffer_len_);
  }

  const char* demangle(std::type_index type_id) {
#if defined(__clang__) || defined(__GLIBCXX__)
    int status = -1;
    char* result =
        abi::__cxa_demangle(type_id.name(), buffer_, &buffer_len_, &status);
    if (status == 0) {
      // Upon success, the buffer_ may be reallocated.
      buffer_ = result;
      return buffer_;
    }
#endif
    return type_id.name();
  }

 private:
  char* buffer_ = nullptr;
  size_t buffer_len_ = 0;
};

void TypeStats::Dump() const {
  Demangler d;
  PrintF("===== TypeStats =====\n");
  for (auto const& item : map_) {
    PrintF("%12zu %s\n", item.second, d.demangle(item.first));
  }
}

void TypeStats::DumpJSON(std::ostringstream& out) const {
  out << "[";
  bool first = true;
  Demangler d;
  for (auto const& item : map_) {
    if (first) {
      first = false;
    } else {
      out << ", ";
    }
    out << "{"
        << "\"type\": \"" << d.demangle(item.first) << "\", "
        << "\"size\": " << item.second << "}";
  }
  out << "]";
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_PRECISE_ZONE_STATS
