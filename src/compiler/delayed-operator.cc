// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/delayed-operator.h"

#include "src/base/functional.h"
#include "src/dtoa.h"
#include "src/objects.h"
#include "src/objects/string-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

StringConstantBase::~StringConstantBase() {}

bool operator==(StringLiteral const& lhs, StringLiteral const& rhs) {
  return lhs.str().location() == rhs.str().location();
}

bool operator!=(StringLiteral const& lhs, StringLiteral const& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(StringLiteral const& p) {
  return base::hash_combine(*p.str());
}

std::ostream& operator<<(std::ostream& os, StringLiteral const& p) {
  return os << Brief(*p.str());
}

bool operator==(NumberToStringConstant const& lhs,
                NumberToStringConstant const& rhs) {
  return lhs.num() == rhs.num();
}

bool operator!=(NumberToStringConstant const& lhs,
                NumberToStringConstant const& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(NumberToStringConstant const& p) {
  return base::hash_combine(p.num());
}

std::ostream& operator<<(std::ostream& os, NumberToStringConstant const& p) {
  return os << p.num();
}

bool operator==(StringCons const& lhs, StringCons const& rhs) {
  // TODO(mslekova): fix this or die trying
  return lhs.lhs() == rhs.lhs() && lhs.rhs() == rhs.rhs();
}

bool operator!=(StringCons const& lhs, StringCons const& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(StringCons const& p) {
  return base::hash_combine(p.lhs(), p.rhs());
}

std::ostream& operator<<(std::ostream& os, const StringConstantBase* base) {
  switch (base->kind()) {
    case StringConstantKind::kStringLiteral: {
      os << "kStringLiteral: ";
      os << *static_cast<const StringLiteral*>(base);
      break;
    }
    case StringConstantKind::kNumberToStringConstant: {
      os << "kNumberToStringConstant: ";
      os << *static_cast<const NumberToStringConstant*>(base);
      break;
    }
    case StringConstantKind::kStringCons: {
      os << "kStringCons: ";
      os << *static_cast<const StringCons*>(base);
      break;
    }
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, StringCons const& p) {
  return os << p.lhs() << ", " << p.rhs();
}

size_t GetMaxStringConstantLength(const StringConstantBase* base) {
  switch (base->kind()) {
    case StringConstantKind::kStringLiteral: {
      return GetMaxStringConstantLength(
          *static_cast<const StringLiteral*>(base));
      break;
    }
    case StringConstantKind::kNumberToStringConstant: {
      return GetMaxStringConstantLength(
          *static_cast<const NumberToStringConstant*>(base));
      break;
    }
    case StringConstantKind::kStringCons: {
      return GetMaxStringConstantLength(*static_cast<const StringCons*>(base));
      break;
    }
  }
}

size_t GetMaxStringConstantLength(const StringLiteral& str_constant) {
  return str_constant.str()->length();
}

size_t GetMaxStringConstantLength(const NumberToStringConstant& str_constant) {
  return kBase10MaximalLength + 1;
}

size_t GetMaxStringConstantLength(const StringCons& str_constant) {
  return GetMaxStringConstantLength(str_constant.lhs()) +
         GetMaxStringConstantLength(str_constant.rhs());
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
