// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/declarations.h"
#include "src/torque/declarable.h"

namespace v8 {
namespace internal {
namespace torque {

Scope* Declarations::GetNodeScope(const AstNode* node) {
  auto i = node_scopes_.find(node);
  if (i != node_scopes_.end()) return i->second;
  Scope* result = chain_.NewScope();
  node_scopes_[node] = result;
  return result;
}

void Declarations::CheckAlreadyDeclared(SourcePosition pos,
                                        const std::string& name,
                                        const char* new_type) {
  auto i = chain_.ShallowLookup(name);
  if (i != nullptr) {
    std::stringstream s;
    s << "cannot redeclare " << name << " (type " << new_type << ") at "
      << PositionAsString(pos) << std::endl;
    ReportError(s.str());
  }
}

Type Declarations::DeclareType(SourcePosition pos, const std::string& name,
                               const std::string& generated,
                               const std::string* parent) {
  CheckAlreadyDeclared(pos, name, "type");
  TypeImpl* parent_type = nullptr;
  if (parent != nullptr) {
    Declarable* maybe_parent_type = Lookup(*parent);
    if (maybe_parent_type == nullptr) {
      std::stringstream s;
      s << "cannot find parent type \"" << *parent << "\" at  "
        << PositionAsString(pos);
      ReportError(s.str());
    }
    if (!maybe_parent_type->IsTypeImpl()) {
      std::stringstream s;
      s << "parent \"" << *parent << "\" of type \"" << name << "\""
        << " is not a type "
        << " at  " << PositionAsString(pos);
      ReportError(s.str());
    }
    parent_type = TypeImpl::cast(maybe_parent_type);
  }
  TypeImpl* result = new TypeImpl(parent_type, name, generated);
  Declare(name, std::unique_ptr<Declarable>(result));
  return Type(result);
}

Label* Declarations::DeclareLabel(SourcePosition pos, const std::string& name) {
  CheckAlreadyDeclared(pos, name, "label");
  Label* result = new Label(name);
  Declare(name, std::unique_ptr<Declarable>(result));
  return result;
}

Macro* Declarations::DeclareMacro(SourcePosition pos, const std::string& name,
                                  const Signature& signature) {
  auto previous = chain_.Lookup(name);
  MacroList* macro_list = nullptr;
  if (previous == nullptr) {
    macro_list = new MacroList();
    Declare(name, std::unique_ptr<Declarable>(macro_list));
  } else if (!previous->IsMacroList()) {
    std::stringstream s;
    s << "cannot redeclare " << name << " as a non-macro at "
      << PositionAsString(pos);
    ReportError(s.str());
  } else {
    macro_list = MacroList::cast(previous);
  }
  for (auto& macro : macro_list->list()) {
    if (signature.parameter_types.types ==
            macro->signature().parameter_types.types &&
        signature.parameter_types.var_args ==
            macro->signature().parameter_types.var_args) {
      std::stringstream s;
      s << "cannot redeclare " << name
        << " as a macro with identical parameter list "
        << signature.parameter_types << PositionAsString(pos);
      ReportError(s.str());
    }
  }
  return macro_list->AddMacro(new Macro(name, signature));
}

Builtin* Declarations::DeclareBuiltin(SourcePosition pos,
                                      const std::string& name,
                                      Builtin::Kind kind,
                                      const Signature& signature) {
  CheckAlreadyDeclared(pos, name, "builtin");
  Builtin* result = new Builtin(name, kind, signature);
  Declare(name, std::unique_ptr<Declarable>(result));
  return result;
}

Runtime* Declarations::DeclareRuntime(SourcePosition pos,
                                      const std::string& name,
                                      const Signature& signature) {
  CheckAlreadyDeclared(pos, name, "runtime");
  Runtime* result = new Runtime(name, signature);
  Declare(name, std::unique_ptr<Declarable>(result));
  return result;
}

Variable* Declarations::DeclareVariable(SourcePosition pos,
                                        const std::string& var, Type type) {
  std::string name(var + std::to_string(GetNextUniqueDeclarationNumber()));
  CheckAlreadyDeclared(pos, var, "variable");
  Variable* result = new Variable(var, name, type);
  Declare(var, std::unique_ptr<Declarable>(result));
  return result;
}

Parameter* Declarations::DeclareParameter(SourcePosition pos,
                                          const std::string& name,
                                          const std::string& var_name,
                                          Type type) {
  CheckAlreadyDeclared(pos, name, "parameter");
  Parameter* result = new Parameter(name, type, var_name);
  Declare(name, std::unique_ptr<Declarable>(result));
  return result;
}

Label* Declarations::DeclarePrivateLabel(SourcePosition pos,
                                         const std::string& raw_name) {
  std::string name =
      raw_name + "_" + std::to_string(GetNextUniqueDeclarationNumber());
  CheckAlreadyDeclared(pos, name, "label");
  Label* result = new Label(name);
  Declare(name, std::unique_ptr<Declarable>(result));
  return result;
}

void Declarations::DeclareConstant(SourcePosition pos, const std::string& name,
                                   Type type, const std::string& value) {
  CheckAlreadyDeclared(pos, name, "constant, parameter or arguments");
  Constant* result = new Constant(name, type, value);
  Declare(name, std::unique_ptr<Declarable>(result));
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
