// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines internal versions of the public API structs. These should
// all be tidy and simple classes which maintain proper ownership (unique_ptr)
// of each other. Each contains an instance of its corresponding public type,
// which can be filled out with GetPublicView.

#ifndef V8_TOOLS_DEBUG_HELPER_DEBUG_HELPER_INTERNAL_H_
#define V8_TOOLS_DEBUG_HELPER_DEBUG_HELPER_INTERNAL_H_

#include <string>
#include <vector>

#include "debug-helper.h"

namespace d = v8::debug_helper;

namespace v8 {
namespace debug_helper_internal {

class ObjectProperty {
 public:
  inline ObjectProperty(std::string name, std::string type,
                        std::vector<d::Value> values)
      : name_(name),
        type_(type),
        values_(values),
        kind_(d::PropertyKind::kIndexed) {}
  inline ObjectProperty(std::string name, std::string type, d::Value value)
      : name_(name),
        type_(type),
        values_({value}),
        kind_(d::PropertyKind::kSingle) {}
  inline ObjectProperty(std::string name, std::string type, uint64_t value)
      : ObjectProperty(name, type,
                       d::Value{d::MemoryAccessResult::kOk, value}) {}

  inline d::ObjectProperty* GetPublicView() {
    public_view_.name = name_.c_str();
    public_view_.type = type_.c_str();
    public_view_.num_values = values_.size();
    public_view_.values = values_.data();
    public_view_.kind = kind_;
    return &public_view_;
  }

 private:
  std::string name_;
  std::string type_;
  std::vector<d::Value> values_;
  d::PropertyKind kind_;

  d::ObjectProperty public_view_;
};

class ObjectPropertiesResult;
using ObjectPropertiesResultInternal = ObjectPropertiesResult;

struct ObjectPropertiesResultExtended : public d::ObjectPropertiesResult {
  ObjectPropertiesResultInternal* base;  // Back reference for cleanup
};

class ObjectPropertiesResult {
 public:
  inline ObjectPropertiesResult(
      d::TypeCheckResult type_check_result, std::string brief, std::string type,
      std::vector<std::unique_ptr<ObjectProperty>> properties)
      : type_check_result_(type_check_result),
        brief_(brief),
        type_(type),
        properties_(std::move(properties)) {}
  inline d::ObjectPropertiesResult* GetPublicView() {
    public_view_.type_check_result = type_check_result_;
    public_view_.brief = brief_.c_str();
    public_view_.type = type_.c_str();
    public_view_.num_properties = properties_.size();
    properties_raw_.resize(0);
    for (const auto& property : properties_) {
      properties_raw_.push_back(property->GetPublicView());
    }
    public_view_.properties = properties_raw_.data();
    public_view_.base = this;
    return &public_view_;
  }

 private:
  d::TypeCheckResult type_check_result_;
  std::string brief_;
  std::string type_;
  std::vector<std::unique_ptr<ObjectProperty>> properties_;

  ObjectPropertiesResultExtended public_view_;
  std::vector<d::ObjectProperty*> properties_raw_;
};

bool IsPointerCompressed(uintptr_t address);
uintptr_t Decompress(uintptr_t address, uintptr_t any_uncompressed_address);

}  // namespace debug_helper_internal
}  // namespace v8

#endif
