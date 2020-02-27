// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CRDTP_DISPATCH_H_
#define V8_CRDTP_DISPATCH_H_

#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include "export.h"
#include "serializable.h"
#include "span.h"
#include "status.h"

namespace v8_crdtp {
class FrontendChannel;
class ErrorSupport;
namespace cbor {
class CBORTokenizer;
}  // namespace cbor

// =============================================================================
// DispatchResponse - Error status and chaining / fall through
// =============================================================================

// Information returned by command handlers. Usually returned after command
// execution attempts.
class DispatchResponse {
 public:
  enum DispatchStatus {
    kSuccess = 0,
    kError = 1,
    kFallThrough = 2,
  };

  // For historical reasons, these error codes correspond to commonly used
  // XMLRPC codes (e.g. see METHOD_NOT_FOUND in
  // https://github.com/python/cpython/blob/master/Lib/xmlrpc/client.py).
  enum ErrorCode {
    kParseError = -32700,
    kInvalidRequest = -32600,
    kMethodNotFound = -32601,
    kInvalidParams = -32602,
    kInternalError = -32603,
    kServerError = -32000,
  };

  DispatchStatus status() const { return status_; }
  const std::string& errorMessage() const { return error_message_; }
  ErrorCode error_code() const { return error_code_; }
  bool isSuccess() const { return status_ == kSuccess; }

  static DispatchResponse OK();
  static DispatchResponse Error(std::string);
  static DispatchResponse InternalError();
  static DispatchResponse InvalidParams(std::string);
  static DispatchResponse FallThrough();

 private:
  DispatchStatus status_;
  std::string error_message_;
  ErrorCode error_code_;
};

// =============================================================================
// Dispatchable - a shallow parser for CBOR encoded DevTools messages
// =============================================================================

// This parser extracts only the known top-level fields from a CBOR encoded map;
// method, id, sessionId, and params.
class Dispatchable {
 public:
  // This constructor parses the |serialized| message. If successful,
  // |ok()| will yield |true|, and |Method()|, |SessionId()|, |CallId()|,
  // |Params()| can be used to access the extracted contents. Otherwise,
  // |ok()| will yield |false|, and |Code()| and |ErrorMessage| can be
  // used to send a response or notification to the client.
  explicit Dispatchable(span<uint8_t> serialized);

  // The serialized message that we just parsed.
  span<uint8_t> Serialized() const { return serialized_; }

  // Yields true if parsing was successful.
  bool ok() const;
  // If !ok(), returns an error code to be sent to the client.
  DispatchResponse::ErrorCode ErrorCode() const;
  // If !ok(), returns an error message to be sent to the client.
  std::string ErrorMessage() const;

  // Top level field: the command to be executed, fully qualified by
  // domain. E.g. "Page.createIsolatedWorld".
  span<uint8_t> Method() const { return method_; }
  // Used to identify protocol connections attached to a specific
  // target. See Target.attachToTarget, Target.setAutoAttach.
  span<uint8_t> SessionId() const { return session_id_; }
  // The call id, a sequence number that's used in responses to indicate
  // the request to which the response belongs.
  int32_t CallId() const { return call_id_; }
  bool HasCallId() const { return has_call_id_; }
  // The payload of the request in CBOR format. The |Dispatchable| parser does
  // not parse into this; it only provides access to its raw contents here.
  span<uint8_t> Params() const { return params_; }

 private:
  void MaybeParseString8Field(cbor::CBORTokenizer* tokenizer,
                              span<uint8_t>* value,
                              const char* error_if_invalid);
  void MaybeParseInt32Field(cbor::CBORTokenizer* tokenizer,
                            bool* has_value,
                            int32_t* value,
                            const char* error_if_invalid);
  void MaybeParseEnvelopeField(cbor::CBORTokenizer* tokenizer,
                               span<uint8_t>* value);

  span<uint8_t> serialized_;

  const char* error_ = nullptr;
  Status status_;

  bool has_call_id_ = false;
  int32_t call_id_;
  span<uint8_t> method_;
  span<uint8_t> params_;
  span<uint8_t> session_id_;
};

// =============================================================================
// Helpers for creating protocol cresponses and notifications.
// =============================================================================

// The resulting notifications can be sent to a protocol client,
// usually via a FrontendChannel (see frontend_channel.h).

std::unique_ptr<Serializable> CreateErrorResponse(
    int callId,
    DispatchResponse::ErrorCode code,
    std::string errorMessage,
    const ErrorSupport* errors = nullptr);

std::unique_ptr<Serializable> CreateErrorNotification(
    DispatchResponse::ErrorCode code,
    std::string errorMessage);

std::unique_ptr<Serializable> CreateResponse(
    int callId,
    std::unique_ptr<Serializable> params);

std::unique_ptr<Serializable> CreateNotification(
    const char* method,
    std::unique_ptr<Serializable> params = nullptr);

// =============================================================================
// DomainDispatcher - Dispatching betwen protocol methods within a domain.
// =============================================================================

// This class is subclassed by |DomainDispatcherImpl|, which we generate per
// DevTools domain. It contains routines called from the generated code,
// e.g. ::MaybeReportInvalidParams, which are optimized for small code size.
// The most important method is ::Dispatch, which implements method dispatch
// by command name lookup.
class DomainDispatcher {
 public:
  class WeakPtr {
   public:
    explicit WeakPtr(DomainDispatcher*);
    ~WeakPtr();
    DomainDispatcher* get() { return dispatcher_; }
    void dispose() { dispatcher_ = nullptr; }

   private:
    DomainDispatcher* dispatcher_;
  };

  class Callback {
   protected:
    Callback(std::unique_ptr<WeakPtr> backend_impl,
             int call_id,
             span<uint8_t> method,
             span<uint8_t> message);

   public:
    virtual ~Callback();
    void dispose();

   protected:
    void sendIfActive(std::unique_ptr<Serializable> partialMessage,
                      const DispatchResponse& response);
    void fallThroughIfActive();

   private:
    std::unique_ptr<WeakPtr> backend_impl_;
    int call_id_;
    span<uint8_t> method_;
    std::vector<uint8_t> message_;
  };

  explicit DomainDispatcher(FrontendChannel*);
  virtual ~DomainDispatcher();

  // Given a |command_name| without domain qualification, looks up the
  // corresponding method. If the method is not found, returns nullptr.
  // Otherwise, Returns a closure that will parse the provided
  // Dispatchable.params() to a protocol object and execute the
  // apprpropriate method. If the parsing fails it will issue an
  // error response on the frontend channel, otherwise it will execute the
  // command.
  virtual std::function<void(const Dispatchable&)> Dispatch(
      span<uint8_t> command_name) = 0;

  // Sends a response to the client via the channel.
  void sendResponse(int call_id,
                    const DispatchResponse&,
                    std::unique_ptr<Serializable> result = nullptr);

  // Returns true if |errors| contains errors *and* reports these errors
  // as a response on the frontend channel. Called from generated code,
  // optimized for code size of the callee.
  bool MaybeReportInvalidParams(const Dispatchable& dispatchable,
                                const ErrorSupport& errors);

  FrontendChannel* channel() { return frontend_channel_; }

  void clearFrontend();

  std::unique_ptr<WeakPtr> weakPtr();

 private:
  FrontendChannel* frontend_channel_;
  std::unordered_set<WeakPtr*> weak_ptrs_;
};

// =============================================================================
// UberDispatcher - dispatches between domains (backends).
// =============================================================================
class UberDispatcher {
 public:
  // Return type for ::Dispatch.
  class DispatchResult {
   public:
    DispatchResult(bool method_found, std::function<void()> runnable);

    // Indicates whether the method was found, that is, it could be dispatched
    // to a backend registered with this dispatcher.
    bool MethodFound() const { return method_found_; }

    // Runs the dispatched result. This will send the appropriate error
    // responses if the method wasn't found or if something went wrong during
    // parameter parsing.
    void Run();

   private:
    bool method_found_;
    std::function<void()> runnable_;
  };

  // |frontend_hannel| can't be nullptr.
  explicit UberDispatcher(FrontendChannel* frontend_channel);
  virtual ~UberDispatcher();

  // Dispatches the provided |dispatchable| considering all redirects and domain
  // handlers registered with this uber dispatcher. Also see |DispatchResult|.
  // |dispatchable.ok()| must hold - callers must check this separately and
  // deal with errors.
  DispatchResult Dispatch(const Dispatchable& dispatchable) const;

  // Invoked from generated code for wiring domain backends; that is,
  // connecting domain handlers to an uber dispatcher.
  // See <domain-namespace>::Dispatcher::Wire(UberDispatcher*,Backend*).
  FrontendChannel* channel() const {
    assert(frontend_channel_);
    return frontend_channel_;
  }

  // Invoked from generated code for wiring domain backends; that is,
  // connecting domain handlers to an uber dispatcher.
  // See <domain-namespace>::Dispatcher::Wire(UberDispatcher*,Backend*).
  void WireBackend(span<uint8_t> domain,
                   const std::vector<std::pair<span<uint8_t>, span<uint8_t>>>&,
                   std::unique_ptr<DomainDispatcher> dispatcher);

 private:
  DomainDispatcher* findDispatcher(span<uint8_t> method);
  FrontendChannel* const frontend_channel_;
  std::vector<std::pair<span<uint8_t>, span<uint8_t>>> redirects_;
  std::vector<std::pair<span<uint8_t>, std::unique_ptr<DomainDispatcher>>>
      dispatchers_;
};
}  // namespace v8_crdtp

#endif  // V8_CRDTP_DISPATCH_H_
