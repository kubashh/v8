// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD

// This file defines Turboshaft's assembler macros. Include this file before
// your reducers and don't forget to include 'undef-assembler-macros.inc'
// afterwards.

#ifdef V8_COMPILER_TURBOSHAFT_ASSEMBLER_MACROS_DEFINED
#error \
    "Assembler macros already defined. Did you forget to #include \"undef-assembler-macros.inc\" in a previous file?"
#endif

#define V8_COMPILER_TURBOSHAFT_ASSEMBLER_MACROS_DEFINED 1

#define LIKELY(...) ConditionWithHint(__VA_ARGS__, BranchHint::kTrue)
#define UNLIKELY(...) ConditionWithHint(__VA_ARGS__, BranchHint::kFalse)

#define BIND(label, ...)                                                  \
  auto [CONCAT(is_bound_, __LINE__), ##__VA_ARGS__] =                     \
    Asm().ControlFlowHelper_Bind(label);                                  \
    (::v8::internal::compiler::turboshaft::detail::SuppressUnusedWarning( \
        CONCAT(is_bound_, __LINE__)))
#define LOOP(loop_label, ...) \
  for(auto [CONCAT(run_loop_, __LINE__), ##__VA_ARGS__] =                      \
    Asm().ControlFlowHelper_BindLoop(loop_label); CONCAT(run_loop_, __LINE__); \
        Asm().ControlFlowHelper_EndLoop(loop_label),                           \
        CONCAT(run_loop_, __LINE__) = false)
#define GOTO(label, ...)                             \
  Asm().ControlFlowHelper_Goto(label, {__VA_ARGS__})
#define GOTO_IF(cond, label, ...)                            \
  Asm().ControlFlowHelper_GotoIf(cond, label, {__VA_ARGS__})
#define GOTO_IF_NOT(cond, label, ...)                           \
  Asm().ControlFlowHelper_GotoIfNot(cond, label, {__VA_ARGS__})

// Clang/GCC helpfully warn us about dangling else in nested if statements. This
// dangling is intentional for the way these macros work, so suppress the
// warning with Pragmas.
#if !defined(V8_CC_MSVC)
#define START_SUPPRESS_DANGLING_ELSE_WARNING() \
  _Pragma("GCC diagnostic push")               \
      _Pragma("GCC diagnostic ignored \"-Wdangling-else\"")
#define END_SUPPRESS_DANGLING_ELSE_WARNING() _Pragma("GCC diagnostic pop")
#else  // !defined(V8_CC_MSVC)
#define START_SUPPRESS_DANGLING_ELSE_WARNING()
#define END_SUPPRESS_DANGLING_ELSE_WARNING()
#endif  // !defined(V8_CC_MSVC)

// IF/ELSE macros. These expand to a real C++ if-else, so that we can get
// similar block syntax behaviour (with an optional `ELSE`). Since C++ will only
// evaluate one side of the if-else, wrap it in a for loop that executes the
// if-else twice, once for each side of the branch. Each iteration also emits a
// goto-end if the corresponding branch target was bound. This for loop also
// encapsulates the state of the IF, including block pointers.
#define IF(...)                                                                \
  for (auto [then_block, else_block, end_block, iteration, explicit_else] =    \
           std::tuple{Asm().NewBlock(), Asm().NewBlock(), Asm().NewBlock(), 0, \
                      false};                                                  \
       iteration < 2 ||                                                        \
       (!explicit_else && Asm().Bind(else_block) ? Asm().Goto(end_block)       \
                                                 : (void)0,                    \
        Asm().Bind(end_block), false);                                         \
       (Asm().current_block() != nullptr ? Asm().Goto(end_block) : (void)0),   \
                                                           iteration++)        \
    if (iteration == 0 &&                                                      \
        (Asm().Branch(ConditionWithHint(__VA_ARGS__), then_block, else_block), \
         true) &&                                                              \
        Asm().Bind(then_block))

#define IF_NOT(...)                                                            \
  for (auto [then_block, else_block, end_block, iteration, explicit_else] =    \
           std::tuple{Asm().NewBlock(), Asm().NewBlock(), Asm().NewBlock(), 0, \
                      false};                                                  \
       iteration < 2 ||                                                        \
       (!explicit_else && Asm().Bind(else_block) ? Asm().Goto(end_block)       \
                                                 : (void)0,                    \
        Asm().Bind(end_block), false);                                         \
       (Asm().current_block() != nullptr ? Asm().Goto(end_block) : (void)0),   \
                                                           iteration++)        \
    if (iteration == 0 &&                                                      \
        (Asm().Branch(ConditionWithHint(__VA_ARGS__), else_block, then_block), \
         true) &&                                                              \
        Asm().Bind(then_block))

#define ELSE                                                                   \
  START_SUPPRESS_DANGLING_ELSE_WARNING()                                       \
  else if (iteration == 1 && (explicit_else = true) && Asm().Bind(else_block)) \
      END_SUPPRESS_DANGLING_ELSE_WARNING()

#define Assert(condition) AssertImpl(condition, #condition, __FILE__, __LINE__)

#ifdef DEBUG
// In debug builds, `Reduce(operation)` makes sure that `operation##Op` exists by using this name in an expression. This will detect typos in the name which would otherwise stay unnoticed potentially.
#define REDUCE(operation) \
  CONCAT(CHECK_Reduce, operation)                                             \
      = (::v8::internal::compiler::turboshaft::detail::SuppressUnusedWarning( \
            std::is_same_v<operation##Op, operation##Op>),                    \
         OpIndex{});                                                          \
  decltype(CONCAT(CHECK_Reduce, operation)) Reduce##operation
#define REDUCE_INPUT_GRAPH(operation)                                         \
  CONCAT(CHECK_ReduceInputGraph, operation)                                   \
      = (::v8::internal::compiler::turboshaft::detail::SuppressUnusedWarning( \
            std::is_same_v<operation##Op, operation##Op>),                    \
            OpIndex{});                                                       \
  decltype(CONCAT(CHECK_ReduceInputGraph, operation))                         \
          ReduceInputGraph##operation
#else
#define REDUCE(operation) Reduce##operation
#define REDUCE_INPUT_GRAPH(operation) ReduceInputGraph##operation
#endif  // DEBUG

#define __ Asm().
