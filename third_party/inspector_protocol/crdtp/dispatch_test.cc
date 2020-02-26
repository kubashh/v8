// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "dispatch.h"
#include "frontend_channel.h"
#include "json.h"
#include "test_platform.h"

namespace v8_crdtp {
// =============================================================================
// Dispatchable - TBD
// =============================================================================
/*
class TestChannel : public FrontendChannel {
 public:
  std::string json() const {
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

TEST(DispatchableTest, EmptyYieldsError) {
  std::vector<uint8_t> empty;
  Dispatchable dispatchable(crdtp::SpanFrom(empty));
  TestChannel channel;
  dispatchable.SendErrorVia(&channel);
  EXPECT_EQ(
      "{\"error\":{\"code\":-32600,\"message\":\"Message must be an object\"}}",
      channel.json());
}
*/
// TEST(DispatchableTest, InvalidStartByteYieldsError) {
//   std::vector<uint8_t> invalid_start = {'x'};
//   Dispatchable dispatchable(crdtp::SpanFrom(invalid_start));
//   EXPECT_EQ(Error::CBOR_INVALID_START_BYTE, dispatchable.Status().error);
//   EXPECT_EQ(0u, dispatchable.Status().pos);
// }

// TEST(DispatchableTest, SmokeTest) {
//   constexpr char kMsg[] =
//       R"({"id":42,"sessionId":"deadbeef","method":"Foo.bar","params":{}})";
//   std::vector<uint8_t> cbor;
//   crdtp::Status status = json::ConvertJSONToCBOR(crdtp::SpanFrom(kMsg),
//   &cbor); ASSERT_EQ("OK", status.ToASCIIString()); Dispatchable
//   dispatchable(crdtp::SpanFrom(cbor)); EXPECT_EQ("OK",
//   dispatchable.Status().ToASCIIString()); EXPECT_EQ(42,
//   dispatchable.CallId());
// }
}  // namespace v8_crdtp
