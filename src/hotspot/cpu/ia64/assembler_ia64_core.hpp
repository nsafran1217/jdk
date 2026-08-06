/*
 * Copyright (c) 2026, IA-64 port contributors. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/*
 * PROVENANCE
 * ----------
 * The instruction-encoding functions in this file are derived from the IA-64
 * backend of Mozilla's SpiderMonkey JIT:
 *
 *     js/src/jit/ia64/AssemblerCore-ia64.h
 *     Copyright (C) 2026 Rene Rebe <rene@exactco.de>
 *     originally licensed MPL-2.0, relicensed for use here
 *
 * shipped in T2 SDE as package/web/firefox/up-next-jit-0030-ia64.patch.ia64.
 * The field layouts there were taken from the binutils IA-64 opcode tables
 * (opcodes/ia64-opc-*.c) and verified byte-for-byte against GNU as.
 *
 * Forms that HotSpot needs and SpiderMonkey did not (the spill/fill family,
 * mf, and the application-register moves) were derived here directly from
 * ia64-linux-gnu-as output; each carries its derivation in a comment. Deriving
 * them also re-validated the inherited layout: GNU as's encoding of
 * "mov ar.unat = r14" lands on x6b = 0x2a, exactly the value SpiderMonkey's
 * MovToArCcv uses, and its "mov r = ar.lc" decomposes into precisely the
 * general form that SpiderMonkey's hand-derived MovFromPfs constant encodes.
 */

#ifndef CPU_IA64_ASSEMBLER_IA64_CORE_HPP
#define CPU_IA64_ASSEMBLER_IA64_CORE_HPP

#include "utilities/debug.hpp"
#include "utilities/globalDefinitions.hpp"

// [IA64DOC] IA-64 instruction and bundle encoding
//
// IA-64 does not have a linear instruction stream: instructions are packed
// three at a time into 128-bit (16-byte) *bundles*:
//
//   bits   4:0    template
//   bits  45:5    slot 0   (41 bits)
//   bits  86:46   slot 1   (41 bits)
//   bits 127:87   slot 2   (41 bits)
//
// The 5-bit template selects, for each slot, which execution unit the
// instruction in it must belong to (M = memory, I = integer/shift, F = float,
// B = branch, L+X = the two-slot long-immediate form), and where the *stop
// bits* fall. A stop bit terminates an instruction group: instructions within a
// group must have no register dependencies, because the hardware issues them in
// parallel with no interlocks.
//
// A code generator could schedule aggressively and pack three instructions per
// bundle, but correctness comes first: this port emits one instruction per
// bundle, padding the unused slots with unit-appropriate nops and always
// setting the stop bit at the end of the bundle. That makes every instruction
// its own instruction group, so no dependency analysis is required and the
// emitted code is correct by construction. It costs 16 bytes per instruction;
// packing is a later optimisation that can be layered on without changing any
// caller (JIT-SCOPE.md phase 7).

namespace ia64 {

typedef uint64_t Insn;  // a 41-bit instruction, right-aligned

struct Bundle {
  uint64_t lo;
  uint64_t hi;
};

// Template encodings. The trailing underscore marks a stop bit at the end of
// the bundle; "_" inside the name marks an internal stop.
enum Template : uint8_t {
  tMII   = 0x00,
  tMII_  = 0x01,
  tMI_I  = 0x02,
  tMI_I_ = 0x03,
  tMLX   = 0x04,
  tMLX_  = 0x05,
  tMMI   = 0x08,
  tMMI_  = 0x09,
  tM_MI  = 0x0a,
  tM_MI_ = 0x0b,
  tMFI   = 0x0c,
  tMFI_  = 0x0d,
  tMMF   = 0x0e,
  tMMF_  = 0x0f,
  tMIB   = 0x10,
  tMIB_  = 0x11,
  tMBB   = 0x12,
  tMBB_  = 0x13,
  tBBB   = 0x16,
  tBBB_  = 0x17,
  tMMB   = 0x18,
  tMMB_  = 0x19,
  tMFB   = 0x1c,
  tMFB_  = 0x1d
};

// Pack three 41-bit slots and a template into a 16-byte bundle.
inline Bundle MakeBundle(Template tmpl, Insn s0, Insn s1, Insn s2) {
  const uint64_t kSlotMask = (uint64_t(1) << 41) - 1;
  s0 &= kSlotMask;
  s1 &= kSlotMask;
  s2 &= kSlotMask;

  Bundle b;
  // lo = template | slot0 << 5 | low 18 bits of slot1 << 46
  b.lo = uint64_t(tmpl & 0x1f) | (s0 << 5) | (s1 << 46);
  // hi = high 23 bits of slot1 | slot2 << 23
  b.hi = (s1 >> 18) | (s2 << 23);
  return b;
}

// ---------------------------------------------------------------------------
// Instruction field helpers. Positions per binutils opcodes/ia64-opc-*.c.
// ---------------------------------------------------------------------------

inline Insn fOp(uint32_t x) { return Insn(x & 0xf) << 37; }
inline Insn fQp(uint32_t x) { return Insn(x & 0x3f); }

// Only r0-r39 exist in this port's register model: r0-r31 static, r32-r39 the
// out0-out7 window sized by call_stub's `alloc`. Encoding anything else names a
// stacked register outside the current frame, and IA-64 raises an Illegal
// Operation fault (SIGILL/ILL_ILLOPC) rather than faulting at the point of the
// mistake. See FRAME-DESIGN.md section 1.
inline void AssertGpr(uint32_t x) { assert(x < 40, "invalid GR encoding"); }

inline Insn fR1(uint32_t x) { AssertGpr(x); return Insn(x & 0x7f) << 6; }
inline Insn fR2(uint32_t x) { AssertGpr(x); return Insn(x & 0x7f) << 13; }
inline Insn fR3(uint32_t x) { AssertGpr(x); return Insn(x & 0x7f) << 20; }

// The same three positions, without the GR range check -- used where the field
// holds something that is not a general register (an application-register
// number, an FP register, a permute type).
inline Insn fSlot1(uint32_t x) { return Insn(x & 0x7f) << 6; }
inline Insn fSlot2(uint32_t x) { return Insn(x & 0x7f) << 13; }
inline Insn fSlot3(uint32_t x) { return Insn(x & 0x7f) << 20; }

inline Insn fX2a(uint32_t x) { return Insn(x & 0x3) << 34; }
inline Insn fX2b(uint32_t x) { return Insn(x & 0x3) << 27; }
inline Insn fX4(uint32_t x)  { return Insn(x & 0xf) << 29; }
inline Insn fVe(uint32_t x)  { return Insn(x & 0x1) << 33; }

// imm14, as used by A4 (adds): imm[6:0] -> 19:13, imm[12:7] -> 32:27,
// imm[13] (sign) -> 36.
inline Insn fImm14(int64_t v) {
  uint64_t u = uint64_t(v);
  return (Insn((u >> 0) & 0x7f) << 13) | (Insn((u >> 7) & 0x3f) << 27) |
         (Insn((u >> 13) & 0x1) << 36);
}

// The range adds can encode. Anything wider needs movl + add.
inline bool is_simm14(int64_t v) { return -(int64_t(1) << 13) <= v && v < (int64_t(1) << 13); }

// ---------------------------------------------------------------------------
// A-type (ALU) instructions, major opcode 8.
// ---------------------------------------------------------------------------

// A1: r1 = r2 <op> r3
inline Insn A1(uint32_t x4, uint32_t x2b, uint32_t r1, uint32_t r2,
               uint32_t r3, uint32_t qp = 0) {
  return fOp(8) | fX2a(0) | fVe(0) | fX4(x4) | fX2b(x2b) | fR3(r3) | fR2(r2) |
         fR1(r1) | fQp(qp);
}

inline Insn Add(uint32_t r1, uint32_t r2, uint32_t r3, uint32_t qp = 0)   { return A1(0, 0, r1, r2, r3, qp); }
inline Insn Sub(uint32_t r1, uint32_t r2, uint32_t r3, uint32_t qp = 0)   { return A1(1, 1, r1, r2, r3, qp); }
inline Insn And(uint32_t r1, uint32_t r2, uint32_t r3, uint32_t qp = 0)   { return A1(3, 0, r1, r2, r3, qp); }
inline Insn Andcm(uint32_t r1, uint32_t r2, uint32_t r3, uint32_t qp = 0) { return A1(3, 1, r1, r2, r3, qp); }
inline Insn Or(uint32_t r1, uint32_t r2, uint32_t r3, uint32_t qp = 0)    { return A1(3, 2, r1, r2, r3, qp); }
inline Insn Xor(uint32_t r1, uint32_t r2, uint32_t r3, uint32_t qp = 0)   { return A1(3, 3, r1, r2, r3, qp); }

// A3: and r1 = imm8, r3. Sign -> bit 36, imm7b -> bits 19:13 (the same split
// as fImm14 minus its extra 6 immediate bits).
inline Insn fImm8(int64_t v) {
  return (Insn(v & 0x7f) << 13) | (Insn((v >> 7) & 1) << 36);
}
inline Insn AndImm(uint32_t r1, int64_t imm8, uint32_t r3, uint32_t qp = 0) {
  return fOp(8) | fX2a(0) | fVe(0) | fX4(0xb) | fX2b(0) | fImm8(imm8) |
         fR3(r3) | fR1(r1) | fQp(qp);
}

// A4: r1 = imm14 + r3 ("adds"). Also the canonical register move when imm == 0,
// which is how "mov r1 = r3" is encoded. IA-64 has no displacement addressing,
// so this is also how every Address(base, offset) is materialised.
inline Insn Adds(uint32_t r1, int64_t imm14, uint32_t r3, uint32_t qp = 0) {
  return fOp(8) | fX2a(2) | fVe(0) | fImm14(imm14) | fR3(r3) | fR1(r1) | fQp(qp);
}

inline Insn MovReg(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return Adds(r1, 0, r3, qp); }

// ---------------------------------------------------------------------------
// A6: compare two registers and write a pair of predicates.
//   cmp.rel p1, p2 = r2, r3
//   tb -> bit 36   x2 -> bits 35:34   ta -> bit 33
//   p2 -> bits 32:27   r3 -> 26:20   r2 -> 19:13   c -> bit 12   p1 -> 11:6
// Predicates are 6 bits (p0-p63), so p1 does not reach bit 12.
// ---------------------------------------------------------------------------

inline Insn fP1(uint32_t x) { return Insn(x & 0x3f) << 6; }
inline Insn fP2(uint32_t x) { return Insn(x & 0x3f) << 27; }
inline Insn fTa(uint32_t x) { return Insn(x & 0x1) << 33; }
inline Insn fTb(uint32_t x) { return Insn(x & 0x1) << 36; }
inline Insn fC(uint32_t x)  { return Insn(x & 0x1) << 12; }
inline Insn fX2(uint32_t x) { return Insn(x & 0x3) << 34; }

inline Insn CmpA6(uint32_t op, uint32_t p1, uint32_t p2, uint32_t r2,
                  uint32_t r3, uint32_t qp = 0) {
  return fOp(op) | fTb(0) | fX2(0) | fTa(0) | fP2(p2) | fR3(r3) | fR2(r2) |
         fC(0) | fP1(p1) | fQp(qp);
}

inline Insn CmpEq(uint32_t p1, uint32_t p2, uint32_t r2, uint32_t r3, uint32_t qp = 0) {
  return CmpA6(0xe, p1, p2, r2, r3, qp);
}
// cmp.ne is cmp.eq with the two predicate destinations exchanged.
inline Insn CmpNe(uint32_t p1, uint32_t p2, uint32_t r2, uint32_t r3, uint32_t qp = 0) {
  return CmpA6(0xe, p2, p1, r2, r3, qp);
}
inline Insn CmpLt(uint32_t p1, uint32_t p2, uint32_t r2, uint32_t r3, uint32_t qp = 0) {
  return CmpA6(0xc, p1, p2, r2, r3, qp);
}
inline Insn CmpLtu(uint32_t p1, uint32_t p2, uint32_t r2, uint32_t r3, uint32_t qp = 0) {
  return CmpA6(0xd, p1, p2, r2, r3, qp);
}

// ---------------------------------------------------------------------------
// M-type (memory) instructions, major opcode 4 (integer) / 6 (floating point).
//   m    -> bit 36      x   -> bit 27
//   x6a  -> bits 35:30  hint -> bits 29:28
//
// Note there is no displacement form: every load and store addresses memory
// through a bare register.
// ---------------------------------------------------------------------------

inline Insn fM(uint32_t x)    { return Insn(x & 0x1) << 36; }
inline Insn fXm(uint32_t x)   { return Insn(x & 0x1) << 27; }
inline Insn fX6a(uint32_t x)  { return Insn(x & 0x3f) << 30; }
inline Insn fHint(uint32_t x) { return Insn(x & 0x3) << 28; }

// M1: r1 = [r3]
inline Insn LoadM1(uint32_t x6a, uint32_t r1, uint32_t r3, uint32_t qp = 0) {
  return fOp(4) | fM(0) | fXm(0) | fX6a(x6a) | fHint(0) | fR3(r3) | fR1(r1) | fQp(qp);
}

// M4: [r3] = r2
inline Insn StoreM4(uint32_t x6a, uint32_t r3, uint32_t r2, uint32_t qp = 0) {
  return fOp(4) | fM(0) | fXm(0) | fX6a(x6a) | fHint(0) | fR3(r3) | fR2(r2) | fQp(qp);
}

inline Insn Ld1(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return LoadM1(0x00, r1, r3, qp); }
inline Insn Ld2(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return LoadM1(0x01, r1, r3, qp); }
inline Insn Ld4(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return LoadM1(0x02, r1, r3, qp); }
inline Insn Ld8(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return LoadM1(0x03, r1, r3, qp); }

inline Insn St1(uint32_t r3, uint32_t r2, uint32_t qp = 0) { return StoreM4(0x30, r3, r2, qp); }
inline Insn St2(uint32_t r3, uint32_t r2, uint32_t qp = 0) { return StoreM4(0x31, r3, r2, qp); }
inline Insn St4(uint32_t r3, uint32_t r2, uint32_t qp = 0) { return StoreM4(0x32, r3, r2, qp); }
inline Insn St8(uint32_t r3, uint32_t r2, uint32_t qp = 0) { return StoreM4(0x33, r3, r2, qp); }

// ld8.fill / st8.spill -- the NaT-preserving general-register forms.
//
// Derived from ia64-linux-gnu-as: "ld8.fill r14=[r15]" encodes to slot
// 0x086c0f00380 (op 4, x6a 0x1b) and "st8.spill [r15]=r14" to 0x08ec0f1c000
// (op 4, x6a 0x3b).
//
// call_stub must use these rather than plain ld8/st8 when saving the C caller's
// preserved registers r4-r7: a plain round-trip drops the register's NaT bit,
// and GCC does emit control/data speculative loads (ld.s, ld.a) on IA-64 at
// -O2. st8.spill deposits the NaT bit into ar.unat, which must therefore be
// saved and restored around the block. See FRAME-DESIGN.md section 5.1.
inline Insn Ld8Fill(uint32_t r1, uint32_t r3, uint32_t qp = 0)  { return LoadM1(0x1b, r1, r3, qp); }
inline Insn St8Spill(uint32_t r3, uint32_t r2, uint32_t qp = 0) { return StoreM4(0x3b, r3, r2, qp); }

// M6/M9: floating-point loads and stores. Same M1/M4 shape but with major
// opcode 6 instead of 4, and f1/f2 in place of the integer register fields
// (they share the same 7-bit slot).
inline Insn LoadF6(uint32_t x6a, uint32_t f1, uint32_t r3, uint32_t qp = 0) {
  return fOp(6) | fM(0) | fXm(0) | fX6a(x6a) | fHint(0) | fR3(r3) | fSlot1(f1) | fQp(qp);
}
inline Insn StoreF9(uint32_t x6a, uint32_t r3, uint32_t f2, uint32_t qp = 0) {
  return fOp(6) | fM(0) | fXm(0) | fX6a(x6a) | fHint(0) | fR3(r3) | fSlot2(f2) | fQp(qp);
}
inline Insn Ldfs(uint32_t f1, uint32_t r3, uint32_t qp = 0) { return LoadF6(0x02, f1, r3, qp); }
inline Insn Ldfd(uint32_t f1, uint32_t r3, uint32_t qp = 0) { return LoadF6(0x03, f1, r3, qp); }
inline Insn Stfs(uint32_t r3, uint32_t f2, uint32_t qp = 0) { return StoreF9(0x32, r3, f2, qp); }
inline Insn Stfd(uint32_t r3, uint32_t f2, uint32_t qp = 0) { return StoreF9(0x33, r3, f2, qp); }

// ldf.fill / stf.spill -- the full 82-bit ("spill") floating-point forms.
//
// Derived from ia64-linux-gnu-as: "ldf.fill f6=[r15]" encodes to slot
// 0x0c6c0f00180 (op 6, x6a 0x1b) and "stf.spill [r15]=f6" to 0x0cec0f0c000
// (op 6, x6a 0x3b) -- the same x6a pair as the integer spill/fill above.
//
// These move 16 bytes and require a 16-byte aligned address. call_stub must use
// them, not stfd/ldfd, to save f2-f5 and f16-f31: an IA-64 FP register holds an
// 82-bit value, and a C caller may legitimately have a long double there that a
// double round-trip would silently truncate. See FRAME-DESIGN.md section 5.1.
inline Insn LdfFill(uint32_t f1, uint32_t r3, uint32_t qp = 0)  { return LoadF6(0x1b, f1, r3, qp); }
inline Insn StfSpill(uint32_t r3, uint32_t f2, uint32_t qp = 0) { return StoreF9(0x3b, r3, f2, qp); }

// M-type atomics and semaphores. The acquire/release loads and stores are
// ordinary M1/M4 with a different x6a; the read-modify-write forms set x.
inline Insn Ld8Acq(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return LoadM1(0x17, r1, r3, qp); }
inline Insn Ld4Acq(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return LoadM1(0x16, r1, r3, qp); }
inline Insn St8Rel(uint32_t r3, uint32_t r2, uint32_t qp = 0) { return StoreM4(0x37, r3, r2, qp); }
inline Insn St4Rel(uint32_t r3, uint32_t r2, uint32_t qp = 0) { return StoreM4(0x36, r3, r2, qp); }

// ---------------------------------------------------------------------------
// M24: mf, the memory fence -- the architecture's only standalone barrier.
//
// Derived from ia64-linux-gnu-as: "mf" encodes to slot 0x00110000000, i.e.
// major opcode 0 with x6 = 0x22 at bits 32:27 and every other field zero.
//
// Ordering is normally expressed with .acq/.rel completers on the individual
// accesses rather than with this; see orderAccess_linux_ia64.hpp.
// ---------------------------------------------------------------------------

inline Insn fX6b(uint32_t x) { return Insn(x & 0x3f) << 27; }

inline Insn Mf() { return fOp(0) | fX6b(0x22); }

// ---------------------------------------------------------------------------
// Application register moves.
//
// The AR file is split between the two units: ARs 0-47 are M-unit accessible
// (ar.ccv 32, ar.unat 36, ar.fpsr 40) and 48-127 are I-unit (ar.pfs 64,
// ar.lc 65, ar.ec 66). That is why the M-unit forms below carry major opcode 1
// and the I-unit forms major opcode 0 -- the operand layout is otherwise
// identical.
//
// Derived from ia64-linux-gnu-as, which encodes
//     mov r14 = ar.unat  -> op 1, x6b 0x22, r3 36, r1 14     (M unit, read)
//     mov ar.unat = r14  -> op 1, x6b 0x2a, r3 36, r2 14     (M unit, write)
//     mov r14 = ar.lc    -> op 0, x6b 0x32, r3 65, r1 14     (I unit, read)
//     mov ar.lc = r14    -> op 0, x6b 0x2a, r3 65, r2 14     (I unit, write)
//
// Note the asymmetry, which is easy to miss and was caught here only by the
// differential test: a *write* uses x6b 0x2a on both units, but a *read* uses
// 0x22 on the M unit and 0x32 on the I unit. Assuming reads were uniform
// produced a wrong "mov rN = ar.pfs" -- an instruction the epilogue of every
// call_stub depends on.
//
// This also cross-checks the inherited encoder: SpiderMonkey's MovToArCcv uses
// x6b 0x2a, and its hand-derived MovFromPfs constant 0x0194000000 decomposes
// to exactly op 0 / x6b 0x32 / r3 64 -- the I-unit read form.
// ---------------------------------------------------------------------------

enum ApplicationRegisterNumber {
  kArCcv  = 32,   // M unit -- the cmpxchg comparand
  kArUnat = 36,   // M unit -- NaT bits deposited by st8.spill
  kArFpsr = 40,   // M unit -- FP status
  kArPfs  = 64,   // I unit -- previous frame marker, written by alloc
  kArLc   = 65,   // I unit -- loop count
  kArEc   = 66    // I unit -- epilogue count
};

inline bool ar_is_m_unit(uint32_t ar) { return ar < 48; }

// mov <gr> = ar[N]
inline Insn MovFromAr(uint32_t r1, uint32_t ar, uint32_t qp = 0) {
  const bool m_unit = ar_is_m_unit(ar);
  return fOp(m_unit ? 1 : 0) | fX6b(m_unit ? 0x22 : 0x32) | fSlot3(ar) | fR1(r1) | fQp(qp);
}
// mov ar[N] = <gr>
inline Insn MovToAr(uint32_t ar, uint32_t r2, uint32_t qp = 0) {
  return fOp(ar_is_m_unit(ar) ? 1 : 0) | fX6b(0x2a) | fSlot3(ar) | fR2(r2) | fQp(qp);
}

inline Insn MovToArCcv(uint32_t r2, uint32_t qp = 0)   { return MovToAr(kArCcv, r2, qp); }
inline Insn MovFromPfs(uint32_t r1, uint32_t qp = 0)   { return MovFromAr(r1, kArPfs, qp); }
inline Insn MovToPfs(uint32_t r2, uint32_t qp = 0)     { return MovToAr(kArPfs, r2, qp); }
inline Insn MovFromUnat(uint32_t r1, uint32_t qp = 0)  { return MovFromAr(r1, kArUnat, qp); }
inline Insn MovToUnat(uint32_t r2, uint32_t qp = 0)    { return MovToAr(kArUnat, r2, qp); }

// M16: r1 = cmpxchg[r3], r2, ar.ccv -- the comparand comes from ar.ccv, which
// the caller must have loaded beforehand. The hardware compares ar.ccv's
// *exact* bit pattern, so a narrow cmpxchg must be fed a comparand that was
// zero-extended to the access width first.
inline Insn CmpxchgM16(uint32_t x6a, uint32_t r1, uint32_t r3, uint32_t r2, uint32_t qp = 0) {
  return fOp(4) | fM(0) | fXm(1) | fX6a(x6a) | fHint(0) | fR3(r3) | fR2(r2) | fR1(r1) | fQp(qp);
}

inline Insn Cmpxchg1Acq(uint32_t r1, uint32_t r3, uint32_t r2, uint32_t qp = 0) { return CmpxchgM16(0x00, r1, r3, r2, qp); }
inline Insn Cmpxchg2Acq(uint32_t r1, uint32_t r3, uint32_t r2, uint32_t qp = 0) { return CmpxchgM16(0x01, r1, r3, r2, qp); }
inline Insn Cmpxchg4Acq(uint32_t r1, uint32_t r3, uint32_t r2, uint32_t qp = 0) { return CmpxchgM16(0x02, r1, r3, r2, qp); }
inline Insn Cmpxchg8Acq(uint32_t r1, uint32_t r3, uint32_t r2, uint32_t qp = 0) { return CmpxchgM16(0x03, r1, r3, r2, qp); }
inline Insn Cmpxchg1Rel(uint32_t r1, uint32_t r3, uint32_t r2, uint32_t qp = 0) { return CmpxchgM16(0x04, r1, r3, r2, qp); }
inline Insn Cmpxchg2Rel(uint32_t r1, uint32_t r3, uint32_t r2, uint32_t qp = 0) { return CmpxchgM16(0x05, r1, r3, r2, qp); }
inline Insn Cmpxchg4Rel(uint32_t r1, uint32_t r3, uint32_t r2, uint32_t qp = 0) { return CmpxchgM16(0x06, r1, r3, r2, qp); }
inline Insn Cmpxchg8Rel(uint32_t r1, uint32_t r3, uint32_t r2, uint32_t qp = 0) { return CmpxchgM16(0x07, r1, r3, r2, qp); }
inline Insn Xchg1(uint32_t r1, uint32_t r3, uint32_t r2, uint32_t qp = 0)       { return CmpxchgM16(0x08, r1, r3, r2, qp); }
inline Insn Xchg2(uint32_t r1, uint32_t r3, uint32_t r2, uint32_t qp = 0)       { return CmpxchgM16(0x09, r1, r3, r2, qp); }
inline Insn Xchg4(uint32_t r1, uint32_t r3, uint32_t r2, uint32_t qp = 0)       { return CmpxchgM16(0x0a, r1, r3, r2, qp); }
inline Insn Xchg8(uint32_t r1, uint32_t r3, uint32_t r2, uint32_t qp = 0)       { return CmpxchgM16(0x0b, r1, r3, r2, qp); }

// M17: fetchadd. The increment is not a plain immediate: bits 14:13 select a
// magnitude from {16, 8, 4, 1} and bit 15 is the sign.
inline Insn fInc3(int32_t inc) {
  uint32_t mag = uint32_t(inc < 0 ? -inc : inc);
  uint32_t code = mag == 16 ? 0 : mag == 8 ? 1 : mag == 4 ? 2 : 3;
  return (Insn(code) << 13) | (Insn(inc < 0 ? 1 : 0) << 15);
}

inline Insn FetchaddM17(uint32_t x6a, uint32_t r1, uint32_t r3, int32_t inc, uint32_t qp = 0) {
  return fOp(4) | fM(0) | fXm(1) | fX6a(x6a) | fHint(0) | fR3(r3) | fInc3(inc) | fR1(r1) | fQp(qp);
}

inline Insn Fetchadd4Acq(uint32_t r1, uint32_t r3, int32_t inc, uint32_t qp = 0) { return FetchaddM17(0x12, r1, r3, inc, qp); }
inline Insn Fetchadd8Acq(uint32_t r1, uint32_t r3, int32_t inc, uint32_t qp = 0) { return FetchaddM17(0x13, r1, r3, inc, qp); }
inline Insn Fetchadd4Rel(uint32_t r1, uint32_t r3, int32_t inc, uint32_t qp = 0) { return FetchaddM17(0x16, r1, r3, inc, qp); }
inline Insn Fetchadd8Rel(uint32_t r1, uint32_t r3, int32_t inc, uint32_t qp = 0) { return FetchaddM17(0x17, r1, r3, inc, qp); }

// M18/M19: move between a general register and a floating-point register.
// getf is major op 4, setf major op 6; both set x (bit 27).
inline Insn GetfM19(uint32_t x6a, uint32_t r1, uint32_t f2, uint32_t qp = 0) {
  return fOp(4) | fM(0) | fXm(1) | fX6a(x6a) | fR1(r1) | fSlot2(f2) | fQp(qp);
}
inline Insn SetfM18(uint32_t x6a, uint32_t f1, uint32_t r2, uint32_t qp = 0) {
  return fOp(6) | fM(0) | fXm(1) | fX6a(x6a) | fSlot1(f1) | fR2(r2) | fQp(qp);
}

inline Insn GetfSig(uint32_t r1, uint32_t f2, uint32_t qp = 0) { return GetfM19(0x1c, r1, f2, qp); }
inline Insn GetfExp(uint32_t r1, uint32_t f2, uint32_t qp = 0) { return GetfM19(0x1d, r1, f2, qp); }
inline Insn GetfS(uint32_t r1, uint32_t f2, uint32_t qp = 0)   { return GetfM19(0x1e, r1, f2, qp); }
inline Insn GetfD(uint32_t r1, uint32_t f2, uint32_t qp = 0)   { return GetfM19(0x1f, r1, f2, qp); }
inline Insn SetfSig(uint32_t f1, uint32_t r2, uint32_t qp = 0) { return SetfM18(0x1c, f1, r2, qp); }
inline Insn SetfExp(uint32_t f1, uint32_t r2, uint32_t qp = 0) { return SetfM18(0x1d, f1, r2, qp); }
inline Insn SetfS(uint32_t f1, uint32_t r2, uint32_t qp = 0)   { return SetfM18(0x1e, f1, r2, qp); }
inline Insn SetfD(uint32_t f1, uint32_t r2, uint32_t qp = 0)   { return SetfM18(0x1f, f1, r2, qp); }

// ---------------------------------------------------------------------------
// M34: alloc r1 = ar.pfs, ins, locals, outs, rot
//
// Establishes this procedure's register-stack frame and saves the previous
// frame marker into r1. This port executes exactly one of these, in
// StubRoutines::call_stub():
//     alloc rN = ar.pfs, 0, 0, 8, 0
// to obtain out0-out7 for calling C functions. Because ins = locals = 0, a
// later br.call leaves CFM unchanged and the window stays valid across nested
// calls. See FRAME-DESIGN.md section 1 -- adding a second alloc anywhere breaks
// the invariant that makes br.ret safe in generated code.
//
//   sof = ins + locals + outs -> bits 19:13
//   sol = ins + locals        -> bits 26:20
//   sor = rot / 8             -> bits 30:27
//   op = 1, x3 = 6 (bits 35:33), r1 -> bits 12:6
// alloc cannot be predicated, so qp is always 0.
// ---------------------------------------------------------------------------

inline Insn fX3(uint32_t x) { return Insn(x & 0x7) << 33; }

inline Insn Alloc(uint32_t r1, uint32_t ins, uint32_t locals, uint32_t outs, uint32_t rot = 0) {
  uint32_t sof = ins + locals + outs;
  uint32_t sol = ins + locals;
  uint32_t sor = rot / 8;
  return fOp(1) | fX3(6) | (Insn(sor & 0xf) << 27) | (Insn(sol & 0x7f) << 20) |
         (Insn(sof & 0x7f) << 13) | fR1(r1);
}

// ---------------------------------------------------------------------------
// B-type (branch) instructions.
//   btype -> bits 8:6     pa  -> bit 12      b1 -> bits 8:6
//   b2    -> bits 15:13   wha -> bits 34:33  whc -> bits 34:32
//   x6    -> bits 32:27   d   -> bit 35
// ---------------------------------------------------------------------------

inline Insn fBtype(uint32_t x) { return Insn(x & 0x7) << 6; }
inline Insn fB1(uint32_t x)    { return Insn(x & 0x7) << 6; }
inline Insn fB2(uint32_t x)    { return Insn(x & 0x7) << 13; }
inline Insn fPa(uint32_t x)    { return Insn(x & 0x1) << 12; }
inline Insn fWha(uint32_t x)   { return Insn(x & 0x3) << 33; }
inline Insn fWhc(uint32_t x)   { return Insn(x & 0x7) << 32; }
inline Insn fD(uint32_t x)     { return Insn(x & 0x1) << 35; }

// B4: br.cond.sptk b2 -- indirect branch through a branch register. Unlike
// br.ret this does not touch CFM, so it is the safe form if the single-alloc
// invariant is ever relaxed.
inline Insn BrCond(uint32_t b2, uint32_t qp = 0) {
  return fOp(0) | fX6b(0x20) | fBtype(0) | fPa(0) | fWha(0) | fD(0) | fB2(b2) | fQp(qp);
}

// B4: br.ret.sptk.many b2. Restores CFM from ar.pfs -- a no-op here only
// because CFM never changes after call_stub's alloc. See FRAME-DESIGN.md 4.4.
inline Insn BrRet(uint32_t b2, uint32_t qp = 0) {
  return fOp(0) | fX6b(0x21) | fBtype(4) | fPa(1) | fWha(0) | fD(0) | fB2(b2) | fQp(qp);
}

// B5: br.call.sptk.many b1 = b2 -- indirect call, return address into b1.
// Clobbers b0 and ar.pfs; both are saved to memory by call_stub.
inline Insn BrCall(uint32_t b1, uint32_t b2, uint32_t qp = 0) {
  return fOp(1) | fPa(1) | fWhc(1) | fD(0) | fB1(b1) | fB2(b2) | fQp(qp);
}

// IP-relative displacements (TGT25c) are measured in whole 16-byte bundles and
// encoded as a signed 21-bit value: imm20 in bits 32:13 and the sign in bit 36.
// That gives +/-2^20 bundles, i.e. +/-16 MiB.
static const int32_t MaxBranchBundleDisp = (1 << 20) - 1;
static const int32_t MinBranchBundleDisp = -(1 << 20);

inline bool BranchDispInRange(int32_t bundleDisp) {
  return bundleDisp >= MinBranchBundleDisp && bundleDisp <= MaxBranchBundleDisp;
}

inline Insn fTgt25c(int32_t bundleDisp) {
  uint32_t u = uint32_t(bundleDisp);
  return (Insn(u & 0xfffff) << 13) | (Insn((u >> 20) & 0x1) << 36);
}

// B1: br.cond.sptk <ip-relative target>
inline Insn BrCondRel(int32_t bundleDisp, uint32_t qp = 0) {
  return fOp(4) | fBtype(0) | fPa(0) | fWha(0) | fD(0) | fTgt25c(bundleDisp) | fQp(qp);
}

// B3: br.call.sptk.many b1 = <ip-relative target>
inline Insn BrCallRel(uint32_t b1, int32_t bundleDisp, uint32_t qp = 0) {
  return fOp(5) | fPa(1) | fWha(0) | fD(0) | fB1(b1) | fTgt25c(bundleDisp) | fQp(qp);
}

// B9: break.b. Major opcode 0; the 21-bit immediate is spread the same way as
// the nops below. This is how a trap / ShouldNotReachHere is emitted.
inline Insn BreakB(uint32_t imm21, uint32_t qp = 0) {
  return fOp(0) | (Insn(imm21 & 0xfffff) << 6) | (Insn((imm21 >> 20) & 1) << 36) | fQp(qp);
}

// Rewrite the IP-relative displacement of a branch already emitted into slot 2
// of |b|, leaving every other field alone. This is what label binding uses to
// resolve forward branches.
inline void PatchBranchDisp(Bundle* b, int32_t bundleDisp) {
  const uint64_t kSlotMask = (uint64_t(1) << 41) - 1;
  // slot 2 occupies bundle bits 127:87, i.e. hi bits 63:23.
  Insn slot2 = (b->hi >> 23) & kSlotMask;
  Insn clearMask = (Insn(0xfffff) << 13) | (Insn(1) << 36);
  slot2 = (slot2 & ~clearMask) | fTgt25c(bundleDisp);
  b->hi = (b->hi & ((uint64_t(1) << 23) - 1)) | (slot2 << 23);
}

// Toggle a lone B-slot bundle between a real branch/call (op 4/5) and a nop
// (op 2), in place. op sits in the same 4 high bits for all three, well clear
// of the tgt25c payload and qp, so flipping just those bits round-trips cleanly
// and preserves whatever displacement the branch had.
inline void ToggleSlot2Op(Bundle* b, uint32_t op) {
  const uint64_t kSlotMask = (uint64_t(1) << 41) - 1;
  Insn slot2 = (b->hi >> 23) & kSlotMask;
  slot2 = (slot2 & ~(Insn(0xf) << 37)) | fOp(op);
  b->hi = (b->hi & ((uint64_t(1) << 23) - 1)) | (slot2 << 23);
}

// ---------------------------------------------------------------------------
// Moves to and from branch registers. An indirect call needs its target in a
// branch register, and every frame must save and restore b0 (the return
// pointer), which br.call overwrites.
//
// The binutils macros for these overlap ambiguously (tag13 and x3 both claim
// bit 33), so the base patterns were taken from GNU as directly:
//   mov bN = rM      b -> bits 8:6    r2 -> bits 19:13
//   mov rN = bM      r1 -> bits 12:6  b2 -> bits 15:13
// ---------------------------------------------------------------------------

inline Insn fBr(uint32_t x) { return Insn(x & 0x7) << 6; }

// mov b1 = r2
inline Insn MovToBr(uint32_t b1, uint32_t r2, uint32_t qp = 0) {
  return Insn(0x0e00100000ull) | fBr(b1) | fR2(r2) | fQp(qp);
}

// mov r1 = b2
inline Insn MovFromBr(uint32_t r1, uint32_t b2, uint32_t qp = 0) {
  return Insn(0x0188000000ull) | fR1(r1) | (Insn(b2 & 0x7) << 13) | fQp(qp);
}

// ---------------------------------------------------------------------------
// I-type shifts, extracts and deposits.
//
// The register-variable shifts (I5/I7, major opcode 7) are the multimedia
// shift group restricted to a full 64-bit element by za/zb:
//   za -> bit 36   zb -> bit 33   ve -> bit 32
//   x2a -> 35:34   x2c -> 31:30   x2b -> 29:28
// Note the I-unit ve and x2b sit at different positions than the A-unit ones.
//
// The immediate forms (I11/I12, major opcode 5) are really extr/dep.z:
//   shr  r1 = r3, n  ==  extr   r1 = r3, n, 64-n
//   shl  r1 = r2, n  ==  dep.z  r1 = r2, n, 64-n
// ---------------------------------------------------------------------------

inline Insn fZa(uint32_t x)     { return Insn(x & 0x1) << 36; }
inline Insn fZb(uint32_t x)     { return Insn(x & 0x1) << 33; }
inline Insn fVeI(uint32_t x)    { return Insn(x & 0x1) << 32; }
inline Insn fX2bI(uint32_t x)   { return Insn(x & 0x3) << 28; }
inline Insn fX2c(uint32_t x)    { return Insn(x & 0x3) << 30; }
inline Insn fXi(uint32_t x)     { return Insn(x & 0x1) << 33; }
inline Insn fYa(uint32_t x)     { return Insn(x & 0x1) << 13; }
inline Insn fYb(uint32_t x)     { return Insn(x & 0x1) << 26; }
inline Insn fPos6(uint32_t x)   { return Insn(x & 0x3f) << 14; }
inline Insn fLen6(uint32_t len) { return Insn((len - 1) & 0x3f) << 27; }
inline Insn fCpos6a(uint32_t x) { return Insn((63 - x) & 0x3f) << 20; }

inline Insn ShiftVar(uint32_t x2b, uint32_t x2c, uint32_t r1, uint32_t r2,
                     uint32_t r3, uint32_t qp = 0) {
  return fOp(7) | fZa(1) | fZb(1) | fVeI(0) | fX2a(0) | fX2c(x2c) | fX2bI(x2b) |
         fR3(r3) | fR2(r2) | fR1(r1) | fQp(qp);
}

// shl takes the count in r3; the shift-rights take it in r2.
inline Insn Shl(uint32_t r1, uint32_t value, uint32_t count, uint32_t qp = 0)  { return ShiftVar(0, 1, r1, value, count, qp); }
inline Insn Shr(uint32_t r1, uint32_t value, uint32_t count, uint32_t qp = 0)  { return ShiftVar(2, 0, r1, count, value, qp); }
inline Insn ShrU(uint32_t r1, uint32_t value, uint32_t count, uint32_t qp = 0) { return ShiftVar(0, 0, r1, count, value, qp); }

// I11: extr / extr.u r1 = r3, pos, len
inline Insn Extr(uint32_t r1, uint32_t r3, uint32_t pos, uint32_t len, uint32_t qp = 0) {
  return fOp(5) | fX2(1) | fXi(0) | fYa(1) | fLen6(len) | fR3(r3) | fPos6(pos) | fR1(r1) | fQp(qp);
}
inline Insn ExtrU(uint32_t r1, uint32_t r3, uint32_t pos, uint32_t len, uint32_t qp = 0) {
  return fOp(5) | fX2(1) | fXi(0) | fYa(0) | fLen6(len) | fR3(r3) | fPos6(pos) | fR1(r1) | fQp(qp);
}

// I12: dep.z r1 = r2, pos, len
inline Insn DepZ(uint32_t r1, uint32_t r2, uint32_t pos, uint32_t len, uint32_t qp = 0) {
  return fOp(5) | fX2(1) | fXi(1) | fYb(0) | fLen6(len) | fCpos6a(pos) | fR2(r2) | fR1(r1) | fQp(qp);
}

inline Insn ShlImm(uint32_t r1, uint32_t r2, uint32_t count, uint32_t qp = 0)  { return DepZ(r1, r2, count, 64 - count, qp); }
inline Insn ShrImm(uint32_t r1, uint32_t r3, uint32_t count, uint32_t qp = 0)  { return Extr(r1, r3, count, 64 - count, qp); }
inline Insn ShrUImm(uint32_t r1, uint32_t r3, uint32_t count, uint32_t qp = 0) { return ExtrU(r1, r3, count, 64 - count, qp); }

// I29: sign/zero extend. Major opcode 0, x3 = 0, x6 selects the width.
inline Insn ExtendI29(uint32_t x6, uint32_t r1, uint32_t r3, uint32_t qp = 0) {
  return fOp(0) | fX3(0) | fX6b(x6) | fR3(r3) | fR1(r1) | fQp(qp);
}

inline Insn Zxt1(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return ExtendI29(0x10, r1, r3, qp); }
inline Insn Zxt2(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return ExtendI29(0x11, r1, r3, qp); }
inline Insn Zxt4(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return ExtendI29(0x12, r1, r3, qp); }
inline Insn Sxt1(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return ExtendI29(0x14, r1, r3, qp); }
inline Insn Sxt2(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return ExtendI29(0x15, r1, r3, qp); }
inline Insn Sxt4(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return ExtendI29(0x16, r1, r3, qp); }

// I29: czx (count trailing/leading zero bytes), same shape.
inline Insn Czx1L(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return ExtendI29(0x18, r1, r3, qp); }
inline Insn Czx2L(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return ExtendI29(0x19, r1, r3, qp); }
inline Insn Czx1R(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return ExtendI29(0x1c, r1, r3, qp); }
inline Insn Czx2R(uint32_t r1, uint32_t r3, uint32_t qp = 0) { return ExtendI29(0x1d, r1, r3, qp); }

// I9: popcnt.
inline Insn Popcnt(uint32_t r1, uint32_t r3, uint32_t qp = 0) {
  return fOp(7) | fZa(0) | fZb(1) | fVeI(0) | fX2a(1) | fX2bI(1) | fX2c(2) | fR3(r3) | fR1(r1) | fQp(qp);
}

// I3: mux1. Shares the major-7 field layout; the third operand carries the
// permute-type immediate (MBTYPE4) instead of a register.
inline Insn Mux1(uint32_t r1, uint32_t r2, uint32_t mbtype4, uint32_t qp = 0) {
  return fOp(7) | fZa(0) | fZb(0) | fVeI(0) | fX2a(3) | fX2bI(2) | fX2c(2) |
         fSlot3(mbtype4) | fR2(r2) | fR1(r1) | fQp(qp);
}
static const uint32_t kMux1Rev = 0xb;   // byte reverse -- Long.reverseBytes

// I10: shrp. Major opcode 5, x2 = 3, x = 0; the shift count reuses the 6-bit
// field at 32:27 that fX6b already models.
inline Insn Shrp(uint32_t r1, uint32_t r2, uint32_t r3, uint32_t count, uint32_t qp = 0) {
  return fOp(5) | fX2(3) | fX6b(count) | fR3(r3) | fR2(r2) | fR1(r1) | fQp(qp);
}

// ---------------------------------------------------------------------------
// F-type (floating point).
//
// IA-64 has no plain fadd/fmul: everything is the fused multiply-add
//   fma.pc.sf f1 = f3, f4, f2   ->   f1 = f3 * f4 + f2
// with f0 (0.0) and f1 (1.0) used as identity operands. fadd is fma with
// f4 = 1, fmpy is fma with f2 = 0, and fnorm is fma with f4 = 1, f2 = 0.
//
//   f1 -> 12:6   f2 -> 19:13   f3 -> 26:20   f4 -> 33:27
//   sf -> 35:34  x (opcode extension) -> bit 36
//
// The .d suffix rounds to IEEE double, which is what Java arithmetic wants;
// the unsuffixed form keeps the 82-bit register format.
// ---------------------------------------------------------------------------

inline Insn fF1(uint32_t x) { return Insn(x & 0x7f) << 6; }
inline Insn fF2(uint32_t x) { return Insn(x & 0x7f) << 13; }
inline Insn fF3(uint32_t x) { return Insn(x & 0x7f) << 20; }
inline Insn fF4(uint32_t x) { return Insn(x & 0x7f) << 27; }
inline Insn fSf(uint32_t x) { return Insn(x & 0x3) << 34; }
inline Insn fXa(uint32_t x) { return Insn(x & 0x1) << 36; }

// Status field: sf0 is the architected default used throughout.
enum FpSf : uint32_t { sf0 = 0, sf1 = 1, sf2 = 2, sf3 = 3 };

// Register f0 reads as +0.0 and f1 as +1.0, which is what makes the pseudo-ops
// below assemble to a bare fma.
static const uint32_t fpZero = 0;
static const uint32_t fpOne  = 1;

// F1: f1 = f3 * f4 (+/-) f2
inline Insn FmaF1(uint32_t op, uint32_t xa, uint32_t f1, uint32_t f3,
                  uint32_t f4, uint32_t f2, uint32_t sf, uint32_t qp) {
  return fOp(op) | fXa(xa) | fSf(sf) | fF4(f4) | fF3(f3) | fF2(f2) | fF1(f1) | fQp(qp);
}

inline Insn Fma(uint32_t f1, uint32_t f3, uint32_t f4, uint32_t f2, uint32_t sf = sf0, uint32_t qp = 0)   { return FmaF1(0x8, 0, f1, f3, f4, f2, sf, qp); }
inline Insn FmaS(uint32_t f1, uint32_t f3, uint32_t f4, uint32_t f2, uint32_t sf = sf0, uint32_t qp = 0)  { return FmaF1(0x8, 1, f1, f3, f4, f2, sf, qp); }
inline Insn FmaD(uint32_t f1, uint32_t f3, uint32_t f4, uint32_t f2, uint32_t sf = sf0, uint32_t qp = 0)  { return FmaF1(0x9, 0, f1, f3, f4, f2, sf, qp); }
inline Insn Fms(uint32_t f1, uint32_t f3, uint32_t f4, uint32_t f2, uint32_t sf = sf0, uint32_t qp = 0)   { return FmaF1(0xa, 0, f1, f3, f4, f2, sf, qp); }
inline Insn FmsS(uint32_t f1, uint32_t f3, uint32_t f4, uint32_t f2, uint32_t sf = sf0, uint32_t qp = 0)  { return FmaF1(0xa, 1, f1, f3, f4, f2, sf, qp); }
inline Insn FmsD(uint32_t f1, uint32_t f3, uint32_t f4, uint32_t f2, uint32_t sf = sf0, uint32_t qp = 0)  { return FmaF1(0xb, 0, f1, f3, f4, f2, sf, qp); }
inline Insn Fnma(uint32_t f1, uint32_t f3, uint32_t f4, uint32_t f2, uint32_t sf = sf0, uint32_t qp = 0)  { return FmaF1(0xc, 0, f1, f3, f4, f2, sf, qp); }
inline Insn FnmaS(uint32_t f1, uint32_t f3, uint32_t f4, uint32_t f2, uint32_t sf = sf0, uint32_t qp = 0) { return FmaF1(0xc, 1, f1, f3, f4, f2, sf, qp); }
inline Insn FnmaD(uint32_t f1, uint32_t f3, uint32_t f4, uint32_t f2, uint32_t sf = sf0, uint32_t qp = 0) { return FmaF1(0xd, 0, f1, f3, f4, f2, sf, qp); }

inline Insn FaddD(uint32_t f1, uint32_t f3, uint32_t f2, uint32_t sf = sf0, uint32_t qp = 0)  { return FmaD(f1, f3, fpOne, f2, sf, qp); }
inline Insn FsubD(uint32_t f1, uint32_t f3, uint32_t f2, uint32_t sf = sf0, uint32_t qp = 0)  { return FmsD(f1, f3, fpOne, f2, sf, qp); }
inline Insn FmpyD(uint32_t f1, uint32_t f3, uint32_t f4, uint32_t sf = sf0, uint32_t qp = 0)  { return FmaD(f1, f3, f4, fpZero, sf, qp); }
inline Insn FnormD(uint32_t f1, uint32_t f3, uint32_t sf = sf0, uint32_t qp = 0)              { return FmaD(f1, f3, fpOne, fpZero, sf, qp); }
inline Insn Fnorm(uint32_t f1, uint32_t f3, uint32_t sf = sf0, uint32_t qp = 0)               { return Fma(f1, f3, fpOne, fpZero, sf, qp); }
inline Insn FmovD(uint32_t f1, uint32_t f3, uint32_t qp = 0)                                  { return FnormD(f1, f3, sf0, qp); }

// F6/F11: conversions. Major opcode 0, xb (bit 33) = 0, x6 -> 32:27.
inline Insn FcvtF11(uint32_t x6, uint32_t f1, uint32_t f2, uint32_t sf, uint32_t qp) {
  return fOp(0) | fZb(0) | fX6b(x6) | fSf(sf) | fF2(f2) | fF1(f1) | fQp(qp);
}

inline Insn FcvtFx(uint32_t f1, uint32_t f2, uint32_t sf = sf0, uint32_t qp = 0)        { return FcvtF11(0x18, f1, f2, sf, qp); }
inline Insn FcvtFxu(uint32_t f1, uint32_t f2, uint32_t sf = sf0, uint32_t qp = 0)       { return FcvtF11(0x19, f1, f2, sf, qp); }
inline Insn FcvtFxTrunc(uint32_t f1, uint32_t f2, uint32_t sf = sf0, uint32_t qp = 0)   { return FcvtF11(0x1a, f1, f2, sf, qp); }
inline Insn FcvtFxuTrunc(uint32_t f1, uint32_t f2, uint32_t sf = sf0, uint32_t qp = 0)  { return FcvtF11(0x1b, f1, f2, sf, qp); }
// fcvt.xf has no status field.
inline Insn FcvtXf(uint32_t f1, uint32_t f2, uint32_t qp = 0)                           { return FcvtF11(0x1c, f1, f2, 0, qp); }

// F4: fcmp.rel.sf p1, p2 = f2, f3
//   ra -> bit 33   rb -> bit 36   ta -> bit 12
inline Insn fRa(uint32_t x) { return Insn(x & 0x1) << 33; }
inline Insn fRb(uint32_t x) { return Insn(x & 0x1) << 36; }

// fFccTa (bit 12) selects the .unc completer: when qp is false, p1/p2 are left
// at 0 rather than holding the relation, which lets a second fcmp be chained
// off the first predicate to build "ordered AND rel" in one extra instruction.
// This is a different field from the A-type compare's fTa (bit 33) despite the
// similar name in the manual.
inline Insn fFccTa(uint32_t x) { return Insn(x & 0x1) << 12; }

inline Insn FcmpF4(uint32_t ra, uint32_t rb, uint32_t p1, uint32_t p2,
                   uint32_t f2, uint32_t f3, uint32_t sf, uint32_t qp, uint32_t ta = 0) {
  return fOp(4) | fRb(rb) | fSf(sf) | fRa(ra) | fP2(p2) | fF3(f3) | fF2(f2) |
         fFccTa(ta) | fP1(p1) | fQp(qp);
}

inline Insn FcmpEq(uint32_t p1, uint32_t p2, uint32_t f2, uint32_t f3, uint32_t sf = sf0, uint32_t qp = 0)    { return FcmpF4(0, 0, p1, p2, f2, f3, sf, qp); }
inline Insn FcmpLt(uint32_t p1, uint32_t p2, uint32_t f2, uint32_t f3, uint32_t sf = sf0, uint32_t qp = 0)    { return FcmpF4(0, 1, p1, p2, f2, f3, sf, qp); }
inline Insn FcmpLe(uint32_t p1, uint32_t p2, uint32_t f2, uint32_t f3, uint32_t sf = sf0, uint32_t qp = 0)    { return FcmpF4(1, 0, p1, p2, f2, f3, sf, qp); }
inline Insn FcmpUnord(uint32_t p1, uint32_t p2, uint32_t f2, uint32_t f3, uint32_t sf = sf0, uint32_t qp = 0) { return FcmpF4(1, 1, p1, p2, f2, f3, sf, qp); }
inline Insn FcmpEqUnc(uint32_t p1, uint32_t p2, uint32_t f2, uint32_t f3, uint32_t sf, uint32_t qp)           { return FcmpF4(0, 0, p1, p2, f2, f3, sf, qp, 1); }
inline Insn FcmpLtUnc(uint32_t p1, uint32_t p2, uint32_t f2, uint32_t f3, uint32_t sf, uint32_t qp)           { return FcmpF4(0, 1, p1, p2, f2, f3, sf, qp, 1); }
inline Insn FcmpLeUnc(uint32_t p1, uint32_t p2, uint32_t f2, uint32_t f3, uint32_t sf, uint32_t qp)           { return FcmpF4(1, 0, p1, p2, f2, f3, sf, qp, 1); }

// F6: frcpa / frsqrta -- the seeds for the Newton-Raphson divide and square
// root sequences. IA-64 has no divide instruction. Both write a status
// predicate p2 in addition to f1.
inline Insn fXb(uint32_t x) { return Insn(x & 0x1) << 33; }
inline Insn fQ(uint32_t x)  { return Insn(x & 0x1) << 36; }

inline Insn Frcpa(uint32_t f1, uint32_t p2, uint32_t f2, uint32_t f3, uint32_t sf = sf0, uint32_t qp = 0) {
  return fOp(0) | fQ(0) | fXb(1) | fSf(sf) | fF3(f3) | fF2(f2) | fP2(p2) | fF1(f1) | fQp(qp);
}
inline Insn Frsqrta(uint32_t f1, uint32_t p2, uint32_t f3, uint32_t sf = sf0, uint32_t qp = 0) {
  return fOp(0) | fQ(1) | fXb(1) | fSf(sf) | fF3(f3) | fP2(p2) | fF1(f1) | fQp(qp);
}

// F2: xma. Fused 64x64 integer multiply-add through the significand path --
// this is how integer multiply is done, since the integer units have no
// multiplier. xa is bit 36, x2 is bits 35:34.
inline Insn XmaF2(uint32_t x2, uint32_t f1, uint32_t f3, uint32_t f4, uint32_t f2, uint32_t qp) {
  return fOp(0xe) | fXa(1) | fX2(x2) | fF4(f4) | fF3(f3) | fF2(f2) | fF1(f1) | fQp(qp);
}
inline Insn XmaL(uint32_t f1, uint32_t f3, uint32_t f4, uint32_t f2, uint32_t qp = 0)  { return XmaF2(0, f1, f3, f4, f2, qp); }
inline Insn XmaHu(uint32_t f1, uint32_t f3, uint32_t f4, uint32_t f2, uint32_t qp = 0) { return XmaF2(2, f1, f3, f4, f2, qp); }
inline Insn XmaH(uint32_t f1, uint32_t f3, uint32_t f4, uint32_t f2, uint32_t qp = 0)  { return XmaF2(3, f1, f3, f4, f2, qp); }

// F9: fmerge. Major opcode 0, xb = 0, x6 -> bits 32:27.
inline Insn FmergeF9(uint32_t x6, uint32_t f1, uint32_t f2, uint32_t f3, uint32_t qp) {
  return fOp(0) | fZb(0) | fX6b(x6) | fF3(f3) | fF2(f2) | fF1(f1) | fQp(qp);
}
inline Insn FmergeS(uint32_t f1, uint32_t f2, uint32_t f3, uint32_t qp = 0)  { return FmergeF9(0x10, f1, f2, f3, qp); }
inline Insn FmergeNs(uint32_t f1, uint32_t f2, uint32_t f3, uint32_t qp = 0) { return FmergeF9(0x11, f1, f2, f3, qp); }
inline Insn FmergeSe(uint32_t f1, uint32_t f2, uint32_t f3, uint32_t qp = 0) { return FmergeF9(0x12, f1, f2, f3, qp); }

// F5: fclass. Major opcode 5, ta (bit 12) = 0 for the non-.unc form. The
// class-test immediate is split across two fields: a 7-bit magnitude mask at
// bits 26:20 and a 2-bit sign selector at bits 34:33.
enum FclassMask : uint32_t {
  FclassZero     = 1u << 0,
  FclassUnorm    = 1u << 1,
  FclassNorm     = 1u << 2,
  FclassInf      = 1u << 3,
  FclassSNaN     = 1u << 4,
  FclassQNaN     = 1u << 5,
  FclassNat      = 1u << 6,
  // The two hardware sign-select bits (bits 34:33), settable independently.
  FclassSignPos  = 1u << 7,
  FclassSignNeg  = 1u << 8,
  FclassSignBoth = FclassSignPos | FclassSignNeg
};
static const uint32_t FclassNaN         = FclassSNaN | FclassQNaN;
static const uint32_t FclassAnySignZero = FclassZero | FclassSignBoth;
static const uint32_t FclassAnySignInf  = FclassInf | FclassSignBoth;
static const uint32_t FclassPosInf      = FclassInf | FclassSignPos;

inline Insn Fclass(uint32_t p1, uint32_t p2, uint32_t f2, uint32_t mask, uint32_t qp = 0) {
  return fOp(5) | fTa(0) | (Insn((mask >> 7) & 0x3) << 33) |
         (Insn(mask & 0x7f) << 20) | fF2(f2) | fP2(p2) | fP1(p1) | fQp(qp);
}

// ---------------------------------------------------------------------------
// Nops. The same 41-bit pattern in every unit except B; only the slot it
// occupies determines which unit executes it.
// ---------------------------------------------------------------------------

inline Insn NopM(uint32_t imm21 = 0) {
  return fOp(0) | (Insn(1) << 27) | (Insn(imm21 & 0xfffff) << 6) |
         (Insn((imm21 >> 20) & 1) << 36);
}
inline Insn NopI(uint32_t imm21 = 0) { return NopM(imm21); }
inline Insn NopF(uint32_t imm21 = 0) { return NopM(imm21); }
inline Insn NopB(uint32_t imm21 = 0) {
  return fOp(2) | (Insn(imm21 & 0xfffff) << 6) | (Insn((imm21 >> 20) & 1) << 36);
}

// ---------------------------------------------------------------------------
// movl: load a full 64-bit immediate. The only two-slot instruction: it
// occupies the L and X slots of an MLX bundle, with the immediate scattered
// across both.
//
//   imm[6:0]   -> X slot 19:13 (imm7b)
//   imm[15:7]  -> X slot 35:27 (imm9d)
//   imm[20:16] -> X slot 26:22 (imm5c)
//   imm[21]    -> X slot 21    (ic)
//   imm[62:22] -> the whole L slot
//   imm[63]    -> X slot 36    (i, the sign bit)
//
// vc (X slot bit 20) is 0 for movl.
//
// This is how the port addresses everything far away: there is no gp-relative
// addressing in generated code (FRAME-DESIGN.md 3.4), and IP-relative branches
// reach only +/-16 MiB, so every absolute address is materialised here.
// ---------------------------------------------------------------------------

inline Bundle MovlBundle(uint32_t r1, uint64_t imm, uint32_t qp = 0) {
  Insn l = Insn((imm >> 22) & ((uint64_t(1) << 41) - 1));

  Insn x = fOp(6) | fR1(r1) | fQp(qp);
  x |= Insn((imm >> 0) & 0x7f) << 13;    // imm7b
  x |= Insn((imm >> 7) & 0x1ff) << 27;   // imm9d
  x |= Insn((imm >> 16) & 0x1f) << 22;   // imm5c
  x |= Insn((imm >> 21) & 0x1) << 21;    // ic
  x |= Insn((imm >> 63) & 0x1) << 36;    // i

  // Slot 0 of an MLX bundle is an M slot; pad it with an M nop.
  return MakeBundle(tMLX_, NopM(), l, x);
}

// Read back the immediate a movl bundle carries. Used by nativeInst to inspect
// and re-patch materialised addresses (oops, metadata, call targets).
inline uint64_t ReadMovlImm(const Bundle* b) {
  const uint64_t kSlotMask = (uint64_t(1) << 41) - 1;
  Insn l = ((b->lo >> 46) | (b->hi << 18)) & kSlotMask;   // slot 1
  Insn x = (b->hi >> 23) & kSlotMask;                     // slot 2

  uint64_t imm = 0;
  imm |= ((x >> 13) & 0x7f);            // imm7b   -> imm[6:0]
  imm |= ((x >> 27) & 0x1ff) << 7;      // imm9d   -> imm[15:7]
  imm |= ((x >> 22) & 0x1f)  << 16;     // imm5c   -> imm[20:16]
  imm |= ((x >> 21) & 0x1)   << 21;     // ic      -> imm[21]
  imm |= (uint64_t(l)        << 22);    // L slot  -> imm[62:22]
  imm |= ((x >> 36) & 0x1)   << 63;     // i       -> imm[63]
  return imm;
}

// Rewrite the immediate of an already-emitted movl bundle in place, preserving
// its destination register and qualifying predicate.
inline void WriteMovlImm(Bundle* b, uint64_t imm) {
  const uint64_t kSlotMask = (uint64_t(1) << 41) - 1;
  Insn x = (b->hi >> 23) & kSlotMask;
  uint32_t r1 = uint32_t((x >> 6) & 0x7f);
  uint32_t qp = uint32_t(x & 0x3f);
  *b = MovlBundle(r1, imm, qp);
}

// ---------------------------------------------------------------------------
// One-instruction-per-bundle emission helpers.
// ---------------------------------------------------------------------------

// An M-unit instruction alone in a bundle: MII with nops and a trailing stop.
inline Bundle BundleM(Insn m) { return MakeBundle(tMII_, m, NopI(), NopI()); }

// An I-unit instruction alone in a bundle: the first slot of MII must be an
// M-unit slot, so pad it with an M nop.
inline Bundle BundleI(Insn i) { return MakeBundle(tMII_, NopM(), i, NopI()); }

// A B-unit instruction alone in a bundle.
inline Bundle BundleB(Insn b) { return MakeBundle(tMIB_, NopM(), NopI(), b); }

// An F-unit instruction alone in a bundle.
inline Bundle BundleF(Insn f) { return MakeBundle(tMFI_, NopM(), f, NopI()); }

// A bundle of three nops -- the padding unit for code alignment.
inline Bundle BundleNop() { return MakeBundle(tMII_, NopM(), NopI(), NopI()); }

} // namespace ia64

#endif // CPU_IA64_ASSEMBLER_IA64_CORE_HPP
