// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_LIVEEDIT_H_
#define V8_DEBUG_LIVEEDIT_H_

#include <vector>

#include "src/globals.h"
#include "src/handles.h"

namespace v8 {
namespace debug {
struct LiveEditResult;
}
namespace internal {

class Script;
class String;
class Debug;
class JavaScriptFrame;

struct SourceChangeRange {
  int start_position;
  int end_position;
  int new_start_position;
  int new_end_position;
};

class LiveEdit : AllStatic {
 public:
  static void InitializeThreadLocal(Debug* debug);

  // Restarts the call frame and completely drops all frames above it.
  // Return error message or nullptr.
  static bool RestartFrame(JavaScriptFrame* frame);

  static void CompareStrings(Handle<String> a, Handle<String> b,
                             std::vector<SourceChangeRange>* changes);
  static int TranslatePosition(const std::vector<SourceChangeRange>& changed,
                               int position);
  static void PatchScript(Handle<Script> script, Handle<String> source,
                          debug::LiveEditResult* result);

  // Architecture-specific constant.
  static const bool kFrameDropperSupported;
};
}  // namespace internal
}  // namespace v8

#endif  // V8_DEBUG_LIVEEDIT_H_
