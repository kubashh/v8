// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dispatch.h"

#include <cassert>
#include "cbor.h"
#include "error_support.h"
#include "frontend_channel.h"

namespace v8_crdtp {
// =============================================================================
// DispatchResponse - Error status and chaining / fall through
// =============================================================================

// static
DispatchResponse DispatchResponse::OK() {
  DispatchResponse result;
  result.status_ = kSuccess;
  result.error_code_ = kParseError;
  return result;
}

// static
DispatchResponse DispatchResponse::Error(std::string error) {
  DispatchResponse result;
  result.status_ = kError;
  result.error_code_ = kServerError;
  result.error_message_ = std::move(error);
  return result;
}

// static
DispatchResponse DispatchResponse::InternalError() {
  DispatchResponse result;
  result.status_ = kError;
  result.error_code_ = kInternalError;
  result.error_message_ = "Internal error";
  return result;
}

// static
DispatchResponse DispatchResponse::InvalidParams(std::string error) {
  DispatchResponse result;
  result.status_ = kError;
  result.error_code_ = kInvalidParams;
  result.error_message_ = std::move(error);
  return result;
}

// static
DispatchResponse DispatchResponse::FallThrough() {
  DispatchResponse result;
  result.status_ = kFallThrough;
  result.error_code_ = kParseError;
  return result;
}

// =============================================================================
// Dispatchable - a shallow parser for CBOR encoded DevTools messages
// =============================================================================
namespace {
constexpr size_t kEncodedEnvelopeHeaderSize = 1 + 1 + sizeof(uint32_t);
const char kMessageObjectError[] = "Message must be an object";
const char kMessageIdError[] = "Message must have integer 'id' property";
const char kMessageMethodError[] = "Message must have string 'method' property";
const char kMessageSessionIdError[] = "sessionId must be 7-bit US-ASCII string";
}  // namespace
Dispatchable::Dispatchable(span<uint8_t> serialized) : serialized_(serialized) {
  Status s = cbor::CheckCBORMessage(serialized);
  if (!s.ok()) {
    error_ = kMessageObjectError;
    return;
  }
  cbor::CBORTokenizer tokenizer(serialized);
  if (tokenizer.TokenTag() == cbor::CBORTokenTag::ERROR_VALUE) {
    status_ = tokenizer.Status();
    return;
  }

  // We checked for the envelope start byte above, so the tokenizer
  // must agree here, since it's not an error.
  assert(tokenizer.TokenTag() == cbor::CBORTokenTag::ENVELOPE);

  // Before we enter the envelope, we save the position that we
  // expect to see after we're done parsing the envelope contents.
  // This way we can compare and produce an error if the contents
  // didn't fit exactly into the envelope length.
  size_t pos_past_envelope = tokenizer.Status().pos +
                             kEncodedEnvelopeHeaderSize +
                             tokenizer.GetEnvelopeContents().size();
  tokenizer.EnterEnvelope();
  if (tokenizer.TokenTag() == cbor::CBORTokenTag::ERROR_VALUE) {
    status_ = tokenizer.Status();
    return;
  }
  if (tokenizer.TokenTag() != cbor::CBORTokenTag::MAP_START) {
    error_ = kMessageObjectError;
    return;
  }
  assert(tokenizer.TokenTag() == cbor::CBORTokenTag::MAP_START);
  tokenizer.Next();  // Now we should be pointed at the map key.
  while (tokenizer.TokenTag() != cbor::CBORTokenTag::STOP) {
    if (tokenizer.TokenTag() == cbor::CBORTokenTag::DONE) {
      status_ =
          Status{Error::CBOR_UNEXPECTED_EOF_IN_MAP, tokenizer.Status().pos};
    } else if (tokenizer.TokenTag() == cbor::CBORTokenTag::ERROR_VALUE) {
      status_ = tokenizer.Status();
    } else if (tokenizer.TokenTag() != cbor::CBORTokenTag::STRING8) {
      // We require the top-level keys to be UTF8 (US-ASCII in practice).
      status_ = Status{Error::CBOR_INVALID_MAP_KEY, tokenizer.Status().pos};
    } else if (SpanEquals(SpanFrom("sessionId"), tokenizer.GetString8())) {
      MaybeParseString8Field(&tokenizer, &session_id_, kMessageSessionIdError);
    } else if (SpanEquals(SpanFrom("id"), tokenizer.GetString8())) {
      MaybeParseInt32Field(&tokenizer, &has_call_id_, &call_id_,
                           kMessageIdError);
    } else if (SpanEquals(SpanFrom("method"), tokenizer.GetString8())) {
      MaybeParseString8Field(&tokenizer, &method_, kMessageMethodError);
    } else if (SpanEquals(SpanFrom("params"), tokenizer.GetString8())) {
      // This is only a shallow parse - we extract the raw contents of the field
      // but do not descend farther into it.
      MaybeParseEnvelopeField(&tokenizer, &params_);
    }
    if (!ok())
      return;
  }
  tokenizer.Next();
  if (!has_call_id_) {
    error_ = kMessageIdError;
    return;
  }
  if (method_.empty()) {
    error_ = kMessageMethodError;
    return;
  }
  // The contents of the envelope parsed OK, now check that we're at
  // the expected position.
  if (pos_past_envelope != tokenizer.Status().pos) {
    status_ = Status{Error::CBOR_ENVELOPE_CONTENTS_LENGTH_MISMATCH,
                     tokenizer.Status().pos};
    return;
  }
  if (tokenizer.TokenTag() != cbor::CBORTokenTag::DONE) {
    status_ = Status{Error::CBOR_TRAILING_JUNK, tokenizer.Status().pos};
    return;
  }
}

bool Dispatchable::ok() const {
  return status_.ok() && error_ == nullptr;
}

DispatchResponse::ErrorCode Dispatchable::ErrorCode() const {
  return error_ ? DispatchResponse::kInvalidRequest
                : DispatchResponse::kParseError;
}

std::string Dispatchable::ErrorMessage() const {
  return error_ ? error_ : status_.ToASCIIString();
}

void Dispatchable::MaybeParseString8Field(cbor::CBORTokenizer* tokenizer,
                                          span<uint8_t>* value,
                                          const char* error_if_invalid) {
  if (!value->empty()) {
    status_ = Status{Error::CBOR_DUPLICATE_MAP_KEY, tokenizer->Status().pos};
    return;
  }
  tokenizer->Next();
  if (tokenizer->TokenTag() != cbor::CBORTokenTag::STRING8) {
    error_ = error_if_invalid;
    return;
  }
  *value = tokenizer->GetString8();
  tokenizer->Next();
}

void Dispatchable::MaybeParseInt32Field(cbor::CBORTokenizer* tokenizer,
                                        bool* has_value,
                                        int32_t* value,
                                        const char* error_if_invalid) {
  if (*has_value) {
    status_ = Status{Error::CBOR_DUPLICATE_MAP_KEY, tokenizer->Status().pos};
    return;
  }
  tokenizer->Next();
  if (tokenizer->TokenTag() != cbor::CBORTokenTag::INT32) {
    error_ = error_if_invalid;
    return;
  }
  *value = tokenizer->GetInt32();
  *has_value = true;
  tokenizer->Next();
}

void Dispatchable::MaybeParseEnvelopeField(cbor::CBORTokenizer* tokenizer,
                                           span<uint8_t>* value) {
  if (!value->empty()) {
    status_ = Status{Error::CBOR_DUPLICATE_MAP_KEY, tokenizer->Status().pos};
    return;
  }
  tokenizer->Next();
  if (tokenizer->TokenTag() != cbor::CBORTokenTag::ENVELOPE) {
    status_ =
        Status{Error::BINDINGS_ENVELOPE_EXPECTED, tokenizer->Status().pos};
    return;
  }
  *value = tokenizer->GetEnvelope();
  tokenizer->Next();
}

namespace {
class ProtocolError : public Serializable {
 public:
  ProtocolError(DispatchResponse::ErrorCode code, std::string error_message)
      : code_(code), error_message_(std::move(error_message)) {}

  void AppendSerialized(std::vector<uint8_t>* out) const override {
    Status status;
    std::unique_ptr<ParserHandler> encoder = cbor::NewCBOREncoder(out, &status);
    encoder->HandleMapBegin();
    if (has_call_id_) {
      encoder->HandleString8(SpanFrom("id"));
      encoder->HandleInt32(call_id_);
    }
    encoder->HandleString8(SpanFrom("error"));
    encoder->HandleMapBegin();
    encoder->HandleString8(SpanFrom("code"));
    encoder->HandleInt32(code_);
    encoder->HandleString8(SpanFrom("message"));
    encoder->HandleString8(SpanFrom(error_message_));
    if (!data_.empty()) {
      encoder->HandleString8(SpanFrom("data"));
      encoder->HandleString8(SpanFrom(data_));
    }
    encoder->HandleMapEnd();
    encoder->HandleMapEnd();
    assert(status.ok());
  }

  void SetCallId(int call_id) {
    has_call_id_ = true;
    call_id_ = call_id;
  }
  void SetData(std::string data) { data_ = std::move(data); }

 private:
  DispatchResponse::ErrorCode code_;
  std::string error_message_;
  std::string data_;
  int call_id_ = 0;
  bool has_call_id_ = false;
};
}  // namespace

// =============================================================================
// Helpers for creating protocol cresponses and notifications.
// =============================================================================

std::unique_ptr<Serializable> CreateErrorResponse(
    int call_id,
    DispatchResponse::ErrorCode code,
    std::string errorMessage,
    const ErrorSupport* errors) {
  auto protocolError =
      std::make_unique<ProtocolError>(code, std::move(errorMessage));
  protocolError->SetCallId(call_id);
  if (errors && !errors->Errors().empty()) {
    protocolError->SetData(
        std::string(errors->Errors().begin(), errors->Errors().end()));
  }
  return protocolError;
}

std::unique_ptr<Serializable> CreateErrorNotification(
    DispatchResponse::ErrorCode code,
    std::string errorMessage) {
  return std::make_unique<ProtocolError>(code, errorMessage);
}

namespace {
class InternalResponse : public Serializable {
 public:
  InternalResponse(int call_id,
                   const char* method,
                   std::unique_ptr<Serializable> params)
      : call_id_(call_id), method_(method), params_(std::move(params)) {}

  void AppendSerialized(std::vector<uint8_t>* out) const override {
    Status status;
    std::unique_ptr<ParserHandler> encoder = cbor::NewCBOREncoder(out, &status);
    encoder->HandleMapBegin();
    if (method_) {
      encoder->HandleString8(SpanFrom("method"));
      encoder->HandleString8(SpanFrom(method_));
      encoder->HandleString8(SpanFrom("params"));
    } else {
      encoder->HandleString8(SpanFrom("id"));
      encoder->HandleInt32(call_id_);
      encoder->HandleString8(SpanFrom("result"));
    }
    if (params_) {
      params_->AppendSerialized(out);
    } else {
      encoder->HandleMapBegin();
      encoder->HandleMapEnd();
    }
    encoder->HandleMapEnd();
    assert(status.ok());
  }

 private:
  int call_id_;
  const char* method_ = nullptr;
  std::unique_ptr<Serializable> params_;
};
}  // namespace

std::unique_ptr<Serializable> CreateResponse(
    int call_id,
    std::unique_ptr<Serializable> params) {
  return std::unique_ptr<Serializable>(
      new InternalResponse(call_id, nullptr, std::move(params)));
}

std::unique_ptr<Serializable> CreateNotification(
    const char* method,
    std::unique_ptr<Serializable> params) {
  return std::unique_ptr<Serializable>(
      new InternalResponse(0, method, std::move(params)));
}

// =============================================================================
// DomainDispatcher - Dispatching betwen protocol methods within a domain.
// =============================================================================
DomainDispatcher::WeakPtr::WeakPtr(DomainDispatcher* dispatcher)
    : dispatcher_(dispatcher) {}

DomainDispatcher::WeakPtr::~WeakPtr() {
  if (dispatcher_)
    dispatcher_->weak_ptrs_.erase(this);
}

DomainDispatcher::Callback::Callback(
    std::unique_ptr<DomainDispatcher::WeakPtr> backend_impl,
    int call_id,
    span<uint8_t> method,
    span<uint8_t> message)
    : backend_impl_(std::move(backend_impl)),
      call_id_(call_id),
      method_(method),
      message_(message.begin(), message.end()) {}

DomainDispatcher::Callback::~Callback() = default;

void DomainDispatcher::Callback::dispose() {
  backend_impl_ = nullptr;
}

void DomainDispatcher::Callback::sendIfActive(
    std::unique_ptr<Serializable> partialMessage,
    const DispatchResponse& response) {
  if (!backend_impl_ || !backend_impl_->get())
    return;
  backend_impl_->get()->sendResponse(call_id_, response,
                                     std::move(partialMessage));
  backend_impl_ = nullptr;
}

void DomainDispatcher::Callback::fallThroughIfActive() {
  if (!backend_impl_ || !backend_impl_->get())
    return;
  backend_impl_->get()->channel()->FallThrough(call_id_, method_,
                                               SpanFrom(message_));
  backend_impl_ = nullptr;
}

DomainDispatcher::DomainDispatcher(FrontendChannel* frontendChannel)
    : frontend_channel_(frontendChannel) {}

DomainDispatcher::~DomainDispatcher() {
  clearFrontend();
}

void DomainDispatcher::sendResponse(int call_id,
                                    const DispatchResponse& response,
                                    std::unique_ptr<Serializable> result) {
  if (!frontend_channel_)
    return;
  std::unique_ptr<Serializable> serializable;
  if (response.status() == DispatchResponse::kError) {
    serializable = CreateErrorResponse(call_id, response.error_code(),
                                       response.errorMessage());
  } else {
    serializable = CreateResponse(call_id, std::move(result));
  }
  frontend_channel_->SendProtocolResponse(call_id, std::move(serializable));
}

bool DomainDispatcher::MaybeReportInvalidParams(
    const Dispatchable& dispatchable,
    const ErrorSupport& errors) {
  if (errors.Errors().empty())
    return false;
  if (frontend_channel_) {
    frontend_channel_->SendProtocolResponse(
        dispatchable.CallId(),
        CreateErrorResponse(dispatchable.CallId(),
                            DispatchResponse::kInvalidParams,
                            "Invalid parameters", &errors));
  }
  return true;
}

void DomainDispatcher::clearFrontend() {
  frontend_channel_ = nullptr;
  for (auto& weak : weak_ptrs_)
    weak->dispose();
  weak_ptrs_.clear();
}

std::unique_ptr<DomainDispatcher::WeakPtr> DomainDispatcher::weakPtr() {
  auto weak = std::make_unique<DomainDispatcher::WeakPtr>(this);
  weak_ptrs_.insert(weak.get());
  return weak;
}

// =============================================================================
// UberDispatcher - dispatches between domains (backends).
// =============================================================================
UberDispatcher::DispatchResult::DispatchResult(bool method_found,
                                               std::function<void()> runnable)
    : method_found_(method_found), runnable_(runnable) {}

void UberDispatcher::DispatchResult::Run() {
  if (!runnable_)
    return;
  runnable_();
  runnable_ = nullptr;
}

UberDispatcher::UberDispatcher(FrontendChannel* frontend_channel)
    : frontend_channel_(frontend_channel) {
  assert(frontend_channel);
}

UberDispatcher::~UberDispatcher() = default;

constexpr size_t kNotFound = std::numeric_limits<size_t>::max();

namespace {
size_t DotIdx(span<uint8_t> method) {
  const void* p = memchr(method.data(), '.', method.size());
  return p ? reinterpret_cast<const uint8_t*>(p) - method.data() : kNotFound;
}
}  // namespace

UberDispatcher::DispatchResult UberDispatcher::Dispatch(
    const Dispatchable& dispatchable) const {
  span<uint8_t> method = FindByFirst(redirects_, dispatchable.Method(),
                                     /*default_value=*/dispatchable.Method());
  size_t dot_idx = DotIdx(method);
  if (dot_idx != kNotFound) {
    span<uint8_t> domain = method.subspan(0, dot_idx);
    span<uint8_t> command = method.subspan(dot_idx + 1);
    DomainDispatcher* dispatcher = FindByFirst(dispatchers_, domain);
    if (dispatcher) {
      std::function<void(const Dispatchable&)> dispatched =
          dispatcher->Dispatch(command);
      if (dispatched) {
        return DispatchResult(
            true, [&dispatchable, dispatched = std::move(dispatched)]() {
              dispatched(dispatchable);
            });
      }
    }
  }
  return DispatchResult(false, [=, &dispatchable]() {
    frontend_channel_->SendProtocolResponse(
        dispatchable.CallId(),
        CreateErrorResponse(
            dispatchable.CallId(), DispatchResponse::kMethodNotFound,
            std::string("'" +
                        std::string(dispatchable.Method().begin(),
                                    dispatchable.Method().end()) +
                        "' wasn't found")));
  });
}

template <typename T>
struct FirstLessThan {
  bool operator()(const std::pair<span<uint8_t>, T>& left,
                  const std::pair<span<uint8_t>, T>& right) {
    return SpanLessThan(left.first, right.first);
  }
};

void UberDispatcher::WireBackend(
    span<uint8_t> domain,
    const std::vector<std::pair<span<uint8_t>, span<uint8_t>>>&
        sorted_redirects,
    std::unique_ptr<DomainDispatcher> dispatcher) {
  auto it = redirects_.insert(redirects_.end(), sorted_redirects.begin(),
                              sorted_redirects.end());
  std::inplace_merge(redirects_.begin(), it, redirects_.end(),
                     FirstLessThan<span<uint8_t>>());
  auto jt = dispatchers_.insert(dispatchers_.end(),
                                std::make_pair(domain, std::move(dispatcher)));
  std::inplace_merge(dispatchers_.begin(), jt, dispatchers_.end(),
                     FirstLessThan<std::unique_ptr<DomainDispatcher>>());
}

}  // namespace v8_crdtp
