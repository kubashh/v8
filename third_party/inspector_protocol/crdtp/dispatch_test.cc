// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "cbor.h"
#include "dispatch.h"
#include "error_support.h"
#include "frontend_channel.h"
#include "json.h"
#include "test_platform.h"

namespace v8_crdtp {
// =============================================================================
// DispatchResponse - Error status and chaining / fall through
// =============================================================================
TEST(DispatchResponseTest, OK) {
  EXPECT_EQ(DispatchResponse::kSuccess, DispatchResponse::OK().status());
  EXPECT_TRUE(DispatchResponse::OK().isSuccess());
}

TEST(DispatchResponseTest, Error) {
  DispatchResponse error = DispatchResponse::Error("Oops!");
  EXPECT_FALSE(error.isSuccess());
  EXPECT_EQ(DispatchResponse::kError, error.status());
  EXPECT_EQ(DispatchResponse::kServerError, error.error_code());
  EXPECT_EQ("Oops!", error.errorMessage());
}

TEST(DispatchResponseTest, InternalError) {
  DispatchResponse error = DispatchResponse::InternalError();
  EXPECT_FALSE(error.isSuccess());
  EXPECT_EQ(DispatchResponse::kError, error.status());
  EXPECT_EQ(DispatchResponse::kInternalError, error.error_code());
  EXPECT_EQ("Internal error", error.errorMessage());
}

TEST(DispatchResponseTest, InvalidParams) {
  DispatchResponse error = DispatchResponse::InvalidParams("too cool");
  EXPECT_FALSE(error.isSuccess());
  EXPECT_EQ(DispatchResponse::kError, error.status());
  EXPECT_EQ(DispatchResponse::kInvalidParams, error.error_code());
  EXPECT_EQ("too cool", error.errorMessage());
}

TEST(DispatchResponseTest, FallThrough) {
  DispatchResponse error = DispatchResponse::FallThrough();
  EXPECT_FALSE(error.isSuccess());
  EXPECT_EQ(DispatchResponse::kFallThrough, error.status());
}

// =============================================================================
// Dispatchable - a shallow parser for CBOR encoded DevTools messages
// =============================================================================
TEST(DispatchableTest, MessageMustBeAnObject) {
  // Provide no input whatsoever.
  span<uint8_t> empty_span;
  Dispatchable empty(empty_span);
  EXPECT_FALSE(empty.ok());
  EXPECT_EQ(DispatchResponse::kInvalidRequest, empty.ErrorCode());
  EXPECT_EQ("Message must be an object", empty.ErrorMessage());
}

TEST(DispatchableTest, MessageMustHaveAnIntegerIdProperty) {
  // Construct an empty map inside of an envelope.
  std::vector<uint8_t> cbor;
  cbor::EnvelopeEncoder envelope;
  envelope.EncodeStart(&cbor);
  cbor.push_back(cbor::EncodeIndefiniteLengthMapStart());
  cbor.push_back(cbor::EncodeStop());
  envelope.EncodeStop(&cbor);
  Dispatchable dispatchable(SpanFrom(cbor));
  EXPECT_FALSE(dispatchable.ok());
  EXPECT_FALSE(dispatchable.HasCallId());
  EXPECT_EQ(DispatchResponse::kInvalidRequest, dispatchable.ErrorCode());
  EXPECT_EQ("Message must have integer 'id' property",
            dispatchable.ErrorMessage());
}

TEST(DispatchableTest, MessageMustHaveAStringMethodProperty) {
  // This time we set the id property, but not the method property.
  std::vector<uint8_t> cbor;
  cbor::EnvelopeEncoder envelope;
  envelope.EncodeStart(&cbor);
  cbor.push_back(cbor::EncodeIndefiniteLengthMapStart());
  cbor::EncodeString8(crdtp::SpanFrom("id"), &cbor);
  cbor::EncodeInt32(42, &cbor);
  cbor.push_back(cbor::EncodeStop());
  envelope.EncodeStop(&cbor);
  Dispatchable dispatchable(SpanFrom(cbor));
  EXPECT_FALSE(dispatchable.ok());
  EXPECT_TRUE(dispatchable.HasCallId());
  EXPECT_EQ(DispatchResponse::kInvalidRequest, dispatchable.ErrorCode());
  EXPECT_EQ("Message must have string 'method' property",
            dispatchable.ErrorMessage());
}

TEST(DispatchableTest, SessionIdMustBe7BitUSASCII) {
  // This time, the session id is an int but it should be a string. Method and
  // call id are present.
  std::vector<uint8_t> cbor;
  cbor::EnvelopeEncoder envelope;
  envelope.EncodeStart(&cbor);
  cbor.push_back(cbor::EncodeIndefiniteLengthMapStart());
  cbor::EncodeString8(crdtp::SpanFrom("id"), &cbor);
  cbor::EncodeInt32(42, &cbor);
  cbor::EncodeString8(crdtp::SpanFrom("method"), &cbor);
  cbor::EncodeString8(crdtp::SpanFrom("Foo.excuteBar"), &cbor);
  cbor::EncodeString8(crdtp::SpanFrom("sessionId"), &cbor);
  cbor::EncodeInt32(42, &cbor);  // int32 is wrong type
  cbor.push_back(cbor::EncodeStop());
  envelope.EncodeStop(&cbor);
  Dispatchable dispatchable(SpanFrom(cbor));
  EXPECT_FALSE(dispatchable.ok());
  EXPECT_TRUE(dispatchable.HasCallId());
  EXPECT_EQ(DispatchResponse::kInvalidRequest, dispatchable.ErrorCode());
  EXPECT_EQ("sessionId must be 7-bit US-ASCII string",
            dispatchable.ErrorMessage());
}

// =============================================================================
// Helpers for creating protocol cresponses and notifications.
// =============================================================================
TEST(CreateErrorResponseTest, SmokeTest) {
  ErrorSupport errors;
  errors.Push();
  errors.SetName("foo");
  errors.Push();
  errors.SetName("bar");
  errors.AddError("expected a string");
  errors.SetName("baz");
  errors.AddError("expected a surprise");
  auto serializable = CreateErrorResponse(42, DispatchResponse::kInvalidParams,
                                          "invalid params", &errors);
  std::string json;
  auto status = json::ConvertCBORToJSON(
      crdtp::SpanFrom(serializable->Serialize()), &json);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ(
      "{\"id\":42,\"error\":"
      "{\"code\":-32602,"
      "\"message\":\"invalid params\","
      "\"data\":\"foo.bar: expected a string; "
      "foo.baz: expected a surprise\"}}",
      json);
}

TEST(CreateErrorNotificationTest, SmokeTest) {
  auto serializable =
      CreateErrorNotification(DispatchResponse::kInternalError, "oops!");
  std::string json;
  auto status = json::ConvertCBORToJSON(
      crdtp::SpanFrom(serializable->Serialize()), &json);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ("{\"error\":{\"code\":-32603,\"message\":\"oops!\"}}", json);
}

TEST(CreateResponseTest, SmokeTest) {
  auto serializable = CreateResponse(42, nullptr);
  std::string json;
  auto status = json::ConvertCBORToJSON(
      crdtp::SpanFrom(serializable->Serialize()), &json);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ("{\"id\":42,\"result\":{}}", json);
}

TEST(CreateNotificationTest, SmokeTest) {
  auto serializable = CreateNotification("Foo.bar");
  std::string json;
  auto status = json::ConvertCBORToJSON(
      crdtp::SpanFrom(serializable->Serialize()), &json);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ("{\"method\":\"Foo.bar\",\"params\":{}}", json);
}

// =============================================================================
// UberDispatcher - dispatches between domains (backends).
// =============================================================================
class TestChannel : public FrontendChannel {
 public:
  std::string JSON() const {
    std::string json;
    json::ConvertCBORToJSON(SpanFrom(cbor_), &json);
    return json;
  }

 private:
  void SendProtocolResponse(int call_id,
                            std::unique_ptr<Serializable> message) override {
    cbor_ = message->Serialize();
  }

  void SendProtocolNotification(
      std::unique_ptr<Serializable> message) override {
    cbor_ = message->Serialize();
  }

  void FallThrough(int call_id,
                   span<uint8_t> method,
                   span<uint8_t> message) override {}

  void FlushProtocolNotifications() override {}

  std::vector<uint8_t> cbor_;
};

TEST(UberDispatcherTest, SmokeTest) {
  TestChannel channel;
  UberDispatcher dispatcher(&channel);
  std::vector<uint8_t> message;
  json::ConvertJSONToCBOR(SpanFrom("{\"id\":42,\"method\":\"Foo.bar\"}"),
                          &message);
  Dispatchable dispatchable(SpanFrom(message));
  ASSERT_TRUE(dispatchable.ok());
  UberDispatcher::DispatchResult dispatched = dispatcher.Dispatch(dispatchable);
  EXPECT_FALSE(dispatched.MethodFound());
  dispatched.Run();
  EXPECT_EQ(
      "{\"id\":42,\"error\":"
      "{\"code\":-32601,\"message\":\"'Foo.bar' wasn't found\"}}",
      channel.JSON());
}
}  // namespace v8_crdtp
