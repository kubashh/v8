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
//
// TODO(chromium:1056170): Add more platforms as backends are implemented.
#ifdef __clang__
#ifdef V8_TARGET_ARCH_X64
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
#endif  // V8_TARGET_ARCH_X64
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
  std::unique_ptr<Container> container_;
  bool found_ = false;
};

}  // namespace

TEST_F(GCStackTest, IterateFindsCalleeSavedRegisters) {
  auto scanner = std::make_unique<StackScanner>();
  int* volatile tmp;

  // On some platforms needle is part of the redzone. This is an implementation
  // artifact which prohibits a test that stack iteration does not find the
  // needle.

  // Put pointer in any stack slot.
  tmp = scanner->needle();
  scanner->Reset();
  GetStack()->IteratePointers(scanner.get());
  tmp = nullptr;
  EXPECT_TRUE(scanner->found());

// First, clear all callee-saved registers.
#define CLEAR_REGISTER(reg) asm("mov $0, %%" reg : : : reg);

  FOR_ALL_CALLEE_SAVED_REGS(CLEAR_REGISTER)
#undef CLEAR_REGISTER

// The following test block
// 1. sets |tmp| to point to needle;
// 2. moves |tmp| into a callee-saved register;
// 3. clears |tmp|, leaving the calee-saved register as the only register
//    referencing needle;
#define KEEP_ALIVE_FROM_CALLEE_SAVED(reg)                                \
  tmp = scanner->needle();                                               \
  /* This moves the temporary into the calee-saved register. */          \
  asm("mov %0, %%" reg : : "r"(tmp) : reg);                              \
  tmp = nullptr;                                                         \
  scanner->Reset();                                                      \
  GetStack()->IteratePointers(scanner.get());                            \
  EXPECT_TRUE(scanner->found())                                          \
      << "pointer in callee-saved register not found. register: " << reg \
      << std::endl;                                                      \
  /* Clear out the register again */                                     \
  asm("mov $0, %%" reg : : : reg);

  FOR_ALL_CALLEE_SAVED_REGS(KEEP_ALIVE_FROM_CALLEE_SAVED)
#undef KEEP_ALIVE_FROM_CALLEE_SAVED
#undef FOR_ALL_CALLEE_SAVED_REGS
}

#endif  // CALLEE_SAVED_TEST_SUPPORTED

}  // namespace internal
}  // namespace cppgc
