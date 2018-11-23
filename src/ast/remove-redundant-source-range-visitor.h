// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_AST_REMOVE_REDUNDANT_SOURCE_RANGE_VISITOR_H_
#define V8_AST_REMOVE_REDUNDANT_SOURCE_RANGE_VISITOR_H_

#include "src/ast/ast-source-ranges.h"
#include "src/ast/ast-traversal-visitor.h"

namespace v8 {
namespace internal {

// Removes the source range associated with the final statement in a block
// or function body (if the parent itself has a source range
// associated with it). The reasoning being that the parent's source range will
// fully enclose the child, but includes additional trailing brackets and
// whitespace that would otherwise be missed by coverage.
//
// This corrects the edge-case outlined below:
//
// function () {
//   return 'hello world'
// } <-- this closing bracket would be unreachable without cleanup step.
class RemoveRedundantSourceRangeVisitor final
    : public AstTraversalVisitor<RemoveRedundantSourceRangeVisitor> {
 public:
  RemoveRedundantSourceRangeVisitor(uintptr_t stack_limit, Expression* root,
                                    SourceRangeMap* source_range_map);

 private:
  // This is required so that the overriden Visit* methods can be
  // called by the base class (template).
  friend class AstTraversalVisitor<RemoveRedundantSourceRangeVisitor>;

  void VisitBlock(Block* stmt);
  void VisitFunctionLiteral(FunctionLiteral* expr);
  void MaybeRemoveLastRange(ZonePtrList<Statement>* stmts);

  SourceRangeMap* source_range_map_ = nullptr;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_AST_REMOVE_REDUNDANT_SOURCE_RANGE_VISITOR_H_
