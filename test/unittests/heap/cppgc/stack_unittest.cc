// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/stack.h"

#include <memory>
#include <ostream>
#include <string>

#include "include/v8config.h"
#include "src/base/platform/platform.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

class GCStackTest : public ::testing::Test {
 protected:
  void SetUp() override {
    stack_.reset(new Stack(v8::base::Stack::GetStackStart()));
  }

  void TearDown() override { stack_.reset(); }

  Stack* GetStack() const { return stack_.get(); }

 private:
  std::unique_ptr<Stack> stack_;
};

}  // namespace

TEST_F(GCStackTest, IsOnStackForStackValue) {
  void* dummy;
  EXPECT_TRUE(GetStack()->IsOnStack(&dummy));
}

TEST_F(GCStackTest, IsOnStackForHeapValue) {
  auto dummy = std::make_unique<int>();
  EXPECT_FALSE(GetStack()->IsOnStack(dummy.get()));
}

// The following test uses inline assembly and has been checked to work on clang
// to verify that the stack-scanning trampoline pushes callee-saved registers.
//
// The test uses a macro loop as asm() can only be passed string literals.
#ifdef __clang__
#ifdef V8_TARGET_ARCH_64_BIT
#ifdef V8_OS_WIN

#define CALLEE_SAVED_TEST_SUPPORTED 1
// Excluded from test: rbp
#define FOR_ALL_CALLEE_SAVED_REGS(V) \
  V("rdi")                           \
  V("rsi")                           \
  V("rbx")                           \
  V("r12")                           \
  V("r13")                           \
  V("r14")                           \
  V("r15")

#else  // !V8_OS_WIN

#define CALLEE_SAVED_TEST_SUPPORTED 1
// Excluded from test: rbp
#define FOR_ALL_CALLEE_SAVED_REGS(V) \
  V("rbx")                           \
  V("r12")                           \
  V("r13")                           \
  V("r14")                           \
  V("r15")

#endif  // !V8_OS_WIN
#endif  // V8_TARGET_ARCH_64_BIT
#endif  // __clang_

#ifdef CALLEE_SAVED_TEST_SUPPORTED

namespace {

class StackScanner final : public StackVisitor {
 public:
  struct Container {
    std::unique_ptr<int> value;
  };

  StackScanner() : container_(new Container{}) {
    container_->value = std::make_unique<int>();
  }

  void VisitPointer(void* address) final {
    if (address == container_->value.get()) found_ = true;
  }

  void Reset() { found_ = false; }

  bool found() const { return found_; }
  int* needle() const { return container_->value.get(); }

 private:
  bool found_ = false;
  std::unique_ptr<Container> container_;
};

}  // namespace

TEST_F(GCStackTest, IterateFindsCaleeSavedRegisters) {
  auto scanner = std::make_unique<StackScanner>();

  int* volatile tmp;
  USE(tmp);

  GetStack()->IteratePointers(scanner.get());
  EXPECT_FALSE(scanner->found());

  // Put pointer anywhere.
  tmp = scanner->needle();
  scanner->Reset();
  GetStack()->IteratePointers(scanner.get());
  tmp = nullptr;
  EXPECT_TRUE(scanner->found());

#define KEEP_ALIVE_FROM_CALLEE_SAVED(reg)                                \
  tmp = scanner->needle();                                               \
  asm("mov %0, %%" reg : : "r"(tmp) : reg);                              \
  tmp = nullptr;                                                         \
  scanner->Reset();                                                      \
  GetStack()->IteratePointers(scanner.get());                            \
  EXPECT_TRUE(scanner->found())                                          \
      << "pointer in callee-saved register not found. register: " << reg \
      << std::endl;

  FOR_ALL_CALLEE_SAVED_REGS(KEEP_ALIVE_FROM_CALLEE_SAVED)
}

#endif  // CALLEE_SAVED_TEST_SUPPORTED

}  // namespace internal
}  // namespace cppgc
