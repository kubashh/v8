// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_EXPRESSION_SCOPE_H_
#define V8_PARSING_EXPRESSION_SCOPE_H_

#include "src/message-template.h"
#include "src/parsing/scanner.h"

namespace v8 {
namespace internal {

template <typename Types>
class ExpressionParsingScope;
template <typename Types>
class AccumulationScope;
template <typename Types>
class ArrowHeadParsingScope;

// ExpressionScope is used in a stack fashion, and is used to specialize
// expression parsing for the task at hand. It allows the parser to reuse the
// same code to parse destructuring declarations, assignment patterns,
// expressions, and (async) arrow function heads.
//
// One of the specific subclasses needs to be instantiated to tell the parser
// the meaning of the expression it will parse next. The parser then calls
// Record* on the expression_scope() to indicate errors. The expression_scope
// can then either discard those errors, immediately report those errors, or
// classify the errors for later validation.
template <typename Types>
class ExpressionScope {
 public:
  typedef typename Types::Impl ParserT;
  typedef typename Types::Expression ExpressionT;

  enum ScopeType : uint8_t {
    // Expression or assignment target.
    kExpression,

    // Declaration or expression or assignment target.
    kArrowParameterDeclaration,
    kAsyncArrowParameterDeclaration,

    // Declarations.
    kParameterDeclaration,
    kVarDeclaration,
    kLexicalDeclaration,
  };

  void ClassifyPattern(ExpressionT expression, int begin, int end) {
    if (!CanBeExpression()) return;
    AsExpressionParsingScope()->ValidatePattern(expression, begin, end);
    AsExpressionParsingScope()->ClearExpressionError();
  }

  void RecordAsyncArrowParametersError(const Scanner::Location& loc,
                                       MessageTemplate message) {
    for (ExpressionScope* scope = this; scope != nullptr;
         scope = scope->parent_) {
      if (!has_async_arrow_in_scope_chain_) break;
      if (scope->type_ == kAsyncArrowParameterDeclaration) {
        scope->AsArrowHeadParsingScope()->RecordDeclarationError(loc, message);
      }
    }
  }

  void RecordParameterInitializerError(const Scanner::Location& loc,
                                       MessageTemplate message) {
    for (ExpressionScope* scope = this; scope != nullptr;
         scope = scope->parent_) {
      if (!has_possible_formal_in_scope_chain_) break;
      if (scope->IsCertainlyParameterDeclaration()) {
        Report(loc, message);
      } else if (scope->CanBeParameterDeclaration()) {
        scope->AsArrowHeadParsingScope()->RecordDeclarationError(loc, message);
      }
    }
  }

  void RecordPatternError(const Scanner::Location& loc,
                          MessageTemplate message) {
    // TODO(verwaest): Non-assigning expression?
    if (IsCertainlyPattern()) {
      Report(loc, message);
    } else {
      AsExpressionParsingScope()->RecordPatternError(loc, message);
    }
  }

  void RecordStrictModeParameterError(const Scanner::Location& loc,
                                      MessageTemplate message) {
    DCHECK_IMPLIES(!has_error(), loc.IsValid());
    if (!CanBeParameterDeclaration()) return;
    if (IsCertainlyParameterDeclaration()) {
      if (is_strict(parser_->language_mode())) {
        Report(loc, message);
      } else if (is_simple_parameter_list()) {
        parser_->parameters_->set_strict_parameter_error(loc, message);
      }
    } else if (is_simple_parameter_list()) {
      parser_->next_arrow_function_info_.strict_formal_error_location = loc;
      parser_->next_arrow_function_info_.strict_formal_error_message = message;
    }
  }

  void RecordDeclarationError(const Scanner::Location& loc,
                              MessageTemplate message) {
    if (!CanBeDeclaration()) return;
    if (IsCertainlyDeclaration()) {
      Report(loc, message);
    } else {
      AsArrowHeadParsingScope()->RecordDeclarationError(loc, message);
    }
  }

  void RecordExpressionError(const Scanner::Location& loc,
                             MessageTemplate message) {
    if (!CanBeExpression()) return;
    // TODO(verwaest): Non-assigning expression?
    // if (IsCertainlyExpression()) Report(loc, message);
    AsExpressionParsingScope()->RecordExpressionError(loc, message);
  }

  void RecordLexicalDeclarationError(const Scanner::Location& loc,
                                     MessageTemplate message) {
    if (IsLexicalDeclaration()) Report(loc, message);
  }

  V8_INLINE bool is_simple_parameter_list() const {
    DCHECK(CanBeParameterDeclaration());
    return is_simple_parameter_list_;
  }

  void RecordNonSimpleParameter() {
    DCHECK(CanBeParameterDeclaration());
    is_simple_parameter_list_ = false;
  }

 protected:
  ParserT* parser() const { return parser_; }

  void Report(const Scanner::Location& loc, MessageTemplate message) const {
    parser_->ReportMessageAt(loc, message);
  }

  ExpressionScope(ParserT* parser, ScopeType type)
      : parser_(parser),
        parent_(parser->expression_scope_),
        type_(type),
        has_async_arrow_in_scope_chain_(
            type == kAsyncArrowParameterDeclaration ||
            (parent_ && parent_->has_async_arrow_in_scope_chain_)),
        has_possible_formal_in_scope_chain_(
            CanBeParameterDeclaration() ||
            (parent_ && parent_->has_possible_formal_in_scope_chain_)) {
    parser->expression_scope_ = this;
  }

  ~ExpressionScope() {
    DCHECK(parser_->expression_scope_ == this ||
           parser_->expression_scope_ == parent_);
    parser_->expression_scope_ = parent_;
  }

#ifdef DEBUG
  bool has_error() const { return parser_->has_error(); }
#endif

  bool CanBeExpression() const {
    return IsInRange(type_, kExpression, kAsyncArrowParameterDeclaration);
  }
  bool CanBeDeclaration() const {
    return IsInRange(type_, kArrowParameterDeclaration, kLexicalDeclaration);
  }
  bool IsCertainlyDeclaration() const {
    return IsInRange(type_, kParameterDeclaration, kLexicalDeclaration);
  }

 private:
  friend class AccumulationScope<Types>;

  ExpressionParsingScope<Types>* AsExpressionParsingScope() {
    DCHECK(CanBeExpression());
    return static_cast<ExpressionParsingScope<Types>*>(this);
  }

  ArrowHeadParsingScope<Types>* AsArrowHeadParsingScope() {
    DCHECK(IsInRange(type_, kArrowParameterDeclaration,
                     kAsyncArrowParameterDeclaration));
    return static_cast<ArrowHeadParsingScope<Types>*>(this);
  }

  bool IsCertainlyPattern() const { return IsCertainlyDeclaration(); }
  bool CanBeParameterDeclaration() const {
    return IsInRange(type_, kArrowParameterDeclaration, kParameterDeclaration);
  }
  bool IsCertainlyParameterDeclaration() const {
    return type_ == kParameterDeclaration;
  }
  bool IsLexicalDeclaration() const { return type_ == kLexicalDeclaration; }

  ParserT* parser_;
  ExpressionScope<Types>* parent_;
  ScopeType type_;
  bool has_async_arrow_in_scope_chain_;
  bool has_possible_formal_in_scope_chain_;
  bool is_simple_parameter_list_ = true;
};

// Used to parse var, let, const declarations and declarations known up-front to
// be parameters.
template <typename Types>
class DeclarationParsingScope : public ExpressionScope<Types> {
 public:
  typedef typename Types::Impl ParserT;
  typedef typename ExpressionScope<Types>::ScopeType ScopeType;

  DeclarationParsingScope(ParserT* parser, ScopeType type)
      : ExpressionScope<Types>(parser, type) {
    DCHECK(this->IsCertainlyDeclaration());
  }
};

// Parsing expressions is always ambiguous between at least left-hand-side and
// right-hand-side of assignments. This class is used to keep track of errors
// relevant for either side until it is clear what was being parsed.
template <typename Types>
class ExpressionParsingScope : public ExpressionScope<Types> {
 public:
  typedef typename Types::Impl ParserT;
  typedef typename ExpressionScope<Types>::ScopeType ScopeType;
  typedef typename Types::Expression ExpressionT;

  ExpressionParsingScope(ParserT* parser,
                         ScopeType type = ExpressionScope<Types>::kExpression)
      : ExpressionScope<Types>(parser, type) {
    DCHECK(this->CanBeExpression());
    clear(kExpressionIndex);
    clear(kPatternIndex);
  }

  ~ExpressionParsingScope() { DCHECK(this->has_error() || verified_); }

  ExpressionT ValidateAndRewriteReference(ExpressionT expression, int beg_pos,
                                          int end_pos) {
    if (V8_LIKELY(this->parser()->IsAssignableIdentifier(expression))) {
      this->mark_verified();
      return expression;
    } else if (V8_LIKELY(expression->IsProperty())) {
      ValidateExpression();
      return expression;
    }
    this->mark_verified();
    return this->parser()->RewriteInvalidReferenceExpression(
        expression, beg_pos, end_pos, MessageTemplate::kInvalidLhsInFor,
        kSyntaxError);
  }

  void RecordExpressionError(const Scanner::Location& loc,
                             MessageTemplate message) {
    Record(kExpressionIndex, loc, message);
  }

  void RecordPatternError(const Scanner::Location& loc,
                          MessageTemplate message) {
    Record(kPatternIndex, loc, message);
  }

  void ValidateExpression() { Validate(kExpressionIndex); }

  void ValidatePattern(ExpressionT expression, int begin, int end) {
    Validate(kPatternIndex);
    if (expression->is_parenthesized()) {
      ExpressionScope<Types>::Report(
          Scanner::Location(begin, end),
          MessageTemplate::kInvalidDestructuringTarget);
    }
  }

  void ClearExpressionError() {
    DCHECK(verified_);
#ifdef DEBUG
    verified_ = false;
#endif
    clear(kExpressionIndex);
  }

 protected:
  bool is_verified() const {
#ifdef DEBUG
    return verified_;
#else
    return false;
#endif
  }

  void ValidatePattern() { Validate(kPatternIndex); }

 private:
  friend class AccumulationScope<Types>;

  enum ErrorNumber : uint8_t {
    kExpressionIndex = 0,
    kPatternIndex = 1,
    kNumberOfErrors = 2,
  };
  void clear(int index) {
    messages_[index] = MessageTemplate::kNone;
    locations_[index] = Scanner::Location::invalid();
  }
  bool is_valid(int index) const { return !locations_[index].IsValid(); }
  void Record(int index, const Scanner::Location& loc,
              MessageTemplate message) {
    DCHECK_IMPLIES(!this->has_error(), loc.IsValid());
    if (!is_valid(index)) return;
    messages_[index] = message;
    locations_[index] = loc;
  }
  void Validate(int index) {
    DCHECK(!this->is_verified());
    if (!is_valid(index)) Report(index);
    this->mark_verified();
  }
  void Report(int index) const {
    ExpressionScope<Types>::Report(locations_[index], messages_[index]);
  }

  // Debug verification to make sure every scope is validated exactly once.
  void mark_verified() {
#ifdef DEBUG
    verified_ = true;
#endif
  }
  void clear_verified() {
#ifdef DEBUG
    verified_ = false;
#endif
  }
#ifdef DEBUG
  bool verified_ = false;
#endif

  MessageTemplate messages_[kNumberOfErrors];
  Scanner::Location locations_[kNumberOfErrors];
};

// This class is used to parse multiple ambiguous expressions and declarations
// in the same scope. It provides a clean error state in the underlying scope to
// parse the individual expressions, while keeping track of the expression and
// pattern errors since the start. The AccumulationScope is only used to keep
// track of the errors so far, and the underlying ExpressionScope keeps being
// used as the expression_scope(). If the expression_scope() isn't ambiguous,
// this class does not do anything.
template <typename Types>
class AccumulationScope {
 public:
  typedef typename Types::Impl ParserT;
  static const int kNumberOfErrors =
      ExpressionParsingScope<Types>::kNumberOfErrors;
  explicit AccumulationScope(ExpressionScope<Types>* scope) : scope_(nullptr) {
    if (!scope->CanBeExpression()) return;
    scope_ = scope->AsExpressionParsingScope();
    for (int i = 0; i < kNumberOfErrors; i++) {
      copy(i);
      scope_->clear(i);
    }
  }

  void ValidateExpression() {
    if (scope_ == nullptr) return;
    DCHECK(!scope_->is_verified());
    scope_->ValidateExpression();
    DCHECK(scope_->is_verified());
    scope_->clear(ExpressionParsingScope<Types>::kPatternIndex);
#ifdef DEBUG
    scope_->clear_verified();
#endif
  }

  void Accumulate() {
    if (scope_ == nullptr) return;
    DCHECK(!scope_->is_verified());
    for (int i = 0; i < kNumberOfErrors; i++) {
      if (!locations_[i].IsValid()) copy(i);
      scope_->clear(i);
    }
  }

  ~AccumulationScope() {
    if (scope_ == nullptr) return;
    for (int i = 0; i < kNumberOfErrors; i++) copy_back(i);
  }

 private:
  void copy(int entry) {
    messages_[entry] = scope_->messages_[entry];
    locations_[entry] = scope_->locations_[entry];
  }

  void copy_back(int entry) {
    if (!locations_[entry].IsValid()) return;
    scope_->messages_[entry] = messages_[entry];
    scope_->locations_[entry] = locations_[entry];
  }

  ExpressionParsingScope<Types>* scope_;
  MessageTemplate messages_[2];
  Scanner::Location locations_[2];
};

// The head of an arrow function is ambiguous between expression, assignment
// pattern and declaration. This keeps track of the additional declaration error
// and allows the scope to be validated as a declaration rather than an
// expression or a pattern.
template <typename Types>
class ArrowHeadParsingScope : public ExpressionParsingScope<Types> {
 public:
  typedef typename Types::Impl ParserT;
  typedef typename ExpressionScope<Types>::ScopeType ScopeType;

  ArrowHeadParsingScope(ParserT* parser, ScopeType type)
      : ExpressionParsingScope<Types>(parser, type) {
    DCHECK(this->CanBeDeclaration());
    DCHECK(!this->IsCertainlyDeclaration());
  }

  void ValidateDeclaration() {
    DCHECK(!this->is_verified());
    if (declaration_error_location.IsValid()) {
      ExpressionScope<Types>::Report(declaration_error_location,
                                     declaration_error_message);
    }
    this->ValidatePattern();
  }

  void RecordDeclarationError(const Scanner::Location& loc,
                              MessageTemplate message) {
    DCHECK_IMPLIES(!this->has_error(), loc.IsValid());
    declaration_error_location = loc;
    declaration_error_message = message;
  }

 private:
  Scanner::Location declaration_error_location = Scanner::Location::invalid();
  MessageTemplate declaration_error_message = MessageTemplate::kNone;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_EXPRESSION_SCOPE_H_
