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

Target::Target(GdbServer* gdb_server)
    : gdb_server_(gdb_server),
      session_(nullptr),
      initial_breakpoints_active_(false),
      cur_signal_(0),
      sig_thread_(1),
      reg_thread_(1),
      step_over_breakpoint_thread_(0),
      all_threads_suspended_(false),
      detaching_(false),
      should_exit_(false),
      process_status_(ProcessStatus::Running),
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

  threads_[1] = new WasmThread();

  initial_breakpoints_active_ = true;

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

WasmThread* Target::GetRegThread() {
  ThreadMap_t::const_iterator itr;

  switch (reg_thread_) {
    // If we want "any" then try the signal'd thread first
    case 0:
    case 0xFFFFFFFF:
      itr = threads_.begin();
      break;

    default:
      itr = threads_.find(reg_thread_);
      break;
  }

  if (itr == threads_.end()) return 0;
  return itr->second;
}

WasmThread* Target::GetThread(uint32_t id) {
  ThreadMap_t::const_iterator itr;
  itr = threads_.find(id);
  if (itr != threads_.end()) return itr->second;
  return nullptr;
}

void Target::Run(Session* session) {
  {
    v8::base::MutexGuard guard(&mutex_);
    session_ = session;
  }

  do {
    WaitForDebugEvent();

    // Lock to prevent anyone else from modifying threads
    // or updating the signal information.
    {
      // v8::base::MutexGuard guard(&mutex_);

      ProcessDebugEvent();
      ProcessCommands();
    }
  } while (session_->Connected());

  {
    v8::base::MutexGuard guard(&mutex_);
    session_ = nullptr;
  }
}

void Target::WaitForDebugEvent() {
  /*
  if (all_threads_suspended_) {
    // If all threads are suspended (which may be left over from a previous
    // connection), we are already ready to handle commands from GDB.
    return;
  }
  // Wait for either:
  //   * an untrusted thread to fault (or single-step)
  //   * an interrupt from GDB
  bool ignore_input_from_gdb =
      step_over_breakpoint_thread_ != 0 || IsInitialBreakpointActive();
  */
  session_->WaitForDebugStubEvent(false /*ignore_input_from_gdb*/);
}

void Target::ProcessDebugEvent() {
  if (all_threads_suspended_) {
    // We are already in a suspended state.
    return;
  }
  //  else if (step_over_breakpoint_thread_ != 0) {
  //  // We are waiting for a specific thread to fault while all other
  //  // threads are suspended.  Note that faulted_thread_count might
  //  // be >1, because multiple threads can fault simultaneously
  //  // before the debug stub gets a chance to suspend all threads.
  //  // This is why we must check the status of a specific thread --
  //  // we cannot call UnqueueAnyFaultedThread() and expect it to
  //  // return step_over_breakpoint_thread_.
  //  Thread* thread = threads_[step_over_breakpoint_thread_];
  //  if (!thread->HasThreadFaulted()) {
  //    // The thread has not faulted.  Nothing to do, so try again.
  //    // Note that we do not respond to input from GDB while in this
  //    // state.
  //    // TODO(mseaborn): We should allow GDB to interrupt execution.
  //    return;
  //  }
  //  // All threads but one are already suspended.  We only need to
  //  // suspend the single thread that we allowed to run.
  //  thread->SuspendThread();
  //  CopyFaultSignalFromAppThread(thread);
  //  cur_signal_ = thread->GetFaultSignal();
  //  thread->UnqueueFaultedThread();
  //  sig_thread_ = step_over_breakpoint_thread_;
  //  reg_thread_ = step_over_breakpoint_thread_;
  //  step_over_breakpoint_thread_ = 0;
  //} else if (nap_->faulted_thread_count != 0) {
  //  // At least one untrusted thread has got an exception.  First we
  //  // need to ensure that all threads are suspended.  Then we can
  //  // retrieve a thread from the set of faulted threads.
  //  SuspendAllThreads();
  //  UnqueueAnyFaultedThread(&sig_thread_, &cur_signal_);
  //  reg_thread_ = sig_thread_;
  //}
  // else {
  // Otherwise look for messages from GDB.  To fix a potential
  // race condition, we don't do this on the first run, because in
  // that case we are waiting for the initial breakpoint to be
  // reached.  We don't want GDB to observe states where the
  // (internal) initial breakpoint is still registered or where
  // the initial thread is suspended in NaClStartThreadInApp()
  // before executing its first untrusted instruction.
  if (/*IsInitialBreakpointActive() ||*/ !session_->IsDataAvailable()) {
    // No input from GDB.  Nothing to do, so try again.
    return;
  }
  // GDB should have tried to interrupt the target.
  // See http://sourceware.org/gdb/current/onlinedocs/gdb/Interrupts.html
  // TODO(eaeltsin): should we verify the interrupt sequence?

  // Indicate we have no current thread.
  // sig_thread_ = 0;
  SuspendAllThreads();
  //}

  // bool initial_breakpoints_active = AreInitialBreakpointsActive();

  if (sig_thread_ != 0) {
    // Reset single stepping.
    threads_[sig_thread_]->SetStep(false);
    RemoveInitialBreakpoints();
  }

  // Next update the current thread info
  char tmp[16];
  snprintf(tmp, sizeof(tmp), "QC%x", sig_thread_);
  properties_["C"] = tmp;

  // if (!initial_breakpoints_active) {
  //  // First time on a connection, we don't send the signal.
  //  // All other times, send the signal that triggered us.
  //  Packet pktOut;
  //  SetStopReply(&pktOut);
  //  session_->SendPacketOnly(&pktOut);
  //}

  all_threads_suspended_ = true;
}

bool Target::GetFirstThreadId(uint32_t* id) {
  threadItr_ = threads_.begin();
  return GetNextThreadId(id);
}

bool Target::GetNextThreadId(uint32_t* id) {
  if (threadItr_ == threads_.end()) return false;

  *id = (*threadItr_).first;
  threadItr_++;

  return true;
}

void Target::ProcessCommands() {
  if (!all_threads_suspended_) {
    // Don't process commands if we haven't stopped all threads.
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

bool Target::AreInitialBreakpointsActive() {
  return initial_breakpoints_active_;
}

void Target::RemoveInitialBreakpoints() {
  if (initial_breakpoints_active_) {
    initial_breakpoints_active_ = false;
  }
}

bool Target::AddBreakpoint(uint64_t user_address) {
  return gdb_server_->addBreakpoint(user_address);
}

bool Target::RemoveBreakpoint(uint64_t user_address) {
  // TODO(paolosev)
  return false;
}

void Target::SuspendAllThreads() {
  if (process_status_ == ProcessStatus::Running) {
    commands_queue_.push(DebugCommand::Pause);
    v8::Isolate* v8_isolate =
        reinterpret_cast<v8::Isolate*>(gdb_server_->isolate());
    v8_isolate->RequestInterrupt(ProcessDebugCommands, this);
    process_status_ = ProcessStatus::WaitingForPause;
  }

  if (process_status_ == ProcessStatus::WaitingForPause) {
    semaphore_.Wait();
    process_status_ = ProcessStatus::Paused;
    cur_signal_ = 17;  // SIGSTOP
    // Packet pktOut;
    // SetStopReply(&pktOut);
    // session_->SendPacketOnly(&pktOut);
  }
}

void Target::ResumeAllThreads() {
  // todo
}

void Target::Resume() {
  // Reset the signal value
  cur_signal_ = 0;

  // TODO(eaeltsin): it might make sense to resume signaled thread before
  // others, though it is not required by GDB docs.
  if (step_over_breakpoint_thread_ == 0) {
    ResumeAllThreads();
  } else {
    // Resume one thread while leaving all others suspended.
    threads_[step_over_breakpoint_thread_]->ResumeThread();
  }

  all_threads_suspended_ = false;
}

// static
void Target::ProcessDebugCommands(v8::Isolate* isolate, void* data) {
  Target* target = reinterpret_cast<Target*>(data);
  target->ProcessDebugCommands();
}

void Target::ProcessDebugCommands() {
  v8::base::MutexGuard guard(&mutex_);

  while (!commands_queue_.empty()) {
    DebugCommand command = commands_queue_.front();
    commands_queue_.pop();
    switch (command) {
      case DebugCommand::Pause:
        gdb_server_->sendPauseRequest();
        break;
      default:
        break;
    }
  }
}

void Target::OnPaused(const std::vector<uint64_t>& call_frames) {
  call_frames_ = call_frames;

  if (process_status_ == ProcessStatus::WaitingForPause) {
    semaphore_.Signal();
  } else if (process_status_ == ProcessStatus::Running) {
    process_status_ = ProcessStatus::Paused;
    cur_signal_ = 17;  // SIGSTOP

    Packet pktOut;
    SetStopReply(&pktOut);
    session_->SendPacketOnly(&pktOut);
  }
}

std::string Target::GetThreadPcsString() const {
  char buff[64];
  snprintf(buff, sizeof(buff), "thread-pcs:%" PRIx64 ";", GetCurrentPc());
  return buff;
}

void Target::SetStopReply(Packet* pktOut) const {
  pktOut->AddRawChar('T');
  pktOut->AddWord8(cur_signal_);
  pktOut->AddString("library:1;");
  pktOut->AddString(GetThreadPcsString().c_str());

  // gdbserver handles GDB interrupt by sending SIGINT to the debuggee, thus
  // GDB interrupt is also a case of a signalled thread.
  // At the moment we handle GDB interrupt differently, without using a signal,
  // so in this case sig_thread_ is 0.
  // This might seem weird to GDB, so at least avoid reporting tid 0.
  if (sig_thread_ != 0) {
    // Add 'thread:<tid>;' pair. Note terminating ';' is required.
    pktOut->AddString("thread:");
    pktOut->AddNumberSep(sig_thread_, ';');
  }
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
      process_status_ = ProcessStatus::Running;
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
      switch (reg_thread_) {
        case 0:
        case 0xFFFFFFFF: {
          uint64_t pc = GetCurrentPc();
          pktOut->AddBlock(&pc, sizeof(pc));
          break;
        }
        default:
          err = BAD_ARGS;
          break;
      }
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
      uint64_t id;

      if (!pktIn->GetRawChar(&type)) {
        err = BAD_FORMAT;
        break;
      }
      if (!pktIn->GetNumberSep(&id, 0)) {
        err = BAD_FORMAT;
        break;
      }

      if (threads_.begin() == threads_.end()) {
        err = BAD_ARGS;
        break;
      }

      // If we are using "any" get the first thread
      if (id == static_cast<uint64_t>(-1)) id = threads_.begin()->first;

      // Verify that we have the thread
      if (threads_.find(static_cast<uint32_t>(id)) == threads_.end()) {
        err = BAD_ARGS;
        break;
      }

      pktOut->AddString("OK");
      switch (type) {
        case 'g':
          reg_thread_ = static_cast<uint32_t>(id);
          break;

        case 'c':
          // 'c' is deprecated in favor of vCont.
        default:
          err = BAD_ARGS;
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
      if (gdb_server_->getWasmMemory(address & 0xffffffff, buff, length)) {
        pktOut->AddBlock(buff, length);
      } else {
        err = FAILED;
      }
      break;
    }

    // IN : $Maaaa,llll:xx..xx
    // OUT: $OK
    case 'M': {
      // not supported for WASM
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
        uint32_t curr;
        bool more = false;
        if (str[0] == 'f') {
          more = GetFirstThreadId(&curr);
        } else {
          more = GetNextThreadId(&curr);
        }

        if (!more) {
          pktOut->AddString("l");
        } else {
          pktOut->AddString("m");
          pktOut->AddNumberSep(curr, 0);
        }
        break;
      }

      if (toks[0] == "WasmCallStack") {
        std::vector<uint64_t> callStackPCs;
        gdb_server_->getWasmCallStack(&callStackPCs);
        pktOut->AddBlock(
            callStackPCs.data(),
            static_cast<uint32_t>(sizeof(uint64_t) * callStackPCs.size()));
        break;
      } else if (toks[0] == "WasmGlobal") {
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
      } else if (toks[0] == "WasmLocal") {
        if (toks.size() == 3) {
          uint32_t module_id =
              static_cast<uint32_t>(strtol(toks[1].data(), nullptr, 10));
          uint32_t index =
              static_cast<uint32_t>(strtol(toks[2].data(), nullptr, 10));
          uint64_t value = 0;
          if (gdb_server_->getWasmLocal(module_id, index, &value)) {
            pktOut->AddBlock(&value, sizeof(value));
          } else {
            err = FAILED;
          }
          break;
        }
        err = BAD_FORMAT;
        break;
      } else if (toks[0] == "WasmStack") {
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
      } else if (toks[0] == "WasmMem") {
        if (toks.size() == 4) {
          // uint32_t module_id = strtol(toks[1].data(), nullptr, 10);
          uint64_t address = strtol(toks[2].data(), nullptr, 16);
          uint32_t length =
              static_cast<uint32_t>(strtol(toks[3].data(), nullptr, 16));
          if (length > Transport::kBufSize / 2) {
            err = BAD_ARGS;
            break;
          }
          uint8_t buff[Transport::kBufSize];
          if (gdb_server_->getWasmMemory(address & 0xffffffff, buff, length)) {
            pktOut->AddBlock(buff, length);
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

      // case 's': {
      // Step
      //// WasmThread* thread = GetRunThread();
      //// if (thread) thread->SetStep(true);
      // if (process_status_ == ProcessStatus::Paused) {
      //  gdb_server_->step();
      //  process_status_ = ProcessStatus::WaitingForPause;
      //}

      // if (process_status_ == ProcessStatus::WaitingForPause) {
      //  semaphore_.Wait();
      //  process_status_ = ProcessStatus::Paused;
      //  pktOut->AddString("S11");
      //} else {
      //  err = FAILED;
      //}
      // return true;
      //}

    case 'T': {
      // Find out if the thread 'id' is alive.
      uint64_t id;
      if (!pktIn->GetNumberSep(&id, 0)) {
        err = BAD_FORMAT;
        break;
      }

      if (GetThread(static_cast<uint32_t>(id)) == nullptr) {
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
