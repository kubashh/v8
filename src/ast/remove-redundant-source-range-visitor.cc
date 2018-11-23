// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ast/remove-redundant-source-range-visitor.h"

namespace v8 {
namespace internal {

RemoveRedundantSourceRangeVisitor::RemoveRedundantSourceRangeVisitor(
    uintptr_t stack_limit, Expression* root, SourceRangeMap* source_range_map)
    : AstTraversalVisitor(stack_limit, root),
      source_range_map_(source_range_map) {}

void RemoveRedundantSourceRangeVisitor::VisitBlock(Block* stmt) {
  AstTraversalVisitor::VisitBlock(stmt);
  ZonePtrList<Statement>* stmts = stmt->statements();
  AstNodeSourceRanges* enclosingSourceRanges = source_range_map_->Find(stmt);
  if (enclosingSourceRanges != nullptr) {
    MaybeRemoveLastRange(stmts);
  }
}

void RemoveRedundantSourceRangeVisitor::VisitFunctionLiteral(
    FunctionLiteral* expr) {
  AstTraversalVisitor::VisitFunctionLiteral(expr);
  ZonePtrList<Statement>* stmts = expr->body();
  MaybeRemoveLastRange(stmts);
}

void RemoveRedundantSourceRangeVisitor::MaybeRemoveLastRange(
    ZonePtrList<Statement>* stmts) {
  if (!stmts->length()) return;
  Statement* last = stmts->at(stmts->length() - 1);
  if (last->IsIfStatement()) {
    IfStatement* ifstmt = reinterpret_cast<IfStatement*>(last);
    IfStatementSourceRanges* ranges =
        reinterpret_cast<IfStatementSourceRanges*>(
            source_range_map_->Find(last));
    if (ranges) {
      source_range_map_->Erase(ifstmt->then_statement());
      source_range_map_->Erase(ifstmt->else_statement());
      ranges->DisableContinuation();
    }
  } else if (last->IsJump()) {
    AstNodeSourceRanges* ranges = source_range_map_->Find(last);
    if (ranges != nullptr) {
      source_range_map_->Erase(last);
    }
  }
}

}  // namespace internal
}  // namespace v8
