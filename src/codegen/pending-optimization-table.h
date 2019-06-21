// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_PENDING_OPTIMIZATION_TABLE_H_
#define V8_CODEGEN_PENDING_OPTIMIZATION_TABLE_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

class PendingOptimizationTable {
 public:
  enum FunctionStatus { kPrepareForOptimize, kMarkForOptimize };

  static void PreparedForOptimization(Isolate* isolate,
                                      Handle<JSFunction> function);
  static void MarkedForOptimization(Isolate* isolate,
                                    Handle<JSFunction> function);
  static void FunctionWasOptimized(Isolate* isolate,
                                   Handle<JSFunction> function);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_PENDING_OPTIMIZATION_TABLE_H_
