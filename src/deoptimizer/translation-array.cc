// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/deoptimizer/translation-array.h"

#include "src/base/vlq.h"
#include "src/deoptimizer/translated-state.h"
#include "src/objects/fixed-array-inl.h"

#ifdef V8_USE_ZLIB
#include "third_party/zlib/google/compression_utils_portable.h"
#endif  // V8_USE_ZLIB

namespace v8 {
namespace internal {

namespace {

#ifdef V8_USE_ZLIB
// Constants describing compressed TranslationArray layout. Only relevant if
// --turbo-compress-translation-arrays is enabled.
constexpr int kUncompressedSizeOffset = 0;
constexpr int kUncompressedSizeSize = kInt32Size;
constexpr int kCompressedDataOffset =
    kUncompressedSizeOffset + kUncompressedSizeSize;
constexpr int kTranslationArrayElementSize = kInt32Size;
#endif  // V8_USE_ZLIB

}  // namespace

TranslationArrayIterator::TranslationArrayIterator(TranslationArray buffer,
                                                   int index)
    : buffer_(buffer) {
  state_[0].index = index;
  state_[0].remaining_ops_to_use_from_previous_translation = 0;
#ifdef V8_USE_ZLIB
  if (V8_UNLIKELY(v8_flags.turbo_compress_translation_arrays)) {
    const int size = buffer_.get_int(kUncompressedSizeOffset);
    uncompressed_contents_.insert(uncompressed_contents_.begin(), size, 0);

    uLongf uncompressed_size = size * kTranslationArrayElementSize;

    CHECK_EQ(zlib_internal::UncompressHelper(
                 zlib_internal::ZRAW,
                 base::bit_cast<Bytef*>(uncompressed_contents_.data()),
                 &uncompressed_size,
                 buffer_.GetDataStartAddress() + kCompressedDataOffset,
                 buffer_.DataSize()),
             Z_OK);
    DCHECK(index >= 0 && index < size);
    return;
  }
#endif  // V8_USE_ZLIB
  DCHECK(!v8_flags.turbo_compress_translation_arrays);
  DCHECK(index >= 0 && index < buffer.length());
  // Starting at a location other than a BEGIN would make
  // MATCH_PREVIOUS_TRANSLATION instructions not work.
  DCHECK_EQ(buffer_.GetDataStartAddress()[index],
            static_cast<byte>(TranslationOpcode::BEGIN));
}

int32_t TranslationArrayIterator::NextOperand() {
  if (V8_UNLIKELY(v8_flags.turbo_compress_translation_arrays)) {
    return uncompressed_contents_[state_[0].index++];
  }
  for (int i = 0; i < num_states_; ++i) {
    if (state_[i].remaining_ops_to_use_from_previous_translation == 0) {
      int32_t value =
          base::VLQDecode(buffer_.GetDataStartAddress(), &state_[i].index);
      DCHECK_LE(state_[i].index, buffer_.length());
      return value;
    }
  }
  UNREACHABLE();
}

uint32_t TranslationArrayIterator::NextOperandUnsigned() {
  if (V8_UNLIKELY(v8_flags.turbo_compress_translation_arrays)) {
    return uncompressed_contents_[state_[0].index++];
  }
  for (int i = 0; i < num_states_; ++i) {
    if (state_[i].remaining_ops_to_use_from_previous_translation == 0) {
      int32_t value = base::VLQDecodeUnsigned(buffer_.GetDataStartAddress(),
                                              &state_[i].index);
      DCHECK_LE(state_[i].index, buffer_.length());
      return value;
    }
  }
  UNREACHABLE();
}

TranslationOpcode TranslationArrayIterator::NextOpcode() {
  if (V8_UNLIKELY(v8_flags.turbo_compress_translation_arrays)) {
    return static_cast<TranslationOpcode>(NextOperandUnsigned());
  }
  std::pair<TranslationOpcode, int> result = NextOpcodeInternal(0);
  DCHECK_LT(static_cast<uint32_t>(result.first), kNumTranslationOpcodes);
  // We already have the answer, but we need to update the rest of the internal
  // iterator states so that future MATCH_PREVIOUS_TRANSLATION instructions
  // work.
  if (result.first == TranslationOpcode::BEGIN) {
    int i = result.second;
    DCHECK_EQ(i, 0);  // BEGIN is never replaced by MATCH_PREVIOUS_TRANSLATION.
    int index_of_lookback_distance = state_[0].index;
    int temp_index = index_of_lookback_distance;
    // The first argument for BEGIN is the distance, in bytes, since the
    // previous BEGIN, or zero to indicate that MATCH_PREVIOUS_TRANSLATION will
    // not be used in this translation.
    uint32_t lookback_distance =
        base::VLQDecodeUnsigned(buffer_.GetDataStartAddress(), &temp_index);
    while (lookback_distance != 0) {
      ++i;
      DCHECK_LT(i, kNumInternalStates);
      index_of_lookback_distance -= lookback_distance;
      state_[i].index = index_of_lookback_distance;
      state_[i].remaining_ops_to_use_from_previous_translation = 0;
      // We're not actually reading the BEGIN opcode, but it should still be
      // there.
      DCHECK_EQ(buffer_.GetDataStartAddress()[state_[i].index - 1],
                static_cast<byte>(TranslationOpcode::BEGIN));
      // Read the lookback distance, and skip the other operands.
      lookback_distance = base::VLQDecodeUnsigned(buffer_.GetDataStartAddress(),
                                                  &state_[i].index);
      DCHECK_EQ(TranslationOpcodeOperandCount(TranslationOpcode::BEGIN), 4);
      for (int j = 0; j < 3; ++j) {
        base::VLQDecodeUnsigned(buffer_.GetDataStartAddress(),
                                &state_[i].index);
      }
    }
    num_states_ = i + 1;
  } else {
    for (int i = result.second + 1; i < num_states_; ++i) {
      TranslationOpcode opcode;
      std::tie(opcode, i) = NextOpcodeInternal(i);
      if (opcode == TranslationOpcode::BEGIN) {
        // No lookback is possible past this point.
        num_states_ = i;
        break;
      }
      int operand_count = TranslationOpcodeOperandCount(opcode);
      while (operand_count != 0) {
        base::VLQDecodeUnsigned(buffer_.GetDataStartAddress(),
                                &state_[i].index);
        --operand_count;
      }
    }
  }
  return result.first;
}

std::pair<TranslationOpcode, int> TranslationArrayIterator::NextOpcodeInternal(
    int state_index) {
  for (int i = state_index; i < num_states_; ++i) {
    if (state_[i].remaining_ops_to_use_from_previous_translation) {
      --state_[i].remaining_ops_to_use_from_previous_translation;
    }
    if (state_[i].remaining_ops_to_use_from_previous_translation) {
      continue;
    }
    TranslationOpcode opcode =
        static_cast<TranslationOpcode>(base::VLQDecodeUnsigned(
            buffer_.GetDataStartAddress(), &state_[i].index));
    DCHECK_LE(state_[i].index, buffer_.length());
    DCHECK_LT(static_cast<uint32_t>(opcode), kNumTranslationOpcodes);
    if (opcode == TranslationOpcode::MATCH_PREVIOUS_TRANSLATION) {
      state_[i].remaining_ops_to_use_from_previous_translation =
          base::VLQDecodeUnsigned(buffer_.GetDataStartAddress(),
                                  &state_[i].index);
      continue;
    }
    return {opcode, i};
  }
  UNREACHABLE();
}

bool TranslationArrayIterator::HasNextOpcode() const {
  if (V8_UNLIKELY(v8_flags.turbo_compress_translation_arrays)) {
    return state_[0].index < static_cast<int>(uncompressed_contents_.size());
  }
  return state_[0].index < buffer_.length() ||
         state_[0].remaining_ops_to_use_from_previous_translation > 1;
}

int TranslationArrayBuilder::BeginTranslation(int frame_count,
                                              int jsframe_count,
                                              int update_feedback_count) {
  FinishPendingInstructionIfNeeded();
  int start_index = Size();
  auto opcode = TranslationOpcode::BEGIN;
  int distance_from_last_start = 0;

  if (translations_til_reset_ == 0) {
    translations_til_reset_ = kMaxLookback;
    recent_instructions_.clear();
  } else {
    --translations_til_reset_;
    distance_from_last_start = start_index - index_of_last_translation_start_;
    recent_instructions_.resize(instruction_index_within_translation_);
  }
  instruction_index_within_translation_ = 0;
  index_of_last_translation_start_ = start_index;

  // BEGIN instructions can't be replaced by MATCH_PREVIOUS_TRANSLATION, so
  // write the data directly rather than calling Add().
  AddRawUnsigned(static_cast<uint32_t>(opcode));
  AddRawUnsigned(distance_from_last_start);
  AddRawSigned(frame_count);
  AddRawSigned(jsframe_count);
  AddRawSigned(update_feedback_count);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 4);
  return start_index;
}

void TranslationArrayBuilder::AddRawSigned(int32_t value) {
  if (V8_UNLIKELY(v8_flags.turbo_compress_translation_arrays)) {
    contents_for_compression_.push_back(value);
  } else {
    base::VLQEncode(&contents_, value);
  }
}

void TranslationArrayBuilder::AddRawUnsigned(uint32_t value) {
  if (V8_UNLIKELY(v8_flags.turbo_compress_translation_arrays)) {
    contents_for_compression_.push_back(value);
  } else {
    base::VLQEncodeUnsigned(&contents_, value);
  }
}

void TranslationArrayBuilder::FinishPendingInstructionIfNeeded() {
  if (matching_instructions_count_) {
    contents_.push_back(
        static_cast<byte>(TranslationOpcode::MATCH_PREVIOUS_TRANSLATION));
    base::VLQEncodeUnsigned(
        &contents_, static_cast<uint32_t>(matching_instructions_count_));
    matching_instructions_count_ = 0;
  }
}

void TranslationArrayBuilder::Add(
    const TranslationArrayBuilder::Instruction& instruction, int value_count) {
  DCHECK_EQ(value_count, TranslationOpcodeOperandCount(instruction.opcode));
  if (V8_UNLIKELY(v8_flags.turbo_compress_translation_arrays)) {
    AddRawUnsigned(static_cast<byte>(instruction.opcode));
    for (int i = 0; i < value_count; ++i) {
      AddRawUnsigned(instruction.operands[i]);
    }
    return;
  }
  bool within_array =
      instruction_index_within_translation_ < recent_instructions_.size();
  if (within_array &&
      instruction ==
          recent_instructions_[instruction_index_within_translation_]) {
    ++matching_instructions_count_;
  } else {
    FinishPendingInstructionIfNeeded();
    AddRawUnsigned(static_cast<byte>(instruction.opcode));
    for (int i = 0; i < value_count; ++i) {
      AddRawUnsigned(instruction.operands[i]);
    }
    if (within_array) {
      recent_instructions_[instruction_index_within_translation_] = instruction;
    } else {
      recent_instructions_.push_back(instruction);
    }
  }
  ++instruction_index_within_translation_;
}

void TranslationArrayBuilder::AddWithNoOperands(TranslationOpcode opcode) {
  AddWithUnsignedOperands(0, opcode);
}

void TranslationArrayBuilder::AddWithSignedOperand(TranslationOpcode opcode,
                                                   int32_t operand) {
  if (V8_UNLIKELY(v8_flags.turbo_compress_translation_arrays)) {
    AddWithUnsignedOperand(opcode, operand);
  } else {
    AddWithUnsignedOperand(opcode, base::VLQConvertToUnsigned(operand));
  }
}

void TranslationArrayBuilder::AddWithSignedOperands(
    int operand_count, TranslationOpcode opcode, int32_t operand_1,
    int32_t operand_2, int32_t operand_3, int32_t operand_4,
    int32_t operand_5) {
  if (V8_UNLIKELY(v8_flags.turbo_compress_translation_arrays)) {
    AddWithUnsignedOperands(operand_count, opcode, operand_1, operand_2,
                            operand_3, operand_4, operand_5);
  } else {
    AddWithUnsignedOperands(operand_count, opcode,
                            base::VLQConvertToUnsigned(operand_1),
                            base::VLQConvertToUnsigned(operand_2),
                            base::VLQConvertToUnsigned(operand_3),
                            base::VLQConvertToUnsigned(operand_4),
                            base::VLQConvertToUnsigned(operand_5));
  }
}

void TranslationArrayBuilder::AddWithUnsignedOperand(TranslationOpcode opcode,
                                                     uint32_t operand) {
  AddWithUnsignedOperands(1, opcode, operand);
}

void TranslationArrayBuilder::AddWithUnsignedOperands(
    int operand_count, TranslationOpcode opcode, uint32_t operand_1,
    uint32_t operand_2, uint32_t operand_3, uint32_t operand_4,
    uint32_t operand_5) {
  Instruction instruction;
  instruction.opcode = opcode;
  instruction.operands[0] = operand_1;
  instruction.operands[1] = operand_2;
  instruction.operands[2] = operand_3;
  instruction.operands[3] = operand_4;
  instruction.operands[4] = operand_5;
  Add(instruction, operand_count);
}

Handle<TranslationArray> TranslationArrayBuilder::ToTranslationArray(
    Factory* factory) {
#ifdef V8_USE_ZLIB
  if (V8_UNLIKELY(v8_flags.turbo_compress_translation_arrays)) {
    const int input_size = SizeInBytes();
    uLongf compressed_data_size = compressBound(input_size);

    ZoneVector<byte> compressed_data(compressed_data_size, zone());

    CHECK_EQ(
        zlib_internal::CompressHelper(
            zlib_internal::ZRAW, compressed_data.data(), &compressed_data_size,
            base::bit_cast<const Bytef*>(contents_for_compression_.data()),
            input_size, Z_DEFAULT_COMPRESSION, nullptr, nullptr),
        Z_OK);

    const int translation_array_size =
        static_cast<int>(compressed_data_size) + kUncompressedSizeSize;
    Handle<TranslationArray> result =
        factory->NewByteArray(translation_array_size, AllocationType::kOld);

    result->set_int(kUncompressedSizeOffset, Size());
    std::memcpy(result->GetDataStartAddress() + kCompressedDataOffset,
                compressed_data.data(), compressed_data_size);

    return result;
  }
#endif
  DCHECK(!v8_flags.turbo_compress_translation_arrays);
  FinishPendingInstructionIfNeeded();
  Handle<TranslationArray> result =
      factory->NewByteArray(SizeInBytes(), AllocationType::kOld);
  memcpy(result->GetDataStartAddress(), contents_.data(),
         contents_.size() * sizeof(uint8_t));
  if (v8_flags.enable_slow_asserts) {
    // Check that the last translation has the stuff we intended.
    recent_instructions_.resize(instruction_index_within_translation_);
    TranslationArrayIterator it(*result, index_of_last_translation_start_);
    CHECK_EQ(it.NextOpcode(), TranslationOpcode::BEGIN);
    it.SkipOperands(4);
    for (size_t i = 0; i < recent_instructions_.size(); ++i) {
      CHECK(it.HasNextOpcode());
      const Instruction& instruction = recent_instructions_[i];
      CHECK_EQ(instruction.opcode, it.NextOpcode());
      for (int j = 0; j < TranslationOpcodeOperandCount(instruction.opcode);
           ++j) {
        CHECK_EQ(instruction.operands[j], it.NextOperandUnsigned());
      }
    }
  }
  return result;
}

void TranslationArrayBuilder::BeginBuiltinContinuationFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height) {
  auto opcode = TranslationOpcode::BUILTIN_CONTINUATION_FRAME;
  AddWithSignedOperands(3, opcode, bytecode_offset.ToInt(), literal_id, height);
}

#if V8_ENABLE_WEBASSEMBLY
void TranslationArrayBuilder::BeginJSToWasmBuiltinContinuationFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height,
    base::Optional<wasm::ValueKind> return_kind) {
  auto opcode = TranslationOpcode::JS_TO_WASM_BUILTIN_CONTINUATION_FRAME;
  AddWithSignedOperands(
      4, opcode, bytecode_offset.ToInt(), literal_id, height,
      return_kind ? static_cast<int>(return_kind.value()) : kNoWasmReturnKind);
}
#endif  // V8_ENABLE_WEBASSEMBLY

void TranslationArrayBuilder::BeginJavaScriptBuiltinContinuationFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height) {
  auto opcode = TranslationOpcode::JAVA_SCRIPT_BUILTIN_CONTINUATION_FRAME;
  AddWithSignedOperands(3, opcode, bytecode_offset.ToInt(), literal_id, height);
}

void TranslationArrayBuilder::BeginJavaScriptBuiltinContinuationWithCatchFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height) {
  auto opcode =
      TranslationOpcode::JAVA_SCRIPT_BUILTIN_CONTINUATION_WITH_CATCH_FRAME;
  AddWithSignedOperands(3, opcode, bytecode_offset.ToInt(), literal_id, height);
}

void TranslationArrayBuilder::BeginConstructStubFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height) {
  auto opcode = TranslationOpcode::CONSTRUCT_STUB_FRAME;
  AddWithSignedOperands(3, opcode, bytecode_offset.ToInt(), literal_id, height);
}

void TranslationArrayBuilder::BeginInlinedExtraArguments(int literal_id,
                                                         unsigned height) {
  auto opcode = TranslationOpcode::INLINED_EXTRA_ARGUMENTS;
  AddWithSignedOperands(2, opcode, literal_id, height);
}

void TranslationArrayBuilder::BeginInterpretedFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height,
    int return_value_offset, int return_value_count) {
  auto opcode = TranslationOpcode::INTERPRETED_FRAME;
  AddWithSignedOperands(5, opcode, bytecode_offset.ToInt(), literal_id, height,
                        return_value_offset, return_value_count);
}

void TranslationArrayBuilder::ArgumentsElements(CreateArgumentsType type) {
  auto opcode = TranslationOpcode::ARGUMENTS_ELEMENTS;
  AddWithSignedOperand(opcode, static_cast<uint8_t>(type));
}

void TranslationArrayBuilder::ArgumentsLength() {
  auto opcode = TranslationOpcode::ARGUMENTS_LENGTH;
  AddWithNoOperands(opcode);
}

void TranslationArrayBuilder::BeginCapturedObject(int length) {
  auto opcode = TranslationOpcode::CAPTURED_OBJECT;
  AddWithSignedOperand(opcode, length);
}

void TranslationArrayBuilder::DuplicateObject(int object_index) {
  auto opcode = TranslationOpcode::DUPLICATED_OBJECT;
  AddWithSignedOperand(opcode, object_index);
}

static uint32_t RegisterToUint32(Register reg) {
  static_assert(Register::kNumRegisters - 1 <= base::kDataMask);
  return static_cast<byte>(reg.code());
}

void TranslationArrayBuilder::StoreRegister(Register reg) {
  auto opcode = TranslationOpcode::REGISTER;
  AddWithUnsignedOperand(opcode, RegisterToUint32(reg));
}

void TranslationArrayBuilder::StoreInt32Register(Register reg) {
  auto opcode = TranslationOpcode::INT32_REGISTER;
  AddWithUnsignedOperand(opcode, RegisterToUint32(reg));
}

void TranslationArrayBuilder::StoreInt64Register(Register reg) {
  auto opcode = TranslationOpcode::INT64_REGISTER;
  AddWithUnsignedOperand(opcode, RegisterToUint32(reg));
}

void TranslationArrayBuilder::StoreSignedBigInt64Register(Register reg) {
  auto opcode = TranslationOpcode::SIGNED_BIGINT64_REGISTER;
  AddWithUnsignedOperand(opcode, RegisterToUint32(reg));
}

void TranslationArrayBuilder::StoreUnsignedBigInt64Register(Register reg) {
  auto opcode = TranslationOpcode::UNSIGNED_BIGINT64_REGISTER;
  AddWithUnsignedOperand(opcode, RegisterToUint32(reg));
}

void TranslationArrayBuilder::StoreUint32Register(Register reg) {
  auto opcode = TranslationOpcode::UINT32_REGISTER;
  AddWithUnsignedOperand(opcode, RegisterToUint32(reg));
}

void TranslationArrayBuilder::StoreBoolRegister(Register reg) {
  auto opcode = TranslationOpcode::BOOL_REGISTER;
  AddWithUnsignedOperand(opcode, RegisterToUint32(reg));
}

void TranslationArrayBuilder::StoreFloatRegister(FloatRegister reg) {
  static_assert(FloatRegister::kNumRegisters - 1 <= base::kDataMask);
  auto opcode = TranslationOpcode::FLOAT_REGISTER;
  AddWithUnsignedOperand(opcode, static_cast<byte>(reg.code()));
}

void TranslationArrayBuilder::StoreDoubleRegister(DoubleRegister reg) {
  static_assert(DoubleRegister::kNumRegisters - 1 <= base::kDataMask);
  auto opcode = TranslationOpcode::DOUBLE_REGISTER;
  AddWithUnsignedOperand(opcode, static_cast<byte>(reg.code()));
}

void TranslationArrayBuilder::StoreStackSlot(int index) {
  auto opcode = TranslationOpcode::STACK_SLOT;
  AddWithSignedOperand(opcode, index);
}

void TranslationArrayBuilder::StoreInt32StackSlot(int index) {
  auto opcode = TranslationOpcode::INT32_STACK_SLOT;
  AddWithSignedOperand(opcode, index);
}

void TranslationArrayBuilder::StoreInt64StackSlot(int index) {
  auto opcode = TranslationOpcode::INT64_STACK_SLOT;
  AddWithSignedOperand(opcode, index);
}

void TranslationArrayBuilder::StoreSignedBigInt64StackSlot(int index) {
  auto opcode = TranslationOpcode::SIGNED_BIGINT64_STACK_SLOT;
  AddWithSignedOperand(opcode, index);
}

void TranslationArrayBuilder::StoreUnsignedBigInt64StackSlot(int index) {
  auto opcode = TranslationOpcode::UNSIGNED_BIGINT64_STACK_SLOT;
  AddWithSignedOperand(opcode, index);
}

void TranslationArrayBuilder::StoreUint32StackSlot(int index) {
  auto opcode = TranslationOpcode::UINT32_STACK_SLOT;
  AddWithSignedOperand(opcode, index);
}

void TranslationArrayBuilder::StoreBoolStackSlot(int index) {
  auto opcode = TranslationOpcode::BOOL_STACK_SLOT;
  AddWithSignedOperand(opcode, index);
}

void TranslationArrayBuilder::StoreFloatStackSlot(int index) {
  auto opcode = TranslationOpcode::FLOAT_STACK_SLOT;
  AddWithSignedOperand(opcode, index);
}

void TranslationArrayBuilder::StoreDoubleStackSlot(int index) {
  auto opcode = TranslationOpcode::DOUBLE_STACK_SLOT;
  AddWithSignedOperand(opcode, index);
}

void TranslationArrayBuilder::StoreLiteral(int literal_id) {
  auto opcode = TranslationOpcode::LITERAL;
  AddWithSignedOperand(opcode, literal_id);
}

void TranslationArrayBuilder::StoreOptimizedOut() {
  auto opcode = TranslationOpcode::OPTIMIZED_OUT;
  AddWithNoOperands(opcode);
}

void TranslationArrayBuilder::AddUpdateFeedback(int vector_literal, int slot) {
  auto opcode = TranslationOpcode::UPDATE_FEEDBACK;
  AddWithSignedOperands(2, opcode, vector_literal, slot);
}

void TranslationArrayBuilder::StoreJSFrameFunction() {
  StoreStackSlot((StandardFrameConstants::kCallerPCOffset -
                  StandardFrameConstants::kFunctionOffset) /
                 kSystemPointerSize);
}

}  // namespace internal
}  // namespace v8
