// Copyright 2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/d8.h"

namespace v8 {

// A utility class that does a non-hanging waitpid on the child process if we
// bail out of the System() function early.  If you don't ever do a waitpid on
// a subprocess then it turns into one of those annoying 'zombie processes'.
class ZombieProtector {
 public:
  explicit ZombieProtector(int pid): pid_(pid) { }
  ~ZombieProtector() { if (pid_ != 0) waitpid(pid_, NULL, 0); }
  void ChildIsDeadNow() { pid_ = 0; }
 private:
  int pid_;
};


// A utility class that closes a file descriptor when it goes out of scope.
class OpenFDCloser {
 public:
  explicit OpenFDCloser(int fd): fd_(fd) { }
  ~OpenFDCloser() { close(fd_); }
 private:
  int fd_;
};


// A utility class that takes the array of command arguments and puts then in an
// array of new[]ed UTF-8 C strings.  Deallocates them again when it goes out of
// scope.
class ExecArgs {
 public:
  ExecArgs() {
    exec_args_[0] = NULL;
  }
  bool Init(Isolate* isolate, Local<Value> arg0, Local<Array> command_args) {
    String::Utf8Value prog(arg0);
    if (*prog == NULL) {
      const char* message =
          "os.system(): String conversion of program name failed";
      isolate->ThrowException(
          String::NewFromUtf8(isolate, message, NewStringType::kNormal)
              .ToLocalChecked());
      return false;
    }
    int len = prog.length() + 3;
    char* c_arg = new char[len];
    snprintf(c_arg, len, "%s", *prog);
    exec_args_[0] = c_arg;
    int i = 1;
    for (unsigned j = 0; j < command_args->Length(); i++, j++) {
      Local<Value> arg(
          command_args->Get(isolate->GetCurrentContext(),
                            Integer::New(isolate, j)).ToLocalChecked());
      String::Utf8Value utf8_arg(arg);
      if (*utf8_arg == NULL) {
        exec_args_[i] = NULL;  // Consistent state for destructor.
        const char* message =
            "os.system(): String conversion of argument failed.";
        isolate->ThrowException(
            String::NewFromUtf8(isolate, message, NewStringType::kNormal)
                .ToLocalChecked());
        return false;
      }
      int len = utf8_arg.length() + 1;
      char* c_arg = new char[len];
      snprintf(c_arg, len, "%s", *utf8_arg);
      exec_args_[i] = c_arg;
    }
    exec_args_[i] = NULL;
    return true;
  }
  ~ExecArgs() {
    for (unsigned i = 0; i < kMaxArgs; i++) {
      if (exec_args_[i] == NULL) {
        return;
      }
      delete [] exec_args_[i];
      exec_args_[i] = 0;
    }
  }
  static const unsigned kMaxArgs = 1000;
  char* const* arg_array() const { return exec_args_; }
  const char* arg0() const { return exec_args_[0]; }

 private:
  char* exec_args_[kMaxArgs + 1];
};

// Modern Linux has the waitid call, which is like waitpid, but more useful
// if you want a timeout.  If we don't have waitid we can't limit the time
// waiting for the process to exit without losing the information about
// whether it exited normally.  In the common case this doesn't matter because
// we don't get here before the child has closed stdout and most programs don't
// do that before they exit.
//
// We're disabling usage of waitid in Mac OS X because it doens't work for us:
// a parent process hangs on waiting while a child process is already a zombie.
// See http://code.google.com/p/v8/issues/detail?id=401.
#if defined(WNOWAIT) && !defined(ANDROID) && !defined(__APPLE__) \
    && !defined(__NetBSD__)
#if !defined(__FreeBSD__)
#define HAS_WAITID 1
#endif
#endif

void Shell::ChangeDirectory(const v8::FunctionCallbackInfo<v8::Value>& args) {
  if (args.Length() != 1) {
    const char* message = "chdir() takes one argument";
    args.GetIsolate()->ThrowException(
        String::NewFromUtf8(args.GetIsolate(), message, NewStringType::kNormal)
            .ToLocalChecked());
    return;
  }
  String::Utf8Value directory(args[0]);
  if (*directory == NULL) {
    const char* message = "os.chdir(): String conversion of argument failed.";
    args.GetIsolate()->ThrowException(
        String::NewFromUtf8(args.GetIsolate(), message, NewStringType::kNormal)
            .ToLocalChecked());
    return;
  }
  if (chdir(*directory) != 0) {
    args.GetIsolate()->ThrowException(
        String::NewFromUtf8(args.GetIsolate(), strerror(errno),
                            NewStringType::kNormal).ToLocalChecked());
    return;
  }
}


void Shell::SetUMask(const v8::FunctionCallbackInfo<v8::Value>& args) {
  if (args.Length() != 1) {
    const char* message = "umask() takes one argument";
    args.GetIsolate()->ThrowException(
        String::NewFromUtf8(args.GetIsolate(), message, NewStringType::kNormal)
            .ToLocalChecked());
    return;
  }
  if (args[0]->IsNumber()) {
    int previous = umask(
        args[0]->Int32Value(args.GetIsolate()->GetCurrentContext()).FromJust());
    args.GetReturnValue().Set(previous);
    return;
  } else {
    const char* message = "umask() argument must be numeric";
    args.GetIsolate()->ThrowException(
        String::NewFromUtf8(args.GetIsolate(), message, NewStringType::kNormal)
            .ToLocalChecked());
    return;
  }
}


static bool CheckItsADirectory(Isolate* isolate, char* directory) {
  struct stat stat_buf;
  int stat_result = stat(directory, &stat_buf);
  if (stat_result != 0) {
    isolate->ThrowException(
        String::NewFromUtf8(isolate, strerror(errno), NewStringType::kNormal)
            .ToLocalChecked());
    return false;
  }
  if ((stat_buf.st_mode & S_IFDIR) != 0) return true;
  isolate->ThrowException(
      String::NewFromUtf8(isolate, strerror(EEXIST), NewStringType::kNormal)
          .ToLocalChecked());
  return false;
}


// Returns true for success.  Creates intermediate directories as needed.  No
// error if the directory exists already.
static bool mkdirp(Isolate* isolate, char* directory, mode_t mask) {
  int result = mkdir(directory, mask);
  if (result == 0) return true;
  if (errno == EEXIST) {
    return CheckItsADirectory(isolate, directory);
  } else if (errno == ENOENT) {  // Intermediate path element is missing.
    char* last_slash = strrchr(directory, '/');
    if (last_slash == NULL) {
      isolate->ThrowException(
          String::NewFromUtf8(isolate, strerror(errno), NewStringType::kNormal)
              .ToLocalChecked());
      return false;
    }
    *last_slash = 0;
    if (!mkdirp(isolate, directory, mask)) return false;
    *last_slash = '/';
    result = mkdir(directory, mask);
    if (result == 0) return true;
    if (errno == EEXIST) {
      return CheckItsADirectory(isolate, directory);
    }
    isolate->ThrowException(
        String::NewFromUtf8(isolate, strerror(errno), NewStringType::kNormal)
            .ToLocalChecked());
    return false;
  } else {
    isolate->ThrowException(
        String::NewFromUtf8(isolate, strerror(errno), NewStringType::kNormal)
            .ToLocalChecked());
    return false;
  }
}


void Shell::MakeDirectory(const v8::FunctionCallbackInfo<v8::Value>& args) {
  mode_t mask = 0777;
  if (args.Length() == 2) {
    if (args[1]->IsNumber()) {
      mask = args[1]
                 ->Int32Value(args.GetIsolate()->GetCurrentContext())
                 .FromJust();
    } else {
      const char* message = "mkdirp() second argument must be numeric";
      args.GetIsolate()->ThrowException(
          String::NewFromUtf8(args.GetIsolate(), message,
                              NewStringType::kNormal).ToLocalChecked());
      return;
    }
  } else if (args.Length() != 1) {
    const char* message = "mkdirp() takes one or two arguments";
    args.GetIsolate()->ThrowException(
        String::NewFromUtf8(args.GetIsolate(), message, NewStringType::kNormal)
            .ToLocalChecked());
    return;
  }
  String::Utf8Value directory(args[0]);
  if (*directory == NULL) {
    const char* message = "os.mkdirp(): String conversion of argument failed.";
    args.GetIsolate()->ThrowException(
        String::NewFromUtf8(args.GetIsolate(), message, NewStringType::kNormal)
            .ToLocalChecked());
    return;
  }
  mkdirp(args.GetIsolate(), *directory, mask);
}


void Shell::RemoveDirectory(const v8::FunctionCallbackInfo<v8::Value>& args) {
  if (args.Length() != 1) {
    const char* message = "rmdir() takes one or two arguments";
    args.GetIsolate()->ThrowException(
        String::NewFromUtf8(args.GetIsolate(), message, NewStringType::kNormal)
            .ToLocalChecked());
    return;
  }
  String::Utf8Value directory(args[0]);
  if (*directory == NULL) {
    const char* message = "os.rmdir(): String conversion of argument failed.";
    args.GetIsolate()->ThrowException(
        String::NewFromUtf8(args.GetIsolate(), message, NewStringType::kNormal)
            .ToLocalChecked());
    return;
  }
  rmdir(*directory);
}


void Shell::SetEnvironment(const v8::FunctionCallbackInfo<v8::Value>& args) {
  if (args.Length() != 2) {
    const char* message = "setenv() takes two arguments";
    args.GetIsolate()->ThrowException(
        String::NewFromUtf8(args.GetIsolate(), message, NewStringType::kNormal)
            .ToLocalChecked());
    return;
  }
  String::Utf8Value var(args[0]);
  String::Utf8Value value(args[1]);
  if (*var == NULL) {
    const char* message =
        "os.setenv(): String conversion of variable name failed.";
    args.GetIsolate()->ThrowException(
        String::NewFromUtf8(args.GetIsolate(), message, NewStringType::kNormal)
            .ToLocalChecked());
    return;
  }
  if (*value == NULL) {
    const char* message =
        "os.setenv(): String conversion of variable contents failed.";
    args.GetIsolate()->ThrowException(
        String::NewFromUtf8(args.GetIsolate(), message, NewStringType::kNormal)
            .ToLocalChecked());
    return;
  }
  setenv(*var, *value, 1);
}


void Shell::UnsetEnvironment(const v8::FunctionCallbackInfo<v8::Value>& args) {
  if (args.Length() != 1) {
    const char* message = "unsetenv() takes one argument";
    args.GetIsolate()->ThrowException(
        String::NewFromUtf8(args.GetIsolate(), message, NewStringType::kNormal)
            .ToLocalChecked());
    return;
  }
  String::Utf8Value var(args[0]);
  if (*var == NULL) {
    const char* message =
        "os.setenv(): String conversion of variable name failed.";
    args.GetIsolate()->ThrowException(
        String::NewFromUtf8(args.GetIsolate(), message, NewStringType::kNormal)
            .ToLocalChecked());
    return;
  }
  unsetenv(*var);
}


void Shell::AddOSMethods(Isolate* isolate, Local<ObjectTemplate> os_templ) {
  os_templ->Set(String::NewFromUtf8(isolate, "chdir", NewStringType::kNormal)
                    .ToLocalChecked(),
                FunctionTemplate::New(isolate, ChangeDirectory));
  os_templ->Set(String::NewFromUtf8(isolate, "setenv", NewStringType::kNormal)
                    .ToLocalChecked(),
                FunctionTemplate::New(isolate, SetEnvironment));
  os_templ->Set(String::NewFromUtf8(isolate, "unsetenv", NewStringType::kNormal)
                    .ToLocalChecked(),
                FunctionTemplate::New(isolate, UnsetEnvironment));
  os_templ->Set(String::NewFromUtf8(isolate, "umask", NewStringType::kNormal)
                    .ToLocalChecked(),
                FunctionTemplate::New(isolate, SetUMask));
  os_templ->Set(String::NewFromUtf8(isolate, "mkdirp", NewStringType::kNormal)
                    .ToLocalChecked(),
                FunctionTemplate::New(isolate, MakeDirectory));
  os_templ->Set(String::NewFromUtf8(isolate, "rmdir", NewStringType::kNormal)
                    .ToLocalChecked(),
                FunctionTemplate::New(isolate, RemoveDirectory));
}

void Shell::Exit(int exit_code) {
  // Use _exit instead of exit to avoid races between isolate
  // threads and static destructors.
  fflush(stdout);
  fflush(stderr);
  _exit(exit_code);
}

}  // namespace v8
