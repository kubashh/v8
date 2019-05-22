// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_EMBEDDED_PLATFORM_EMBEDDED_FILE_WRITER_GENERIC_H_
#define V8_SNAPSHOT_EMBEDDED_PLATFORM_EMBEDDED_FILE_WRITER_GENERIC_H_

#include <cinttypes>
#include <cstdio>  // For FILE.

#include "src/base/macros.h"
#include "src/snapshot/embedded/platform-embedded-file-writer-base.h"

namespace v8 {
namespace internal {

class PlatformEmbeddedFileWriterGeneric
    : public PlatformEmbeddedFileWriterBase {
 public:
  PlatformEmbeddedFileWriterGeneric(EmbeddedTargetArch target_arch,
                                    EmbeddedTargetOs target_os)
      : target_arch_(target_arch), target_os_(target_os) {
    // TODO(jgruber): Remove these once platforms have been split off.
    USE(target_arch_);
    USE(target_os_);
  }

  void SectionText() override;
  void SectionData() override;
  void SectionRoData() override;

  void AlignToCodeAlignment() override;
  void AlignToDataAlignment() override;

  void DeclareUint32(const char* name, uint32_t value) override;
  void DeclarePointerToSymbol(const char* name, const char* target) override;

  void DeclareLabel(const char* name) override;

  void SourceInfo(int fileid, const char* filename, int line) override;
  void DeclareFunctionBegin(const char* name) override;
  void DeclareFunctionEnd(const char* name) override;

  int HexLiteral(uint64_t value) override;

  void Comment(const char* string) override;

  void FilePrologue() override;
  void DeclareExternalFilename(int fileid, const char* filename) override;
  void FileEpilogue() override;

  int IndentedDataDirective(DataDirective directive) override;

#if defined(V8_OS_WIN_X64)
  // TODO(jgruber): Move these to the windows-specific writer.
  void StartPdataSection();
  void EndPdataSection();
  void StartXdataSection();
  void EndXdataSection();
  void DeclareExternalFunction(const char* name);
  void DeclareRvaToSymbol(const char* name, uint64_t offset);
#endif  // defined(V8_OS_WIN_X64)

 private:
  void DeclareSymbolGlobal(const char* name);

 private:
  EmbeddedTargetArch target_arch_;
  EmbeddedTargetOs target_os_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_EMBEDDED_PLATFORM_EMBEDDED_FILE_WRITER_GENERIC_H_
