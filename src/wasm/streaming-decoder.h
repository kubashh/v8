// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_STREAMING_DECODER_H_
#define V8_WASM_STREAMING_DECODER_H_

#include <vector>
#include "src/isolate.h"
#include "src/wasm/decoder.h"
#include "src/wasm/wasm-objects.h"

namespace v8 {
namespace internal {
namespace wasm {

// This class is an interface for the StreamingDecoder to start the processing
// of the incoming module bytes.
class V8_EXPORT_PRIVATE StreamingProcessor {
 public:
  virtual ~StreamingProcessor() = default;
  // Process the first 8 bytes of a WebAssembly module.
  virtual bool ProcessModuleHeader(Vector<const uint8_t> bytes) = 0;
  // Process all sections but the code section.
  virtual bool ProcessSection(Vector<const uint8_t> bytes) = 0;
  // Process a function body.
  virtual bool ProcessFunctionBody(Vector<const uint8_t> bytes) = 0;
  // Report an error detected in the StreamingDecoder.
  virtual void Error(Vector<const uint8_t> bytes) = 0;
  // Finish the processing of the stream.
  virtual void Finish() = 0;
};

// The StreamingDecoder takes a sequence of byte arrays, each received by a call
// of {OnBytesReceived}, and extracts the bytes which belong to section payloads
// and function bodies.
class V8_EXPORT_PRIVATE StreamingDecoder {
 public:
  explicit StreamingDecoder(StreamingProcessor* processor);

  // The buffer passed into OnBytesReceived is owned by the caller.
  void OnBytesReceived(Vector<const uint8_t> bytes);

  // Finishes the stream and returns compiled WasmModuleObject.
  void Finish();

 private:
  // The SectionBuffer is the data object for the content of a single section.
  // It stores all bytes of the section (including section id and section
  // length), and the offset where the actual payload starts.
  class SectionBuffer {
   public:
    // id: The section id.
    // payload_length: The length of the payload.
    // length_bytes: The section length, as it is encoded in the module bytes.
    SectionBuffer(uint8_t id, size_t payload_length,
                  Vector<const uint8_t> length_bytes)
        :  // ID + length + payload
          length_(1 + length_bytes.length() + payload_length),
          bytes_(new uint8_t[length_]),
          payload_offset_(1 + length_bytes.length()) {
      bytes_[0] = id;
      memcpy(bytes_.get() + 1, &length_bytes.first(), length_bytes.length());
    }
    uint8_t* bytes() const { return bytes_.get(); }
    size_t length() const { return length_; }
    size_t payload_offset() const { return payload_offset_; }
    size_t payload_length() const { return length_ - payload_offset_; }

   private:
    size_t length_;
    std::unique_ptr<uint8_t[]> bytes_;
    size_t payload_offset_;
  };

  // The decoding of a stream of wasm module bytes is organized in states. Each
  // state provides a buffer to store the bytes required for the current state,
  // information on how many bytes have already been received, how many bytes
  // are needed, and a {Next} function which starts the next state once all
  // bytes of the current state were received.
  //
  // The states change according to the following state diagram:
  //
  //       Start
  //         |
  //         |
  //         v
  // DecodeModuleHeader
  //         |   _________________________________________
  //         |   |                                        |
  //         v   v                                        |
  //  DecodeSectionID --> DecodeSectionLength --> DecodeSectionPayload
  //         A                  |
  //         |                  | (if the section id == code)
  //         |                  v
  //         |      DecodeNumberOfFunctions -- > DecodeFunctionLength
  //         |                                          A    |
  //         |                                          |    |
  //         |  (after all functions were read)         |    v
  //         ------------------------------------- DecodeFunctionBody
  //
  class DecodingState {
   public:
    virtual ~DecodingState() = default;

    // Reads the bytes for the current state and returns the number of read
    // bytes.
    virtual size_t ReadBytes(StreamingDecoder* streaming,
                             Vector<const uint8_t> bytes);

    // Returns the next state of the streaming decoding.
    virtual std::unique_ptr<DecodingState> Next(
        StreamingDecoder* streaming) = 0;
    // The number of bytes to be received.
    virtual size_t size() const = 0;
    // The buffer to store the received bytes.
    virtual uint8_t* buffer() = 0;
    // In case of an error the buffer returned by {get_error_buffer} contains
    // the {SectionBuffer} with the bytes which caused the error. This means
    // that states like {DecodeFunctionLength} have to copy their buffer into
    // the {SectionBuffer}.
    virtual Vector<const uint8_t> get_error_buffer(
        StreamingDecoder* streaming) = 0;
    // The number of bytes which were already received.
    size_t offset() const { return offset_; }
    void set_offset(size_t value) { offset_ = value; }
    // The number of bytes which are still needed.
    size_t remaining() const { return size() - offset(); }
    bool is_finished() const { return offset() == size(); }
    // A flag to indicate if finishing the streaming decoder is allowed without
    // error.
    virtual bool is_finishing_allowed() const { return false; }

   private:
    size_t offset_ = 0;
  };

  // Forward declarations of the concrete states. This is needed so that they
  // can access private members of the StreamingDecoder.
  class DecodeVarInt32;
  class DecodeModuleHeader;
  class DecodeSectionID;
  class DecodeSectionLength;
  class DecodeSectionPayload;
  class DecodeNumberOfFunctions;
  class DecodeFunctionLength;
  class DecodeFunctionBody;

  // Creates a buffer for the next section of the module.
  SectionBuffer* CreateNewBuffer(uint8_t id, size_t length,
                                 Vector<const uint8_t> length_bytes) {
    section_buffers_.emplace_back(new SectionBuffer(id, length, length_bytes));
    return section_buffers_.back().get();
  }

  std::unique_ptr<DecodingState> Error() {
    ok_ = false;
    processor_->Error(state_->get_error_buffer(this));
    return std::unique_ptr<DecodingState>(nullptr);
  }

  bool ProcessModuleHeader() {
    ok_ &= processor_->ProcessModuleHeader(Vector<const uint8_t>(
        state_->buffer(), static_cast<int>(state_->size())));
    return ok_;
  }

  bool ProcessSection(SectionBuffer* buffer) {
    ok_ &= processor_->ProcessSection(Vector<const uint8_t>(
        buffer->bytes(), static_cast<int>(buffer->length())));
    return ok_;
  }

  bool ProcessFunctionBody(Vector<const uint8_t> bytes) {
    ok_ &= processor_->ProcessFunctionBody(bytes);
    return ok_;
  }

  bool ok() const { return ok_; }

  StreamingProcessor* processor_;
  bool ok_ = true;
  std::unique_ptr<DecodingState> state_;
  // The decoder is an instance variable because we use it for error handling.
  std::vector<std::unique_ptr<SectionBuffer>> section_buffers_;
  size_t total_size_ = 0;

  DISALLOW_COPY_AND_ASSIGN(StreamingDecoder);
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_STREAMING_DECODER_H_
