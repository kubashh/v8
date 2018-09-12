// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_DELAYED_OPERATOR_H_
#define V8_COMPILER_DELAYED_OPERATOR_H_

#include "src/objects/string.h"

namespace v8 {
namespace internal {
namespace compiler {

enum class StringConstantKind {
  kStringLiteral,
  kNumberToStringConstant,
  kStringCons
};

class StringConstantBase {
 public:
  explicit StringConstantBase(StringConstantKind kind) : kind_(kind) {}
  virtual ~StringConstantBase() = 0;

  StringConstantKind kind() const { return kind_; }

 private:
  StringConstantKind kind_;
};

class StringLiteral final : public StringConstantBase {
 public:
  explicit StringLiteral(Handle<String> str)
      : StringConstantBase(StringConstantKind::kStringLiteral), str_(str) {}
  virtual ~StringLiteral() {}

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
  virtual ~NumberToStringConstant() {}

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
  virtual ~StringCons() {}

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

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_DELAYED_OPERATOR_H_
