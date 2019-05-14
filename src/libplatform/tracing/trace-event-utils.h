// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBPLATFORM_TRACING_TRACE_EVENT_UTILS_H_
#define V8_LIBPLATFORM_TRACING_TRACE_EVENT_UTILS_H_

#include "include/libplatform/v8-tracing.h"  // For kTraceMaxNumArgs

namespace perfetto {
namespace protos {
namespace pbzero {
class ChromeTraceEvent;
}  // namespace pbzero
}  // namespace protos
}  // namespace perfetto

namespace v8 {
namespace platform {
namespace tracing {

// Used for storing pending trace events where we are still waiting for the end
// part to be logged.
struct TempTraceRecord {
  const char* name;
  int64_t timestamp;
  char phase;
  int thread_id;
  uint64_t duration;
  uint64_t thread_duration;
  const char* scope;
  uint64_t id;
  unsigned int flags;
  const uint8_t* category_enabled_flag;
  int process_id;
  int64_t thread_timestamp;
  uint64_t bind_id;
  int num_args;
  const char* arg_names[kTraceMaxNumArgs];
  uint8_t arg_types[kTraceMaxNumArgs];
  uint64_t arg_values[kTraceMaxNumArgs];
  // Takes ownerships of these convertables.
  std::unique_ptr<v8::ConvertableToTraceFormat>
      arg_convertables[kTraceMaxNumArgs];

  // Set the duration and thread duration based on the provided timestamps and
  // the previously recorded timestamps.
  void UpdateDuration(int64_t now_timestamp, int64_t now_thread_timestamp);

  // Fill |trace_event| with the contents of this record. |trace_event| takes
  // ownerships of the arg_convertables.
  void ConvertToChromeTraceEvent(
      ::perfetto::protos::pbzero::ChromeTraceEvent* trace_event);
};

class ChromeTraceEventUtils {
 public:
  // Shared logic for adding the given arguments to a Perfetto trace proto.
  static void AddArgsToTraceProto(
      ::perfetto::protos::pbzero::ChromeTraceEvent* event, int num_args,
      const char** arg_names, const uint8_t* arg_types,
      const uint64_t* arg_values,
      std::unique_ptr<v8::ConvertableToTraceFormat>* arg_convertables);
};

}  // namespace tracing
}  // namespace platform
}  // namespace v8

#endif  // V8_LIBPLATFORM_TRACING_TRACE_EVENT_UTILS_H_
