// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CRDTP_EXPORT_TEMPLATE_H_
#define V8_CRDTP_EXPORT_TEMPLATE_H_

// This file was copied from Chromium's base/export_template.h.

// Synopsis
//
// This header provides macros for using FOO_EXPORT macros with explicit
// template instantiation declarations and definitions.
// Generally, the FOO_EXPORT macros are used at declarations,
// and GCC requires them to be used at explicit instantiation declarations,
// but MSVC requires __declspec(dllexport) to be used at the explicit
// instantiation definitions instead.

// Usage
//
// In a header file, write:
//
//   extern template class V8_CRDTP_EXPORT_TEMPLATE_DECLARE(FOO_EXPORT)
//   foo<bar>;
//
// In a source file, write:
//
//   template class V8_CRDTP_EXPORT_TEMPLATE_DEFINE(FOO_EXPORT) foo<bar>;

// Implementation notes
//
// On Windows, when building the FOO library (that is, when FOO_EXPORT expands
// to __declspec(dllexport)), we want the two lines to expand to:
//
//     extern template class foo<bar>;
//     template class FOO_EXPORT foo<bar>;
//
// In all other cases (non-Windows, and Windows when using the FOO library (that
// is when FOO_EXPORT expands to __declspec(dllimport)), we want:
//
//     extern template class FOO_EXPORT foo<bar>;
//     template class foo<bar>;
//
// The implementation of this header uses some subtle macro semantics to
// detect what the provided FOO_EXPORT value was defined as and then
// to dispatch to appropriate macro definitions.  Unfortunately,
// MSVC's C preprocessor is rather non-compliant and requires special
// care to make it work.
//
// Issue 1.
//
//   #define F(x)
//   F()
//
// MSVC emits warning C4003 ("not enough actual parameters for macro
// 'F'), even though it's a valid macro invocation.  This affects the
// macros below that take just an "export" parameter, because export
// may be empty.
//
// As a workaround, we can add a dummy parameter and arguments:
//
//   #define F(x,_)
//   F(,)
//
// Issue 2.
//
//   #define F(x) G##x
//   #define Gj() ok
//   F(j())
//
// The correct replacement for "F(j())" is "ok", but MSVC replaces it
// with "Gj()".  As a workaround, we can pass the result to an
// identity macro to force MSVC to look for replacements again.  (This
// is why V8_CRDTP_EXPORT_TEMPLATE_STYLE_3 exists.)

#define V8_CRDTP_EXPORT_TEMPLATE_DECLARE(export) \
  V8_CRDTP_EXPORT_TEMPLATE_INVOKE(               \
      DECLARE, V8_CRDTP_EXPORT_TEMPLATE_STYLE(export, ), export)
#define V8_CRDTP_EXPORT_TEMPLATE_DEFINE(export) \
  V8_CRDTP_EXPORT_TEMPLATE_INVOKE(              \
      DEFINE, V8_CRDTP_EXPORT_TEMPLATE_STYLE(export, ), export)

// INVOKE is an internal helper macro to perform parameter replacements
// and token pasting to chain invoke another macro.  E.g.,
//     V8_CRDTP_EXPORT_TEMPLATE_INVOKE(DECLARE, DEFAULT, FOO_EXPORT)
// will export to call
//     V8_CRDTP_EXPORT_TEMPLATE_DECLARE_DEFAULT(FOO_EXPORT, )
// (but with FOO_EXPORT expanded too).
#define V8_CRDTP_EXPORT_TEMPLATE_INVOKE(which, style, export) \
  V8_CRDTP_EXPORT_TEMPLATE_INVOKE_2(which, style, export)
#define V8_CRDTP_EXPORT_TEMPLATE_INVOKE_2(which, style, export) \
  V8_CRDTP_EXPORT_TEMPLATE_##which##_##style(export, )

// Default style is to apply the FOO_EXPORT macro at declaration sites.
#define V8_CRDTP_EXPORT_TEMPLATE_DECLARE_DEFAULT(export, _) export
#define V8_CRDTP_EXPORT_TEMPLATE_DEFINE_DEFAULT(export, _)

// The "MSVC hack" style is used when FOO_EXPORT is defined
// as __declspec(dllexport), which MSVC requires to be used at
// definition sites instead.
#define V8_CRDTP_EXPORT_TEMPLATE_DECLARE_MSVC_HACK(export, _)
#define V8_CRDTP_EXPORT_TEMPLATE_DEFINE_MSVC_HACK(export, _) export

// V8_CRDTP_EXPORT_TEMPLATE_STYLE is an internal helper macro that identifies
// which export style needs to be used for the provided FOO_EXPORT macro
// definition.
// "", "__attribute__(...)", and "__declspec(dllimport)" are mapped
// to "DEFAULT"; while "__declspec(dllexport)" is mapped to "MSVC_HACK".
//
// It's implemented with token pasting to transform the __attribute__ and
// __declspec annotations into macro invocations.  E.g., if FOO_EXPORT is
// defined as "__declspec(dllimport)", it undergoes the following sequence of
// macro substitutions:
//     V8_CRDTP_EXPORT_TEMPLATE_STYLE(FOO_EXPORT, )
//     V8_CRDTP_EXPORT_TEMPLATE_STYLE_2(__declspec(dllimport), )
//     V8_CRDTP_EXPORT_TEMPLATE_STYLE_3(V8_CRDTP_EXPORT_TEMPLATE_STYLE_MATCH__declspec(dllimport))
//     V8_CRDTP_EXPORT_TEMPLATE_STYLE_MATCH__declspec(dllimport)
//     V8_CRDTP_EXPORT_TEMPLATE_STYLE_MATCH_DECLSPEC_dllimport
//     DEFAULT
#define V8_CRDTP_EXPORT_TEMPLATE_STYLE(export, _) \
  V8_CRDTP_EXPORT_TEMPLATE_STYLE_2(export, )
#define V8_CRDTP_EXPORT_TEMPLATE_STYLE_2(export, _) \
  V8_CRDTP_EXPORT_TEMPLATE_STYLE_3(                 \
      V8_CRDTP_EXPORT_TEMPLATE_STYLE_MATCH_foj3FJo5StF0OvIzl7oMxA##export)
#define V8_CRDTP_EXPORT_TEMPLATE_STYLE_3(style) style

// Internal helper macros for V8_CRDTP_EXPORT_TEMPLATE_STYLE.
//
// XXX: C++ reserves all identifiers containing "__" for the implementation,
// but "__attribute__" and "__declspec" already contain "__" and the token-paste
// operator can only add characters; not remove them.  To minimize the risk of
// conflict with implementations, we include "foj3FJo5StF0OvIzl7oMxA" (a random
// 128-bit string, encoded in Base64) in the macro name.
#define V8_CRDTP_EXPORT_TEMPLATE_STYLE_MATCH_foj3FJo5StF0OvIzl7oMxA DEFAULT
#define V8_CRDTP_EXPORT_TEMPLATE_STYLE_MATCH_foj3FJo5StF0OvIzl7oMxA__attribute__( \
    ...)                                                                          \
  DEFAULT
#define V8_CRDTP_EXPORT_TEMPLATE_STYLE_MATCH_foj3FJo5StF0OvIzl7oMxA__declspec( \
    arg)                                                                       \
  V8_CRDTP_EXPORT_TEMPLATE_STYLE_MATCH_DECLSPEC_##arg

// Internal helper macros for V8_CRDTP_EXPORT_TEMPLATE_STYLE.
#define V8_CRDTP_EXPORT_TEMPLATE_STYLE_MATCH_DECLSPEC_dllexport MSVC_HACK
#define V8_CRDTP_EXPORT_TEMPLATE_STYLE_MATCH_DECLSPEC_dllimport DEFAULT

// Sanity checks.
//
// V8_CRDTP_EXPORT_TEMPLATE_TEST uses the same macro invocation pattern as
// V8_CRDTP_EXPORT_TEMPLATE_DECLARE and V8_CRDTP_EXPORT_TEMPLATE_DEFINE do to
// check that they're working correctly.  When they're working correctly, the
// sequence of macro replacements should go something like:
//
//     V8_CRDTP_EXPORT_TEMPLATE_TEST(DEFAULT, __declspec(dllimport));
//
//     static_assert(V8_CRDTP_EXPORT_TEMPLATE_INVOKE(TEST_DEFAULT,
//         V8_CRDTP_EXPORT_TEMPLATE_STYLE(__declspec(dllimport), ),
//         __declspec(dllimport)), "__declspec(dllimport)");
//
//     static_assert(V8_CRDTP_EXPORT_TEMPLATE_INVOKE(TEST_DEFAULT,
//         DEFAULT, __declspec(dllimport)), "__declspec(dllimport)");
//
//     static_assert(V8_CRDTP_EXPORT_TEMPLATE_TEST_DEFAULT_DEFAULT(
//         __declspec(dllimport)), "__declspec(dllimport)");
//
//     static_assert(true, "__declspec(dllimport)");
//
// When they're not working correctly, a syntax error should occur instead.
#define V8_CRDTP_EXPORT_TEMPLATE_TEST(want, export)                       \
  static_assert(                                                          \
      V8_CRDTP_EXPORT_TEMPLATE_INVOKE(                                    \
          TEST_##want, V8_CRDTP_EXPORT_TEMPLATE_STYLE(export, ), export), \
      #export)
#define V8_CRDTP_EXPORT_TEMPLATE_TEST_DEFAULT_DEFAULT(...) true
#define V8_CRDTP_EXPORT_TEMPLATE_TEST_MSVC_HACK_MSVC_HACK(...) true

V8_CRDTP_EXPORT_TEMPLATE_TEST(DEFAULT, );
V8_CRDTP_EXPORT_TEMPLATE_TEST(DEFAULT, __attribute__((visibility("default"))));
V8_CRDTP_EXPORT_TEMPLATE_TEST(MSVC_HACK, __declspec(dllexport));
V8_CRDTP_EXPORT_TEMPLATE_TEST(DEFAULT, __declspec(dllimport));

#undef V8_CRDTP_EXPORT_TEMPLATE_TEST
#undef V8_CRDTP_EXPORT_TEMPLATE_TEST_DEFAULT_DEFAULT
#undef V8_CRDTP_EXPORT_TEMPLATE_TEST_MSVC_HACK_MSVC_HACK

#endif  // V8_CRDTP_EXPORT_TEMPLATE_H_
