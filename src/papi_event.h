#ifndef PAPI_EVENT_H
#define PAPI_EVENT_H

#define PAPI_AVAIL

#include "papi.h"
#include "src/base/macros.h"

#include <iostream>
#include <iomanip>

#ifdef PAPI_AVAIL
constexpr int EMPTY_HANDLE = PAPI_NULL;
#else
constexpr int EMPTY_HANDLE = -1;
#endif

class PAPI {
 public:
  PAPI() { PAPI_library_init(PAPI_VER_CURRENT); }
  ~PAPI() { PAPI_shutdown(); }
};

class PAPIEventSetImpl {
public:
 PAPIEventSetImpl() = default;
 PAPIEventSetImpl(int event_set_handle) : event_set_handle_(event_set_handle) {}

 void initEventSet(std::string event_name) {
   PAPI_create_eventset(&event_set_handle_);
   PAPI_add_named_event(getHandle(), const_cast<char*>(event_name.c_str()));
 }
 void setHandle(int event_set_handle) { event_set_handle_ = event_set_handle; }
 int getHandle() const { return event_set_handle_; }
 bool isValidHandle() const { return event_set_handle_ != EMPTY_HANDLE; }
 operator bool() const { return isValidHandle(); }

private:
  int event_set_handle_ = EMPTY_HANDLE;
};

class PAPIEventSetDummy {
public:
  PAPIEventSetDummy() = default;
  PAPIEventSetDummy(int) {};

  void initEventSet(std::string) {}
  void setHandle(int event_set_handle) {}
  int getHandle() const { return EMPTY_HANDLE; }
  bool isValidHandle() const { return false; }
  operator bool() const { return isValidHandle(); }
};

#ifdef PAPI_AVAIL
using PAPIEventSet = PAPIEventSetImpl;

inline void initPAPI() { static PAPI papi; };

V8_INLINE void PAPIstop(const PAPIEventSet& event_set, long long* count) {
  PAPI_stop(event_set.getHandle(), count);
  DCHECK_GE(*count, 0);
}
V8_INLINE void PAPIreset(const PAPIEventSet &event_set) {
  PAPI_reset(event_set.getHandle());
}
V8_INLINE void PAPIstart(const PAPIEventSet &event_set) {
  PAPI_start(event_set.getHandle());
}

struct addEventHeader {};
inline std::ostream& operator<<(std::ostream& os, addEventHeader&&) {
  os << std::setw(23) << "PAPI_L1_DCM"
     << "/pC";
  return os;
}
struct addEventHeaderOffSet {};
inline std::ostream& operator<<(std::ostream& os, addEventHeaderOffSet&&) {
  os << std::string(18, '=');
  return os;
}

struct addEventFooterOffSet {};
inline std::ostream& operator<<(std::ostream& os, addEventFooterOffSet&&) {
  os << std::string(18, '-');
  return os;
}
#else
using PAPIEventSet = PAPIEventSetDummy;

inline void initPAPI() {};

V8_INLINE void PAPIstop(const PAPIEventSet& event_set, long long* count) {}
V8_INLINE void PAPIreset(const PAPIEventSet &event_set) {}
V8_INLINE void PAPIstart(const PAPIEventSet &event_set) {}

struct addEventHeader {};
inline std::ostream& operator<<(std::ostream& os, addEventHeader&&) {
  return os;
}
struct addEventHeaderOffSet {};
inline std::ostream& operator<<(std::ostream& os, addEventHeaderOffSet&&) {
  return os;
}
struct addEventFooterOffSet {};
inline std::ostream& operator<<(std::ostream& os, addEventFooterOffSet&&) {
  return os;
}
#endif

enum class Event {
  L1_DCM = 0,
};

constexpr inline char* getEventName(Event event) {
  switch (event) {
    case Event::L1_DCM:
      return const_cast<char*>("PAPI_L1_DCM");
  }
}

template <Event event, int instance>
class EventCounter {
public:
  static int init() {
    return getEventSetHandle();
  }

  static int getEventSetHandle() {
    static EventCounter counter;
    //std::cout << instance << " -> " <<counter.getLocalEventSetHandle() << '\n';
    return counter.getLocalEventSetHandle();
  }

 private:
  EventCounter() {
    initPAPI();
    event_set_.initEventSet(getEventName(event));
    std::cout << this  <<"\n";
  }

  int getLocalEventSetHandle() { return event_set_.getHandle(); }

  PAPIEventSet event_set_;
};

template <Event event, int instance = 0>
class EventCounterScope {
public:
  EventCounterScope() {
    int event_set_handle = EventCounter<event, instance>::init();
    //PAPIreset(event_set_handle);
    PAPIstart(event_set_handle);
  }

  ~EventCounterScope() {
    int event_set_handle = EventCounter<event, instance>::getEventSetHandle();
    long long count = 0;
    PAPIstop(event_set_handle, &count);
    std::cout << "Res: " << event_set_handle  << " " << count << '\n';
  }
};

#endif // PAPI_EVENT_H
