// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_GDB_SERVER_TRANSPORT_H_
#define V8_INSPECTOR_GDB_SERVER_TRANSPORT_H_

#include <sstream>
#include <vector>
#include "src/base/macros.h"
#include "src/wasm/gdb-server/util.h"

#if _WIN32
#include <windows.h>
#include <winsock2.h>

typedef SOCKET SocketHandle;

#define CloseSocket closesocket
#define InvalidSocket INVALID_SOCKET
#define SocketGetLastError() WSAGetLastError()
typedef int ssize_t;
typedef int socklen_t;
#else

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int SocketHandle;

#define CloseSocket close
#define InvalidSocket (-1)
#define SocketGetLastError() errno

#endif

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

class Transport;

// scoped_array<C> is like scoped_ptr<C>, except that the caller must allocate
// with new [] and the destructor deletes objects with delete [].
//
// As with scoped_ptr<C>, a scoped_array<C> either points to an object
// or is NULL.  A scoped_array<C> owns the object that it points to.
// scoped_array<T> is thread-compatible, and once you index into it,
// the returned objects have only the threadsafety guarantees of T.
//
// Size: sizeof(scoped_array<C>) == sizeof(C*)
template <class C>
class scoped_array {
 public:
  // The element type
  typedef C element_type;

  // Constructor.  Defaults to intializing with NULL.
  // There is no way to create an uninitialized scoped_array.
  // The input parameter must be allocated with new [].
  explicit scoped_array(C* p = NULL) : array_(p) {}

  // Destructor.  If there is a C object, delete it.
  // We don't need to test array_ == NULL because C++ does that for us.
  ~scoped_array() {
    enum { type_must_be_complete = sizeof(C) };
    delete[] array_;
  }

  // Reset.  Deletes the current owned object, if any.
  // Then takes ownership of a new object, if given.
  // this->reset(this->get()) works.
  void reset(C* p = NULL) {
    if (p != array_) {
      enum { type_must_be_complete = sizeof(C) };
      delete[] array_;
      array_ = p;
    }
  }

  // Get one element of the current object.
  // Will assert() if there is no current object, or index i is negative.
  C& operator[](std::ptrdiff_t i) const {
    assert(i >= 0);
    assert(array_ != NULL);
    return array_[i];
  }

  // Get a pointer to the zeroth element of the current object.
  // If there is no current object, return NULL.
  C* get() const { return array_; }

  // Comparison operators.
  // These return whether two scoped_array refer to the same object, not just to
  // two different but equal objects.
  bool operator==(C* p) const { return array_ == p; }
  bool operator!=(C* p) const { return array_ != p; }

  // Swap two scoped arrays.
  void swap(scoped_array& p2) {
    C* tmp = array_;
    array_ = p2.array_;
    p2.array_ = tmp;
  }

  // Release an array.
  // The return value is the current pointer held by this object.
  // If this object holds a NULL pointer, the return value is NULL.
  // After this operation, this object will hold a NULL pointer,
  // and will not own the array any more.
  C* release() {
    C* retVal = array_;
    array_ = NULL;
    return retVal;
  }

 private:
  C* array_;

  // Forbid comparison of different scoped_array types.
  template <class C2>
  bool operator==(scoped_array<C2> const& p2) const;
  template <class C2>
  bool operator!=(scoped_array<C2> const& p2) const;

  // Disallow evil constructors
  scoped_array(const scoped_array&);
  void operator=(const scoped_array&);
};

class SocketBinding {
 public:
  // Wrap existing socket handle.
  explicit SocketBinding(SocketHandle socket_handle);
  // Bind to the specified TCP port.
  static SocketBinding* Bind(const char* addr);

  // Create a transport object from this socket binding
  Transport* CreateTransport();

  // Get port the socket is bound to.
  uint16_t GetBoundPort();

 private:
  SocketHandle socket_handle_;
};

class Transport {
 public:
  explicit Transport(SocketHandle s);
  ~Transport();

#if _WIN32
  void CreateSocketEvent() {
    socket_event_ = WSACreateEvent();
    if (socket_event_ == WSA_INVALID_EVENT) {
      GdbRemoteLog(
          LOG_FATAL,
          "Transport::CreateSocketEvent: Failed to create socket event\n");
    }
    // Listen for close events in order to handle them correctly.
    // Additionally listen for read readiness as WSAEventSelect sets the socket
    // to non-blocking mode.
    // http://msdn.microsoft.com/en-us/library/windows/desktop/ms738547(v=vs.85).aspx
    if (WSAEventSelect(handle_accept_, socket_event_, FD_CLOSE | FD_READ) ==
        SOCKET_ERROR) {
      GdbRemoteLog(
          LOG_FATAL,
          "Transport::CreateSocketEvent: Failed to bind event to socket\n");
    }
  }
#endif

  // Read from this transport, return true on success.
  bool Read(void* ptr, int32_t len);

  // Write to this transport, return true on success.
  bool Write(const void* ptr, int32_t len);

  // Return true if there is data to read.
  bool IsDataAvailable() {
    if (pos_ < size_) {
      return true;
    }
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(handle_accept_, &fds);

    // We want a "non-blocking" check
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    // Check if this file handle can select on read
    int cnt =
        select(static_cast<int>(handle_accept_) + 1, &fds, 0, 0, &timeout);

    // If we are ready, or if there is an error.  We return true
    // on error, to let the next IO request fail.
    if (cnt != 0) return true;

    return false;
  }

  void WaitForDebugStubEvent();

  bool SignalThreadEvent();

// On windows, the header that defines this has other definition
// colitions, so we define it outselves just in case
#ifndef SD_BOTH
#define SD_BOTH 2
#endif

  void Disconnect() {
    // Shutdown the connection in both diections.  This should
    // always succeed, and nothing we can do if this fails.
    (void)::shutdown(handle_accept_, SD_BOTH);

    if (handle_accept_ != InvalidSocket) CloseSocket(handle_accept_);
#if _WIN32
    if (!WSACloseEvent(socket_event_)) {
      GdbRemoteLog(LOG_FATAL,
                   "Transport::~Transport: Failed to close socket event\n");
    }
    socket_event_ = WSA_INVALID_EVENT;
#endif
    handle_accept_ = InvalidSocket;
  }

  bool AcceptConnection() {
    CHECK(handle_accept_ == InvalidSocket);
    handle_accept_ = ::accept(handle_bind_, NULL, 0);
    if (handle_accept_ != InvalidSocket) {
      // Do not delay sending small packets.  This significantly speeds up
      // remote debugging.  Debug stub uses buffering to send outgoing packets
      // so they are not split into more TCP packets than necessary.
      int nodelay = 1;
      if (setsockopt(handle_accept_, IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<char*>(&nodelay), sizeof(nodelay))) {
        GdbRemoteLog(LOG_WARNING, "Failed to set TCP_NODELAY option.\n");
      }
#if _WIN32
      CreateSocketEvent();
#endif
      return true;
    }
    return false;
  }

  static const int kBufSize = 4096;

 private:
  // Copy buffered data to *dst up to len bytes and update dst and len.
  void CopyFromBuffer(char** dst, int32_t* len);

  // Read available data from the socket. Return false on EOF or error.
  bool ReadSomeData();

  scoped_array<char> buf_;
  int32_t pos_;
  int32_t size_;
  SocketHandle handle_bind_;
  SocketHandle handle_accept_;
#if _WIN32
  HANDLE socket_event_;
  HANDLE faulted_thread_event_;
#else
  int faulted_thread_fd_read_;
  int faulted_thread_fd_write_;
#endif
};

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_INSPECTOR_GDB_SERVER_TRANSPORT_H_
