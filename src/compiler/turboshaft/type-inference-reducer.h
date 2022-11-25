// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_TYPE_INFERENCE_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_TYPE_INFERENCE_REDUCER_H_

#include <limits>

#include "src/base/logging.h"
#include "src/base/vector.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/compiler/turboshaft/snapshot-table.h"
#include "src/compiler/turboshaft/types.h"

// #define TRACE_TYPING(...) PrintF(__VA_ARGS__)
#define TRACE_TYPING(...) ((void)0)

namespace v8::internal::compiler::turboshaft {

namespace {

double next_smaller(double v) {
  DCHECK(!std::isnan(v));
  return std::nextafter(v, -std::numeric_limits<double>::infinity());
}

double next_larger(double v) {
  DCHECK(!std::isnan(v));
  return std::nextafter(v, std::numeric_limits<double>::infinity());
}

// Returns the array's least element, ignoring NaN.
// There must be at least one non-NaN element.
// Any -0 is converted to 0.
template <typename T>
T array_min(T a[], size_t n) {
  DCHECK_NE(0, n);
  T x = +std::numeric_limits<T>::infinity();
  for (size_t i = 0; i < n; ++i) {
    if (!std::isnan(a[i])) {
      x = std::min(a[i], x);
    }
  }
  DCHECK(!std::isnan(x));
  return x == T{0} ? T{0} : x;  // -0 -> 0
}

// Returns the array's greatest element, ignoring NaN.
// There must be at least one non-NaN element.
// Any -0 is converted to 0.
template <typename T>
T array_max(T a[], size_t n) {
  DCHECK_NE(0, n);
  T x = -std::numeric_limits<T>::infinity();
  for (size_t i = 0; i < n; ++i) {
    if (!std::isnan(a[i])) {
      x = std::max(a[i], x);
    }
  }
  DCHECK(!std::isnan(x));
  return x == T{0} ? T{0} : x;  // -0 -> 0
}

}  // namespace

template <size_t Bits>
struct WordOperationTyper {
  static_assert(Bits == 32 || Bits == 64);
  using word_t = uint_type<Bits>;
  using type_t = WordType<Bits>;

  static type_t SetToRange(const type_t& set) {
    DCHECK(set.is_set());
    // TODO(nicohartmann@): A wrapping range may be a better fit in some cases.
    return type_t::Range(set.unsigned_min(), set.unsigned_max());
  }

  static Type Add(const type_t& lhs, const type_t& rhs, Zone* zone) {
    if (lhs.is_complete() || rhs.is_complete()) return type_t::Complete();

    // If both sides are decently small sets, we produce the product set.
    if (lhs.is_set() && rhs.is_set()) {
      std::vector<word_t> result_elements;
      for (int i = 0; i < lhs.set_size(); ++i) {
        for (int j = 0; j < rhs.set_size(); ++j) {
          result_elements.push_back(lhs.set_element(i) + rhs.set_element(j));
        }
      }
      base::sort(result_elements);
      auto it = std::unique(result_elements.begin(), result_elements.end());
      result_elements.erase(it, result_elements.end());
      DCHECK(!result_elements.empty());
      if (result_elements.size() <= type_t::kMaxSetSize) {
        return type_t::Set(result_elements, zone);
      }
      // TODO(nicohartmann@): A wrapping range may be a better fit in some
      // cases.
      return type_t::Range(result_elements.front(), result_elements.back());
    }

    // Otherwise just construct a range.
    type_t x = lhs.is_range() ? lhs : SetToRange(lhs);
    type_t y = rhs.is_range() ? rhs : SetToRange(rhs);

    // Check: (lhs.to + rhs.to + 1) - (lhs.from + rhs.from + 1) < max
    // =====> (lhs.to - lhs.from) + (rhs.to - rhs.from) < max
    // =====> (lhs.to - lhs.from) < max - (rhs.to - rhs.from)
    if (!x.is_wrapping() && !y.is_wrapping()) {
      if (x.range_to() - x.range_from() < std::numeric_limits<word_t>::max() -
                                              (y.range_to() - y.range_from())) {
        const word_t result_from = x.range_from() + y.range_from();
        const word_t result_to = x.range_to() + y.range_to();
        return type_t::Range(result_from, result_to);
      } else {
        return type_t::Complete();
      }
    }

    // TODO(nicohartmann@): Improve the wrapping cases.
    return type_t::Complete();
  }

  static Type Subtract(const type_t& lhs, const type_t& rhs, Zone* zone) {
    if (lhs.is_complete() || rhs.is_complete()) return type_t::Complete();

    // If both sides are decently small sets, we produce the product set.
    if (lhs.is_set() && rhs.is_set()) {
      std::vector<word_t> result_elements;
      for (int i = 0; i < lhs.set_size(); ++i) {
        for (int j = 0; j < rhs.set_size(); ++j) {
          result_elements.push_back(lhs.set_element(i) - rhs.set_element(j));
        }
      }
      base::sort(result_elements);
      auto it = std::unique(result_elements.begin(), result_elements.end());
      result_elements.erase(it, result_elements.end());
      DCHECK(!result_elements.empty());
      if (result_elements.size() <= type_t::kMaxSetSize) {
        return type_t::Set(result_elements, zone);
      }
      // TODO(nicohartmann@): A wrapping range may be a better fit in some
      // cases.
      return type_t::Range(result_elements.front(), result_elements.back());
    }

    // Otherwise just construct a range.
    type_t x = lhs.is_range() ? lhs : SetToRange(lhs);
    type_t y = rhs.is_range() ? rhs : SetToRange(rhs);

    if (!x.is_wrapping() && !y.is_wrapping()) {
      const word_t result_from = x.range_from() - y.range_to();
      const word_t result_to = x.range_to() - y.range_from();
      return type_t::Range(result_from, result_to);
    }

    // TODO(nicohartmann@): Improve the wrapping cases.
    return type_t::Complete();
  }
};

template <size_t Bits>
struct FloatOperationTyper {
  static_assert(Bits == 32 || Bits == 64);
  using float_t = std::conditional_t<Bits == 32, float, double>;
  using type_t = FloatType<Bits>;
  static constexpr int kSetThreshold = type_t::kMaxSetSize;

  static type_t Range(float_t min, float_t max, bool maybe_nan, Zone* zone) {
    DCHECK_LE(min, max);
    if (min == max) return Set({min}, maybe_nan, zone);
    return type_t::Range(min, max, maybe_nan ? type_t::Special::kNaN : 0);
  }

  static type_t Set(std::vector<float_t> elements, bool maybe_nan, Zone* zone) {
    base::sort(elements);
    elements.erase(std::unique(elements.begin(), elements.end()),
                   elements.end());
    if (base::erase_if(elements, [](float_t v) { return std::isnan(v); }) > 0) {
      maybe_nan = true;
    }
    return type_t::Set(elements, maybe_nan ? type_t::Special::kNaN : 0, zone);
  }

  // Tries to construct the product of two sets where values are generated using
  // {combine}. Returns Type::Invalid() if a set cannot be constructed (e.g.
  // because the result exceeds the maximal number of set elements).
  static Type ProductSet(const type_t& l, const type_t& r, bool maybe_nan,
                         Zone* zone,
                         std::function<float_t(float_t, float_t)> combine) {
    DCHECK(l.is_set());
    DCHECK(r.is_set());
    std::vector<float_t> results;
    for (int i = 0; i < l.set_size(); ++i) {
      for (int j = 0; j < r.set_size(); ++j) {
        results.push_back(combine(l.set_element(i), r.set_element(j)));
      }
    }
    maybe_nan = (base::erase_if(results,
                                [](float_t v) { return std::isnan(v); }) > 0) ||
                maybe_nan;
    base::sort(results);
    auto it = std::unique(results.begin(), results.end());
    if (std::distance(results.begin(), it) > kSetThreshold)
      return Type::Invalid();
    results.erase(it, results.end());
    return Set(std::move(results), maybe_nan ? type_t::Special::kNaN : 0, zone);
  }

  static Type Add(const type_t& l, const type_t& r, Zone* zone) {
    if (l.is_only_nan() || r.is_only_nan()) return type_t::NaN();
    bool maybe_nan = l.has_nan() || r.has_nan();

    // If both sides are decently small sets, we produce the product set.
    auto combine = [](float_t a, float_t b) { return a + b; };
    if (l.is_set() && r.is_set()) {
      auto result = ProductSet(l, r, maybe_nan, zone, combine);
      if (!result.IsInvalid()) return result;
    }

    // Otherwise just construct a range.
    auto [l_min, l_max] = l.minmax();
    auto [r_min, r_max] = r.minmax();

    float_t results[4];
    results[0] = l_min + r_min;
    results[1] = l_min + r_max;
    results[2] = l_max + r_min;
    results[3] = l_max + r_max;

    int nans = 0;
    for (int i = 0; i < 4; ++i) {
      if (std::isnan(results[i])) ++nans;
    }
    if (nans >= 4) {
      // All combinations of inputs produce NaN.
      return type_t::NaN();
    }
    maybe_nan = maybe_nan || nans > 0;
    const float_t result_min = array_min(results, 4);
    const float_t result_max = array_max(results, 4);
    return Range(result_min, result_max, maybe_nan, zone);
  }

  static Type Subtract(const type_t& l, const type_t& r, Zone* zone) {
    if (l.is_only_nan() || r.is_only_nan()) return type_t::NaN();
    bool maybe_nan = l.has_nan() || r.has_nan();

    // If both sides are decently small sets, we produce the product set.
    auto combine = [](float_t a, float_t b) { return a - b; };
    if (l.is_set() && r.is_set()) {
      auto result = ProductSet(l, r, maybe_nan, zone, combine);
      if (!result.IsInvalid()) return result;
    }

    // Otherwise just construct a range.
    auto [l_min, l_max] = l.minmax();
    auto [r_min, r_max] = r.minmax();

    float_t results[4];
    results[0] = l_min - r_min;
    results[1] = l_min - r_max;
    results[2] = l_max - r_min;
    results[3] = l_max - r_max;

    int nans = 0;
    for (int i = 0; i < 4; ++i) {
      if (std::isnan(results[i])) ++nans;
    }
    if (nans >= 4) {
      // All combinations of inputs produce NaN.
      return type_t::NaN();
    }
    maybe_nan = maybe_nan || nans > 0;
    const float_t result_min = array_min(results, 4);
    const float_t result_max = array_max(results, 4);
    return Range(result_min, result_max, maybe_nan, zone);
  }
};

class Typer {
 public:
  static Type TypeConstant(ConstantOp::Kind kind, ConstantOp::Storage value) {
    switch (kind) {
      case ConstantOp::Kind::kFloat32:
        return Type::Float32(value.float32);
      case ConstantOp::Kind::kFloat64:
        return Type::Float64(value.float64);
      case ConstantOp::Kind::kWord32:
        return Word32Type::Constant(static_cast<uint32_t>(value.integral));
      case ConstantOp::Kind::kWord64:
        return Word64Type::Constant(static_cast<uint64_t>(value.integral));
      default:
        // TODO(nicohartmann@): Support remaining {kind}s.
        return Type::Invalid();
    }
  }

  static Type LeastUpperBound(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsAny() || rhs.IsAny()) return Type::Any();
    if (lhs.IsNone()) return rhs;
    if (rhs.IsNone()) return lhs;

    // TODO(nicohartmann@): We might use more precise types here but currently
    // there is not much benefit in that.
    if (lhs.kind() != rhs.kind()) return Type::Any();

    switch (lhs.kind()) {
      case Type::Kind::kInvalid:
        UNREACHABLE();
      case Type::Kind::kNone:
        UNREACHABLE();
      case Type::Kind::kWord32:
        return Word32Type::LeastUpperBound(lhs.AsWord32(), rhs.AsWord32(),
                                           zone);
      case Type::Kind::kWord64:
        return Word64Type::LeastUpperBound(lhs.AsWord64(), rhs.AsWord64(),
                                           zone);
      case Type::Kind::kFloat32:
        return Float32Type::LeastUpperBound(lhs.AsFloat32(), rhs.AsFloat32(),
                                            zone);
      case Type::Kind::kFloat64:
        return Float64Type::LeastUpperBound(lhs.AsFloat64(), rhs.AsFloat64(),
                                            zone);
      case Type::Kind::kAny:
        UNREACHABLE();
    }
  }

  static Type TypeWord32Add(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    auto l = TruncateWord32Input(lhs, true);
    auto r = TruncateWord32Input(rhs, true);
    return WordOperationTyper<32>::Add(l, r, zone);
  }

  static Type TypeWord32Sub(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    auto l = TruncateWord32Input(lhs, true);
    auto r = TruncateWord32Input(rhs, true);
    return WordOperationTyper<32>::Subtract(l, r, zone);
  }

  static Type TypeWord64Add(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    if (!InputIs(lhs, Type::Kind::kWord64) ||
        !InputIs(rhs, Type::Kind::kWord64)) {
      return Word64Type::Complete();
    }
    const auto& l = lhs.AsWord64();
    const auto& r = rhs.AsWord64();

    return WordOperationTyper<64>::Add(l, r, zone);
  }

  static Type TypeWord64Sub(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    if (!InputIs(lhs, Type::Kind::kWord64) ||
        !InputIs(rhs, Type::Kind::kWord64)) {
      return Word64Type::Complete();
    }

    const auto& l = lhs.AsWord64();
    const auto& r = rhs.AsWord64();

    return WordOperationTyper<64>::Subtract(l, r, zone);
  }

  static Type TypeFloat32Add(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    if (!InputIs(lhs, Type::Kind::kFloat32) ||
        !InputIs(rhs, Type::Kind::kFloat32)) {
      return Type::Float32();
    }
    const auto& l = lhs.AsFloat32();
    const auto& r = rhs.AsFloat32();

    return FloatOperationTyper<32>::Add(l, r, zone);
  }

  static Type TypeFloat32Sub(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    if (!InputIs(lhs, Type::Kind::kFloat32) ||
        !InputIs(rhs, Type::Kind::kFloat32)) {
      return Type::Float32();
    }
    const auto& l = lhs.AsFloat32();
    const auto& r = rhs.AsFloat32();

    return FloatOperationTyper<32>::Subtract(l, r, zone);
  }

  static Type TypeFloat64Add(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    if (!InputIs(lhs, Type::Kind::kFloat64) ||
        !InputIs(rhs, Type::Kind::kFloat64)) {
      return Type::Float64();
    }
    const auto& l = lhs.AsFloat64();
    const auto& r = rhs.AsFloat64();

    return FloatOperationTyper<64>::Add(l, r, zone);
  }

  static Type TypeFloat64Sub(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    if (!InputIs(lhs, Type::Kind::kFloat64) ||
        !InputIs(rhs, Type::Kind::kFloat64)) {
      return Type::Float64();
    }
    const auto& l = lhs.AsFloat64();
    const auto& r = rhs.AsFloat64();

    return FloatOperationTyper<64>::Subtract(l, r, zone);
  }

  static std::pair<Type, Type> SplitWord32Range(
      const Word32Type& lhs, ComparisonOp::Kind comparison_kind, uint32_t rhs,
      Zone* zone) {
    const bool is_less_than =
        comparison_kind == ComparisonOp::Kind::kSignedLessThan ||
        comparison_kind == ComparisonOp::Kind::kUnsignedLessThan;
    const bool is_unsigned_comparison =
        comparison_kind == ComparisonOp::Kind::kUnsignedLessThan ||
        comparison_kind == ComparisonOp::Kind::kUnsignedLessThanOrEqual;

    if (is_unsigned_comparison) {
      if (is_less_than && rhs == 0) return {Type::None(), lhs};
      if (!is_less_than && rhs == std::numeric_limits<uint32_t>::max()) {
        return {lhs, Type::None()};
      }
      auto true_range = Word32Type::Range(0, is_less_than ? rhs - 1 : rhs);
      auto false_range = Word32Type::Range(
          is_less_than ? rhs : rhs + 1, std::numeric_limits<uint32_t>::max());
      return {Word32Type::Intersect(lhs, true_range, zone),
              Word32Type::Intersect(lhs, false_range, zone)};
    } else {
      // TODO(nicohartmann@): Implement this case.
      return {lhs, lhs};
    }
  }

  static std::pair<Type, Type> SplitFloat64Range(
      const Float64Type& lhs, ComparisonOp::Kind comparison_kind, double rhs,
      Zone* zone) {
    DCHECK(comparison_kind == ComparisonOp::Kind::kSignedLessThan ||
           comparison_kind == ComparisonOp::Kind::kSignedLessThanOrEqual);
    const bool is_less_than =
        comparison_kind == ComparisonOp::Kind::kSignedLessThan;

    auto true_range =
        Float64Type::Range(-std::numeric_limits<double>::infinity(),
                           is_less_than ? next_smaller(rhs) : rhs);
    auto false_range = Float64Type::Range(
        is_less_than ? rhs : next_larger(rhs),
        std::numeric_limits<double>::infinity(), Float64Type::kNaN);

    return {Float64Type::Intersect(lhs, true_range, zone),
            Float64Type::Intersect(lhs, false_range, zone)};
  }

  static Word32Type TruncateWord32Input(const Type& input,
                                        bool implicit_word64_narrowing) {
    if (input.IsNone() || input.IsAny()) {
      if (allow_invalid_inputs()) return Word32Type::Complete();
    } else if (input.IsWord32()) {
      return input.AsWord32();
    } else if (input.IsWord64() && implicit_word64_narrowing) {
      // The input is implicitly converted to word32.
      const auto& w64 = input.AsWord64();
      if (auto constant_opt = w64.try_get_constant()) {
        return Word32Type::Constant(static_cast<uint32_t>(*constant_opt));
      }
      // TODO(nicohartmann@): Compute a more precise range here.
      return Word32Type::Complete();
    }
    UNREACHABLE();
  }

  static bool InputIs(const Type& input, Type::Kind expected) {
    if (input.IsInvalid()) {
      if (allow_invalid_inputs()) return false;
    } else if (input.kind() == expected) {
      return true;
    } else if (input.IsAny()) {
      if (allow_invalid_inputs()) return false;
    }
    UNREACHABLE();
  }

  // For now we allow invalid inputs (which will then just lead to very generic
  // typing). Once all operations are implemented, we are going to disable this.
  static bool allow_invalid_inputs() { return true; }
};

template <class Next>
class TypeInferenceReducer : public Next {
  using table_t = SnapshotTable<Type>;

 public:
  using Next::Asm;
  TypeInferenceReducer()
      : types_(Asm().output_graph().operation_types()),
        table_(Asm().phase_zone()),
        op_to_key_mapping_(Asm().phase_zone()),
        block_to_snapshot_mapping_(Asm().input_graph().block_count(),
                                   base::nullopt, Asm().phase_zone()),
        predecessors_(Asm().phase_zone()) {}

  void Bind(Block* new_block, const Block* origin) {
    Next::Bind(new_block, origin);

    if (table_.IsSealed()) {
      DCHECK_NULL(current_block_);
    } else {
      DCHECK_NOT_NULL(current_block_);
      SnapshotTable<Type>::Snapshot snapshot = table_.Seal();

      DCHECK(current_block_->index().valid());
      size_t id = current_block_->index().id();
      if (id >= block_to_snapshot_mapping_.size()) {
        // The table initially contains as many entries as blocks in the input
        // graphs. In most cases, the number of blocks between input and ouput
        // graphs shouldn't grow too much, so a growth factor of 1.5 should be
        // reasonable.
        static constexpr double kGrowthFactor = 1.5;
        size_t new_size = std::max<size_t>(
            id, kGrowthFactor * block_to_snapshot_mapping_.size());
        block_to_snapshot_mapping_.resize(new_size);
      }
      block_to_snapshot_mapping_[id] = std::move(snapshot);
      current_block_ = nullptr;
    }

    predecessors_.clear();

    for (const Block* pred = new_block->LastPredecessor(); pred != nullptr;
         pred = pred->NeighboringPredecessor()) {
      DCHECK_LT(pred->index().id(), block_to_snapshot_mapping_.size());
      base::Optional<table_t::Snapshot> pred_snapshot =
          block_to_snapshot_mapping_[pred->index().id()];
      DCHECK(pred_snapshot.has_value());
      predecessors_.push_back(pred_snapshot.value());
    }
    std::reverse(predecessors_.begin(), predecessors_.end());

    auto MergeTypes = [](table_t::Key,
                         base::Vector<Type> predecessors) -> Type {
      DCHECK_GT(predecessors.size(), 0);
      // TODO(nicohartmann@): Actually merge types.
      return predecessors[0];
    };

    table_.StartNewSnapshot(base::VectorOf(predecessors_), MergeTypes);
    // Update refined types of predecessor.
    if (auto it = type_refinements_.find(new_block);
        it != type_refinements_.end()) {
      // We rely on split-edge form, so this block must have at most one
      // predecessor.
      DCHECK_LE(new_block->PredecessorCount(), 1);
      for (const auto& [op_index, type] : it->second) {
        SetType(op_index, type);
      }
    }

    current_block_ = new_block;
  }

  Type TypeForRepresentation(RegisterRepresentation rep) {
    switch (rep.value()) {
      case RegisterRepresentation::Word32():
        return Word32Type::Complete();
      case RegisterRepresentation::Word64():
        return Word64Type::Complete();
      case RegisterRepresentation::Float32():
        return Type::Float32();
      case RegisterRepresentation::Float64():
        return Type::Float64();

      case RegisterRepresentation::Tagged():
      case RegisterRepresentation::Compressed():
        // TODO(nicohartmann@): Support these representations.
        return Type::Any();
    }
  }

  OpIndex ReducePhi(base::Vector<const OpIndex> inputs,
                    RegisterRepresentation rep) {
    OpIndex index = Next::ReducePhi(inputs, rep);

    Type result_type = Type::None();
    for (const OpIndex input : inputs) {
      Type type = types_[input];
      if (type.IsInvalid()) {
        type = TypeForRepresentation(rep);
      }
      // TODO(nicohartmann@): Should all intermediate types be in the
      // graph_zone()?
      result_type =
          Typer::LeastUpperBound(result_type, type, Asm().graph_zone());
    }

    SetType(index, result_type);
    return index;
  }

  OpIndex ReduceBranch(OpIndex condition, Block* if_true, Block* if_false) {
    OpIndex index = Next::ReduceBranch(condition, if_true, if_false);
    if (!index.valid()) return index;

    // Inspect branch condition.
    const Operation& condition_op = Asm().output_graph().Get(condition);
    const ComparisonOp* comparison_op = condition_op.TryCast<ComparisonOp>();
    // We only handle comparison ops for now.
    if (comparison_op == nullptr) return index;
    Type lhs = GetType(comparison_op->left());
    Type rhs = GetType(comparison_op->right());
    // If we don't have proper types, there is nothing we can do.
    if (lhs.IsInvalid() || rhs.IsInvalid()) return index;

    // TODO(nicohartmann@): Might get rid of this once everything is properly
    // typed.
    if (lhs.IsAny() || rhs.IsAny()) return index;

    Zone* zone = Asm().graph_zone();

    Type l_true = Type::Invalid();
    Type l_false = Type::Invalid();
    switch (comparison_op->rep.value()) {
      case RegisterRepresentation::Word32(): {
        lhs = Typer::TruncateWord32Input(lhs, true);
        rhs = Typer::TruncateWord32Input(rhs, true);
        DCHECK(lhs.IsWord32());
        DCHECK(rhs.IsWord32());
        // For now we only handle constants on the right hand side.
        auto r_constant = rhs.AsWord32().try_get_constant();
        if (!r_constant.has_value()) return index;
        std::tie(l_true, l_false) = Typer::SplitWord32Range(
            lhs.AsWord32(), comparison_op->kind, *r_constant, zone);
        break;
      }
      case RegisterRepresentation::Float64(): {
        DCHECK(lhs.IsFloat64());
        DCHECK(rhs.IsFloat64());
        // For now we only handle constants on the right hand side.
        auto r_constant = rhs.AsFloat64().try_get_constant();
        if (!r_constant.has_value()) return index;
        std::tie(l_true, l_false) = Typer::SplitFloat64Range(
            lhs.AsFloat64(), comparison_op->kind, *r_constant, zone);
        break;
      }
      default:
        // TODO(nicohartmann@): Support remaining reps.
        return index;
    }

    DCHECK(!l_true.IsInvalid());
    DCHECK(!l_false.IsInvalid());
    OpIndex comparison_left = comparison_op->left();

    const std::string branch_str =
        Asm().output_graph().Get(index).ToString().substr(0, 40);
    const std::string comparison_left_str =
        Asm().output_graph().Get(comparison_left).ToString().substr(0, 40);
    USE(branch_str);
    USE(comparison_left_str);
    TRACE_TYPING(
        "\033[32mBr   %3d:%-40s\n  T: %3d:%-40s ~~> %s\n  F: %3d:%-40s ~~> "
        "%s\033[0m\n",
        index.id(), branch_str.c_str(), comparison_left.id(),
        comparison_left_str.c_str(), l_true.ToString().c_str(),
        comparison_left.id(), comparison_left_str.c_str(),
        l_false.ToString().c_str());

    // Refine if_true branch.
    {
      auto& block_refinements = type_refinements_[if_true];
      DCHECK_EQ(block_refinements.find(comparison_left),
                block_refinements.end());
      block_refinements.emplace(comparison_left, l_true);
    }
    // Refine if_false branch.
    {
      auto& block_refinements = type_refinements_[if_false];
      DCHECK_EQ(block_refinements.find(comparison_left),
                block_refinements.end());
      block_refinements.emplace(comparison_left, l_false);
    }

    return index;
  }

  OpIndex ReduceConstant(ConstantOp::Kind kind, ConstantOp::Storage value) {
    OpIndex index = Next::ReduceConstant(kind, value);
    if (!index.valid()) return index;

    Type type = Typer::TypeConstant(kind, value);
    SetType(index, type);
    return index;
  }

  OpIndex ReduceWordBinop(OpIndex left, OpIndex right, WordBinopOp::Kind kind,
                          WordRepresentation rep) {
    OpIndex index = Next::ReduceWordBinop(left, right, kind, rep);
    if (!index.valid()) return index;

    Type result_type = Type::Invalid();
    Type left_type = GetType(left);
    Type right_type = GetType(right);
    Zone* zone = Asm().graph_zone();

    if (!left_type.IsInvalid() && !right_type.IsInvalid()) {
      if (rep == WordRepresentation::Word32()) {
        switch (kind) {
          case WordBinopOp::Kind::kAdd:
            result_type = Typer::TypeWord32Add(left_type, right_type, zone);
            break;
          case WordBinopOp::Kind::kSub:
            result_type = Typer::TypeWord32Sub(left_type, right_type, zone);
            break;
          default:
            // TODO(nicohartmann@): Support remaining {kind}s.
            break;
        }
      } else {
        DCHECK_EQ(rep, WordRepresentation::Word64());
        switch (kind) {
          case WordBinopOp::Kind::kAdd:
            result_type = Typer::TypeWord64Add(left_type, right_type, zone);
            break;
          case WordBinopOp::Kind::kSub:
            result_type = Typer::TypeWord64Sub(left_type, right_type, zone);
            break;
          default:
            // TODO(nicohartmann@): Support remaining {kind}s.
            break;
        }
      }
    }

    SetType(index, result_type);
    return index;
  }

  OpIndex ReduceFloatBinop(OpIndex left, OpIndex right, FloatBinopOp::Kind kind,
                           FloatRepresentation rep) {
    OpIndex index = Next::ReduceFloatBinop(left, right, kind, rep);
    if (!index.valid()) return index;

    Type result_type = Type::Invalid();
    Type left_type = GetType(left);
    Type right_type = GetType(right);
    Zone* zone = Asm().graph_zone();

    if (!left_type.IsInvalid() && !right_type.IsInvalid()) {
      if (rep == FloatRepresentation::Float32()) {
        switch (kind) {
          case FloatBinopOp::Kind::kAdd:
            result_type = Typer::TypeFloat32Add(left_type, right_type, zone);
            break;
          case FloatBinopOp::Kind::kSub:
            result_type = Typer::TypeFloat32Sub(left_type, right_type, zone);
            break;
          default:
            // TODO(nicohartmann@): Support remaining {kind}s.
            break;
        }
      } else {
        DCHECK_EQ(rep, FloatRepresentation::Float64());
        switch (kind) {
          case FloatBinopOp::Kind::kAdd:
            result_type = Typer::TypeFloat64Add(left_type, right_type, zone);
            break;
          case FloatBinopOp::Kind::kSub:
            result_type = Typer::TypeFloat64Sub(left_type, right_type, zone);
            break;
          default:
            // TODO(nicohartmann@): Support remaining {kind}s.
            break;
        }
      }
    }

    SetType(index, result_type);
    return index;
  }

  Type GetType(const OpIndex index) {
    if (auto key = op_to_key_mapping_[index]) return table_.Get(*key);
    return Type::Invalid();
  }

  void SetType(const OpIndex index, const Type& result_type) {
    if (!result_type.IsInvalid()) {
      if (auto key_opt = op_to_key_mapping_[index]) {
        table_.Set(*key_opt, result_type);
        DCHECK(!types_[index].IsInvalid());
      } else {
        auto key = table_.NewKey(Type::None());
        table_.Set(key, result_type);
        types_[index] = result_type;
        op_to_key_mapping_[index] = key;
      }
    }

    TRACE_TYPING(
        "\033[%smType %3d:%-40s ==> %s\033[0m\n",
        (result_type.IsInvalid() ? "31" : "32"), index.id(),
        Asm().output_graph().Get(index).ToString().substr(0, 40).c_str(),
        (result_type.IsInvalid() ? "" : result_type.ToString().c_str()));
  }

 private:
  GrowingSidetable<Type>& types_;
  table_t table_;
  const Block* current_block_ = nullptr;
  GrowingSidetable<base::Optional<table_t::Key>> op_to_key_mapping_;
  ZoneVector<base::Optional<table_t::Snapshot>> block_to_snapshot_mapping_;
  // TODO(nicohartmann@): Redesign this.
  std::unordered_map<const Block*,
                     std::unordered_map<OpIndex, Type, fast_hash<OpIndex>>>
      type_refinements_;

  // {predecessors_} is used during merging, but we use an instance variable for
  // it, in order to save memory and not reallocate it for each merge.
  ZoneVector<table_t::Snapshot> predecessors_;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_TYPE_INFERENCE_REDUCER_H_
