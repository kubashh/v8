// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/gdb-server/gdb-server-thread.h"
#include "src/wasm/gdb-server/gdb-server.h"
#include "src/wasm/gdb-server/session.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

GdbServerThread::GdbServerThread(GdbServer* gdb_server)
    : Thread(v8::base::Thread::Options("GdbServerThread")),
      gdb_server_(gdb_server) {}

void GdbServerThread::Run() {
#ifdef _WIN32
  // Initialize Winsock
  WSADATA wsaData;
  int iResult = ::WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0) {
    TRACE_GDB_REMOTE("GdbServerThread::Run: WSAStartup failed\n");
    return;
  }
#endif

  // If the default port is not available, try any port.
  std::unique_ptr<SocketBinding> socket_binding =
      SocketBinding::Bind(FLAG_wasm_gdb_remote_port);
  if (!socket_binding) {
    socket_binding = SocketBinding::Bind(0);
  }
  if (!socket_binding) {
    TRACE_GDB_REMOTE("GdbServerThread::Run: Failed to bind any TCP port\n");
    return;
  }
  TRACE_GDB_REMOTE("gdb-remote(%d) : Connect GDB with 'target remote :%d\n",
                   __LINE__, socket_binding->GetBoundPort());

  transport_ = socket_binding->CreateTransport();
  target_ = std::make_unique<Target>(gdb_server_);

  while (!target_->IsTerminated()) {
    // Wait for a connection.
    if (!transport_->AcceptConnection()) continue;

    // Create a new session for this connection
    Session session(transport_.get());
    TRACE_GDB_REMOTE("debug : Connected\n");

    // Run this session for as long as it lasts
    target_->Run(&session);
  }

  target_ = nullptr;
  transport_ = nullptr;

#if _WIN32
  ::WSACleanup();
#endif
}

void GdbServerThread::Stop() {
  if (target_) {
    target_->Terminate();
  }

  if (transport_) {
    transport_->Close();
  }
}

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8
