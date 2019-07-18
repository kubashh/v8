// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-flags.h"

#include "src/ast/ast-value-factory.h"
#include "src/ast/ast.h"
#include "src/builtins/builtins-constructor.h"
#include "src/code-stubs.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {
namespace interpreter {

// static
uint8_t CreateArrayLiteralFlags::Encode(bool use_fast_shallow_clone,
                                        int runtime_flags) {
  uint8_t result = FlagsBits::encode(runtime_flags);
  result |= FastCloneSupportedBit::encode(use_fast_shallow_clone);
  return result;
}

// static
void CreateArrayLiteralFlags::Decode(uint8_t raw_flag,
                                     bool* use_fast_shallow_clone,
                                     int* runtime_flags) {
  *use_fast_shallow_clone = FastCloneSupportedBit::decode(raw_flag);
  *runtime_flags = FlagsBits::decode(raw_flag);
}

// static
uint8_t CreateObjectLiteralFlags::Encode(int runtime_flags,
                                         bool fast_clone_supported) {
  uint8_t result = FlagsBits::encode(runtime_flags);
  result |= FastCloneSupportedBit::encode(fast_clone_supported);
  return result;
}

// static
void CreateObjectLiteralFlags::Decode(uint8_t raw_flag,
                                      bool* fast_clone_supported,
                                      int* runtime_flags) {
  *fast_clone_supported |= FastCloneSupportedBit::decode(raw_flag);
  *runtime_flags = FlagsBits::decode(raw_flag);
}

// static
uint8_t CreateClosureFlags::Encode(bool pretenure, bool is_function_scope) {
  uint8_t result = PretenuredBit::encode(pretenure);
  if (!FLAG_always_opt && !FLAG_prepare_always_opt &&
      pretenure == NOT_TENURED && is_function_scope) {
    result |= FastNewClosureBit::encode(true);
  }
  return result;
}

// static
void CreateClosureFlags::Decode(uint8_t raw_flag, bool* fast_new_closure,
                                bool* pretenure) {
  *fast_new_closure = FastNewClosureBit::decode(raw_flag);
  *pretenure = PretenuredBit::decode(raw_flag);
  DCHECK_IMPLIES(*pretenure, !*fast_new_closure);
}

// static
TestTypeOfFlags::LiteralFlag TestTypeOfFlags::GetFlagForLiteral(
    const AstStringConstants* ast_constants, Literal* literal) {
  const AstRawString* raw_literal = literal->AsRawString();
  if (raw_literal == ast_constants->number_string()) {
    return LiteralFlag::kNumber;
  } else if (raw_literal == ast_constants->string_string()) {
    return LiteralFlag::kString;
  } else if (raw_literal == ast_constants->symbol_string()) {
    return LiteralFlag::kSymbol;
  } else if (raw_literal == ast_constants->boolean_string()) {
    return LiteralFlag::kBoolean;
  } else if (raw_literal == ast_constants->bigint_string()) {
    return LiteralFlag::kBigInt;
  } else if (raw_literal == ast_constants->undefined_string()) {
    return LiteralFlag::kUndefined;
  } else if (raw_literal == ast_constants->function_string()) {
    return LiteralFlag::kFunction;
  } else if (raw_literal == ast_constants->object_string()) {
    return LiteralFlag::kObject;
  } else {
    return LiteralFlag::kOther;
  }
}

// static
uint8_t TestTypeOfFlags::Encode(LiteralFlag literal_flag) {
  return static_cast<uint8_t>(literal_flag);
}

// static
TestTypeOfFlags::LiteralFlag TestTypeOfFlags::Decode(uint8_t raw_flag) {
  DCHECK_LE(raw_flag, static_cast<uint8_t>(LiteralFlag::kOther));
  return static_cast<LiteralFlag>(raw_flag);
}

// static
uint8_t StoreLookupSlotFlags::Encode(LanguageMode language_mode,
                                     LookupHoistingMode lookup_hoisting_mode) {
  DCHECK_IMPLIES(lookup_hoisting_mode == LookupHoistingMode::kLegacySloppy,
                 language_mode == LanguageMode::kSloppy);
  return LanguageModeBit::encode(language_mode) |
         LookupHoistingModeBit::encode(static_cast<bool>(lookup_hoisting_mode));
}

// static
void StoreLookupSlotFlags::Decode(uint8_t raw_flag, LanguageMode* language_mode,
                                  LookupHoistingMode* lookup_hoisting_mode) {
  *lookup_hoisting_mode =
      static_cast<LookupHoistingMode>(LookupHoistingModeBit::decode(raw_flag));
  *language_mode = LanguageModeBit::decode(raw_flag);
  DCHECK_IMPLIES(*lookup_hoisting_mode == LookupHoistingMode::kLegacySloppy,
                 *language_mode == LanguageMode::kSloppy);
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
