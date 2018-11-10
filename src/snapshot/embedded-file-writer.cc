// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/embedded-file-writer.h"

namespace v8 {
namespace internal {

// Platform-independent bits.
// -----------------------------------------------------------------------------

namespace {

DataDirective PointerSizeDirective() {
  if (kPointerSize == 8) {
    return kQuad;
  } else {
    CHECK_EQ(4, kPointerSize);
    return kLong;
  }
}

}  // namespace

void PlatformDependentEmbeddedFileWriter::AlignToCodeAlignment() {
  fprintf(fp_, ".balign 32\n");
}

void PlatformDependentEmbeddedFileWriter::Comment(const char* string) {
  fprintf(fp_, "// %s\n", string);
}

// static
const char* PlatformDependentEmbeddedFileWriter::DirectiveAsString(
    DataDirective directive) {
  switch (directive) {
    case kByte:
      return ".byte";
    case kLong:
      return ".long";
    case kQuad:
      return ".quad";
    case kOcta:
      return ".octa";
  }
  UNREACHABLE();
}

// V8_OS_MACOSX
// -----------------------------------------------------------------------------

#if defined(V8_OS_MACOSX)

void PlatformDependentEmbeddedFileWriter::SectionText() {
  fprintf(fp_, ".text\n");
}

void PlatformDependentEmbeddedFileWriter::SectionData() {
  fprintf(fp_, ".data\n");
}

void PlatformDependentEmbeddedFileWriter::SectionRoData() {
  fprintf(fp_, ".const_data\n");
}

void PlatformDependentEmbeddedFileWriter::AnnotateSymbolAsFunction(
    const char* name) {
  // TODO(mvstanton): Investigate the proper directives on OSX.
}

void PlatformDependentEmbeddedFileWriter::DeclareSymbolGlobal(
    const char* name) {
  // TODO(jgruber): Investigate switching to .globl. Using .private_extern
  // prevents something along the compilation chain from messing with the
  // embedded blob. Using .global here causes embedded blob hash verification
  // failures at runtime.
  fprintf(fp_, ".private_extern _%s\n", name);
}

void PlatformDependentEmbeddedFileWriter::DeclareSymbol(const char* name) {
  fprintf(fp_, ".private_extern _%s\n", name);
}

void PlatformDependentEmbeddedFileWriter::Symbol(const char* name) {
  fprintf(fp_, "_%s:\n", name);
}

void PlatformDependentEmbeddedFileWriter::IndentedReferenceToSymbol(
    const char* name) {
  fprintf(fp_, "  %s _%s\n", DirectiveAsString(PointerSizeDirective()), name);
}

int PlatformDependentEmbeddedFileWriter::IndentedDataDirective(
    DataDirective directive) {
  return fprintf(fp_, "  %s ", DirectiveAsString(directive));
}

// V8_OS_AIX
// -----------------------------------------------------------------------------

#elif defined(V8_OS_AIX)

// TODO(aix): Update custom logic previously contained in section header macros.
// See
// https://cs.chromium.org/chromium/src/v8/src/snapshot/macros.h?l=81&rcl=31b2546b348e864539ade15897eac971b3c0e402

void PlatformDependentEmbeddedFileWriter::SectionText() {
  fprintf(fp_, ".csect .text[PR]\n");
}

void PlatformDependentEmbeddedFileWriter::SectionData() {
  // TODO(aix): Confirm and update if needed.
  fprintf(fp_, ".csect .data[RW]\n");
}

void PlatformDependentEmbeddedFileWriter::SectionRoData() {
  fprintf(fp_, ".csect[RO]\n");
}

void PlatformDependentEmbeddedFileWriter::AnnotateSymbolAsFunction(
    const char* name) {
  // TODO(aix): Update.
}

void PlatformDependentEmbeddedFileWriter::DeclareSymbolGlobal(
    const char* name) {
  fprintf(fp_, ".globl %s\n", name);
}

void PlatformDependentEmbeddedFileWriter::DeclareSymbol(const char* name) {
  // TODO(aix): Update.
  DeclareSymbolGlobal(name);
}

void PlatformDependentEmbeddedFileWriter::Symbol(const char* name) {
  fprintf(fp_, "%s:\n", name);
}

void PlatformDependentEmbeddedFileWriter::IndentedReferenceToSymbol(
    const char* name) {
  fprintf(fp_, "  %s %s\n", DirectiveAsString(PointerSizeDirective()), name);
}

int PlatformDependentEmbeddedFileWriter::IndentedDataDirective(
    DataDirective directive) {
  return fprintf(fp_, "  %s ", DirectiveAsString(directive));
}

// V8_OS_WIN
// -----------------------------------------------------------------------------

#elif defined(V8_OS_WIN)

void PlatformDependentEmbeddedFileWriter::SectionText() {
  fprintf(fp_, ".section .text\n");
}

void PlatformDependentEmbeddedFileWriter::SectionData() {
  fprintf(fp_, ".section .data\n");
}

void PlatformDependentEmbeddedFileWriter::SectionRoData() {
  fprintf(fp_, ".section .rodata\n");
}

void PlatformDependentEmbeddedFileWriter::AnnotateSymbolAsFunction(
    const char* name) {
// .scl 2 means StorageClass external.
// .type 32 means Type Representation Function.
#if defined(V8_TARGET_ARCH_X64) || defined(V8_TARGET_ARCH_ARM64)
  fprintf(fp_, ".def %s; .scl 2; .type 32; .endef;\n", name);
#else
  fprintf(fp_, ".def _%s; .scl 2; .type 32; .endef;\n", name);
#endif
}

void PlatformDependentEmbeddedFileWriter::DeclareSymbolGlobal(
    const char* name) {
#if defined(V8_TARGET_ARCH_X64) || defined(V8_TARGET_ARCH_ARM64)
  fprintf(fp_, ".globl %s\n", name);
#else
  fprintf(fp_, ".globl _%s\n", name);
#endif
}

void PlatformDependentEmbeddedFileWriter::DeclareSymbol(const char* name) {}

void PlatformDependentEmbeddedFileWriter::Symbol(const char* name) {
#if defined(V8_TARGET_ARCH_X64) || defined(V8_TARGET_ARCH_ARM64)
  fprintf(fp_, "%s:\n", name);
#else
  fprintf(fp_, "_%s:\n", name);
#endif
}

void PlatformDependentEmbeddedFileWriter::IndentedReferenceToSymbol(
    const char* name) {
#if defined(V8_TARGET_ARCH_X64) || defined(V8_TARGET_ARCH_ARM64)
  fprintf(fp_, "  %s %s\n", DirectiveAsString(PointerSizeDirective()), name);
#else
  fprintf(fp_, "  %s _%s\n", DirectiveAsString(PointerSizeDirective()), name);
#endif
}

int PlatformDependentEmbeddedFileWriter::IndentedDataDirective(
    DataDirective directive) {
  return fprintf(fp_, "  %s ", DirectiveAsString(directive));
}

// Everything but AIX, Windows, or OSX.
// -----------------------------------------------------------------------------

#else

void PlatformDependentEmbeddedFileWriter::SectionText() {
#ifdef OS_CHROMEOS
  fprintf(fp_, ".section .text.hot.embedded\n");
#else
  fprintf(fp_, ".section .text\n");
#endif
}

void PlatformDependentEmbeddedFileWriter::SectionData() {
  fprintf(fp_, ".section .data\n");
}

void PlatformDependentEmbeddedFileWriter::SectionRoData() {
  fprintf(fp_, ".section .rodata\n");
}

void PlatformDependentEmbeddedFileWriter::AnnotateSymbolAsFunction(
    const char* name) {
#if defined(V8_TARGET_ARCH_ARM) || defined(V8_TARGET_ARCH_ARM64)
  // ELF format binaries on ARM use ".type <function name>, %function"
  // to create a DWARF subprogram entry.
  fprintf(fp_, ".type %s, %%function\n", name);
#else
  // Other ELF Format binaries use ".type <function name>, @function"
  // to create a DWARF subprogram entry.
  fprintf(fp_, ".type %s, @function\n", name);
#endif
}

void PlatformDependentEmbeddedFileWriter::DeclareSymbolGlobal(
    const char* name) {
  fprintf(fp_, ".global %s\n", name);
}

void PlatformDependentEmbeddedFileWriter::DeclareSymbol(const char* name) {
  fprintf(fp_, ".local %s\n", name);
}

void PlatformDependentEmbeddedFileWriter::Symbol(const char* name) {
  fprintf(fp_, "%s:\n", name);
}

void PlatformDependentEmbeddedFileWriter::IndentedReferenceToSymbol(
    const char* name) {
  fprintf(fp_, "  %s %s\n", DirectiveAsString(PointerSizeDirective()), name);
}

int PlatformDependentEmbeddedFileWriter::IndentedDataDirective(
    DataDirective directive) {
  return fprintf(fp_, "  %s ", DirectiveAsString(directive));
}

#endif

}  // namespace internal
}  // namespace v8
