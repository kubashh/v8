// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-generator.h"

#include "src/ast/ast-source-ranges.h"
#include "src/ast/compile-time-value.h"
#include "src/ast/scopes.h"
#include "src/builtins/builtins-constructor.h"
#include "src/code-stubs.h"
#include "src/compilation-info.h"
#include "src/compiler.h"
#include "src/interpreter/bytecode-flags.h"
#include "src/interpreter/bytecode-jump-table.h"
#include "src/interpreter/bytecode-label.h"
#include "src/interpreter/bytecode-register-allocator.h"
#include "src/interpreter/control-flow-builders.h"
#include "src/objects-inl.h"
#include "src/objects/debug-objects.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/token.h"

namespace v8 {
namespace internal {
namespace interpreter {

// ContextReference should not be used directly, but instead wrapped through
// BytecodeGenerator-level helpers (PushContextIfNeeded, PopContextIfNeeded), or
// ContextScope.
class BytecodeGenerator::ContextReference {
 private:
  ContextData data;
  DISALLOW_COPY_AND_ASSIGN(ContextReference)

 public:
  ContextReference(Scope* scope, ContextReference* outer)
      : data{scope, outer, Register::current_context(), DepthOf(outer) + 1} {
    if (!scope) return;
    DCHECK(outer == nullptr || scope->NeedsContext());
  }

  explicit ContextReference(ContextReference* context)
      : data{nullptr, nullptr, Register::invalid_value(), -1} {
    if (context == nullptr) return;
    if (context->scope() && context->scope()->NeedsContext()) {
      data = ContextData{context->scope(), context->data.outer, context->reg(),
                         context->depth()};
      context->Clear();
    }
  }

  static int DepthOf(ContextReference* ref) { return ref ? ref->depth() : -1; }

  void Clear() {
    data = ContextData{nullptr, nullptr, Register::invalid_value(), -1};
  }

  bool NeedsContext() const {
    return scope() != nullptr && scope()->NeedsContext();
  }

  void Initialize(BytecodeGenerator* g) {
    if (data.outer == nullptr || !NeedsContext()) return;
    DCHECK_EQ(data.outer->reg(), Register::current_context());
    data.outer->data.context_register = g->register_allocator()->NewRegister();
  }

  void MakeCurrent(BytecodeGenerator* g) {
    if (data.outer != nullptr && NeedsContext()) {
      DCHECK_NE(data.outer->reg(), Register::current_context());
      g->builder()->PushContext(data.outer->reg());
    }

    if (this == g->execution_context()) return;
    if (!data.outer || NeedsContext()) {
      g->set_execution_context(this);
    }
  }

  // Pop the current context, leaving on the top of the execution context stack
  void PopContext(BytecodeGenerator* g) {
    if (!data.outer || data.depth == -1) return;
    DCHECK(NeedsContext());
    DCHECK_EQ(data.context_register, Register::current_context());
    g->builder()->PopContext(data.outer->reg());
  }

  // Pop the current context _and_ remove from the execution context stack
  void PopExecutionContext(BytecodeGenerator* g) {
    if (g->execution_context() != this) return;

    ContextReference* outer = data.outer;
    DCHECK_IMPLIES(outer != nullptr, NeedsContext());
    Clear();

    if (outer != nullptr) {
      g->builder()->PopContext(outer->reg());
      outer->data.context_register = Register::current_context();
    }

    g->set_execution_context(outer);
  }

  Scope* scope() const { return data.scope; }

  int depth() const { return data.depth; }

  // Returns the depth of the given |scope| for the current execution context.
  int ContextChainDepth(Scope* scope) {
    return data.scope->ContextChainLength(scope);
  }

  // Returns the execution context at |depth| in the current context chain if it
  // is a function local execution context, otherwise returns nullptr.
  ContextReference* Previous(int depth) {
    DCHECK_NOT_NULL(this);
    DCHECK_GT(depth, -1);
    if (depth > data.depth) return nullptr;

    ContextReference* previous = this;
    for (int i = depth; i > 0; --i) {
      previous = previous->data.outer;
    }
    return previous;
  }

  Register reg() const {
    DCHECK_GT(data.depth, -1);
    return data.context_register;
  }
};

BytecodeGenerator::ContextReference* BytecodeGenerator::PushContextIfNeeded(
    Scope* scope) {
  if (!scope || !scope->NeedsContext()) return nullptr;

  ContextReference* outer = execution_context();

  context_stack_.emplace(ContextData{scope, outer, Register::current_context(),
                                     outer->depth() + 1});

  ContextReference* ref =
      reinterpret_cast<ContextReference*>(&context_stack_.top());
  ref->Initialize(this);
  ref->MakeCurrent(this);

  return ref;
}

BytecodeGenerator::ContextReference* BytecodeGenerator::OuterContextReference(
    ContextReference* current) const {
  if (!current) return execution_context();
  return current->Previous(1);
}

void BytecodeGenerator::PushContextIfNeeded(ContextReference* context,
                                            Register context_reg) {
  if (!context->scope()) return;
  DCHECK(context->scope()->NeedsContext());
  DCHECK(context_reg.is_valid());
  builder()->LoadAccumulatorWithRegister(context_reg);
  context->MakeCurrent(this);
}

void BytecodeGenerator::PopContextIfNeeded(ContextReference* context) {
  if (!context) return;
  DCHECK_EQ(context, execution_context());

  context->PopExecutionContext(this);
}

// Scoped class tracking context objects created by the visitor. Represents
// mutations of the context chain within the function body, allowing pushing and
// popping of the current {context_register} during visitation.
class BytecodeGenerator::ContextScope
    : private BytecodeGenerator::ContextReference {
 public:
  ContextScope(BytecodeGenerator* generator, Scope* scope)
      : ContextReference(scope, generator->execution_context()),
        generator_(generator) {
    Initialize(generator);
    MakeCurrent(generator);
  }

  ContextScope(BytecodeGenerator* generator, ContextReference* context)
      : ContextReference(context), generator_(generator) {
    if (scope() && scope()->NeedsContext()) {
      DCHECK_EQ(context, generator->execution_context());
      generator->set_execution_context(this);
    }
  }

  ~ContextScope() { PopExecutionContext(generator_); }

  using ContextReference::ContextChainDepth;
  using ContextReference::Previous;
  using ContextReference::reg;

 private:
  BytecodeGenerator* generator_;
};

// Scoped class for tracking control statements entered by the
// visitor. The pattern derives AstGraphBuilder::ControlScope.
class BytecodeGenerator::ControlScope BASE_EMBEDDED {
 public:
  explicit ControlScope(BytecodeGenerator* generator)
      : generator_(generator), outer_(generator->execution_control()),
        context_(generator->execution_context()) {
    generator_->set_execution_control(this);
  }
  ControlScope(BytecodeGenerator* generator, ContextReference* context)
      : generator_(generator),
        outer_(generator->execution_control()),
        context_(context ? context : generator->execution_context()) {
    generator_->set_execution_control(this);
  }
  virtual ~ControlScope() { generator_->set_execution_control(outer()); }

  void Break(Statement* stmt) {
    PerformCommand(CMD_BREAK, stmt, kNoSourcePosition);
  }
  void Continue(Statement* stmt) {
    PerformCommand(CMD_CONTINUE, stmt, kNoSourcePosition);
  }
  void ReturnAccumulator(int source_position = kNoSourcePosition) {
    PerformCommand(CMD_RETURN, nullptr, source_position);
  }
  void AsyncReturnAccumulator(int source_position = kNoSourcePosition) {
    PerformCommand(CMD_ASYNC_RETURN, nullptr, source_position);
  }

  class DeferredCommands;

 protected:
  enum Command {
    CMD_BREAK,
    CMD_CONTINUE,
    CMD_RETURN,
    CMD_ASYNC_RETURN,
    CMD_RETHROW
  };
  static constexpr bool CommandUsesAccumulator(Command command) {
    return command != CMD_BREAK && command != CMD_CONTINUE;
  }

  void PerformCommand(Command command, Statement* statement,
                      int source_position);
  virtual bool Execute(Command command, Statement* statement,
                       int source_position) = 0;

  // Helper to pop the context chain to a depth expected by this control scope.
  // Note that it is the responsibility of each individual {Execute} method to
  // trigger this when commands are handled and control-flow continues locally.
  void PopContextToExpectedDepth();

  BytecodeGenerator* generator() const { return generator_; }
  ControlScope* outer() const { return outer_; }
  ContextReference* context() const { return context_; }

 private:
  BytecodeGenerator* generator_;
  ControlScope* outer_;
  ContextReference* context_;

  DISALLOW_COPY_AND_ASSIGN(ControlScope);
};

// Helper class for a try-finally control scope. It can record intercepted
// control-flow commands that cause entry into a finally-block, and re-apply
// them after again leaving that block. Special tokens are used to identify
// paths going through the finally-block to dispatch after leaving the block.
class BytecodeGenerator::ControlScope::DeferredCommands final {
 public:
  DeferredCommands(BytecodeGenerator* generator, Register token_register,
                   Register result_register)
      : generator_(generator),
        deferred_(generator->zone()),
        token_register_(token_register),
        result_register_(result_register),
        return_token_(-1),
        async_return_token_(-1),
        rethrow_token_(-1) {}

  // One recorded control-flow command.
  struct Entry {
    Command command;       // The command type being applied on this path.
    Statement* statement;  // The target statement for the command or {nullptr}.
    int token;             // A token identifying this particular path.
  };

  // Records a control-flow command while entering the finally-block. This also
  // generates a new dispatch token that identifies one particular path. This
  // expects the result to be in the accumulator.
  void RecordCommand(Command command, Statement* statement) {
    int token = GetTokenForCommand(command, statement);

    DCHECK_LT(token, deferred_.size());
    DCHECK_EQ(deferred_[token].command, command);
    DCHECK_EQ(deferred_[token].statement, statement);
    DCHECK_EQ(deferred_[token].token, token);

    if (CommandUsesAccumulator(command)) {
      builder()->StoreAccumulatorInRegister(result_register_);
    }
    builder()->LoadLiteral(Smi::FromInt(token));
    builder()->StoreAccumulatorInRegister(token_register_);
    if (!CommandUsesAccumulator(command)) {
      // If we're not saving the accumulator in the result register, shove a
      // harmless value there instead so that it is still considered "killed" in
      // the liveness analysis. Normally we would LdaUndefined first, but the
      // Smi token value is just as good, and by reusing it we save a bytecode.
      builder()->StoreAccumulatorInRegister(result_register_);
    }
  }

  // Records the dispatch token to be used to identify the re-throw path when
  // the finally-block has been entered through the exception handler. This
  // expects the exception to be in the accumulator.
  void RecordHandlerReThrowPath() {
    // The accumulator contains the exception object.
    RecordCommand(CMD_RETHROW, nullptr);
  }

  // Records the dispatch token to be used to identify the implicit fall-through
  // path at the end of a try-block into the corresponding finally-block.
  void RecordFallThroughPath() {
    builder()->LoadLiteral(Smi::FromInt(-1));
    builder()->StoreAccumulatorInRegister(token_register_);
    // Since we're not saving the accumulator in the result register, shove a
    // harmless value there instead so that it is still considered "killed" in
    // the liveness analysis. Normally we would LdaUndefined first, but the Smi
    // token value is just as good, and by reusing it we save a bytecode.
    builder()->StoreAccumulatorInRegister(result_register_);
  }

  // Applies all recorded control-flow commands after the finally-block again.
  // This generates a dynamic dispatch on the token from the entry point.
  void ApplyDeferredCommands() {
    if (deferred_.size() == 0) return;

    BytecodeLabel fall_through;

    if (deferred_.size() == 1) {
      // For a single entry, just jump to the fallthrough if we don't match the
      // entry token.
      const Entry& entry = deferred_[0];

      builder()
          ->LoadLiteral(Smi::FromInt(entry.token))
          .CompareOperation(Token::EQ_STRICT, token_register_)
          .JumpIfFalse(ToBooleanMode::kAlreadyBoolean, &fall_through);

      if (CommandUsesAccumulator(entry.command)) {
        builder()->LoadAccumulatorWithRegister(result_register_);
      }
      execution_control()->PerformCommand(entry.command, entry.statement,
                                          kNoSourcePosition);
    } else {
      // For multiple entries, build a jump table and switch on the token,
      // jumping to the fallthrough if none of them match.

      BytecodeJumpTable* jump_table =
          builder()->AllocateJumpTable(static_cast<int>(deferred_.size()), 0);
      builder()
          ->LoadAccumulatorWithRegister(token_register_)
          .SwitchOnSmiNoFeedback(jump_table)
          .Jump(&fall_through);
      for (const Entry& entry : deferred_) {
        builder()->Bind(jump_table, entry.token);

        if (CommandUsesAccumulator(entry.command)) {
          builder()->LoadAccumulatorWithRegister(result_register_);
        }
        execution_control()->PerformCommand(entry.command, entry.statement,
                                            kNoSourcePosition);
      }
    }

    builder()->Bind(&fall_through);
  }

  BytecodeArrayBuilder* builder() { return generator_->builder(); }
  ControlScope* execution_control() { return generator_->execution_control(); }

  int RethrowToken() const {
    // SimpleTryFinally can use this to determine if the finally handler is
    // caused by an exception
    DCHECK_NE(rethrow_token_, -1);
    return rethrow_token_;
  }

 private:
  int GetTokenForCommand(Command command, Statement* statement) {
    switch (command) {
      case CMD_RETURN:
        return GetReturnToken();
      case CMD_ASYNC_RETURN:
        return GetAsyncReturnToken();
      case CMD_RETHROW:
        return GetRethrowToken();
      default:
        // TODO(leszeks): We could also search for entries with the same
        // command and statement.
        return GetNewTokenForCommand(command, statement);
    }
  }

  int GetReturnToken() {
    if (return_token_ == -1) {
      return_token_ = GetNewTokenForCommand(CMD_RETURN, nullptr);
    }
    return return_token_;
  }

  int GetAsyncReturnToken() {
    if (async_return_token_ == -1) {
      async_return_token_ = GetNewTokenForCommand(CMD_ASYNC_RETURN, nullptr);
    }
    return async_return_token_;
  }

  int GetRethrowToken() {
    if (rethrow_token_ == -1) {
      rethrow_token_ = GetNewTokenForCommand(CMD_RETHROW, nullptr);
    }
    return rethrow_token_;
  }

  int GetNewTokenForCommand(Command command, Statement* statement) {
    int token = static_cast<int>(deferred_.size());
    deferred_.push_back({command, statement, token});
    return token;
  }

  BytecodeGenerator* generator_;
  ZoneVector<Entry> deferred_;
  Register token_register_;
  Register result_register_;

  // Tokens for commands that don't need a statement.
  int return_token_;
  int async_return_token_;
  int rethrow_token_;
};

// Scoped class for dealing with control flow reaching the function level.
class BytecodeGenerator::ControlScopeForTopLevel final
    : public BytecodeGenerator::ControlScope {
 public:
  explicit ControlScopeForTopLevel(BytecodeGenerator* generator)
      : ControlScope(generator) {}

 protected:
  bool Execute(Command command, Statement* statement,
               int source_position) override {
    switch (command) {
      case CMD_BREAK:  // We should never see break/continue in top-level.
      case CMD_CONTINUE:
        UNREACHABLE();
      case CMD_RETURN:
        // No need to pop contexts, execution leaves the method body.
        generator()->BuildReturn(source_position);
        return true;
      case CMD_ASYNC_RETURN:
        // No need to pop contexts, execution leaves the method body.
        generator()->BuildAsyncReturn(source_position);
        return true;
      case CMD_RETHROW:
        // No need to pop contexts, execution leaves the method body.
        generator()->BuildReThrow();
        return true;
    }
    return false;
  }
};

// Scoped class for enabling break inside blocks and switch blocks.
class BytecodeGenerator::ControlScopeForBreakable final
    : public BytecodeGenerator::ControlScope {
 public:
  ControlScopeForBreakable(BytecodeGenerator* generator,
                           BreakableStatement* statement,
                           BreakableControlFlowBuilder* control_builder)
      : ControlScope(generator),
        statement_(statement),
        control_builder_(control_builder) {}

 protected:
  bool Execute(Command command, Statement* statement,
               int source_position) override {
    control_builder_->set_needs_continuation_counter();
    if (statement != statement_) return false;
    switch (command) {
      case CMD_BREAK:
        PopContextToExpectedDepth();
        control_builder_->Break();
        return true;
      case CMD_CONTINUE:
      case CMD_RETURN:
      case CMD_ASYNC_RETURN:
      case CMD_RETHROW:
        break;
    }
    return false;
  }

 private:
  Statement* statement_;
  BreakableControlFlowBuilder* control_builder_;
};

// Scoped class for enabling 'break' and 'continue' in iteration
// constructs, e.g. do...while, while..., for...
class BytecodeGenerator::ControlScopeForIteration final
    : public BytecodeGenerator::ControlScope {
 public:
  ControlScopeForIteration(BytecodeGenerator* generator,
                           IterationStatement* statement,
                           LoopBuilder* loop_builder)
      : ControlScope(generator),
        statement_(statement),
        loop_builder_(loop_builder) {
    generator->loop_depth_++;
  }
  ControlScopeForIteration(BytecodeGenerator* generator, ContextReference* env,
                           IterationStatement* statement,
                           LoopBuilder* loop_builder)
      : ControlScope(generator, env),
        statement_(statement),
        loop_builder_(loop_builder) {
    generator->loop_depth_++;
  }
  ~ControlScopeForIteration() { generator()->loop_depth_--; }

 protected:
  bool Execute(Command command, Statement* statement,
               int source_position) override {
    if (statement != statement_) return false;
    switch (command) {
      case CMD_BREAK:
        PopContextToExpectedDepth();
        loop_builder_->Break();
        return true;
      case CMD_CONTINUE:
        PopContextToExpectedDepth();
        loop_builder_->Continue();
        return true;
      case CMD_RETURN:
      case CMD_ASYNC_RETURN:
      case CMD_RETHROW:
        break;
    }
    return false;
  }

 private:
  Statement* statement_;
  LoopBuilder* loop_builder_;
};

// Scoped class for enabling 'throw' in try-catch constructs.
class BytecodeGenerator::ControlScopeForTryCatch final
    : public BytecodeGenerator::ControlScope {
 public:
  ControlScopeForTryCatch(BytecodeGenerator* generator,
                          TryCatchBuilder* try_catch_builder)
      : ControlScope(generator) {}

 protected:
  bool Execute(Command command, Statement* statement,
               int source_position) override {
    switch (command) {
      case CMD_BREAK:
      case CMD_CONTINUE:
      case CMD_RETURN:
      case CMD_ASYNC_RETURN:
        break;
      case CMD_RETHROW:
        // No need to pop contexts, execution re-enters the method body via the
        // stack unwinding mechanism which itself restores contexts correctly.
        generator()->BuildReThrow();
        return true;
    }
    return false;
  }
};

// Scoped class for enabling control flow through try-finally constructs.
class BytecodeGenerator::ControlScopeForTryFinally final
    : public BytecodeGenerator::ControlScope {
 public:
  ControlScopeForTryFinally(BytecodeGenerator* generator,
                            TryFinallyBuilder* try_finally_builder,
                            DeferredCommands* commands)
      : ControlScope(generator),
        try_finally_builder_(try_finally_builder),
        commands_(commands) {}

 protected:
  bool Execute(Command command, Statement* statement,
               int source_position) override {
    switch (command) {
      case CMD_BREAK:
      case CMD_RETURN:
      case CMD_ASYNC_RETURN:
      case CMD_RETHROW:
      case CMD_CONTINUE:
        PopContextToExpectedDepth();
        // We don't record source_position here since we don't generate return
        // bytecode right here and will generate it later as part of finally
        // block. Each return bytecode generated in finally block will get own
        // return source position from corresponded return statement or we'll
        // use end of function if no return statement is presented.
        commands_->RecordCommand(command, statement);
        try_finally_builder_->LeaveTry();
        return true;
    }
    return false;
  }

 private:
  TryFinallyBuilder* try_finally_builder_;
  DeferredCommands* commands_;
};

void BytecodeGenerator::ControlScope::PerformCommand(Command command,
                                                     Statement* statement,
                                                     int source_position) {
  ControlScope* current = this;
  do {
    if (current->Execute(command, statement, source_position)) {
      return;
    }
    current = current->outer();
  } while (current != nullptr);
  UNREACHABLE();
}

void BytecodeGenerator::ControlScope::PopContextToExpectedDepth() {
  // Pop context to the expected depth. Note that this can in fact pop multiple
  // contexts at once because the {PopContext} bytecode takes a saved register.
  if (generator()->execution_context() != context()) {
    generator()->builder()->PopContext(context()->reg());
  }
}

class BytecodeGenerator::RegisterAllocationScope final {
 public:
  explicit RegisterAllocationScope(BytecodeGenerator* generator)
      : generator_(generator),
        outer_next_register_index_(
            generator->register_allocator()->next_register_index()) {}

  ~RegisterAllocationScope() {
    generator_->register_allocator()->ReleaseRegisters(
        outer_next_register_index_);
  }

 private:
  BytecodeGenerator* generator_;
  int outer_next_register_index_;

  DISALLOW_COPY_AND_ASSIGN(RegisterAllocationScope);
};

// Scoped base class for determining how the result of an expression will be
// used.
class BytecodeGenerator::ExpressionResultScope {
 public:
  ExpressionResultScope(BytecodeGenerator* generator, Expression::Context kind)
      : generator_(generator),
        outer_(generator->execution_result()),
        allocator_(generator),
        kind_(kind),
        type_hint_(TypeHint::kAny) {
    generator_->set_execution_result(this);
  }

  virtual ~ExpressionResultScope() {
    generator_->set_execution_result(outer_);
  }

  bool IsEffect() const { return kind_ == Expression::kEffect; }
  bool IsValue() const { return kind_ == Expression::kValue; }
  bool IsTest() const { return kind_ == Expression::kTest; }

  TestResultScope* AsTest() {
    DCHECK(IsTest());
    return reinterpret_cast<TestResultScope*>(this);
  }

  // Specify expression always returns a Boolean result value.
  void SetResultIsBoolean() {
    DCHECK_EQ(type_hint_, TypeHint::kAny);
    type_hint_ = TypeHint::kBoolean;
  }

  TypeHint type_hint() const { return type_hint_; }

 private:
  BytecodeGenerator* generator_;
  ExpressionResultScope* outer_;
  RegisterAllocationScope allocator_;
  Expression::Context kind_;
  TypeHint type_hint_;

  DISALLOW_COPY_AND_ASSIGN(ExpressionResultScope);
};

// Scoped class used when the result of the current expression is not
// expected to produce a result.
class BytecodeGenerator::EffectResultScope final
    : public ExpressionResultScope {
 public:
  explicit EffectResultScope(BytecodeGenerator* generator)
      : ExpressionResultScope(generator, Expression::kEffect) {}
};

// Scoped class used when the result of the current expression to be
// evaluated should go into the interpreter's accumulator.
class BytecodeGenerator::ValueResultScope final : public ExpressionResultScope {
 public:
  explicit ValueResultScope(BytecodeGenerator* generator)
      : ExpressionResultScope(generator, Expression::kValue) {}
};

// Scoped class used when the result of the current expression to be
// evaluated is only tested with jumps to two branches.
class BytecodeGenerator::TestResultScope final : public ExpressionResultScope {
 public:
  TestResultScope(BytecodeGenerator* generator, BytecodeLabels* then_labels,
                  BytecodeLabels* else_labels, TestFallthrough fallthrough)
      : ExpressionResultScope(generator, Expression::kTest),
        result_consumed_by_test_(false),
        fallthrough_(fallthrough),
        then_labels_(then_labels),
        else_labels_(else_labels) {}

  // Used when code special cases for TestResultScope and consumes any
  // possible value by testing and jumping to a then/else label.
  void SetResultConsumedByTest() {
    result_consumed_by_test_ = true;
  }
  bool result_consumed_by_test() { return result_consumed_by_test_; }

  // Inverts the control flow of the operation, swapping the then and else
  // labels and the fallthrough.
  void InvertControlFlow() {
    std::swap(then_labels_, else_labels_);
    fallthrough_ = inverted_fallthrough();
  }

  BytecodeLabel* NewThenLabel() { return then_labels_->New(); }
  BytecodeLabel* NewElseLabel() { return else_labels_->New(); }

  BytecodeLabels* then_labels() const { return then_labels_; }
  BytecodeLabels* else_labels() const { return else_labels_; }

  void set_then_labels(BytecodeLabels* then_labels) {
    then_labels_ = then_labels;
  }
  void set_else_labels(BytecodeLabels* else_labels) {
    else_labels_ = else_labels;
  }

  TestFallthrough fallthrough() const { return fallthrough_; }
  TestFallthrough inverted_fallthrough() const {
    switch (fallthrough_) {
      case TestFallthrough::kThen:
        return TestFallthrough::kElse;
      case TestFallthrough::kElse:
        return TestFallthrough::kThen;
      default:
        return TestFallthrough::kNone;
    }
  }
  void set_fallthrough(TestFallthrough fallthrough) {
    fallthrough_ = fallthrough;
  }

 private:
  bool result_consumed_by_test_;
  TestFallthrough fallthrough_;
  BytecodeLabels* then_labels_;
  BytecodeLabels* else_labels_;

  DISALLOW_COPY_AND_ASSIGN(TestResultScope);
};

// Used to build a list of global declaration initial value pairs.
class BytecodeGenerator::GlobalDeclarationsBuilder final : public ZoneObject {
 public:
  explicit GlobalDeclarationsBuilder(Zone* zone)
      : declarations_(0, zone),
        constant_pool_entry_(0),
        has_constant_pool_entry_(false) {}

  void AddFunctionDeclaration(const AstRawString* name, FeedbackSlot slot,
                              FeedbackSlot literal_slot,
                              FunctionLiteral* func) {
    DCHECK(!slot.IsInvalid());
    declarations_.push_back(Declaration(name, slot, literal_slot, func));
  }

  void AddUndefinedDeclaration(const AstRawString* name, FeedbackSlot slot) {
    DCHECK(!slot.IsInvalid());
    declarations_.push_back(Declaration(name, slot, nullptr));
  }

  Handle<FixedArray> AllocateDeclarations(CompilationInfo* info,
                                          Handle<Script> script) {
    DCHECK(has_constant_pool_entry_);
    int array_index = 0;
    Handle<FixedArray> data = info->isolate()->factory()->NewFixedArray(
        static_cast<int>(declarations_.size() * 4), TENURED);
    for (const Declaration& declaration : declarations_) {
      FunctionLiteral* func = declaration.func;
      Handle<Object> initial_value;
      if (func == nullptr) {
        initial_value = info->isolate()->factory()->undefined_value();
      } else {
        initial_value =
            Compiler::GetSharedFunctionInfo(func, script, info->isolate());
      }

      // Return a null handle if any initial values can't be created. Caller
      // will set stack overflow.
      if (initial_value.is_null()) return Handle<FixedArray>();

      data->set(array_index++, *declaration.name->string());
      data->set(array_index++, Smi::FromInt(declaration.slot.ToInt()));
      Object* undefined_or_literal_slot;
      if (declaration.literal_slot.IsInvalid()) {
        undefined_or_literal_slot = info->isolate()->heap()->undefined_value();
      } else {
        undefined_or_literal_slot =
            Smi::FromInt(declaration.literal_slot.ToInt());
      }
      data->set(array_index++, undefined_or_literal_slot);
      data->set(array_index++, *initial_value);
    }
    return data;
  }

  size_t constant_pool_entry() {
    DCHECK(has_constant_pool_entry_);
    return constant_pool_entry_;
  }

  void set_constant_pool_entry(size_t constant_pool_entry) {
    DCHECK(!empty());
    DCHECK(!has_constant_pool_entry_);
    constant_pool_entry_ = constant_pool_entry;
    has_constant_pool_entry_ = true;
  }

  bool empty() { return declarations_.empty(); }

 private:
  struct Declaration {
    Declaration() : slot(FeedbackSlot::Invalid()), func(nullptr) {}
    Declaration(const AstRawString* name, FeedbackSlot slot,
                FeedbackSlot literal_slot, FunctionLiteral* func)
        : name(name), slot(slot), literal_slot(literal_slot), func(func) {}
    Declaration(const AstRawString* name, FeedbackSlot slot,
                FunctionLiteral* func)
        : name(name),
          slot(slot),
          literal_slot(FeedbackSlot::Invalid()),
          func(func) {}

    const AstRawString* name;
    FeedbackSlot slot;
    FeedbackSlot literal_slot;
    FunctionLiteral* func;
  };
  ZoneVector<Declaration> declarations_;
  size_t constant_pool_entry_;
  bool has_constant_pool_entry_;
};

class BytecodeGenerator::CurrentScope final {
 public:
  CurrentScope(BytecodeGenerator* generator, Scope* scope)
      : generator_(generator), outer_scope_(generator->current_scope()) {
    if (scope != nullptr) {
      DCHECK_EQ(outer_scope_, scope->outer_scope());
      generator_->set_current_scope(scope);
    }
  }
  ~CurrentScope() {
    if (outer_scope_ != generator_->current_scope()) {
      generator_->set_current_scope(outer_scope_);
    }
  }

 private:
  BytecodeGenerator* generator_;
  Scope* outer_scope_;
};

class BytecodeGenerator::FeedbackSlotCache : public ZoneObject {
 public:
  typedef std::pair<TypeofMode, void*> Key;

  explicit FeedbackSlotCache(Zone* zone) : map_(zone) {}

  void Put(TypeofMode typeof_mode, Variable* variable, FeedbackSlot slot) {
    Key key = std::make_pair(typeof_mode, variable);
    auto entry = std::make_pair(key, slot);
    map_.insert(entry);
  }
  void Put(AstNode* node, FeedbackSlot slot) {
    Key key = std::make_pair(NOT_INSIDE_TYPEOF, node);
    auto entry = std::make_pair(key, slot);
    map_.insert(entry);
  }

  FeedbackSlot Get(TypeofMode typeof_mode, Variable* variable) const {
    Key key = std::make_pair(typeof_mode, variable);
    auto iter = map_.find(key);
    if (iter != map_.end()) {
      return iter->second;
    }
    return FeedbackSlot();
  }
  FeedbackSlot Get(AstNode* node) const {
    Key key = std::make_pair(NOT_INSIDE_TYPEOF, node);
    auto iter = map_.find(key);
    if (iter != map_.end()) {
      return iter->second;
    }
    return FeedbackSlot();
  }

 private:
  ZoneMap<Key, FeedbackSlot> map_;
};

class BytecodeGenerator::SimpleTryFinally : private TryFinallyBuilder {
 public:
  enum class Mode { kNone, kTry, kAfterTry, kFinally };

  // We can't know whether the finally block will override ("catch") an
  // exception thrown in the try block, so we just adopt the outer prediction.

  // We keep a record of all paths that enter the finally-block to be able to
  // dispatch to the correct continuation point after the statements in the
  // finally-block have been evaluated.
  //
  // The try-finally construct can enter the finally-block in three ways:
  // 1. By exiting the try-block normally, falling through at the end.
  // 2. By exiting the try-block with a function-local control flow transfer
  //    (i.e. through break/continue/return statements).
  // 3. By exiting the try-block with a thrown exception.
  //
  // The result register semantics depend on how the block was entered:
  //  - ReturnStatement: It represents the return value being returned.
  //  - ThrowStatement: It represents the exception being thrown.
  //  - BreakStatement/ContinueStatement: Undefined and not used.
  //  - Falling through into finally-block: Undefined and not used.

  explicit SimpleTryFinally(BytecodeGenerator* self)
      : TryFinallyBuilder(self->builder(), self->catch_prediction()),
        mode_(Mode::kNone),
        break_labels_(self->zone()),
        control_scope_(nullptr) {
    this->self = self;
  }

  ~SimpleTryFinally() { DCHECK_EQ(mode_, Mode::kNone); }

  void BeginTry() {
    DCHECK_EQ(mode_, Mode::kNone);
    mode_ = Mode::kTry;

    // We keep a record of all paths that enter the finally-block to be able to
    // dispatch to the correct continuation point after the statements in the
    // finally-block have been evaluated.
    //
    // The try-finally construct can enter the finally-block in three ways:
    // 1. By exiting the try-block normally, falling through at the end.
    // 2. By exiting the try-block with a function-local control flow transfer
    //    (i.e. through break/continue/return statements).
    // 3. By exiting the try-block with a thrown exception.
    //
    // The result register semantics depend on how the block was entered:
    //  - ReturnStatement: It represents the return value being returned.
    //  - ThrowStatement: It represents the exception being thrown.
    //  - BreakStatement/ContinueStatement: Undefined and not used.
    //  - Falling through into finally-block: Undefined and not used.
    token_ = register_allocator()->NewRegister();
    result_ = register_allocator()->NewRegister();
    commands_ = new ControlScope::DeferredCommands(self, token_, result_);

    // Preserve the context in a dedicated register, so that it can be restored
    // when the handler is entered by the stack-unwinding machinery.
    // TODO(mstarzinger): Be smarter about register allocation.
    context_ = register_allocator()->NewRegister();
    self->builder()->MoveRegister(Register::current_context(), context_);

    // Evaluate the try-block inside a control scope. This simulates a handler
    // that is intercepting all control commands.
    TryFinallyBuilder::BeginTry(context_);
    control_scope_ = new ControlScopeForTryFinally(self, this, commands_);
  }

  void EndTry() {
    DCHECK_EQ(mode_, Mode::kTry);
    mode_ = Mode::kAfterTry;
    delete control_scope_;
    control_scope_ = nullptr;

    TryFinallyBuilder::EndTry();

    // Record fall-through and exception cases.
    commands_->RecordFallThroughPath();
    TryFinallyBuilder::LeaveTry();
  }

  void BeginFinally() {
    DCHECK_EQ(mode_, Mode::kAfterTry);
    mode_ = Mode::kFinally;

    TryFinallyBuilder::BeginHandler();
    commands_->RecordHandlerReThrowPath();

    // Pending message object is saved on entry.
    TryFinallyBuilder::BeginFinally();
    Register message = context_;  // Reuse register.

    // Clear message object as we enter the finally block.
    builder()->LoadTheHole().SetPendingMessage().StoreAccumulatorInRegister(
        message);
  }

  void EndFinally() {
    DCHECK_EQ(mode_, Mode::kFinally);
    mode_ = Mode::kNone;

    if (!break_labels_.empty()) break_labels_.Bind(builder());

    TryFinallyBuilder::EndFinally();

    // Pending message object is restored on exit.
    Register message = context_;  // Reuse register.
    builder()->LoadAccumulatorWithRegister(message).SetPendingMessage();

    // Dynamic dispatch after the finally-block.
    commands_->ApplyDeferredCommands();

    delete commands_;
    commands_ = nullptr;
  }

  void RethrowAccumulator(bool keep_original_exception) {
    DCHECK_EQ(mode_, Mode::kFinally);

    if (keep_original_exception) {
      // Check if the token is an exception token, and if it is, leave it alone
      Register temp = register_allocator()->NewRegister();
      builder()->StoreAccumulatorInRegister(temp);
      BreakIfRethrow();
      Rethrow(temp);
      return;
    }

    // Otherwise, always update the completion
    builder()
        ->StoreAccumulatorInRegister(result_)
        .LoadLiteral(Smi::FromInt(commands_->RethrowToken()))
        .StoreAccumulatorInRegister(token_)
        .Jump(break_labels_.New());
  }

  void Rethrow(Register exception) {
    DCHECK_EQ(mode_, Mode::kFinally);
    builder()
        ->MoveRegister(exception, result_)
        .LoadLiteral(Smi::FromInt(commands_->RethrowToken()))
        .StoreAccumulatorInRegister(token_)
        .Jump(break_labels_.New());
  }

  void BreakIfRethrow() {
    DCHECK_EQ(mode_, Mode::kFinally);
    builder()
        ->LoadLiteral(Smi::FromInt(commands_->RethrowToken()))
        .CompareOperation(Token::EQ_STRICT, token_)
        .JumpIfTrue(ToBooleanMode::kAlreadyBoolean, break_labels_.New());
  }

 private:
  BytecodeArrayBuilder* builder() { return self->builder(); }
  BytecodeRegisterAllocator* register_allocator() {
    return self->register_allocator();
  }

  BytecodeGenerator* self;
  Register token_;
  Register result_;
  Register context_;
  Mode mode_;
  BytecodeLabels break_labels_;

  ControlScope::DeferredCommands* commands_;
  ControlScopeForTryFinally* control_scope_;
};

class BytecodeGenerator::SimpleTryCatch : private TryCatchBuilder {
 public:
  enum class Mode { kNone, kTry, kAfterTry, kCatch };

  SimpleTryCatch(BytecodeGenerator* self,
                 HandlerTable::CatchPrediction catch_prediction,
                 Scope* scope = nullptr)
      : TryCatchBuilder(self->builder(),
                        GetCatchPrediction(self, catch_prediction)),
        old_catch_prediction_(self->catch_prediction()),
        mode_(Mode::kNone),
        scope_(scope) {
    this->self = self;
  }

  ~SimpleTryCatch() { DCHECK_EQ(mode_, Mode::kNone); }

  void BeginTry() {
    DCHECK_EQ(mode_, Mode::kNone);
    mode_ = Mode::kTry;

    // Preserve the context in a dedicated register, so that it can be restored
    // when the handler is entered by the stack-unwinding machinery.
    // TODO(mstarzinger): Be smarter about register allocation.
    context_ = register_allocator()->NewRegister();
    builder()->MoveRegister(Register::current_context(), context_);

    self->set_catch_prediction(TryCatchBuilder::catch_prediction());

    // Evaluate the try-block inside a control scope. This simulates a handler
    // that is intercepting all control commands.
    TryCatchBuilder::BeginTry(context_);
    control_scope_ = new ControlScopeForTryCatch(self, this);
  }

  void EndTry() {
    DCHECK_EQ(mode_, Mode::kTry);
    mode_ = Mode::kAfterTry;

    self->set_catch_prediction(old_catch_prediction_);

    delete control_scope_;
    control_scope_ = nullptr;
    TryCatchBuilder::EndTry();
  }

  Register context() const {
    DCHECK_NOT_NULL(scope_);
    return context_;
  }

  // If a scope is used, builts (but does not enter) the scope, and returns
  // with the new context in the accumulator.
  //
  // Otherwise, returns with the exception in the accumulator.
  void BeginCatch() {
    DCHECK_EQ(mode_, Mode::kAfterTry);
    mode_ = Mode::kCatch;

    if (scope_ != nullptr) {
      DCHECK(scope_->is_catch_scope());
      DCHECK(scope_->NeedsContext());
      DCHECK_EQ(self->current_scope(), scope_->outer_scope());
      self->BuildNewLocalCatchContext(scope_);

      if (ShouldClearPendingException()) {
        builder()
            ->StoreAccumulatorInRegister(context_)
            .LoadTheHole()
            .SetPendingMessage()
            .LoadAccumulatorWithRegister(context_);
      }
      return;
    }

    // If requested, clear message object as we enter the catch block.
    if (ShouldClearPendingException()) {
      RegisterAllocationScope register_scope(self);
      Register thrown_object = register_allocator()->NewRegister();
      builder()->StoreAccumulatorInRegister(thrown_object);
      builder()->LoadTheHole().SetPendingMessage();
      builder()->LoadAccumulatorWithRegister(thrown_object);
    }
  }

  void EndCatch() {
    DCHECK_EQ(mode_, Mode::kCatch);
    mode_ = Mode::kNone;

    TryCatchBuilder::EndCatch();
  }

 private:
  BytecodeArrayBuilder* builder() { return self->builder(); }
  BytecodeRegisterAllocator* register_allocator() {
    return self->register_allocator();
  }

  BytecodeGenerator* self;
  HandlerTable::CatchPrediction old_catch_prediction_;
  Mode mode_;
  Register context_;

  ControlScopeForTryCatch* control_scope_;
  Scope* scope_;

  // Prediction of whether exceptions thrown into the handler for this try block
  // will be caught.
  //
  // BytecodeGenerator tracks the state of catch prediction, which can change
  // with each TryCatchStatement encountered. The tracked catch prediction is
  // later compiled into the code's handler table. The runtime uses this
  // information to implement a feature that notifies the debugger when an
  // uncaught exception is thrown, _before_ the exception propagates to the top.
  //
  // If this try/catch statement is meant to rethrow (HandlerTable::UNCAUGHT),
  // the catch prediction value is set to the same value as the surrounding
  // catch prediction.
  //
  // Since it's generally undecidable whether an exception will be caught, our
  // prediction is only an approximation.
  // ---------------------------------------------------------------------------
  static HandlerTable::CatchPrediction GetCatchPrediction(
      BytecodeGenerator* self, HandlerTable::CatchPrediction new_prediction) {
    if (new_prediction == HandlerTable::UNCAUGHT) {
      return self->catch_prediction();
    }
    return new_prediction;
  }

  // Indicates whether or not code should be generated to clear the pending
  // exception. The pending exception is cleared for cases where the exception
  // is not guaranteed to be rethrown, indicated by the value
  // HandlerTable::UNCAUGHT. If both the current and surrounding catch handler's
  // are predicted uncaught, the exception is not cleared.
  //
  // If this handler is not going to simply rethrow the exception, this method
  // indicates that the isolate's pending exception message should be cleared
  // before executing the catch_block.
  // In the normal use case, this flag is always on because the message object
  // is not needed anymore when entering the catch block and should not be
  // kept alive.
  // The use case where the flag is off is when the catch block is guaranteed
  // to rethrow the caught exception (using %ReThrow), which reuses the
  // pending message instead of generating a new one.
  // (When the catch block doesn't rethrow but is guaranteed to perform an
  // ordinary throw, not clearing the old message is safe but not very
  // useful.)
  bool ShouldClearPendingException() const {
    return TryCatchBuilder::catch_prediction() != HandlerTable::UNCAUGHT ||
           old_catch_prediction_ != HandlerTable::UNCAUGHT;
  }
};

class BytecodeGenerator::Reference final {
 public:
  enum State { kUnresolved, kResolved, kPattern, kElision };
  using RegisterAllocationScope = BytecodeGenerator::RegisterAllocationScope;

  Reference(BytecodeGenerator* generator, Expression* ref,
            Token::Value op = Token::ASSIGN)
      : state_(kUnresolved),
        op_(op),
        did_check_lhs_hole_(false),
        generator_(generator),
        reference_(ref),
        name_(nullptr) {
    if (ref == nullptr) {
      state_ = kElision;
      return;
    }

    if (ref->IsVarExpression()) {
      VarExpression* expr = ref->AsVarExpression();
      // VarExpression references must only contain a single pattern
      reference_ = ref = expr->pattern();
      if (IsLexicalVariableMode(expr->mode())) {
        op_ = Token::INIT;
      }
    }

    DCHECK(ref->IsPattern() || ref->IsValidReferenceExpression() ||
           (op == Token::INIT && ref->IsVariableProxy() &&
            ref->AsVariableProxy()->is_this()));
    if (ref->IsPattern()) {
      state_ = kPattern;
    }
  }

  bool IsPattern() const { return state_ == kPattern; }

  bool IsObjectPattern() const {
    return IsPattern() && reference_->IsObjectPattern();
  }

  ObjectPattern* GetObjectPattern() const {
    if (IsPattern()) return reference_->AsObjectPattern();
    return nullptr;
  }

  bool IsArrayPattern() const {
    return IsPattern() && reference_->IsArrayPattern();
  }

  ArrayPattern* GetArrayPattern() const {
    if (IsPattern()) return reference_->AsArrayPattern();
    return nullptr;
  }

  void Resolve();
  void TryResolve(BytecodeLabels* handler,
                  HandlerTable::CatchPrediction prediction);
  bool HasNoopResolve() const {
    if (state_ != kUnresolved) return true;
    if (IsPattern()) return true;
    if (reference_->IsVariableProxy()) {
      VariableProxy* proxy = reference_->AsVariableProxy();
      Variable* var = proxy->var();
      bool kNeedsHoleCheck = op_ != Token::INIT && proxy->hole_check_mode() ==
                                                       HoleCheckMode::kRequired;
      const bool kStrictLookup =
          var->IsUnallocated() && is_strict(generator_->language_mode());

      if (kNeedsHoleCheck || kStrictLookup) return false;

      return true;
    }

    return false;
  }

  // Alternative form of Resolve which does not visit VariableProxy nodes,
  // for use by VisitAssignment
  void PrepareForAssignment();
  void GetValue();
  void SetValue(Register value = Register::invalid_value(),
                bool require_object_coercible = true,
                LookupHoistingMode hoisting_mode = LookupHoistingMode::kNormal);
  inline void SetValue(bool require_object_coercible) {
    SetValue(Register::invalid_value(), require_object_coercible);
  }

  inline void TrySetValue(Register value, bool require_object_coercible,
                          BytecodeLabels* handler,
                          HandlerTable::CatchPrediction prediction);
  inline void TrySetValue(Register value, BytecodeLabels* handler,
                          HandlerTable::CatchPrediction prediction) {
    static const bool kRequireObjectCoercible = true;
    return TrySetValue(value, kRequireObjectCoercible, handler, prediction);
  }

 private:
  inline BytecodeArrayBuilder* builder() { return generator_->builder(); }
  inline BytecodeRegisterAllocator* register_allocator() {
    return generator_->register_allocator();
  }
  inline FeedbackVectorSpec* feedback_spec() {
    return generator_->feedback_spec();
  }
  inline int feedback_index(FeedbackSlot slot) const {
    return generator_->feedback_index(slot);
  }

  State state_;
  Token::Value op_;
  // Track if hole-checking has occurred in the LHS or not
  bool did_check_lhs_hole_ : 1;
  LhsKind lhs_type_;

  BytecodeGenerator* generator_;
  Expression* reference_;
  Register object_;
  Register key_;
  const AstRawString* name_;
  RegisterList super_property_args_;
};

void BytecodeGenerator::Reference::Resolve() {
  if (state_ != kUnresolved) return;
  Property* property = reference_->AsProperty();
  lhs_type_ = Property::GetAssignType(property);

  if (lhs_type_ == VARIABLE) {
    VariableProxy* proxy = reference_->AsVariableProxy();
    if (!HasNoopResolve()) {
      // with-blocks and global variables can have side-effects when
      // resolving the binding, so we perform a load to ensure that those
      // side-effects can occur.

      // TODO: handle with-block lookups
      generator_->VisitForEffect(proxy);
      did_check_lhs_hole_ = true;
    }
    state_ = kResolved;
  } else {
    PrepareForAssignment();
  }
}

void BytecodeGenerator::Reference::TryResolve(
    BytecodeLabels* handler, HandlerTable::CatchPrediction prediction) {
  if (state_ != kUnresolved) return;
  SimpleTryCatch try_catch(generator_, prediction);
  try_catch.BeginTry();
  Resolve();
  try_catch.EndTry();

  try_catch.BeginCatch();
  builder()->Jump(handler->New());
  try_catch.EndCatch();
}

void BytecodeGenerator::Reference::PrepareForAssignment() {
  if (state_ != kUnresolved) return;
  Property* property = reference_->AsProperty();
  lhs_type_ = Property::GetAssignType(property);

  switch (lhs_type_) {
    case VARIABLE: {
      // Nothing to do to evaluate variable assignment LHS.
      break;
    }
    case NAMED_PROPERTY:
    case KEYED_PROPERTY: {
      object_ = generator_->VisitForRegisterValue(property->obj());
      if (lhs_type_ == NAMED_PROPERTY) {
        name_ = property->key()->AsLiteral()->AsRawPropertyName();
        DCHECK_NOT_NULL(name_);
      } else {
        key_ = generator_->VisitForRegisterValue(property->key());
      }
      break;
    }

    case NAMED_SUPER_PROPERTY:
    case KEYED_SUPER_PROPERTY: {
      super_property_args_ = register_allocator()->NewRegisterList(4);
      SuperPropertyReference* super_property =
          property->obj()->AsSuperPropertyReference();
      generator_->VisitForRegisterValue(super_property->this_var(),
                                        super_property_args_[0]);
      generator_->VisitForRegisterValue(super_property->home_object(),
                                        super_property_args_[1]);
      if (lhs_type_ == NAMED_SUPER_PROPERTY) {
        builder()
            ->LoadLiteral(property->key()->AsLiteral()->AsRawPropertyName())
            .StoreAccumulatorInRegister(super_property_args_[2]);
      } else {
        generator_->VisitForRegisterValue(property->key(),
                                          super_property_args_[2]);
      }
      break;
    }
  }
  state_ = kResolved;
}

void BytecodeGenerator::Reference::GetValue() {
  DCHECK_NE(state_, kPattern);
  DCHECK_NE(state_, kElision);
  Resolve();

  switch (lhs_type_) {
    case VARIABLE: {
      VariableProxy* proxy = reference_->AsVariableProxy();
      HoleCheckMode hole_check_mode = did_check_lhs_hole_
                                          ? HoleCheckMode::kElided
                                          : proxy->hole_check_mode();
      generator_->BuildVariableLoad(proxy->var(), hole_check_mode);
      did_check_lhs_hole_ = true;
      break;
    }
    case NAMED_PROPERTY: {
      FeedbackSlot slot = feedback_spec()->AddLoadICSlot();
      builder()->LoadNamedProperty(object_, name_, feedback_index(slot));
      break;
    }
    case KEYED_PROPERTY: {
      // Key may no longer be in the accumulator, so load it
      FeedbackSlot slot = feedback_spec()->AddKeyedLoadICSlot();
      builder()->LoadAccumulatorWithRegister(key_).LoadKeyedProperty(
          object_, feedback_index(slot));
      break;
    }
    case NAMED_SUPER_PROPERTY: {
      builder()->CallRuntime(Runtime::kLoadFromSuper,
                             super_property_args_.Truncate(3));
      break;
    }
    case KEYED_SUPER_PROPERTY: {
      builder()->CallRuntime(Runtime::kLoadKeyedFromSuper,
                             super_property_args_.Truncate(3));
      break;
    }
  }
}

void BytecodeGenerator::Reference::SetValue(
    Register value, bool require_object_coercible,
    LookupHoistingMode lookup_hoisting_mode) {
  DCHECK_NE(state_, kElision);

  RegisterAllocationScope register_scope(generator_);
  bool value_in_register = value.is_valid();
  LanguageMode language_mode = generator_->language_mode();

  if (IsPattern()) {
    if (!value_in_register) {
      value = register_allocator()->NewRegister();
      builder()->StoreAccumulatorInRegister(value);
    }

    if (IsObjectPattern()) {
      return generator_->VisitObjectPattern(GetObjectPattern(), value, op_,
                                            require_object_coercible);
    } else if (IsArrayPattern()) {
      return generator_->VisitArrayPattern(GetArrayPattern(), value, op_);
    }
  }

  if (state_ == kUnresolved) {
    if (!value_in_register) {
      value = register_allocator()->NewRegister();
      builder()->StoreAccumulatorInRegister(value);
    }
    Resolve();

    // Resolve() likely clobbered the accumulator.
    value_in_register = true;
  }

  switch (lhs_type_) {
    case VARIABLE: {
      VariableProxy* proxy = reference_->AsVariableProxy();
      if (op_ == Token::INIT) {
        // Skip the hole-check when setting the value.
        did_check_lhs_hole_ = true;
      }

      HoleCheckMode hole_check_mode = did_check_lhs_hole_
                                          ? HoleCheckMode::kElided
                                          : proxy->hole_check_mode();

      if (value_in_register) builder()->LoadAccumulatorWithRegister(value);
      generator_->BuildVariableAssignment(proxy->var(), op_, hole_check_mode,
                                          lookup_hoisting_mode);
      break;
    }
    case NAMED_PROPERTY: {
      if (value_in_register) builder()->LoadAccumulatorWithRegister(value);
      FeedbackSlot slot = feedback_spec()->AddStoreICSlot(language_mode);
      builder()->StoreNamedProperty(object_, name_, feedback_index(slot),
                                    language_mode);
      break;
    }
    case KEYED_PROPERTY: {
      if (value_in_register) builder()->LoadAccumulatorWithRegister(value);
      FeedbackSlot slot = feedback_spec()->AddKeyedStoreICSlot(language_mode);
      builder()->StoreKeyedProperty(object_, key_, feedback_index(slot),
                                    language_mode);
      break;
    }
    case NAMED_SUPER_PROPERTY:
    case KEYED_SUPER_PROPERTY: {
      if (value_in_register) {
        builder()->MoveRegister(value, super_property_args_[3]);
      } else {
        builder()->StoreAccumulatorInRegister(super_property_args_[3]);
      }

      if (lhs_type_ == NAMED_SUPER_PROPERTY) {
        builder()->CallRuntime(generator_->StoreToSuperRuntimeId(),
                               super_property_args_);
      } else {
        builder()->CallRuntime(generator_->StoreKeyedToSuperRuntimeId(),
                               super_property_args_);
      }

      break;
    }
  }
}

void BytecodeGenerator::Reference::TrySetValue(
    Register value, bool require_object_coercible, BytecodeLabels* handler,
    HandlerTable::CatchPrediction prediction) {
  SimpleTryCatch try_catch(generator_, prediction);
  try_catch.BeginTry();
  SetValue(value, require_object_coercible);
  try_catch.EndTry();

  try_catch.BeginCatch();
  builder()->Jump(handler->New());
  try_catch.EndCatch();
}

BytecodeGenerator::BytecodeGenerator(CompilationInfo* info)
    : zone_(info->zone()),
      builder_(new (zone()) BytecodeArrayBuilder(
          info->isolate(), info->zone(), info->num_parameters_including_this(),
          info->scope()->num_stack_slots(), info->feedback_vector_spec(),
          info->SourcePositionRecordingMode())),
      info_(info),
      ast_string_constants_(info->isolate()->ast_string_constants()),
      closure_scope_(info->scope()),
      current_scope_(info->scope()),
      feedback_slot_cache_(new (zone()) FeedbackSlotCache(zone())),
      globals_builder_(new (zone()) GlobalDeclarationsBuilder(zone())),
      block_coverage_builder_(nullptr),
      global_declarations_(0, zone()),
      function_literals_(0, zone()),
      native_function_literals_(0, zone()),
      object_literals_(0, zone()),
      array_literals_(0, zone()),
      template_objects_(0, zone()),
      execution_control_(nullptr),
      execution_context_(nullptr),
      execution_result_(nullptr),
      incoming_new_target_or_generator_(),
      generator_jump_table_(nullptr),
      generator_state_(),
      loop_depth_(0),
      context_stack_(zone()),
      catch_prediction_(HandlerTable::UNCAUGHT) {
  DCHECK_EQ(closure_scope(), closure_scope()->GetClosureScope());
  if (info->has_source_range_map()) {
    block_coverage_builder_ = new (zone())
        BlockCoverageBuilder(zone(), builder(), info->source_range_map());
  }
}

Handle<BytecodeArray> BytecodeGenerator::FinalizeBytecode(
    Isolate* isolate, Handle<Script> script) {
  DCHECK(ThreadId::Current().Equals(isolate->thread_id()));

  AllocateDeferredConstants(isolate, script);

  if (block_coverage_builder_) {
    info()->set_coverage_info(
        isolate->factory()->NewCoverageInfo(block_coverage_builder_->slots()));
    if (FLAG_trace_block_coverage) {
      info()->coverage_info()->Print(info()->shared_info()->name());
    }
  }

  if (HasStackOverflow()) return Handle<BytecodeArray>();
  Handle<BytecodeArray> bytecode_array = builder()->ToBytecodeArray(isolate);

  if (incoming_new_target_or_generator_.is_valid()) {
    bytecode_array->set_incoming_new_target_or_generator_register(
        incoming_new_target_or_generator_);
  }

  return bytecode_array;
}

void BytecodeGenerator::AllocateDeferredConstants(Isolate* isolate,
                                                  Handle<Script> script) {
  // Build global declaration pair arrays.
  for (GlobalDeclarationsBuilder* globals_builder : global_declarations_) {
    Handle<FixedArray> declarations =
        globals_builder->AllocateDeclarations(info(), script);
    if (declarations.is_null()) return SetStackOverflow();
    builder()->SetDeferredConstantPoolEntry(
        globals_builder->constant_pool_entry(), declarations);
  }

  // Find or build shared function infos.
  for (std::pair<FunctionLiteral*, size_t> literal : function_literals_) {
    FunctionLiteral* expr = literal.first;
    Handle<SharedFunctionInfo> shared_info =
        Compiler::GetSharedFunctionInfo(expr, script, isolate);
    if (shared_info.is_null()) return SetStackOverflow();
    builder()->SetDeferredConstantPoolEntry(literal.second, shared_info);
  }

  // Find or build shared function infos for the native function templates.
  for (std::pair<NativeFunctionLiteral*, size_t> literal :
       native_function_literals_) {
    NativeFunctionLiteral* expr = literal.first;
    Handle<SharedFunctionInfo> shared_info =
        Compiler::GetSharedFunctionInfoForNative(expr->extension(),
                                                 expr->name());
    if (shared_info.is_null()) return SetStackOverflow();
    builder()->SetDeferredConstantPoolEntry(literal.second, shared_info);
  }

  // Build object literal constant properties
  for (std::pair<ObjectLiteral*, size_t> literal : object_literals_) {
    ObjectLiteral* object_literal = literal.first;
    if (object_literal->properties_count() > 0) {
      // If constant properties is an empty fixed array, we've already added it
      // to the constant pool when visiting the object literal.
      Handle<BoilerplateDescription> constant_properties =
          object_literal->GetOrBuildConstantProperties(isolate);

      builder()->SetDeferredConstantPoolEntry(literal.second,
                                              constant_properties);
    }
  }

  // Build array literal constant elements
  for (std::pair<ArrayLiteral*, size_t> literal : array_literals_) {
    ArrayLiteral* array_literal = literal.first;
    Handle<ConstantElementsPair> constant_elements =
        array_literal->GetOrBuildConstantElements(isolate);
    builder()->SetDeferredConstantPoolEntry(literal.second, constant_elements);
  }

  // Build template literals.
  for (std::pair<GetTemplateObject*, size_t> literal : template_objects_) {
    GetTemplateObject* get_template_object = literal.first;
    Handle<TemplateObjectDescription> description =
        get_template_object->GetOrBuildDescription(isolate);
    builder()->SetDeferredConstantPoolEntry(literal.second, description);
  }
}

void BytecodeGenerator::GenerateBytecode(uintptr_t stack_limit) {
  DisallowHeapAllocation no_allocation;
  DisallowHandleAllocation no_handles;
  DisallowHandleDereference no_deref;

  InitializeAstVisitor(stack_limit);

  // Initialize the incoming context.
  ContextScope incoming_context(this, closure_scope());

  // Initialize control scope.
  ControlScopeForTopLevel control(this);

  RegisterAllocationScope register_scope(this);

  AllocateTopLevelRegisters();

  if (info()->literal()->CanSuspend()) {
    BuildGeneratorPrologue();
  }

  if (closure_scope()->NeedsContext()) {
    // Push a new inner context scope for the function.
    BuildNewLocalActivationContext();
    ContextScope local_function_context(this, closure_scope());
    BuildLocalActivationContextInitialization();
    GenerateBytecodeBody();
  } else {
    GenerateBytecodeBody();
  }

  // Check that we are not falling off the end.
  DCHECK(!builder()->RequiresImplicitReturn());
}

void BytecodeGenerator::BuildGeneratorBody() {
  DCHECK(generator_object().is_valid());

  // Build non-simple parameter initialization
  const FunctionParameters* parameters = info()->literal()->parameters();
  if (parameters != nullptr) BuildFunctionParameters(*parameters);

  builder()->LoadAccumulatorWithRegister(generator_object());
  BuildYieldAccumulator(0);

  auto old_prediction = catch_prediction();
  if (IsAsyncGeneratorFunction(function_kind())) {
    set_catch_prediction(HandlerTable::ASYNC_AWAIT);
  }

  BuildFunctionBody();

  set_catch_prediction(old_prediction);
}

void BytecodeGenerator::BuildAsyncFunctionBody() {
  DCHECK(await_promise().is_valid());

  // Initialize the Promise to be returned from this function here.
  //
  // Historically, the position of the asyncFunctionPromiseCreate call has been
  // the position of the first statement in the function. Now, it's the position
  // of the function scope. This impacts some inspector tests, but I don't
  // believe in a hurtful way (the historic source position wasn't really based
  // on anything, so the fact that it's slightly earlier now doesn't matter too
  // much).
  builder()
      ->CallJSRuntime(Context::ASYNC_FUNCTION_PROMISE_CREATE_INDEX,
                      RegisterList())
      .StoreAccumulatorInRegister(await_promise());

  SimpleTryFinally release_promise_on_exit(this);
  release_promise_on_exit.BeginTry();
  {
    SimpleTryCatch reject_on_exception(this, HandlerTable::ASYNC_AWAIT);
    reject_on_exception.BeginTry();
    {
      // Build non-simple parameter initialization
      const FunctionParameters* parameters = info()->literal()->parameters();
      if (parameters != nullptr) BuildFunctionParameters(*parameters);

      BuildFunctionBody();

      if (builder()->RequiresImplicitReturn()) {
        builder()->LoadUndefined();
        execution_control()->AsyncReturnAccumulator();
      }
    }
    reject_on_exception.EndTry();
    reject_on_exception.BeginCatch();
    {
      RegisterAllocationScope register_scope(this);
      RegisterList args = register_allocator()->NewRegisterList(3);
      // Reject the Promise with no debug event (the exception is being
      // rethrown)
      builder()
          ->MoveRegister(await_promise(), args[0])
          .StoreAccumulatorInRegister(args[1])
          .LoadFalse()
          .StoreAccumulatorInRegister(args[2])
          .CallJSRuntime(Context::PROMISE_INTERNAL_REJECT_INDEX, args)
          .LoadAccumulatorWithRegister(await_promise());
      execution_control()->ReturnAccumulator();
    }
    reject_on_exception.EndCatch();
  }
  release_promise_on_exit.EndTry();
  release_promise_on_exit.BeginFinally();
  {
    builder()->CallJSRuntime(Context::ASYNC_FUNCTION_PROMISE_RELEASE_INDEX,
                             RegisterList(await_promise()));
  }
  release_promise_on_exit.EndFinally();
}

void BytecodeGenerator::BuildFunctionBody() {
  auto statements = info()->literal()->body();
  if (statements->length() == 1 && statements->at(0)->IsBlock()) {
    Block* block = statements->at(0)->AsBlock();

    if (block->scope() && block->scope()->IsFunctionVarblockScope()) {
      // Special case --- Var declarations require special handling if they
      // shadow formal parameters.
      CurrentScope current_scope(this, block->scope());
      ContextReference* environment = nullptr;
      if (block->scope()->NeedsContext()) {
        BuildNewLocalBlockContext(block->scope());
        environment = PushContextIfNeeded(block->scope());
      }
      ContextScope environment_scope(this, environment);
      DCHECK(globals_builder()->empty());

      auto declarations = block->scope()->declarations();
      if (declarations != nullptr) {
        RegisterAllocationScope register_scope(this);
        for (Declaration* declaration : *declarations) {
          RegisterAllocationScope register_scope(this);
          if (declaration->IsVariableDeclaration()) {
            VariableDeclaration* decl = declaration->AsVariableDeclaration();
            Variable* shadowed_var = decl->proxy()->var();
            Variable* parameter =
                closure_scope()->LookupLocal(decl->proxy()->raw_name());
            if (parameter != nullptr && parameter->scope() == closure_scope() &&
                !parameter->IsUnallocated() && !shadowed_var->IsUnallocated() &&
                shadowed_var->mode() == VAR) {
              DCHECK_NE(shadowed_var, parameter);
              const HoleCheckMode skip_hole_check = HoleCheckMode::kElided;
              BuildVariableLoadForAccumulatorValue(parameter, skip_hole_check);
              BuildVariableAssignment(shadowed_var, Token::ASSIGN,
                                      skip_hole_check);
              continue;
            }
          }
          Visit(declaration);
        }

        if (!globals_builder()->empty()) {
          globals_builder()->set_constant_pool_entry(
              builder()->AllocateDeferredConstantPoolEntry());
          int encoded_flags = info()->GetDeclareGlobalsFlags();

          // Emit code to declare globals.
          RegisterList args = register_allocator()->NewRegisterList(3);
          builder()
              ->LoadConstantPoolEntry(globals_builder()->constant_pool_entry())
              .StoreAccumulatorInRegister(args[0])
              .LoadLiteral(Smi::FromInt(encoded_flags))
              .StoreAccumulatorInRegister(args[1])
              .MoveRegister(Register::function_closure(), args[2])
              .CallRuntime(Runtime::kDeclareGlobalsForInterpreter, args);

          // Push and reset globals builder.
          global_declarations_.push_back(globals_builder());
          globals_builder_ = new (zone()) GlobalDeclarationsBuilder(zone());
        }
      }

      VisitStatements(block->statements());
      return;
    }
  }

  // Visit ordinary function body without shadowed declarations
  VisitStatements(statements);
}

void BytecodeGenerator::GenerateBytecodeBody() {
  // Build the arguments object if it is used.
  VisitArgumentsObject(closure_scope()->arguments());

  // Build rest arguments array if it is used.
  Variable* rest_parameter = closure_scope()->rest_parameter();
  VisitRestArgumentsArray(rest_parameter);

  // Build assignment to {.this_function} variable if it is used.
  VisitThisFunctionVariable(closure_scope()->this_function_var());

  // Build assignment to {new.target} variable if it is used.
  VisitNewTargetVariable(closure_scope()->new_target_var());

  // If the closure is a named expression, assign the name
  if (closure_scope()->is_function_scope()) {
    VisitFunctionVariable(closure_scope()->function_var());
  }

  // Create a generator object if necessary and initialize the
  // {.generator_object} variable.
  if (info()->literal()->CanSuspend()) {
    BuildGeneratorObjectVariableInitialization();
  }

  // Emit tracing call if requested to do so.
  if (FLAG_trace) builder()->CallRuntime(Runtime::kTraceEnter);

  // Emit type profile call.
  if (info()->collect_type_profile()) {
    feedback_spec()->AddTypeProfileSlot();
    int num_parameters = closure_scope()->num_parameters();
    for (int i = 0; i < num_parameters; i++) {
      Register parameter(builder()->Parameter(i));
      builder()->LoadAccumulatorWithRegister(parameter).CollectTypeProfile(
          closure_scope()->parameter(i)->initializer_position());
    }
  }

  // Visit declarations within the function scope.
  VisitDeclarations(closure_scope()->declarations());

  // Emit initializing assignments for module namespace imports (if any).
  VisitModuleNamespaceImports();

  // Perform a stack-check before the body.
  builder()->StackCheck(info()->literal()->start_position());

  if (IsGeneratorFunction(function_kind()) || IsModule(function_kind())) {
    BuildGeneratorBody();
  } else if (IsAsyncFunction(function_kind())) {
    BuildAsyncFunctionBody();
  } else {
    // Build non-simple parameter initialization
    const FunctionParameters* parameters = info()->literal()->parameters();
    if (parameters != nullptr) BuildFunctionParameters(*parameters);

    BuildFunctionBody();
  }

  // Emit an implicit return instruction in case control flow can fall off the
  // end of the function without an explicit return being present on all paths.
  if (builder()->RequiresImplicitReturn()) {
    builder()->LoadUndefined();
    BuildReturn();
  }
}

void BytecodeGenerator::AllocateTopLevelRegisters() {
  if (info()->literal()->CanSuspend()) {
    // Allocate a register for generator_state_.
    generator_state_ = register_allocator()->NewRegister();
    // Either directly use generator_object_var or allocate a new register for
    // the incoming generator object.
    incoming_new_target_or_generator_ = register_allocator()->NewRegister();
  } else if (closure_scope()->new_target_var()) {
    // Either directly use new_target_var or allocate a new register for
    // the incoming new target object.
    Variable* new_target_var = closure_scope()->new_target_var();
    if (new_target_var->location() == VariableLocation::LOCAL) {
      incoming_new_target_or_generator_ =
          GetRegisterForLocalVariable(new_target_var);
    } else {
      incoming_new_target_or_generator_ = register_allocator()->NewRegister();
    }
  }

  // For async functions, allocate a local register for the returned Promise
  if (IsAsyncFunction(info()->literal()->kind()) &&
      !IsGeneratorFunction(info()->literal()->kind())) {
    await_promise_ = register_allocator()->NewRegister();
  }
}

void BytecodeGenerator::VisitIterationHeader(IterationStatement* stmt,
                                             LoopBuilder* loop_builder) {
  VisitIterationHeader(stmt->first_suspend_id(), stmt->suspend_count(),
                       loop_builder);
}

void BytecodeGenerator::VisitIterationHeader(int first_suspend_id,
                                             int suspend_count,
                                             LoopBuilder* loop_builder) {
  // Recall that suspend_count is always zero inside ordinary (i.e.
  // non-generator) functions.
  if (suspend_count == 0) {
    loop_builder->LoopHeader();
  } else {
    loop_builder->LoopHeaderInGenerator(&generator_jump_table_,
                                        first_suspend_id, suspend_count);

    // Perform state dispatch on the generator state, assuming this is a resume.
    builder()
        ->LoadAccumulatorWithRegister(generator_state_)
        .SwitchOnSmiNoFeedback(generator_jump_table_);

    // We fall through when the generator state is not in the jump table. If we
    // are not resuming, we want to fall through to the loop body.
    // TODO(leszeks): Only generate this test for debug builds, we can skip it
    // entirely in release assuming that the generator states is always valid.
    BytecodeLabel not_resuming;
    builder()
        ->LoadLiteral(Smi::FromInt(JSGeneratorObject::kGeneratorExecuting))
        .CompareOperation(Token::Value::EQ_STRICT, generator_state_)
        .JumpIfTrue(ToBooleanMode::kAlreadyBoolean, &not_resuming);

    // Otherwise this is an error.
    builder()->Abort(BailoutReason::kInvalidJumpTableIndex);

    builder()->Bind(&not_resuming);
  }
}

void BytecodeGenerator::BuildFunctionParameters(
    const FunctionParameters& parameters) {
  int index = 0;
  auto it = parameters.begin();
  // Skip initial simple parameters, as they don't require a lexical assignment
  for (; it != parameters.end() && it->is_simple(); ++it) ++index;

  for (; it != parameters.end(); ++it) {
    const FunctionParameter& p = *it;
    CurrentScope current_scope(this, p.parameter_scope());
    CreateContextScopeIfNeeded(p.parameter_scope());
    ContextScope environment_scope(this, p.parameter_scope());
    Reference target_reference(this, p.pattern(), Token::INIT);
    Register param_reg;

    if (p.is_rest_parameter()) {
      Variable* rest_parameter = closure_scope()->rest_parameter();
      DCHECK_NOT_NULL(rest_parameter);
      Variable* var = p.var();
      if (var && var == rest_parameter) {
        // The variable is already assigned, and not destructured
        continue;
      }
      param_reg = builder()->Local(rest_parameter->index());
    } else {
      param_reg = builder()->Parameter(index++);
    }

    target_reference.PrepareForAssignment();
    builder()->LoadAccumulatorWithRegister(param_reg);

    if (p.initializer() != nullptr) {
      DCHECK(!p.is_rest_parameter());
      BytecodeLabel done;
      builder()->JumpIfNotUndefined(&done);
      builder()->SetExpressionPosition(p.initializer());
      VisitForAccumulatorValue(p.initializer());
      builder()->Bind(&done);
    }

    builder()->SetExpressionPosition(p.pattern());
    target_reference.SetValue();
  }
}

void BytecodeGenerator::BuildGeneratorPrologue() {
  DCHECK_GT(info()->literal()->suspend_count(), 0);
  DCHECK(generator_state_.is_valid());
  DCHECK(generator_object().is_valid());
  generator_jump_table_ =
      builder()->AllocateJumpTable(info()->literal()->suspend_count(), 0);

  BytecodeLabel regular_call;
  builder()
      ->LoadAccumulatorWithRegister(generator_object())
      .JumpIfUndefined(&regular_call);

  // This is a resume call. Restore the current context and the registers,
  // then perform state dispatch.
  {
    RegisterAllocationScope register_scope(this);
    Register generator_context = register_allocator()->NewRegister();
    builder()
        ->CallRuntime(Runtime::kInlineGeneratorGetContext, generator_object())
        .PushContext(generator_context)
        .RestoreGeneratorState(generator_object())
        .StoreAccumulatorInRegister(generator_state_)
        .SwitchOnSmiNoFeedback(generator_jump_table_);
  }
  // We fall through when the generator state is not in the jump table.
  // TODO(leszeks): Only generate this for debug builds.
  builder()->Abort(BailoutReason::kInvalidJumpTableIndex);

  // This is a regular call.
  builder()
      ->Bind(&regular_call)
      .LoadLiteral(Smi::FromInt(JSGeneratorObject::kGeneratorExecuting))
      .StoreAccumulatorInRegister(generator_state_);
  // Now fall through to the ordinary function prologue, after which we will run
  // into the generator object creation and other extra code inserted by the
  // parser.
}

void BytecodeGenerator::VisitBlock(Block* stmt) {
  // Visit declarations and statements.
  CurrentScope current_scope(this, stmt->scope());
  if (stmt->scope() != nullptr && stmt->scope()->NeedsContext()) {
    BuildNewLocalBlockContext(stmt->scope());
    ContextScope scope(this, stmt->scope());
    VisitBlockDeclarationsAndStatements(stmt);
  } else {
    VisitBlockDeclarationsAndStatements(stmt);
  }
}

void BytecodeGenerator::VisitBlockDeclarationsAndStatements(Block* stmt) {
  BlockBuilder block_builder(builder(), block_coverage_builder_, stmt);
  ControlScopeForBreakable execution_control(this, stmt, &block_builder);
  if (stmt->scope() != nullptr) {
    VisitDeclarations(stmt->scope()->declarations());
  }
  VisitStatements(stmt->statements());
}

void BytecodeGenerator::VisitVariableDeclaration(VariableDeclaration* decl) {
  Variable* variable = decl->proxy()->var();
  switch (variable->location()) {
    case VariableLocation::UNALLOCATED: {
      DCHECK(!variable->binding_needs_init());
      FeedbackSlot slot =
          GetCachedLoadGlobalICSlot(NOT_INSIDE_TYPEOF, variable);
      globals_builder()->AddUndefinedDeclaration(variable->raw_name(), slot);
      break;
    }
    case VariableLocation::LOCAL:
      if (variable->binding_needs_init()) {
        Register destination(builder()->Local(variable->index()));
        builder()->LoadTheHole().StoreAccumulatorInRegister(destination);
      }
      break;
    case VariableLocation::PARAMETER:
      if (variable->binding_needs_init()) {
        Register destination(builder()->Parameter(variable->index()));
        builder()->LoadTheHole().StoreAccumulatorInRegister(destination);
      }
      break;
    case VariableLocation::CONTEXT:
      if (variable->binding_needs_init()) {
        DCHECK_EQ(0, execution_context()->ContextChainDepth(variable->scope()));
        builder()->LoadTheHole().StoreContextSlot(execution_context()->reg(),
                                                  variable->index(), 0);
      }
      break;
    case VariableLocation::LOOKUP: {
      DCHECK_EQ(VAR, variable->mode());
      DCHECK(!variable->binding_needs_init());

      Register name = register_allocator()->NewRegister();

      builder()
          ->LoadLiteral(variable->raw_name())
          .StoreAccumulatorInRegister(name)
          .CallRuntime(Runtime::kDeclareEvalVar, name);
      break;
    }
    case VariableLocation::MODULE:
      if (variable->IsExport() && variable->binding_needs_init()) {
        builder()->LoadTheHole();
        BuildVariableAssignment(variable, Token::INIT, HoleCheckMode::kElided);
      }
      // Nothing to do for imports.
      break;
  }
}

void BytecodeGenerator::VisitFunctionDeclaration(FunctionDeclaration* decl) {
  Variable* variable = decl->proxy()->var();
  DCHECK(variable->mode() == LET || variable->mode() == VAR);
  switch (variable->location()) {
    case VariableLocation::UNALLOCATED: {
      FeedbackSlot slot =
          GetCachedLoadGlobalICSlot(NOT_INSIDE_TYPEOF, variable);
      FeedbackSlot literal_slot = GetCachedCreateClosureSlot(decl->fun());
      globals_builder()->AddFunctionDeclaration(variable->raw_name(), slot,
                                                literal_slot, decl->fun());
      break;
    }
    case VariableLocation::PARAMETER:
    case VariableLocation::LOCAL: {
      VisitForAccumulatorValue(decl->fun());
      BuildVariableAssignment(variable, Token::INIT, HoleCheckMode::kElided);
      break;
    }
    case VariableLocation::CONTEXT: {
      DCHECK_EQ(0, execution_context()->ContextChainDepth(variable->scope()));
      VisitForAccumulatorValue(decl->fun());
      builder()->StoreContextSlot(execution_context()->reg(), variable->index(),
                                  0);
      break;
    }
    case VariableLocation::LOOKUP: {
      RegisterList args = register_allocator()->NewRegisterList(2);
      builder()
          ->LoadLiteral(variable->raw_name())
          .StoreAccumulatorInRegister(args[0]);
      VisitForAccumulatorValue(decl->fun());
      builder()->StoreAccumulatorInRegister(args[1]).CallRuntime(
          Runtime::kDeclareEvalFunction, args);
      break;
    }
    case VariableLocation::MODULE:
      DCHECK_EQ(variable->mode(), LET);
      DCHECK(variable->IsExport());
      VisitForAccumulatorValue(decl->fun());
      BuildVariableAssignment(variable, Token::INIT, HoleCheckMode::kElided);
      break;
  }
}

void BytecodeGenerator::VisitVarExpression(VarExpression* node) {
  Token::Value op =
      IsLexicalVariableMode(node->mode()) ? Token::INIT : Token::ASSIGN;

  for (auto& element : *node) {
    Reference target_reference(this, element.pattern(), op);
    target_reference.PrepareForAssignment();

    bool require_object_coercible = true;
    if (element.initializer() != nullptr) {
      if (element.initializer()->IsAssignment() &&
          element.initializer()->AsAssignment()->target()->IsPattern()) {
        require_object_coercible = false;
      }

      builder()->SetExpressionAsStatementPosition(element.initializer());
      VisitForAccumulatorValue(element.initializer());
    } else {
      DCHECK(element.pattern()->IsVariableProxy());
      VariableProxy* proxy = element.pattern()->AsVariableProxy();

      if (node->mode() == VAR) {
        // Skip initialization for eval-declared vars and vars which shadow
        // formal parameter bindings.
        if (proxy->var()->IsLookupSlot() || current_scope()->is_eval_scope() ||
            proxy->var()->IsUnallocated()) {
          continue;
        }

        if (current_scope()->is_function_scope() ||
            current_scope()->IsFunctionVarblockScope()) {
          // TODO: Make it cheaper to find out if the variable is a shadowed
          Variable* parameter = closure_scope()->LookupLocal(proxy->raw_name());
          if (parameter != nullptr && parameter->scope() == closure_scope()) {
            // Don't initialize shadowed parameters to undefined at the
            // declaration, so that they still hold the value of the original
            // parameter binding.
            continue;
          }
        }
      }

      builder()->LoadUndefined();
    }

    target_reference.SetValue(require_object_coercible);
  }
}

void BytecodeGenerator::VisitModuleNamespaceImports() {
  if (!closure_scope()->is_module_scope()) return;

  RegisterAllocationScope register_scope(this);
  Register module_request = register_allocator()->NewRegister();

  ModuleDescriptor* descriptor = closure_scope()->AsModuleScope()->module();
  for (auto entry : descriptor->namespace_imports()) {
    builder()
        ->LoadLiteral(Smi::FromInt(entry->module_request))
        .StoreAccumulatorInRegister(module_request)
        .CallRuntime(Runtime::kGetModuleNamespace, module_request);
    Variable* var = closure_scope()->LookupLocal(entry->local_name);
    DCHECK_NOT_NULL(var);
    BuildVariableAssignment(var, Token::INIT, HoleCheckMode::kElided);
  }
}

void BytecodeGenerator::VisitDeclarations(Declaration::List* declarations) {
  RegisterAllocationScope register_scope(this);
  DCHECK(globals_builder()->empty());
  for (Declaration* decl : *declarations) {
    RegisterAllocationScope register_scope(this);
    Visit(decl);
  }
  if (globals_builder()->empty()) return;

  globals_builder()->set_constant_pool_entry(
      builder()->AllocateDeferredConstantPoolEntry());
  int encoded_flags = info()->GetDeclareGlobalsFlags();

  // Emit code to declare globals.
  RegisterList args = register_allocator()->NewRegisterList(3);
  builder()
      ->LoadConstantPoolEntry(globals_builder()->constant_pool_entry())
      .StoreAccumulatorInRegister(args[0])
      .LoadLiteral(Smi::FromInt(encoded_flags))
      .StoreAccumulatorInRegister(args[1])
      .MoveRegister(Register::function_closure(), args[2])
      .CallRuntime(Runtime::kDeclareGlobalsForInterpreter, args);

  // Push and reset globals builder.
  global_declarations_.push_back(globals_builder());
  globals_builder_ = new (zone()) GlobalDeclarationsBuilder(zone());
}

void BytecodeGenerator::VisitStatements(ZoneList<Statement*>* statements) {
  for (int i = 0; i < statements->length(); i++) {
    // Allocate an outer register allocations scope for the statement.
    RegisterAllocationScope allocation_scope(this);
    Statement* stmt = statements->at(i);
    Visit(stmt);
    if (stmt->IsJump()) break;
  }
}

void BytecodeGenerator::VisitExpressionStatement(ExpressionStatement* stmt) {
  builder()->SetStatementPosition(stmt);
  VisitForEffect(stmt->expression());
}

void BytecodeGenerator::VisitEmptyStatement(EmptyStatement* stmt) {
}

void BytecodeGenerator::VisitIfStatement(IfStatement* stmt) {
  ConditionalControlFlowBuilder conditional_builder(
      builder(), block_coverage_builder_, stmt);
  builder()->SetStatementPosition(stmt);

  if (stmt->condition()->ToBooleanIsTrue()) {
    // Generate then block unconditionally as always true.
    conditional_builder.Then();
    Visit(stmt->then_statement());
  } else if (stmt->condition()->ToBooleanIsFalse()) {
    // Generate else block unconditionally if it exists.
    if (stmt->HasElseStatement()) {
      conditional_builder.Else();
      Visit(stmt->else_statement());
    }
  } else {
    // TODO(oth): If then statement is BreakStatement or
    // ContinueStatement we can reduce number of generated
    // jump/jump_ifs here. See BasicLoops test.
    VisitForTest(stmt->condition(), conditional_builder.then_labels(),
                 conditional_builder.else_labels(), TestFallthrough::kThen);

    conditional_builder.Then();
    Visit(stmt->then_statement());

    if (stmt->HasElseStatement()) {
      conditional_builder.JumpToEnd();
      conditional_builder.Else();
      Visit(stmt->else_statement());
    }
  }
}

void BytecodeGenerator::VisitSloppyBlockFunctionStatement(
    SloppyBlockFunctionStatement* stmt) {
  Visit(stmt->statement());
}

void BytecodeGenerator::VisitContinueStatement(ContinueStatement* stmt) {
  AllocateBlockCoverageSlotIfEnabled(stmt, SourceRangeKind::kContinuation);
  builder()->SetStatementPosition(stmt);
  execution_control()->Continue(stmt->target());
}

void BytecodeGenerator::VisitBreakStatement(BreakStatement* stmt) {
  AllocateBlockCoverageSlotIfEnabled(stmt, SourceRangeKind::kContinuation);
  builder()->SetStatementPosition(stmt);
  execution_control()->Break(stmt->target());
}

void BytecodeGenerator::VisitReturnStatement(ReturnStatement* stmt) {
  AllocateBlockCoverageSlotIfEnabled(stmt, SourceRangeKind::kContinuation);
  builder()->SetStatementPosition(stmt);
  VisitForAccumulatorValue(stmt->expression());
  if (stmt->is_async_return()) {
    execution_control()->AsyncReturnAccumulator(stmt->end_position());
  } else {
    execution_control()->ReturnAccumulator(stmt->end_position());
  }
}

void BytecodeGenerator::VisitWithStatement(WithStatement* stmt) {
  builder()->SetStatementPosition(stmt);
  VisitForAccumulatorValue(stmt->expression());
  BuildNewLocalWithContext(stmt->scope());
  VisitInScope(stmt->statement(), stmt->scope());
}

void BytecodeGenerator::VisitSwitchStatement(SwitchStatement* stmt) {
  // We need this scope because we visit for register values. We have to
  // maintain a execution result scope where registers can be allocated.
  ZoneList<CaseClause*>* clauses = stmt->cases();
  SwitchBuilder switch_builder(builder(), block_coverage_builder_, stmt,
                               clauses->length());
  ControlScopeForBreakable scope(this, stmt, &switch_builder);
  int default_index = -1;

  builder()->SetStatementPosition(stmt);

  // Keep the switch value in a register until a case matches.
  Register tag = VisitForRegisterValue(stmt->tag());

  // Iterate over all cases and create nodes for label comparison.
  for (int i = 0; i < clauses->length(); i++) {
    CaseClause* clause = clauses->at(i);

    // The default is not a test, remember index.
    if (clause->is_default()) {
      default_index = i;
      continue;
    }

    // Perform label comparison as if via '===' with tag.
    VisitForAccumulatorValue(clause->label());
    builder()->CompareOperation(
        Token::Value::EQ_STRICT, tag,
        feedback_index(feedback_spec()->AddCompareICSlot()));
    switch_builder.Case(ToBooleanMode::kAlreadyBoolean, i);
  }

  if (default_index >= 0) {
    // Emit default jump if there is a default case.
    switch_builder.DefaultAt(default_index);
  } else {
    // Otherwise if we have reached here none of the cases matched, so jump to
    // the end.
    switch_builder.Break();
  }

  // Iterate over all cases and create the case bodies.
  for (int i = 0; i < clauses->length(); i++) {
    CaseClause* clause = clauses->at(i);
    switch_builder.SetCaseTarget(i, clause);
    VisitStatements(clause->statements());
  }
}

void BytecodeGenerator::VisitIterationBody(IterationStatement* stmt,
                                           LoopBuilder* loop_builder,
                                           ContextReference* env) {
  loop_builder->LoopBody();
  ControlScopeForIteration execution_control(this, env, stmt, loop_builder);
  builder()->StackCheck(stmt->position());
  Visit(stmt->body());
  loop_builder->BindContinueTarget();
}

void BytecodeGenerator::VisitDoWhileStatement(DoWhileStatement* stmt) {
  LoopBuilder loop_builder(builder(), block_coverage_builder_, stmt);
  if (stmt->cond()->ToBooleanIsFalse()) {
    VisitIterationBody(stmt, &loop_builder);
  } else if (stmt->cond()->ToBooleanIsTrue()) {
    VisitIterationHeader(stmt, &loop_builder);
    VisitIterationBody(stmt, &loop_builder);
    loop_builder.JumpToHeader(loop_depth_);
  } else {
    VisitIterationHeader(stmt, &loop_builder);
    VisitIterationBody(stmt, &loop_builder);
    builder()->SetExpressionAsStatementPosition(stmt->cond());
    BytecodeLabels loop_backbranch(zone());
    VisitForTest(stmt->cond(), &loop_backbranch, loop_builder.break_labels(),
                 TestFallthrough::kThen);
    loop_backbranch.Bind(builder());
    loop_builder.JumpToHeader(loop_depth_);
  }
}

void BytecodeGenerator::VisitWhileStatement(WhileStatement* stmt) {
  LoopBuilder loop_builder(builder(), block_coverage_builder_, stmt);

  if (stmt->cond()->ToBooleanIsFalse()) {
    // If the condition is false there is no need to generate the loop.
    return;
  }

  VisitIterationHeader(stmt, &loop_builder);
  if (!stmt->cond()->ToBooleanIsTrue()) {
    builder()->SetExpressionAsStatementPosition(stmt->cond());
    BytecodeLabels loop_body(zone());
    VisitForTest(stmt->cond(), &loop_body, loop_builder.break_labels(),
                 TestFallthrough::kThen);
    loop_body.Bind(builder());
  }
  VisitIterationBody(stmt, &loop_builder);
  loop_builder.JumpToHeader(loop_depth_);
}

void BytecodeGenerator::UpdatePerIterationEnvironment(
    const VarExpression* declarations, ContextReference* environment,
    Register new_context) {
  if (environment == nullptr) return;

  // The Per-Iteration Environment is present only if declared loop variables
  // are LET declarations which may have been captured by closures or eval.
  DCHECK_NOT_NULL(declarations);
  DCHECK_EQ(declarations->mode(), LET);

  // TODO: This whole thing could be a single runtime call inlined nicely in
  // TurboFan.
  const BoundNames& bound_names = declarations->GetBoundNames();
  if (bound_names.empty()) return;

  Scope* scope = environment->scope();
  DCHECK(scope->NeedsContext());

  DCHECK_EQ(environment, execution_context());

  Register context_reg = Register::current_context();

  BuildNewLocalBlockContext(scope);
  builder()->StoreAccumulatorInRegister(new_context);

  // Overwrite the `PREVIOUS_INDEX` context slot (which currently points to the
  // previous per-iteration context)
  builder()->LoadAccumulatorWithRegister(environment->Previous(1)->reg());
  builder()->StoreContextSlot(new_context, Context::PREVIOUS_INDEX, 0);

  for (const BoundName& binding : bound_names) {
    Variable* var = binding.var();
    DCHECK_NOT_NULL(var);
    DCHECK_EQ(scope, var->scope());

    if (var->IsUnallocated() || var->IsStackLocal()) continue;

    // Load accumulator with old value
    if (var->IsContextSlot()) {
      int depth = 0;

      BytecodeArrayBuilder::ContextSlotMutability immutable =
          (var->maybe_assigned() == kNotAssigned)
              ? BytecodeArrayBuilder::kImmutableSlot
              : BytecodeArrayBuilder::kMutableSlot;

      builder()
          ->LoadContextSlot(context_reg, var->index(), depth, immutable)
          .StoreContextSlot(new_context, var->index(), depth);
    } else {
      // Per-iteration variables must be stack or context allocated locals
      UNREACHABLE();
    }
  }

  // At this point, the current context is the new context with all values
  // copied in.
  builder()->MoveRegister(new_context, context_reg);
}

void BytecodeGenerator::PushNewIterationEnvironment(
    const VarExpression* declarations, ContextReference* environment) {
  if (environment == nullptr || declarations == nullptr) return;
  DCHECK_EQ(environment, execution_context());

  Scope* scope = environment->scope();
  if (scope == nullptr || !scope->NeedsContext()) return;

  VariableMode binding_type = declarations->mode();
  const BoundNames& bound_names = declarations->GetBoundNames();
  if (!IsLexicalVariableMode(binding_type) || bound_names.empty()) return;

  DCHECK_EQ(execution_context()->ContextChainDepth(scope), 0);

  BuildNewLocalBlockContext(scope);
  environment->MakeCurrent(this);
  VisitDeclarations(scope->declarations());
}

void BytecodeGenerator::VisitForStatement(ForStatement* stmt) {
  Scope* per_iteration_scope = stmt->per_iteration_scope();
  CurrentScope current_scope(this, per_iteration_scope);

  // Allocate an initial PerIterationScope if required.
  ContextReference* environment = nullptr;
  Register context_reg;
  if (per_iteration_scope) {
    if (per_iteration_scope->NeedsContext()) {
      context_reg = register_allocator()->NewRegister();
      BuildNewLocalBlockContext(per_iteration_scope);
      builder()->StoreAccumulatorInRegister(context_reg);
      environment = PushContextIfNeeded(per_iteration_scope);
    }
    VisitDeclarations(per_iteration_scope->declarations());
  }

  // Run the inititializer expression (if present)
  BytecodeLabel start;
  VarExpression* declarations = nullptr;
  if (stmt->init()) {
    declarations = stmt->init()->AsVarExpression();
    builder()->SetExpressionPosition(stmt->init());
    VisitForEffect(stmt->init());
    if (environment != nullptr) environment->PopContext(this);
  }

  LoopBuilder loop_builder(builder(), block_coverage_builder_, stmt);
  {
    RegisterAllocationScope register_scope(this);

    // If the condition is known to be false there is no need to generate
    // body, next or condition blocks. Init block should be generated.
    if (stmt->cond() && stmt->cond()->ToBooleanIsFalse()) {
      return PopContextIfNeeded(environment);
    }

    VisitIterationHeader(stmt, &loop_builder);

    if (environment != nullptr) {
      builder()->LoadAccumulatorWithRegister(context_reg);
      environment->MakeCurrent(this);
    }

    // Similarly, skip testing if the condition is guaranteed to be true
    if (stmt->cond() && !stmt->cond()->ToBooleanIsTrue()) {
      builder()->SetExpressionAsStatementPosition(stmt->cond());
      BytecodeLabels loop_body(zone());
      if (environment == nullptr) {
        VisitForTest(stmt->cond(), &loop_body, loop_builder.break_labels(),
                     TestFallthrough::kThen);
      } else {
        BytecodeLabels break_labels(zone());
        VisitForTest(stmt->cond(), &loop_body, &break_labels,
                     TestFallthrough::kElse);
        break_labels.Bind(builder());
        environment->PopContext(this);
        loop_builder.Break();
      }
      loop_body.Bind(builder());
    }

    VisitIterationBody(stmt, &loop_builder, OuterContextReference(environment));

    // Produce a new per-iteration environment on completion
    UpdatePerIterationEnvironment(declarations, environment, context_reg);

    if (stmt->next() != nullptr) {
      builder()->SetExpressionAsStatementPosition(stmt->next());
      VisitForEffect(stmt->next());
    }

    // TODO: Pop context outside of the loop. Currently, this breaks
    // loop peeling.
    PopContextIfNeeded(environment);
  }
  loop_builder.JumpToHeader(loop_depth_);
}

void BytecodeGenerator::VisitForInStatement(ForInStatement* stmt) {
  Expression* target = stmt->target();
  VarExpression* declarations = target->AsVarExpression();
  Expression* target_for_position =
      declarations ? declarations->pattern() : stmt->target();
  Scope* iteration_scope = stmt->iteration_scope();
  ContextReference* environment = nullptr;
  CurrentScope current_scope(this, iteration_scope);

  if (declarations != nullptr && declarations->initializer()) {
    DCHECK_EQ(VAR, declarations->mode());
    DCHECK_NULL(iteration_scope);

    // Handle legacy for-var-in behaviour:
    Reference target_reference(this, target);
    target_reference.Resolve();

    builder()->SetExpressionPosition(declarations->initializer());

    // No need to require object coercible, as the LHS cannot be a pattern.
    DCHECK(!target->IsPattern());
    const bool kRequireObjectCoercible = false;

    VisitForAccumulatorValue(declarations->initializer());
    target_reference.SetValue(kRequireObjectCoercible);
  }

  if (stmt->subject()->IsNullLiteral() ||
      stmt->subject()->IsUndefinedLiteral()) {
    // ForIn generates lots of code, skip if it wouldn't produce any effects.
    return;
  }

  BytecodeLabel subject_null_label, subject_undefined_label;
  FeedbackSlot slot = feedback_spec()->AddForInSlot();

  if (iteration_scope) {
    if (iteration_scope->NeedsContext()) {
      // TODO: Avoid allocation of this initial block context for the
      // RHS evaluation if the RHS does not contain any closures, eval calls,
      // or references to loop variables.
      BuildNewLocalBlockContext(iteration_scope);
      environment = PushContextIfNeeded(iteration_scope);
    }
    VisitDeclarations(iteration_scope->declarations());
  }

  // Prepare the state for executing ForIn (Evaluate the subject within the
  // iteration context to ensure any loop variables captured by function
  // declarations in the subject are the hole)
  builder()->SetExpressionAsStatementPosition(stmt->subject());
  VisitForAccumulatorValue(stmt->subject());

  if (environment) environment->PopContext(this);

  builder()->JumpIfUndefined(&subject_undefined_label);
  builder()->JumpIfNull(&subject_null_label);
  Register receiver = register_allocator()->NewRegister();
  builder()->ToObject(receiver);

  // Used as kRegTriple and kRegPair in ForInPrepare and ForInNext.
  RegisterList triple = register_allocator()->NewRegisterList(3);
  Register cache_length = triple[2];
  builder()->ForInEnumerate(receiver);
  builder()->ForInPrepare(triple, feedback_index(slot));

  // Set up loop counter
  Register index = register_allocator()->NewRegister();
  builder()->LoadLiteral(Smi::kZero);
  builder()->StoreAccumulatorInRegister(index);

  Register current_value;
  // The loop
  {
    LoopBuilder loop_builder(builder(), block_coverage_builder_, stmt);
    VisitIterationHeader(stmt, &loop_builder);

    builder()->SetExpressionAsStatementPosition(target_for_position);

    builder()->ForInContinue(index, cache_length);
    loop_builder.BreakIfFalse(ToBooleanMode::kAlreadyBoolean);

    builder()->ForInNext(receiver, index, triple.Truncate(2),
                         feedback_index(slot));
    loop_builder.ContinueIfUndefined();

    Reference target_reference(this, target);
    if (environment != nullptr || !target_reference.HasNoopResolve()) {
      current_value = register_allocator()->NewRegister();
      builder()->StoreAccumulatorInRegister(current_value);
    }

    PushNewIterationEnvironment(declarations, environment);

    // Result value is always a string
    target_reference.Resolve();
    const bool kRequireObjectCoercible = false;
    target_reference.SetValue(current_value, kRequireObjectCoercible);

    VisitIterationBody(stmt, &loop_builder);

    PopContextIfNeeded(environment);
    builder()->ForInStep(index);
    builder()->StoreAccumulatorInRegister(index);

    loop_builder.JumpToHeader(loop_depth_);
  }

  builder()->Bind(&subject_null_label);
  builder()->Bind(&subject_undefined_label);
}

void BytecodeGenerator::VisitForOfStatement(ForOfStatement* stmt) {
  IteratorType iterator_type = stmt->iterator_type();
  Scope* iteration_scope = stmt->iteration_scope();
  CurrentScope current_scope(this, iteration_scope);
  ContextReference* environment = nullptr;

  VarExpression* declarations = stmt->target()->AsVarExpression();
  Expression* target_for_position =
      declarations ? declarations->pattern() : stmt->target();

  // ForIn/OfHeadEvaluation TDZ scope
  if (iteration_scope != nullptr) {
    if (iteration_scope->NeedsContext()) {
      // TODO: Avoid allocation of this initial block context for the
      // RHS evaluation if the RHS does not contain any closures, eval calls,
      // or references to loop variables.
      BuildNewLocalBlockContext(iteration_scope);
      environment = PushContextIfNeeded(iteration_scope);
    }
    VisitDeclarations(iteration_scope->declarations());
  }

  Reference target_reference(this, stmt->target());

  // Perform GetIterator(node.[[Iterable]], node.[[IteratorType]])
  builder()->SetExpressionAsStatementPosition(stmt->iterable());
  BuildGetIterator(stmt->iterable(), iterator_type);
  IteratorRecord iterator = BuildIteratorRecord(iterator_type);
  if (environment) environment->PopContext(this);

  static Smi* kNormalCompletion = Smi::FromInt(0);
  static Smi* kAbruptCompletion = Smi::FromInt(1);
  Register completion = register_allocator()->NewRegister();

  SimpleTryFinally try_finally(this);
  try_finally.BeginTry();
  {
    LoopBuilder loop_builder(builder(), block_coverage_builder_, stmt);
    VisitIterationHeader(stmt, &loop_builder);

    builder()
        ->LoadLiteral(kNormalCompletion)
        .StoreAccumulatorInRegister(completion);

    // Let nextResult be ? IteratorStep(iteratorRecord)
    Register next_result = register_allocator()->NewRegister();
    builder()->SetExpressionAsStatementPosition(target_for_position);
    BuildIteratorNext(next_result, iterator, stmt->next_suspend_id());
    builder()->LoadNamedProperty(
        next_result, ast_string_constants()->done_string(),
        feedback_index(feedback_spec()->AddLoadICSlot()));
    loop_builder.BreakIfTrue(ToBooleanMode::kConvertToBoolean);
    builder()->LoadNamedProperty(
        next_result, ast_string_constants()->value_string(),
        feedback_index(feedback_spec()->AddLoadICSlot()));

    Register current_value = next_result;
    builder()->StoreAccumulatorInRegister(current_value);

    builder()
        ->LoadLiteral(kAbruptCompletion)
        .StoreAccumulatorInRegister(completion);

    // Initialize bindings and evaluate loop
    PushNewIterationEnvironment(declarations, environment);

    // If destructuring is false, then
    //     Let lhsRef be the result of evaluating lhs.
    target_reference.Resolve();

    // Initialize loop variables
    builder()->SetExpressionAsStatementPosition(target_for_position);
    target_reference.SetValue(current_value);

    VisitIterationBody(stmt, &loop_builder);

    PopContextIfNeeded(environment);
    loop_builder.JumpToHeader(loop_depth_);
  }
  try_finally.EndTry();

  try_finally.BeginFinally();
  {
    BytecodeLabel done_loop;
    builder()->LoadLiteral(kAbruptCompletion);
    builder()->CompareOperation(Token::EQ_STRICT, completion);
    builder()->JumpIfFalse(ToBooleanMode::kAlreadyBoolean, &done_loop);

    builder()->SetExpressionPosition(stmt->iterable());
    BuildIteratorClose(iterator, &try_finally, stmt->close_suspend_id());

    builder()->Bind(&done_loop);
  }
  try_finally.EndFinally();
}

void BytecodeGenerator::VisitTryCatchStatement(TryCatchStatement* stmt) {
  // Update catch prediction tracking. The updated catch_prediction value lasts
  // until the end of the try_block in the AST node, and does not apply to the
  // catch_block.
  SimpleTryCatch try_control_builder(
      this, stmt->GetCatchPrediction(catch_prediction()), stmt->scope());

  // Evaluate the try-block inside a control scope. This simulates a handler
  // that is intercepting 'throw' control commands.
  try_control_builder.BeginTry();
  {
    Visit(stmt->try_block());
  }
  try_control_builder.EndTry();

  try_control_builder.BeginCatch();
  {
    CurrentScope current_scope(this, stmt->scope());
    ContextScope catch_context(this, stmt->scope());

    // Set up bindings --- CreateCatchContext has already initialized
    // single-name bindings, so this step is performed only for patterns.
    if (stmt->pattern() != nullptr) {
      if (!stmt->pattern()->IsVariableProxy()) {
        // Catch scopes don't have declarations, but the variables must be
        // initialized anyways.
        for (const BoundName& binding : stmt->GetBoundNames()) {
          Variable* var = binding.var();
          DCHECK_NOT_NULL(var);
          DCHECK_EQ(var->mode(), LET);

          builder()->LoadTheHole();
          BuildVariableAssignment(var, Token::INIT, HoleCheckMode::kElided);
        }

        Reference catch_bindings(this, stmt->pattern(), Token::INIT);
        Variable* catch_variable = stmt->scope()->catch_variable();
        BuildVariableLoadForAccumulatorValue(catch_variable,
                                             HoleCheckMode::kElided);
        catch_bindings.SetValue();
      }
    }

    // Evaluate the catch-block.
    BuildIncrementBlockCoverageCounterIfEnabled(stmt, SourceRangeKind::kCatch);
    VisitBlock(stmt->catch_block());
  }
  try_control_builder.EndCatch();
  BuildIncrementBlockCoverageCounterIfEnabled(stmt,
                                              SourceRangeKind::kContinuation);
}

void BytecodeGenerator::VisitTryFinallyStatement(TryFinallyStatement* stmt) {
  SimpleTryFinally try_control_builder(this);

  try_control_builder.BeginTry();
  {
    // Evaluate the try-block.
    Visit(stmt->try_block());
  }
  try_control_builder.EndTry();

  try_control_builder.BeginFinally();
  {
    // Evaluate the finally-block.
    BuildIncrementBlockCoverageCounterIfEnabled(stmt,
                                                SourceRangeKind::kFinally);
    Visit(stmt->finally_block());
  }
  try_control_builder.EndFinally();
  BuildIncrementBlockCoverageCounterIfEnabled(stmt,
                                              SourceRangeKind::kContinuation);
}

void BytecodeGenerator::VisitDebuggerStatement(DebuggerStatement* stmt) {
  builder()->SetStatementPosition(stmt);
  builder()->Debugger();
}

void BytecodeGenerator::VisitFunctionLiteral(FunctionLiteral* expr) {
  DCHECK_EQ(expr->scope()->outer_scope(), current_scope());
  uint8_t flags = CreateClosureFlags::Encode(
      expr->pretenure(), closure_scope()->is_function_scope());
  size_t entry = builder()->AllocateDeferredConstantPoolEntry();
  FeedbackSlot slot = GetCachedCreateClosureSlot(expr);
  builder()->CreateClosure(entry, feedback_index(slot), flags);
  function_literals_.push_back(std::make_pair(expr, entry));
}

void BytecodeGenerator::BuildClassLiteral(ClassLiteral* expr) {
  VisitDeclarations(expr->scope()->declarations());
  Register constructor = VisitForRegisterValue(expr->constructor());
  {
    RegisterAllocationScope register_scope(this);
    RegisterList args = register_allocator()->NewRegisterList(4);
    VisitForAccumulatorValueOrTheHole(expr->extends());
    builder()
        ->StoreAccumulatorInRegister(args[0])
        .MoveRegister(constructor, args[1])
        .LoadLiteral(Smi::FromInt(expr->start_position()))
        .StoreAccumulatorInRegister(args[2])
        .LoadLiteral(Smi::FromInt(expr->end_position()))
        .StoreAccumulatorInRegister(args[3])
        .CallRuntime(Runtime::kDefineClass, args);
  }
  Register prototype = register_allocator()->NewRegister();
  builder()->StoreAccumulatorInRegister(prototype);

  if (FunctionLiteral::NeedsHomeObject(expr->constructor())) {
    // Prototype is already in the accumulator.
    FeedbackSlot slot = feedback_spec()->AddStoreICSlot(language_mode());
    builder()->StoreHomeObjectProperty(constructor, feedback_index(slot),
                                       language_mode());
  }

  VisitClassLiteralProperties(expr, constructor, prototype);
  BuildClassLiteralNameProperty(expr, constructor);
  builder()->CallRuntime(Runtime::kToFastProperties, constructor);
  // Assign to class variable.
  if (expr->class_variable() != nullptr) {
    DCHECK(expr->class_variable()->IsStackLocal() ||
           expr->class_variable()->IsContextSlot());
    BuildVariableAssignment(expr->class_variable(), Token::INIT,
                            HoleCheckMode::kElided);
  }
}

void BytecodeGenerator::VisitClassLiteral(ClassLiteral* expr) {
  CurrentScope current_scope(this, expr->scope());
  DCHECK_NOT_NULL(expr->scope());
  if (expr->scope()->NeedsContext()) {
    BuildNewLocalBlockContext(expr->scope());
    ContextScope scope(this, expr->scope());
    BuildClassLiteral(expr);
  } else {
    BuildClassLiteral(expr);
  }
}

void BytecodeGenerator::VisitClassLiteralProperties(ClassLiteral* expr,
                                                    Register constructor,
                                                    Register prototype) {
  RegisterAllocationScope register_scope(this);
  RegisterList args = register_allocator()->NewRegisterList(4);
  Register receiver = args[0], key = args[1], value = args[2], attr = args[3];

  bool attr_assigned = false;
  Register old_receiver = Register::invalid_value();

  // Create nodes to store method values into the literal.
  for (int i = 0; i < expr->properties()->length(); i++) {
    ClassLiteral::Property* property = expr->properties()->at(i);

    // Set-up receiver.
    Register new_receiver = property->is_static() ? constructor : prototype;
    if (new_receiver != old_receiver) {
      builder()->MoveRegister(new_receiver, receiver);
      old_receiver = new_receiver;
    }

    BuildLoadPropertyKey(property, key);
    if (property->is_static() && property->is_computed_name()) {
      // The static prototype property is read only. We handle the non computed
      // property name case in the parser. Since this is the only case where we
      // need to check for an own read only property we special case this so we
      // do not need to do this for every property.
      BytecodeLabel done;
      builder()
          ->LoadLiteral(ast_string_constants()->prototype_string())
          .CompareOperation(Token::Value::EQ_STRICT, key)
          .JumpIfFalse(ToBooleanMode::kAlreadyBoolean, &done)
          .CallRuntime(Runtime::kThrowStaticPrototypeError)
          .Bind(&done);
    }

    VisitForRegisterValue(property->value(), value);
    VisitSetHomeObject(value, receiver, property);

    if (!attr_assigned) {
      builder()
          ->LoadLiteral(Smi::FromInt(DONT_ENUM))
          .StoreAccumulatorInRegister(attr);
      attr_assigned = true;
    }

    switch (property->kind()) {
      case ClassLiteral::Property::METHOD: {
        DataPropertyInLiteralFlags flags = DataPropertyInLiteralFlag::kDontEnum;
        if (property->NeedsSetFunctionName()) {
          flags |= DataPropertyInLiteralFlag::kSetFunctionName;
        }

        FeedbackSlot slot =
            feedback_spec()->AddStoreDataPropertyInLiteralICSlot();
        builder()
            ->LoadAccumulatorWithRegister(value)
            .StoreDataPropertyInLiteral(receiver, key, flags,
                                        feedback_index(slot));
        break;
      }
      case ClassLiteral::Property::GETTER: {
        builder()->CallRuntime(Runtime::kDefineGetterPropertyUnchecked, args);
        break;
      }
      case ClassLiteral::Property::SETTER: {
        builder()->CallRuntime(Runtime::kDefineSetterPropertyUnchecked, args);
        break;
      }
      case ClassLiteral::Property::FIELD: {
        UNREACHABLE();
        break;
      }
    }
  }
}

void BytecodeGenerator::BuildClassLiteralNameProperty(ClassLiteral* expr,
                                                      Register literal) {
  if (!expr->has_name_static_property() &&
      expr->constructor()->has_shared_name()) {
    Runtime::FunctionId runtime_id =
        expr->has_static_computed_names()
            ? Runtime::kInstallClassNameAccessorWithCheck
            : Runtime::kInstallClassNameAccessor;
    builder()->CallRuntime(runtime_id, literal);
  }
}

void BytecodeGenerator::VisitNativeFunctionLiteral(
    NativeFunctionLiteral* expr) {
  size_t entry = builder()->AllocateDeferredConstantPoolEntry();
  FeedbackSlot slot = feedback_spec()->AddCreateClosureSlot();
  builder()->CreateClosure(entry, feedback_index(slot), NOT_TENURED);
  native_function_literals_.push_back(std::make_pair(expr, entry));
}

void BytecodeGenerator::VisitDoExpression(DoExpression* expr) {
  VisitBlock(expr->block());
  VisitVariableProxy(expr->result());
}

void BytecodeGenerator::VisitConditional(Conditional* expr) {
  ConditionalControlFlowBuilder conditional_builder(
      builder(), block_coverage_builder_, expr);

  if (expr->condition()->ToBooleanIsTrue()) {
    // Generate then block unconditionally as always true.
    conditional_builder.Then();
    VisitForAccumulatorValue(expr->then_expression());
  } else if (expr->condition()->ToBooleanIsFalse()) {
    // Generate else block unconditionally if it exists.
    conditional_builder.Else();
    VisitForAccumulatorValue(expr->else_expression());
  } else {
    VisitForTest(expr->condition(), conditional_builder.then_labels(),
                 conditional_builder.else_labels(), TestFallthrough::kThen);

    conditional_builder.Then();
    VisitForAccumulatorValue(expr->then_expression());
    conditional_builder.JumpToEnd();

    conditional_builder.Else();
    VisitForAccumulatorValue(expr->else_expression());
  }
}

void BytecodeGenerator::VisitLiteral(Literal* expr) {
  if (!execution_result()->IsEffect()) {
    const AstValue* raw_value = expr->raw_value();
    builder()->LoadLiteral(raw_value);
    if (raw_value->IsTrue() || raw_value->IsFalse()) {
      execution_result()->SetResultIsBoolean();
    }
  }
}

void BytecodeGenerator::VisitRegExpLiteral(RegExpLiteral* expr) {
  // Materialize a regular expression literal.
  builder()->CreateRegExpLiteral(
      expr->raw_pattern(), feedback_index(feedback_spec()->AddLiteralSlot()),
      expr->flags());
}

void BytecodeGenerator::VisitObjectLiteral(ObjectLiteral* expr) {
  // Fast path for the empty object literal which doesn't need an
  // AllocationSite.
  if (expr->IsEmptyObjectLiteral()) {
    DCHECK(expr->IsFastCloningSupported());
    builder()->CreateEmptyObjectLiteral();
    return;
  }

  int literal_index = feedback_index(feedback_spec()->AddLiteralSlot());
  // Deep-copy the literal boilerplate.
  uint8_t flags = CreateObjectLiteralFlags::Encode(
      expr->ComputeFlags(), expr->IsFastCloningSupported());

  Register literal = register_allocator()->NewRegister();
  size_t entry;
  // If constant properties is an empty fixed array, use a cached empty fixed
  // array to ensure it's only added to the constant pool once.
  if (expr->properties_count() == 0) {
    entry = builder()->EmptyFixedArrayConstantPoolEntry();
  } else {
    entry = builder()->AllocateDeferredConstantPoolEntry();
    object_literals_.push_back(std::make_pair(expr, entry));
  }
  // TODO(cbruni): Directly generate runtime call for literals we cannot
  // optimize once the CreateShallowObjectLiteral stub is in sync with the TF
  // optimizations.
  builder()->CreateObjectLiteral(entry, literal_index, flags, literal);

  // Store computed values into the literal.
  int property_index = 0;
  AccessorTable accessor_table(zone());
  for (; property_index < expr->properties()->length(); property_index++) {
    ObjectLiteral::Property* property = expr->properties()->at(property_index);
    if (property->is_computed_name()) break;
    if (property->IsCompileTimeValue()) continue;

    RegisterAllocationScope inner_register_scope(this);
    Literal* key = property->key()->AsLiteral();
    switch (property->kind()) {
      case ObjectLiteral::Property::SPREAD:
      case ObjectLiteral::Property::CONSTANT:
        UNREACHABLE();
      case ObjectLiteral::Property::MATERIALIZED_LITERAL:
        DCHECK(!CompileTimeValue::IsCompileTimeValue(property->value()));
      // Fall through.
      case ObjectLiteral::Property::COMPUTED: {
        // It is safe to use [[Put]] here because the boilerplate already
        // contains computed properties with an uninitialized value.
        if (key->IsStringLiteral()) {
          DCHECK(key->IsPropertyName());
          if (property->emit_store()) {
            VisitForAccumulatorValue(property->value());
            FeedbackSlot slot = feedback_spec()->AddStoreOwnICSlot();
            if (FunctionLiteral::NeedsHomeObject(property->value())) {
              RegisterAllocationScope register_scope(this);
              Register value = register_allocator()->NewRegister();
              builder()->StoreAccumulatorInRegister(value);
              builder()->StoreNamedOwnProperty(
                  literal, key->AsRawPropertyName(), feedback_index(slot));
              VisitSetHomeObject(value, literal, property);
            } else {
              builder()->StoreNamedOwnProperty(
                  literal, key->AsRawPropertyName(), feedback_index(slot));
            }
          } else {
            VisitForEffect(property->value());
          }
        } else {
          RegisterList args = register_allocator()->NewRegisterList(4);

          builder()->MoveRegister(literal, args[0]);
          VisitForRegisterValue(property->key(), args[1]);
          VisitForRegisterValue(property->value(), args[2]);
          if (property->emit_store()) {
            builder()
                ->LoadLiteral(Smi::FromEnum(LanguageMode::kSloppy))
                .StoreAccumulatorInRegister(args[3])
                .CallRuntime(Runtime::kSetProperty, args);
            Register value = args[2];
            VisitSetHomeObject(value, literal, property);
          }
        }
        break;
      }
      case ObjectLiteral::Property::PROTOTYPE: {
        // __proto__:null is handled by CreateObjectLiteral.
        if (property->IsNullPrototype()) break;
        DCHECK(property->emit_store());
        DCHECK(!property->NeedsSetFunctionName());
        RegisterList args = register_allocator()->NewRegisterList(2);
        builder()->MoveRegister(literal, args[0]);
        VisitForRegisterValue(property->value(), args[1]);
        builder()->CallRuntime(Runtime::kInternalSetPrototype, args);
        break;
      }
      case ObjectLiteral::Property::GETTER:
        if (property->emit_store()) {
          accessor_table.lookup(key)->second->getter = property;
        }
        break;
      case ObjectLiteral::Property::SETTER:
        if (property->emit_store()) {
          accessor_table.lookup(key)->second->setter = property;
        }
        break;
    }
  }

  // Define accessors, using only a single call to the runtime for each pair of
  // corresponding getters and setters.
  for (AccessorTable::Iterator it = accessor_table.begin();
       it != accessor_table.end(); ++it) {
    RegisterAllocationScope inner_register_scope(this);
    RegisterList args = register_allocator()->NewRegisterList(5);
    builder()->MoveRegister(literal, args[0]);
    VisitForRegisterValue(it->first, args[1]);
    VisitObjectLiteralAccessor(literal, it->second->getter, args[2]);
    VisitObjectLiteralAccessor(literal, it->second->setter, args[3]);
    builder()
        ->LoadLiteral(Smi::FromInt(NONE))
        .StoreAccumulatorInRegister(args[4])
        .CallRuntime(Runtime::kDefineAccessorPropertyUnchecked, args);
  }

  // Object literals have two parts. The "static" part on the left contains no
  // computed property names, and so we can compute its map ahead of time; see
  // Runtime_CreateObjectLiteralBoilerplate. The second "dynamic" part starts
  // with the first computed property name and continues with all properties to
  // its right. All the code from above initializes the static component of the
  // object literal, and arranges for the map of the result to reflect the
  // static order in which the keys appear. For the dynamic properties, we
  // compile them into a series of "SetOwnProperty" runtime calls. This will
  // preserve insertion order.
  for (; property_index < expr->properties()->length(); property_index++) {
    ObjectLiteral::Property* property = expr->properties()->at(property_index);
    RegisterAllocationScope inner_register_scope(this);

    if (property->IsPrototype()) {
      // __proto__:null is handled by CreateObjectLiteral.
      if (property->IsNullPrototype()) continue;
      DCHECK(property->emit_store());
      DCHECK(!property->NeedsSetFunctionName());
      RegisterList args = register_allocator()->NewRegisterList(2);
      builder()->MoveRegister(literal, args[0]);
      VisitForRegisterValue(property->value(), args[1]);
      builder()->CallRuntime(Runtime::kInternalSetPrototype, args);
      continue;
    }

    switch (property->kind()) {
      case ObjectLiteral::Property::CONSTANT:
      case ObjectLiteral::Property::COMPUTED:
      case ObjectLiteral::Property::MATERIALIZED_LITERAL: {
        Register key = register_allocator()->NewRegister();
        BuildLoadPropertyKey(property, key);
        Register value = VisitForRegisterValue(property->value());
        VisitSetHomeObject(value, literal, property);

        DataPropertyInLiteralFlags data_property_flags =
            DataPropertyInLiteralFlag::kNoFlags;
        if (property->NeedsSetFunctionName()) {
          data_property_flags |= DataPropertyInLiteralFlag::kSetFunctionName;
        }

        FeedbackSlot slot =
            feedback_spec()->AddStoreDataPropertyInLiteralICSlot();
        builder()
            ->LoadAccumulatorWithRegister(value)
            .StoreDataPropertyInLiteral(literal, key, data_property_flags,
                                        feedback_index(slot));
        break;
      }
      case ObjectLiteral::Property::GETTER:
      case ObjectLiteral::Property::SETTER: {
        RegisterList args = register_allocator()->NewRegisterList(4);
        builder()->MoveRegister(literal, args[0]);
        BuildLoadPropertyKey(property, args[1]);
        VisitForRegisterValue(property->value(), args[2]);
        VisitSetHomeObject(args[2], literal, property);
        builder()
            ->LoadLiteral(Smi::FromInt(NONE))
            .StoreAccumulatorInRegister(args[3]);
        Runtime::FunctionId function_id =
            property->kind() == ObjectLiteral::Property::GETTER
                ? Runtime::kDefineGetterPropertyUnchecked
                : Runtime::kDefineSetterPropertyUnchecked;
        builder()->CallRuntime(function_id, args);
        break;
      }
      case ObjectLiteral::Property::SPREAD: {
        RegisterList args = register_allocator()->NewRegisterList(2);
        builder()->MoveRegister(literal, args[0]);
        VisitForRegisterValue(property->value(), args[1]);
        builder()->CallRuntime(Runtime::kCopyDataProperties, args);
        break;
      }
      case ObjectLiteral::Property::PROTOTYPE:
        UNREACHABLE();  // Handled specially above.
        break;
    }
  }

  builder()->LoadAccumulatorWithRegister(literal);
}

void BytecodeGenerator::VisitArrayLiteral(ArrayLiteral* expr) {
  // Deep-copy the literal boilerplate.
  int literal_index = feedback_index(feedback_spec()->AddLiteralSlot());
  if (expr->is_empty()) {
    // Empty array literal fast-path.
    DCHECK(expr->IsFastCloningSupported());
    builder()->CreateEmptyArrayLiteral(literal_index);
    return;
  }

  uint8_t flags = CreateArrayLiteralFlags::Encode(
      expr->IsFastCloningSupported(), expr->ComputeFlags());
  size_t entry = builder()->AllocateDeferredConstantPoolEntry();
  builder()->CreateArrayLiteral(entry, literal_index, flags);
  array_literals_.push_back(std::make_pair(expr, entry));

  Register index, literal;

  // We'll reuse the same literal slot for all of the non-constant
  // subexpressions that use a keyed store IC.

  // Evaluate all the non-constant subexpressions and store them into the
  // newly cloned array.
  bool literal_in_accumulator = true;
  FeedbackSlot slot;
  for (int array_index = 0; array_index < expr->values()->length();
       array_index++) {
    Expression* subexpr = expr->values()->at(array_index);
    if (CompileTimeValue::IsCompileTimeValue(subexpr)) continue;

    if (literal_in_accumulator) {
      index = register_allocator()->NewRegister();
      literal = register_allocator()->NewRegister();
      builder()->StoreAccumulatorInRegister(literal);
      literal_in_accumulator = false;
    }

    if (subexpr->IsSpread()) break;

    if (slot.IsInvalid()) {
      slot = feedback_spec()->AddKeyedStoreICSlot(language_mode());
    }

    builder()
        ->LoadLiteral(Smi::FromInt(array_index))
        .StoreAccumulatorInRegister(index);
    VisitForAccumulatorValue(subexpr);
    builder()->StoreKeyedProperty(literal, index, feedback_index(slot),
                                  language_mode());
  }

  if (expr->FirstSpread() != expr->EndValue()) {
    DCHECK(!literal_in_accumulator);
    Register value = register_allocator()->NewRegister();
    DCHECK_EQ(value.index(), literal.index() + 1);
    RegisterList args(literal.index(), 2);

    struct SpreadIterator {
      ArrayLiteral* array;

      ZoneList<Expression*>::iterator begin() { return array->FirstSpread(); }
      ZoneList<Expression*>::iterator end() { return array->EndValue(); }
    };

    SpreadIterator spreads{expr};

    for (Expression* subexpr : spreads) {
      if (subexpr->IsSpread()) {
        Spread* spread = subexpr->AsSpread();
        builder()->SetExpressionPosition(spread);
        BuildGetIterator(spread->expression(), IteratorType::kNormal);
        IteratorRecord iterator = BuildIteratorRecord(IteratorType::kNormal);
        LoopBuilder loop_builder(builder(), nullptr, nullptr);
        loop_builder.LoopHeader();

        // Let nextResult be ? IteratorStep(iteratorRecord)
        Register next_result = register_allocator()->NewRegister();
        BuildIteratorNext(next_result, iterator);
        builder()->LoadNamedProperty(
            next_result, ast_string_constants()->done_string(),
            feedback_index(feedback_spec()->AddLoadICSlot()));
        loop_builder.BreakIfTrue(ToBooleanMode::kConvertToBoolean);

        loop_builder.LoopBody();
        builder()->LoadNamedProperty(
            next_result, ast_string_constants()->value_string(),
            feedback_index(feedback_spec()->AddLoadICSlot()));
        builder()->StoreAccumulatorInRegister(value);

        builder()->CallRuntime(Runtime::kAppendElement, args);
        loop_builder.BindContinueTarget();
        loop_builder.JumpToHeader(loop_depth_);
        continue;
      }

      if (subexpr->IsLiteral() &&
          subexpr->AsLiteral()->raw_value()->IsTheHole()) {
        // Peform ++<array>.length;
        // TODO: Why can't we just %_AppendElement(array, <The Hole>?)
        auto length = ast_string_constants()->length_string();
        builder()->LoadNamedProperty(
            literal, length, feedback_index(feedback_spec()->AddLoadICSlot()));
        builder()->UnaryOperation(
            Token::INC, feedback_index(feedback_spec()->AddBinaryOpICSlot()));
        builder()->StoreNamedProperty(
            literal, length,
            feedback_index(
                feedback_spec()->AddStoreICSlot(LanguageMode::kStrict)),
            LanguageMode::kStrict);
      } else {
        VisitForRegisterValue(subexpr, value);
        builder()->CallRuntime(Runtime::kAppendElement, args);
      }
    }
  }

  if (!literal_in_accumulator) {
    // Restore literal array into accumulator.
    builder()->LoadAccumulatorWithRegister(literal);
  }
}

// ObjectPattern and ArrayPattern are handled by VisitDestructuringAssignment
void BytecodeGenerator::VisitObjectPattern(ObjectPattern* pattern) {
  UNREACHABLE();
}
void BytecodeGenerator::VisitArrayPattern(ArrayPattern* pattern) {
  UNREACHABLE();
}

void BytecodeGenerator::VisitObjectPattern(ObjectPattern* pattern,
                                           Register current_value,
                                           Token::Value op,
                                           bool require_object_coercible) {
  using Element = ObjectPattern::Element;
  const ZoneVector<Element>& elements = pattern->elements();

  DCHECK(current_value.is_valid());

  if (require_object_coercible) {
    RegisterAllocationScope register_scope(this);
    // TODO: Make this a bytecode?
    BytecodeLabel not_coercible, coercible;
    builder()->LoadAccumulatorWithRegister(current_value);
    builder()->JumpIfNull(&not_coercible);
    builder()->JumpIfNotUndefined(&coercible);
    builder()->Bind(&not_coercible);
    {
      const AstRawString* property = nullptr;
      MessageTemplate::Template msg = MessageTemplate::kNonCoercible;
      Expression* item = pattern;
      for (const Element& element : elements) {
        if (element.name()->IsPropertyName()) {
          item = element.name();
          property = item->AsLiteral()->AsRawPropertyName();
          msg = MessageTemplate::kNonCoercibleWithProperty;
          break;
        }
      }
      RegisterList args = register_allocator()->NewRegisterList(2);
      builder()->SetExpressionPosition(item);
      builder()->LoadLiteral(Smi::FromInt(msg));
      builder()->StoreAccumulatorInRegister(args[0]);

      if (property) {
        builder()->LoadLiteral(property);
        builder()->StoreAccumulatorInRegister(args[1]);
      } else {
        args = args.Truncate(1);
      }

      builder()->CallRuntime(Runtime::kThrowTypeError, args);
    }
    builder()->Bind(&coercible);
  }

  RegisterList rest_args;
  int rest_argc = 0;
  if (pattern->has_rest_element()) {
    // TODO: If pattern contains no computed properties, use a constant-pool
    //       FixedArray
    //       Otherwise, use a constant pool boilerplate array and fill in the
    //       computed values only
    rest_args = register_allocator()->NewRegisterList(
        static_cast<int>(elements.size()));
    builder()->MoveRegister(current_value, rest_args[rest_argc++]);
  }

  for (const Element& element : elements) {
    RegisterAllocationScope register_scope(this);
    Reference target_reference(this, element.target(), op);
    target_reference.Resolve();

    if (element.type() == ObjectPattern::BindingType::kRestElement) {
      DCHECK_NULL(element.initializer());
      builder()->CallRuntime(Runtime::kCopyDataPropertiesWithExcludedProperties,
                             rest_args);
      target_reference.SetValue();
      continue;
    }

    // There are no elision elements in ObjectPatterns
    DCHECK_EQ(element.type(), ObjectPattern::BindingType::kElement);

    Literal* name = element.name()->AsLiteral();
    const AstRawString* raw_property_name =
        name && name->IsPropertyName() ? name->AsRawPropertyName() : nullptr;

    Register key;
    if (pattern->has_rest_element()) {
      // Load property from the right-hand-side
      VisitForAccumulatorValue(element.name());
      key = rest_args[rest_argc++];
      if (name && (name->IsStringLiteral() || name->IsNumberLiteral())) {
        builder()->StoreAccumulatorInRegister(key);
      } else {
        DCHECK(element.is_computed_name());
        builder()->ToName(key);
      }
    }

    if (raw_property_name != nullptr) {
      builder()->LoadNamedProperty(
          current_value, raw_property_name,
          feedback_index(feedback_spec()->AddLoadICSlot()));
    } else {
      if (key.is_valid()) {
        builder()->LoadAccumulatorWithRegister(key);
      } else {
        VisitForAccumulatorValue(element.name());
      }
      builder()->LoadKeyedProperty(
          current_value, feedback_index(feedback_spec()->AddKeyedLoadICSlot()));
    }

    if (element.initializer()) {
      BytecodeLabel apply_value;
      builder()->JumpIfNotUndefined(&apply_value);
      VisitForAccumulatorValue(element.initializer());
      builder()->Bind(&apply_value);
    }

    target_reference.SetValue();
  }
}

void BytecodeGenerator::BuildIteratorClose(const IteratorRecord& iterator,
                                           SimpleTryFinally* try_finally,
                                           int await_suspend_id) {
  RegisterAllocationScope register_scope(this);
  Register iterator_result = register_allocator()->NewRegister();
  Register return_method = register_allocator()->NewRegister();
  Register temp = register_allocator()->NewRegister();

  builder()->LoadTheHole().StoreAccumulatorInRegister(temp);

  BytecodeLabels done(zone());
  BytecodeLabel throw_return_not_callable;
  SimpleTryCatch try_catch(this, HandlerTable::UNCAUGHT);
  try_catch.BeginTry();
  {
    builder()
        ->LoadNamedProperty(iterator.object,
                            ast_string_constants()->return_string(),
                            feedback_index(feedback_spec()->AddLoadICSlot()))
        .JumpIfNull(done.New())
        .JumpIfUndefined(done.New())
        .StoreAccumulatorInRegister(return_method)
        .JumpIfNotCallable(&throw_return_not_callable)
        .CallProperty(return_method, RegisterList(iterator.object),
                      feedback_index(feedback_spec()->AddCallICSlot()));
    if (iterator.type == IteratorType::kAsync) {
      DCHECK_GT(await_suspend_id, 0);
      BuildAwait(await_suspend_id);
    }
  }
  try_catch.EndTry();
  try_catch.BeginCatch();
  {
    // If an exception occurred during IteratorClose, and the original loop
    // completion was not an exception, change the completion to throw
    const bool kKeepOriginalException = true;
    try_finally->RethrowAccumulator(kKeepOriginalException);
  }
  try_catch.EndCatch();

  builder()
      ->JumpIfJSReceiver(done.New())
      .StoreAccumulatorInRegister(iterator_result);

  // If an exception occurred, rethrow that error instead of the
  // "not-an-iterator-result" error
  try_finally->BreakIfRethrow();

  builder()
      ->CallRuntime(Runtime::kThrowIteratorResultNotAnObject, iterator_result)
      .Bind(&throw_return_not_callable)
      .ThrowTypeError(MessageTemplate::kReturnMethodNotCallable);
  done.Bind(builder());
}

void BytecodeGenerator::BuildIteratorClose(const IteratorRecord& iterator,
                                           BytecodeLabel* throw_not_callable,
                                           int await_suspend_id) {
  RegisterAllocationScope register_scope(this);
  Register return_method = register_allocator()->NewRegister();
  Register iterator_result = register_allocator()->NewRegister();
  BytecodeLabels done(zone());
  builder()
      ->LoadNamedProperty(iterator.object,
                          ast_string_constants()->return_string(),
                          feedback_index(feedback_spec()->AddLoadICSlot()))
      .JumpIfNull(done.New())
      .JumpIfUndefined(done.New())
      .StoreAccumulatorInRegister(return_method)
      .JumpIfNotCallable(throw_not_callable)
      .CallProperty(return_method, RegisterList(iterator.object),
                    feedback_index(feedback_spec()->AddCallICSlot()));

  if (iterator.type == IteratorType::kAsync) {
    DCHECK_GT(await_suspend_id, 0);
    BuildAwait(await_suspend_id);
  }

  builder()
      ->JumpIfJSReceiver(done.New())
      .StoreAccumulatorInRegister(iterator_result)
      .CallRuntime(Runtime::kThrowIteratorResultNotAnObject, iterator_result);
  done.Bind(builder());
}

Register BytecodeGenerator::BuildIteratorNext(Register dest,
                                              const IteratorRecord& iterator,
                                              int suspend_id) {
  RegisterAllocationScope register_scope(this);
  if (!dest.is_valid()) {
    dest = register_allocator()->NewRegister();
  }

  builder()->CallProperty(iterator.next, RegisterList(iterator.object),
                          feedback_index(feedback_spec()->AddCallICSlot()));

  if (iterator.type == IteratorType::kAsync) {
    DCHECK_GE(suspend_id, 0);
    BuildAwait(suspend_id);
  }

  BytecodeLabel is_object;
  builder()
      ->StoreAccumulatorInRegister(dest)
      .JumpIfJSReceiver(&is_object)
      .CallRuntime(Runtime::kThrowIteratorResultNotAnObject, dest)
      .Bind(&is_object);

  return dest;
}

void BytecodeGenerator::VisitArrayPattern(ArrayPattern* pattern,
                                          Register current_value,
                                          Token::Value op) {
  using Element = ArrayPattern::Element;
  const ZoneVector<Element>& elements = pattern->elements();

  RegisterAllocationScope register_scope(this);
  RegisterList iterator_and_input = register_allocator()->NewRegisterList(2);

  builder()->LoadAccumulatorWithRegister(current_value);
  BuildGetIteratorFromAccumulator(IteratorType::kNormal);
  IteratorRecord iterator = BuildIteratorRecord(iterator_and_input[0]);

  if (elements.empty()) {
    // If iteratorRecord.[[Done]] is false,
    //    return ? IteratorClose(iteratorRecord, result).
    BytecodeLabel done, throw_return_not_callable;
    BuildIteratorClose(iterator, &throw_return_not_callable);
    builder()
        ->Jump(&done)
        .Bind(&throw_return_not_callable)
        .ThrowTypeError(MessageTemplate::kReturnMethodNotCallable)
        .Bind(&done);
    return;
  }

  Register was_abrupt = register_allocator()->NewRegister();

  SimpleTryFinally try_finally(this);
  BytecodeLabel skip_iterator_close;
  try_finally.BeginTry();
  {
    for (auto it = elements.begin(); it != elements.end(); ++it) {
      const Element& element = *it;
      RegisterAllocationScope register_scope(this);
      Reference target_reference(this, element.target(), op);

      if (!target_reference.IsPattern() &&
          element.type() != ArrayPattern::BindingType::kElision &&
          !target_reference.HasNoopResolve()) {
        builder()->LoadTrue().StoreAccumulatorInRegister(was_abrupt);
        target_reference.Resolve();
      }

      BytecodeLabels apply_value(zone());

      RegisterList append_args;

      if (element.IsRestElement()) {
        append_args = register_allocator()->NewRegisterList(2);
        builder()
            ->CreateEmptyArrayLiteral(
                feedback_index(feedback_spec()->AddLiteralSlot()))
            .StoreAccumulatorInRegister(append_args[0]);
      }

      if (it != elements.begin()) {
        // If the iterator has already completed, skip invoking the iterator
        builder()->LoadAccumulatorWithRegister(iterator.next);
        builder()->JumpIfUndefined(apply_value.New());
      }

      switch (element.type()) {
        case ArrayPattern::BindingType::kElision:
        case ArrayPattern::BindingType::kElement: {
          BytecodeLabel load_value;

          builder()->LoadFalse().StoreAccumulatorInRegister(was_abrupt);
          Register result = register_allocator()->NewRegister();
          BuildIteratorNext(result, iterator);
          builder()->LoadNamedProperty(
              result, ast_string_constants()->done_string(),
              feedback_index(feedback_spec()->AddLoadICSlot()));
          builder()->JumpIfFalse(ToBooleanMode::kConvertToBoolean, &load_value);
          builder()->LoadUndefined();
          builder()->StoreAccumulatorInRegister(iterator.next);
          builder()->Jump(apply_value.New());

          if (element.IsElision()) {
            // Don't store value for elisions
            builder()->Bind(&load_value);
            apply_value.Bind(builder());
            continue;
          }

          builder()->Bind(&load_value);
          builder()->LoadNamedProperty(
              result, ast_string_constants()->value_string(),
              feedback_index(feedback_spec()->AddLoadICSlot()));

          apply_value.Bind(builder());
          builder()
              ->StoreAccumulatorInRegister(result)
              .LoadTrue()
              .StoreAccumulatorInRegister(was_abrupt);

          if (element.initializer() != nullptr) {
            BytecodeLabel have_value;
            builder()->LoadAccumulatorWithRegister(result);
            builder()->JumpIfNotUndefined(&have_value);
            builder()->SetExpressionPosition(element.initializer());
            VisitForRegisterValue(element.initializer(), result);
            builder()->Bind(&have_value);
          }

          builder()->SetExpressionPosition(element.target());
          builder()->LoadTrue().StoreAccumulatorInRegister(was_abrupt);
          target_reference.SetValue(result);
          break;
        }
        case ArrayPattern::BindingType::kRestElement: {
          builder()->LoadFalse().StoreAccumulatorInRegister(was_abrupt);
          {
            LoopBuilder loop_builder(builder(), nullptr, nullptr);
            loop_builder.LoopHeader();
            Register result = append_args[1];
            BuildIteratorNext(result, iterator);
            builder()->LoadNamedProperty(
                result, ast_string_constants()->done_string(),
                feedback_index(feedback_spec()->AddLoadICSlot()));
            loop_builder.BreakIfTrue(ToBooleanMode::kConvertToBoolean);

            loop_builder.LoopBody();
            builder()
                ->LoadNamedProperty(
                    result, ast_string_constants()->value_string(),
                    feedback_index(feedback_spec()->AddLoadICSlot()))
                .StoreAccumulatorInRegister(result)
                .CallRuntime(Runtime::kAppendElement, append_args);

            loop_builder.BindContinueTarget();
            loop_builder.JumpToHeader(loop_depth_);
          }

          // Recursively destructure the produced Rest array.
          apply_value.Bind(builder());
          // builder()->LoadTrue().StoreAccumulatorInRegister(was_abrupt);
          const bool kRequireObjectCoercible = false;
          target_reference.SetValue(append_args[0], kRequireObjectCoercible);
          break;
        }
      }
    }

    // If we arrive here, no exceptions occurred --- But we may need to close
    // the iterator if no step produced a completed iterator result.
    if (elements.back().IsRestElement()) {
      // If the last handled element was a rest element, and no exception
      // occurred, then it's safe to skip iterator close
      builder()->Jump(&skip_iterator_close);
    } else {
      builder()
          ->LoadAccumulatorWithRegister(iterator.next)
          .JumpIfUndefined(&skip_iterator_close);
    }
  }
  try_finally.EndTry();

  try_finally.BeginFinally();
  {
    BytecodeLabel done_loop;
    builder()->LoadAccumulatorWithRegister(was_abrupt);
    builder()->JumpIfFalse(ToBooleanMode::kAlreadyBoolean, &done_loop);

    builder()->SetExpressionPosition(pattern);
    BuildIteratorClose(iterator, &try_finally);

    builder()->Bind(&done_loop);
  }
  try_finally.EndFinally();
  builder()->Bind(&skip_iterator_close);
}

void BytecodeGenerator::VisitVariableProxy(VariableProxy* proxy) {
  builder()->SetExpressionPosition(proxy);
  BuildVariableLoad(proxy->var(), proxy->hole_check_mode());
}

void BytecodeGenerator::BuildVariableLoad(Variable* variable,
                                          HoleCheckMode hole_check_mode,
                                          TypeofMode typeof_mode) {
  switch (variable->location()) {
    case VariableLocation::LOCAL: {
      Register source(builder()->Local(variable->index()));
      // We need to load the variable into the accumulator, even when in a
      // VisitForRegisterScope, in order to avoid register aliasing if
      // subsequent expressions assign to the same variable.
      builder()->LoadAccumulatorWithRegister(source);
      if (hole_check_mode == HoleCheckMode::kRequired) {
        BuildThrowIfHole(variable);
      }
      break;
    }
    case VariableLocation::PARAMETER: {
      Register source;
      if (variable->IsReceiver()) {
        source = builder()->Receiver();
      } else {
        source = builder()->Parameter(variable->index());
      }
      // We need to load the variable into the accumulator, even when in a
      // VisitForRegisterScope, in order to avoid register aliasing if
      // subsequent expressions assign to the same variable.
      builder()->LoadAccumulatorWithRegister(source);
      if (hole_check_mode == HoleCheckMode::kRequired) {
        BuildThrowIfHole(variable);
      }
      break;
    }
    case VariableLocation::UNALLOCATED: {
      // The global identifier "undefined" is immutable. Everything
      // else could be reassigned. For performance, we do a pointer comparison
      // rather than checking if the raw_name is really "undefined".
      if (variable->raw_name() == ast_string_constants()->undefined_string()) {
        builder()->LoadUndefined();
      } else {
        FeedbackSlot slot = GetCachedLoadGlobalICSlot(typeof_mode, variable);
        builder()->LoadGlobal(variable->raw_name(), feedback_index(slot),
                              typeof_mode);
      }
      break;
    }
    case VariableLocation::CONTEXT: {
      int depth = execution_context()->ContextChainDepth(variable->scope());
      ContextReference* context = execution_context()->Previous(depth);
      Register context_reg;
      if (context) {
        context_reg = context->reg();
        depth = 0;
      } else {
        context_reg = execution_context()->reg();
      }

      BytecodeArrayBuilder::ContextSlotMutability immutable =
          (variable->maybe_assigned() == kNotAssigned)
              ? BytecodeArrayBuilder::kImmutableSlot
              : BytecodeArrayBuilder::kMutableSlot;

      builder()->LoadContextSlot(context_reg, variable->index(), depth,
                                 immutable);
      if (hole_check_mode == HoleCheckMode::kRequired) {
        BuildThrowIfHole(variable);
      }
      break;
    }
    case VariableLocation::LOOKUP: {
      switch (variable->mode()) {
        case DYNAMIC_LOCAL: {
          Variable* local_variable = variable->local_if_not_shadowed();
          int depth =
              execution_context()->ContextChainDepth(local_variable->scope());
          builder()->LoadLookupContextSlot(variable->raw_name(), typeof_mode,
                                           local_variable->index(), depth);
          if (hole_check_mode == HoleCheckMode::kRequired) {
            BuildThrowIfHole(variable);
          }
          break;
        }
        case DYNAMIC_GLOBAL: {
          int depth =
              closure_scope()->ContextChainLengthUntilOutermostSloppyEval();
          FeedbackSlot slot = GetCachedLoadGlobalICSlot(typeof_mode, variable);
          builder()->LoadLookupGlobalSlot(variable->raw_name(), typeof_mode,
                                          feedback_index(slot), depth);
          break;
        }
        default:
          builder()->LoadLookupSlot(variable->raw_name(), typeof_mode);
      }
      break;
    }
    case VariableLocation::MODULE: {
      int depth = execution_context()->ContextChainDepth(variable->scope());
      builder()->LoadModuleVariable(variable->index(), depth);
      if (hole_check_mode == HoleCheckMode::kRequired) {
        BuildThrowIfHole(variable);
      }
      break;
    }
  }
}

void BytecodeGenerator::BuildVariableLoadForAccumulatorValue(
    Variable* variable, HoleCheckMode hole_check_mode, TypeofMode typeof_mode) {
  ValueResultScope accumulator_result(this);
  BuildVariableLoad(variable, hole_check_mode, typeof_mode);
}

void BytecodeGenerator::BuildReturn(int source_position) {
  if (FLAG_trace) {
    RegisterAllocationScope register_scope(this);
    Register result = register_allocator()->NewRegister();
    // Runtime returns {result} value, preserving accumulator.
    builder()->StoreAccumulatorInRegister(result).CallRuntime(
        Runtime::kTraceExit, result);
  }
  if (info()->collect_type_profile()) {
    builder()->CollectTypeProfile(info()->literal()->return_position());
  }
  builder()->SetReturnPosition(source_position, info()->literal());
  builder()->Return();
}

void BytecodeGenerator::BuildAsyncReturn(int source_position) {
  RegisterAllocationScope register_scope(this);

  if (!IsAsyncGeneratorFunction(info()->literal()->kind())) {
    DCHECK(IsAsyncFunction(info()->literal()->kind()));
    RegisterList args = register_allocator()->NewRegisterList(2);
    builder()
        ->MoveRegister(await_promise(), args[0])
        .StoreAccumulatorInRegister(args[1])
        .CallJSRuntime(Context::PROMISE_RESOLVE_INDEX, args)
        .LoadAccumulatorWithRegister(args[0]);
  }
  BuildReturn();
}

void BytecodeGenerator::BuildReThrow() { builder()->ReThrow(); }

void BytecodeGenerator::BuildThrowIfHole(Variable* variable) {
  if (variable->is_this()) {
    DCHECK(variable->mode() == CONST);
    builder()->ThrowSuperNotCalledIfHole();
  } else {
    builder()->ThrowReferenceErrorIfHole(variable->raw_name());
  }
}

void BytecodeGenerator::BuildHoleCheckForVariableAssignment(Variable* variable,
                                                            Token::Value op) {
  if (variable->is_this() && variable->mode() == CONST && op == Token::INIT) {
    // Perform an initialization check for 'this'. 'this' variable is the
    // only variable able to trigger bind operations outside the TDZ
    // via 'super' calls.
    builder()->ThrowSuperAlreadyCalledIfNotHole();
  } else {
    // Perform an initialization check for let/const declared variables.
    // E.g. let x = (x = 20); is not allowed.
    DCHECK(IsLexicalVariableMode(variable->mode()));
    BuildThrowIfHole(variable);
  }
}

void BytecodeGenerator::BuildVariableAssignment(
    Variable* variable, Token::Value op, HoleCheckMode hole_check_mode,
    LookupHoistingMode lookup_hoisting_mode) {
  VariableMode mode = variable->mode();
  RegisterAllocationScope assignment_register_scope(this);
  BytecodeLabel end_label;
  switch (variable->location()) {
    case VariableLocation::PARAMETER:
    case VariableLocation::LOCAL: {
      Register destination;
      if (VariableLocation::PARAMETER == variable->location()) {
        if (variable->IsReceiver()) {
          destination = builder()->Receiver();
        } else {
          destination = builder()->Parameter(variable->index());
        }
      } else {
        destination = builder()->Local(variable->index());
      }

      if (hole_check_mode == HoleCheckMode::kRequired) {
        // Load destination to check for hole.
        Register value_temp = register_allocator()->NewRegister();
        builder()
            ->StoreAccumulatorInRegister(value_temp)
            .LoadAccumulatorWithRegister(destination);

        BuildHoleCheckForVariableAssignment(variable, op);
        builder()->LoadAccumulatorWithRegister(value_temp);
      }

      if (mode != CONST || op == Token::INIT) {
        builder()->StoreAccumulatorInRegister(destination);
      } else if (variable->throw_on_const_assignment(language_mode())) {
        builder()->CallRuntime(Runtime::kThrowConstAssignError);
      }
      break;
    }
    case VariableLocation::UNALLOCATED: {
      // TODO(ishell): consider using FeedbackSlotCache for variables here.
      FeedbackSlot slot =
          feedback_spec()->AddStoreGlobalICSlot(language_mode());
      builder()->StoreGlobal(variable->raw_name(), feedback_index(slot),
                             language_mode());
      break;
    }
    case VariableLocation::CONTEXT: {
      int depth = execution_context()->ContextChainDepth(variable->scope());
      ContextReference* context = execution_context()->Previous(depth);
      Register context_reg;

      if (context) {
        context_reg = context->reg();
        depth = 0;
      } else {
        context_reg = execution_context()->reg();
      }

      if (hole_check_mode == HoleCheckMode::kRequired) {
        // Load destination to check for hole.
        Register value_temp = register_allocator()->NewRegister();
        builder()
            ->StoreAccumulatorInRegister(value_temp)
            .LoadContextSlot(context_reg, variable->index(), depth,
                             BytecodeArrayBuilder::kMutableSlot);

        BuildHoleCheckForVariableAssignment(variable, op);
        builder()->LoadAccumulatorWithRegister(value_temp);
      }

      if (mode != CONST || op == Token::INIT) {
        builder()->StoreContextSlot(context_reg, variable->index(), depth);
      } else if (variable->throw_on_const_assignment(language_mode())) {
        builder()->CallRuntime(Runtime::kThrowConstAssignError);
      }
      break;
    }
    case VariableLocation::LOOKUP: {
      builder()->StoreLookupSlot(variable->raw_name(), language_mode(),
                                 lookup_hoisting_mode);
      break;
    }
    case VariableLocation::MODULE: {
      DCHECK(IsDeclaredVariableMode(mode));

      if (mode == CONST && op != Token::INIT) {
        builder()->CallRuntime(Runtime::kThrowConstAssignError);
        break;
      }

      // If we don't throw above, we know that we're dealing with an
      // export because imports are const and we do not generate initializing
      // assignments for them.
      DCHECK(variable->IsExport());

      int depth = execution_context()->ContextChainDepth(variable->scope());
      if (hole_check_mode == HoleCheckMode::kRequired) {
        Register value_temp = register_allocator()->NewRegister();
        builder()
            ->StoreAccumulatorInRegister(value_temp)
            .LoadModuleVariable(variable->index(), depth);
        BuildHoleCheckForVariableAssignment(variable, op);
        builder()->LoadAccumulatorWithRegister(value_temp);
      }
      builder()->StoreModuleVariable(variable->index(), depth);
      break;
    }
  }
}

void BytecodeGenerator::VisitDestructuringAssignment(Assignment* expr) {
  DCHECK(expr->target()->IsPattern());

  RegisterAllocationScope register_scope(this);
  Register current_value = register_allocator()->NewRegister();

  std::stack<Assignment*> stack;
  for (Assignment* a = expr; a && a->target()->IsPattern();
       a = a->value()->AsAssignment()) {
    stack.push(a);
  }

  DCHECK(!stack.top()->value()->IsPattern());
  VisitForRegisterValue(stack.top()->value(), current_value);

  // ObjectAssignmentPatterns perform RequireObjectCoercible(value) as
  // a first step, but we only need to do this for the right-most
  // destructuring assignment.
  //
  // Also don't perform this check if the right-most pattern is an ArrayPattern,
  // because GetIterator() will similarly throw if not coercible.
  bool require_object_coercible = true;

  while (stack.size()) {
    RegisterAllocationScope inner_scope(this);
    Assignment* curr = stack.top();
    stack.pop();
    switch (curr->target()->node_type()) {
      case AstNode::kObjectPattern: {
        VisitObjectPattern(curr->target()->AsObjectPattern(), current_value,
                           Token::ASSIGN, require_object_coercible);
        break;
      }
      case AstNode::kArrayPattern: {
        VisitArrayPattern(curr->target()->AsArrayPattern(), current_value,
                          Token::ASSIGN);
        break;
      }
      default: { UNREACHABLE(); }
    }
    require_object_coercible = false;
  }

  builder()->LoadAccumulatorWithRegister(current_value);
}

void BytecodeGenerator::VisitAssignment(Assignment* expr) {
  if (expr->target()->IsPattern()) return VisitDestructuringAssignment(expr);

  Reference reference(this, expr->target(), expr->op());

  reference.PrepareForAssignment();

  if (expr->IsCompoundAssignment()) {
    reference.GetValue();
    BinaryOperation* binop = expr->AsCompoundAssignment()->binary_operation();
    FeedbackSlot slot = feedback_spec()->AddBinaryOpICSlot();
    if (expr->value()->IsSmiLiteral()) {
      builder()->BinaryOperationSmiLiteral(
          binop->op(), expr->value()->AsLiteral()->AsSmiLiteral(),
          feedback_index(slot));
    } else {
      Register old_value = register_allocator()->NewRegister();
      builder()->StoreAccumulatorInRegister(old_value);
      VisitForAccumulatorValue(expr->value());
      builder()->BinaryOperation(binop->op(), old_value, feedback_index(slot));
    }
  } else {
    VisitForAccumulatorValue(expr->value());
  }

  builder()->SetExpressionPosition(expr);
  constexpr bool kRequireObjectCoercible = true;
  reference.SetValue(Register::invalid_value(), kRequireObjectCoercible,
                     expr->lookup_hoisting_mode());
}

void BytecodeGenerator::VisitCompoundAssignment(CompoundAssignment* expr) {
  VisitAssignment(expr);
}

// Suspends the generator to resume at |suspend_id|, with output stored in the
// accumulator. When the generator is resumed, the sent value is loaded in the
// accumulator.
void BytecodeGenerator::BuildSuspendPoint(int suspend_id) {
  RegisterList registers(0, register_allocator()->next_register_index());

  // Save context, registers, and state. Then return.
  builder()->SuspendGenerator(generator_object(), registers, suspend_id);

  builder()->SetReturnPosition(kNoSourcePosition, info()->literal());
  builder()->Return();  // Hard return (ignore any finally blocks).

  // Upon resume, we continue here.
  builder()->Bind(generator_jump_table_, suspend_id);

  // Clobbers all registers.
  builder()->RestoreGeneratorRegisters(generator_object(), registers);

  // Update state to indicate that we have finished resuming. Loop headers
  // rely on this.
  builder()
      ->LoadLiteral(Smi::FromInt(JSGeneratorObject::kGeneratorExecuting))
      .StoreAccumulatorInRegister(generator_state_);

  // When resuming execution of a generator, module or async function, the sent
  // value is in the [[input_or_debug_pos]] slot.
  builder()->CallRuntime(Runtime::kInlineGeneratorGetInputOrDebugPos,
                         generator_object());
}

void BytecodeGenerator::BuildYieldAccumulator(int suspend_id, Yield* expr) {
  DCHECK(IsGeneratorFunction(info()->literal()->kind()) ||
         IsModule(info()->literal()->kind()));
  int position = expr != nullptr ? expr->position() : kNoSourcePosition;

  const bool kIsInitialYield = suspend_id == 0;
  if (!kIsInitialYield) {
    if (IsAsyncGeneratorFunction(function_kind())) {
      // AsyncGenerator yields (with the exception of the initial yield)
      // delegate work to the AsyncGeneratorYield stub, which Awaits the operand
      // and on success, wraps the value in an IteratorResult.
      RegisterAllocationScope register_scope(this);
      RegisterList args = register_allocator()->NewRegisterList(3);
      builder()
          ->MoveRegister(generator_object(), args[0])  // generator
          .StoreAccumulatorInRegister(args[1])         // value
          .LoadBoolean(catch_prediction() != HandlerTable::ASYNC_AWAIT)
          .StoreAccumulatorInRegister(args[2])  // is_caught
          .CallRuntime(Runtime::kInlineAsyncGeneratorYield, args);
    } else {
      // Generator yields (with the exception of the initial yield) wrap the
      // value into IteratorResult.
      RegisterAllocationScope register_scope(this);
      RegisterList args = register_allocator()->NewRegisterList(2);
      builder()
          ->StoreAccumulatorInRegister(args[0])  // value
          .LoadFalse()
          .StoreAccumulatorInRegister(args[1])   // done
          .CallRuntime(Runtime::kInlineCreateIterResultObject, args);
    }
  }

  BuildSuspendPoint(suspend_id);
  // At this point, the generator has been resumed, with the received value in
  // the accumulator.
  Register input = register_allocator()->NewRegister();
  builder()->StoreAccumulatorInRegister(input).CallRuntime(
      Runtime::kInlineGeneratorGetResumeMode, generator_object());

  // Now dispatch on resume mode.
  STATIC_ASSERT(JSGeneratorObject::kNext + 1 == JSGeneratorObject::kReturn);
  BytecodeJumpTable* jump_table =
      builder()->AllocateJumpTable(2, JSGeneratorObject::kNext);

  builder()->SwitchOnSmiNoFeedback(jump_table);

  {
    // Resume with throw (switch fallthrough).
    // TODO(leszeks): Add a debug-only check that the accumulator is
    // JSGeneratorObject::kThrow.
    builder()->SetExpressionPosition(position);
    builder()->LoadAccumulatorWithRegister(input);
    builder()->Throw();
  }

  {
    // Resume with return.
    builder()->Bind(jump_table, JSGeneratorObject::kReturn);
    builder()->LoadAccumulatorWithRegister(input);
    if (IsAsyncGeneratorFunction(function_kind())) {
      execution_control()->AsyncReturnAccumulator();
    } else {
      execution_control()->ReturnAccumulator();
    }
  }

  {
    // Resume with next.
    builder()->Bind(jump_table, JSGeneratorObject::kNext);
    BuildIncrementBlockCoverageCounterIfEnabled(expr,
                                                SourceRangeKind::kContinuation);
    builder()->LoadAccumulatorWithRegister(input);
  }
}

void BytecodeGenerator::VisitYield(Yield* expr) {
  builder()->SetExpressionPosition(expr);
  VisitForAccumulatorValue(expr->expression());
  BuildYieldAccumulator(expr->suspend_id(), expr);
}

// Desugaring of (yield* iterable)
//
//   do {
//     const kNext = 0;
//     const kReturn = 1;
//     const kThrow = 2;
//
//     let output; // uninitialized
//
//     let iterator = GetIterator(iterable);
//     let input = undefined;
//     let resumeMode = kNext;
//
//     while (true) {
//       // From the generator to the iterator:
//       // Forward input according to resumeMode and obtain output.
//       switch (resumeMode) {
//         case kNext:
//           output = iterator.next(input);
//           break;
//         case kReturn:
//           let iteratorReturn = iterator.return;
//           if (IS_NULL_OR_UNDEFINED(iteratorReturn)) return input;
//           output = %_Call(iteratorReturn, iterator, input);
//           break;
//         case kThrow:
//           let iteratorThrow = iterator.throw;
//           if (IS_NULL_OR_UNDEFINED(iteratorThrow)) {
//             let iteratorReturn = iterator.return;
//             if (!IS_NULL_OR_UNDEFINED(iteratorReturn)) {
//               output = %_Call(iteratorReturn, iterator);
//               if (IS_ASYNC_GENERATOR) output = await output;
//               if (!IS_RECEIVER(output)) %ThrowIterResultNotAnObject(output);
//             }
//             throw MakeTypeError(kThrowMethodMissing);
//           }
//           output = %_Call(iteratorThrow, iterator, input);
//           break;
//       }
//
//       if (IS_ASYNC_GENERATOR) output = await output;
//       if (!IS_RECEIVER(output)) %ThrowIterResultNotAnObject(output);
//       if (output.done) break;
//
//       // From the generator to its user:
//       // Forward output, receive new input, and determine resume mode.
//       if (IS_ASYNC_GENERATOR) {
//         // AsyncGeneratorYield abstract operation awaits the operand before
//         // resolving the promise for the current AsyncGeneratorRequest.
//         %_AsyncGeneratorYield(output.value)
//       }
//       input = Suspend(output);
//       resumeMode = %GeneratorGetResumeMode();
//     }
//
//     if (resumeMode === kReturn) {
//       return output.value;
//     }
//     output.value
//   }
void BytecodeGenerator::VisitYieldStar(YieldStar* expr) {
  Register output = register_allocator()->NewRegister();
  Register resume_mode = register_allocator()->NewRegister();
  IteratorType iterator_type = IsAsyncGeneratorFunction(function_kind())
                                   ? IteratorType::kAsync
                                   : IteratorType::kNormal;

  {
    RegisterAllocationScope register_scope(this);

    RegisterList iterator_and_input = register_allocator()->NewRegisterList(2);

    Register iterator = iterator_and_input[0];

    BuildGetIterator(expr->expression(), iterator_type);
    builder()->StoreAccumulatorInRegister(iterator);
    Register input = iterator_and_input[1];
    builder()->LoadUndefined().StoreAccumulatorInRegister(input);
    builder()
        ->LoadLiteral(Smi::FromInt(JSGeneratorObject::kNext))
        .StoreAccumulatorInRegister(resume_mode);

    {
      // This loop builder does not construct counters as the loop is not
      // visible to the user, and we therefore neither pass the block coverage
      // builder nor the expression.
      //
      // YieldStar in AsyncGenerator functions includes 3 suspend points, rather
      // than 1. These are documented in the YieldStar AST node.
      LoopBuilder loop(builder(), nullptr, nullptr);
      VisitIterationHeader(expr->suspend_id(), expr->suspend_count(), &loop);

      {
        BytecodeLabels after_switch(zone());
        BytecodeJumpTable* switch_jump_table =
            builder()->AllocateJumpTable(2, 1);

        builder()
            ->LoadAccumulatorWithRegister(resume_mode)
            .SwitchOnSmiNoFeedback(switch_jump_table);

        // Fallthrough to default case.
        // TODO(tebbi): Add debug code to check that {resume_mode} really is
        // {JSGeneratorObject::kNext} in this case.
        STATIC_ASSERT(JSGeneratorObject::kNext == 0);
        {
          RegisterAllocationScope register_scope(this);
          // output = iterator.next(input);
          Register iterator_next = register_allocator()->NewRegister();
          FeedbackSlot load_slot = feedback_spec()->AddLoadICSlot();
          FeedbackSlot call_slot = feedback_spec()->AddCallICSlot();
          builder()
              ->LoadNamedProperty(iterator,
                                  ast_string_constants()->next_string(),
                                  feedback_index(load_slot))
              .StoreAccumulatorInRegister(iterator_next)
              .CallProperty(iterator_next, iterator_and_input,
                            feedback_index(call_slot))
              .Jump(after_switch.New());
        }

        STATIC_ASSERT(JSGeneratorObject::kReturn == 1);
        builder()->Bind(switch_jump_table, JSGeneratorObject::kReturn);
        {
          RegisterAllocationScope register_scope(this);
          BytecodeLabels return_input(zone());
          // Trigger return from within the inner iterator.
          Register iterator_return = register_allocator()->NewRegister();
          FeedbackSlot load_slot = feedback_spec()->AddLoadICSlot();
          FeedbackSlot call_slot = feedback_spec()->AddCallICSlot();
          builder()
              ->LoadNamedProperty(iterator,
                                  ast_string_constants()->return_string(),
                                  feedback_index(load_slot))
              .JumpIfUndefined(return_input.New())
              .JumpIfNull(return_input.New())
              .StoreAccumulatorInRegister(iterator_return)
              .CallProperty(iterator_return, iterator_and_input,
                            feedback_index(call_slot))
              .Jump(after_switch.New());

          return_input.Bind(builder());
          {
            builder()->LoadAccumulatorWithRegister(input);
            if (iterator_type == IteratorType::kAsync) {
              execution_control()->AsyncReturnAccumulator();
            } else {
              execution_control()->ReturnAccumulator();
            }
          }
        }

        STATIC_ASSERT(JSGeneratorObject::kThrow == 2);
        builder()->Bind(switch_jump_table, JSGeneratorObject::kThrow);
        {
          BytecodeLabels iterator_throw_is_undefined(zone());
          {
            RegisterAllocationScope register_scope(this);
            // If the inner iterator has a throw method, use it to trigger an
            // exception inside.
            Register iterator_throw = register_allocator()->NewRegister();
            FeedbackSlot load_slot = feedback_spec()->AddLoadICSlot();
            FeedbackSlot call_slot = feedback_spec()->AddCallICSlot();
            builder()
                ->LoadNamedProperty(iterator,
                                    ast_string_constants()->throw_string(),
                                    feedback_index(load_slot))
                .JumpIfUndefined(iterator_throw_is_undefined.New())
                .JumpIfNull(iterator_throw_is_undefined.New())
                .StoreAccumulatorInRegister(iterator_throw);
            builder()
                ->CallProperty(iterator_throw, iterator_and_input,
                               feedback_index(call_slot))
                .Jump(after_switch.New());
          }

          iterator_throw_is_undefined.Bind(builder());
          {
            RegisterAllocationScope register_scope(this);
            BytecodeLabels throw_throw_method_missing(zone());
            Register iterator_return = register_allocator()->NewRegister();
            // If iterator.throw does not exist, try to use iterator.return to
            // inform the iterator that it should stop.
            FeedbackSlot load_slot = feedback_spec()->AddLoadICSlot();
            FeedbackSlot call_slot = feedback_spec()->AddCallICSlot();
            builder()
                ->LoadNamedProperty(iterator,
                                    ast_string_constants()->return_string(),
                                    feedback_index(load_slot))
                .StoreAccumulatorInRegister(iterator_return);
            builder()
                ->JumpIfUndefined(throw_throw_method_missing.New())
                .JumpIfNull(throw_throw_method_missing.New())
                .CallProperty(iterator_return, RegisterList(iterator),
                              feedback_index(call_slot));

            if (iterator_type == IteratorType::kAsync) {
              // For async generators, await the result of the .return() call.
              BuildAwait(expr->await_iterator_close_suspend_id());
              builder()->StoreAccumulatorInRegister(output);
            }

            builder()
                ->JumpIfJSReceiver(throw_throw_method_missing.New())
                .CallRuntime(Runtime::kThrowIteratorResultNotAnObject, output);

            throw_throw_method_missing.Bind(builder());
            builder()->CallRuntime(Runtime::kThrowThrowMethodMissing);
          }
        }

        after_switch.Bind(builder());
      }

      if (iterator_type == IteratorType::kAsync) {
        // Await the result of the method invocation.
        BuildAwait(expr->await_delegated_iterator_output_suspend_id());
      }

      // Check that output is an object.
      BytecodeLabel check_if_done;
      builder()
          ->StoreAccumulatorInRegister(output)
          .JumpIfJSReceiver(&check_if_done)
          .CallRuntime(Runtime::kThrowIteratorResultNotAnObject, output);

      builder()->Bind(&check_if_done);
      // Break once output.done is true.
      builder()->LoadNamedProperty(
          output, ast_string_constants()->done_string(),
          feedback_index(feedback_spec()->AddLoadICSlot()));

      loop.BreakIfTrue(ToBooleanMode::kConvertToBoolean);

      // Suspend the current generator.
      if (iterator_type == IteratorType::kNormal) {
        builder()->LoadAccumulatorWithRegister(output);
      } else {
        RegisterAllocationScope register_scope(this);
        DCHECK_EQ(iterator_type, IteratorType::kAsync);
        // If generatorKind is async, perform AsyncGeneratorYield(output.value),
        // which will await `output.value` before resolving the current
        // AsyncGeneratorRequest's promise.
        builder()->LoadNamedProperty(
            output, ast_string_constants()->value_string(),
            feedback_index(feedback_spec()->AddLoadICSlot()));

        RegisterList args = register_allocator()->NewRegisterList(3);
        builder()
            ->MoveRegister(generator_object(), args[0])  // generator
            .StoreAccumulatorInRegister(args[1])         // value
            .LoadBoolean(catch_prediction() != HandlerTable::ASYNC_AWAIT)
            .StoreAccumulatorInRegister(args[2])  // is_caught
            .CallRuntime(Runtime::kInlineAsyncGeneratorYield, args);
      }

      BuildSuspendPoint(expr->suspend_id());
      builder()->StoreAccumulatorInRegister(input);
      builder()
          ->CallRuntime(Runtime::kInlineGeneratorGetResumeMode,
                        generator_object())
          .StoreAccumulatorInRegister(resume_mode);

      loop.BindContinueTarget();
      loop.JumpToHeader(loop_depth_);
    }
  }

  // Decide if we trigger a return or if the yield* expression should just
  // produce a value.
  BytecodeLabel completion_is_output_value;
  Register output_value = register_allocator()->NewRegister();
  builder()
      ->LoadNamedProperty(output, ast_string_constants()->value_string(),
                          feedback_index(feedback_spec()->AddLoadICSlot()))
      .StoreAccumulatorInRegister(output_value)
      .LoadLiteral(Smi::FromInt(JSGeneratorObject::kReturn))
      .CompareOperation(Token::EQ_STRICT, resume_mode)
      .JumpIfFalse(ToBooleanMode::kAlreadyBoolean, &completion_is_output_value)
      .LoadAccumulatorWithRegister(output_value);
  if (iterator_type == IteratorType::kAsync) {
    execution_control()->AsyncReturnAccumulator();
  } else {
    execution_control()->ReturnAccumulator();
  }

  builder()->Bind(&completion_is_output_value);
  BuildIncrementBlockCoverageCounterIfEnabled(expr,
                                              SourceRangeKind::kContinuation);
  builder()->LoadAccumulatorWithRegister(output_value);
}

void BytecodeGenerator::BuildAwait(int suspend_id) {
  // Rather than HandlerTable::UNCAUGHT, async functions use
  // HandlerTable::ASYNC_AWAIT to communicate that top-level exceptions are
  // transformed into promise rejections. This is necessary to prevent emitting
  // multiple debug events for the same uncaught exception. There is no point
  // in the body of an async function where catch prediction is
  // HandlerTable::UNCAUGHT.
  DCHECK(catch_prediction() != HandlerTable::UNCAUGHT);

  {
    // Await(operand) and suspend.
    RegisterAllocationScope register_scope(this);

    int await_builtin_context_index;
    RegisterList args;
    if (IsAsyncGeneratorFunction(function_kind())) {
      await_builtin_context_index =
          catch_prediction() == HandlerTable::ASYNC_AWAIT
              ? Context::ASYNC_GENERATOR_AWAIT_UNCAUGHT
              : Context::ASYNC_GENERATOR_AWAIT_CAUGHT;
      args = register_allocator()->NewRegisterList(2);
      builder()
          ->MoveRegister(generator_object(), args[0])
          .StoreAccumulatorInRegister(args[1]);
    } else {
      await_builtin_context_index =
          catch_prediction() == HandlerTable::ASYNC_AWAIT
              ? Context::ASYNC_FUNCTION_AWAIT_UNCAUGHT_INDEX
              : Context::ASYNC_FUNCTION_AWAIT_CAUGHT_INDEX;
      args = register_allocator()->NewRegisterList(3);
      builder()
          ->MoveRegister(generator_object(), args[0])
          .StoreAccumulatorInRegister(args[1])
          .MoveRegister(await_promise(), args[2]);
    }

    builder()->CallJSRuntime(await_builtin_context_index, args);
  }

  BuildSuspendPoint(suspend_id);

  Register input = register_allocator()->NewRegister();
  Register resume_mode = register_allocator()->NewRegister();

  // Now dispatch on resume mode.
  BytecodeLabel resume_next;
  builder()
      ->StoreAccumulatorInRegister(input)
      .CallRuntime(Runtime::kInlineGeneratorGetResumeMode, generator_object())
      .StoreAccumulatorInRegister(resume_mode)
      .LoadLiteral(Smi::FromInt(JSGeneratorObject::kNext))
      .CompareOperation(Token::EQ_STRICT, resume_mode)
      .JumpIfTrue(ToBooleanMode::kAlreadyBoolean, &resume_next);

  // Resume with "throw" completion (rethrow the received value).
  // TODO(leszeks): Add a debug-only check that the accumulator is
  // JSGeneratorObject::kThrow.
  builder()->LoadAccumulatorWithRegister(input).ReThrow();

  // Resume with next.
  builder()->Bind(&resume_next);
  builder()->LoadAccumulatorWithRegister(input);
}

void BytecodeGenerator::VisitAwait(Await* expr) {
  builder()->SetExpressionPosition(expr);
  VisitForAccumulatorValue(expr->expression());
  BuildAwait(expr->suspend_id());
  BuildIncrementBlockCoverageCounterIfEnabled(expr,
                                              SourceRangeKind::kContinuation);
}

void BytecodeGenerator::VisitThrow(Throw* expr) {
  AllocateBlockCoverageSlotIfEnabled(expr, SourceRangeKind::kContinuation);
  VisitForAccumulatorValue(expr->exception());
  builder()->SetExpressionPosition(expr);
  builder()->Throw();
}

void BytecodeGenerator::VisitPropertyLoad(Register obj, Property* property) {
  LhsKind property_kind = Property::GetAssignType(property);
  switch (property_kind) {
    case VARIABLE:
      UNREACHABLE();
    case NAMED_PROPERTY: {
      builder()->SetExpressionPosition(property);
      builder()->LoadNamedProperty(
          obj, property->key()->AsLiteral()->AsRawPropertyName(),
          feedback_index(feedback_spec()->AddLoadICSlot()));
      break;
    }
    case KEYED_PROPERTY: {
      VisitForAccumulatorValue(property->key());
      builder()->SetExpressionPosition(property);
      builder()->LoadKeyedProperty(
          obj, feedback_index(feedback_spec()->AddKeyedLoadICSlot()));
      break;
    }
    case NAMED_SUPER_PROPERTY:
      VisitNamedSuperPropertyLoad(property, Register::invalid_value());
      break;
    case KEYED_SUPER_PROPERTY:
      VisitKeyedSuperPropertyLoad(property, Register::invalid_value());
      break;
  }
}

void BytecodeGenerator::VisitPropertyLoadForRegister(Register obj,
                                                     Property* expr,
                                                     Register destination) {
  ValueResultScope result_scope(this);
  VisitPropertyLoad(obj, expr);
  builder()->StoreAccumulatorInRegister(destination);
}

void BytecodeGenerator::VisitNamedSuperPropertyLoad(Property* property,
                                                    Register opt_receiver_out) {
  RegisterAllocationScope register_scope(this);
  SuperPropertyReference* super_property =
      property->obj()->AsSuperPropertyReference();
  RegisterList args = register_allocator()->NewRegisterList(3);
  VisitForRegisterValue(super_property->this_var(), args[0]);
  VisitForRegisterValue(super_property->home_object(), args[1]);

  builder()->SetExpressionPosition(property);
  builder()
      ->LoadLiteral(property->key()->AsLiteral()->AsRawPropertyName())
      .StoreAccumulatorInRegister(args[2])
      .CallRuntime(Runtime::kLoadFromSuper, args);

  if (opt_receiver_out.is_valid()) {
    builder()->MoveRegister(args[0], opt_receiver_out);
  }
}

void BytecodeGenerator::VisitKeyedSuperPropertyLoad(Property* property,
                                                    Register opt_receiver_out) {
  RegisterAllocationScope register_scope(this);
  SuperPropertyReference* super_property =
      property->obj()->AsSuperPropertyReference();
  RegisterList args = register_allocator()->NewRegisterList(3);
  VisitForRegisterValue(super_property->this_var(), args[0]);
  VisitForRegisterValue(super_property->home_object(), args[1]);
  VisitForRegisterValue(property->key(), args[2]);

  builder()->SetExpressionPosition(property);
  builder()->CallRuntime(Runtime::kLoadKeyedFromSuper, args);

  if (opt_receiver_out.is_valid()) {
    builder()->MoveRegister(args[0], opt_receiver_out);
  }
}

void BytecodeGenerator::VisitProperty(Property* expr) {
  LhsKind property_kind = Property::GetAssignType(expr);
  if (property_kind != NAMED_SUPER_PROPERTY &&
      property_kind != KEYED_SUPER_PROPERTY) {
    Register obj = VisitForRegisterValue(expr->obj());
    VisitPropertyLoad(obj, expr);
  } else {
    VisitPropertyLoad(Register::invalid_value(), expr);
  }
}

void BytecodeGenerator::VisitArguments(ZoneList<Expression*>* args,
                                       RegisterList* arg_regs) {
  // Visit arguments.
  for (int i = 0; i < static_cast<int>(args->length()); i++) {
    VisitAndPushIntoRegisterList(args->at(i), arg_regs);
  }
}

void BytecodeGenerator::VisitCall(Call* expr) {
  Expression* callee_expr = expr->expression();
  Call::CallType call_type = expr->GetCallType();

  if (call_type == Call::SUPER_CALL) {
    return VisitCallSuper(expr);
  }

  // Grow the args list as we visit receiver / arguments to avoid allocating all
  // the registers up-front. Otherwise these registers are unavailable during
  // receiver / argument visiting and we can end up with memory leaks due to
  // registers keeping objects alive.
  Register callee = register_allocator()->NewRegister();
  RegisterList args = register_allocator()->NewGrowableRegisterList();

  bool implicit_undefined_receiver = false;
  // When a call contains a spread, a Call AST node is only created if there is
  // exactly one spread, and it is the last argument.
  bool is_spread_call = expr->only_last_arg_is_spread();

  // TODO(petermarshall): We have a lot of call bytecodes that are very similar,
  // see if we can reduce the number by adding a separate argument which
  // specifies the call type (e.g., property, spread, tailcall, etc.).

  // Prepare the callee and the receiver to the function call. This depends on
  // the semantics of the underlying call type.
  switch (call_type) {
    case Call::NAMED_PROPERTY_CALL:
    case Call::KEYED_PROPERTY_CALL: {
      Property* property = callee_expr->AsProperty();
      VisitAndPushIntoRegisterList(property->obj(), &args);
      VisitPropertyLoadForRegister(args.last_register(), property, callee);
      break;
    }
    case Call::GLOBAL_CALL: {
      // Receiver is undefined for global calls.
      if (!is_spread_call) {
        implicit_undefined_receiver = true;
      } else {
        // TODO(leszeks): There's no special bytecode for tail calls or spread
        // calls with an undefined receiver, so just push undefined ourselves.
        BuildPushUndefinedIntoRegisterList(&args);
      }
      // Load callee as a global variable.
      VariableProxy* proxy = callee_expr->AsVariableProxy();
      BuildVariableLoadForAccumulatorValue(proxy->var(),
                                           proxy->hole_check_mode());
      builder()->StoreAccumulatorInRegister(callee);
      break;
    }
    case Call::WITH_CALL: {
      Register receiver = register_allocator()->GrowRegisterList(&args);
      DCHECK(callee_expr->AsVariableProxy()->var()->IsLookupSlot());
      {
        RegisterAllocationScope inner_register_scope(this);
        Register name = register_allocator()->NewRegister();

        // Call %LoadLookupSlotForCall to get the callee and receiver.
        DCHECK(Register::AreContiguous(callee, receiver));
        RegisterList result_pair(callee.index(), 2);
        USE(receiver);

        Variable* variable = callee_expr->AsVariableProxy()->var();
        builder()
            ->LoadLiteral(variable->raw_name())
            .StoreAccumulatorInRegister(name)
            .CallRuntimeForPair(Runtime::kLoadLookupSlotForCall, name,
                                result_pair);
      }
      break;
    }
    case Call::OTHER_CALL: {
      // Receiver is undefined for other calls.
      if (!is_spread_call) {
        implicit_undefined_receiver = true;
      } else {
        // TODO(leszeks): There's no special bytecode for tail calls or spread
        // calls with an undefined receiver, so just push undefined ourselves.
        BuildPushUndefinedIntoRegisterList(&args);
      }
      VisitForRegisterValue(callee_expr, callee);
      break;
    }
    case Call::NAMED_SUPER_PROPERTY_CALL: {
      Register receiver = register_allocator()->GrowRegisterList(&args);
      Property* property = callee_expr->AsProperty();
      VisitNamedSuperPropertyLoad(property, receiver);
      builder()->StoreAccumulatorInRegister(callee);
      break;
    }
    case Call::KEYED_SUPER_PROPERTY_CALL: {
      Register receiver = register_allocator()->GrowRegisterList(&args);
      Property* property = callee_expr->AsProperty();
      VisitKeyedSuperPropertyLoad(property, receiver);
      builder()->StoreAccumulatorInRegister(callee);
      break;
    }
    case Call::SUPER_CALL:
      UNREACHABLE();
      break;
  }

  // Evaluate all arguments to the function call and store in sequential args
  // registers.
  VisitArguments(expr->arguments(), &args);
  int reciever_arg_count = implicit_undefined_receiver ? 0 : 1;
  CHECK_EQ(reciever_arg_count + expr->arguments()->length(),
           args.register_count());

  // Resolve callee for a potential direct eval call. This block will mutate the
  // callee value.
  if (expr->is_possibly_eval() && expr->arguments()->length() > 0) {
    RegisterAllocationScope inner_register_scope(this);
    // Set up arguments for ResolvePossiblyDirectEval by copying callee, source
    // strings and function closure, and loading language and
    // position.
    Register first_arg = args[reciever_arg_count];
    RegisterList runtime_call_args = register_allocator()->NewRegisterList(6);
    builder()
        ->MoveRegister(callee, runtime_call_args[0])
        .MoveRegister(first_arg, runtime_call_args[1])
        .MoveRegister(Register::function_closure(), runtime_call_args[2])
        .LoadLiteral(Smi::FromEnum(language_mode()))
        .StoreAccumulatorInRegister(runtime_call_args[3])
        .LoadLiteral(Smi::FromInt(current_scope()->start_position()))
        .StoreAccumulatorInRegister(runtime_call_args[4])
        .LoadLiteral(Smi::FromInt(expr->position()))
        .StoreAccumulatorInRegister(runtime_call_args[5]);

    // Call ResolvePossiblyDirectEval and modify the callee.
    builder()
        ->CallRuntime(Runtime::kResolvePossiblyDirectEval, runtime_call_args)
        .StoreAccumulatorInRegister(callee);
  }

  builder()->SetExpressionPosition(expr);

  int feedback_slot_index = feedback_index(feedback_spec()->AddCallICSlot());

  if (is_spread_call) {
    DCHECK(!implicit_undefined_receiver);
    builder()->CallWithSpread(callee, args, feedback_slot_index);
  } else if (call_type == Call::NAMED_PROPERTY_CALL ||
             call_type == Call::KEYED_PROPERTY_CALL) {
    DCHECK(!implicit_undefined_receiver);
    builder()->CallProperty(callee, args, feedback_slot_index);
  } else if (implicit_undefined_receiver) {
    builder()->CallUndefinedReceiver(callee, args, feedback_slot_index);
  } else {
    builder()->CallAnyReceiver(callee, args, feedback_slot_index);
  }
}

void BytecodeGenerator::VisitCallSuper(Call* expr) {
  RegisterAllocationScope register_scope(this);
  SuperCallReference* super = expr->expression()->AsSuperCallReference();
  Variable* this_var = super->this_var()->var();

  // Prepare the constructor to the super call.
  VisitForAccumulatorValue(super->this_function_var());
  Register constructor = register_allocator()->NewRegister();
  builder()->GetSuperConstructor(constructor);

  ZoneList<Expression*>* args = expr->arguments();
  RegisterList args_regs = register_allocator()->NewGrowableRegisterList();
  VisitArguments(args, &args_regs);
  // The new target is loaded into the accumulator from the
  // {new.target} variable.
  VisitForAccumulatorValue(super->new_target_var());
  builder()->SetExpressionPosition(expr);

  int feedback_slot_index = feedback_index(feedback_spec()->AddCallICSlot());

  // When a super call contains a spread, a CallSuper AST node is only created
  // if there is exactly one spread, and it is the last argument.
  if (expr->only_last_arg_is_spread()) {
    builder()->ConstructWithSpread(constructor, args_regs, feedback_slot_index);
  } else {
    // Call construct.
    // TODO(turbofan): For now we do gather feedback on super constructor
    // calls, utilizing the existing machinery to inline the actual call
    // target and the JSCreate for the implicit receiver allocation. This
    // is not an ideal solution for super constructor calls, but it gets
    // the job done for now. In the long run we might want to revisit this
    // and come up with a better way.
    builder()->Construct(constructor, args_regs, feedback_slot_index);
  }

  // Return ? thisER.BindThisValue(result).
  // TODO: Eliminate this hole-check when possible.
  Register result = args_regs.register_count()
                        ? args_regs[0]
                        : register_allocator()->NewRegister();
  builder()->StoreAccumulatorInRegister(result);

  // Throw if super() has already been called
  BuildVariableLoad(this_var, HoleCheckMode::kElided);
  builder()->ThrowSuperAlreadyCalledIfNotHole();

  builder()->LoadAccumulatorWithRegister(result);
  BuildVariableAssignment(this_var, Token::INIT, HoleCheckMode::kElided);
}

void BytecodeGenerator::VisitCallNew(CallNew* expr) {
  Register constructor = VisitForRegisterValue(expr->expression());
  RegisterList args = register_allocator()->NewGrowableRegisterList();
  VisitArguments(expr->arguments(), &args);

  // The accumulator holds new target which is the same as the
  // constructor for CallNew.
  builder()->SetExpressionPosition(expr);
  builder()->LoadAccumulatorWithRegister(constructor);

  int feedback_slot_index = feedback_index(feedback_spec()->AddCallICSlot());
  if (expr->only_last_arg_is_spread()) {
    builder()->ConstructWithSpread(constructor, args, feedback_slot_index);
  } else {
    builder()->Construct(constructor, args, feedback_slot_index);
  }
}

void BytecodeGenerator::VisitCallRuntime(CallRuntime* expr) {
  if (expr->is_jsruntime()) {
    RegisterList args = register_allocator()->NewGrowableRegisterList();
    VisitArguments(expr->arguments(), &args);
    builder()->CallJSRuntime(expr->context_index(), args);
  } else {
    // Evaluate all arguments to the runtime call.
    RegisterList args = register_allocator()->NewGrowableRegisterList();
    VisitArguments(expr->arguments(), &args);
    Runtime::FunctionId function_id = expr->function()->function_id;
    builder()->CallRuntime(function_id, args);
  }
}

void BytecodeGenerator::VisitVoid(UnaryOperation* expr) {
  VisitForEffect(expr->expression());
  builder()->LoadUndefined();
}

void BytecodeGenerator::VisitForTypeOfValue(Expression* expr) {
  if (expr->IsVariableProxy()) {
    // Typeof does not throw a reference error on global variables, hence we
    // perform a non-contextual load in case the operand is a variable proxy.
    VariableProxy* proxy = expr->AsVariableProxy();
    BuildVariableLoadForAccumulatorValue(proxy->var(), proxy->hole_check_mode(),
                                         INSIDE_TYPEOF);
  } else {
    VisitForAccumulatorValue(expr);
  }
}

void BytecodeGenerator::VisitTypeOf(UnaryOperation* expr) {
  VisitForTypeOfValue(expr->expression());
  builder()->TypeOf();
}

void BytecodeGenerator::VisitNot(UnaryOperation* expr) {
  if (execution_result()->IsEffect()) {
    VisitForEffect(expr->expression());
  } else if (execution_result()->IsTest()) {
    // No actual logical negation happening, we just swap the control flow, by
    // swapping the target labels and the fallthrough branch, and visit in the
    // same test result context.
    TestResultScope* test_result = execution_result()->AsTest();
    test_result->InvertControlFlow();
    VisitInSameTestExecutionScope(expr->expression());
  } else {
    TypeHint type_hint = VisitForAccumulatorValue(expr->expression());
    builder()->LogicalNot(ToBooleanModeFromTypeHint(type_hint));
    // Always returns a boolean value.
    execution_result()->SetResultIsBoolean();
  }
}

void BytecodeGenerator::VisitUnaryOperation(UnaryOperation* expr) {
  switch (expr->op()) {
    case Token::Value::NOT:
      VisitNot(expr);
      break;
    case Token::Value::TYPEOF:
      VisitTypeOf(expr);
      break;
    case Token::Value::VOID:
      VisitVoid(expr);
      break;
    case Token::Value::DELETE:
      VisitDelete(expr);
      break;
    case Token::Value::ADD:
    case Token::Value::SUB:
    case Token::Value::BIT_NOT:
      VisitForAccumulatorValue(expr->expression());
      builder()->SetExpressionPosition(expr);
      builder()->UnaryOperation(
          expr->op(), feedback_index(feedback_spec()->AddBinaryOpICSlot()));
      break;
    default:
      UNREACHABLE();
  }
}

void BytecodeGenerator::VisitDelete(UnaryOperation* expr) {
  if (expr->expression()->IsProperty()) {
    // Delete of an object property is allowed both in sloppy
    // and strict modes.
    Property* property = expr->expression()->AsProperty();
    Register object = VisitForRegisterValue(property->obj());
    VisitForAccumulatorValue(property->key());
    builder()->Delete(object, language_mode());
  } else if (expr->expression()->IsVariableProxy()) {
    // Delete of an unqualified identifier is allowed in sloppy mode but is
    // not allowed in strict mode. Deleting 'this' and 'new.target' is allowed
    // in both modes.
    VariableProxy* proxy = expr->expression()->AsVariableProxy();
    DCHECK(is_sloppy(language_mode()) || proxy->is_this() ||
           proxy->is_new_target());
    if (proxy->is_this() || proxy->is_new_target()) {
      builder()->LoadTrue();
    } else {
      Variable* variable = proxy->var();
      switch (variable->location()) {
        case VariableLocation::PARAMETER:
        case VariableLocation::LOCAL:
        case VariableLocation::CONTEXT: {
          // Deleting local var/let/const, context variables, and arguments
          // does not have any effect.
          builder()->LoadFalse();
          break;
        }
        case VariableLocation::UNALLOCATED:
        // TODO(adamk): Falling through to the runtime results in correct
        // behavior, but does unnecessary context-walking (since scope
        // analysis has already proven that the variable doesn't exist in
        // any non-global scope). Consider adding a DeleteGlobal bytecode
        // that knows how to deal with ScriptContexts as well as global
        // object properties.
        case VariableLocation::LOOKUP: {
          Register name_reg = register_allocator()->NewRegister();
          builder()
              ->LoadLiteral(variable->raw_name())
              .StoreAccumulatorInRegister(name_reg)
              .CallRuntime(Runtime::kDeleteLookupSlot, name_reg);
          break;
        }
        default:
          UNREACHABLE();
      }
    }
  } else {
    // Delete of an unresolvable reference returns true.
    VisitForEffect(expr->expression());
    builder()->LoadTrue();
  }
}

void BytecodeGenerator::VisitCountOperation(CountOperation* expr) {
  DCHECK(expr->expression()->IsValidReferenceExpression());

  // Left-hand side can only be a property, a global or a variable slot.
  Property* property = expr->expression()->AsProperty();
  LhsKind assign_type = Property::GetAssignType(property);

  bool is_postfix = expr->is_postfix() && !execution_result()->IsEffect();

  // Evaluate LHS expression and get old value.
  Register object, key, old_value;
  RegisterList super_property_args;
  const AstRawString* name;
  switch (assign_type) {
    case VARIABLE: {
      VariableProxy* proxy = expr->expression()->AsVariableProxy();
      BuildVariableLoadForAccumulatorValue(proxy->var(),
                                           proxy->hole_check_mode());
      break;
    }
    case NAMED_PROPERTY: {
      object = VisitForRegisterValue(property->obj());
      name = property->key()->AsLiteral()->AsRawPropertyName();
      builder()->LoadNamedProperty(
          object, name, feedback_index(feedback_spec()->AddLoadICSlot()));
      break;
    }
    case KEYED_PROPERTY: {
      object = VisitForRegisterValue(property->obj());
      // Use visit for accumulator here since we need the key in the accumulator
      // for the LoadKeyedProperty.
      key = register_allocator()->NewRegister();
      VisitForAccumulatorValue(property->key());
      builder()->StoreAccumulatorInRegister(key).LoadKeyedProperty(
          object, feedback_index(feedback_spec()->AddKeyedLoadICSlot()));
      break;
    }
    case NAMED_SUPER_PROPERTY: {
      super_property_args = register_allocator()->NewRegisterList(4);
      RegisterList load_super_args = super_property_args.Truncate(3);
      SuperPropertyReference* super_property =
          property->obj()->AsSuperPropertyReference();
      VisitForRegisterValue(super_property->this_var(), load_super_args[0]);
      VisitForRegisterValue(super_property->home_object(), load_super_args[1]);
      builder()
          ->LoadLiteral(property->key()->AsLiteral()->AsRawPropertyName())
          .StoreAccumulatorInRegister(load_super_args[2])
          .CallRuntime(Runtime::kLoadFromSuper, load_super_args);
      break;
    }
    case KEYED_SUPER_PROPERTY: {
      super_property_args = register_allocator()->NewRegisterList(4);
      RegisterList load_super_args = super_property_args.Truncate(3);
      SuperPropertyReference* super_property =
          property->obj()->AsSuperPropertyReference();
      VisitForRegisterValue(super_property->this_var(), load_super_args[0]);
      VisitForRegisterValue(super_property->home_object(), load_super_args[1]);
      VisitForRegisterValue(property->key(), load_super_args[2]);
      builder()->CallRuntime(Runtime::kLoadKeyedFromSuper, load_super_args);
      break;
    }
  }

  // Save result for postfix expressions.
  FeedbackSlot count_slot = feedback_spec()->AddBinaryOpICSlot();
  if (is_postfix) {
    old_value = register_allocator()->NewRegister();
    // Convert old value into a number before saving it.
    // TODO(ignition): Think about adding proper PostInc/PostDec bytecodes
    // instead of this ToNumber + Inc/Dec dance.
    builder()
        ->ToNumeric(feedback_index(count_slot))
        .StoreAccumulatorInRegister(old_value);
  }

  // Perform +1/-1 operation.
  builder()->UnaryOperation(expr->op(), feedback_index(count_slot));

  // Store the value.
  builder()->SetExpressionPosition(expr);
  switch (assign_type) {
    case VARIABLE: {
      VariableProxy* proxy = expr->expression()->AsVariableProxy();
      BuildVariableAssignment(proxy->var(), expr->op(),
                              proxy->hole_check_mode());
      break;
    }
    case NAMED_PROPERTY: {
      FeedbackSlot slot = feedback_spec()->AddStoreICSlot(language_mode());
      builder()->StoreNamedProperty(object, name, feedback_index(slot),
                                    language_mode());
      break;
    }
    case KEYED_PROPERTY: {
      FeedbackSlot slot = feedback_spec()->AddKeyedStoreICSlot(language_mode());
      builder()->StoreKeyedProperty(object, key, feedback_index(slot),
                                    language_mode());
      break;
    }
    case NAMED_SUPER_PROPERTY: {
      builder()
          ->StoreAccumulatorInRegister(super_property_args[3])
          .CallRuntime(StoreToSuperRuntimeId(), super_property_args);
      break;
    }
    case KEYED_SUPER_PROPERTY: {
      builder()
          ->StoreAccumulatorInRegister(super_property_args[3])
          .CallRuntime(StoreKeyedToSuperRuntimeId(), super_property_args);
      break;
    }
  }

  // Restore old value for postfix expressions.
  if (is_postfix) {
    builder()->LoadAccumulatorWithRegister(old_value);
  }
}

void BytecodeGenerator::VisitBinaryOperation(BinaryOperation* binop) {
  switch (binop->op()) {
    case Token::COMMA:
      VisitCommaExpression(binop);
      break;
    case Token::OR:
      VisitLogicalOrExpression(binop);
      break;
    case Token::AND:
      VisitLogicalAndExpression(binop);
      break;
    default:
      VisitArithmeticExpression(binop);
      break;
  }
}

void BytecodeGenerator::BuildLiteralCompareNil(Token::Value op, NilValue nil) {
  if (execution_result()->IsTest()) {
    TestResultScope* test_result = execution_result()->AsTest();
    switch (test_result->fallthrough()) {
      case TestFallthrough::kThen:
        builder()->JumpIfNotNil(test_result->NewElseLabel(), op, nil);
        break;
      case TestFallthrough::kElse:
        builder()->JumpIfNil(test_result->NewThenLabel(), op, nil);
        break;
      case TestFallthrough::kNone:
        builder()
            ->JumpIfNil(test_result->NewThenLabel(), op, nil)
            .Jump(test_result->NewElseLabel());
    }
    test_result->SetResultConsumedByTest();
  } else {
    builder()->CompareNil(op, nil);
  }
}

void BytecodeGenerator::VisitCompareOperation(CompareOperation* expr) {
  Expression* sub_expr;
  Literal* literal;
  if (expr->IsLiteralCompareTypeof(&sub_expr, &literal)) {
    // Emit a fast literal comparion for expressions of the form:
    // typeof(x) === 'string'.
    VisitForTypeOfValue(sub_expr);
    builder()->SetExpressionPosition(expr);
    TestTypeOfFlags::LiteralFlag literal_flag =
        TestTypeOfFlags::GetFlagForLiteral(ast_string_constants(), literal);
    if (literal_flag == TestTypeOfFlags::LiteralFlag::kOther) {
      builder()->LoadFalse();
    } else {
      builder()->CompareTypeOf(literal_flag);
    }
  } else if (expr->IsLiteralCompareUndefined(&sub_expr)) {
    VisitForAccumulatorValue(sub_expr);
    builder()->SetExpressionPosition(expr);
    BuildLiteralCompareNil(expr->op(), kUndefinedValue);
  } else if (expr->IsLiteralCompareNull(&sub_expr)) {
    VisitForAccumulatorValue(sub_expr);
    builder()->SetExpressionPosition(expr);
    BuildLiteralCompareNil(expr->op(), kNullValue);
  } else {
    Register lhs = VisitForRegisterValue(expr->left());
    VisitForAccumulatorValue(expr->right());
    builder()->SetExpressionPosition(expr);
    if (expr->op() == Token::INSTANCEOF || expr->op() == Token::IN) {
      builder()->CompareOperation(expr->op(), lhs);
    } else {
      FeedbackSlot slot = feedback_spec()->AddCompareICSlot();
      builder()->CompareOperation(expr->op(), lhs, feedback_index(slot));
    }
  }
  // Always returns a boolean value.
  execution_result()->SetResultIsBoolean();
}

void BytecodeGenerator::VisitArithmeticExpression(BinaryOperation* expr) {
  FeedbackSlot slot = feedback_spec()->AddBinaryOpICSlot();
  Expression* subexpr;
  Smi* literal;
  if (expr->IsSmiLiteralOperation(&subexpr, &literal)) {
    VisitForAccumulatorValue(subexpr);
    builder()->SetExpressionPosition(expr);
    builder()->BinaryOperationSmiLiteral(expr->op(), literal,
                                         feedback_index(slot));
  } else {
    Register lhs = VisitForRegisterValue(expr->left());
    VisitForAccumulatorValue(expr->right());
    builder()->SetExpressionPosition(expr);
    builder()->BinaryOperation(expr->op(), lhs, feedback_index(slot));
  }
}

void BytecodeGenerator::VisitSpread(Spread* expr) { Visit(expr->expression()); }

void BytecodeGenerator::VisitEmptyParentheses(EmptyParentheses* expr) {
  UNREACHABLE();
}

void BytecodeGenerator::VisitImportCallExpression(ImportCallExpression* expr) {
  RegisterList args = register_allocator()->NewRegisterList(2);
  VisitForRegisterValue(expr->argument(), args[1]);
  builder()
      ->MoveRegister(Register::function_closure(), args[0])
      .CallRuntime(Runtime::kDynamicImportCall, args);
}

void BytecodeGenerator::BuildGetIterator(Expression* iterable,
                                         IteratorType hint) {
  VisitForAccumulatorValue(iterable);
  BuildGetIteratorFromAccumulator(hint);
}

void BytecodeGenerator::BuildGetIteratorFromAccumulator(IteratorType hint) {
  RegisterList args = register_allocator()->NewRegisterList(1);
  Register method = register_allocator()->NewRegister();
  Register obj = args[0];

  if (hint == IteratorType::kAsync) {
    // Set method to GetMethod(obj, @@asyncIterator)
    builder()->StoreAccumulatorInRegister(obj).LoadAsyncIteratorProperty(
        obj, feedback_index(feedback_spec()->AddLoadICSlot()));

    BytecodeLabel async_iterator_undefined, async_iterator_null, done;
    // TODO(ignition): Add a single opcode for JumpIfNullOrUndefined
    builder()->JumpIfUndefined(&async_iterator_undefined);
    builder()->JumpIfNull(&async_iterator_null);

    // Let iterator be Call(method, obj)
    builder()->StoreAccumulatorInRegister(method).CallProperty(
        method, args, feedback_index(feedback_spec()->AddCallICSlot()));

    // If Type(iterator) is not Object, throw a TypeError exception.
    builder()->JumpIfJSReceiver(&done);
    builder()->CallRuntime(Runtime::kThrowSymbolAsyncIteratorInvalid);

    builder()->Bind(&async_iterator_undefined);
    builder()->Bind(&async_iterator_null);
    // If method is undefined,
    //     Let syncMethod be GetMethod(obj, @@iterator)
    BytecodeLabel call_method;
    builder()
        ->LoadIteratorProperty(obj,
                               feedback_index(feedback_spec()->AddLoadICSlot()))
        .StoreAccumulatorInRegister(method);

    //     Let syncIterator be Call(syncMethod, obj)
    builder()->CallProperty(method, args,
                            feedback_index(feedback_spec()->AddCallICSlot()));

    // Return CreateAsyncFromSyncIterator(syncIterator)
    // alias `method` register as it's no longer used
    Register sync_iter = method;
    builder()->StoreAccumulatorInRegister(sync_iter).CallRuntime(
        Runtime::kInlineCreateAsyncFromSyncIterator, sync_iter);

    builder()->Bind(&done);
  } else {
    // Let method be GetMethod(obj, @@iterator).
    builder()
        ->StoreAccumulatorInRegister(obj)
        .LoadIteratorProperty(obj,
                              feedback_index(feedback_spec()->AddLoadICSlot()))
        .StoreAccumulatorInRegister(method);

    // Let iterator be Call(method, obj).
    builder()->CallProperty(method, args,
                            feedback_index(feedback_spec()->AddCallICSlot()));

    // If Type(iterator) is not Object, throw a TypeError exception.
    BytecodeLabel no_type_error;
    builder()->JumpIfJSReceiver(&no_type_error);
    builder()->CallRuntime(Runtime::kThrowSymbolIteratorInvalid);
    builder()->Bind(&no_type_error);
  }
}

BytecodeGenerator::IteratorRecord BytecodeGenerator::BuildIteratorRecord(
    Register iterator, IteratorType type) {
  DCHECK(iterator.is_valid());
  Register iterator_next = register_allocator()->NewRegister();

  builder()
      ->StoreAccumulatorInRegister(iterator)
      .LoadNamedProperty(iterator, ast_string_constants()->next_string(),
                         feedback_index(feedback_spec()->AddLoadICSlot()))
      .StoreAccumulatorInRegister(iterator_next);

  return IteratorRecord{type, iterator, iterator_next};
}

BytecodeGenerator::IteratorRecord BytecodeGenerator::BuildIteratorRecord(
    IteratorType type) {
  Register iterator = register_allocator()->NewRegister();
  return BuildIteratorRecord(iterator, type);
}

void BytecodeGenerator::VisitGetIterator(GetIterator* expr) {
  builder()->SetExpressionPosition(expr);
  BuildGetIterator(expr->iterable(), expr->hint());
}

void BytecodeGenerator::VisitGetTemplateObject(GetTemplateObject* expr) {
  builder()->SetExpressionPosition(expr);
  size_t entry = builder()->AllocateDeferredConstantPoolEntry();
  template_objects_.push_back(std::make_pair(expr, entry));
  builder()->GetTemplateObject(entry);
}

void BytecodeGenerator::VisitThisFunction(ThisFunction* expr) {
  builder()->LoadAccumulatorWithRegister(Register::function_closure());
}

void BytecodeGenerator::VisitSuperCallReference(SuperCallReference* expr) {
  // Handled by VisitCall().
  UNREACHABLE();
}

void BytecodeGenerator::VisitSuperPropertyReference(
    SuperPropertyReference* expr) {
  builder()->CallRuntime(Runtime::kThrowUnsupportedSuperError);
}

void BytecodeGenerator::VisitCommaExpression(BinaryOperation* binop) {
  VisitForEffect(binop->left());
  Visit(binop->right());
}

void BytecodeGenerator::BuildLogicalTest(Token::Value token, Expression* left,
                                         Expression* right) {
  DCHECK(token == Token::OR || token == Token::AND);
  TestResultScope* test_result = execution_result()->AsTest();
  BytecodeLabels* then_labels = test_result->then_labels();
  BytecodeLabels* else_labels = test_result->else_labels();
  TestFallthrough fallthrough = test_result->fallthrough();
  {
    // Visit the left side using current TestResultScope.
    BytecodeLabels test_right(zone());
    if (token == Token::OR) {
      test_result->set_fallthrough(TestFallthrough::kElse);
      test_result->set_else_labels(&test_right);
    } else {
      DCHECK_EQ(Token::AND, token);
      test_result->set_fallthrough(TestFallthrough::kThen);
      test_result->set_then_labels(&test_right);
    }
    VisitInSameTestExecutionScope(left);
    test_right.Bind(builder());
  }
  // Visit the right side in a new TestResultScope.
  VisitForTest(right, then_labels, else_labels, fallthrough);
}

void BytecodeGenerator::VisitLogicalOrExpression(BinaryOperation* binop) {
  Expression* left = binop->left();
  Expression* right = binop->right();

  if (execution_result()->IsTest()) {
    TestResultScope* test_result = execution_result()->AsTest();
    if (left->ToBooleanIsTrue()) {
      builder()->Jump(test_result->NewThenLabel());
    } else if (left->ToBooleanIsFalse() && right->ToBooleanIsFalse()) {
      builder()->Jump(test_result->NewElseLabel());
    } else {
      BuildLogicalTest(Token::OR, left, right);
    }
    test_result->SetResultConsumedByTest();
  } else {
    if (left->ToBooleanIsTrue()) {
      VisitForAccumulatorValue(left);
    } else if (left->ToBooleanIsFalse()) {
      VisitForAccumulatorValue(right);
    } else {
      BytecodeLabel end_label;
      TypeHint type_hint = VisitForAccumulatorValue(left);
      builder()->JumpIfTrue(ToBooleanModeFromTypeHint(type_hint), &end_label);
      VisitForAccumulatorValue(right);
      builder()->Bind(&end_label);
    }
  }
}

void BytecodeGenerator::VisitLogicalAndExpression(BinaryOperation* binop) {
  Expression* left = binop->left();
  Expression* right = binop->right();

  if (execution_result()->IsTest()) {
    TestResultScope* test_result = execution_result()->AsTest();
    if (left->ToBooleanIsFalse()) {
      builder()->Jump(test_result->NewElseLabel());
    } else if (left->ToBooleanIsTrue() && right->ToBooleanIsTrue()) {
      builder()->Jump(test_result->NewThenLabel());
    } else {
      BuildLogicalTest(Token::AND, left, right);
    }
    test_result->SetResultConsumedByTest();
  } else {
    if (left->ToBooleanIsFalse()) {
      VisitForAccumulatorValue(left);
    } else if (left->ToBooleanIsTrue()) {
      VisitForAccumulatorValue(right);
    } else {
      BytecodeLabel end_label;
      TypeHint type_hint = VisitForAccumulatorValue(left);
      builder()->JumpIfFalse(ToBooleanModeFromTypeHint(type_hint), &end_label);
      VisitForAccumulatorValue(right);
      builder()->Bind(&end_label);
    }
  }
}

void BytecodeGenerator::VisitRewritableExpression(RewritableExpression* expr) {
  Visit(expr->expression());
}

void BytecodeGenerator::BuildNewLocalActivationContext() {
  ValueResultScope value_execution_result(this);
  Scope* scope = closure_scope();

  // Create the appropriate context.
  if (scope->is_script_scope()) {
    RegisterList args = register_allocator()->NewRegisterList(2);
    builder()
        ->LoadAccumulatorWithRegister(Register::function_closure())
        .StoreAccumulatorInRegister(args[0])
        .LoadLiteral(scope)
        .StoreAccumulatorInRegister(args[1])
        .CallRuntime(Runtime::kNewScriptContext, args);
  } else if (scope->is_module_scope()) {
    // We don't need to do anything for the outer script scope.
    DCHECK(scope->outer_scope()->is_script_scope());

    // A JSFunction representing a module is called with the module object as
    // its sole argument, which we pass on to PushModuleContext.
    RegisterList args = register_allocator()->NewRegisterList(3);
    builder()
        ->MoveRegister(builder()->Parameter(0), args[0])
        .LoadAccumulatorWithRegister(Register::function_closure())
        .StoreAccumulatorInRegister(args[1])
        .LoadLiteral(scope)
        .StoreAccumulatorInRegister(args[2])
        .CallRuntime(Runtime::kPushModuleContext, args);
  } else {
    DCHECK(scope->is_function_scope() || scope->is_eval_scope());
    int slot_count = scope->num_heap_slots() - Context::MIN_CONTEXT_SLOTS;
    if (slot_count <= ConstructorBuiltins::MaximumFunctionContextSlots()) {
      switch (scope->scope_type()) {
        case EVAL_SCOPE:
          builder()->CreateEvalContext(slot_count);
          break;
        case FUNCTION_SCOPE:
          builder()->CreateFunctionContext(slot_count);
          break;
        default:
          UNREACHABLE();
      }
    } else {
      RegisterList args = register_allocator()->NewRegisterList(2);
      builder()
          ->MoveRegister(Register::function_closure(), args[0])
          .LoadLiteral(Smi::FromInt(scope->scope_type()))
          .StoreAccumulatorInRegister(args[1])
          .CallRuntime(Runtime::kNewFunctionContext, args);
    }
  }
}

void BytecodeGenerator::BuildLocalActivationContextInitialization() {
  DeclarationScope* scope = closure_scope();

  if (scope->has_this_declaration() && scope->receiver()->IsContextSlot()) {
    Variable* variable = scope->receiver();
    Register receiver(builder()->Receiver());
    // Context variable (at bottom of the context chain).
    DCHECK_EQ(0, scope->ContextChainLength(variable->scope()));
    builder()->LoadAccumulatorWithRegister(receiver).StoreContextSlot(
        execution_context()->reg(), variable->index(), 0);
  }

  // Copy parameters into context if necessary.
  int num_parameters = scope->num_parameters();
  for (int i = 0; i < num_parameters; i++) {
    Variable* variable = scope->parameter(i);
    if (!variable->IsContextSlot()) continue;

    Register parameter(builder()->Parameter(i));
    // Context variable (at bottom of the context chain).
    DCHECK_EQ(0, scope->ContextChainLength(variable->scope()));
    builder()->LoadAccumulatorWithRegister(parameter).StoreContextSlot(
        execution_context()->reg(), variable->index(), 0);
  }
}

void BytecodeGenerator::CreateContextScopeIfNeeded(Scope* scope) {
  if (!scope) return;
  if (!scope->NeedsContext()) return;
  if (scope->is_block_scope()) return BuildNewLocalBlockContext(scope);
  if (scope->is_with_scope()) return BuildNewLocalWithContext(scope);
  if (scope->is_catch_scope()) return BuildNewLocalCatchContext(scope);
}

void BytecodeGenerator::BuildNewLocalBlockContext(Scope* scope) {
  ValueResultScope value_execution_result(this);
  DCHECK(scope->is_block_scope());

  VisitFunctionClosureForContext();
  builder()->CreateBlockContext(scope);
}

void BytecodeGenerator::BuildNewLocalWithContext(Scope* scope) {
  ValueResultScope value_execution_result(this);

  Register extension_object = register_allocator()->NewRegister();

  builder()->ToObject(extension_object);
  VisitFunctionClosureForContext();
  builder()->CreateWithContext(extension_object, scope);
}

void BytecodeGenerator::BuildNewLocalCatchContext(Scope* scope) {
  ValueResultScope value_execution_result(this);
  DCHECK(scope->catch_variable()->IsContextSlot());

  Register exception = register_allocator()->NewRegister();
  builder()->StoreAccumulatorInRegister(exception);
  VisitFunctionClosureForContext();
  builder()->CreateCatchContext(exception, scope->catch_variable()->raw_name(),
                                scope);
}

void BytecodeGenerator::VisitObjectLiteralAccessor(
    Register home_object, ObjectLiteralProperty* property, Register value_out) {
  if (property == nullptr) {
    builder()->LoadNull().StoreAccumulatorInRegister(value_out);
  } else {
    VisitForRegisterValue(property->value(), value_out);
    VisitSetHomeObject(value_out, home_object, property);
  }
}

void BytecodeGenerator::VisitSetHomeObject(Register value, Register home_object,
                                           LiteralProperty* property) {
  Expression* expr = property->value();
  if (FunctionLiteral::NeedsHomeObject(expr)) {
    FeedbackSlot slot = feedback_spec()->AddStoreICSlot(language_mode());
    builder()
        ->LoadAccumulatorWithRegister(home_object)
        .StoreHomeObjectProperty(value, feedback_index(slot), language_mode());
  }
}

void BytecodeGenerator::VisitArgumentsObject(Variable* variable) {
  if (variable == nullptr) return;

  DCHECK(variable->IsContextSlot() || variable->IsStackAllocated());

  // Allocate and initialize a new arguments object and assign to the
  // {arguments} variable.
  CreateArgumentsType type =
      is_strict(language_mode()) || !info()->has_simple_parameters()
          ? CreateArgumentsType::kUnmappedArguments
          : CreateArgumentsType::kMappedArguments;
  builder()->CreateArguments(type);
  BuildVariableAssignment(variable, Token::ASSIGN, HoleCheckMode::kElided);
}

void BytecodeGenerator::VisitRestArgumentsArray(Variable* rest) {
  if (rest == nullptr) return;

  // If the rest array isn't used or captured, skip this step.
  if (rest->IsUnallocated()) return;

  // Allocate and initialize a new rest parameter and assign to the {rest}
  // variable.
  builder()->CreateArguments(CreateArgumentsType::kRestParameter);
  DCHECK(rest->IsContextSlot() || rest->IsStackAllocated());
  BuildVariableAssignment(rest, Token::ASSIGN, HoleCheckMode::kElided);
}

void BytecodeGenerator::VisitThisFunctionVariable(Variable* variable) {
  if (variable == nullptr) return;

  // Store the closure we were called with in the given variable.
  builder()->LoadAccumulatorWithRegister(Register::function_closure());
  BuildVariableAssignment(variable, Token::INIT, HoleCheckMode::kElided);
}

void BytecodeGenerator::VisitNewTargetVariable(Variable* variable) {
  if (variable == nullptr) return;

  // The generator resume trampoline abuses the new.target register
  // to pass in the generator object.  In ordinary calls, new.target is always
  // undefined because generator functions are non-constructible, so don't
  // assign anything to the new.target variable.
  if (info()->literal()->CanSuspend()) return;

  if (variable->location() == VariableLocation::LOCAL) {
    // The new.target register was already assigned by entry trampoline.
    DCHECK_EQ(incoming_new_target_or_generator_.index(),
              GetRegisterForLocalVariable(variable).index());
    return;
  }

  // Store the new target we were called with in the given variable.
  builder()->LoadAccumulatorWithRegister(incoming_new_target_or_generator_);
  BuildVariableAssignment(variable, Token::INIT, HoleCheckMode::kElided);
}

void BytecodeGenerator::VisitFunctionVariable(Variable* variable) {
  if (variable == nullptr || variable->IsUnallocated()) return;

  if (variable->IsStackLocal()) {
    builder()->MoveRegister(Register::function_closure(),
                            builder()->Local(variable->index()));
  } else {
    builder()->LoadAccumulatorWithRegister(Register::function_closure());
    BuildVariableAssignment(variable, Token::INIT, HoleCheckMode::kElided);
  }
}

void BytecodeGenerator::BuildGeneratorObjectVariableInitialization() {
  DCHECK(IsResumableFunction(info()->literal()->kind()));

  RegisterAllocationScope register_scope(this);
  RegisterList args = register_allocator()->NewRegisterList(2);
  builder()
      ->MoveRegister(Register::function_closure(), args[0])
      .MoveRegister(builder()->Receiver(), args[1])
      .CallRuntime(Runtime::kInlineCreateJSGeneratorObject, args)
      .StoreAccumulatorInRegister(generator_object());
}

void BytecodeGenerator::VisitFunctionClosureForContext() {
  ValueResultScope value_execution_result(this);
  if (closure_scope()->is_script_scope()) {
    // Contexts nested in the native context have a canonical empty function as
    // their closure, not the anonymous closure containing the global code.
    Register native_context = register_allocator()->NewRegister();
    builder()
        ->LoadContextSlot(execution_context()->reg(),
                          Context::NATIVE_CONTEXT_INDEX, 0,
                          BytecodeArrayBuilder::kImmutableSlot)
        .StoreAccumulatorInRegister(native_context)
        .LoadContextSlot(native_context, Context::CLOSURE_INDEX, 0,
                         BytecodeArrayBuilder::kImmutableSlot);
  } else if (closure_scope()->is_eval_scope()) {
    // Contexts created by a call to eval have the same closure as the
    // context calling eval, not the anonymous closure containing the eval
    // code. Fetch it from the context.
    builder()->LoadContextSlot(execution_context()->reg(),
                               Context::CLOSURE_INDEX, 0,
                               BytecodeArrayBuilder::kImmutableSlot);
  } else {
    DCHECK(closure_scope()->is_function_scope() ||
           closure_scope()->is_module_scope());
    builder()->LoadAccumulatorWithRegister(Register::function_closure());
  }
}

void BytecodeGenerator::BuildPushUndefinedIntoRegisterList(
    RegisterList* reg_list) {
  Register reg = register_allocator()->GrowRegisterList(reg_list);
  builder()->LoadUndefined().StoreAccumulatorInRegister(reg);
}

void BytecodeGenerator::BuildLoadPropertyKey(LiteralProperty* property,
                                             Register out_reg) {
  if (property->key()->IsStringLiteral()) {
    VisitForRegisterValue(property->key(), out_reg);
  } else {
    VisitForAccumulatorValue(property->key());
    builder()->ToName(out_reg);
  }
}

int BytecodeGenerator::AllocateBlockCoverageSlotIfEnabled(
    AstNode* node, SourceRangeKind kind) {
  return (block_coverage_builder_ == nullptr)
             ? BlockCoverageBuilder::kNoCoverageArraySlot
             : block_coverage_builder_->AllocateBlockCoverageSlot(node, kind);
}

void BytecodeGenerator::BuildIncrementBlockCoverageCounterIfEnabled(
    AstNode* node, SourceRangeKind kind) {
  if (block_coverage_builder_ == nullptr) return;
  block_coverage_builder_->IncrementBlockCounter(node, kind);
}

void BytecodeGenerator::BuildIncrementBlockCoverageCounterIfEnabled(
    int coverage_array_slot) {
  if (block_coverage_builder_ != nullptr) {
    block_coverage_builder_->IncrementBlockCounter(coverage_array_slot);
  }
}

// Visits the expression |expr| and places the result in the accumulator.
BytecodeGenerator::TypeHint BytecodeGenerator::VisitForAccumulatorValue(
    Expression* expr) {
  ValueResultScope accumulator_scope(this);
  Visit(expr);
  return accumulator_scope.type_hint();
}

void BytecodeGenerator::VisitForAccumulatorValueOrTheHole(Expression* expr) {
  if (expr == nullptr) {
    builder()->LoadTheHole();
  } else {
    VisitForAccumulatorValue(expr);
  }
}

// Visits the expression |expr| and discards the result.
void BytecodeGenerator::VisitForEffect(Expression* expr) {
  EffectResultScope effect_scope(this);
  Visit(expr);
}

// Visits the expression |expr| and returns the register containing
// the expression result.
Register BytecodeGenerator::VisitForRegisterValue(Expression* expr) {
  VisitForAccumulatorValue(expr);
  Register result = register_allocator()->NewRegister();
  builder()->StoreAccumulatorInRegister(result);
  return result;
}

// Visits the expression |expr| and stores the expression result in
// |destination|.
void BytecodeGenerator::VisitForRegisterValue(Expression* expr,
                                              Register destination) {
  ValueResultScope register_scope(this);
  Visit(expr);
  builder()->StoreAccumulatorInRegister(destination);
}

// Visits the expression |expr| and pushes the result into a new register
// added to the end of |reg_list|.
void BytecodeGenerator::VisitAndPushIntoRegisterList(Expression* expr,
                                                     RegisterList* reg_list) {
  {
    ValueResultScope register_scope(this);
    Visit(expr);
  }
  // Grow the register list after visiting the expression to avoid reserving
  // the register across the expression evaluation, which could cause memory
  // leaks for deep expressions due to dead objects being kept alive by pointers
  // in registers.
  Register destination = register_allocator()->GrowRegisterList(reg_list);
  builder()->StoreAccumulatorInRegister(destination);
}

void BytecodeGenerator::BuildTest(ToBooleanMode mode,
                                  BytecodeLabels* then_labels,
                                  BytecodeLabels* else_labels,
                                  TestFallthrough fallthrough) {
  switch (fallthrough) {
    case TestFallthrough::kThen:
      builder()->JumpIfFalse(mode, else_labels->New());
      break;
    case TestFallthrough::kElse:
      builder()->JumpIfTrue(mode, then_labels->New());
      break;
    case TestFallthrough::kNone:
      builder()->JumpIfTrue(mode, then_labels->New());
      builder()->Jump(else_labels->New());
      break;
  }
}

// Visits the expression |expr| for testing its boolean value and jumping to the
// |then| or |other| label depending on value and short-circuit semantics
void BytecodeGenerator::VisitForTest(Expression* expr,
                                     BytecodeLabels* then_labels,
                                     BytecodeLabels* else_labels,
                                     TestFallthrough fallthrough) {
  bool result_consumed;
  TypeHint type_hint;
  {
    // To make sure that all temporary registers are returned before generating
    // jumps below, we ensure that the result scope is deleted before doing so.
    // Dead registers might be materialized otherwise.
    TestResultScope test_result(this, then_labels, else_labels, fallthrough);
    Visit(expr);
    result_consumed = test_result.result_consumed_by_test();
    type_hint = test_result.type_hint();
    // Labels and fallthrough might have been mutated, so update based on
    // TestResultScope.
    then_labels = test_result.then_labels();
    else_labels = test_result.else_labels();
    fallthrough = test_result.fallthrough();
  }
  if (!result_consumed) {
    BuildTest(ToBooleanModeFromTypeHint(type_hint), then_labels, else_labels,
              fallthrough);
  }
}

void BytecodeGenerator::VisitInSameTestExecutionScope(Expression* expr) {
  DCHECK(execution_result()->IsTest());
  {
    RegisterAllocationScope reg_scope(this);
    Visit(expr);
  }
  if (!execution_result()->AsTest()->result_consumed_by_test()) {
    TestResultScope* result_scope = execution_result()->AsTest();
    BuildTest(ToBooleanModeFromTypeHint(result_scope->type_hint()),
              result_scope->then_labels(), result_scope->else_labels(),
              result_scope->fallthrough());
    result_scope->SetResultConsumedByTest();
  }
}

void BytecodeGenerator::VisitInScope(Statement* stmt, Scope* scope) {
  DCHECK(scope->declarations()->is_empty());
  CurrentScope current_scope(this, scope);
  ContextScope context_scope(this, scope);
  Visit(stmt);
}

Register BytecodeGenerator::GetRegisterForLocalVariable(Variable* variable) {
  DCHECK_EQ(VariableLocation::LOCAL, variable->location());
  return builder()->Local(variable->index());
}

FunctionKind BytecodeGenerator::function_kind() const {
  return info()->literal()->kind();
}

LanguageMode BytecodeGenerator::language_mode() const {
  return current_scope()->language_mode();
}

Register BytecodeGenerator::generator_object() const {
  DCHECK(info()->literal()->CanSuspend());
  return incoming_new_target_or_generator_;
}

Register BytecodeGenerator::await_promise() const { return await_promise_; }

FeedbackVectorSpec* BytecodeGenerator::feedback_spec() {
  return info()->feedback_vector_spec();
}

int BytecodeGenerator::feedback_index(FeedbackSlot slot) const {
  DCHECK(!slot.IsInvalid());
  return FeedbackVector::GetIndex(slot);
}

FeedbackSlot BytecodeGenerator::GetCachedLoadGlobalICSlot(
    TypeofMode typeof_mode, Variable* variable) {
  FeedbackSlot slot = feedback_slot_cache()->Get(typeof_mode, variable);
  if (!slot.IsInvalid()) {
    return slot;
  }
  slot = feedback_spec()->AddLoadGlobalICSlot(typeof_mode);
  feedback_slot_cache()->Put(typeof_mode, variable, slot);
  return slot;
}

FeedbackSlot BytecodeGenerator::GetCachedCreateClosureSlot(
    FunctionLiteral* literal) {
  FeedbackSlot slot = feedback_slot_cache()->Get(literal);
  if (!slot.IsInvalid()) {
    return slot;
  }
  slot = feedback_spec()->AddCreateClosureSlot();
  feedback_slot_cache()->Put(literal, slot);
  return slot;
}

Runtime::FunctionId BytecodeGenerator::StoreToSuperRuntimeId() {
  return is_strict(language_mode()) ? Runtime::kStoreToSuper_Strict
                                    : Runtime::kStoreToSuper_Sloppy;
}

Runtime::FunctionId BytecodeGenerator::StoreKeyedToSuperRuntimeId() {
  return is_strict(language_mode()) ? Runtime::kStoreKeyedToSuper_Strict
                                    : Runtime::kStoreKeyedToSuper_Sloppy;
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
