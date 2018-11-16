// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_AST_SOURCE_RANGE_REMOVER_H_
#define V8_AST_SOURCE_RANGE_REMOVER_H_

#include "src/ast/ast-source-ranges.h"
#include "src/ast/ast-traversal-visitor.h"

namespace v8 {
namespace internal {

class SourceRangeRemover final
    : public AstTraversalVisitor<SourceRangeRemover> {
 public:
  SourceRangeRemover(uintptr_t stack_limit, Expression* root,
                     SourceRangeMap* source_range_map);

 private:
  // This is required so that the overriden Visit* methods can be
  // called by the base class (template).
  friend class AstTraversalVisitor<SourceRangeRemover>;

  // remove the final source ranges in statement lists, if they terminate
  // with statements that return early (return, break, continue).
  void VisitStatements(const ZonePtrList<Statement>* stmts);

  SourceRangeMap* source_range_map_ = nullptr;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_AST_SOURCE_RANGE_REMOVER_H_
