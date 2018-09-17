// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/string-constants.h"

#include "src/base/functional.h"
#include "src/dtoa.h"
#include "src/objects.h"
#include "src/objects/string-inl.h"

namespace v8 {
namespace internal {

Handle<String> StringConstantBase::AllocateStringConstant(
    Isolate* isolate) const {
  if (!flattened_.is_null()) {
    return flattened_;
  }

  Handle<String> result;
  switch (kind()) {
    case StringConstantKind::kStringLiteral: {
      result = static_cast<const StringLiteral*>(this)->str();
      break;
    }
    case StringConstantKind::kNumberToStringConstant: {
      auto num_constant = static_cast<const NumberToStringConstant*>(this);
      Handle<Object> num_obj =
          isolate->factory()->NewNumber(num_constant->num());
      result = isolate->factory()->NumberToString(num_obj);
      break;
    }
    case StringConstantKind::kStringCons: {
      Handle<String> lhs =
          static_cast<const StringCons*>(this)->lhs()->AllocateStringConstant(
              isolate);
      Handle<String> rhs =
          static_cast<const StringCons*>(this)->rhs()->AllocateStringConstant(
              isolate);
      result = isolate->factory()->NewConsString(lhs, rhs).ToHandleChecked();
      break;
    }
    default: { UNREACHABLE(); }
  }

  Memoize(String::Flatten(isolate, result));
  return flattened_;
}

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
  // TODO(mslekova): Think if we can express this in a more readable manner
  return lhs.lhs() == rhs.lhs() && lhs.rhs() == rhs.rhs();
}

bool operator!=(StringCons const& lhs, StringCons const& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(StringCons const& p) {
  return base::hash_combine(p.lhs(), p.rhs());
}

std::ostream& operator<<(std::ostream& os, const StringConstantBase* base) {
  os << "DelayedStringConstant: ";
  switch (base->kind()) {
    case StringConstantKind::kStringLiteral: {
      os << *static_cast<const StringLiteral*>(base);
      break;
    }
    case StringConstantKind::kNumberToStringConstant: {
      os << *static_cast<const NumberToStringConstant*>(base);
      break;
    }
    case StringConstantKind::kStringCons: {
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
    }
    case StringConstantKind::kNumberToStringConstant: {
      return GetMaxStringConstantLength(
          *static_cast<const NumberToStringConstant*>(base));
    }
    case StringConstantKind::kStringCons: {
      return GetMaxStringConstantLength(*static_cast<const StringCons*>(base));
    }
  }
  UNREACHABLE();
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

}  // namespace internal
}  // namespace v8
