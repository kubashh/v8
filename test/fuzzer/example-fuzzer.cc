// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "src/base/utils/random-number-generator.h"
#include "src/base/vector.h"
#include "test/unittests/fuzztest.h"
#include "testing/gtest/include/gtest/gtest.h"
// #include "test/unittests/test-utils.h"

// ----------------------------------------------------------------------------
// Small graph example with one compound and two leave nodes.

class Node {
 public:
  virtual ~Node() {}
  virtual const std::string to_string() const = 0;
};

// <name> = <val>;
class Variable final : public Node {
 public:
  Variable(std::string name, int val) : name_(name), val_(val) {}
  ~Variable() {}

  const std::string to_string() const {
    std::stringstream s;
    s << name_ << " = " << val_ << "; ";
    return s.str();
  }

private:
  std::string name_;
  int val_;
};

// <c> = <a> + <b>;
class Add final : public Node {
 public:
  Add(std::string a, std::string b, std::string c) : a_(a), b_(b), c_(c) {}
  ~Add() {}

  const std::string to_string() const {
    std::stringstream s;
    s << c_ << " = " << a_ << " + " << b_ << "; ";
    return s.str();
  }

private:
  std::string a_;
  std::string b_;
  std::string c_;
};

// { <node>; <node>; ... }
class Scope final : public Node {
 public:
  Scope(const std::vector<Node*>& nodes) : nodes_(nodes) {}
  ~Scope() {}

  const std::string to_string() const {
    std::stringstream s;
    s << "{ ";
    for (auto node : nodes_) {
      s << node->to_string();
    }
    s << "}; ";
    return s.str();
  }

private:
  std::vector<Node*> nodes_;
};

// ----------------------------------------------------------------------------
// C/P of DataRange from v8/src/wasm/fuzzing/random-module-generation.cc

class DataRange {
  // data_ is used for general random values for fuzzing.
  v8::base::Vector<const uint8_t> data_;
  // The RNG is used for generating random values (i32.consts etc.) for which
  // the quality of the input is less important.
  v8::base::RandomNumberGenerator rng_;

 public:
  explicit DataRange(v8::base::Vector<const uint8_t> data, int64_t seed = -1)
      : data_(data), rng_(seed == -1 ? get<int64_t>() : seed) {}
  DataRange(const DataRange&) = delete;
  DataRange& operator=(const DataRange&) = delete;

  // Don't accidentally pass DataRange by value. This will reuse bytes and might
  // lead to OOM because the end might not be reached.
  // Define move constructor and move assignment, disallow copy constructor and
  // copy assignment (below).
  DataRange(DataRange&& other) V8_NOEXCEPT : data_(other.data_),
                                             rng_(other.rng_) {
    other.data_ = {};
  }
  DataRange& operator=(DataRange&& other) V8_NOEXCEPT {
    data_ = other.data_;
    rng_ = other.rng_;
    other.data_ = {};
    return *this;
  }

  size_t size() const { return data_.size(); }

  DataRange split() {
    // As we might split many times, only use 2 bytes if the data size is large.
    uint16_t random_choice = data_.size() > std::numeric_limits<uint8_t>::max()
                                 ? get<uint16_t>()
                                 : get<uint8_t>();
    uint16_t num_bytes = random_choice % std::max(size_t{1}, data_.size());
    int64_t new_seed = rng_.initial_seed() ^ rng_.NextInt64();
    DataRange split(data_.SubVector(0, num_bytes), new_seed);
    data_ += num_bytes;
    return split;
  }

  template <typename T, size_t max_bytes = sizeof(T)>
  T getPseudoRandom() {
    static_assert(!std::is_same<T, bool>::value, "bool needs special handling");
    static_assert(max_bytes <= sizeof(T));
    // Special handling for signed integers: Calling getPseudoRandom<int32_t, 1>
    // () should be equal to getPseudoRandom<int8_t>(). (The NextBytes() below
    // does not achieve that due to depending on endianness and either never
    // generating negative values or filling in the highest significant bits
    // which would be unexpected).
    if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
      switch (max_bytes) {
        case 1:
          return static_cast<int8_t>(getPseudoRandom<uint8_t>());
        case 2:
          return static_cast<int16_t>(getPseudoRandom<uint16_t>());
        case 4:
          return static_cast<int32_t>(getPseudoRandom<uint32_t>());
        default:
          return static_cast<T>(
              getPseudoRandom<std::make_unsigned_t<T>, max_bytes>());
      }
    }

    T result{};
    rng_.NextBytes(&result, max_bytes);
    return result;
  }

  template <typename T>
  T get() {
    // Bool needs special handling (see template specialization below).
    static_assert(!std::is_same<T, bool>::value, "bool needs special handling");

    // We want to support the case where we have less than sizeof(T) bytes
    // remaining in the slice. We'll just use what we have, so we get a bit of
    // randomness when there are still some bytes left. If size == 0, get<T>()
    // returns the type's value-initialized value.
    const size_t num_bytes = std::min(sizeof(T), data_.size());
    T result{};
    memcpy(&result, data_.begin(), num_bytes);
    data_ += num_bytes;
    return result;
  }
};

// Explicit specialization must be defined outside of class body.
template <>
bool DataRange::get() {
  // The general implementation above is not instantiable for bool, as that
  // would cause undefinied behaviour when memcpy'ing random bytes to the
  // bool. This can result in different observable side effects when invoking
  // get<bool> between debug and release version, which eventually makes the
  // code output different as well as raising various unrecoverable errors on
  // runtime.
  // Hence we specialize get<bool> to consume a full byte and use the least
  // significant bit only (0 == false, 1 == true).
  return get<uint8_t>() % 2;
}

// ----------------------------------------------------------------------------
// Fuzzer

std::string get_variable_name(DataRange* data, int& known_vars) {
  uint8_t var = data->get<uint8_t>() % known_vars;
  return std::string("v" + std::to_string(var));
}

std::string get_or_create_variable_name(DataRange* data, int& known_vars) {
  if (!known_vars || (known_vars < 5 && data->get<bool>())) {
    known_vars++;
    return std::string("v" + std::to_string(known_vars - 1));
  }
  return get_variable_name(data, known_vars);
}

// The known_vars is passed around by reference while in the same scope to use
// any newly created variable. It's copied here across different scopes.
Node* create_scope(DataRange* data, int max_depth, int known_vars);

Node* create_variable(DataRange* data, int& known_vars) {
  std::string name = get_or_create_variable_name(data, known_vars);
  return new Variable(name, data->getPseudoRandom<int8_t>());
}

Node* create_node(DataRange* data, int max_depth, int& known_vars) {
  uint8_t node_type = data->get<uint8_t>() % 5;
  switch (node_type) {
    case 0:
    case 1: {
      return create_variable(data, known_vars);
    }
    case 2:
    case 3: {
      if (!known_vars) break;
      std::string a = get_variable_name(data, known_vars);
      std::string b = get_variable_name(data, known_vars);
      std::string c = get_or_create_variable_name(data, known_vars);
      return new Add(a, b, c);
    }
    case 4: {
      if (max_depth <= 0) break;
      return create_scope(data, max_depth - 1, known_vars);
    }
  }
  return create_variable(data, known_vars);
}

Node* create_scope(DataRange* data, int max_depth, int known_vars) {
  std::vector<Node*> nodes;
  int n_nodes = data->get<uint8_t>() % 10;
  // std::cout << "n_nodes " << n_nodes << " max_depth " << max_depth
  //           << " known_vars " << known_vars << std::endl;
  for (int i = 0; i < n_nodes; i++) {
    nodes.emplace_back(create_node(data, max_depth, known_vars));
  }
  return new Scope(nodes);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* input_data, size_t size) {
  v8::base::Vector<const uint8_t> vec = {input_data, size};
  DataRange data(vec);
  int max_depth = 2 + data.get<uint8_t>() % 2;

  int known_vars = 0;
  Node* root = create_node(&data, max_depth, known_vars);
  std::cout << root->to_string() << std::endl;

  return 0;
}

TEST(ExampleFuzzerTest, NoFuzz) {
  uint8_t* data = (unsigned char *) "chqbAsdflVGEWEGkskejkdjclskRTREhtewigjdslkjglksjdlgkWEGWEGsdlgksjdlgkjsSSWREGRHjglskdjglskdUIoIUIjlksjgdguvhxvFKLAsdfgwgsdgSDFSDFWEFssdg";
  CHECK_EQ(1, LLVMFuzzerTestOneInput(data, 128));
}

static void Fuzz(const std::string& input) {
  CHECK_EQ(0,
           LLVMFuzzerTestOneInput((unsigned char*)input.c_str(), input.size()));
}

V8_FUZZ_TEST(ExampleFuzzerTest, Fuzz);

struct State {
  int known_vars = 0;
};

struct NodeBuilder {
  virtual ~NodeBuilder() {}
  virtual Node* create(State& state) const = 0;
};

struct ScopeBuilder : NodeBuilder {
  ScopeBuilder(std::vector<std::unique_ptr<NodeBuilder>>&& builders)
      : builders_(std::move(builders)) {}
  Node* create(State& state) const {
    std::vector<Node*> nodes;
    for (const std::unique_ptr<NodeBuilder>& builder : builders_) {
      Node* node = builder->create(state);
      if (node != nullptr) nodes.emplace_back(node);
    }
    return new Scope(nodes);
  }
  std::vector<std::unique_ptr<NodeBuilder>> builders_;
};

std::string get_variable_name_ng(const State& state, int index) {
  return std::string("v" + std::to_string(index % state.known_vars));
}

std::string get_or_create_variable_name_ng(State& state, bool maybe_create,
                                           int index) {
  if (!state.known_vars || (state.known_vars < 5 && maybe_create)) {
    state.known_vars++;
    return std::string("v" + std::to_string(state.known_vars - 1));
  } else {
    return get_variable_name_ng(state, index);
  }
}

struct VariableBuilder : NodeBuilder {
  VariableBuilder(bool maybe_create, int choose_index, int8_t initializer)
      : maybe_create_(maybe_create),
        choose_index_(choose_index),
        initializer_(initializer) {}

  ~VariableBuilder() {}

  Node* create(State& state) const {
    std::string name =
        get_or_create_variable_name_ng(state, maybe_create_, choose_index_);
    return new Variable(name, initializer_);
  }
  bool maybe_create_;
  int choose_index_;
  int8_t initializer_;
};

struct AddBuilder : NodeBuilder {
  AddBuilder(bool maybe_create, int choose_index0, int choose_index1,
             int choose_index2)
      : maybe_create_(maybe_create),
        choose_index0_(choose_index0),
        choose_index1_(choose_index1),
        choose_index2_(choose_index2) {}

  Node* create(State& state) const {
    if (!state.known_vars) return nullptr;
    std::string a = get_variable_name_ng(state, choose_index0_);
    std::string b = get_variable_name_ng(state, choose_index1_);
    std::string c =
        get_or_create_variable_name_ng(state, maybe_create_, choose_index2_);
    return new Add(a, b, c);
  }
  bool maybe_create_;
  int choose_index0_;
  int choose_index1_;
  int choose_index2_;
};

// ------- Domains ------------------------------------------------------------

template <typename T>
fuzztest::Domain<std::unique_ptr<T>> NonNullSharedPtrOf(
    fuzztest::Domain<T>&& constructor_domain) {
  return fuzztest::ReversibleMap(
      [](T&& arg) -> std::unique_ptr<T> { return std::make_unique<T>(arg); },
      [](const std::unique_ptr<T>& ptr) -> std::optional<std::tuple<T>> {
        return std::optional{std::tuple{*ptr}};
      },
      std::move(constructor_domain));
}

template <typename T, typename U>
fuzztest::Domain<std::unique_ptr<T>> BaseSharedPtrOf(
    fuzztest::Domain<std::unique_ptr<U>>&& domain) {
  return fuzztest::ReversibleMap(
      [](std::unique_ptr<U>& ptr) -> std::unique_ptr<T> {
        return std::unique_ptr<T>{static_cast<T*>(ptr.release())};
      },
      [](std::unique_ptr<T>& ptr)
          -> std::optional<std::tuple<std::unique_ptr<U>>> {
        return std::optional{std::tuple{std::unique_ptr<U>{static_cast<U*>(ptr.release())}}};
      },
      std::move(domain));
}

fuzztest::Domain<std::unique_ptr<NodeBuilder>> ArbitraryVariableBuilder() {
  auto constructor_domain = fuzztest::ConstructorOf<VariableBuilder>(
      fuzztest::Arbitrary<bool>(), fuzztest::Arbitrary<int>(),
      fuzztest::Arbitrary<int8_t>());
  return BaseSharedPtrOf<NodeBuilder, VariableBuilder>(
      NonNullSharedPtrOf<VariableBuilder>(constructor_domain));
}

fuzztest::Domain<std::unique_ptr<NodeBuilder>> ArbitraryAddBuilder() {
  auto constructor_domain = fuzztest::ConstructorOf<AddBuilder>(
      fuzztest::Arbitrary<bool>(), fuzztest::Arbitrary<int>(),
      fuzztest::Arbitrary<int>(), fuzztest::Arbitrary<int>());
  return BaseSharedPtrOf<NodeBuilder, AddBuilder>(
      NonNullSharedPtrOf<AddBuilder>(constructor_domain));
}

static fuzztest::Domain<std::unique_ptr<NodeBuilder>> ArbitraryNodeBuilder() {
  fuzztest::DomainBuilder builder;

  fuzztest::Domain<std::unique_ptr<NodeBuilder>> variable_domain =
      ArbitraryVariableBuilder();
  fuzztest::Domain<std::unique_ptr<NodeBuilder>> add_domain =
      ArbitraryAddBuilder();

  auto nodes = fuzztest::ContainerOf<std::vector<std::unique_ptr<NodeBuilder>>>(
      builder.Get<std::unique_ptr<NodeBuilder>>("node"));
  auto constructor_domain = fuzztest::ConstructorOf<ScopeBuilder>(nodes);
  fuzztest::Domain<std::unique_ptr<NodeBuilder>> scope_domain =
      BaseSharedPtrOf<NodeBuilder, ScopeBuilder>(
          NonNullSharedPtrOf<ScopeBuilder>(constructor_domain));

  builder.Set<std::unique_ptr<NodeBuilder>>(
      "node", fuzztest::OneOf(variable_domain, add_domain, scope_domain));
  return std::move(builder).Finalize<std::unique_ptr<NodeBuilder>>("node");
}

static fuzztest::Domain<std::unique_ptr<NodeBuilder>> ArbitraryScopeBuilder() {
  auto nodes = fuzztest::ContainerOf<std::vector<std::unique_ptr<NodeBuilder>>>(
      ArbitraryNodeBuilder());
  auto constructor_domain = fuzztest::ConstructorOf<ScopeBuilder>(nodes);
  return BaseSharedPtrOf<NodeBuilder, ScopeBuilder>(
      NonNullSharedPtrOf<ScopeBuilder>(constructor_domain));
}

static void FuzzBetter(std::unique_ptr<NodeBuilder> node_builder) {
  State state;
  Node* root = node_builder->create(state);
  std::cout << root->to_string() << std::endl;
}

V8_FUZZ_TEST(ExampleFuzzerTest, FuzzBetter)
    .WithDomains(ArbitraryScopeBuilder());

/*
int main(int argc, char** argv) {
  uint8_t* data = (unsigned char *)
"chqbAsdflVGEWEGkskejkdjclskRTREhtewigjdslkjglksjdlgkWEGWEGsdlgksjdlgkjsSSWREGRHjglskdjglskdUIoIUIjlksjgdguvhxvFKLAsdfgwgsdgSDFSDFWEFssdg";

  LLVMFuzzerTestOneInput(data, 128);
}*/