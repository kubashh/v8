// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/baseline-compilation-info.h"

#include "src/isolate.h"
#include "src/objects.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/objects/shared-function-info.h"

namespace v8 {
namespace internal {

BaselineCompilationInfo::BaselineCompilationInfo(
    Zone* zone, Handle<SharedFunctionInfo> shared_info,
    Handle<FeedbackVector> feedback_vector)
    : flags_(FLAG_untrusted_code_mitigations ? kUntrustedCodeMitigations : 0),
      zone_(zone),
      shared_info_(shared_info),
      feedback_vector_(feedback_vector) {
  DCHECK(shared_info_->HasBytecodeArray());
}

BytecodeArray* BaselineCompilationInfo::bytecode_array() const {
  return shared_info()->GetBytecodeArray();
}

int BaselineCompilationInfo::num_parameters_including_this() const {
  return bytecode_array()->parameter_count();
}

std::unique_ptr<char[]> BaselineCompilationInfo::GetDebugName() const {
  return shared_info()->DebugName()->ToCString();
}

}  // namespace internal
}  // namespace v8
