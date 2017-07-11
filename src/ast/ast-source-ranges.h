// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_AST_AST_SOURCE_RANGES_H_
#define V8_AST_AST_SOURCE_RANGES_H_

#include "src/ast/ast.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

#define AST_SOURCE_RANGE_LIST(V) V(Throw)

enum class SourceRangeKind {
  kContinuation,
};

class AstNodeSourceRanges : public ZoneObject {
 public:
  explicit AstNodeSourceRanges(AstNode::NodeType node_type)
      : node_type_(node_type) {
    switch (node_type) {
#define GENERATE_TYPE_CASE(NodeType) \
  case AstNode::k##NodeType:         \
    break;
      AST_NODE_LIST(GENERATE_TYPE_CASE)
#undef GENERATE_TYPE_CASE
      default:
        UNREACHABLE();
    }
  }

  virtual ~AstNodeSourceRanges() {}

  AstNode::NodeType node_type() const { return node_type_; }
  virtual SourceRange GetRange(SourceRangeKind kind) = 0;

 private:
  const AstNode::NodeType node_type_;
};

class ContinuationSourceRanges : public AstNodeSourceRanges {
 public:
  ContinuationSourceRanges(AstNode::NodeType node_type,
                           int32_t continuation_position)
      : AstNodeSourceRanges(node_type),
        continuation_position_(continuation_position) {}

  SourceRange GetRange(SourceRangeKind kind) {
    DCHECK(kind == SourceRangeKind::kContinuation);
    return SourceRange::OpenEnded(continuation_position_);
  }

 private:
  int32_t continuation_position_;
};

class ThrowSourceRanges final : public ContinuationSourceRanges {
 public:
  explicit ThrowSourceRanges(int32_t continuation_position)
      : ContinuationSourceRanges(AstNode::kThrow, continuation_position) {}
};

class SourceRangeMap final {
 public:
  explicit SourceRangeMap(Zone* zone) : map_(zone) {}

  AstNodeSourceRanges* Find(AstNode* node) {
    auto it = map_.find(node);
    if (it == map_.end()) return nullptr;
    return it->second;
  }

// Type-checked insertion.
#define DEFINE_MAP_INSERT(type)                         \
  void Insert(type* node, type##SourceRanges* ranges) { \
    map_.emplace(node, ranges);                         \
  }
  AST_SOURCE_RANGE_LIST(DEFINE_MAP_INSERT)
#undef DEFINE_MAP_INSERT

 private:
  ZoneMap<AstNode*, AstNodeSourceRanges*> map_;
};

#undef AST_SOURCE_RANGE_LIST

}  // namespace internal
}  // namespace v8

#endif  // V8_AST_AST_SOURCE_RANGES_H_
