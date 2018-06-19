// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_LIVEEDIT_H_
#define V8_DEBUG_LIVEEDIT_H_

#include "src/allocation.h"
#include "src/ast/ast.h"
#include "src/parsing/parser.h"

namespace v8 {
namespace debug {
struct LiveEditResult;
}
namespace internal {

class JavaScriptFrame;

struct SourceChangeRange {
  int start_position;
  int end_position;
  int new_start_position;
  int new_end_position;
};

struct FunctionLiteralChange {
  int new_start_position;
  int new_end_position;
  bool has_changes;
  FunctionLiteral* outer_literal;

  explicit FunctionLiteralChange(int new_start_position, FunctionLiteral* outer)
      : new_start_position(new_start_position),
        new_end_position(kNoSourcePosition),
        has_changes(false),
        outer_literal(outer) {}
};

class LiveEdit : AllStatic {
 public:
  static void InitializeThreadLocal(Debug* debug);

  // Restarts the call frame and completely drops all frames above it.
  // Return error message or nullptr.
  static const char* RestartFrame(JavaScriptFrame* frame);

  static void CompareStrings(Handle<String> a, Handle<String> b,
                             std::vector<SourceChangeRange>* changes);
  static int TranslatePosition(const std::vector<SourceChangeRange>& changed,
                               int position);

  // TODO(kozyatinskiy): move these two functions to impl.
  using FunctionLiteralChanges =
      std::unordered_map<FunctionLiteral*, FunctionLiteralChange>;
  static void CalculateFunctionLiteralChanges(
      const std::vector<FunctionLiteral*>& literals,
      const std::vector<SourceChangeRange>& source_changes,
      FunctionLiteralChanges* result);

  using LiteralMap = std::unordered_map<FunctionLiteral*, FunctionLiteral*>;
  static void MapLiterals(const FunctionLiteralChanges& changes,
                          const std::vector<FunctionLiteral*>& new_literals,
                          LiteralMap* changed, LiteralMap* source_changed,
                          LiteralMap* moved);

  static void PatchScript(Handle<Script> script, Handle<String> source,
                          debug::LiveEditResult* result);

  // Architecture-specific constant.
  static const bool kFrameDropperSupported;
};
}  // namespace internal
}  // namespace v8

#endif  // V8_DEBUG_LIVEEDIT_H_
