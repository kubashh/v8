// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBPLATFORM_TRACING_PERFETTO_TRACING_CONTROLLER_H_
#define V8_LIBPLATFORM_TRACING_PERFETTO_TRACING_CONTROLLER_H_

#include <atomic>
#include <fstream>
#include <memory>
#include <vector>

#include "src/base/platform/platform.h"
#include "src/base/platform/semaphore.h"

namespace perfetto {
class TraceConfig;
class TraceWriter;
class TracingService;
}  // namespace perfetto

namespace v8 {
namespace platform {
namespace tracing {

class PerfettoJSONConsumer;
class PerfettoProducer;
class PerfettoTaskRunner;
struct TempTraceRecord;

// This is the top-level interface for performing tracing with perfetto. The
// user of this class should call StartTracing() to start tracing, and
// StopTracing() to stop it. To write trace events, the user can obtain a
// thread-local TraceWriter object using GetOrCreateThreadLocalWriter().
class PerfettoTracingController {
 public:
  PerfettoTracingController();

  // Blocks and sets up all required data structures for tracing. It is safe to
  // call GetOrCreateThreadLocalWriter() to obtain thread-local TraceWriters for
  // writing trace events once this call returns.
  void StartTracing(const ::perfetto::TraceConfig& trace_config);

  // Blocks and finishes all existing AddTraceEvent tasks. Stops the tracing
  // thread.
  void StopTracing();

  ~PerfettoTracingController();

  // Each thread that wants to trace should call this to get their TraceWriter.
  // PerfettoTracingController creates and owns the TraceWriter.
  ::perfetto::TraceWriter* GetOrCreateThreadLocalWriter();

  // Adds a TempTraceRecord to the pending event stack. Sets |handle_out| to
  // the index of the TempTraceRecord on the event stack. If the stack is full,
  // returns nullptr and does not set handle_out.
  TempTraceRecord* NewPendingEvent(uint64_t* handle_out);

  // Retrieves a TempTraceRecord from the pending event stack. The handle
  // must reference the top object in the stack. Removes the object from the
  // stack; calling NewPendingEvent() will invalidate the result of this
  // function.
  TempTraceRecord* GetAndRemoveEventByHandle(uint64_t handle);

 private:
  // Signals the producer_ready_semaphore_.
  void OnProducerReady();

  // PerfettoProducer is the only class allowed to call OnProducerReady().
  friend class PerfettoProducer;

  std::unique_ptr<::perfetto::TracingService> service_;
  std::unique_ptr<PerfettoProducer> producer_;
  std::unique_ptr<PerfettoJSONConsumer> consumer_;
  std::unique_ptr<PerfettoTaskRunner> task_runner_;
  base::Thread::LocalStorageKey writer_key_;
  // A semaphore that is signalled when StartRecording is called. StartTracing
  // waits on this semaphore to be notified when the tracing service is ready to
  // receive trace events.
  base::Semaphore producer_ready_semaphore_;
  base::Semaphore consumer_finished_semaphore_;

  // TODO(petermarshall): pass this in instead.
  std::ofstream trace_file_;

  // Same depth that Chrome uses. This is essentially the maximum number of
  // nested 'X' trace events that perfetto can handle.
  static constexpr size_t kPendingStackSize = 30;
  // Thread-local stack of pending 'X' events which come in two parts - begin
  // and end, but we haven't received the end part yet.
  base::Thread::LocalStorageKey pending_events_stack_key_;
  // Index of the next free slot in the pending events stack. There are no free
  // slots if the pending events index == kPendingStackSize.
  base::Thread::LocalStorageKey pending_events_index_key_;
  TempTraceRecord* pending_events_stack() const;
  void InitializeThreadLocals();

  DISALLOW_COPY_AND_ASSIGN(PerfettoTracingController);
};

}  // namespace tracing
}  // namespace platform
}  // namespace v8

#endif  // V8_LIBPLATFORM_TRACING_PERFETTO_TRACING_CONTROLLER_H_
