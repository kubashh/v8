// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_STRING_CONSTANTS_H_
#define V8_STRING_CONSTANTS_H_

#include "src/objects/string.h"

namespace v8 {
namespace internal {

enum class StringConstantKind {
  kStringLiteral,
  kNumberToStringConstant,
  kStringCons
};

class StringConstantBase {
 public:
  explicit StringConstantBase(StringConstantKind kind) : kind_(kind) {}

  virtual StringConstantKind kind() const { return kind_; }

 private:
  StringConstantKind kind_;
};

class StringLiteral final : public StringConstantBase {
 public:
  explicit StringLiteral(Handle<String> str)
      : StringConstantBase(StringConstantKind::kStringLiteral), str_(str) {}

  Handle<String> str() const { return str_; }

 private:
  Handle<String> str_;
};

bool operator==(StringLiteral const& lhs, StringLiteral const& rhs);
bool operator!=(StringLiteral const& lhs, StringLiteral const& rhs);

size_t hash_value(StringLiteral const& parameters);

std::ostream& operator<<(std::ostream& os, StringLiteral const& parameters);

class NumberToStringConstant final : public StringConstantBase {
 public:
  explicit NumberToStringConstant(double num)
      : StringConstantBase(StringConstantKind::kNumberToStringConstant),
        num_(num) {}

  double num() const { return num_; }

 private:
  double num_;
};

bool operator==(NumberToStringConstant const& lhs,
                NumberToStringConstant const& rhs);
bool operator!=(NumberToStringConstant const& lhs,
                NumberToStringConstant const& rhs);

size_t hash_value(NumberToStringConstant const& parameters);

std::ostream& operator<<(std::ostream& os,
                         NumberToStringConstant const& parameters);

class StringCons final : public StringConstantBase {
 public:
  explicit StringCons(const StringConstantBase* lhs,
                      const StringConstantBase* rhs)
      : StringConstantBase(StringConstantKind::kStringCons),
        lhs_(lhs),
        rhs_(rhs) {}

  const StringConstantBase* lhs() const { return lhs_; }
  const StringConstantBase* rhs() const { return rhs_; }

 private:
  const StringConstantBase* lhs_;
  const StringConstantBase* rhs_;
};

bool operator==(StringCons const& lhs, StringCons const& rhs);
bool operator!=(StringCons const& lhs, StringCons const& rhs);

size_t hash_value(StringCons const& parameters);

std::ostream& operator<<(std::ostream& os, StringCons const& parameters);

size_t GetMaxStringConstantLength(const StringConstantBase* base);
// TODO(mslekova): Possibly move as methods to the resp. classes
size_t GetMaxStringConstantLength(const StringLiteral& str_constant);
size_t GetMaxStringConstantLength(const NumberToStringConstant& str_constant);
size_t GetMaxStringConstantLength(const StringCons& str_constant);

Handle<String> AllocateStringConstant(const StringConstantBase* base,
                                      Isolate* isolate);
}  // namespace internal
}  // namespace v8

#endif  // V8_STRING_CONSTANTS_H_
