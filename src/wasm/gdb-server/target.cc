// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/gdb-server/target.h"
#include <inttypes.h>
#include <sstream>
#include "src/wasm/gdb-server/gdb-server.h"
#include "src/wasm/gdb-server/packet.h"
#include "src/wasm/gdb-server/session.h"
#include "src/wasm/gdb-server/transport.h"
#include "src/wasm/gdb-server/util.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

// static const int kSigInt = 2;
static const int kSigTrace = 5;
static const int kThreadId = 1;

Target::Target(GdbServer* gdb_server)
    : gdb_server_(gdb_server),
      session_(nullptr),
      cur_signal_(0),
      detaching_(false),
      should_exit_(false),
      waiting_for_initial_suspension_(false),
      status_(Status::Running),
      semaphore_(0) {
  Init();
}

Target::~Target() { Destroy(); }

bool Target::Init() {
  properties_["Supported"] =
      "PacketSize=1000;vContSupported-;qXfer:libraries:read+;"
      "jGetLoadedDynamicLibrariesInfos+";
  properties_["Attached"] = "1";

  properties_["RegisterInfo0"] =
      "name:pc;alt-name:pc;bitsize:64;offset:0;encoding:uint;format:hex;set:"
      "General Purpose Registers;gcc:16;dwarf:16;generic:pc;";
  properties_["RegisterInfo1"] = "E45";

  properties_["ProcessInfo"] =
      "pid:1;ppid:1;uid:1;gid:1;euid:1;egid:1;name:6c6c6462;triple:" +
      Mem2Hex("wasm32-unknown-unknown-wasm") + ";ptrsize:4;";

  properties_["Symbol"] = "OK";

  waiting_for_initial_suspension_ = true;
  return true;
}

void Target::Destroy() {}

void Target::Detach() {
  GdbRemoteLog(LOG_INFO, "Requested Detach.\n");
  detaching_ = true;
}

void Target::Kill() {
  GdbRemoteLog(LOG_INFO, "Requested Kill.\n");
  should_exit_ = true;
}

void Target::Run(Session* session) {
  {
    v8::base::MutexGuard guard(&mutex_);
    session_ = session;
  }

  do {
    WaitForDebugEvent();
    ProcessDebugEvent();
    ProcessCommands();
  } while (session_->Connected());

  {
    v8::base::MutexGuard guard(&mutex_);
    session_ = nullptr;
  }
}

void Target::WaitForDebugEvent() {
  // TODO(paolosev): Carefully review thread-safety of Target class

  if (status_ == Status::Suspended) {
    // If the thread is suspended (which may be left over from a previous
    // connection), we are already ready to handle commands from LLDB.
    return;
  }

  // Wait for either:
  //   * the thread to fault (or single-step) // TODO(paolosev)
  //   * an interrupt from LLDB
  session_->WaitForDebugStubEvent();
}

void Target::ProcessDebugEvent() {
  if (cur_signal_ != 0) {
  } else if (status_ == Status::Suspended || status_ == WaitingForSuspension) {
    // Already suspended. Nothing to do.
    return;
  } else if (!waiting_for_initial_suspension_ && !session_->IsDataAvailable()) {
    // No input from LLDB. Nothing to do, so try again.
    return;
  } else {
    // Here we can block, waiting for the engine to suspend.
    Suspend();
  }

  // Here, the wasm interpreter has suspended and we have updated the current
  // thread info.
  {
    v8::base::MutexGuard guard(&mutex_);

    if (!waiting_for_initial_suspension_) {
      // First time on a connection, we don't send the signal.
      // All other times, send the signal that triggered us.
      Packet pktOut;
      SetStopReply(&pktOut);
      session_->SendPacketOnly(&pktOut);
    }
    waiting_for_initial_suspension_ = false;
  }
}

void Target::Suspend() {
  // Executed in the GdbServer thread
  if (status_ == Status::Running) {
    // TODO(paolosev) - this only suspends the wasm interpreter.
    gdb_server_->suspend();

    status_ = Status::WaitingForSuspension;
  }

  if (status_ == Status::WaitingForSuspension) {
    semaphore_.Wait();
    // Here the wasm interpreter is suspended.
  }
}

void Target::OnSuspended(const std::vector<uint64_t>& call_frames) {
  // This function will be called in the isolate thread, when the wasm
  // interpreter gets suspended.
  v8::base::MutexGuard guard(&mutex_);

  // Update the current thread info
  call_frames_ = call_frames;
  cur_signal_ = kSigTrace;  // TODO(paolosev): handle traps

  char tmp[16];
  snprintf(tmp, sizeof(tmp), "QC%x", kThreadId);
  properties_["C"] = tmp;

  bool isWaitingForSuspension = (status_ == Status::WaitingForSuspension);
  status_ = Status::Suspended;

  if (isWaitingForSuspension) {
    // The GdbServer thread was blocked waiting for suspension
    semaphore_.Signal();
  } else if (session_) {
    session_->SignalThreadEvent();
  }
}

void Target::ProcessCommands() {
  // GDB-remote messages are processed in the GDBServer thread.
  // Here the wasm engine should be suspended.

  v8::base::MutexGuard guard(&mutex_);

  if (status_ != Suspended) {
    // Don't process commands if we haven't stopped.
    return;
  }

  // Now we are ready to process commands.
  // Loop through packets until we process a continue packet or a detach.
  Packet recv, reply;
  do {
    if (!session_->GetPacket(&recv)) continue;
    reply.Clear();
    if (ProcessPacket(&recv, &reply)) {
      // If this is a continue type command, break out of this loop.
      break;
    }
    // Otherwise send the response.
    session_->SendPacket(&reply);

    if (detaching_) {
      detaching_ = false;
      session_->Disconnect();
      Resume();
      return;
    }

    if (should_exit_) {
      exit(-9);
    }
  } while (session_->Connected());

  if (session_->Connected()) {
    // Continue if we're still connected.
    Resume();
  }
}

bool Target::AddBreakpoint(uint64_t user_address) {
  return gdb_server_->addBreakpoint(user_address);
}

bool Target::RemoveBreakpoint(uint64_t user_address) {
  return gdb_server_->removeBreakpoint(user_address);
}

void Target::Resume() {
  // Reset the signal value
  cur_signal_ = 0;
}

std::string Target::GetThreadPcsString() const {
  char buff[64];
  snprintf(buff, sizeof(buff), "thread-pcs:%" PRIx64 ";", GetCurrentPc());
  return buff;
}

void Target::SetStopReply(Packet* pktOut) const {
  pktOut->AddRawChar('T');
  pktOut->AddWord8(cur_signal_);
  pktOut->AddString(GetThreadPcsString().c_str());

  // Add 'thread:<tid>;' pair. Note terminating ';' is required.
  pktOut->AddString("thread:");
  pktOut->AddNumberSep(kThreadId, ';');
}

bool Target::ProcessPacket(Packet* pktIn, Packet* pktOut) {
  char cmd;
  int32_t seq = -1;
  ErrDef err = NONE;

  // Clear the outbound message
  pktOut->Clear();

  // Pull out the sequence.
  pktIn->GetSequence(&seq);
  if (seq != -1) pktOut->SetSequence(seq);

  // Find the command
  pktIn->GetRawChar(&cmd);

  switch (cmd) {
    // IN : $?
    // OUT: $Sxx
    case '?':
      SetStopReply(pktOut);
      break;

    // continue
    case 'c':
      status_ = Status::Running;
      gdb_server_->quitMessageLoopOnPause();
      return true;

      // IN : $D
      // OUT: $OK
    case 'D':
      Detach();
      pktOut->AddString("OK");
      return false;

    // IN : $g
    // OUT: $xx...xx
    case 'g': {
      uint64_t pc = GetCurrentPc();
      pktOut->AddBlock(&pc, sizeof(pc));
      break;
    }

      // IN : $Gxx..xx
      // OUT: $OK
    case 'G': {
      // Write general registers - NOT SUPPORTED
      break;
    }

    // IN : $H(c/g)(-1,0,xxxx)
    // OUT: $OK
    case 'H': {
      char type;
      if (!pktIn->GetRawChar(&type)) {
        err = BAD_FORMAT;
        break;
      }

      uint64_t id;
      if (!pktIn->GetNumberSep(&id, 0)) {
        err = BAD_FORMAT;
        break;
      }

      // -1: "any"
      if (id != (uint64_t)-1 && id != 0 && id != kThreadId) {
        err = BAD_ARGS;
        break;
      }

      pktOut->AddString("OK");
      switch (type) {
        case 'g':
        case 'c':
        default:
          // TODO(paolosev) - Only one thread supported for now.
          break;
      }
      break;
    }

      // IN : $k
      // OUT: $OK
    case 'k':
      Kill();
      pktOut->AddString("OK");
      return false;

      // IN : $maaaa,llll
      // OUT: $xx..xx
    case 'm': {
      uint64_t address;
      uint64_t wlen;
      if (!pktIn->GetNumberSep(&address, 0)) {
        err = BAD_FORMAT;
        break;
      }
      if (!pktIn->GetNumberSep(&wlen, 0)) {
        err = BAD_FORMAT;
        break;
      }
      if (wlen > Transport::kBufSize / 2) {
        err = BAD_ARGS;
        break;
      }
      uint32_t length = static_cast<uint32_t>(wlen);
      uint8_t buff[Transport::kBufSize];
      uint32_t read =
          (address >= 1ull << 32)
              ? gdb_server_->getWasmModuleBytes(address, buff, length)
              : gdb_server_->getWasmMemory(address & 0xffffffff, buff, length);
      if (read > 0) {
        pktOut->AddBlock(buff, read);
      } else {
        err = FAILED;
      }
      break;
    }

    // IN : $Maaaa,llll:xx..xx
    // OUT: $OK
    case 'M': {
      // writing to memory not supported for WASM
      break;
    }

      // pN: Read the value of register N.
    case 'p': {
      uint64_t pc = GetCurrentPc();
      pktOut->AddBlock(&pc, sizeof(pc));
    } break;

    case 'q': {
      const char* str = &pktIn->GetPayload()[1];
      std::vector<std::string> toks = StringSplit(str, ":;");

      // If this is a thread query
      if (!strcmp(str, "fThreadInfo") || !strcmp(str, "sThreadInfo")) {
        if (str[0] == 'f') {
          pktOut->AddString("m");
          pktOut->AddNumberSep(kThreadId, 0);
        } else {
          pktOut->AddString("l");
        }
        break;
      }

      if (toks[0] == "WasmCallStack") {  // qWasmCallStack
        std::vector<uint64_t> callStackPCs;
        gdb_server_->getWasmCallStack(&callStackPCs);
        pktOut->AddBlock(
            callStackPCs.data(),
            static_cast<uint32_t>(sizeof(uint64_t) * callStackPCs.size()));
        break;
      } else if (toks[0] == "WasmGlobal") {  // qWasmGlobal:moduleId;index
        if (toks.size() == 3) {
          uint32_t module_id =
              static_cast<uint32_t>(strtol(toks[1].data(), nullptr, 10));
          uint32_t index =
              static_cast<uint32_t>(strtol(toks[2].data(), nullptr, 10));
          uint64_t value = 0;
          if (gdb_server_->getWasmGlobal(module_id, index, &value)) {
            pktOut->AddBlock(&value, sizeof(value));
          } else {
            err = FAILED;
          }
          break;
        }
        err = BAD_FORMAT;
        break;
      } else if (toks[0] ==
                 "WasmLocal") {  // qWasmLocal:moduleId;frameIndex;index
        if (toks.size() == 4) {
          uint32_t module_id =
              static_cast<uint32_t>(strtol(toks[1].data(), nullptr, 10));
          uint32_t frame_index =
              static_cast<uint32_t>(strtol(toks[2].data(), nullptr, 10));
          uint32_t index =
              static_cast<uint32_t>(strtol(toks[3].data(), nullptr, 10));
          uint64_t value = 0;
          if (gdb_server_->getWasmLocal(module_id, frame_index, index,
                                        &value)) {
            pktOut->AddBlock(&value, sizeof(value));
          } else {
            err = FAILED;
          }
          break;
        }
        err = BAD_FORMAT;
        break;
      } else if (toks[0] == "WasmStack") {  // qWasmStack:moduleId;index
        if (toks.size() == 3) {
          uint32_t module_id =
              static_cast<uint32_t>(strtol(toks[1].data(), nullptr, 10));
          uint32_t index =
              static_cast<uint32_t>(strtol(toks[2].data(), nullptr, 10));
          uint64_t value = 0;
          if (gdb_server_->getWasmStackValue(module_id, index, &value)) {
            pktOut->AddBlock(&value, sizeof(value));
          } else {
            err = FAILED;
          }
          break;
        }
        err = BAD_FORMAT;
        break;
      } else if (toks[0] == "WasmMem") {  // qWasmMem:memId;addr;len
        if (toks.size() == 4) {
          // TODO(paolosev) - toks[1] should identify a Memory instance.
          uint64_t address = strtol(toks[2].data(), nullptr, 16);
          uint32_t length =
              static_cast<uint32_t>(strtol(toks[3].data(), nullptr, 16));
          if (length > Transport::kBufSize / 2) {
            err = BAD_ARGS;
            break;
          }
          uint8_t buff[Transport::kBufSize];
          uint32_t read =
              gdb_server_->getWasmMemory(address & 0xffffffff, buff, length);
          if (read > 0) {
            pktOut->AddBlock(buff, read);
          } else {
            err = FAILED;
          }
          break;
        }
        err = BAD_FORMAT;
        break;
      }

      // Check for architecture query
      std::string tmp = "Xfer:libraries:read";
      if (!strncmp(str, tmp.data(), tmp.length())) {
        pktOut->AddString(gdb_server_->getWasmModuleString().c_str());
        break;
      }

      // Check the property cache
      PropertyMap_t::const_iterator itr = properties_.find(toks[0]);
      if (itr != properties_.end()) {
        pktOut->AddString(itr->second.data());
      }
      break;
    }

    case 'Q': {
      std::string tmp(&pktIn->GetPayload()[1]);
      break;
    }

    case 's': {
      // Step
      if (status_ == Status::Suspended) {
        gdb_server_->prepareStep();
        status_ = Status::Running;
        gdb_server_->quitMessageLoopOnPause();
      }
      return true;
    }

    case 'T': {
      // Find out if the thread 'id' is alive.
      uint64_t id;
      if (!pktIn->GetNumberSep(&id, 0)) {
        err = BAD_FORMAT;
        break;
      }

      if (id != kThreadId) {
        err = BAD_ARGS;
        break;
      }

      pktOut->AddString("OK");
      break;
    }

      // Z: Add breakpoint
    case 'Z': {
      uint64_t breakpoint_type;
      uint64_t breakpoint_address;
      uint64_t breakpoint_kind;
      if (!pktIn->GetNumberSep(&breakpoint_type, 0) || breakpoint_type != 0 ||
          !pktIn->GetNumberSep(&breakpoint_address, 0) ||
          !pktIn->GetNumberSep(&breakpoint_kind, 0)) {
        err = BAD_FORMAT;
        break;
      }
      if (!AddBreakpoint(breakpoint_address)) {
        err = FAILED;
        break;
      }
      pktOut->AddString("OK");
      break;
    }

      // z: Remove breakpoint
    case 'z': {
      uint64_t breakpoint_type;
      uint64_t breakpoint_address;
      uint64_t breakpoint_kind;
      if (!pktIn->GetNumberSep(&breakpoint_type, 0) || breakpoint_type != 0 ||
          !pktIn->GetNumberSep(&breakpoint_address, 0) ||
          !pktIn->GetNumberSep(&breakpoint_kind, 0)) {
        err = BAD_FORMAT;
        break;
      }
      if (!RemoveBreakpoint(breakpoint_address)) {
        err = FAILED;
        break;
      }
      pktOut->AddString("OK");
      break;
    }

    default: {
      // If the command is not recognzied, ignore it by sending an empty reply.
      std::string str;
      pktIn->GetString(&str);
      // GdbRemoteLog(LOG_ERROR, "Unknown command: %s\n", pktIn->GetPayload());
      return false;
    }
  }

  // If there is an error, return the error code instead of a payload
  if (err) {
    pktOut->Clear();
    pktOut->AddRawChar('E');
    pktOut->AddWord8(err);
  }
  return false;
}

uint64_t Target::GetCurrentPc() const {
  uint64_t offset = 0;
  if (call_frames_.size() > 0) {
    offset = call_frames_[0];
  }
  return offset;
}

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8
