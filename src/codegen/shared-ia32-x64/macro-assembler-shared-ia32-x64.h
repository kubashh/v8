// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_SHARED_IA32_X64_MACRO_ASSEMBLER_SHARED_IA32_X64_H_
#define V8_CODEGEN_SHARED_IA32_X64_MACRO_ASSEMBLER_SHARED_IA32_X64_H_

#include "src/base/macros.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/turbo-assembler.h"

#if V8_TARGET_ARCH_IA32
#include "src/codegen/ia32/register-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "src/codegen/x64/register-x64.h"
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {
class Assembler;

// For WebAssembly we care about the full floating point register. If we are not
// running Wasm, we can get away with saving half of those registers.
#if V8_ENABLE_WEBASSEMBLY
constexpr int kStackSavedSavedFPSize = 2 * kDoubleSize;
#else
constexpr int kStackSavedSavedFPSize = kDoubleSize;
#endif  // V8_ENABLE_WEBASSEMBLY

// Common base class template shared by ia32 and x64 TurboAssembler. This uses
// the Curiously Recurring Template Pattern (CRTP), where Impl is the actual
// class (subclass of SharedTurboAssembler instantiated with the actual class).
// This allows static polymorphism, where member functions can be move into
// SharedTurboAssembler, and we can also call into member functions defined in
// ia32 or x64 specific TurboAssembler from within this template class.
template <typename Impl>
class V8_EXPORT_PRIVATE SharedTurboAssembler : public TurboAssemblerBase {
 public:
  using TurboAssemblerBase::TurboAssemblerBase;

  Impl* impl() { return static_cast<Impl*>(this); }

  void Move(Register dst, uint32_t src) { impl()->Move(dst, src); }

  // Move if registers are not identical.
  void Move(Register dst, Register src) { impl()->Move(dst, src); }

  void Add(Register dst, Immediate src) {
    // Helper to paper over the different assembler function names.
#if V8_TARGET_ARCH_IA32
    add(dst, src);
#elif V8_TARGET_ARCH_X64
    addq(dst, src);
#else
#error Unsupported target architecture.
#endif
  }

  void And(Register dst, Immediate src) {
    // Helper to paper over the different assembler function names.
#if V8_TARGET_ARCH_IA32
    and_(dst, src);
#elif V8_TARGET_ARCH_X64
    andq(dst, src);
#else
#error Unsupported target architecture.
#endif
  }

  void Movapd(XMMRegister dst, XMMRegister src) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vmovapd(dst, src);
    } else {
      // On SSE, movaps is 1 byte shorter than movapd, and has the same
      // behavior.
      movaps(dst, src);
    }
  }

  template <typename Dst, typename Src>
  void Movdqu(Dst dst, Src src) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vmovdqu(dst, src);
    } else {
      // movups is 1 byte shorter than movdqu. On most SSE systems, this incurs
      // no delay moving between integer and floating-point domain.
      movups(dst, src);
    }
  }

  // Shufps that will mov src1 into dst if AVX is not supported.
  void Shufps(XMMRegister dst, XMMRegister src1, XMMRegister src2,
              uint8_t imm8) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vshufps(dst, src1, src2, imm8);
    } else {
      if (dst != src1) {
        movaps(dst, src1);
      }
      shufps(dst, src2, imm8);
    }
  }

  // Helper struct to implement functions that check for AVX support and
  // dispatch to the appropriate AVX/SSE instruction.
  template <typename Dst, typename Arg, typename... Args>
  struct AvxHelper {
    Assembler* assm;
    base::Optional<CpuFeature> feature = base::nullopt;
    // Call a method where the AVX version expects the dst argument to be
    // duplicated.
    // E.g. Andps(x, y) -> vandps(x, x, y)
    //                  -> andps(x, y)
    template <void (Assembler::*avx)(Dst, Dst, Arg, Args...),
              void (Assembler::*no_avx)(Dst, Arg, Args...)>
    void emit(Dst dst, Arg arg, Args... args) {
      if (CpuFeatures::IsSupported(AVX)) {
        CpuFeatureScope scope(assm, AVX);
        (assm->*avx)(dst, dst, arg, args...);
      } else if (feature.has_value()) {
        DCHECK(CpuFeatures::IsSupported(*feature));
        CpuFeatureScope scope(assm, *feature);
        (assm->*no_avx)(dst, arg, args...);
      } else {
        (assm->*no_avx)(dst, arg, args...);
      }
    }

    // Call a method in the AVX form (one more operand), but if unsupported will
    // check that dst == first src.
    // E.g. Andps(x, y, z) -> vandps(x, y, z)
    //                     -> andps(x, z) and check that x == y
    template <void (Assembler::*avx)(Dst, Arg, Args...),
              void (Assembler::*no_avx)(Dst, Args...)>
    void emit(Dst dst, Arg arg, Args... args) {
      if (CpuFeatures::IsSupported(AVX)) {
        CpuFeatureScope scope(assm, AVX);
        (assm->*avx)(dst, arg, args...);
      } else if (feature.has_value()) {
        DCHECK_EQ(dst, arg);
        DCHECK(CpuFeatures::IsSupported(*feature));
        CpuFeatureScope scope(assm, *feature);
        (assm->*no_avx)(dst, args...);
      } else {
        DCHECK_EQ(dst, arg);
        (assm->*no_avx)(dst, args...);
      }
    }

    // Call a method where the AVX version expects no duplicated dst argument.
    // E.g. Movddup(x, y) -> vmovddup(x, y)
    //                    -> movddup(x, y)
    template <void (Assembler::*avx)(Dst, Arg, Args...),
              void (Assembler::*no_avx)(Dst, Arg, Args...)>
    void emit(Dst dst, Arg arg, Args... args) {
      if (CpuFeatures::IsSupported(AVX)) {
        CpuFeatureScope scope(assm, AVX);
        (assm->*avx)(dst, arg, args...);
      } else if (feature.has_value()) {
        DCHECK(CpuFeatures::IsSupported(*feature));
        CpuFeatureScope scope(assm, *feature);
        (assm->*no_avx)(dst, arg, args...);
      } else {
        (assm->*no_avx)(dst, arg, args...);
      }
    }
  };

#define AVX_OP(macro_name, name)                                        \
  template <typename Dst, typename Arg, typename... Args>               \
  void macro_name(Dst dst, Arg arg, Args... args) {                     \
    AvxHelper<Dst, Arg, Args...>{this}                                  \
        .template emit<&Assembler::v##name, &Assembler::name>(dst, arg, \
                                                              args...); \
  }

#define AVX_OP_SSE3(macro_name, name)                                    \
  template <typename Dst, typename Arg, typename... Args>                \
  void macro_name(Dst dst, Arg arg, Args... args) {                      \
    AvxHelper<Dst, Arg, Args...>{this, base::Optional<CpuFeature>(SSE3)} \
        .template emit<&Assembler::v##name, &Assembler::name>(dst, arg,  \
                                                              args...);  \
  }

#define AVX_OP_SSSE3(macro_name, name)                                    \
  template <typename Dst, typename Arg, typename... Args>                 \
  void macro_name(Dst dst, Arg arg, Args... args) {                       \
    AvxHelper<Dst, Arg, Args...>{this, base::Optional<CpuFeature>(SSSE3)} \
        .template emit<&Assembler::v##name, &Assembler::name>(dst, arg,   \
                                                              args...);   \
  }

#define AVX_OP_SSE4_1(macro_name, name)                                    \
  template <typename Dst, typename Arg, typename... Args>                  \
  void macro_name(Dst dst, Arg arg, Args... args) {                        \
    AvxHelper<Dst, Arg, Args...>{this, base::Optional<CpuFeature>(SSE4_1)} \
        .template emit<&Assembler::v##name, &Assembler::name>(dst, arg,    \
                                                              args...);    \
  }

#define AVX_OP_SSE4_2(macro_name, name)                                    \
  template <typename Dst, typename Arg, typename... Args>                  \
  void macro_name(Dst dst, Arg arg, Args... args) {                        \
    AvxHelper<Dst, Arg, Args...>{this, base::Optional<CpuFeature>(SSE4_2)} \
        .template emit<&Assembler::v##name, &Assembler::name>(dst, arg,    \
                                                              args...);    \
  }

  // Keep this list sorted by required extension, then instruction name.
  AVX_OP(Addpd, addpd)
  AVX_OP(Addps, addps)
  AVX_OP(Andnpd, andnpd)
  AVX_OP(Andnps, andnps)
  AVX_OP(Andpd, andpd)
  AVX_OP(Andps, andps)
  AVX_OP(Cmpeqpd, cmpeqpd)
  AVX_OP(Cmplepd, cmplepd)
  AVX_OP(Cmpleps, cmpleps)
  AVX_OP(Cmpltpd, cmpltpd)
  AVX_OP(Cmpneqpd, cmpneqpd)
  AVX_OP(Cmpunordpd, cmpunordpd)
  AVX_OP(Cmpunordps, cmpunordps)
  AVX_OP(Cvtdq2pd, cvtdq2pd)
  AVX_OP(Cvtdq2ps, cvtdq2ps)
  AVX_OP(Cvtpd2ps, cvtpd2ps)
  AVX_OP(Cvtps2pd, cvtps2pd)
  AVX_OP(Cvttps2dq, cvttps2dq)
  AVX_OP(Divpd, divpd)
  AVX_OP(Divps, divps)
  AVX_OP(Maxpd, maxpd)
  AVX_OP(Maxps, maxps)
  AVX_OP(Minpd, minpd)
  AVX_OP(Minps, minps)
  AVX_OP(Movaps, movaps)
  AVX_OP(Movd, movd)
  AVX_OP(Movhlps, movhlps)
  AVX_OP(Movhps, movhps)
  AVX_OP(Movlps, movlps)
  AVX_OP(Movmskpd, movmskpd)
  AVX_OP(Movmskps, movmskps)
  AVX_OP(Movsd, movsd)
  AVX_OP(Movss, movss)
  AVX_OP(Movupd, movupd)
  AVX_OP(Movups, movups)
  AVX_OP(Mulpd, mulpd)
  AVX_OP(Mulps, mulps)
  AVX_OP(Orpd, orpd)
  AVX_OP(Orps, orps)
  AVX_OP(Packssdw, packssdw)
  AVX_OP(Packsswb, packsswb)
  AVX_OP(Packuswb, packuswb)
  AVX_OP(Paddb, paddb)
  AVX_OP(Paddd, paddd)
  AVX_OP(Paddq, paddq)
  AVX_OP(Paddsb, paddsb)
  AVX_OP(Paddusb, paddusb)
  AVX_OP(Paddusw, paddusw)
  AVX_OP(Paddw, paddw)
  AVX_OP(Pand, pand)
  AVX_OP(Pavgb, pavgb)
  AVX_OP(Pavgw, pavgw)
  AVX_OP(Pcmpgtb, pcmpgtb)
  AVX_OP(Pcmpeqd, pcmpeqd)
  AVX_OP(Pmaxub, pmaxub)
  AVX_OP(Pminub, pminub)
  AVX_OP(Pmovmskb, pmovmskb)
  AVX_OP(Pmullw, pmullw)
  AVX_OP(Pmuludq, pmuludq)
  AVX_OP(Por, por)
  AVX_OP(Pshufd, pshufd)
  AVX_OP(Pshufhw, pshufhw)
  AVX_OP(Pshuflw, pshuflw)
  AVX_OP(Pslld, pslld)
  AVX_OP(Psllq, psllq)
  AVX_OP(Psllw, psllw)
  AVX_OP(Psrad, psrad)
  AVX_OP(Psraw, psraw)
  AVX_OP(Psrld, psrld)
  AVX_OP(Psrlq, psrlq)
  AVX_OP(Psrlw, psrlw)
  AVX_OP(Psubb, psubb)
  AVX_OP(Psubd, psubd)
  AVX_OP(Psubq, psubq)
  AVX_OP(Psubsb, psubsb)
  AVX_OP(Psubusb, psubusb)
  AVX_OP(Psubw, psubw)
  AVX_OP(Punpckhbw, punpckhbw)
  AVX_OP(Punpckhdq, punpckhdq)
  AVX_OP(Punpckhqdq, punpckhqdq)
  AVX_OP(Punpckhwd, punpckhwd)
  AVX_OP(Punpcklbw, punpcklbw)
  AVX_OP(Punpckldq, punpckldq)
  AVX_OP(Punpcklqdq, punpcklqdq)
  AVX_OP(Punpcklwd, punpcklwd)
  AVX_OP(Pxor, pxor)
  AVX_OP(Rcpps, rcpps)
  AVX_OP(Rsqrtps, rsqrtps)
  AVX_OP(Sqrtpd, sqrtpd)
  AVX_OP(Sqrtps, sqrtps)
  AVX_OP(Sqrtsd, sqrtsd)
  AVX_OP(Sqrtss, sqrtss)
  AVX_OP(Subpd, subpd)
  AVX_OP(Subps, subps)
  AVX_OP(Unpcklps, unpcklps)
  AVX_OP(Xorpd, xorpd)
  AVX_OP(Xorps, xorps)

  AVX_OP_SSE3(Haddps, haddps)
  AVX_OP_SSE3(Movddup, movddup)
  AVX_OP_SSE3(Movshdup, movshdup)

  AVX_OP_SSSE3(Pabsb, pabsb)
  AVX_OP_SSSE3(Pabsd, pabsd)
  AVX_OP_SSSE3(Pabsw, pabsw)
  AVX_OP_SSSE3(Palignr, palignr)
  AVX_OP_SSSE3(Psignb, psignb)
  AVX_OP_SSSE3(Psignd, psignd)
  AVX_OP_SSSE3(Psignw, psignw)

  AVX_OP_SSE4_1(Extractps, extractps)
  AVX_OP_SSE4_1(Pblendw, pblendw)
  AVX_OP_SSE4_1(Pextrb, pextrb)
  AVX_OP_SSE4_1(Pextrw, pextrw)
  AVX_OP_SSE4_1(Pmaxsb, pmaxsb)
  AVX_OP_SSE4_1(Pmaxsd, pmaxsd)
  AVX_OP_SSE4_1(Pminsb, pminsb)
  AVX_OP_SSE4_1(Pmovsxbw, pmovsxbw)
  AVX_OP_SSE4_1(Pmovsxdq, pmovsxdq)
  AVX_OP_SSE4_1(Pmovsxwd, pmovsxwd)
  AVX_OP_SSE4_1(Pmovzxbw, pmovzxbw)
  AVX_OP_SSE4_1(Pmovzxdq, pmovzxdq)
  AVX_OP_SSE4_1(Pmovzxwd, pmovzxwd)
  AVX_OP_SSE4_1(Ptest, ptest)
  AVX_OP_SSE4_1(Roundpd, roundpd)
  AVX_OP_SSE4_1(Roundps, roundps)

  void F64x2ExtractLane(DoubleRegister dst, XMMRegister src, uint8_t lane) {
    if (lane == 0) {
      if (dst != src) {
        Movaps(dst, src);
      }
    } else {
      DCHECK_EQ(1, lane);
      if (CpuFeatures::IsSupported(AVX)) {
        CpuFeatureScope avx_scope(this, AVX);
        // Pass src as operand to avoid false-dependency on dst.
        vmovhlps(dst, src, src);
      } else {
        movhlps(dst, src);
      }
    }
  }

  void F64x2ReplaceLane(XMMRegister dst, XMMRegister src, DoubleRegister rep,
                        uint8_t lane) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope scope(this, AVX);
      if (lane == 0) {
        vmovsd(dst, src, rep);
      } else {
        vmovlhps(dst, src, rep);
      }
    } else {
      CpuFeatureScope scope(this, SSE4_1);
      if (dst != src) {
        DCHECK_NE(dst, rep);  // Ensure rep is not overwritten.
        movaps(dst, src);
      }
      if (lane == 0) {
        movsd(dst, rep);
      } else {
        movlhps(dst, rep);
      }
    }
  }

  void F64x2Min(XMMRegister dst, XMMRegister lhs, XMMRegister rhs,
                XMMRegister scratch) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope scope(this, AVX);
      // The minpd instruction doesn't propagate NaNs and +0's in its first
      // operand. Perform minpd in both orders, merge the resuls, and adjust.
      vminpd(scratch, lhs, rhs);
      vminpd(dst, rhs, lhs);
      // propagate -0's and NaNs, which may be non-canonical.
      vorpd(scratch, scratch, dst);
      // Canonicalize NaNs by quieting and clearing the payload.
      vcmpunordpd(dst, dst, scratch);
      vorpd(scratch, scratch, dst);
      vpsrlq(dst, dst, byte{13});
      vandnpd(dst, dst, scratch);
    } else {
      // Compare lhs with rhs, and rhs with lhs, and have the results in scratch
      // and dst. If dst overlaps with lhs or rhs, we can save a move.
      if (dst == lhs || dst == rhs) {
        XMMRegister src = dst == lhs ? rhs : lhs;
        movaps(scratch, src);
        minpd(scratch, dst);
        minpd(dst, src);
      } else {
        movaps(scratch, lhs);
        movaps(dst, rhs);
        minpd(scratch, rhs);
        minpd(dst, lhs);
      }
      orpd(scratch, dst);
      cmpunordpd(dst, scratch);
      orpd(scratch, dst);
      psrlq(dst, byte{13});
      andnpd(dst, scratch);
    }
  }

  void F64x2Max(XMMRegister dst, XMMRegister lhs, XMMRegister rhs,
                XMMRegister scratch) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope scope(this, AVX);
      // The maxpd instruction doesn't propagate NaNs and +0's in its first
      // operand. Perform maxpd in both orders, merge the resuls, and adjust.
      vmaxpd(scratch, lhs, rhs);
      vmaxpd(dst, rhs, lhs);
      // Find discrepancies.
      vxorpd(dst, dst, scratch);
      // Propagate NaNs, which may be non-canonical.
      vorpd(scratch, scratch, dst);
      // Propagate sign discrepancy and (subtle) quiet NaNs.
      vsubpd(scratch, scratch, dst);
      // Canonicalize NaNs by clearing the payload. Sign is non-deterministic.
      vcmpunordpd(dst, dst, scratch);
      vpsrlq(dst, dst, byte{13});
      vandnpd(dst, dst, scratch);
    } else {
      if (dst == lhs || dst == rhs) {
        XMMRegister src = dst == lhs ? rhs : lhs;
        movaps(scratch, src);
        maxpd(scratch, dst);
        maxpd(dst, src);
      } else {
        movaps(scratch, lhs);
        movaps(dst, rhs);
        maxpd(scratch, rhs);
        maxpd(dst, lhs);
      }
      xorpd(dst, scratch);
      orpd(scratch, dst);
      subpd(scratch, dst);
      cmpunordpd(dst, scratch);
      psrlq(dst, byte{13});
      andnpd(dst, scratch);
    }
  }

  void F32x4Splat(XMMRegister dst, DoubleRegister src) {
    if (CpuFeatures::IsSupported(AVX2)) {
      CpuFeatureScope avx2_scope(this, AVX2);
      vbroadcastss(dst, src);
    } else if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vshufps(dst, src, src, 0);
    } else {
      if (dst == src) {
        // 1 byte shorter than pshufd.
        shufps(dst, src, 0);
      } else {
        pshufd(dst, src, 0);
      }
    }
  }

  void F32x4ExtractLane(FloatRegister dst, XMMRegister src, uint8_t lane) {
    DCHECK_LT(lane, 4);
    // These instructions are shorter than insertps, but will leave junk in
    // the top lanes of dst.
    if (lane == 0) {
      if (dst != src) {
        Movaps(dst, src);
      }
    } else if (lane == 1) {
      Movshdup(dst, src);
    } else if (lane == 2 && dst == src) {
      // Check dst == src to avoid false dependency on dst.
      Movhlps(dst, src);
    } else if (dst == src) {
      Shufps(dst, src, src, lane);
    } else {
      Pshufd(dst, src, lane);
    }
  }

  void S128Store32Lane(Operand dst, XMMRegister src, uint8_t laneidx) {
    if (laneidx == 0) {
      Movss(dst, src);
    } else {
      DCHECK_GE(3, laneidx);
      Extractps(dst, src, laneidx);
    }
  }

  void I8x16Shl(XMMRegister dst, XMMRegister src1, uint8_t src2, Register tmp1,
                XMMRegister tmp2) {
    DCHECK_NE(dst, tmp2);
    // Perform 16-bit shift, then mask away low bits.
    if (!CpuFeatures::IsSupported(AVX) && (dst != src1)) {
      movaps(dst, src1);
      src1 = dst;
    }

    uint8_t shift = truncate_to_int3(src2);
    Psllw(dst, src1, byte{shift});

    uint8_t bmask = static_cast<uint8_t>(0xff << shift);
    uint32_t mask = bmask << 24 | bmask << 16 | bmask << 8 | bmask;
    Move(tmp1, mask);
    Movd(tmp2, tmp1);
    Pshufd(tmp2, tmp2, uint8_t{0});
    Pand(dst, tmp2);
  }

  void I8x16Shl(XMMRegister dst, XMMRegister src1, Register src2, Register tmp1,
                XMMRegister tmp2, XMMRegister tmp3) {
    DCHECK(!AreAliased(dst, tmp2, tmp3));
    DCHECK(!AreAliased(src1, tmp2, tmp3));

    // Take shift value modulo 8.
    Move(tmp1, src2);
    And(tmp1, Immediate(7));
    Add(tmp1, Immediate(8));
    // Create a mask to unset high bits.
    Movd(tmp3, tmp1);
    Pcmpeqd(tmp2, tmp2);
    Psrlw(tmp2, tmp2, tmp3);
    Packuswb(tmp2, tmp2);
    if (!CpuFeatures::IsSupported(AVX) && (dst != src1)) {
      movaps(dst, src1);
      src1 = dst;
    }
    // Mask off the unwanted bits before word-shifting.
    Pand(dst, src1, tmp2);
    Add(tmp1, Immediate(-8));
    Movd(tmp3, tmp1);
    Psllw(dst, dst, tmp3);
  }

  void I8x16ShrS(XMMRegister dst, XMMRegister src1, uint8_t src2,
                 XMMRegister tmp) {
    // Unpack bytes into words, do word (16-bit) shifts, and repack.
    DCHECK_NE(dst, tmp);
    uint8_t shift = truncate_to_int3(src2) + 8;

    Punpckhbw(tmp, src1);
    Punpcklbw(dst, src1);
    Psraw(tmp, shift);
    Psraw(dst, shift);
    Packsswb(dst, tmp);
  }

  void I8x16ShrS(XMMRegister dst, XMMRegister src1, Register src2,
                 Register tmp1, XMMRegister tmp2, XMMRegister tmp3) {
    DCHECK(!AreAliased(dst, tmp2, tmp3));
    DCHECK_NE(src1, tmp2);

    // Unpack the bytes into words, do arithmetic shifts, and repack.
    Punpckhbw(tmp2, src1);
    Punpcklbw(dst, src1);
    // Prepare shift value
    Move(tmp1, src2);
    // Take shift value modulo 8.
    And(tmp1, Immediate(7));
    Add(tmp1, Immediate(8));
    Movd(tmp3, tmp1);
    Psraw(tmp2, tmp3);
    Psraw(dst, tmp3);
    Packsswb(dst, tmp2);
  }

  void I8x16ShrU(XMMRegister dst, XMMRegister src1, uint8_t src2, Register tmp1,
                 XMMRegister tmp2) {
    DCHECK_NE(dst, tmp2);
    if (!CpuFeatures::IsSupported(AVX) && (dst != src1)) {
      movaps(dst, src1);
      src1 = dst;
    }

    // Perform 16-bit shift, then mask away high bits.
    uint8_t shift = truncate_to_int3(src2);
    Psrlw(dst, src1, shift);

    uint8_t bmask = 0xff >> shift;
    uint32_t mask = bmask << 24 | bmask << 16 | bmask << 8 | bmask;
    Move(tmp1, mask);
    Movd(tmp2, tmp1);
    Pshufd(tmp2, tmp2, byte{0});
    Pand(dst, tmp2);
  }

  void I8x16ShrU(XMMRegister dst, XMMRegister src1, Register src2,
                 Register tmp1, XMMRegister tmp2, XMMRegister tmp3) {
    DCHECK(!AreAliased(dst, tmp2, tmp3));
    DCHECK_NE(src1, tmp2);

    // Unpack the bytes into words, do logical shifts, and repack.
    Punpckhbw(tmp2, src1);
    Punpcklbw(dst, src1);
    // Prepare shift value.
    Move(tmp1, src2);
    // Take shift value modulo 8.
    And(tmp1, Immediate(7));
    Add(tmp1, Immediate(8));
    Movd(tmp3, tmp1);
    Psrlw(tmp2, tmp3);
    Psrlw(dst, tmp3);
    Packuswb(dst, tmp2);
  }

  void I16x8ExtMulLow(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                      XMMRegister scratch, bool is_signed) {
    is_signed ? Pmovsxbw(scratch, src1) : Pmovzxbw(scratch, src1);
    is_signed ? Pmovsxbw(dst, src2) : Pmovzxbw(dst, src2);
    Pmullw(dst, scratch);
  }

  void I16x8ExtMulHighS(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                        XMMRegister scratch) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vpunpckhbw(scratch, src1, src1);
      vpsraw(scratch, scratch, 8);
      vpunpckhbw(dst, src2, src2);
      vpsraw(dst, dst, 8);
      vpmullw(dst, dst, scratch);
    } else {
      if (dst != src1) {
        movaps(dst, src1);
      }
      movaps(scratch, src2);
      punpckhbw(dst, dst);
      psraw(dst, 8);
      punpckhbw(scratch, scratch);
      psraw(scratch, 8);
      pmullw(dst, scratch);
    }
  }

  void I16x8ExtMulHighU(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                        XMMRegister scratch) {
    // The logic here is slightly complicated to handle all the cases of
    // register aliasing. This allows flexibility for callers in TurboFan and
    // Liftoff.
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      if (src1 == src2) {
        vpxor(scratch, scratch, scratch);
        vpunpckhbw(dst, src1, scratch);
        vpmullw(dst, dst, dst);
      } else {
        if (dst == src2) {
          // We overwrite dst, then use src2, so swap src1 and src2.
          std::swap(src1, src2);
        }
        vpxor(scratch, scratch, scratch);
        vpunpckhbw(dst, src1, scratch);
        vpunpckhbw(scratch, src2, scratch);
        vpmullw(dst, dst, scratch);
      }
    } else {
      if (src1 == src2) {
        xorps(scratch, scratch);
        if (dst != src1) {
          movaps(dst, src1);
        }
        punpckhbw(dst, scratch);
        pmullw(dst, scratch);
      } else {
        // When dst == src1, nothing special needs to be done.
        // When dst == src2, swap src1 and src2, since we overwrite dst.
        // When dst is unique, copy src1 to dst first.
        if (dst == src2) {
          std::swap(src1, src2);
          // Now, dst == src1.
        } else if (dst != src1) {
          // dst != src1 && dst != src2.
          movaps(dst, src1);
        }
        xorps(scratch, scratch);
        punpckhbw(dst, scratch);
        punpckhbw(scratch, src2);
        psrlw(scratch, 8);
        pmullw(dst, scratch);
      }
    }
  }

  void I16x8SConvertI8x16High(XMMRegister dst, XMMRegister src) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      // src = |a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p| (high)
      // dst = |i|i|j|j|k|k|l|l|m|m|n|n|o|o|p|p|
      vpunpckhbw(dst, src, src);
      vpsraw(dst, dst, 8);
    } else {
      CpuFeatureScope sse_scope(this, SSE4_1);
      if (dst == src) {
        // 2 bytes shorter than pshufd, but has depdency on dst.
        movhlps(dst, src);
        pmovsxbw(dst, dst);
      } else {
        // No dependency on dst.
        pshufd(dst, src, 0xEE);
        pmovsxbw(dst, dst);
      }
    }
  }

  void I16x8UConvertI8x16High(XMMRegister dst, XMMRegister src,
                              XMMRegister scratch) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      // tmp = |0|0|0|0|0|0|0|0 | 0|0|0|0|0|0|0|0|
      // src = |a|b|c|d|e|f|g|h | i|j|k|l|m|n|o|p|
      // dst = |0|a|0|b|0|c|0|d | 0|e|0|f|0|g|0|h|
      XMMRegister tmp = dst == src ? scratch : dst;
      vpxor(tmp, tmp, tmp);
      vpunpckhbw(dst, src, tmp);
    } else {
      CpuFeatureScope sse_scope(this, SSE4_1);
      if (dst == src) {
        // xorps can be executed on more ports than pshufd.
        xorps(scratch, scratch);
        punpckhbw(dst, scratch);
      } else {
        // No dependency on dst.
        pshufd(dst, src, 0xEE);
        pmovzxbw(dst, dst);
      }
    }
  }

  // Requires that dst == src1 if AVX is not supported.
  // 1. Multiply low word into scratch.
  // 2. Multiply high word (can be signed or unsigned) into dst.
  // 3. Unpack and interleave scratch and dst into dst.
  void I32x4ExtMul(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                   XMMRegister scratch, bool low, bool is_signed) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vpmullw(scratch, src1, src2);
      is_signed ? vpmulhw(dst, src1, src2) : vpmulhuw(dst, src1, src2);
      low ? vpunpcklwd(dst, scratch, dst) : vpunpckhwd(dst, scratch, dst);
    } else {
      DCHECK_EQ(dst, src1);
      movaps(scratch, src1);
      pmullw(dst, src2);
      is_signed ? pmulhw(scratch, src2) : pmulhuw(scratch, src2);
      low ? punpcklwd(dst, scratch) : punpckhwd(dst, scratch);
    }
  }

  // Requires dst == src if AVX is not supported.
  void I32x4SConvertF32x4(XMMRegister dst, XMMRegister src,
                          XMMRegister scratch) {
    // Convert NAN to 0.
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope scope(this, AVX);
      vcmpeqps(scratch, src, src);
      vpand(dst, src, scratch);
    } else {
      movaps(scratch, src);
      cmpeqps(scratch, src);
      if (dst != src) movaps(dst, src);
      andps(dst, scratch);
    }

    // Set top bit if >= 0 (but not -0.0!).
    Pxor(scratch, dst);
    // Convert to packed single-precision.
    Cvttps2dq(dst, dst);
    // Set top bit if >=0 is now < 0.
    Pand(scratch, dst);
    Psrad(scratch, scratch, byte{31});
    // Set positive overflow lanes to 0x7FFFFFFF.
    Pxor(dst, scratch);
  }

  void I32x4SConvertI16x8High(XMMRegister dst, XMMRegister src) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      // src = |a|b|c|d|e|f|g|h| (high)
      // dst = |e|e|f|f|g|g|h|h|
      vpunpckhwd(dst, src, src);
      vpsrad(dst, dst, 16);
    } else {
      CpuFeatureScope sse_scope(this, SSE4_1);
      if (dst == src) {
        // 2 bytes shorter than pshufd, but has depdency on dst.
        movhlps(dst, src);
        pmovsxwd(dst, dst);
      } else {
        // No dependency on dst.
        pshufd(dst, src, 0xEE);
        pmovsxwd(dst, dst);
      }
    }
  }

  void I32x4UConvertI16x8High(XMMRegister dst, XMMRegister src,
                              XMMRegister scratch) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      // scratch = |0|0|0|0|0|0|0|0|
      // src     = |a|b|c|d|e|f|g|h|
      // dst     = |0|a|0|b|0|c|0|d|
      XMMRegister tmp = dst == src ? scratch : dst;
      vpxor(tmp, tmp, tmp);
      vpunpckhwd(dst, src, tmp);
    } else {
      if (dst == src) {
        // xorps can be executed on more ports than pshufd.
        xorps(scratch, scratch);
        punpckhwd(dst, scratch);
      } else {
        CpuFeatureScope sse_scope(this, SSE4_1);
        // No dependency on dst.
        pshufd(dst, src, 0xEE);
        pmovzxwd(dst, dst);
      }
    }
  }

  void I64x2Neg(XMMRegister dst, XMMRegister src, XMMRegister scratch) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope scope(this, AVX);
      vpxor(scratch, scratch, scratch);
      vpsubq(dst, scratch, src);
    } else {
      if (dst == src) {
        movaps(scratch, src);
        std::swap(src, scratch);
      }
      pxor(dst, dst);
      psubq(dst, src);
    }
  }

  void I64x2Abs(XMMRegister dst, XMMRegister src, XMMRegister scratch) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      XMMRegister tmp = dst == src ? scratch : dst;
      vpxor(tmp, tmp, tmp);
      vpsubq(tmp, tmp, src);
      vblendvpd(dst, src, tmp, src);
    } else {
      CpuFeatureScope sse_scope(this, SSE3);
      movshdup(scratch, src);
      if (dst != src) {
        movaps(dst, src);
      }
      psrad(scratch, 31);
      xorps(dst, scratch);
      psubq(dst, scratch);
    }
  }

  void I64x2GtS(XMMRegister dst, XMMRegister src0, XMMRegister src1,
                XMMRegister scratch) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vpcmpgtq(dst, src0, src1);
    } else if (CpuFeatures::IsSupported(SSE4_2)) {
      CpuFeatureScope sse_scope(this, SSE4_2);
      DCHECK_EQ(dst, src0);
      pcmpgtq(dst, src1);
    } else {
      CpuFeatureScope sse_scope(this, SSE3);
      DCHECK_NE(dst, src0);
      DCHECK_NE(dst, src1);
      movaps(dst, src1);
      movaps(scratch, src0);
      psubq(dst, src0);
      pcmpeqd(scratch, src1);
      andps(dst, scratch);
      movaps(scratch, src0);
      pcmpgtd(scratch, src1);
      orps(dst, scratch);
      movshdup(dst, dst);
    }
  }

  void I64x2GeS(XMMRegister dst, XMMRegister src0, XMMRegister src1,
                XMMRegister scratch) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vpcmpgtq(dst, src1, src0);
      vpcmpeqd(scratch, scratch, scratch);
      vpxor(dst, dst, scratch);
    } else if (CpuFeatures::IsSupported(SSE4_2)) {
      CpuFeatureScope sse_scope(this, SSE4_2);
      DCHECK_NE(dst, src0);
      if (dst != src1) {
        movaps(dst, src1);
      }
      pcmpgtq(dst, src0);
      pcmpeqd(scratch, scratch);
      xorps(dst, scratch);
    } else {
      CpuFeatureScope sse_scope(this, SSE3);
      DCHECK_NE(dst, src0);
      DCHECK_NE(dst, src1);
      movaps(dst, src0);
      movaps(scratch, src1);
      psubq(dst, src1);
      pcmpeqd(scratch, src0);
      andps(dst, scratch);
      movaps(scratch, src1);
      pcmpgtd(scratch, src0);
      orps(dst, scratch);
      movshdup(dst, dst);
      pcmpeqd(scratch, scratch);
      xorps(dst, scratch);
    }
  }

  void I64x2ShrS(XMMRegister dst, XMMRegister src, uint8_t shift,
                 XMMRegister xmm_tmp) {
    DCHECK_GT(64, shift);
    DCHECK_NE(xmm_tmp, dst);
    DCHECK_NE(xmm_tmp, src);
    // Use logical right shift to emulate arithmetic right shifts:
    // Given:
    // signed >> c
    //   == (signed + 2^63 - 2^63) >> c
    //   == ((signed + 2^63) >> c) - (2^63 >> c)
    //                                ^^^^^^^^^
    //                                 xmm_tmp
    // signed + 2^63 is an unsigned number, so we can use logical right shifts.

    // xmm_tmp = wasm_i64x2_const(0x80000000'00000000).
    Pcmpeqd(xmm_tmp, xmm_tmp);
    Psllq(xmm_tmp, byte{63});

    if (!CpuFeatures::IsSupported(AVX) && (dst != src)) {
      Movapd(dst, src);
      src = dst;
    }
    // Add a bias of 2^63 to convert signed to unsigned.
    // Since only highest bit changes, use pxor instead of paddq.
    Pxor(dst, src, xmm_tmp);
    // Logically shift both value and bias.
    Psrlq(dst, shift);
    Psrlq(xmm_tmp, shift);
    // Subtract shifted bias to convert back to signed value.
    Psubq(dst, xmm_tmp);
  }

  void I64x2ShrS(XMMRegister dst, XMMRegister src, Register shift,
                 XMMRegister xmm_tmp, XMMRegister xmm_shift,
                 Register tmp_shift) {
    DCHECK_NE(xmm_tmp, dst);
    DCHECK_NE(xmm_tmp, src);
    DCHECK_NE(xmm_shift, dst);
    DCHECK_NE(xmm_shift, src);
    // tmp_shift can alias shift since we don't use shift after masking it.

    // See I64x2ShrS with constant shift for explanation of this algorithm.
    Pcmpeqd(xmm_tmp, xmm_tmp);
    Psllq(xmm_tmp, byte{63});

    // Shift modulo 64.
    Move(tmp_shift, shift);
    And(shift, Immediate(0x3F));
    Movd(xmm_shift, shift);

    if (!CpuFeatures::IsSupported(AVX) && (dst != src)) {
      Movapd(dst, src);
      src = dst;
    }
    Pxor(dst, src, xmm_tmp);
    Psrlq(dst, xmm_shift);
    Psrlq(xmm_tmp, xmm_shift);
    Psubq(dst, xmm_tmp);
  }

  // 1. Unpack src0, src1 into even-number elements of scratch.
  // 2. Unpack src1, src0 into even-number elements of dst.
  // 3. Multiply 1. with 2.
  // For non-AVX, use non-destructive pshufd instead of punpckldq/punpckhdq.
  void I64x2ExtMul(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                   XMMRegister scratch, bool low, bool is_signed) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      if (low) {
        vpunpckldq(scratch, src1, src1);
        vpunpckldq(dst, src2, src2);
      } else {
        vpunpckhdq(scratch, src1, src1);
        vpunpckhdq(dst, src2, src2);
      }
      if (is_signed) {
        vpmuldq(dst, scratch, dst);
      } else {
        vpmuludq(dst, scratch, dst);
      }
    } else {
      uint8_t mask = low ? 0x50 : 0xFA;
      pshufd(scratch, src1, mask);
      pshufd(dst, src2, mask);
      if (is_signed) {
        CpuFeatureScope sse4_scope(this, SSE4_1);
        pmuldq(dst, scratch);
      } else {
        pmuludq(dst, scratch);
      }
    }
  }

  void I64x2SConvertI32x4High(XMMRegister dst, XMMRegister src) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vpunpckhqdq(dst, src, src);
      vpmovsxdq(dst, dst);
    } else {
      CpuFeatureScope sse_scope(this, SSE4_1);
      if (dst == src) {
        movhlps(dst, src);
      } else {
        pshufd(dst, src, 0xEE);
      }
      pmovsxdq(dst, dst);
    }
  }

  void I64x2UConvertI32x4High(XMMRegister dst, XMMRegister src,
                              XMMRegister scratch) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vpxor(scratch, scratch, scratch);
      vpunpckhdq(dst, src, scratch);
    } else {
      if (dst != src) {
        movaps(dst, src);
      }
      xorps(scratch, scratch);
      punpckhdq(dst, scratch);
    }
  }

  void S128Not(XMMRegister dst, XMMRegister src, XMMRegister scratch) {
    if (dst == src) {
      Pcmpeqd(scratch, scratch);
      Pxor(dst, scratch);
    } else {
      Pcmpeqd(dst, dst);
      Pxor(dst, src);
    }
  }

  // Requires dst == mask when AVX is not supported.
  void S128Select(XMMRegister dst, XMMRegister mask, XMMRegister src1,
                  XMMRegister src2, XMMRegister scratch) {
    // v128.select = v128.or(v128.and(v1, c), v128.andnot(v2, c)).
    // pandn(x, y) = !x & y, so we have to flip the mask and input.
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vpandn(scratch, mask, src2);
      vpand(dst, src1, mask);
      vpor(dst, dst, scratch);
    } else {
      DCHECK_EQ(dst, mask);
      // Use float ops as they are 1 byte shorter than int ops.
      movaps(scratch, mask);
      andnps(scratch, src2);
      andps(dst, src1);
      orps(dst, scratch);
    }
  }
};
}  // namespace internal
}  // namespace v8
#endif  // V8_CODEGEN_SHARED_IA32_X64_MACRO_ASSEMBLER_SHARED_IA32_X64_H_
