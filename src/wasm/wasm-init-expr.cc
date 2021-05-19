// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-init-expr.h"

namespace v8 {
namespace internal {
namespace wasm {

std::ostream& operator<<(std::ostream& os, const WasmInitExpr& expr) {
  os << "WasmInitExpr.";
  switch (expr.kind()) {
    case WasmInitExpr::kNone:
      UNREACHABLE();
    case WasmInitExpr::kS128Const:
    case WasmInitExpr::kRttCanon:
    case WasmInitExpr::kRttSub:
    case WasmInitExpr::kRefNullConst:
      // TODO(manoskouk): Implement these.
      UNIMPLEMENTED();
    case WasmInitExpr::kGlobalGet:
      os << "GlobalGet(" << expr.immediate().index;
      break;
    case WasmInitExpr::kI32Const:
      os << "I32Const(" << expr.immediate().i32_const;
      break;
    case WasmInitExpr::kI64Const:
      os << "I64Const(" << expr.immediate().i64_const;
      break;
    case WasmInitExpr::kF32Const:
      os << "F32Const(" << expr.immediate().f32_const;
      break;
    case WasmInitExpr::kF64Const:
      os << "F64Const(" << expr.immediate().f64_const;
      break;
    case WasmInitExpr::kRefFuncConst:
      os << "RefFunc(" << expr.immediate().index;
      break;
  }
  return os << ")";
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
