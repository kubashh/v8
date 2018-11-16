// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ast/source-range-remover.h"

namespace v8 {
namespace internal {

SourceRangeRemover::SourceRangeRemover(uintptr_t stack_limit, Expression* root,
                                       SourceRangeMap* source_range_map)
    : AstTraversalVisitor(stack_limit, root),
      source_range_map_(source_range_map) {}

// remove the final source ranges in statement lists, if they terminate
// with statements that return early (return, break, continue).
void SourceRangeRemover::VisitStatements(const ZonePtrList<Statement>* stmts) {
  AstTraversalVisitor::VisitStatements(stmts);
  if (stmts->length() == 0) return;
  Statement* last = stmts->at(stmts->length() - 1);
  // only remove source ranges for statements like break, return, continue ...
  if (last->IsJump() && !last->IsIfStatement() && !last->IsIfStatement() &&
      !last->IsExpressionStatement() && !last->IsBlock()) {
    AstNodeSourceRanges* ranges = source_range_map_->Find(last);
    if (ranges != nullptr) {
      source_range_map_->Erase(last);
    }
  }
}

}  // namespace internal
}  // namespace v8
