// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/streaming-decoder.h"

#include "src/objects-inl.h"

#include "src/handles.h"
#include "src/wasm/decoder.h"
#include "src/wasm/leb-helper.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-result.h"

#include "src/objects/descriptor-array.h"
#include "src/objects/dictionary.h"

using namespace v8::internal;
using namespace v8::internal::wasm;

void StreamingDecoder::OnBytesReceived(Vector<const uint8_t> bytes) {
  int current = 0;
  while (ok() && current < bytes.length()) {
    int num_bytes = static_cast<int>(
        state_->ReadBytes(this, bytes.SubVector(current, bytes.length())));
    current += num_bytes;
    if (state_->is_finished()) {
      state_ = state_->Next(this);
    }
  }
  total_size_ += bytes.length();
}

size_t StreamingDecoder::DecodingState::ReadBytes(StreamingDecoder* streaming,
                                                  Vector<const uint8_t> bytes) {
  size_t num_bytes = std::min(static_cast<size_t>(bytes.length()), remaining());
  memcpy(buffer() + offset(), bytes.begin(), num_bytes);
  set_offset(offset() + num_bytes);
  return num_bytes;
}

void StreamingDecoder::Finish() {
  if (ok() && !state_->is_finishing_allowed()) {
    // The byte stream ended too early, we report an error.
    Error();
  }
  processor_->Finish();
}

// An abstract class to share code among the states which decode VarInts. This
// class takes over the decoding of the VarInt and then calls the actual decode
// code with the decoded value.
class StreamingDecoder::DecodeVarInt32 : public DecodingState {
 public:
  explicit DecodeVarInt32(size_t max_value) : max_value_(max_value) {}
  uint8_t* buffer() override { return byte_buffer_; }
  size_t size() const override { return kMaxVarInt32Size; }

  size_t ReadBytes(StreamingDecoder* streaming,
                   Vector<const uint8_t> bytes) override;

  std::unique_ptr<DecodingState> Next(StreamingDecoder* streaming) override;

  virtual std::unique_ptr<DecodingState> NextWithValue(
      StreamingDecoder* streaming) = 0;

  size_t value() const { return value_; }
  size_t bytes_needed() const { return bytes_needed_; }

 private:
  uint8_t byte_buffer_[kMaxVarInt32Size];
  // The maximum valid value decoded in this state. {Next} returns an error if
  // this value is exceeded.
  size_t max_value_;
  size_t value_ = 0;
  size_t bytes_needed_ = 0;
};

class StreamingDecoder::DecodeModuleHeader : public DecodingState {
 public:
  size_t size() const override { return kModuleHeaderSize; }
  uint8_t* buffer() override { return byte_buffer_; }

  Vector<const uint8_t> get_error_buffer(StreamingDecoder* streaming) override {
    return Vector<const uint8_t>(buffer(), static_cast<int>(size()));
  }

  std::unique_ptr<DecodingState> Next(StreamingDecoder* streaming) override;

 private:
  // Checks if the magic bytes of the module header are correct.
  void CheckHeader(Decoder* decoder);

  // The size of the module header.
  static constexpr size_t kModuleHeaderSize = 8;
  uint8_t byte_buffer_[kModuleHeaderSize];
};

class StreamingDecoder::DecodeSectionID : public DecodingState {
 public:
  size_t size() const override { return 1; }
  uint8_t* buffer() override { return &id_; }
  Vector<const uint8_t> get_error_buffer(StreamingDecoder* streaming) override {
    // No streaming errors happen when decoding the section ID.
    UNREACHABLE();
    return Vector<const uint8_t>();
  }

  bool is_finishing_allowed() const override { return true; }

  uint8_t id() const { return id_; }

  std::unique_ptr<DecodingState> Next(StreamingDecoder* streaming) override;

 private:
  uint8_t id_ = 0;
};

class StreamingDecoder::DecodeSectionLength : public DecodeVarInt32 {
 public:
  explicit DecodeSectionLength(uint8_t id)
      : DecodeVarInt32(kV8MaxWasmModuleSize), section_id_(id) {}

  uint8_t section_id() const { return section_id_; }

  std::unique_ptr<DecodingState> NextWithValue(
      StreamingDecoder* streaming) override;

  Vector<const uint8_t> get_error_buffer(StreamingDecoder* streaming) override {
    // No {SectionBuffer} exists yet, we have to create it.
    SectionBuffer* b = streaming->CreateNewBuffer(
        section_id(), 0,
        Vector<const uint8_t>(buffer(), static_cast<int>(size())));
    return Vector<const uint8_t>(b->bytes(), static_cast<int>(b->length()));
  }

 private:
  uint8_t section_id_;
};

class StreamingDecoder::DecodeSectionPayload : public DecodingState {
 public:
  explicit DecodeSectionPayload(SectionBuffer* section_buffer)
      : section_buffer_(section_buffer) {}

  size_t size() const override { return section_buffer_->payload_length(); }
  uint8_t* buffer() override {
    return section_buffer_->bytes() + section_buffer_->payload_offset();
  }

  std::unique_ptr<DecodingState> Next(StreamingDecoder* streaming) override;

  Vector<const uint8_t> get_error_buffer(StreamingDecoder* streaming) override {
    return Vector<const uint8_t>(section_buffer_->bytes(),
                                 static_cast<int>(section_buffer_->length()));
  }

  SectionBuffer* section_buffer() const { return section_buffer_; }

 private:
  SectionBuffer* section_buffer_;
};

class StreamingDecoder::DecodeNumberOfFunctions : public DecodeVarInt32 {
 public:
  explicit DecodeNumberOfFunctions(SectionBuffer* section_buffer)
      : DecodeVarInt32(kV8MaxWasmFunctions), section_buffer_(section_buffer) {}

  SectionBuffer* section_buffer() const { return section_buffer_; }

  std::unique_ptr<DecodingState> NextWithValue(
      StreamingDecoder* streaming) override;

  Vector<const uint8_t> get_error_buffer(StreamingDecoder* streaming) override {
    // Copy the bytes we read into the section buffer.
    size_t num_bytes =
        std::min(section_buffer_->payload_length(), bytes_needed());
    memcpy(section_buffer_->bytes() + section_buffer_->payload_offset(),
           buffer(), num_bytes);
    return Vector<const uint8_t>(section_buffer_->bytes(),
                                 static_cast<int>(section_buffer_->length()));
  }

 private:
  SectionBuffer* section_buffer_;
};

class StreamingDecoder::DecodeFunctionLength : public DecodeVarInt32 {
 public:
  explicit DecodeFunctionLength(SectionBuffer* section_buffer,
                                size_t buffer_offset,
                                size_t num_remaining_functions)
      : DecodeVarInt32(kV8MaxWasmFunctionSize),
        section_buffer_(section_buffer),
        buffer_offset_(buffer_offset),
        // We are reading a new function, so one function less is remaining.
        num_remaining_functions_(num_remaining_functions - 1) {
    DCHECK_GT(num_remaining_functions, 0);
  }

  size_t num_remaining_functions() const { return num_remaining_functions_; }
  size_t buffer_offset() const { return buffer_offset_; }
  SectionBuffer* section_buffer() const { return section_buffer_; }

  std::unique_ptr<DecodingState> NextWithValue(
      StreamingDecoder* streaming) override;

  Vector<const uint8_t> get_error_buffer(StreamingDecoder* streaming) override {
    // Copy the bytes we read into the section buffer.
    size_t num_bytes =
        std::min(section_buffer_->length() - buffer_offset_, bytes_needed());
    memcpy(section_buffer_->bytes() + buffer_offset_, buffer(), num_bytes);
    return Vector<const uint8_t>(section_buffer_->bytes(),
                                 static_cast<int>(section_buffer_->length()));
  }

 private:
  SectionBuffer* section_buffer_;
  size_t buffer_offset_;
  size_t num_remaining_functions_;
};

class StreamingDecoder::DecodeFunctionBody : public DecodingState {
 public:
  explicit DecodeFunctionBody(SectionBuffer* section_buffer,
                              size_t buffer_offset, size_t function_length,
                              size_t num_remaining_functions)
      : section_buffer_(section_buffer),
        buffer_offset_(buffer_offset),
        size_(function_length),
        num_remaining_functions_(num_remaining_functions) {}

  size_t size() const override { return size_; }
  uint8_t* buffer() override {
    return section_buffer_->bytes() + buffer_offset_;
  }
  size_t num_remaining_functions() const { return num_remaining_functions_; }
  size_t buffer_offset() const { return buffer_offset_; }
  SectionBuffer* section_buffer() const { return section_buffer_; }

  std::unique_ptr<DecodingState> Next(StreamingDecoder* streaming) override;
  Vector<const uint8_t> get_error_buffer(StreamingDecoder* streaming) override {
    return Vector<const uint8_t>(section_buffer_->bytes(),
                                 static_cast<int>(section_buffer_->length()));
  }

 private:
  SectionBuffer* section_buffer_;
  size_t buffer_offset_;
  size_t size_;
  size_t num_remaining_functions_;
};

size_t StreamingDecoder::DecodeVarInt32::ReadBytes(
    StreamingDecoder* streaming, Vector<const uint8_t> bytes) {
  size_t bytes_read =
      std::min(static_cast<size_t>(bytes.length()), remaining());
  memcpy(buffer() + offset(), bytes.begin(), bytes_read);
  Decoder decoder(buffer(), buffer() + offset() + bytes_read);
  value_ = decoder.consume_i32v();
  // The number of bytes we actually needed to read.
  DCHECK_GT(decoder.pc(), buffer());
  bytes_needed_ = static_cast<size_t>(decoder.pc() - buffer());

  if (decoder.failed()) {
    if (offset() + bytes_read == size()) {
      // We only report an error if we read all bytes.
      streaming->Error();
    }
    set_offset(offset() + bytes_read);
    return bytes_read;
  } else {
    DCHECK_GT(bytes_needed_, offset());
    size_t result = bytes_needed_ - offset();
    // We read all the bytes we needed.
    set_offset(size());
    return result;
  }
}

std::unique_ptr<StreamingDecoder::DecodingState>
StreamingDecoder::DecodeVarInt32::Next(StreamingDecoder* streaming) {
  if (!streaming->ok()) {
    return std::unique_ptr<DecodingState>(nullptr);
  }
  if (value() > max_value_) {
    return streaming->Error();
  }

  return NextWithValue(streaming);
}

std::unique_ptr<StreamingDecoder::DecodingState>
StreamingDecoder::DecodeModuleHeader::Next(StreamingDecoder* streaming) {
  if (!streaming->ProcessModuleHeader())
    return std::unique_ptr<DecodingState>(nullptr);
  return std::unique_ptr<DecodingState>(new DecodeSectionID());
}

std::unique_ptr<StreamingDecoder::DecodingState>
StreamingDecoder::DecodeSectionID::Next(StreamingDecoder* streaming) {
  return std::unique_ptr<DecodingState>(new DecodeSectionLength(id()));
}

std::unique_ptr<StreamingDecoder::DecodingState>
StreamingDecoder::DecodeSectionLength::NextWithValue(
    StreamingDecoder* streaming) {
  SectionBuffer* buf = streaming->CreateNewBuffer(
      section_id(), value(),
      Vector<const uint8_t>(buffer(), static_cast<int>(bytes_needed())));
  if (value() == 0) {
    // There is no payload, we go to the next section immediately.
    if (section_id() != SectionCode::kCodeSectionCode) {
      if (!streaming->ProcessSection(buf)) {
        return std::unique_ptr<DecodingState>(nullptr);
      }
    }
    return std::unique_ptr<DecodingState>(new DecodeSectionID());
  } else if (section_id() == SectionCode::kCodeSectionCode) {
    // We reached the code section. All functions of the code section are put
    // into the same SectionBuffer.
    return std::unique_ptr<DecodingState>(new DecodeNumberOfFunctions(buf));
  } else {
    return std::unique_ptr<DecodingState>(new DecodeSectionPayload(buf));
  }
}

std::unique_ptr<StreamingDecoder::DecodingState>
StreamingDecoder::DecodeSectionPayload::Next(StreamingDecoder* streaming) {
  if (!streaming->ProcessSection(section_buffer())) {
    return std::unique_ptr<DecodingState>(nullptr);
  }
  return std::unique_ptr<DecodingState>(new DecodeSectionID());
}

std::unique_ptr<StreamingDecoder::DecodingState>
StreamingDecoder::DecodeNumberOfFunctions::NextWithValue(
    StreamingDecoder* streaming) {
  // Copy the bytes we read into the section buffer.
  if (section_buffer_->payload_length() >= bytes_needed()) {
    memcpy(section_buffer_->bytes() + section_buffer_->payload_offset(),
           buffer(), bytes_needed());
  } else {
    return streaming->Error();
  }

  // {value} is the number of functions.
  if (value() > 0) {
    return std::unique_ptr<DecodingState>(new DecodeFunctionLength(
        section_buffer(), section_buffer()->payload_offset() + bytes_needed(),
        value()));
  } else {
    return std::unique_ptr<DecodingState>(new DecodeSectionID());
  }
}

std::unique_ptr<StreamingDecoder::DecodingState>
StreamingDecoder::DecodeFunctionLength::NextWithValue(
    StreamingDecoder* streaming) {
  // Copy the bytes we read into the section buffer.
  if (section_buffer_->length() >= buffer_offset_ + bytes_needed()) {
    memcpy(section_buffer_->bytes() + buffer_offset_, buffer(), bytes_needed());
  } else {
    return streaming->Error();
  }

  // {value} is the length of the function.
  if (value() == 0) {
    return streaming->Error();
  } else if (buffer_offset() + bytes_needed() + value() >
             section_buffer()->length()) {
    return streaming->Error();
  }

  return std::unique_ptr<DecodingState>(
      new DecodeFunctionBody(section_buffer(), buffer_offset() + bytes_needed(),
                             value(), num_remaining_functions()));
}

std::unique_ptr<StreamingDecoder::DecodingState>
StreamingDecoder::DecodeFunctionBody::Next(StreamingDecoder* streaming) {
  if (!streaming->ProcessFunctionBody(
          Vector<const uint8_t>(buffer(), static_cast<int>(size())))) {
    return std::unique_ptr<DecodingState>(nullptr);
  }
  if (num_remaining_functions() != 0) {
    return std::unique_ptr<DecodingState>(new DecodeFunctionLength(
        section_buffer(), buffer_offset() + size(), num_remaining_functions()));
  } else {
    if (buffer_offset() + size() != section_buffer()->length()) {
      return streaming->Error();
    }
    return std::unique_ptr<DecodingState>(new DecodeSectionID());
  }
}

StreamingDecoder::StreamingDecoder(StreamingProcessor* processor)
    : processor_(processor),
      // A module always starts with a module header.
      state_(new DecodeModuleHeader()) {}
