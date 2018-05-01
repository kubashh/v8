// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/file-visitor.h"

#include "src/torque/declarable.h"

namespace v8 {
namespace internal {
namespace torque {

Signature FileVisitor::MakeSignature(SourcePosition pos,
                                     const ParameterList& parameters,
                                     const std::string& return_type,
                                     const LabelAndTypesVector& labels) {
  LabelDeclarationVector definition_vector;
  for (auto label : labels) {
    LabelDeclaration def = {label.name, GetTypeVector(pos, label.types)};
    definition_vector.push_back(def);
  }
  Signature result{
      parameters.names,
      {GetTypeVector(pos, parameters.types), parameters.has_varargs},
      LookupType(pos, return_type),
      definition_vector};
  return result;
}

Type FileVisitor::LookupType(SourcePosition pos, const std::string& name) {
  Declarable* raw = declarations()->Lookup(name);
  if (raw == nullptr) {
    std::stringstream s;
    s << "definition of type \"" << name << "\" not found at "
      << PositionAsString(pos);
    ReportError(s.str());
  }
  if (!raw->IsTypeImpl()) {
    std::stringstream s;
    s << "\"" << name << "\" is not a type at " << PositionAsString(pos);
    ReportError(s.str());
  }
  return Type(TypeImpl::cast(raw));
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
