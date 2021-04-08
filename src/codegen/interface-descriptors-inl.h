// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_INTERFACE_DESCRIPTORS_INL_H_
#define V8_CODEGEN_INTERFACE_DESCRIPTORS_INL_H_

#include <utility>

#include "src/codegen/interface-descriptors.h"

#if V8_TARGET_ARCH_X64
#include "src/codegen/x64/interface-descriptors-x64-inl.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/codegen/arm64/interface-descriptors-arm64-inl.h"
#elif V8_TARGET_ARCH_IA32
#include "src/codegen/ia32/interface-descriptors-ia32-inl.h"
#elif V8_TARGET_ARCH_ARM
#include "src/codegen/arm/interface-descriptors-arm-inl.h"
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {

namespace detail {

template <typename T>
struct IsRegisterArray : std::false_type {};
template <int i>
struct IsRegisterArray<std::array<Register, i>> : std::true_type {};

template <std::size_t M, std::size_t... I>
constexpr std::array<Register, sizeof...(I)> RegisterArraySliceImpl(
    std::array<Register, M> arr, std::index_sequence<I...>) {
  return {std::get<I>(arr)...};
}

template <std::size_t N, std::size_t M>
constexpr std::array<Register, N> RegisterArraySlice(
    std::array<Register, M> arr) {
  return RegisterArraySliceImpl(arr, std::make_index_sequence<N>());
}

}  // namespace detail

// static
constexpr std::array<Register, 4>
CallInterfaceDescriptor::DefaultJSRegisterArray() {
  CONSTEXPR_DCHECK(!AreAliased(
      kJavaScriptCallTargetRegister, kJavaScriptCallNewTargetRegister,
      kJavaScriptCallArgCountRegister, kJavaScriptCallExtraArg1Register));

  return RegisterArray(
      kJavaScriptCallTargetRegister, kJavaScriptCallNewTargetRegister,
      kJavaScriptCallArgCountRegister, kJavaScriptCallExtraArg1Register);
}

// static
template <typename Descriptor, typename Base>
constexpr auto StaticCallInterfaceDescriptor<Descriptor, Base>::registers() {
  return CallInterfaceDescriptor::DefaultRegisterArray();
}

// static
template <typename Descriptor, typename Base>
constexpr auto StaticJSCallInterfaceDescriptor<Descriptor, Base>::registers() {
  return CallInterfaceDescriptor::DefaultJSRegisterArray();
}

template <typename Descriptor, typename Base>
void StaticCallInterfaceDescriptor<Descriptor, Base>::
    InitializePlatformSpecific(CallInterfaceDescriptorData* data) {
  data->InitializePlatformSpecific(Descriptor::register_param_count(),
                                   Descriptor::registers().data());
}

// static
template <typename Descriptor, typename Base>
constexpr int
StaticCallInterfaceDescriptor<Descriptor, Base>::register_param_count() {
  static_assert(
      detail::IsRegisterArray<decltype(Descriptor::registers())>::value,
      "Descriptor subclass should define a registers() function "
      "returning a std::array<Register>");
  static_assert(Descriptor::kParameterCount >= 0,
                "Descriptor subclass should override parameter count with a "
                "value that is greater than 0");

  return std::min<int>(Descriptor::kParameterCount,
                       Descriptor::registers().size());
}

constexpr Register FastNewObjectDescriptor::TargetRegister() {
  return kJSFunctionRegister;
}

constexpr Register FastNewObjectDescriptor::NewTargetRegister() {
  return kJavaScriptCallNewTargetRegister;
}

constexpr Register ApiGetterDescriptor::ReceiverRegister() {
  return LoadDescriptor::ReceiverRegister();
}

constexpr inline Register LoadGlobalNoFeedbackDescriptor::NameRegister() {
  return LoadDescriptor::NameRegister();
}

constexpr inline Register LoadGlobalNoFeedbackDescriptor::ICKindRegister() {
  return LoadDescriptor::SlotRegister();
}

constexpr inline Register LoadNoFeedbackDescriptor::ReceiverRegister() {
  return LoadDescriptor::ReceiverRegister();
}

constexpr inline Register LoadNoFeedbackDescriptor::NameRegister() {
  return LoadGlobalNoFeedbackDescriptor::NameRegister();
}

constexpr inline Register LoadNoFeedbackDescriptor::ICKindRegister() {
  return LoadGlobalNoFeedbackDescriptor::ICKindRegister();
}

constexpr inline Register LoadGlobalDescriptor::NameRegister() {
  return LoadDescriptor::NameRegister();
}

constexpr inline Register LoadGlobalDescriptor::SlotRegister() {
  return LoadDescriptor::SlotRegister();
}

constexpr inline Register StoreGlobalDescriptor::NameRegister() {
  return StoreDescriptor::NameRegister();
}

constexpr inline Register StoreGlobalDescriptor::ValueRegister() {
  return StoreDescriptor::ValueRegister();
}

constexpr inline Register StoreGlobalDescriptor::SlotRegister() {
  return StoreDescriptor::SlotRegister();
}

#if V8_TARGET_ARCH_IA32
// On ia32, LoadWithVectorDescriptor passes vector on the stack and thus we
// need to choose a new register here.
constexpr inline Register LoadGlobalWithVectorDescriptor::VectorRegister() {
  return edx;
}
#else
constexpr inline Register LoadGlobalWithVectorDescriptor::VectorRegister() {
  return LoadWithVectorDescriptor::VectorRegister();
}
#endif

constexpr inline Register StoreGlobalWithVectorDescriptor::VectorRegister() {
  return StoreWithVectorDescriptor::VectorRegister();
}

// static
constexpr auto LoadDescriptor::registers() {
  return RegisterArray(ReceiverRegister(), NameRegister(), SlotRegister());
}

// static
constexpr auto LoadBaselineDescriptor::registers() {
  return LoadDescriptor::registers();
}

// static
constexpr auto LoadGlobalDescriptor::registers() {
  return RegisterArray(NameRegister(), SlotRegister());
}

// static
constexpr auto LoadGlobalBaselineDescriptor::registers() {
  return LoadGlobalDescriptor::registers();
}

// static
constexpr auto StoreDescriptor::registers() {
  auto registers = RegisterArray(ReceiverRegister(), NameRegister(),
                                 ValueRegister(), SlotRegister());
  constexpr int len = kParameterCount - kStackArgumentsCount;

  return detail::RegisterArraySlice<len>(registers);
}

// static
constexpr auto StoreBaselineDescriptor::registers() {
  return StoreDescriptor::registers();
}

// static
constexpr auto StoreGlobalDescriptor::registers() {
  auto registers =
      RegisterArray(NameRegister(), ValueRegister(), SlotRegister());
  constexpr int len = kParameterCount - kStackArgumentsCount;

  return detail::RegisterArraySlice<len>(registers);
}

// static
constexpr auto StoreGlobalBaselineDescriptor::registers() {
  return StoreGlobalDescriptor::registers();
}

// static
constexpr auto LoadWithReceiverBaselineDescriptor::registers() {
  return RegisterArray(
      LoadWithReceiverAndVectorDescriptor::ReceiverRegister(),
      LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister(),
      LoadWithReceiverAndVectorDescriptor::NameRegister(),
      LoadWithReceiverAndVectorDescriptor::SlotRegister());
}

// static
constexpr auto BaselineOutOfLinePrologueDescriptor::registers() {
  // TODO(v8:11421): Implement on other platforms.
#if V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_IA32 || \
    V8_TARGET_ARCH_ARM
  auto registers = RegisterArray(
      kContextRegister, kJSFunctionRegister, kJavaScriptCallArgCountRegister,
      kJavaScriptCallExtraArg1Register, kJavaScriptCallNewTargetRegister,
      kInterpreterBytecodeArrayRegister);
  constexpr int len = kParameterCount - kStackArgumentsCount;

  return detail::RegisterArraySlice<len>(registers);
#else
  return DefaultRegisterArray();
#endif
}

// static
constexpr auto BaselineLeaveFrameDescriptor::registers() {
  // TODO(v8:11421): Implement on other platforms.
#if V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_ARM64 || \
    V8_TARGET_ARCH_ARM
  return RegisterArray(ParamsSizeRegister(), WeightRegister());
#else
  return DefaultRegisterArray();
#endif
}

#define DEFINE_STATIC_BUILTIN_DESCRIPTOR_GETTER(Name, DescriptorName) \
  template <>                                                         \
  struct CallInterfaceDescriptorFor<Builtins::k##Name> {              \
    using type = DescriptorName##Descriptor;                          \
  };
BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN,
             /*TFC*/ DEFINE_STATIC_BUILTIN_DESCRIPTOR_GETTER, IGNORE_BUILTIN,
             /*TFH*/ DEFINE_STATIC_BUILTIN_DESCRIPTOR_GETTER, IGNORE_BUILTIN,
             /*ASM*/ DEFINE_STATIC_BUILTIN_DESCRIPTOR_GETTER)
#undef DEFINE_STATIC_BUILTIN_DESCRIPTOR_GETTER
#define DEFINE_STATIC_BUILTIN_DESCRIPTOR_GETTER(Name, ...) \
  template <>                                              \
  struct CallInterfaceDescriptorFor<Builtins::k##Name> {   \
    using type = Name##Descriptor;                         \
  };
BUILTIN_LIST_TFS(DEFINE_STATIC_BUILTIN_DESCRIPTOR_GETTER)
#undef DEFINE_STATIC_BUILTIN_DESCRIPTOR_GETTER

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_INTERFACE_DESCRIPTORS_INL_H_
