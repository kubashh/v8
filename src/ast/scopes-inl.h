// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_AST_SCOPES_INL_H_
#define V8_AST_SCOPES_INL_H_

#include "src/ast/scopes.h"

namespace v8 {
namespace internal {

void Scope::CollectNonLocals(DeclarationScope* max_outer_scope,
                             Isolate* isolate, ParseInfo* info,
                             Handle<StringSet>* non_locals) {
  // Module variables must be allocated before variable resolution
  // to ensure that UpdateNeedsHoleCheck() can detect import variables.
  if (is_module_scope()) AsModuleScope()->AllocateModuleVariables();

  // Lazy parsed declaration scopes are already partially analyzed. If there are
  // unresolved references remaining, they just need to be resolved in outer
  // scopes.
  Scope* lookup =
      is_declaration_scope() && AsDeclarationScope()->was_lazily_parsed()
          ? outer_scope()
          : this;

  for (VariableProxy* proxy = unresolved_list_.first(); proxy != nullptr;
       proxy = proxy->next_unresolved()) {
    DCHECK(!proxy->is_resolved());
    Variable* var = Lookup(proxy, lookup, max_outer_scope->outer_scope());
    if (var == nullptr) {
      *non_locals = StringSet::Add(isolate, *non_locals, proxy->name());
    } else {
      // In this case we need to leave scopes in a way that they can be
      // allocated. If we resolved variables from lazy parsed scopes, we need
      // to context allocate the var.
      ResolveTo(info, proxy, var);
      if (!var->is_dynamic() && lookup != this) var->ForceContextAllocation();
    }
  }

  // Clear unresolved_list_ as it's in an inconsistent state.
  unresolved_list_.Clear();

  for (Scope* scope = inner_scope_; scope != nullptr; scope = scope->sibling_) {
    scope->CollectNonLocals(max_outer_scope, isolate, info, non_locals);
  }
}

void Scope::AnalyzePartially(
    DeclarationScope* max_outer_scope, AstNodeFactory* ast_node_factory,
    base::ThreadedList<VariableProxy>* new_unresolved_list) {
  DCHECK_IMPLIES(is_declaration_scope(),
                 !AsDeclarationScope()->was_lazily_parsed());

  for (VariableProxy* proxy = unresolved_list_.first(); proxy != nullptr;
       proxy = proxy->next_unresolved()) {
    DCHECK(!proxy->is_resolved());
    Variable* var = Lookup(proxy, this, max_outer_scope->outer_scope());
    if (var == nullptr) {
      // Don't copy unresolved references to the script scope, unless it's a
      // reference to a private name or method. In that case keep it so we
      // can fail later.
      if (!max_outer_scope->outer_scope()->is_script_scope() ||
          proxy->is_private_name()) {
        VariableProxy* copy = ast_node_factory->CopyVariableProxy(proxy);
        new_unresolved_list->AddFront(copy);
      }
    } else if (var != Scope::kDummyPreParserVariable &&
               var != Scope::kDummyPreParserLexicalVariable) {
      var->set_is_used();
      if (proxy->is_assigned()) var->set_maybe_assigned();
    }
  }

  // Clear unresolved_list_ as it's in an inconsistent state.
  unresolved_list_.Clear();

  for (Scope* scope = inner_scope_; scope != nullptr; scope = scope->sibling_) {
    scope->AnalyzePartially(max_outer_scope, ast_node_factory,
                            new_unresolved_list);
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_AST_SCOPES_INL_H_
