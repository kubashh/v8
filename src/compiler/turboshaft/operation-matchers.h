// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_OPERATION_MATCHERS_H_
#define V8_COMPILER_TURBOSHAFT_OPERATION_MATCHERS_H_

#include <tuple>
#include <utility>

#include "src/compiler/turboshaft/cfg.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

// struct Matcher {
// };

// template<class... Values>
// struct MatchResult: base::Optional<std::tuple<Values...>> {
//     using base::Optional<std::tuple<Values...>>::Optional;

//     template<class... OtherValues>
//     MatchResult<Values..., OtherValues...> Extend(const
//     MatchResult<OtherValues...>& other) {
//         if (this->has_value() && other.has_value()) {
//             return {std::tuple_cat(**this, *other)};
//         }
//         return base::nullopt;
//     }
//     MatchResult Extend() { return *this; }
//     template<class A, class B, class... Rest>
//     auto Extend(A a, B b, Rest... rest) {
//         return Extend(a).Extend(b, rest...);
//     }
// };

// template<class Op, class... SubMatchers>
// struct OpAndInputsMatcher: Matcher {
//     std::tuple<SubMatchers...> children;

//     using ResultType =
//     decltype(MatchResult<>().Extend(std::declval<SubMatchers>().Match(std::declval<Operation>(),
//     std::declval<Graph>())...));

//     ResultType Match(const Operation& op, const Graph& graph) {
//         if (auto* casted_op = op.TryCast<Op>()) {
//             base::Vector<const OpIndex> inputs = casted_op->Inputs();
//             if (sizeof...(SubMatchers) != inputs.size()) return {};
//             return MatchInput<0>(inputs, graph);
//         }
//         return {};
//     }

//     template<size_t i>
//     auto MatchInput(base::Vector<OpIndex> inputs, const Graph& graph) {
//         auto child_result = std::get<i>(children).Match(graph.Get(inputs[i]),
//         graph); return child_result.Extend(MatchInput<i+1>(inputs, graph));
//     }
//     template<>
//     MatchResult<> MatchInput<sizeof...(SubMatchers)>(ResultType* result,
//     base::Vector<OpIndex> inputs, const Graph& graph) {
//         return MatchResult<>{std::tuple<>{}};
//     }
// };

// template<class Op, class... SubMatchers>
// struct OpAndInputsMatcherYieldingOperation: Matcher {
//     std::tuple<SubMatchers...> children;

//     using ResultType = decltype(MatchResult<const
//     Op&>().Extend(std::declval<SubMatchers>().Match(std::declval<Operation>(),
//     std::declval<Graph>())...));

//     ResultType Match(const Operation& op, const Graph& graph) {
//         if (auto* casted_op = op.TryCast<Op>()) {
//             base::Vector<const OpIndex> inputs = casted_op->Inputs();
//             if (sizeof...(SubMatchers) != inputs.size()) return {};
//             return MatchResult<const Op&>(std::tuple<const
//             Op&>{op}).MatchInput<0>(inputs, graph);
//         }
//         return {};
//     }

//     template<size_t i>
//     auto MatchInput(base::Vector<OpIndex> inputs, const Graph& graph) {
//         auto child_result = std::get<i>(children).Match(graph.Get(inputs[i]),
//         graph); return child_result.Extend(MatchInput<i+1>(inputs, graph));
//     }
//     template<>
//     MatchResult<> MatchInput<sizeof...(SubMatchers)>(ResultType* result,
//     base::Vector<OpIndex> inputs, const Graph& graph) {
//         return MatchResult<>{std::tuple<>{}};
//     }
// };

// struct WildcardMatcher: Matcher {
//     using ResultType = MatchResult<const Operation&>;

//     ResultType Match(const Operation& op, const Graph& graph) {
//         return {op};
//     }
// };

// template<class Op>
// struct OpMatcher: Matcher {
//     using ResultType = MatchResult<const Operation&>;

//     ResultType Match(const Operation& op, const Graph& graph) {
//         return {op};
//     }
// };

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOSHAFT_OPERATION_MATCHERS_H_
