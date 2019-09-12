// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/gdb-server/gdb-server-thread.h"
#include <sstream>
#include "src/inspector/gdb-server/session.h"

// using namespace v8;

namespace v8_inspector {

class Packet;

GdbServerThread::GdbServerThread(GdbServer* gdb_server,
                                 V8InspectorClient* inspector_client,
                                 V8InspectorSession* session)
    : Thread(v8::base::Thread::Options("GdbServerThread")),
      gdb_server_(gdb_server),
      session_(session),
      process_status_(ProcessStatus::Running) {}

void GdbServerThread::Run() {
#ifdef _WIN32
  // Initialize Winsock
  WSADATA wsaData;
  int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0) {
    GdbRemoteLog(LOG_FATAL, "Transport::Transport: WSAStartup failed\n");
    return;
  }
#endif

  // Try port 8765 first.
  SocketBinding* socket_binding = SocketBinding::Bind("127.0.0.1:8765");
  // If port 8765 is not available, try any port.
  if (!socket_binding) {
    socket_binding = SocketBinding::Bind("127.0.0.1:0");
  }
  if (!socket_binding) {
    GdbRemoteLog(LOG_ERROR,
                 "NaClDebugStubBindSocket: Failed to bind any TCP port\n");
    return;
  }
  // GdbRemoteLog(LOG_WARNING,
  //        "nacl_debug(%d) : Connect GDB with 'target remote :%d\n",
  //        __LINE__, g_socket_binding->GetBoundPort());

  transport_.reset(socket_binding->CreateTransport());
  target_ = std::make_unique<Target>(gdb_server_);

  while (true) {
    // Wait for a connection.
    if (!transport_->AcceptConnection()) continue;

    // Create a new session for this connection
    Session session(transport_.get());
    session.SetFlags(Session::DEBUG_MASK);

    GdbRemoteLog(LOG_WARNING, "debug : Connected\n");

    // Run this session for as long as it lasts
    target_->Run(&session);
  }

  // target_ = nullptr;
  // transport_ = nullptr;

  //#if _WIN32
  //  WSACleanup();
  //#endif
}

void GdbServerThread::OnPaused(const std::vector<uint64_t>& callFrames) {
  target_->OnPaused(callFrames);
}

}  // namespace v8_inspector
