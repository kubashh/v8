// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/type-visitor.h"

#include "src/globals.h"
#include "src/torque/declarable.h"
#include "src/torque/server-data.h"

namespace v8 {
namespace internal {
namespace torque {

const Type* TypeVisitor::ComputeType(TypeDeclaration* decl) {
  CurrentSourcePosition::Scope scope(decl->pos);
  switch (decl->kind) {
#define ENUM_ITEM(name)        \
  case AstNode::Kind::k##name: \
    return ComputeType(name::cast(decl));
    AST_TYPE_DECLARATION_NODE_KIND_LIST(ENUM_ITEM)
#undef ENUM_ITEM
    default:
      UNIMPLEMENTED();
  }
}

const Type* TypeVisitor::ComputeType(TypeAliasDeclaration* decl) {
  const Type* type = ComputeType(decl->type);
  type->AddAlias(decl->name->value);
  return type;
}

const AbstractType* TypeVisitor::ComputeType(AbstractTypeDeclaration* decl) {
  std::string generates = decl->generates ? *decl->generates : std::string("");
  if (!decl->non_constexpr_version && decl->generates) {
    if (generates.length() < 7 || generates.substr(0, 6) != "TNode<" ||
        generates.substr(generates.length() - 1, 1) != ">") {
      ReportError("generated type \"", generates,
                  "\" should be of the form \"TNode<...>\"");
    }
    generates = generates.substr(6, generates.length() - 7);
  }

  const Type* parent_type = nullptr;
  if (decl->extends) {
    parent_type = Declarations::ResolveType(*decl->extends)->type();
  }

  base::Optional<const AbstractType*> non_constexpr_type;
  if (decl->non_constexpr_version) {
    const Type* type =
        Declarations::ResolveType(*decl->non_constexpr_version)->type();
    non_constexpr_type = AbstractType::cast(type);
  }

  if (generates == "" && parent_type) {
    generates = parent_type->GetGeneratedTNodeTypeName();
  }
  return TypeOracle::GetAbstractType(parent_type, decl->name->value,
                                     decl->transient, generates,
                                     non_constexpr_type);
}

const StructType* TypeVisitor::ComputeType(StructDeclaration* decl) {
  StructType* struct_type = TypeOracle::GetStructType(decl->name->value);
  AddStructFieldsAndMethods(struct_type, decl);
  return struct_type;
}

const ClassType* TypeVisitor::ComputeType(ClassDeclaration* decl) {
  ClassType* new_class;
  if (decl->is_extern) {
    if (!decl->super) {
      ReportError("Extern class must extend another type.");
    }
    // Compute the offset of the class' first member. If the class extends
    // another class, it's the size of the extended class, otherwise zero.
    const Type* super_type = Declarations::ResolveType(*decl->super)->type();
    if (super_type != TypeOracle::GetTaggedType()) {
      const ClassType* super_class = ClassType::DynamicCast(super_type);
      if (!super_class) {
        ReportError(
            "class \"", decl->name->value,
            "\" must extend either Tagged or an already declared class");
      }
    }

    // The generates clause must create a TNode<>
    std::string generates = decl->name->value;
    if (decl->generates) {
      generates = *decl->generates;
      if (generates.length() < 7 || generates.substr(0, 6) != "TNode<" ||
          generates.substr(generates.length() - 1, 1) != ">") {
        ReportError("generated type \"", generates,
                    "\" should be of the form \"TNode<...>\"");
      }
      generates = generates.substr(6, generates.length() - 7);
    }

    new_class = TypeOracle::GetClassType(super_type, decl->name->value,
                                         decl->is_extern, decl->generate_print,
                                         decl->transient, generates);
  } else {
    if (decl->super) {
      ReportError("Only extern classes can inherit.");
    }
    if (decl->generates) {
      ReportError("Only extern classes can specify a generated type.");
    }
    new_class = TypeOracle::GetClassType(
        TypeOracle::GetTaggedType(), decl->name->value, decl->is_extern,
        decl->generate_print, decl->transient, "FixedArray");
  }
  GlobalContext::RegisterClass(decl->name->value, new_class);
  return new_class;
}

const Type* TypeVisitor::ComputeType(TypeExpression* type_expression) {
  if (auto* basic = BasicTypeExpression::DynamicCast(type_expression)) {
    std::string name =
        (basic->is_constexpr ? CONSTEXPR_TYPE_PREFIX : "") + basic->name;
    const TypeAlias* alias = Declarations::LookupTypeAlias(
        QualifiedName{basic->namespace_qualification, name});
    if (GlobalContext::collect_language_server_data()) {
      LanguageServerData::AddDefinition(type_expression->pos,
                                        alias->GetDeclarationPosition());
    }
    alias->Resolve();
    return alias->type();
  } else if (auto* union_type =
                 UnionTypeExpression::DynamicCast(type_expression)) {
    return TypeOracle::GetUnionType(ComputeType(union_type->a),
                                    ComputeType(union_type->b));
  } else if (auto* reference_type =
                 ReferenceTypeExpression::DynamicCast(type_expression)) {
    return TypeOracle::GetReferenceType(
        ComputeType(reference_type->referenced_type));
  } else {
    auto* function_type_exp = FunctionTypeExpression::cast(type_expression);
    TypeVector argument_types;
    for (TypeExpression* type_exp : function_type_exp->parameters) {
      argument_types.push_back(ComputeType(type_exp));
    }
    return TypeOracle::GetBuiltinPointerType(
        argument_types, ComputeType(function_type_exp->return_type));
  }
}

Signature TypeVisitor::MakeSignature(const CallableNodeSignature* signature) {
  LabelDeclarationVector definition_vector;
  for (const auto& label : signature->labels) {
    LabelDeclaration def = {label.name, ComputeTypeVector(label.types)};
    definition_vector.push_back(def);
  }
  base::Optional<std::string> arguments_variable;
  if (signature->parameters.has_varargs)
    arguments_variable = signature->parameters.arguments_variable;
  Signature result{signature->parameters.names,
                   arguments_variable,
                   {ComputeTypeVector(signature->parameters.types),
                    signature->parameters.has_varargs},
                   signature->parameters.implicit_count,
                   ComputeType(signature->return_type),
                   definition_vector};
  return result;
}

void TypeVisitor::DeclareMethods(AggregateType* container_type,
                                 const std::vector<Declaration*>& methods) {
  // Declare the class' methods
  for (auto declaration : methods) {
    CurrentSourcePosition::Scope pos_scope(declaration->pos);
    StandardDeclaration* standard_declaration =
        StandardDeclaration::DynamicCast(declaration);
    DCHECK(standard_declaration);
    TorqueMacroDeclaration* method =
        TorqueMacroDeclaration::DynamicCast(standard_declaration->callable);
    Signature signature = MakeSignature(method->signature.get());
    signature.parameter_names.insert(
        signature.parameter_names.begin() + signature.implicit_count,
        MakeNode<Identifier>(kThisParameterName));
    Statement* body = *(standard_declaration->body);
    std::string method_name(method->name);
    signature.parameter_types.types.insert(
        signature.parameter_types.types.begin() + signature.implicit_count,
        container_type);
    Declarations::CreateMethod(container_type, method_name, signature, false,
                               body);
  }
}

void TypeVisitor::AddStructFieldsAndMethods(
    StructType* struct_type, StructDeclaration* struct_declaration) {
  size_t offset = 0;
  for (auto& field : struct_declaration->fields) {
    CurrentSourcePosition::Scope position_activator(
        field.name_and_type.type->pos);
    const Type* field_type = ComputeType(field.name_and_type.type);
    struct_type->RegisterField({field.name_and_type.name->pos,
                                struct_type,
                                base::nullopt,
                                {field.name_and_type.name->value, field_type},
                                offset,
                                false,
                                field.const_qualified});
    offset += LoweredSlotCount(field_type);
  }
  CurrentSourcePosition::Scope position_activator(struct_declaration->pos);
  DeclareMethods(struct_type, struct_declaration->methods);
}

void TypeVisitor::FinalizeClassFieldsAndMethods(
    ClassType* class_type, ClassDeclaration* class_declaration) {
  const ClassType* super_class = class_type->GetSuperClass();
  size_t class_offset = super_class ? super_class->size() : 0;
  bool seen_indexed_field = false;
  for (ClassFieldExpression& field_expression : class_declaration->fields) {
    CurrentSourcePosition::Scope position_activator(
        field_expression.name_and_type.type->pos);
    const Type* field_type = ComputeType(field_expression.name_and_type.type);
    if (!class_declaration->is_extern) {
      if (!field_type->IsSubtypeOf(TypeOracle::GetTaggedType())) {
        ReportError("non-extern classes do not support untagged fields");
      }
      if (field_expression.weak) {
        ReportError("non-extern classes do not support weak fields");
      }
    }
    if (field_expression.index) {
      if (seen_indexed_field ||
          (super_class && super_class->HasIndexedField())) {
        ReportError(
            "only one indexable field is currently supported per class");
      }
      seen_indexed_field = true;
      const Field* index_field =
          &(class_type->LookupField(*field_expression.index));
      class_type->RegisterField(
          {field_expression.name_and_type.name->pos,
           class_type,
           index_field,
           {field_expression.name_and_type.name->value, field_type},
           class_offset,
           field_expression.weak,
           field_expression.const_qualified});
    } else {
      if (seen_indexed_field) {
        ReportError("cannot declare non-indexable field \"",
                    field_expression.name_and_type.name,
                    "\" after an indexable field "
                    "declaration");
      }
      const Field& field = class_type->RegisterField(
          {field_expression.name_and_type.name->pos,
           class_type,
           base::nullopt,
           {field_expression.name_and_type.name->value, field_type},
           class_offset,
           field_expression.weak,
           field_expression.const_qualified});
      size_t field_size;
      std::string size_string;
      std::string machine_type;
      std::tie(field_size, size_string, machine_type) =
          field.GetFieldSizeInformation();
      // Our allocations don't support alignments beyond kTaggedSize.
      size_t alignment = std::min(size_t{kTaggedSize}, field_size);
      if (class_offset % alignment != 0) {
        ReportError("field ", field_expression.name_and_type.name,
                    " at offset ", class_offset, " is not ", alignment,
                    "-byte aligned.");
      }
      class_offset += field_size;
    }
  }
  class_type->SetSize(class_offset);

  // For each field, construct AST snippets that implement a CSA accessor
  // function and define a corresponding '.field' operator. The
  // implementation iterator will turn the snippets into code.
  for (auto& field : class_type->fields()) {
    if (field.index) continue;
    CurrentSourcePosition::Scope position_activator(field.pos);
    IdentifierExpression* parameter =
        MakeNode<IdentifierExpression>(MakeNode<Identifier>(std::string{"o"}));

    // Load accessor
    std::string camel_field_name = CamelifyString(field.name_and_type.name);
    std::string load_macro_name =
        "Load" + class_type->name() + camel_field_name;
    Signature load_signature;
    load_signature.parameter_names.push_back(MakeNode<Identifier>("o"));
    load_signature.parameter_types.types.push_back(class_type);
    load_signature.parameter_types.var_args = false;
    load_signature.return_type = field.name_and_type.type;
    Statement* load_body =
        MakeNode<ReturnStatement>(MakeNode<FieldAccessExpression>(
            parameter, MakeNode<Identifier>(field.name_and_type.name)));
    Declarations::DeclareMacro(load_macro_name, base::nullopt, load_signature,
                               false, load_body);

    // Store accessor
    IdentifierExpression* value = MakeNode<IdentifierExpression>(
        std::vector<std::string>{}, MakeNode<Identifier>(std::string{"v"}));
    std::string store_macro_name =
        "Store" + class_type->name() + camel_field_name;
    Signature store_signature;
    store_signature.parameter_names.push_back(MakeNode<Identifier>("o"));
    store_signature.parameter_names.push_back(MakeNode<Identifier>("v"));
    store_signature.parameter_types.types.push_back(class_type);
    store_signature.parameter_types.types.push_back(field.name_and_type.type);
    store_signature.parameter_types.var_args = false;
    // TODO(danno): Store macros probably should return their value argument
    store_signature.return_type = TypeOracle::GetVoidType();
    Statement* store_body =
        MakeNode<ExpressionStatement>(MakeNode<AssignmentExpression>(
            MakeNode<FieldAccessExpression>(
                parameter, MakeNode<Identifier>(field.name_and_type.name)),
            value));
    Declarations::DeclareMacro(store_macro_name, base::nullopt, store_signature,
                               false, store_body);
  }

  DeclareMethods(class_type, class_declaration->methods);
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
