// Copyright 2023 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8_LOGGING_PERFETTO_JIT_H_
#define V8_LOGGING_PERFETTO_JIT_H_

#include <cstdint>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "perfetto/ext/base/hash.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "perfetto/tracing/data_source.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/logging/log.h"
#include "src/objects/script.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/tagged.h"
#include "src/utils/allocation.h"
#include "v8-isolate.h"

namespace v8 {
namespace internal {

struct Function {
  uint64_t name_iid;
  uint64_t script_iid;
  int line_num;
  int column_num;

  bool operator==(const Function& other) const {
    static_assert(std::has_unique_object_representations_v<Function>);
    return std::memcmp(this, &other, sizeof(Function)) == 0;
  }

  bool operator!=(const Function& other) const { return !(*this == other); }

  struct Hash {
    size_t operator()(const Function& function) const {
      ::perfetto::base::Hasher h;
      static_assert(std::has_unique_object_representations_v<Function>);
      h.Update(reinterpret_cast<const char*>(this), sizeof(Function));
      return h.digest();
    }
  };
};

struct JitDsIncrementalState {
  protozero::HeapBuffered<perfetto::protos::pbzero::InternedData>
      serialized_interned_data;

  std::unordered_map<std::string, uint64_t> function_names;
  uint64_t next_function_name_iid{1};

  std::unordered_map<std::string, uint64_t> scripts_;
  uint64_t next_script_iid{1};

  std::unordered_map<int, uint64_t> isolates_;
  uint64_t next_isolate_iid{1};

  struct std::unordered_map<Function, uint64_t, Function::Hash> functions_;
  std::unordered_map<uint64_t, uint64_t> name_only_functions_;
  uint64_t next_function_iid{1};

  bool was_cleared = true;
};

struct JitDataSourceTraits : public perfetto::DefaultDataSourceTraits {
  using IncrementalStateType = JitDsIncrementalState;
  using TlsStateType = void;
};

class JitDataSource
    : public perfetto::DataSource<JitDataSource, JitDataSourceTraits> {
 public:
  class TraceHandle {
   public:
    TraceHandle(JitDataSource::TraceContext ctx, Isolate* isolate);
    ~TraceHandle();

    perfetto::protos::pbzero::TracePacket* trace_packet() {
      return &*trace_packet_;
    }

    uint64_t InternFunction(Tagged<SharedFunctionInfo> function);
    uint64_t InternNameOnlyFunction(const char* name);
    uint64_t InternNameOnlyFunction(Handle<Name> name);
    uint64_t InternFunctionName(std::string name);
    uint64_t InternScript(Tagged<Script> script);
    uint64_t InternIsolate();

   private:
    uint64_t InternNameOnlyFunction(uint64_t name_iid);

    TraceContext ctx_;
    Isolate& isolate_;
    TraceContext::TracePacketHandle trace_packet_;
    JitDsIncrementalState* incremental_state_;
  };

  static void TraceCodeRangeCreation(Isolate* isolate,
                                     const base::AddressRegion& region);
  static void TraceCodeRangeDestruction(Isolate* isolate,
                                        const base::AddressRegion& region);
  static void TraceRemapEmbeddedBuiltins(Isolate* isolate,
                                         const uint8_t* embedded_blob_code,
                                         size_t embedded_blob_code_size);

  void OnSetup(const SetupArgs&) override;
  void OnStart(const StartArgs&) override;
  void OnStop(const StopArgs&) override;

  static void RegisterIsolate(Isolate* isolate);
  static void UnregisterIsolate(Isolate* isolate);
};

}  // namespace internal
}  // namespace v8

PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(v8::internal::JitDataSource,
                                            v8::internal::JitDataSourceTraits);

#endif  // V8_LOGGING_PERFETTO_JIT_H_
