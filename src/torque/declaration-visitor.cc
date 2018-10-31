// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/declaration-visitor.h"

namespace v8 {
namespace internal {
namespace torque {

void DeclarationVisitor::Visit(Declaration* decl) {
  CurrentSourcePosition::Scope scope(decl->pos);
  switch (decl->kind) {
#define ENUM_ITEM(name)        \
  case AstNode::Kind::k##name: \
    return Visit(name::cast(decl));
    AST_DECLARATION_NODE_KIND_LIST(ENUM_ITEM)
#undef ENUM_ITEM
    default:
      UNIMPLEMENTED();
  }
}

void DeclarationVisitor::Visit(CallableNode* decl, const Signature& signature,
                               base::Optional<Statement*> body) {
  switch (decl->kind) {
#define ENUM_ITEM(name)        \
  case AstNode::Kind::k##name: \
    return Visit(name::cast(decl), signature, body);
    AST_CALLABLE_NODE_KIND_LIST(ENUM_ITEM)
#undef ENUM_ITEM
    default:
      UNIMPLEMENTED();
  }
}

Builtin* DeclarationVisitor::CreateBuiltin(BuiltinDeclaration* decl,
                                           std::string external_name,
                                           std::string readable_name,
                                           Signature signature,
                                           base::Optional<Statement*> body) {
  const bool javascript = decl->javascript_linkage;
  const bool varargs = decl->signature->parameters.has_varargs;
  Builtin::Kind kind = !javascript ? Builtin::kStub
                                   : varargs ? Builtin::kVarArgsJavaScript
                                             : Builtin::kFixedArgsJavaScript;

  if (signature.types().size() == 0 ||
      !(signature.types()[0] ==
        Declarations::LookupGlobalType(CONTEXT_TYPE_STRING))) {
    std::stringstream stream;
    stream << "first parameter to builtin " << decl->name
           << " is not a context but should be";
    ReportError(stream.str());
  }

  if (varargs && !javascript) {
    std::stringstream stream;
    stream << "builtin " << decl->name
           << " with rest parameters must be a JavaScript builtin";
    ReportError(stream.str());
  }

  if (javascript) {
    if (signature.types().size() < 2 ||
        !(signature.types()[1] ==
          Declarations::LookupGlobalType(OBJECT_TYPE_STRING))) {
      std::stringstream stream;
      stream << "second parameter to javascript builtin " << decl->name
             << " is " << *signature.types()[1] << " but should be Object";
      ReportError(stream.str());
    }
  }

  if (const StructType* struct_type =
          StructType::DynamicCast(signature.return_type)) {
    std::stringstream stream;
    stream << "builtins (in this case" << decl->name
           << ") cannot return structs (in this case " << struct_type->name()
           << ")";
    ReportError(stream.str());
  }

  return Declarations::CreateBuiltin(
      std::move(external_name), std::move(readable_name), kind,
      std::move(signature), decl->transitioning, body);
}

void DeclarationVisitor::Visit(ExternalRuntimeDeclaration* decl,
                               const Signature& signature,
                               base::Optional<Statement*> body) {
  if (GlobalContext::verbose()) {
    std::cout << "found declaration of external runtime " << decl->name
              << " with signature ";
  }

  if (signature.parameter_types.types.size() == 0 ||
      !(signature.parameter_types.types[0] ==
        Declarations::LookupGlobalType(CONTEXT_TYPE_STRING))) {
    std::stringstream stream;
    stream << "first parameter to runtime " << decl->name
           << " is not a context but should be";
    ReportError(stream.str());
  }

  if (signature.return_type->IsStructType()) {
    std::stringstream stream;
    stream << "runtime functions (in this case" << decl->name
           << ") cannot return structs (in this case "
           << static_cast<const StructType*>(signature.return_type)->name()
           << ")";
    ReportError(stream.str());
  }

  Declarations::DeclareRuntimeFunction(decl->name, signature,
                                       decl->transitioning);
}

void DeclarationVisitor::Visit(ExternalMacroDeclaration* decl,
                               const Signature& signature,
                               base::Optional<Statement*> body) {
  if (GlobalContext::verbose()) {
    std::cout << "found declaration of external macro " << decl->name
              << " with signature ";
  }

  Declarations::DeclareMacro(decl->name, decl->external_assembler_name,
                             signature, decl->transitioning, body, decl->op);
}

void DeclarationVisitor::Visit(TorqueBuiltinDeclaration* decl,
                               const Signature& signature,
                               base::Optional<Statement*> body) {
  Declarations::Declare(
      decl->name, CreateBuiltin(decl, decl->name, decl->name, signature, body));
}

void DeclarationVisitor::Visit(TorqueMacroDeclaration* decl,
                               const Signature& signature,
                               base::Optional<Statement*> body) {
  Declarations::DeclareMacro(decl->name, base::nullopt, signature,
                             decl->transitioning, body, decl->op);
}

void DeclarationVisitor::Visit(ConstDeclaration* decl) {
  Declarations::DeclareModuleConstant(
      decl->name, Declarations::GetType(decl->type), decl->expression);
}

void DeclarationVisitor::Visit(StandardDeclaration* decl) {
  Signature signature = MakeSignature(decl->callable->signature.get());
  Visit(decl->callable, signature, decl->body);
}

void DeclarationVisitor::Visit(GenericDeclaration* decl) {
  Declarations::DeclareGeneric(decl->callable->name, decl);
}

void DeclarationVisitor::Visit(SpecializationDeclaration* decl) {
  if ((decl->body != nullptr) == decl->external) {
    std::stringstream stream;
    stream << "specialization of " << decl->name
           << " must either be marked 'extern' or have a body";
    ReportError(stream.str());
  }

  std::vector<Generic*> generic_list = Declarations::LookupGeneric(decl->name);
  // Find the matching generic specialization based on the concrete parameter
  // list.
  Generic* matching_generic = nullptr;
  Signature signature_with_types = MakeSignature(decl->signature.get());
  for (Generic* generic : generic_list) {
    Signature generic_signature_with_types = MakeSpecializedSignature(
        generic, GetTypeVector(decl->generic_parameters));
    if (signature_with_types.HasSameTypesAs(generic_signature_with_types,
                                            ParameterMode::kIgnoreImplicit)) {
      if (matching_generic != nullptr) {
        std::stringstream stream;
        stream << "specialization of " << decl->name
               << " is ambigous, it matches more than one generic declaration ("
               << *matching_generic << " and " << *generic << ")";
        ReportError(stream.str());
      }
      matching_generic = generic;
    }
  }

  if (matching_generic == nullptr) {
    std::stringstream stream;
    if (generic_list.size() == 0) {
      stream << "no generic defined with the name " << decl->name;
      ReportError(stream.str());
    }
    stream << "specialization of " << decl->name
           << " doesn't match any generic declaration\n";
    stream << "specialization signature:";
    stream << "\n  " << signature_with_types;
    stream << "\ncandidates are:";
    for (Generic* generic : generic_list) {
      stream << "\n  "
             << MakeSpecializedSignature(
                    generic, GetTypeVector(decl->generic_parameters));
    }
    ReportError(stream.str());
  }

  Specialize(matching_generic, GetTypeVector(decl->generic_parameters),
             matching_generic->declaration()->callable, decl->signature.get(),
             decl->body);
}

void DeclarationVisitor::Visit(ExternConstDeclaration* decl) {
  const Type* type = Declarations::GetType(decl->type);
  if (!type->IsConstexpr()) {
    std::stringstream stream;
    stream << "extern constants must have constexpr type, but found: \""
           << *type << "\"\n";
    ReportError(stream.str());
  }

  Declarations::DeclareExternConstant(decl->name, type, decl->literal);
}

void DeclarationVisitor::Visit(StructDeclaration* decl) {
  std::vector<NameAndType> fields;
  for (auto& field : decl->fields) {
    const Type* field_type = Declarations::GetType(field.type);
    fields.push_back({field.name, field_type});
  }
  Declarations::DeclareStruct(decl->name, fields);
}

void DeclarationVisitor::Visit(TypeDeclaration* decl) {
  std::string generates = decl->generates ? *decl->generates : std::string("");
  const AbstractType* type = Declarations::DeclareAbstractType(
      decl->name, decl->transient, generates, {}, decl->extends);

  if (decl->constexpr_generates) {
    if (decl->transient) {
      ReportError("cannot declare a transient type that is also constexpr");
    }
    std::string constexpr_name = CONSTEXPR_TYPE_PREFIX + decl->name;
    base::Optional<std::string> constexpr_extends;
    if (decl->extends)
      constexpr_extends = CONSTEXPR_TYPE_PREFIX + *decl->extends;
    Declarations::DeclareAbstractType(constexpr_name, false,
                                      *decl->constexpr_generates, type,
                                      constexpr_extends);
  }
}

void DeclarationVisitor::DeclareSpecializedTypes(
    Generic* generic, const TypeVector& specialized_types) {
  size_t i = 0;
  const std::size_t generic_parameter_count =
      generic->declaration()->generic_parameters.size();
  if (generic_parameter_count != specialized_types.size()) {
    std::stringstream stream;
    stream << "Wrong generic argument count for specialization of \""
           << generic->name() << "\", expected: " << generic_parameter_count
           << ", actual: " << specialized_types.size();
    ReportError(stream.str());
  }

  for (auto type : specialized_types) {
    std::string generic_type_name =
        generic->declaration()->generic_parameters[i++];
    Declarations::DeclareType(generic_type_name, type, true);
  }
}

Signature DeclarationVisitor::MakeSpecializedSignature(
    Generic* generic, const TypeVector& specialized_types) {
  CurrentScope::Scope generic_scope(generic->ParentScope());
  // Create a temporary fake-module just to temporarily declare the
  // specialization aliases for the generic types to create a signature.
  Module tmp_module("_tmp");
  CurrentScope::Scope tmp_module_scope(&tmp_module);
  DeclareSpecializedTypes(generic, specialized_types);
  return MakeSignature(generic->declaration()->callable->signature.get());
}

Callable* DeclarationVisitor::SpecializeImplicit(
    Generic* generic, const TypeVector& specialized_types) {
  if (!generic->declaration()->body) {
    ReportError("missing specialization of ", generic->name(), " with types <",
                specialized_types, "> declared at ", generic->pos());
  }
  CurrentScope::Scope generic_scope(generic->ParentScope());
  Callable* result =
      Specialize(generic, specialized_types, generic->declaration()->callable,
                 base::nullopt, *generic->declaration()->body);
  CurrentScope::Scope callable_scope(result);
  DeclareSpecializedTypes(generic, specialized_types);
  return result;
}

Callable* DeclarationVisitor::Specialize(
    Generic* generic, const TypeVector& specialized_types,
    CallableNode* declaration,
    base::Optional<const CallableNodeSignature*> signature, Statement* body) {
  // TODO(tebbi): The error should point to the source position where the
  // instantiation was requested.
  CurrentSourcePosition::Scope pos_scope(generic->declaration()->pos);
  size_t generic_parameter_count =
      generic->declaration()->generic_parameters.size();
  if (generic_parameter_count != specialized_types.size()) {
    std::stringstream stream;
    stream << "number of template parameters ("
           << std::to_string(specialized_types.size())
           << ") to intantiation of generic " << declaration->name
           << " doesnt match the generic's declaration ("
           << std::to_string(generic_parameter_count) << ")";
    ReportError(stream.str());
  }
  if (generic->GetSpecialization(specialized_types)) {
    ReportError("cannot redeclare specialization of ", generic->name(),
                " with types <", specialized_types, ">");
  }

  Signature type_signature =
      signature ? MakeSignature(*signature)
                : MakeSpecializedSignature(generic, specialized_types);

  std::string generated_name = Declarations::GetGeneratedCallableName(
      declaration->name, specialized_types);
  std::stringstream readable_name;
  readable_name << declaration->name << "<";
  bool first = true;
  for (const Type* t : specialized_types) {
    if (!first) readable_name << ", ";
    readable_name << *t;
    first = false;
  }
  readable_name << ">";
  Callable* callable;
  if (MacroDeclaration::DynamicCast(declaration) != nullptr) {
    callable = Declarations::CreateMacro(generated_name, readable_name.str(),
                                         base::nullopt, type_signature,
                                         declaration->transitioning, body);
  } else {
    BuiltinDeclaration* builtin = BuiltinDeclaration::cast(declaration);
    callable = CreateBuiltin(builtin, generated_name, readable_name.str(),
                             type_signature, body);
  }
  generic->AddSpecialization(specialized_types, callable);
  return callable;
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
