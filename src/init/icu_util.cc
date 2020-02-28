// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/init/icu_util.h"

#if defined(_WIN32)
#include "src/base/win32-headers.h"
#endif

#if defined(V8_INTL_SUPPORT)
#include <stdio.h>
#include <stdlib.h>

#include "unicode/putil.h"
#include "unicode/udata.h"
#include "unicode/utrace.h"

#include "src/base/build_config.h"
#include "src/base/file-utils.h"

#define ICU_UTIL_DATA_FILE 0
#define ICU_UTIL_DATA_STATIC 1

#endif

namespace v8 {

namespace internal {

#if defined(V8_INTL_SUPPORT) && (ICU_UTIL_DATA_IMPL == ICU_UTIL_DATA_FILE)
namespace {
char* g_icu_data_ptr = nullptr;

void free_icu_data_ptr() { delete[] g_icu_data_ptr; }

}  // namespace
#endif

bool InitializeICUDefaultLocation(const char* exec_path,
                                  const char* icu_data_file) {
#if !defined(V8_INTL_SUPPORT)
  return true;
#elif ICU_UTIL_DATA_IMPL == ICU_UTIL_DATA_FILE
  if (icu_data_file) {
    return InitializeICU(icu_data_file);
  }
#if defined(V8_TARGET_LITTLE_ENDIAN)
  std::unique_ptr<char[]> icu_data_file_default =
      base::RelativePath(exec_path, "icudtl.dat");
#elif defined(V8_TARGET_BIG_ENDIAN)
  std::unique_ptr<char[]> icu_data_file_default =
      base::RelativePath(exec_path, "icudtb.dat");
#else
#error Unknown byte ordering
#endif
  return InitializeICU(icu_data_file_default.get());
#else
  return InitializeICU(nullptr);
#endif
}

static void U_CALLCONV traceData(const void* context, int32_t fnNumber,
                                 int32_t level, const char* fmt, va_list args) {
  if (UTRACE_UDATA_DATA_FILE != fnNumber) return;
  const char* name = va_arg(args, const char*);
  if (strchr(name, '-') == name + 8) name += 9;
  printf("%d %s\n", fnNumber, name);
  va_end(args);
}

void CommonInit() {
  const void* context = nullptr;
  utrace_setFunctions(context, nullptr, nullptr, traceData);
  utrace_setLevel(UTRACE_VERBOSE);
}

bool InitializeICU(const char* icu_data_file) {
#if !defined(V8_INTL_SUPPORT)
  return true;
#else
#if ICU_UTIL_DATA_IMPL == ICU_UTIL_DATA_STATIC
  // Use bundled ICU data.
  CommonInit();
  return true;
#elif ICU_UTIL_DATA_IMPL == ICU_UTIL_DATA_FILE
  if (!icu_data_file) return false;

  if (g_icu_data_ptr) {
    CommonInit();
    return true;
  }

  FILE* inf = fopen(icu_data_file, "rb");
  if (!inf) return false;

  fseek(inf, 0, SEEK_END);
  size_t size = ftell(inf);
  rewind(inf);

  g_icu_data_ptr = new char[size];
  if (fread(g_icu_data_ptr, 1, size, inf) != size) {
    delete[] g_icu_data_ptr;
    g_icu_data_ptr = nullptr;
    fclose(inf);
    return false;
  }
  fclose(inf);

  atexit(free_icu_data_ptr);

  UErrorCode err = U_ZERO_ERROR;
  udata_setCommonData(reinterpret_cast<void*>(g_icu_data_ptr), &err);
  // Never try to load ICU data from files.
  udata_setFileAccess(UDATA_ONLY_PACKAGES, &err);
  if (err == U_ZERO_ERROR) CommonInit();
  return err == U_ZERO_ERROR;
#endif
#endif
}

#undef ICU_UTIL_DATA_FILE
#undef ICU_UTIL_DATA_STATIC

}  // namespace internal
}  // namespace v8
