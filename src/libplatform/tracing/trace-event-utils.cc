// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/tracing/trace-event-utils.h"

#include "base/trace_event/common/trace_event_common.h"
#include "perfetto/trace/chrome/chrome_trace_event.pbzero.h"
#include "src/base/logging.h"

namespace v8 {
namespace platform {
namespace tracing {

void TempTraceRecord::UpdateDuration(int64_t now_timestamp,
                                     int64_t now_thread_timestamp) {
  duration = now_timestamp - timestamp;
  thread_duration = now_thread_timestamp - thread_timestamp;
}

void TempTraceRecord::ConvertToChromeTraceEvent(
    ::perfetto::protos::pbzero::ChromeTraceEvent* trace_event) {
  trace_event->set_name(name);
  trace_event->set_timestamp(timestamp);
  trace_event->set_phase(phase);
  trace_event->set_thread_id(thread_id);
  trace_event->set_duration(duration);
  trace_event->set_thread_duration(thread_duration);
  if (scope) trace_event->set_scope(scope);
  trace_event->set_id(id);
  trace_event->set_flags(flags);
  if (category_enabled_flag) {
    const char* category_group_name =
        TracingController::GetCategoryGroupName(category_enabled_flag);
    DCHECK_NOT_NULL(category_group_name);
    trace_event->set_category_group_name(category_group_name);
  }
  trace_event->set_process_id(process_id);
  trace_event->set_thread_timestamp(thread_timestamp);
  trace_event->set_bind_id(bind_id);

  ChromeTraceEventUtils::AddArgsToTraceProto(trace_event, num_args, arg_names,
                                             arg_types, arg_values,
                                             arg_convertables);
}

// static
void ChromeTraceEventUtils::AddArgsToTraceProto(
    ::perfetto::protos::pbzero::ChromeTraceEvent* event, int num_args,
    const char** arg_names, const uint8_t* arg_types,
    const uint64_t* arg_values,
    std::unique_ptr<v8::ConvertableToTraceFormat>* arg_convertables) {
  for (int i = 0; i < num_args; i++) {
    ::perfetto::protos::pbzero::ChromeTraceEvent_Arg* arg = event->add_args();
    // TODO(petermarshall): Set name_index instead if need be.
    arg->set_name(arg_names[i]);

    TraceObject::ArgValue arg_value;
    arg_value.as_uint = arg_values[i];
    switch (arg_types[i]) {
      case TRACE_VALUE_TYPE_CONVERTABLE: {
        // TODO(petermarshall): Support AppendToProto for Convertables.
        std::string json_value;
        arg_convertables[i]->AppendAsTraceFormat(&json_value);
        // TODO(petermarshall): .reset() the convertables once we no longer run
        // the legacy tracing implementation alongside perfetto.
        arg->set_json_value(json_value.c_str());
        break;
      }
      case TRACE_VALUE_TYPE_BOOL:
        arg->set_bool_value(arg_value.as_bool);
        break;
      case TRACE_VALUE_TYPE_UINT:
        arg->set_uint_value(arg_value.as_uint);
        break;
      case TRACE_VALUE_TYPE_INT:
        arg->set_int_value(arg_value.as_int);
        break;
      case TRACE_VALUE_TYPE_DOUBLE:
        arg->set_double_value(arg_value.as_double);
        break;
      case TRACE_VALUE_TYPE_POINTER:
        arg->set_pointer_value(arg_value.as_uint);
        break;
      // TODO(petermarshall): Treat copy strings specially.
      case TRACE_VALUE_TYPE_COPY_STRING:
      case TRACE_VALUE_TYPE_STRING:
        arg->set_string_value(arg_value.as_string);
        break;
      default:
        UNREACHABLE();
    }
  }
}

}  // namespace tracing
}  // namespace platform
}  // namespace v8
