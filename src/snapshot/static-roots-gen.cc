// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/static-roots-gen.h"

#include <fstream>
#include <functional>
#include <initializer_list>
#include <ranges>

#include "src/common/globals.h"
#include "src/common/ptr-compr-inl.h"
#include "src/execution/isolate.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/instance-type.h"
#include "src/objects/objects-definitions.h"
#include "src/objects/visitors.h"
#include "src/roots/roots-inl.h"
#include "src/roots/roots.h"
#include "src/roots/static-roots.h"

namespace v8 {
namespace internal {

class StaticRootsTableGenImpl {
 public:
  explicit StaticRootsTableGenImpl(Isolate* isolate) {
    // Define some object type ranges of interest
    //
    // These are manually curated lists of objects that are explicitly placed
    // next to each other on the read only heap and also correspond to important
    // instance type ranges.
    std::list<RootIndex> string, internalized_string;
#define ELEMENT(type, size, name, CamelName)                     \
  string.push_back(RootIndex::k##CamelName##Map);                \
  if (InstanceTypeChecker::IsInternalizedString(type)) {         \
    internalized_string.push_back(RootIndex::k##CamelName##Map); \
  }
    STRING_TYPE_LIST(ELEMENT)
#undef ELEMENT

    root_ranges_.emplace_back("FIRST_STRING_TYPE", "LAST_STRING_TYPE", string);
    root_ranges_.emplace_back("INTERNALIZED_STRING_TYPE",
                              "INTERNALIZED_STRING_TYPE", internalized_string);
    string.push_back(RootIndex::kSymbolMap);
    root_ranges_.emplace_back("FIRST_NAME_TYPE", "LAST_NAME_TYPE", string);

    std::list<RootIndex> allocation_site;
#define ELEMENT(_1, _2, CamelName) \
  allocation_site.push_back(RootIndex::k##CamelName);
    ALLOCATION_SITE_MAPS_LIST(ELEMENT);
#undef ELEMENT
    root_ranges_.emplace_back("ALLOCATION_SITE_TYPE", "ALLOCATION_SITE_TYPE",
                              allocation_site);

    // Collect all roots
    ReadOnlyRoots ro_roots(isolate);
    {
      RootIndex pos = RootIndex::kFirstReadOnlyRoot;
#define ADD_ROOT(_, value, CamelName)                       \
  {                                                         \
    Tagged_t ptr = V8HeapCompressionScheme::CompressTagged( \
        ro_roots.unchecked_##value().ptr());                \
    sorted_roots_[ptr].push_back(pos);                      \
    camel_names_[RootIndex::k##CamelName] = #CamelName;     \
    ++pos;                                                  \
  }
      READ_ONLY_ROOT_LIST(ADD_ROOT)
#undef ADD_ROOT
    }

    // Compute start and end of ranges
    for (auto& entry : sorted_roots_) {
      Tagged_t ptr = entry.first;
      auto& roots = entry.second;

      for (auto& pos : roots) {
        std::string& name = camel_names_.at(pos);
        for (auto& range : root_ranges_) {
          range.apply(name, pos, ptr);
        }
      }
    }
  }

  // Used to compute ranges of objects next to each other on the r/o heap
  class ObjectRange {
   public:
    ObjectRange(const std::string& first, const std::string& last,
                const std::list<RootIndex> objects)
        : first_instance_type_(first),
          last_instance_type_(last),
          objects_(objects) {}
    ~ObjectRange() {
      CHECK(!open_);
      CHECK(first_ != RootIndex::kRootListLength);
      CHECK(last_ != RootIndex::kRootListLength);
    }

    ObjectRange(ObjectRange& range) = delete;
    ObjectRange& operator=(ObjectRange& range) = delete;
    ObjectRange(ObjectRange&& range) V8_NOEXCEPT = default;
    ObjectRange& operator=(ObjectRange&& range) V8_NOEXCEPT = default;

    void apply(const std::string& root_name, RootIndex idx, Tagged_t ptr) {
      auto test = [&](RootIndex obj) {
        return std::find(objects_.begin(), objects_.end(), obj) !=
               objects_.end();
      };
      if (open_) {
        if (test(idx)) {
          last_ = idx;
        } else {
          open_ = false;
        }
        return;
      }

      if (first_ == RootIndex::kRootListLength) {
        if (test(idx)) {
          first_ = idx;
          open_ = true;
        }
      } else {
        CHECK_WITH_MSG(!test(idx),
                       (first_instance_type_ + "-" + last_instance_type_ +
                        " does not specify a continuous range of "
                        "objects. There is a gap before " +
                        root_name)
                           .c_str());
      }
    }

    const std::string& first_instance_type() const {
      return first_instance_type_;
    }
    const std::string& last_instance_type() const {
      return last_instance_type_;
    }
    RootIndex first() const { return first_; }
    RootIndex last() const { return last_; }

   private:
    RootIndex first_ = RootIndex::kRootListLength;
    RootIndex last_ = RootIndex::kRootListLength;
    std::string first_instance_type_;
    std::string last_instance_type_;

    std::list<RootIndex> objects_;
    bool open_ = false;

    friend class StaticRootsTableGenImpl;
  };

  size_t RangesHash() const {
    size_t hash = 0;
    for (auto& range : root_ranges_) {
      hash = base::hash_combine(hash, range.first_,
                                base::hash_combine(hash, range.last_));
    }
    return hash;
  }

  const std::map<Tagged_t, std::list<RootIndex>>& sorted_roots() {
    return sorted_roots_;
  }

  const std::list<ObjectRange>& root_ranges() { return root_ranges_; }

  const std::string& camel_name(RootIndex idx) { return camel_names_.at(idx); }

 private:
  std::map<Tagged_t, std::list<RootIndex>> sorted_roots_;
  std::list<ObjectRange> root_ranges_;
  std::unordered_map<RootIndex, std::string> camel_names_;
};

// Check if the computed ranges are still valid, ie. all their members lie
// between known boundaries.
void StaticRootsTableGen::VerifyRanges(Isolate* isolate) {
#if V8_STATIC_ROOTS_BOOL
  StaticRootsTableGenImpl gen(isolate);
  CHECK_WITH_MSG(kStaticReadOnlyRootRangesHash == gen.RangesHash(),
                 "StaticReadOnlyRanges changed. Run "
                 "tools/dev/gen-static-roots.py` to update static-roots.h.");
#endif  // V8_STATIC_ROOTS_BOOL
}

void StaticRootsTableGen::write(Isolate* isolate, const char* file) {
  CHECK_WITH_MSG(!V8_STATIC_ROOTS_BOOL,
                 "Re-generating the table of roots is only supported in builds "
                 "with v8_enable_static_roots disabled");
  CHECK(file);
  static_assert(static_cast<int>(RootIndex::kFirstReadOnlyRoot) == 0);

  std::ofstream out(file);

  out << "// Copyright 2022 the V8 project authors. All rights reserved.\n"
      << "// Use of this source code is governed by a BSD-style license "
         "that can be\n"
      << "// found in the LICENSE file.\n"
      << "\n"
      << "// This file is automatically generated by "
         "`tools/dev/gen-static-roots.py`. Do\n// not edit manually.\n"
      << "\n"
      << "#ifndef V8_ROOTS_STATIC_ROOTS_H_\n"
      << "#define V8_ROOTS_STATIC_ROOTS_H_\n"
      << "\n"
      << "#include \"src/common/globals.h\"\n"
      << "\n"
      << "#if V8_STATIC_ROOTS_BOOL\n"
      << "\n"
      << "// Disabling Wasm or Intl invalidates the contents of "
         "static-roots.h.\n"
      << "// TODO(olivf): To support static roots for multiple build "
         "configurations we\n"
      << "//              will need to generate target specific versions of "
         "this file.\n"
      << "static_assert(V8_ENABLE_WEBASSEMBLY);\n"
      << "static_assert(V8_INTL_SUPPORT);\n"
      << "\n"
      << "namespace v8 {\n"
      << "namespace internal {\n"
      << "\n"
      << "struct StaticReadOnlyRoot {\n";

  // Output a symbol for every root. Ordered by ptr to make it easier to see the
  // memory layout of the read only page.
  const auto size = static_cast<int>(RootIndex::kReadOnlyRootsCount);
  StaticRootsTableGenImpl gen(isolate);

  for (auto& entry : gen.sorted_roots()) {
    Tagged_t ptr = entry.first;
    auto& roots = entry.second;

    for (auto& root : roots) {
      const std::string& name = gen.camel_name(root);
      out << "  static constexpr Tagged_t k" << name << " =";
      if (name.length() + 39 > 80) out << "\n     ";
      out << " " << reinterpret_cast<void*>(ptr) << ";\n";
    }
  }
  out << "};\n";

  // Output in order of roots table
  out << "\nstatic constexpr std::array<Tagged_t, " << size
      << "> StaticReadOnlyRootsPointerTable = {\n";

  {
#define ENTRY(_1, _2, CamelName) \
  { out << "    StaticReadOnlyRoot::k" << #CamelName << ",\n"; }
    READ_ONLY_ROOT_LIST(ENTRY)
#undef ENTRY
    out << "};\n";
  }
  out << "\n";

  // Output interesting ranges of consecutive roots
  out << "inline constexpr std::optional<std::pair<RootIndex, RootIndex>>\n"
         "StaticReadOnlyRootRange(InstanceType first, InstanceType last) {\n";
  for (const auto& rng : gen.root_ranges()) {
    const std::string& first_name = gen.camel_name(rng.first());
    const std::string& last_name = gen.camel_name(rng.last());
    out << "  if (first == " << rng.first_instance_type()
        << " && last == " << rng.last_instance_type() << ") {\n"
        << "    return {{RootIndex::k" << first_name << ",";
    if (rng.first_instance_type().length() + rng.last_instance_type().length() +
            43 >
        80) {
      out << "\n            ";
    }
    out << " RootIndex::k" << last_name << "}};\n"
        << "  }\n";
  }
  out << "  return {};\n}\n";
  out << "static constexpr size_t kStaticReadOnlyRootRangesHash = "
      << gen.RangesHash() << "UL;\n";

  out << "\n}  // namespace internal\n"
      << "}  // namespace v8\n"
      << "#endif  // V8_STATIC_ROOTS_BOOL\n"
      << "#endif  // V8_ROOTS_STATIC_ROOTS_H_\n";
}

}  // namespace internal
}  // namespace v8
