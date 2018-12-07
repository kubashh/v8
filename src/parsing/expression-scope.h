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
class ExpressionScope {
 public:
  typedef typename Types::Impl Parser;
  typedef typename Types::Expression ExpressionT;

  enum ScopeType : uint8_t {
    // Expression or assignment target.
    kExpression,
    kExpressionOrReference,

    // Declaration or expression or assignment target.
    kArrowFormalDeclaration,
    kAsyncArrowFormalDeclaration,

    // Declarations.
    kFormalDeclaration,
    kVarDeclaration,
    kLexicalDeclaration,
  };

  ExpressionScope(Parser* parser, ScopeType type)
      : parser_(parser),
        parent_(parser->expression_scope_),
        type_(type),
        has_async_arrow_in_scope_chain_(
            type == kAsyncArrowFormalDeclaration ||
            (parent_ && parent_->has_async_arrow_in_scope_chain_)),
        has_possible_formal_in_scope_chain_(
            CanBeFormalDeclaration() ||
            (parent_ && parent_->has_possible_formal_in_scope_chain_)) {
    Clear();
    parser->expression_scope_ = this;
  }

  ~ExpressionScope() {
    DCHECK(parser_->expression_scope_ == this ||
           parser_->expression_scope_ == parent_);
    DCHECK(parser_->has_error() || verified_);
    parser_->expression_scope_ = parent_;
  }

  class AccumulationScope {
   public:
    explicit AccumulationScope(Parser* parser)
        : scope_(parser->expression_scope()) {
      clear(0);
      clear(1);
      Accumulate();
    }

    void ValidateExpression() {
      DCHECK(!scope_->verified_);
      scope_->ValidateExpression();
      DCHECK(scope_->verified_);
      scope_->clear(kPatternIndex);
#ifdef DEBUG
      scope_->verified_ = false;
#endif
    }

    void Accumulate() {
      DCHECK(!scope_->verified_);
      accumulate(0, kPatternIndex);
      accumulate(1, kExpressionIndex);
    }

    ~AccumulationScope() {
      copy_back(kPatternIndex, 0);
      copy_back(kExpressionIndex, 1);
    }

   private:
    void clear(int index) {
      locations_[index] = Scanner::Location::invalid();
      messages_[index] = MessageTemplate::kNone;
    }

    void accumulate(int target, int source) {
      if (!locations_[target].IsValid()) {
        messages_[target] = scope_->messages_[source];
        locations_[target] = scope_->locations_[source];
      }
      scope_->messages_[source] = MessageTemplate::kNone;
      scope_->locations_[source] = Scanner::Location::invalid();
    }

    void copy_back(int target, int source) {
      if (!locations_[source].IsValid()) return;
      scope_->messages_[target] = messages_[source];
      scope_->locations_[target] = locations_[source];
    }

    ExpressionScope* scope_;
    MessageTemplate messages_[2];
    Scanner::Location locations_[2];
  };

  bool CanBeAssignment() const {
    return IsInRange(type_, kExpression, kAsyncArrowFormalDeclaration);
  }
  bool CanBeExpression() const { return CanBeAssignment(); }
  bool CanBeDeclaration() const {
    return IsInRange(type_, kArrowFormalDeclaration, kLexicalDeclaration);
  }
  bool IsCertainlyDeclaration() const {
    return IsInRange(type_, kFormalDeclaration, kLexicalDeclaration);
  }
  bool IsCertainlyPattern() const { return IsCertainlyDeclaration(); }
  bool CanBeFormalDeclaration() const {
    return IsInRange(type_, kArrowFormalDeclaration, kFormalDeclaration);
  }
  bool IsCertainlyFormalDeclaration() const {
    return type_ == kFormalDeclaration;
  }
  bool IsLexicalDeclaration() const { return type_ == kLexicalDeclaration; }

  void RecordAsyncArrowFormalParametersError(const Scanner::Location& loc,
                                             MessageTemplate message) {
    for (ExpressionScope* scope = this; scope != nullptr;
         scope = scope->parent_) {
      if (!has_async_arrow_in_scope_chain_) break;
      if (scope->type_ == kAsyncArrowFormalDeclaration) {
        scope->Record(kDeclarationIndex, loc, message);
      }
    }
  }

  void RecordFormalParameterInitializerError(const Scanner::Location& loc,
                                             MessageTemplate message) {
    for (ExpressionScope* scope = this; scope != nullptr;
         scope = scope->parent_) {
      if (!has_possible_formal_in_scope_chain_) break;
      if (scope->CanBeFormalDeclaration()) {
        if (scope->IsCertainlyFormalDeclaration()) {
          parser_->ReportMessageAt(loc, message);
        } else {
          scope->Record(kDeclarationIndex, loc, message);
        }
      }
    }
  }

  void RecordPatternError(const Scanner::Location& loc,
                          MessageTemplate message) {
    // TODO(verwaest): Non-assigning expression?
    // if (!CanBeAssignmentOrDeclaration()) return;
    // Record.
    if (IsCertainlyPattern()) {
      parser_->ReportMessageAt(loc, message);
    } else {
      Record(kPatternIndex, loc, message);
    }
  }

  void RecordStrictModeFormalParameterError(const Scanner::Location& loc,
                                            MessageTemplate message) {
    DCHECK(loc.IsValid());
    if (!CanBeFormalDeclaration()) return;
    if (IsCertainlyFormalDeclaration()) {
      if (is_strict(parser_->language_mode())) {
        parser_->ReportMessageAt(loc, message);
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
      parser_->ReportMessageAt(loc, message);
    } else {
      Record(kDeclarationIndex, loc, message);
    }
  }

  void RecordExpressionError(const Scanner::Location& loc,
                             MessageTemplate message) {
    if (!CanBeExpression()) return;
    // TODO(verwaest): Non-assigning expression?
    // if (IsCertainlyExpression()) parser_->ReportMessageAt(loc, message);
    Record(kExpressionIndex, loc, message);
  }

  void ValidatePattern(ExpressionT expression) {
    Validate(kPatternIndex);
    if (expression->is_parenthesized()) {
      parser_->ReportMessageAt(
          Scanner::Location(expression->position(), parser_->end_position()),
          MessageTemplate::kInvalidDestructuringTarget);
    }
  }

  void ValidateDeclaration() {
    DCHECK_IMPLIES(!parser_->has_error(), CanBeDeclaration());
    DCHECK(!verified_);
    if (!is_valid(kDeclarationIndex)) Report(kDeclarationIndex);
    if (!is_valid(kPatternIndex)) Report(kPatternIndex);
    mark_verified();
  }

  void ValidateExpression() {
    DCHECK_IMPLIES(!parser_->has_error(), CanBeExpression());
    Validate(kExpressionIndex);
  }

  ExpressionT ValidateAndRewriteReference(ExpressionT expression, int beg_pos,
                                          int end_pos) {
    if (V8_LIKELY(parser_->IsAssignableIdentifier(expression))) {
      mark_verified();
      return expression;
    } else if (V8_LIKELY(expression->IsProperty())) {
      ValidateExpression();
      return expression;
    }
    mark_verified();
    return parser_->RewriteInvalidReferenceExpression(
        expression, beg_pos, end_pos, MessageTemplate::kInvalidLhsInFor,
        kSyntaxError);
  }

  V8_INLINE bool is_simple_parameter_list() const {
    DCHECK(CanBeFormalDeclaration());
    return is_simple_parameter_list_;
  }

  void RecordNonSimpleParameter() {
    DCHECK(CanBeFormalDeclaration());
    is_simple_parameter_list_ = false;
  }

  void ClearExpression() {
    DCHECK(verified_);
#ifdef DEBUG
    verified_ = false;
#endif
    clear(kExpressionIndex);
  }

 private:
  void clear(int index) {
    messages_[index] = MessageTemplate::kNone;
    locations_[index] = Scanner::Location::invalid();
  }

  void Clear() {
    DCHECK(verified_);
#ifdef DEBUG
    verified_ = false;
#endif
    for (int i = 0; i < kNumberOfErrors; i++) clear(i);
  }

  void mark_verified() {
#ifdef DEBUG
    verified_ = true;
#endif
  }

  enum ErrorNumber : uint8_t {
    kExpressionIndex = 0,
    kPatternIndex = 1,
    kDeclarationIndex = 2,
    kNumberOfErrors = 3,
  };

  void Accumulate(int index) const {
    if (is_valid(index)) return;
    parent_->Record(index, locations_[index], messages_[index]);
  }

  void Validate(int index) {
    DCHECK(!verified_);
    if (!is_valid(index)) Report(index);
    mark_verified();
  }

  bool is_valid(int index) const { return !locations_[index].IsValid(); }
  void Report(int index) const {
    parser_->ReportMessageAt(locations_[index], messages_[index]);
  }

  void Record(int index, const Scanner::Location& loc,
              MessageTemplate message) {
    if (!is_valid(index)) return;
    messages_[index] = message;
    locations_[index] = loc;
    // Blah.
    if (is_valid(index)) locations_[index] = Scanner::Location(0, 0);
  }

  Parser* parser_;
  ExpressionScope<Types>* parent_;
  ScopeType type_;
  bool has_async_arrow_in_scope_chain_;
  bool has_possible_formal_in_scope_chain_;
  bool is_simple_parameter_list_ = true;
#ifdef DEBUG
  bool verified_ = true;
#endif

  MessageTemplate messages_[kNumberOfErrors];
  Scanner::Location locations_[kNumberOfErrors];
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_EXPRESSION_SCOPE_H_
