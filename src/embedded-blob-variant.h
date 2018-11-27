// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EMBEDDED_BLOB_VARIANT_H_
#define V8_EMBEDDED_BLOB_VARIANT_H_

namespace v8 {
namespace internal {

class EmbeddedBlobVariant {
 public:
  EmbeddedBlobVariant() : blob_(nullptr), size_(0), cpu_features_(0) {}

  bool IsEmpty() const { return size_ == 0; }
  bool IsBetterThan(const EmbeddedBlobVariant& other) const;
  bool IsSupported() const;

  const uint8_t* blob() const { return blob_; }
  uint32_t size() const { return size_; }
  unsigned cpu_features() const { return cpu_features_; }

 protected:
  EmbeddedBlobVariant(const uint8_t* blob, uint32_t size, unsigned cpu_features)
      : blob_(blob), size_(size), cpu_features_(cpu_features) {}

  const uint8_t* blob_;
  uint32_t size_;
  unsigned cpu_features_;
};

class DefaultEmbeddedBlobVariant : public EmbeddedBlobVariant {
 public:
  DefaultEmbeddedBlobVariant(const uint8_t* blob, uint32_t size,
                             unsigned cpu_features);
};

class TrustedEmbeddedBlobVariant : public EmbeddedBlobVariant {
  TrustedEmbeddedBlobVariant(const uint8_t* blob, uint32_t size,
                             unsigned cpu_features);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EMBEDDED_BLOB_VARIANT_H_
