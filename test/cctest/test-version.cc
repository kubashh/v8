// Copyright 2009 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "src/v8.h"

#include "src/utils.h"
#include "src/version.h"
#include "test/cctest/cctest.h"

using namespace v8::internal;

// Calculate the V8 version string.
template <int Major, int Minor, int Build, int Patch, bool Candidate>
void VersionImpl<Major, Minor, Build, Patch, Candidate>::GetString(
    Vector<char> str) {
  // void VersionImpl::GetString(Vector<char> str) {
  const char* candidate = IsCandidate() ? " (candidate)" : "";
#ifdef USE_SIMULATOR
  const char* is_simulator = " SIMULATOR";
#else
  const char* is_simulator = "";
#endif  // USE_SIMULATOR
  if (GetPatch() > 0) {
    SNPrintF(str, "%d.%d.%d.%d%s%s", GetMajor(), GetMinor(), GetBuild(),
             GetPatch(), candidate, is_simulator);
  } else {
    SNPrintF(str, "%d.%d.%d%s%s", GetMajor(), GetMinor(), GetBuild(), candidate,
             is_simulator);
  }
}

// Calculate the SONAME for the V8 shared library.
template <int Major, int Minor, int Build, int Patch, bool Candidate>
void VersionImpl<Major, Minor, Build, Patch, Candidate>::GetSONAME(
    Vector<char> str) {
  // void VersionImpl::GetSONAME(Vector<char> str) {
  if (soname_ == NULL || *soname_ == '\0') {
    // Generate generic SONAME if no specific SONAME is defined.
    const char* candidate = IsCandidate() ? "-candidate" : "";
    if (GetPatch() > 0) {
      SNPrintF(str, "libv8-%d.%d.%d.%d%s.so", GetMajor(), GetMinor(),
               GetBuild(), GetPatch(), candidate);
    } else {
      SNPrintF(str, "libv8-%d.%d.%d%s.so", GetMajor(), GetMinor(), GetBuild(),
               candidate);
    }
  } else {
    // Use specific SONAME.
    SNPrintF(str, "%s", soname_);
  }
}

template <int Major, int Minor, int Build, int Patch, bool Candidate>
void VersionImpl<Major, Minor, Build, Patch, Candidate>::CheckVersion(
    const char* expected_version_string, const char* expected_generic_soname) {
  static v8::internal::EmbeddedVector<char, 128> version_str;
  static v8::internal::EmbeddedVector<char, 128> soname_str;

  // Test version without specific SONAME.
  soname_ = "";
  GetString(version_str);
  CHECK_EQ(0, strcmp(expected_version_string, version_str.start()));
  GetSONAME(soname_str);
  CHECK_EQ(0, strcmp(expected_generic_soname, soname_str.start()));

  // Test version with specific SONAME.
  const char* soname = "libv8.so.1";
  soname_ = soname;
  GetString(version_str);
  CHECK_EQ(0, strcmp(expected_version_string, version_str.start()));
  GetSONAME(soname_str);
  CHECK_EQ(0, strcmp(soname, soname_str.start()));
}

using ver1 = VersionImpl<0, 0, 0, 0, false>;
using ver2 = VersionImpl<0, 0, 0, 0, true>;
using ver3 = VersionImpl<1, 0, 0, 0, false>;
using ver4 = VersionImpl<1, 0, 0, 0, true>;
using ver5 = VersionImpl<1, 0, 0, 1, false>;
using ver6 = VersionImpl<1, 0, 0, 1, true>;
using ver7 = VersionImpl<2, 5, 10, 7, false>;
using ver8 = VersionImpl<2, 5, 10, 7, true>;

template <>
const char* ver1::soname_ = nullptr;
template <>
const char* ver2::soname_ = nullptr;
template <>
const char* ver3::soname_ = nullptr;
template <>
const char* ver4::soname_ = nullptr;
template <>
const char* ver5::soname_ = nullptr;
template <>
const char* ver6::soname_ = nullptr;
template <>
const char* ver7::soname_ = nullptr;
template <>
const char* ver8::soname_ = nullptr;

TEST(VersionString) {
#ifdef USE_SIMULATOR
  ver1::CheckVersion("0.0.0 SIMULATOR", "libv8-0.0.0.so");
  ver2::CheckVersion("0.0.0 (candidate) SIMULATOR", "libv8-0.0.0-candidate.so");
  ver3::CheckVersion("1.0.0 SIMULATOR", "libv8-1.0.0.so");
  ver4::CheckVersion("1.0.0 (candidate) SIMULATOR", "libv8-1.0.0-candidate.so");
  ver5::CheckVersion("1.0.0.1 SIMULATOR", "libv8-1.0.0.1.so");
  ver6::CheckVersion("1.0.0.1 (candidate) SIMULATOR",
                     "libv8-1.0.0.1-candidate.so");
  ver7::CheckVersion("2.5.10.7 SIMULATOR", "libv8-2.5.10.7.so");
  ver8::CheckVersion("2.5.10.7 (candidate) SIMULATOR",
                     "libv8-2.5.10.7-candidate.so");
#else
  ver1::CheckVersion("0.0.0", "libv8-0.0.0.so");
  ver2::CheckVersion("0.0.0 (candidate)", "libv8-0.0.0-candidate.so");
  ver3::CheckVersion("1.0.0", "libv8-1.0.0.so");
  ver4::CheckVersion("1.0.0 (candidate)", "libv8-1.0.0-candidate.so");
  ver5::CheckVersion("1.0.0.1", "libv8-1.0.0.1.so");
  ver6::CheckVersion("1.0.0.1 (candidate)", "libv8-1.0.0.1-candidate.so");
  ver7::CheckVersion("2.5.10.7", "libv8-2.5.10.7.so");
  ver8::CheckVersion("2.5.10.7 (candidate)", "libv8-2.5.10.7-candidate.so");
#endif
}
