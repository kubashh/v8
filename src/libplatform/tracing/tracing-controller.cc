// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/libplatform/v8-tracing.h"

#include "src/base/atomicops.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"

#ifdef V8_USE_PERFETTO
#include "base/trace_event/common/trace_event_common.h"
#include "perfetto/trace/chrome/chrome_trace_event.pbzero.h"
#include "perfetto/trace/trace_packet.pbzero.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/core/trace_writer.h"
#include "src/libplatform/tracing/perfetto-tracing-controller.h"
#include "src/libplatform/tracing/trace-event-utils.h"
#endif  // V8_USE_PERFETTO

namespace v8 {
namespace platform {
namespace tracing {

static const size_t kMaxCategoryGroups = 200;

// Parallel arrays g_category_groups and g_category_group_enabled are separate
// so that a pointer to a member of g_category_group_enabled can be easily
// converted to an index into g_category_groups. This allows macros to deal
// only with char enabled pointers from g_category_group_enabled, and we can
// convert internally to determine the category name from the char enabled
// pointer.
const char* g_category_groups[kMaxCategoryGroups] = {
    "toplevel",
    "tracing categories exhausted; must increase kMaxCategoryGroups",
    "__metadata"};

// The enabled flag is char instead of bool so that the API can be used from C.
unsigned char g_category_group_enabled[kMaxCategoryGroups] = {0};
// Indexes here have to match the g_category_groups array indexes above.
const int g_category_categories_exhausted = 1;
// Metadata category not used in V8.
// const int g_category_metadata = 2;
const int g_num_builtin_categories = 3;

// Skip default categories.
v8::base::AtomicWord g_category_index = g_num_builtin_categories;

TracingController::TracingController() = default;

TracingController::~TracingController() {
  StopTracing();

  {
    // Free memory for category group names allocated via strdup.
    base::MutexGuard lock(mutex_.get());
    for (size_t i = g_category_index - 1; i >= g_num_builtin_categories; --i) {
      const char* group = g_category_groups[i];
      g_category_groups[i] = nullptr;
      free(const_cast<char*>(group));
    }
    g_category_index = g_num_builtin_categories;
  }
}

void TracingController::Initialize(TraceBuffer* trace_buffer) {
  trace_buffer_.reset(trace_buffer);
  mutex_.reset(new base::Mutex());
}

#ifdef V8_USE_PERFETTO
void TracingController::InitializeForPerfetto(std::ostream* output_stream) {
  output_stream_ = output_stream;
  DCHECK_NOT_NULL(output_stream);
  DCHECK(output_stream->good());
}
#endif

int64_t TracingController::CurrentTimestampMicroseconds() {
  return base::TimeTicks::HighResolutionNow().ToInternalValue();
}

int64_t TracingController::CurrentCpuTimestampMicroseconds() {
  return base::ThreadTicks::Now().ToInternalValue();
}

uint64_t TracingController::AddTraceEvent(
    char phase, const uint8_t* category_enabled_flag, const char* name,
    const char* scope, uint64_t id, uint64_t bind_id, int num_args,
    const char** arg_names, const uint8_t* arg_types,
    const uint64_t* arg_values,
    std::unique_ptr<v8::ConvertableToTraceFormat>* arg_convertables,
    unsigned int flags) {
  int64_t now_us = CurrentTimestampMicroseconds();

  return AddTraceEventWithTimestamp(
      phase, category_enabled_flag, name, scope, id, bind_id, num_args,
      arg_names, arg_types, arg_values, arg_convertables, flags, now_us);
}

uint64_t TracingController::AddTraceEventWithTimestamp(
    char phase, const uint8_t* category_enabled_flag, const char* name,
    const char* scope, uint64_t id, uint64_t bind_id, int num_args,
    const char** arg_names, const uint8_t* arg_types,
    const uint64_t* arg_values,
    std::unique_ptr<v8::ConvertableToTraceFormat>* arg_convertables,
    unsigned int flags, int64_t timestamp) {
  int64_t cpu_now_us = CurrentCpuTimestampMicroseconds();
  uint64_t handle = 0;

#ifdef V8_USE_PERFETTO

  if (perfetto_recording_.load()) {
    if (phase != TRACE_EVENT_PHASE_COMPLETE) {
      ::perfetto::TraceWriter* writer =
          perfetto_tracing_controller_->GetOrCreateThreadLocalWriter();
      // TODO(petermarshall): We shouldn't start one packet for each event.
      // We should try to bundle them together in one bundle.
      auto packet = writer->NewTracePacket();
      auto* trace_event_bundle = packet->set_chrome_events();
      auto* trace_event = trace_event_bundle->add_trace_events();

      trace_event->set_name(name);
      trace_event->set_timestamp(timestamp);
      trace_event->set_phase(phase);
      trace_event->set_thread_id(base::OS::GetCurrentThreadId());
      trace_event->set_duration(0);
      trace_event->set_thread_duration(0);
      if (scope) trace_event->set_scope(scope);
      trace_event->set_id(id);
      trace_event->set_flags(flags);
      if (category_enabled_flag) {
        const char* category_group_name =
            GetCategoryGroupName(category_enabled_flag);
        DCHECK_NOT_NULL(category_group_name);
        trace_event->set_category_group_name(category_group_name);
      }
      trace_event->set_process_id(base::OS::GetCurrentProcessId());
      trace_event->set_thread_timestamp(cpu_now_us);
      trace_event->set_bind_id(bind_id);

      ChromeTraceEventUtils::AddArgsToTraceProto(trace_event, num_args,
                                                 arg_names, arg_types,
                                                 arg_values, arg_convertables);

      packet->Finalize();
    } else {
      // It is a Complete event. We need to create a TempTraceRecord. For a
      // handle we just use the index into the unfinished event stack.

      // 'Complete'/'X' events expect a handle to be returned that can be used
      // with UpdateTraceEventDuration() to set their duration when they finish.
      // This isn't directly compatible with Perfetto so we need to maintain a
      // thread-local stack of unfinished Complete events.
      TempTraceRecord* temp_trace_object =
          perfetto_tracing_controller_->NewPendingEvent(&handle);
      // If this fails, we'ev exceed the maximum number of nested 'X' events.
      CHECK_NOT_NULL(temp_trace_object);

      temp_trace_object->name = name;
      temp_trace_object->timestamp = timestamp;
      temp_trace_object->phase = phase;
      temp_trace_object->thread_id = base::OS::GetCurrentThreadId();
      temp_trace_object->duration = 0;
      temp_trace_object->thread_duration = 0;
      temp_trace_object->scope = scope;
      temp_trace_object->id = id;
      temp_trace_object->flags = flags;
      temp_trace_object->category_enabled_flag = category_enabled_flag;
      temp_trace_object->process_id = base::OS::GetCurrentProcessId();
      temp_trace_object->thread_timestamp = cpu_now_us;
      temp_trace_object->bind_id = bind_id;
      temp_trace_object->num_args = num_args;
      DCHECK_LE(num_args, kTraceMaxNumArgs);
      for (int i = 0; i < num_args; i++) {
        temp_trace_object->arg_names[i] = arg_names[i];
        temp_trace_object->arg_types[i] = arg_types[i];
        temp_trace_object->arg_values[i] = arg_values[i];
        if (arg_types[i] == TRACE_VALUE_TYPE_CONVERTABLE) {
          temp_trace_object->arg_convertables[i] =
              std::move(arg_convertables[i]);
        }
      }
    }
  }
#endif  // V8_USE_PERFETTO

  if (recording_.load(std::memory_order_acquire)) {
    TraceObject* trace_object = trace_buffer_->AddTraceEvent(&handle);
    if (trace_object) {
      {
        base::MutexGuard lock(mutex_.get());
        trace_object->Initialize(phase, category_enabled_flag, name, scope, id,
                                 bind_id, num_args, arg_names, arg_types,
                                 arg_values, arg_convertables, flags, timestamp,
                                 cpu_now_us);
      }
    }
  }
  return handle;
}

void TracingController::UpdateTraceEventDuration(
    const uint8_t* category_enabled_flag, const char* name, uint64_t handle) {
  int64_t now_us = CurrentTimestampMicroseconds();
  int64_t cpu_now_us = CurrentCpuTimestampMicroseconds();

#ifdef V8_USE_PERFETTO
  // TODO(petermarshall): Should we still record the end of unfinished events
  // when tracing has stopped?
  // TODO(petermarshall): We shouldn't start one packet for each event. We
  // should try to bundle them together in one bundle.
  if (perfetto_recording_.load()) {
    ::perfetto::TraceWriter* writer =
        perfetto_tracing_controller_->GetOrCreateThreadLocalWriter();

    auto packet = writer->NewTracePacket();
    auto* trace_event_bundle = packet->set_chrome_events();
    auto* trace_event = trace_event_bundle->add_trace_events();

    // TODO(petermarshall): The handle here is wrong because it is the handle
    // for the legacy tracing controller, not perfetto. We can't remember both
    // but we run both controllers side-by-side right now. This will be removed
    // when they don't run side-by-side anymore.
    TempTraceRecord* temp_trace_record =
        perfetto_tracing_controller_->GetAndRemoveEventByHandle(handle);
    temp_trace_record->UpdateDuration(now_us, cpu_now_us);

    temp_trace_record->ConvertToChromeTraceEvent(trace_event);
    packet->Finalize();
  }
#endif  // V8_USE_PERFETTO

  TraceObject* trace_object = trace_buffer_->GetEventByHandle(handle);
  if (!trace_object) return;
  trace_object->UpdateDuration(now_us, cpu_now_us);
}

const char* TracingController::GetCategoryGroupName(
    const uint8_t* category_group_enabled) {
  // Calculate the index of the category group by finding
  // category_group_enabled in g_category_group_enabled array.
  uintptr_t category_begin =
      reinterpret_cast<uintptr_t>(g_category_group_enabled);
  uintptr_t category_ptr = reinterpret_cast<uintptr_t>(category_group_enabled);
  // Check for out of bounds category pointers.
  DCHECK(category_ptr >= category_begin &&
         category_ptr < reinterpret_cast<uintptr_t>(g_category_group_enabled +
                                                    kMaxCategoryGroups));
  uintptr_t category_index =
      (category_ptr - category_begin) / sizeof(g_category_group_enabled[0]);
  return g_category_groups[category_index];
}

void TracingController::StartTracing(TraceConfig* trace_config) {
#ifdef V8_USE_PERFETTO
  perfetto_tracing_controller_ = base::make_unique<PerfettoTracingController>();

  ::perfetto::TraceConfig perfetto_trace_config;

  perfetto_trace_config.add_buffers()->set_size_kb(4096);
  auto* ds_config = perfetto_trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("v8.trace_events");

  DCHECK_NOT_NULL(output_stream_);
  DCHECK(output_stream_->good());
  // TODO(petermarshall): Set all the params from |perfetto_trace_config|.
  perfetto_tracing_controller_->StartTracing(perfetto_trace_config,
                                             output_stream_);
  perfetto_recording_.store(true);
#endif  // V8_USE_PERFETTO

  trace_config_.reset(trace_config);
  std::unordered_set<v8::TracingController::TraceStateObserver*> observers_copy;
  {
    base::MutexGuard lock(mutex_.get());
    recording_.store(true, std::memory_order_release);
    UpdateCategoryGroupEnabledFlags();
    observers_copy = observers_;
  }
  for (auto o : observers_copy) {
    o->OnTraceEnabled();
  }
}

void TracingController::StopTracing() {
  bool expected = true;
  if (!recording_.compare_exchange_strong(expected, false)) {
    return;
  }
  DCHECK(trace_buffer_);
  UpdateCategoryGroupEnabledFlags();
  std::unordered_set<v8::TracingController::TraceStateObserver*> observers_copy;
  {
    base::MutexGuard lock(mutex_.get());
    observers_copy = observers_;
  }
  for (auto o : observers_copy) {
    o->OnTraceDisabled();
  }

#ifdef V8_USE_PERFETTO
  perfetto_recording_.store(false);
  perfetto_tracing_controller_->StopTracing();
  perfetto_tracing_controller_.reset();
#endif  // V8_USE_PERFETTO

  {
    base::MutexGuard lock(mutex_.get());
    trace_buffer_->Flush();
  }
}

void TracingController::UpdateCategoryGroupEnabledFlag(size_t category_index) {
  unsigned char enabled_flag = 0;
  const char* category_group = g_category_groups[category_index];
  if (recording_.load(std::memory_order_acquire) &&
      trace_config_->IsCategoryGroupEnabled(category_group)) {
    enabled_flag |= ENABLED_FOR_RECORDING;
  }

  // TODO(fmeawad): EventCallback and ETW modes are not yet supported in V8.
  // TODO(primiano): this is a temporary workaround for catapult:#2341,
  // to guarantee that metadata events are always added even if the category
  // filter is "-*". See crbug.com/618054 for more details and long-term fix.
  if (recording_.load(std::memory_order_acquire) &&
      !strcmp(category_group, "__metadata")) {
    enabled_flag |= ENABLED_FOR_RECORDING;
  }

  base::Relaxed_Store(reinterpret_cast<base::Atomic8*>(
                          g_category_group_enabled + category_index),
                      enabled_flag);
}

void TracingController::UpdateCategoryGroupEnabledFlags() {
  size_t category_index = base::Acquire_Load(&g_category_index);
  for (size_t i = 0; i < category_index; i++) UpdateCategoryGroupEnabledFlag(i);
}

const uint8_t* TracingController::GetCategoryGroupEnabled(
    const char* category_group) {
  // Check that category group does not contain double quote
  DCHECK(!strchr(category_group, '"'));

  // The g_category_groups is append only, avoid using a lock for the fast path.
  size_t category_index = base::Acquire_Load(&g_category_index);

  // Search for pre-existing category group.
  for (size_t i = 0; i < category_index; ++i) {
    if (strcmp(g_category_groups[i], category_group) == 0) {
      return &g_category_group_enabled[i];
    }
  }

  // Slow path. Grab the lock.
  base::MutexGuard lock(mutex_.get());

  // Check the list again with lock in hand.
  unsigned char* category_group_enabled = nullptr;
  category_index = base::Acquire_Load(&g_category_index);
  for (size_t i = 0; i < category_index; ++i) {
    if (strcmp(g_category_groups[i], category_group) == 0) {
      return &g_category_group_enabled[i];
    }
  }

  // Create a new category group.
  // Check that there is a slot for the new category_group.
  DCHECK(category_index < kMaxCategoryGroups);
  if (category_index < kMaxCategoryGroups) {
    // Don't hold on to the category_group pointer, so that we can create
    // category groups with strings not known at compile time (this is
    // required by SetWatchEvent).
    const char* new_group = strdup(category_group);
    g_category_groups[category_index] = new_group;
    DCHECK(!g_category_group_enabled[category_index]);
    // Note that if both included and excluded patterns in the
    // TraceConfig are empty, we exclude nothing,
    // thereby enabling this category group.
    UpdateCategoryGroupEnabledFlag(category_index);
    category_group_enabled = &g_category_group_enabled[category_index];
    // Update the max index now.
    base::Release_Store(&g_category_index, category_index + 1);
  } else {
    category_group_enabled =
        &g_category_group_enabled[g_category_categories_exhausted];
  }
  return category_group_enabled;
}

void TracingController::AddTraceStateObserver(
    v8::TracingController::TraceStateObserver* observer) {
  {
    base::MutexGuard lock(mutex_.get());
    observers_.insert(observer);
    if (!recording_.load(std::memory_order_acquire)) return;
  }
  // Fire the observer if recording is already in progress.
  observer->OnTraceEnabled();
}

void TracingController::RemoveTraceStateObserver(
    v8::TracingController::TraceStateObserver* observer) {
  base::MutexGuard lock(mutex_.get());
  DCHECK(observers_.find(observer) != observers_.end());
  observers_.erase(observer);
}

}  // namespace tracing
}  // namespace platform
}  // namespace v8
