// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_MACHINE_ASSEMBLER_H_
#define V8_COMPILER_TURBOSHAFT_MACHINE_ASSEMBLER_H_

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>

#include "src/base/bits.h"
#include "src/base/functional.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/overflowing-math.h"
#include "src/base/template-utils.h"
#include "src/base/vector.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

// Mask of observed bits.
// Not declared as `enum class` to preserve implicit conversions to integral
// types.
namespace truncation {
enum Truncation : uint64_t {
  kWord8 = 0xff,
  kWord16 = 0xffff,
  kWord32 = 0xffffffff,
  kWord64 = 0xffffffffffffffff
};
}
using truncation::Truncation;

template <class T, class CompatibleValues>
class InOrOut;

template <class T, class CompatibleValues>
struct MatchAttempt {
  InOrOut<T, CompatibleValues>* param;
  T value;
};

struct AllowWord64ToWord32Truncation {
  bool operator()(MachineRepresentation expected_rep,
                  MachineRepresentation op_rep) {
    if (expected_rep == MachineRepresentation::kWord32 &&
        op_rep == MachineRepresentation::kWord64) {
      return true;
    }
    return expected_rep == op_rep;
  }
};

// Either an in- or an out-parameter, depending on if it is initialized with a
// pointer or a value.
template <class T, class CompatibleValues = std::equal_to<T>>
class InOrOut {
 public:
  InOrOut(T* out)  // NOLINT(runtime/explicit)
      : out_param_(out), kind_(Kind::kOut) {}
  template <class CV = CompatibleValues>
  InOrOut(  // NOLINT(runtime/explicit)
      T in,
      std::enable_if_t<std::is_same<CV, std::equal_to<T>>::value>* = nullptr)
      : in_param_(in), kind_(Kind::kIn) {}
  template <class CV = CompatibleValues>
  explicit InOrOut(
      T in,
      std::enable_if_t<!std::is_same<CV, std::equal_to<T>>::value>* = nullptr)
      : in_param_(in), kind_(Kind::kIn) {}

  MatchAttempt<T, CompatibleValues> With(T value) {
    return {this, std::move(value)};
  }

  bool IsInParameter() const { return kind_ == Kind::kIn; }
  bool IsOutParameter() const { return kind_ == Kind::kOut; }

  const T& InParameter() const {
    DCHECK(IsInParameter());
    return in_param_;
  }

  void Match(T value) {
    DCHECK(CanMatch(value));
    switch (kind_) {
      case Kind::kIn:
        return;
      case Kind::kOut:
        *out_param_ = value;
        return;
    }
  }

  bool CanMatch(const T& value) {
    switch (kind_) {
      case Kind::kIn:
        return CompatibleValues()(in_param_, value);
      case Kind::kOut:
        return true;
    }
  }

  bool TryMatch(T value) {
    if (CanMatch(value)) {
      Match(value);
      return true;
    }
    return false;
  }

 private:
  union {
    T* out_param_;
    T in_param_;
  };
  enum class Kind : uint8_t { kIn, kOut };
  Kind kind_;
};

InOrOut<MachineRepresentation, AllowWord64ToWord32Truncation> AllowTruncation(
    MachineRepresentation rep) {
  return InOrOut<MachineRepresentation, AllowWord64ToWord32Truncation>(rep);
}

// Try to match multiple values at once.
// Only assign out-parameters as a side-effect if all of them match.
template <class... T, class... CompatibleValue>
bool TryMatch(MatchAttempt<T, CompatibleValue>... args) {
  bool can_match = base::all(args.param->CanMatch(args.value)...);
  if (!can_match) return false;
  ITERATE_PACK(args.param->Match(args.value));
  return true;
}

template <class Base>
class MachineAssembler
    : public AssemblerInterface<MachineAssembler<Base>, Base> {
 public:
  Graph& graph() { return Base::graph(); }

  MachineAssembler(Graph* graph, Zone* phase_zone)
      : AssemblerInterface<MachineAssembler<Base>, Base>(graph, phase_zone) {}

  OpIndex Change(OpIndex input, ChangeOp::Kind kind, MachineRepresentation from,
                 MachineRepresentation to) {
    if (ShouldSkipOptimizationStep())
      return Base::Change(input, kind, from, to);
    {
      uint64_t input_const;
      if (kind == ChangeOp::Kind::kIntegerTruncate &&
          from == MachineRepresentation::kWord64 &&
          to == MachineRepresentation::kWord32 &&
          MatchWord64Constant(input, &input_const)) {
        return this->Word64Constant(input_const);
      }
    }
    return Base::Change(input, kind, from, to);
  }

  OpIndex Binary(OpIndex left, OpIndex right, BinaryOp::Kind kind,
                 MachineRepresentation rep) {
    if (ShouldSkipOptimizationStep())
      return Base::Binary(left, right, kind, rep);
    // Place constant on the right for commutative operators.
    if (BinaryOp::IsCommutative(kind) && Is<ConstantOp>(left) &&
        !Is<ConstantOp>(right)) {
      return Binary(right, left, kind, rep);
    }
    // integral constant folding
    {
      uint64_t k1, k2;
      if (IsIntegral(rep) &&
          MatchIntegralConstant(left, AllowTruncation(rep), &k1) &&
          MatchIntegralConstant(right, AllowTruncation(rep), &k2)) {
        switch (kind) {
          case BinaryOp::Kind::kAdd:
            return this->IntegralConstant(k1 + k2, rep);
          case BinaryOp::Kind::kMul:
            return this->IntegralConstant(k1 * k2, rep);
          case BinaryOp::Kind::kBitwiseAnd:
            return this->IntegralConstant(k1 & k2, rep);
          case BinaryOp::Kind::kBitwiseOr:
            return this->IntegralConstant(k1 | k2, rep);
          case BinaryOp::Kind::kBitwiseXor:
            return this->IntegralConstant(k1 ^ k2, rep);
          case BinaryOp::Kind::kSub:
            return this->IntegralConstant(k1 - k2, rep);
        }
      }
    }
    // float32 constant folding
    {
      float k1, k2;
      if (rep == MachineRepresentation::kFloat32 &&
          MatchFloat32Constant(left, &k1) && MatchFloat32Constant(right, &k2)) {
        switch (kind) {
          case BinaryOp::Kind::kAdd:
            return this->Float32Constant(k1 + k2);
          case BinaryOp::Kind::kMul:
            return this->Float32Constant(k1 * k2);
          case BinaryOp::Kind::kSub:
            return this->Float32Constant(k1 - k2);
          default:
            UNREACHABLE();
        }
      }
    }
    // float64 constant folding
    {
      double k1, k2;
      if (rep == MachineRepresentation::kFloat64 &&
          MatchFloat64Constant(left, &k1) && MatchFloat64Constant(right, &k2)) {
        switch (kind) {
          case BinaryOp::Kind::kAdd:
            return this->Float64Constant(k1 + k2);
          case BinaryOp::Kind::kMul:
            return this->Float64Constant(k1 * k2);
          case BinaryOp::Kind::kSub:
            return this->Float64Constant(k1 - k2);
          default:
            UNREACHABLE();
        }
      }
    }

    if (Is<ConstantOp>(right)) {
      {
        // (a <op> k1) <op> k2  =>  a <op> (k1 <op> k2)
        OpIndex k2 = right;
        OpIndex a, k1;
        if (BinaryOp::IsAssociative(kind, rep) &&
            MatchBinaryOp(left, &a, &k1, kind, AllowTruncation(rep)) &&
            Is<ConstantOp>(k1)) {
          return Binary(a, Binary(k1, k2, kind, rep), kind, rep);
        }
      }
      switch (kind) {
        case BinaryOp::Kind::kAdd:
        case BinaryOp::Kind::kBitwiseXor:
        case BinaryOp::Kind::kBitwiseOr:
        case BinaryOp::Kind::kSub:
          // left <op> 0  =>  left
          if (MatchZero(right)) {
            return left;
          }
          break;
        case BinaryOp::Kind::kMul:
          // left * 1  =>  left
          if (MatchOne(right)) {
            return left;
          }
          break;
        case BinaryOp::Kind::kBitwiseAnd:
          // left & 0xff..ff => left
          if (MatchIntegralConstant(
                  right, AllowTruncation(rep),
                  rep == MachineRepresentation::kWord64
                      ? std::numeric_limits<uint64_t>::max()
                      : uint64_t{std::numeric_limits<uint32_t>::max()})) {
            return left;
          }
          break;
      }
    }

    if (kind == BinaryOp::Kind::kAdd && IsIntegral(rep)) {
      OpIndex x, y, zero;
      // (0 - x) + y => y - x
      if (MatchSub(left, &zero, &x, AllowTruncation(rep)) && MatchZero(zero)) {
        y = right;
        return this->Sub(y, x, rep);
      }
      // x + (0 - y) => x - y
      if (MatchSub(right, &zero, &y, AllowTruncation(rep)) && MatchZero(zero)) {
        x = left;
        return this->Sub(x, y, rep);
      }
    }

    if (kind == BinaryOp::Kind::kSub && IsIntegral(rep)) {
      // x - x => 0
      if (left == right) {
        return this->IntegralConstant(0, rep);
      }
      // x - K => x + -K
      OpIndex x = left;
      uint64_t k;
      if (MatchIntegralConstant(right, AllowTruncation(rep), &k)) {
        return this->Add(x, this->IntegralConstant(-k, rep), rep);
      }
    }

    if (kind == BinaryOp::Kind::kMul && IsIntegral(rep)) {
      int64_t right_constant;
      if (MatchIntegralConstant(right, AllowTruncation(rep), &right_constant)) {
        // x * 0 => 0
        if (right_constant == 0) {
          return this->IntegralConstant(0, rep);
        }
        // x * 1 => x
        if (right_constant == 1) {
          return left;
        }
        // x * -1 => 0 - x
        if (right_constant == -1) {
          return this->Sub(this->IntegralConstant(0, rep), left, rep);
        }
        // x * 2^n => x << n
        if (base::bits::IsPowerOfTwo(right_constant)) {
          int power = base::bits::WhichPowerOfTwo(right_constant);
          if (power < ElementSizeInBits(rep) - 1) {
            return this->ShiftLeft(left, this->IntegralConstant(power, rep),
                                   rep);
          }
        }
      }
    }

    // TODO(tebbi): Division, modulo optimizations.

    return Base::Binary(left, right, kind, rep);
  }

  OpIndex Equal(OpIndex left, OpIndex right, MachineRepresentation rep) {
    if (ShouldSkipOptimizationStep()) return Base::Equal(left, right, rep);
    if (left == right) {
      return this->Word32Constant(true);
    }
    if (Is<ConstantOp>(left) && !Is<ConstantOp>(right)) {
      return Equal(right, left, rep);
    }
    {
      // Remove Word64 to Word32 truncations.
      OpIndex input;
      if (rep == MachineRepresentation::kWord32 &&
          MatchChange(left, &input, ChangeOp::Kind::kIntegerTruncate,
                      MachineRepresentation::kWord64,
                      MachineRepresentation::kWord32)) {
        return Equal(input, right, rep);
      }
      if (rep == MachineRepresentation::kWord32 &&
          MatchChange(right, &input, ChangeOp::Kind::kIntegerTruncate,
                      MachineRepresentation::kWord64,
                      MachineRepresentation::kWord32)) {
        return Equal(left, input, rep);
      }
    }
    if (Is<ConstantOp>(right)) {
      if (Is<ConstantOp>(left)) {
        // k1 == k2  =>  k
        switch (rep) {
          case MachineRepresentation::kWord32:
          case MachineRepresentation::kWord64: {
            uint64_t k1, k2;
            if (MatchIntegralConstant(left, AllowTruncation(rep), &k1) &&
                MatchIntegralConstant(right, AllowTruncation(rep), &k2)) {
              return this->Word32Constant(k1 == k2);
            }
            break;
          }
          case MachineRepresentation::kFloat32: {
            float k1, k2;
            if (MatchFloat32Constant(left, &k1) &&
                MatchFloat32Constant(right, &k2)) {
              return this->Word32Constant(k1 == k2);
            }
            break;
          }
          case MachineRepresentation::kFloat64: {
            double k1, k2;
            if (MatchFloat64Constant(left, &k1) &&
                MatchFloat64Constant(right, &k2)) {
              return this->Word32Constant(k1 == k2);
            }
            break;
          }
          default:
            UNREACHABLE();
        }
      }
      {
        // x - y == 0  =>  x == y
        OpIndex x, y;
        if (IsIntegral(rep) && MatchZero(right) &&
            MatchSub(left, &x, &y, AllowTruncation(rep))) {
          return Equal(x, y, rep);
        }
      }
      {
        //     ((x >> shift_amount) & mask) == k
        // =>  (x & (mask << shift_amount)) == (k << shift_amount)
        OpIndex shift, x, mask_op;
        int shift_amount;
        uint64_t mask, k;
        ShiftOp::Kind shift_kind;
        if (IsIntegral(rep) &&
            MatchBitwiseAnd(left, &shift, &mask_op, AllowTruncation(rep)) &&
            MatchConstantShift(shift, &x, &shift_kind, AllowTruncation(rep),
                               &shift_amount) &&
            MatchIntegralConstant(mask_op, AllowTruncation(rep), &mask) &&
            MatchIntegralConstant(right, AllowTruncation(rep), &k) &&
            ShiftOp::IsRightShift(shift_kind) &&
            mask <= MaxUnsignedValue(rep) >> shift_amount &&
            k <= MaxUnsignedValue(rep) >> shift_amount) {
          return Equal(this->BitwiseAnd(
                           x, this->Word64Constant(mask << shift_amount), rep),
                       this->Word64Constant(k << shift_amount), rep);
        }
      }
    }
    // TODO(tebbi): Add ObjectMayAlias for WebAssembly.
    return Base::Equal(left, right, rep);
  }

  OpIndex Store(OpIndex base, OpIndex value, StoreOp::Kind kind,
                MachineRepresentation stored_rep,
                WriteBarrierKind write_barrier, int32_t offset) {
    if (ShouldSkipOptimizationStep())
      return Base::Store(base, value, kind, stored_rep, write_barrier, offset);
    return IndexedStore(base, OpIndex::Invalid(), value, kind, stored_rep,
                        write_barrier, offset, 0);
  }

  OpIndex IndexedStore(OpIndex base, OpIndex index, OpIndex value,
                       IndexedStoreOp::Kind kind,
                       MachineRepresentation stored_rep,
                       WriteBarrierKind write_barrier, int32_t offset,
                       uint8_t element_scale) {
    if (!ShouldSkipOptimizationStep()) {
      while (index.valid()) {
        const Operation& index_op = graph().Get(index);
        if (TryAdjustOffset(&offset, index_op, element_scale)) {
          index = OpIndex::Invalid();
          element_scale = 0;
        } else if (index_op.Is<ShiftOp>()) {
          const ShiftOp& shift_op = index_op.Cast<ShiftOp>();
          if (shift_op.kind == ShiftOp::Kind::kShiftLeft &&
              TryAdjustElementScale(&element_scale, shift_op.right())) {
            index = shift_op.left();
            continue;
          }
        } else if (index_op.Is<BinaryOp>()) {
          const BinaryOp& binary_op = index_op.Cast<BinaryOp>();
          if (binary_op.kind == BinaryOp::Kind::kAdd &&
              TryAdjustOffset(&offset, graph().Get(binary_op.right()),
                              element_scale)) {
            index = binary_op.left();
            continue;
          }
        }
        break;
      }
      switch (stored_rep) {
        case MachineRepresentation::kWord8:
          value = ReduceWithTruncation(value, Truncation::kWord8);
          break;
        case MachineRepresentation::kWord16:
          value = ReduceWithTruncation(value, Truncation::kWord16);
          break;
        case MachineRepresentation::kWord32:
          value = ReduceWithTruncation(value, Truncation::kWord32);
          break;
        default:
          break;
      }
    }
    if (index.valid()) {
      return Base::IndexedStore(base, index, value, kind, stored_rep,
                                write_barrier, offset, element_scale);
    } else {
      return Base::Store(base, value, kind, stored_rep, write_barrier, offset);
    }
  }

  OpIndex Load(OpIndex base, LoadOp::Kind kind, MachineType loaded_rep,
               int32_t offset) {
    return IndexedLoad(base, OpIndex::Invalid(), kind, loaded_rep, offset, 0);
  }

  OpIndex IndexedLoad(OpIndex base, OpIndex index, IndexedLoadOp::Kind kind,
                      MachineType loaded_rep, int32_t offset,
                      uint8_t element_scale) {
    while (true) {
      if (ShouldSkipOptimizationStep()) break;
      if (index.valid()) {
        const Operation& index_op = graph().Get(index);
        if (TryAdjustOffset(&offset, index_op, element_scale)) {
          index = OpIndex::Invalid();
          element_scale = 0;
        } else if (index_op.Is<ShiftOp>()) {
          const ShiftOp& shift_op = index_op.Cast<ShiftOp>();
          if (shift_op.kind == ShiftOp::Kind::kShiftLeft &&
              TryAdjustElementScale(&element_scale, shift_op.right())) {
            index = shift_op.left();
            continue;
          }
        } else if (index_op.Is<BinaryOp>()) {
          const BinaryOp& binary_op = index_op.Cast<BinaryOp>();
          if (binary_op.kind == BinaryOp::Kind::kAdd &&
              TryAdjustOffset(&offset, graph().Get(binary_op.right()),
                              element_scale)) {
            index = binary_op.left();
            continue;
          }
        }
      } else {
        DCHECK(!index.valid());
        if (element_scale == 0 &&
            MatchAdd(base, &base, &index,
                     AllowTruncation(MachineType::PointerRepresentation()))) {
          continue;
        }
      }
      break;
    }
    if (index.valid()) {
      return Base::IndexedLoad(base, index, kind, loaded_rep, offset,
                               element_scale);
    } else {
      return Base::Load(base, kind, loaded_rep, offset);
    }
  }

 private:
  bool TryAdjustOffset(int32_t* offset, const Operation& maybe_constant,
                       uint8_t element_scale) {
    if (!maybe_constant.Is<ConstantOp>()) return false;
    const ConstantOp& constant = maybe_constant.Cast<ConstantOp>();
    int64_t diff = constant.signed_integral();
    int32_t new_offset;
    if (diff <= (std::numeric_limits<int32_t>::max() >> element_scale) &&
        diff >= (std::numeric_limits<int32_t>::min() >> element_scale) &&
        !base::bits::SignedAddOverflow32(
            *offset, static_cast<int32_t>(diff) << element_scale,
            &new_offset)) {
      *offset = new_offset;
      return true;
    }
    return false;
  }

  bool TryAdjustElementScale(uint8_t* element_scale, OpIndex maybe_constant) {
    uint64_t diff;
    if (!MatchIntegralConstant(
            maybe_constant,
            AllowTruncation(MachineType::PointerRepresentation()), &diff)) {
      return false;
    }
    DCHECK_LT(*element_scale,
              ElementSizeInBits(MachineType::PointerRepresentation()));
    if (diff < ElementSizeInBits(MachineType::PointerRepresentation()) -
                   *element_scale) {
      *element_scale += diff;
      return true;
    }
    return false;
  }

  template <class Op>
  bool Is(OpIndex op_idx) {
    return graph().Get(op_idx).template Is<Op>();
  }
  template <class Op>
  const Op& Cast(OpIndex op_idx) {
    return graph().Get(op_idx).template Cast<Op>();
  }

  bool MatchZero(OpIndex matched) {
    const Operation& op = graph().Get(matched);
    if (!op.Is<ConstantOp>()) return false;
    const ConstantOp& constant_op = op.Cast<ConstantOp>();
    switch (constant_op.kind) {
      case ConstantOp::Kind::kWord32:
      case ConstantOp::Kind::kWord64:
        return constant_op.integral() == 0;
      case ConstantOp::Kind::kFloat32:
        return constant_op.float32() == 0;
      case ConstantOp::Kind::kFloat64:
        return constant_op.float64() == 0;
      default:
        return false;
    }
  }

  bool MatchOne(OpIndex matched) {
    const Operation& op = graph().Get(matched);
    if (!op.Is<ConstantOp>()) return false;
    const ConstantOp& constant_op = op.Cast<ConstantOp>();
    switch (constant_op.kind) {
      case ConstantOp::Kind::kWord32:
      case ConstantOp::Kind::kWord64:
        return constant_op.integral() == 1;
      case ConstantOp::Kind::kFloat32:
        return constant_op.float32() == 1;
      case ConstantOp::Kind::kFloat64:
        return constant_op.float64() == 1;
      case ConstantOp::Kind::kNumber:
        return constant_op.number() == 1;
      default:
        UNREACHABLE();
    }
  }

  bool MatchFloat32Constant(OpIndex matched, InOrOut<float> constant) {
    const Operation& op = graph().Get(matched);
    if (!op.Is<ConstantOp>()) return false;
    const ConstantOp& constant_op = op.Cast<ConstantOp>();
    if (constant_op.kind != ConstantOp::Kind::kFloat32) return false;
    return constant.TryMatch(constant_op.float32());
  }

  bool MatchFloat64Constant(OpIndex matched, InOrOut<double> constant) {
    const Operation& op = graph().Get(matched);
    if (!op.Is<ConstantOp>()) return false;
    const ConstantOp& constant_op = op.Cast<ConstantOp>();
    if (constant_op.kind != ConstantOp::Kind::kFloat64) return false;
    return constant.TryMatch(constant_op.float64());
  }

  bool MatchIntegralConstant(
      OpIndex matched,
      InOrOut<MachineRepresentation, AllowWord64ToWord32Truncation> rep,
      InOrOut<uint64_t> constant) {
    const Operation& op = graph().Get(matched);
    if (!op.Is<ConstantOp>()) return false;
    const ConstantOp& constant_op = op.Cast<ConstantOp>();
    switch (constant_op.Representation()) {
      case MachineRepresentation::kWord32:
        return TryMatch(rep.With(constant_op.Representation()),
                        constant.With(constant_op.word32()));
      case MachineRepresentation::kWord64: {
        bool truncate = rep.IsInParameter() &&
                        rep.InParameter() == MachineRepresentation::kWord32;
        return TryMatch(rep.With(constant_op.Representation()),
                        constant.With(truncate ? constant_op.word32()
                                               : constant_op.word64()));
      }
      default:
        return false;
    }
  }

  bool MatchWord64Constant(OpIndex matched, InOrOut<uint64_t> constant) {
    return MatchIntegralConstant(
        matched, AllowTruncation(MachineRepresentation::kWord64), constant);
  }

  bool MatchIntegralConstant(
      OpIndex matched,
      InOrOut<MachineRepresentation, AllowWord64ToWord32Truncation> rep,
      int64_t* constant) {
    DCHECK(rep.IsInParameter());
    uint64_t value;
    if (!MatchIntegralConstant(matched, rep, &value)) return false;
    switch (rep.InParameter()) {
      case MachineRepresentation::kWord32:
        *constant = static_cast<int32_t>(value);
        return true;
      case MachineRepresentation::kWord64:
        *constant = static_cast<int64_t>(value);
        return true;
      default:
        UNREACHABLE();
    }
  }

  bool MatchChange(OpIndex matched, InOrOut<OpIndex> input,
                   InOrOut<ChangeOp::Kind> kind,
                   InOrOut<MachineRepresentation> from,
                   InOrOut<MachineRepresentation> to) {
    const Operation& op = graph().Get(matched);
    const ChangeOp* change_op = op.TryCast<ChangeOp>();
    if (!change_op) return false;
    return TryMatch(input.With(change_op->input()), kind.With(change_op->kind),
                    from.With(change_op->from), to.With(change_op->to));
  }

  bool MatchBinaryOp(
      OpIndex matched, OpIndex* left, OpIndex* right,
      InOrOut<BinaryOp::Kind> kind,
      InOrOut<MachineRepresentation, AllowWord64ToWord32Truncation> rep) {
    const Operation& op = graph().Get(matched);
    const BinaryOp* binary_op = op.TryCast<BinaryOp>();
    if (!binary_op) return false;
    if (!BinaryOp::AllowsWord64ToWord32Truncation(binary_op->kind) &&
        rep.IsInParameter() && rep.InParameter() != binary_op->rep) {
      return false;
    }
    return TryMatch(InOrOut<OpIndex>(left).With(binary_op->left()),
                    InOrOut<OpIndex>(right).With(binary_op->right()),
                    kind.With(binary_op->kind), rep.With(binary_op->rep));
  }

  bool MatchAdd(
      OpIndex matched, OpIndex* left, OpIndex* right,
      InOrOut<MachineRepresentation, AllowWord64ToWord32Truncation> rep) {
    return MatchBinaryOp(matched, left, right, BinaryOp::Kind::kAdd, rep);
  }

  bool MatchSub(
      OpIndex matched, OpIndex* left, OpIndex* right,
      InOrOut<MachineRepresentation, AllowWord64ToWord32Truncation> rep) {
    return MatchBinaryOp(matched, left, right, BinaryOp::Kind::kSub, rep);
  }

  bool MatchBitwiseAnd(
      OpIndex matched, OpIndex* left, OpIndex* right,
      InOrOut<MachineRepresentation, AllowWord64ToWord32Truncation> rep) {
    return MatchBinaryOp(matched, left, right, BinaryOp::Kind::kBitwiseAnd,
                         rep);
  }

  bool MatchConstantShift(
      OpIndex matched, OpIndex* input, InOrOut<ShiftOp::Kind> kind,
      InOrOut<MachineRepresentation, AllowWord64ToWord32Truncation> rep,
      InOrOut<int> amount) {
    const Operation& op = graph().Get(matched);
    if (!op.Is<ShiftOp>()) return false;
    const ShiftOp& shift_op = op.Cast<ShiftOp>();
    uint64_t rhs_constant;
    if (MatchIntegralConstant(shift_op.right(), AllowTruncation(shift_op.rep),
                              &rhs_constant) &&
        rhs_constant < static_cast<uint64_t>(ElementSizeInBits(shift_op.rep))) {
      return TryMatch(InOrOut<OpIndex>(input).With(shift_op.left()),
                      kind.With(shift_op.kind), rep.With(shift_op.rep),
                      amount.With(static_cast<int>(rhs_constant)));
      return true;
    }
    return false;
  }

  bool MatchIntegralBinopWithConstant(OpIndex matched,
                                      InOrOut<BinaryOp::Kind> kind,
                                      InOrOut<MachineRepresentation> rep,
                                      OpIndex* input, InOrOut<uint64_t> rhs) {
    OpIndex left, right;
    BinaryOp::Kind op_kind;
    MachineRepresentation op_rep;
    uint64_t rhs_constant;
    if (MatchBinaryOp(matched, &left, &right, &op_kind, &op_rep) &&
        MatchIntegralConstant(right, AllowTruncation(op_rep), &rhs_constant)) {
      return TryMatch(InOrOut<OpIndex>(input).With(left), kind.With(op_kind),
                      rep.With(op_rep), rhs.With(rhs_constant));
    }
    return false;
  }

  uint64_t TruncateIntegral(uint64_t value, MachineRepresentation rep) {
    if (rep == MachineRepresentation::kWord32) {
      return static_cast<uint32_t>(value);
    } else {
      DCHECK_EQ(rep, MachineRepresentation::kWord64);
      return value;
    }
  }

  OpIndex ReduceWithTruncation(OpIndex op_idx, Truncation truncation_mask) {
    {
      // Remove bitwise-and with a mask whose zero-bits are not observed.
      MachineRepresentation rep;
      OpIndex input;
      uint64_t mask;
      if (MatchIntegralBinopWithConstant(op_idx, BinaryOp::Kind::kAdd, &rep,
                                         &input, &mask) &&
          (mask & truncation_mask) == truncation_mask) {
        return ReduceWithTruncation(input, truncation_mask);
      }
    }
    {
      int left_shift_amount;
      MachineRepresentation rep;
      OpIndex right_shift;
      ShiftOp::Kind right_shift_kind;
      int right_shift_amount;
      OpIndex right_shift_input;
      if (MatchConstantShift(op_idx, &right_shift, ShiftOp::Kind::kShiftLeft,
                             &rep, &left_shift_amount) &&
          MatchConstantShift(right_shift, &right_shift_input, &right_shift_kind,
                             AllowTruncation(rep), &right_shift_amount) &&
          ShiftOp::IsRightShift(right_shift_kind)) {
        uint64_t preserved_bits = truncation_mask;
        preserved_bits =
            TruncateIntegral(preserved_bits << right_shift_amount, rep);
        preserved_bits =
            TruncateIntegral(preserved_bits >> left_shift_amount, rep);
        if (left_shift_amount == right_shift_amount &&
            preserved_bits == truncation_mask) {
          return right_shift_input;
        } else if (left_shift_amount < right_shift_amount &&
                   preserved_bits >> (right_shift_amount - left_shift_amount) ==
                       truncation_mask) {
          OpIndex shift_amount = this->IntegralConstant(
              right_shift_amount - left_shift_amount, rep);
          return this->Shift(right_shift_input, shift_amount, right_shift_kind,
                             rep);
        } else if (left_shift_amount > right_shift_amount &&
                   preserved_bits << (left_shift_amount - right_shift_amount) ==
                       truncation_mask) {
          OpIndex shift_amount = this->IntegralConstant(
              left_shift_amount - right_shift_amount, rep);
          return this->Shift(right_shift_input, shift_amount,
                             ShiftOp::Kind::kShiftLeft, rep);
        }
      }
    }
    return op_idx;
  }
};

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOSHAFT_MACHINE_ASSEMBLER_H_
