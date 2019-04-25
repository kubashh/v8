// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_DECLARATION_VISITOR_H_
#define V8_TORQUE_DECLARATION_VISITOR_H_

#include <set>
#include <string>

#include "src/base/macros.h"
#include "src/torque/declarations.h"
#include "src/torque/global-context.h"
#include "src/torque/types.h"
#include "src/torque/utils.h"

namespace v8 {
namespace internal {
namespace torque {

Namespace* GetOrCreateNamespace(const std::string& name);

class TypeDeclarationVisitor {
 public:
  void Visit(Ast* ast) {
    CurrentScope::Scope current_namespace(GlobalContext::GetDefaultNamespace());
    for (Declaration* child : ast->declarations()) Visit(child);
  }
  void Visit(Declaration* decl);
  void Visit(NamespaceDeclaration* decl) {
    CurrentScope::Scope current_scope(GetOrCreateNamespace(decl->name));
    for (Declaration* child : decl->declarations) Visit(child);
  }
  void Visit(AbstractTypeDeclaration* decl);
  void Visit(StructDeclaration* decl) {
    Declarations::PreDeclareTypeAlias(decl->name, decl, false);
  }
  void Visit(ClassDeclaration* decl) {
    const TypeAlias* alias =
        Declarations::PreDeclareTypeAlias(decl->name, decl, false);
    class_declarations_.push_back(std::make_tuple(decl, alias));
  }
  void Visit(TypeAliasDeclaration* decl) {
    Declarations::PreDeclareTypeAlias(decl->name, decl, true);
  }

  void ResolveAliases();
  void FinalizeClasses();

 private:
  std::vector<std::tuple<ClassDeclaration*, const TypeAlias*>>
      class_declarations_;
};

class DeclarationVisitor {
 public:
  static void Visit(Ast* ast) {
    CurrentScope::Scope current_namespace(GlobalContext::GetDefaultNamespace());
    for (Declaration* child : ast->declarations()) Visit(child);
  }
  static void Visit(Declaration* decl);
  static void Visit(NamespaceDeclaration* decl) {
    CurrentScope::Scope current_scope(GetOrCreateNamespace(decl->name));
    for (Declaration* child : decl->declarations) Visit(child);
  }

  static void Visit(TypeDeclaration* decl) {
    Declarations::ResolveType(decl->name);
  }

  static Builtin* CreateBuiltin(BuiltinDeclaration* decl,
                                std::string external_name,
                                std::string readable_name, Signature signature,
                                base::Optional<Statement*> body);
  static void Visit(ExternalBuiltinDeclaration* decl,
                    const Signature& signature,
                    base::Optional<Statement*> body) {
    Declarations::Declare(
        decl->name,
        CreateBuiltin(decl, decl->name, decl->name, signature, base::nullopt));
  }

  static void Visit(ExternalRuntimeDeclaration* decl, const Signature& sig,
                    base::Optional<Statement*> body);
  static void Visit(ExternalMacroDeclaration* decl, const Signature& sig,
                    base::Optional<Statement*> body);
  static void Visit(TorqueBuiltinDeclaration* decl, const Signature& signature,
                    base::Optional<Statement*> body);
  static void Visit(TorqueMacroDeclaration* decl, const Signature& signature,
                    base::Optional<Statement*> body);
  static void Visit(IntrinsicDeclaration* decl, const Signature& signature,
                    base::Optional<Statement*> body);

  static void Visit(CallableNode* decl, const Signature& signature,
                    base::Optional<Statement*> body);

  static void Visit(ConstDeclaration* decl);
  static void Visit(StandardDeclaration* decl);
  static void Visit(GenericDeclaration* decl);
  static void Visit(SpecializationDeclaration* decl);
  static void Visit(ExternConstDeclaration* decl);
  static void Visit(CppIncludeDeclaration* decl);

  static Signature MakeSpecializedSignature(const SpecializationKey& key);
  static Callable* SpecializeImplicit(const SpecializationKey& key);
  static Callable* Specialize(
      const SpecializationKey& key, CallableNode* declaration,
      base::Optional<const CallableNodeSignature*> signature,
      base::Optional<Statement*> body);

 private:
  static void DeclareSpecializedTypes(const SpecializationKey& key);
};

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_DECLARATION_VISITOR_H_
