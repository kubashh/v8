// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/embedded/platform-embedded-file-writer-base.h"

#include <string>

#include "src/base/memory.h"
#include "src/base/platform/wrappers.h"
#include "src/common/globals.h"
#include "src/snapshot/embedded/platform-embedded-file-writer-aix.h"
#include "src/snapshot/embedded/platform-embedded-file-writer-generic.h"
#include "src/snapshot/embedded/platform-embedded-file-writer-mac.h"
#include "src/snapshot/embedded/platform-embedded-file-writer-win.h"

namespace v8 {
namespace internal {

DataDirective PointerSizeDirective() {
  if (kSystemPointerSize == 8) {
    return kQuad;
  } else {
    CHECK_EQ(4, kSystemPointerSize);
    return kLong;
  }
}

PlatformEmbeddedFileWriterBase::PlatformEmbeddedFileWriterBase(
    EmbeddedTargetArch target_arch) {
  switch (target_arch) {
    case EmbeddedTargetArch::kArm:
    case EmbeddedTargetArch::kArm64:
    case EmbeddedTargetArch::kIA32:
    case EmbeddedTargetArch::kX64:
    case EmbeddedTargetArch::kGeneric:
      target_byte_order_ = EmbeddedTargetByteOrder::kLittle;
      break;
    case EmbeddedTargetArch::kMips:
    case EmbeddedTargetArch::kMips64:
    case EmbeddedTargetArch::kPpc:
    case EmbeddedTargetArch::kS390:
    case EmbeddedTargetArch::kS390x:
      target_byte_order_ = EmbeddedTargetByteOrder::kBig;
      break;
    case EmbeddedTargetArch::kPpc64:
      // TODO(bwh): Straighten out ppc64 architecture naming so the
      // byte order is explicit
#if V8_TARGET_ARCH_LITTLE_ENDIAN
      target_byte_order_ = EmbeddedTargetByteOrder::kLittle;
#else
      target_byte_order_ = EmbeddedTargetByteOrder::kBig;
#endif
      break;
    default:
      UNREACHABLE();
  }
}

int PlatformEmbeddedFileWriterBase::HexLiteral(uint64_t value) {
  return fprintf(fp_, "0x%" PRIx64, value);
}

int DataDirectiveSize(DataDirective directive) {
  switch (directive) {
    case kByte:
      return 1;
    case kLong:
      return 4;
    case kQuad:
      return 8;
    case kOcta:
      return 16;
  }
  UNREACHABLE();
}

int PlatformEmbeddedFileWriterBase::WriteByteChunk(const uint8_t* data) {
  size_t kSize = DataDirectiveSize(ByteChunkDataDirective());
  size_t kHalfSize = kSize / 2;
  uint64_t high = 0, low = 0;

  switch (kSize) {
    case 1:
      low = *data;
      break;
    case 4:
      if (target_byte_order_ == EmbeddedTargetByteOrder::kLittle)
        low = base::ReadLittleEndianValue<uint32_t>(
            reinterpret_cast<Address>(data));
      else
        low =
            base::ReadBigEndianValue<uint32_t>(reinterpret_cast<Address>(data));
      break;
    case 8:
      if (target_byte_order_ == EmbeddedTargetByteOrder::kLittle)
        low = base::ReadLittleEndianValue<uint64_t>(
            reinterpret_cast<Address>(data));
      else
        low =
            base::ReadBigEndianValue<uint64_t>(reinterpret_cast<Address>(data));
      break;
    case 16:
      if (target_byte_order_ == EmbeddedTargetByteOrder::kLittle) {
        high = base::ReadLittleEndianValue<uint64_t>(
            reinterpret_cast<Address>(data) + kHalfSize);
        low = base::ReadLittleEndianValue<uint64_t>(
            reinterpret_cast<Address>(data));
      } else {
        high =
            base::ReadBigEndianValue<uint64_t>(reinterpret_cast<Address>(data));
        low = base::ReadBigEndianValue<uint64_t>(
            reinterpret_cast<Address>(data) + kHalfSize);
      }
      break;
    default:
      UNREACHABLE();
  }

  if (high != 0) {
    return fprintf(fp(), "0x%" PRIx64 "%016" PRIx64, high, low);
  } else {
    return fprintf(fp(), "0x%" PRIx64, low);
  }
}

namespace {

EmbeddedTargetArch DefaultEmbeddedTargetArch() {
#if defined(V8_TARGET_ARCH_ARM)
  return EmbeddedTargetArch::kArm;
#elif defined(V8_TARGET_ARCH_ARM64)
  return EmbeddedTargetArch::kArm64;
#elif defined(V8_TARGET_ARCH_IA32)
  return EmbeddedTargetArch::kIA32;
#elif defined(V8_TARGET_ARCH_MIPS) && defined(V8_TARGET_ARCH_BIG_ENDIAN)
  return EmbeddedTargetArch::kMips;
#elif defined(V8_TARGET_ARCH_MIPS64) && defined(V8_TARGET_ARCH_BIG_ENDIAN)
  return EmbeddedTargetArch::kMips64;
#elif defined(V8_TARGET_ARCH_PPC)
  return EmbeddedTargetArch::kPpc;
#elif defined(V8_TARGET_ARCH_PPC64)
  return EmbeddedTargetArch::kPpc64;
#elif defined(V8_TARGET_ARCH_S390)
  return EmbeddedTargetArch::kS390;
#elif defined(V8_TARGET_ARCH_S390X)
  return EmbeddedTargetArch::kS390x;
#elif defined(V8_TARGET_ARCH_X64)
  return EmbeddedTargetArch::kX64;
#else
  return EmbeddedTargetArch::kGeneric;
#endif
}

EmbeddedTargetArch ToEmbeddedTargetArch(const char* s) {
  if (s == nullptr) {
    return DefaultEmbeddedTargetArch();
  }

  std::string string(s);
  if (string == "arm") {
    return EmbeddedTargetArch::kArm;
  } else if (string == "arm64") {
    return EmbeddedTargetArch::kArm64;
  } else if (string == "ia32") {
    return EmbeddedTargetArch::kIA32;
  } else if (string == "mips") {
    return EmbeddedTargetArch::kMips;
  } else if (string == "mips64") {
    return EmbeddedTargetArch::kMips64;
  } else if (string == "ppc") {
    return EmbeddedTargetArch::kPpc;
  } else if (string == "ppc64") {
    return EmbeddedTargetArch::kPpc64;
  } else if (string == "s390") {
    return EmbeddedTargetArch::kS390;
  } else if (string == "s390x") {
    return EmbeddedTargetArch::kS390x;
  } else if (string == "x64") {
    return EmbeddedTargetArch::kX64;
  } else {
    return EmbeddedTargetArch::kGeneric;
  }
}

EmbeddedTargetOs DefaultEmbeddedTargetOs() {
#if defined(V8_OS_AIX)
  return EmbeddedTargetOs::kAIX;
#elif defined(V8_OS_MACOSX)
  return EmbeddedTargetOs::kMac;
#elif defined(V8_OS_WIN)
  return EmbeddedTargetOs::kWin;
#else
  return EmbeddedTargetOs::kGeneric;
#endif
}

EmbeddedTargetOs ToEmbeddedTargetOs(const char* s) {
  if (s == nullptr) {
    return DefaultEmbeddedTargetOs();
  }

  std::string string(s);
  if (string == "aix") {
    return EmbeddedTargetOs::kAIX;
  } else if (string == "chromeos") {
    return EmbeddedTargetOs::kChromeOS;
  } else if (string == "fuchsia") {
    return EmbeddedTargetOs::kFuchsia;
  } else if (string == "ios" || string == "mac") {
    return EmbeddedTargetOs::kMac;
  } else if (string == "win") {
    return EmbeddedTargetOs::kWin;
  } else {
    return EmbeddedTargetOs::kGeneric;
  }
}

}  // namespace

std::unique_ptr<PlatformEmbeddedFileWriterBase> NewPlatformEmbeddedFileWriter(
    const char* target_arch, const char* target_os) {
  auto embedded_target_arch = ToEmbeddedTargetArch(target_arch);
  auto embedded_target_os = ToEmbeddedTargetOs(target_os);

  if (embedded_target_os == EmbeddedTargetOs::kAIX) {
    return std::make_unique<PlatformEmbeddedFileWriterAIX>(embedded_target_arch,
                                                           embedded_target_os);
  } else if (embedded_target_os == EmbeddedTargetOs::kMac) {
    return std::make_unique<PlatformEmbeddedFileWriterMac>(embedded_target_arch,
                                                           embedded_target_os);
  } else if (embedded_target_os == EmbeddedTargetOs::kWin) {
    return std::make_unique<PlatformEmbeddedFileWriterWin>(embedded_target_arch,
                                                           embedded_target_os);
  } else {
    return std::make_unique<PlatformEmbeddedFileWriterGeneric>(
        embedded_target_arch, embedded_target_os);
  }

  UNREACHABLE();
}

}  // namespace internal
}  // namespace v8
