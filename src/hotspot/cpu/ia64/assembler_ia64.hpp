/*
 * Copyright (c) 1997, 2025, Oracle and/or its affiliates. All rights reserved.
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
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef CPU_IA64_ASSEMBLER_IA64_HPP
#define CPU_IA64_ASSEMBLER_IA64_HPP

#include "asm/register.hpp"
#include "assembler_ia64_core.hpp"
#include "utilities/align.hpp"
#include "utilities/powerOfTwo.hpp"

// ---------------------------------------------------------------------------
// Register roles. See FRAME-DESIGN.md section 2.2 for the full rationale.
//
// The governing constraint is that IA-64 has only *four* preserved static
// general registers, r4-r7 -- against twelve on riscv, ten on aarch64, six on
// x86-64. Every other port keeps its interpreter state in callee-saved
// registers and gets it back from a C call for free. Here only the four
// hottest can live there; everything else is caller-saved and must be reloaded
// from the frame after any call that crosses into C++, the way x86-32 does.
// ---------------------------------------------------------------------------

// ABI-fixed.
constexpr Register zr           = r0;    // hardwired zero
constexpr Register gp           = r1;    // global pointer; scratch here (3.4)
constexpr Register sp           = r12;   // stack pointer, 16-byte aligned
constexpr Register tp           = r13;   // thread pointer -- ABI reserved

// MacroAssembler scratch. Two are not enough for the multi-instruction
// sequences (large immediates, address synthesis, CAS retry loops), so four
// are reserved rather than taken from the allocatable set: the register
// allocator has no way to know a masm helper clobbered one.
constexpr Register t0           = r2;
constexpr Register t1           = r3;
constexpr Register t2           = r9;
constexpr Register t3           = r10;
constexpr Register t4           = r11;

// Preserved across a C call -- the only four the architecture gives us.
//
// fp is HotSpot's frame linkage (frame::fp(), the link slot, last_Java_fp),
// not an ABI frame pointer: IA-64 unwinding is table-driven and C code keeps
// no fp chain. It must be preserved because generated code reaches every
// other piece of interpreter state through it once a C call has clobbered
// the scratch registers. SpiderMonkey's IA-64 backend makes the same choice
// (FramePointer = r4). See FRAME-DESIGN.md section 2.2.
constexpr Register fp           = r4;    // frame pointer (HotSpot linkage)
constexpr Register Rthread      = r5;    // current JavaThread
constexpr Register Rbcp         = r6;    // bytecode pointer
constexpr Register Resp         = r7;    // Java expression stack pointer

// Caller-saved: reloaded from the frame after every VM call. Rlocals is
// constant for the life of a frame and lives in the locals slot, so it costs
// one fp-relative load to get back; the interpreter's call_VM_base and
// call_VM_leaf_base do that unconditionally.
constexpr Register Rlocals      = r19;   // locals base
constexpr Register Rmethod      = r14;
constexpr Register Rcpool       = r15;
constexpr Register Rsender_sp   = r18;
// r16 and r17 were Rmonitors and Rdispatch in the first design; neither earns
// a register. The monitor block top lives in the frame (as on riscv), and a
// dispatch table's address is a constant -- safepoints copy table *contents*
// into the active table -- so one movl rematerialises it with no load.
// FRAME-DESIGN.md 9.3, now closed.

// The interpreter's cached top-of-stack: r8 / f8, the psABI integer and FP
// return registers, so a C or Java call's result lands where the template
// expects it.
constexpr Register      Rtos  = r8;
constexpr FloatRegister Ftos  = f8;

// Exception dispatch (forward_exception, catch_exception, the interpreter's
// throw entries, the exception blob): the exception oop in r8, the throwing
// pc in r28. riscv uses x10/x13 the same way; r28 is chosen because it is
// neither an argument register nor MacroAssembler scratch.
constexpr Register Rexception    = r8;
constexpr Register Rexception_pc = r28;

// Return values. r8 also carries the buffer address for a large aggregate
// return, which does not consume out0.
constexpr Register Rret         = r8;

// The outgoing-argument window created by call_stub's single `alloc`. The
// psABI numbers arguments positionally 1-8; each position has both a GR slot
// here and an FP slot in f8-f15, and an argument consumes *both* whichever it
// uses. See ABIArgGenerator-equivalent handling in sharedRuntime_ia64.
constexpr Register c_rarg0 = out0;
constexpr Register c_rarg1 = out1;
constexpr Register c_rarg2 = out2;
constexpr Register c_rarg3 = out3;
constexpr Register c_rarg4 = out4;
constexpr Register c_rarg5 = out5;
constexpr Register c_rarg6 = out6;
constexpr Register c_rarg7 = out7;

constexpr FloatRegister c_farg0 = f8;
constexpr FloatRegister c_farg1 = f9;
constexpr FloatRegister c_farg2 = f10;
constexpr FloatRegister c_farg3 = f11;
constexpr FloatRegister c_farg4 = f12;
constexpr FloatRegister c_farg5 = f13;
constexpr FloatRegister c_farg6 = f14;
constexpr FloatRegister c_farg7 = f15;

// Java arguments in compiled code. Kept in the caller-saved static registers
// so that they survive a Java-to-Java call without touching r4-r7.
constexpr Register j_rarg0 = r20;
constexpr Register j_rarg1 = r21;
constexpr Register j_rarg2 = r22;
constexpr Register j_rarg3 = r23;
constexpr Register j_rarg4 = r24;
constexpr Register j_rarg5 = r25;
constexpr Register j_rarg6 = r26;
constexpr Register j_rarg7 = r27;

constexpr FloatRegister j_farg0 = f8;
constexpr FloatRegister j_farg1 = f9;
constexpr FloatRegister j_farg2 = f10;
constexpr FloatRegister j_farg3 = f11;
constexpr FloatRegister j_farg4 = f12;
constexpr FloatRegister j_farg5 = f13;
constexpr FloatRegister j_farg6 = f14;
constexpr FloatRegister j_farg7 = f15;

class Argument {
 public:
  enum {
    // The psABI numbers C arguments positionally, 1-8: each position has both
    // a GR slot (out0-out7) and an FR slot (f8-f15), and an argument consumes
    // the position whichever kind it uses. So there are 8 register positions
    // in total, not 8 + 8 -- c_calling_convention must count them jointly.
    n_int_register_parameters_c   = 8,  // out0 ... out7 (c_rarg0, c_rarg1, ...)
    n_float_register_parameters_c = 8,  // f8 ... f15    (c_farg0, c_farg1, ...)

    // The Java convention is this port's own choice (FRAME-DESIGN.md 6.2) and
    // is NOT positional: integer and FP arguments are counted independently.
    // SharedRuntime::java_calling_convention must agree with these counts;
    // shared code (signature.cpp) uses them to size the stack arguments.
    n_int_register_parameters_j   = 8,  // r20 ... r27   (j_rarg0, j_rarg1, ...)
    n_float_register_parameters_j = 8   // f8 ... f15    (j_farg0, j_farg1, ...)
  };
};

// FP scratch. f2-f5 are reserved separately as internal temporaries for the
// multi-step divide, sqrt and 64x64 multiply sequences and are never handed
// out; f6/f7 are the general-purpose pair.
constexpr FloatRegister ftmp0 = f6;
constexpr FloatRegister ftmp1 = f7;

// Predicate scratch. p0 is hardwired true, which is what makes an
// unpredicated instruction simply "predicated on p0".
constexpr PredicateRegister pTrue = p0;
constexpr PredicateRegister ptmp0 = p6;
constexpr PredicateRegister ptmp1 = p7;
constexpr PredicateRegister ptmp2 = p8;
constexpr PredicateRegister ptmp3 = p9;

// Branch registers. b0 is the return pointer written by br.call; b6 is the
// indirect branch and call target throughout.
constexpr BranchRegister breturn = b0;
constexpr BranchRegister btmp    = b6;

// Size of one instruction bundle. Every emitted instruction occupies exactly
// one, so this is also the instruction size everywhere in this port.
const int BytesPerBundle = 16;

// ---------------------------------------------------------------------------
// Address
//
// IA-64 has no displacement addressing: ld8/st8 take a bare register. So an
// Address is not something the hardware understands -- it is a request that
// MacroAssembler materialise base+offset into a scratch register before the
// access. Keeping the abstraction anyway is what lets the interpreter and C1
// be written in the same shape as the other ports.
// ---------------------------------------------------------------------------

class Address {
 public:
  enum mode { no_mode, base_plus_offset, literal };

 private:
  Register _base;
  int64_t  _offset;
  enum mode _mode;

  RelocationHolder _rspec;
  // If the target is far away or not yet known, the address is materialised
  // from a 64-bit literal instead of from a base register.
  address  _target;

 public:
  Address()
    : _base(noreg), _offset(0), _mode(no_mode), _target(nullptr) {}

  // explicit: st8(Register, Register) and st8(Address, Register) must never be
  // confused by an implicit conversion -- they take their operands in the
  // same order but mean different things if one is silently an Address.
  explicit Address(Register base, int64_t offset = 0)
    : _base(base), _offset(offset), _mode(base_plus_offset), _target(nullptr) {}
  Address(Register base, ByteSize offset)
    : _base(base), _offset(in_bytes(offset)), _mode(base_plus_offset), _target(nullptr) {}

  Address(address target, const RelocationHolder& rspec)
    : _base(noreg), _offset(0), _mode(literal), _rspec(rspec), _target(target) {}

  Register base()   const { assert(_mode == base_plus_offset, "wrong mode"); return _base; }
  int64_t  offset() const { assert(_mode == base_plus_offset, "wrong mode"); return _offset; }
  address  target() const { assert(_mode == literal, "wrong mode"); return _target; }
  enum mode getMode() const { return _mode; }

  const RelocationHolder& rspec() const { return _rspec; }

  // True when the offset fits the 14-bit signed immediate of `adds`, i.e. when
  // materialising the address costs one instruction rather than three.
  bool offset_is_simm14() const {
    return _mode == base_plus_offset && ia64::is_simm14(_offset);
  }
};

// ---------------------------------------------------------------------------
// Assembler
//
// A thin, typed layer over assembler_ia64_core.hpp. Its only jobs are to turn
// Register/FloatRegister objects into encodings and to push finished bundles
// into the CodeBuffer; all encoding knowledge lives in the core header, which
// is differential-tested against GNU as by tools/enc-difftest.sh.
// ---------------------------------------------------------------------------

class Assembler : public AbstractAssembler {
 public:
  Assembler(CodeBuffer* code) : AbstractAssembler(code) {}

  // Every instruction is a whole bundle, so instruction length is constant.
  static unsigned int instr_len(unsigned char* instr) { return BytesPerBundle; }
  static unsigned int instr_maxlen()                  { return BytesPerBundle; }

  // The instruction after the one at |inst|. A pc from a signal context may
  // carry a slot number in its low bits; one instruction per bundle makes the
  // next instruction simply the next bundle.
  static address locate_next_instruction(address inst) {
    return align_down(inst, BytesPerBundle) + BytesPerBundle;
  }

  // Resolve a label reference recorded at |branch| now that the label is
  // bound to |target|. Two kinds of site are ever registered:
  //   - an IP-relative branch or call, alone in slot 2 of an MIB bundle;
  //   - the movl of an la() sequence (see below), an MLX bundle, which holds
  //     target minus the address of the `mov r = ip` bundle just before it.
  // pd_patch_instruction tells them apart by bundle template.
  void pd_patch_instruction(address branch, address target, const char* file, int line);

  // ---- raw bundle emission ------------------------------------------------

  void emit_bundle(ia64::Bundle b) {
    assert(is_aligned(offset(), BytesPerBundle),
           "code position must stay bundle-aligned");
    // Little-endian target, so emitting lo then hi lays the 128-bit bundle out
    // exactly as the hardware reads it.
    emit_int64(b.lo);
    emit_int64(b.hi);
  }

  // One instruction per bundle, padded with unit-appropriate nops and a
  // trailing stop bit. See the [IA64DOC] comment in assembler_ia64_core.hpp.
  void emit_m(ia64::Insn i) { emit_bundle(ia64::BundleM(i)); }
  void emit_i(ia64::Insn i) { emit_bundle(ia64::BundleI(i)); }
  void emit_f(ia64::Insn i) { emit_bundle(ia64::BundleF(i)); }
  void emit_b(ia64::Insn i) { emit_bundle(ia64::BundleB(i)); }

  void nop() { emit_bundle(ia64::BundleNop()); }

  // Every form below takes an optional trailing qualifying predicate. The
  // instruction executes only if it is true; the default, p0, is hardwired
  // true. Predication is how IA-64 expresses short conditional code without
  // a branch, and the MacroAssembler leans on it heavily.
#define QP PredicateRegister qp = pTrue
#define Q  qp.encoding()

  // ---- loads and stores (M unit) -----------------------------------------
  //
  // No displacement form exists; the address must already be in a register.
  // IA-64 loads zero-extend: a signed narrow load needs an sxt afterwards.

  void ld1(Register r1, Register r3, QP)  { emit_m(ia64::Ld1(r1->encoding(), r3->encoding(), Q)); }
  void ld2(Register r1, Register r3, QP)  { emit_m(ia64::Ld2(r1->encoding(), r3->encoding(), Q)); }
  void ld4(Register r1, Register r3, QP)  { emit_m(ia64::Ld4(r1->encoding(), r3->encoding(), Q)); }
  void ld8(Register r1, Register r3, QP)  { emit_m(ia64::Ld8(r1->encoding(), r3->encoding(), Q)); }

  void st1(Register r3, Register r2, QP)  { emit_m(ia64::St1(r3->encoding(), r2->encoding(), Q)); }
  void st2(Register r3, Register r2, QP)  { emit_m(ia64::St2(r3->encoding(), r2->encoding(), Q)); }
  void st4(Register r3, Register r2, QP)  { emit_m(ia64::St4(r3->encoding(), r2->encoding(), Q)); }
  void st8(Register r3, Register r2, QP)  { emit_m(ia64::St8(r3->encoding(), r2->encoding(), Q)); }

  // Post-increment: access [r3], then r3 += imm9 (signed 9 bits). The one
  // addressing mode beyond a bare register; it is how the expression stack is
  // pushed and popped without a separate adds.
  void ld1_inc(Register r1, Register r3, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::Ld1Inc(r1->encoding(), r3->encoding(), imm9, Q)); }
  void ld2_inc(Register r1, Register r3, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::Ld2Inc(r1->encoding(), r3->encoding(), imm9, Q)); }
  void ld4_inc(Register r1, Register r3, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::Ld4Inc(r1->encoding(), r3->encoding(), imm9, Q)); }
  void ld8_inc(Register r1, Register r3, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::Ld8Inc(r1->encoding(), r3->encoding(), imm9, Q)); }
  void st1_inc(Register r3, Register r2, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::St1Inc(r3->encoding(), r2->encoding(), imm9, Q)); }
  void st2_inc(Register r3, Register r2, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::St2Inc(r3->encoding(), r2->encoding(), imm9, Q)); }
  void st4_inc(Register r3, Register r2, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::St4Inc(r3->encoding(), r2->encoding(), imm9, Q)); }
  void st8_inc(Register r3, Register r2, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::St8Inc(r3->encoding(), r2->encoding(), imm9, Q)); }

  void ld4_acq(Register r1, Register r3, QP) { emit_m(ia64::Ld4Acq(r1->encoding(), r3->encoding(), Q)); }
  void ld8_acq(Register r1, Register r3, QP) { emit_m(ia64::Ld8Acq(r1->encoding(), r3->encoding(), Q)); }
  void st4_rel(Register r3, Register r2, QP) { emit_m(ia64::St4Rel(r3->encoding(), r2->encoding(), Q)); }
  void st8_rel(Register r3, Register r2, QP) { emit_m(ia64::St8Rel(r3->encoding(), r2->encoding(), Q)); }

  // NaT-preserving forms. call_stub must use these for the C caller's r4-r7:
  // a plain ld8/st8 round-trip drops the NaT bit, and st8.spill deposits it
  // into ar.unat, which must be saved and restored around the block.
  void ld8_fill(Register r1, Register r3, QP)  { emit_m(ia64::Ld8Fill(r1->encoding(), r3->encoding(), Q)); }
  void st8_spill(Register r3, Register r2, QP) { emit_m(ia64::St8Spill(r3->encoding(), r2->encoding(), Q)); }

  void ldfs(FloatRegister f1, Register r3, QP) { emit_m(ia64::Ldfs(f1->encoding(), r3->encoding(), Q)); }
  void ldfd(FloatRegister f1, Register r3, QP) { emit_m(ia64::Ldfd(f1->encoding(), r3->encoding(), Q)); }
  void stfs(Register r3, FloatRegister f2, QP) { emit_m(ia64::Stfs(r3->encoding(), f2->encoding(), Q)); }
  void stfd(Register r3, FloatRegister f2, QP) { emit_m(ia64::Stfd(r3->encoding(), f2->encoding(), Q)); }

  // Full 82-bit FP save/restore, 16 bytes and 16-byte aligned. Required for
  // f2-f5 and f16-f31 in call_stub: a double round-trip would silently
  // truncate a C caller's long double.
  void ldf_fill(FloatRegister f1, Register r3, QP)  { emit_m(ia64::LdfFill(f1->encoding(), r3->encoding(), Q)); }
  void stf_spill(Register r3, FloatRegister f2, QP) { emit_m(ia64::StfSpill(r3->encoding(), f2->encoding(), Q)); }

  // ---- ALU (A unit, issued on M here since we pad to MII) ----------------

  void add(Register r1, Register r2, Register r3, QP)   { emit_m(ia64::Add(r1->encoding(), r2->encoding(), r3->encoding(), Q)); }
  void sub(Register r1, Register r2, Register r3, QP)   { emit_m(ia64::Sub(r1->encoding(), r2->encoding(), r3->encoding(), Q)); }
  void and_(Register r1, Register r2, Register r3, QP)  { emit_m(ia64::And(r1->encoding(), r2->encoding(), r3->encoding(), Q)); }
  void andcm(Register r1, Register r2, Register r3, QP) { emit_m(ia64::Andcm(r1->encoding(), r2->encoding(), r3->encoding(), Q)); }
  void or_(Register r1, Register r2, Register r3, QP)   { emit_m(ia64::Or(r1->encoding(), r2->encoding(), r3->encoding(), Q)); }
  void xor_(Register r1, Register r2, Register r3, QP)  { emit_m(ia64::Xor(r1->encoding(), r2->encoding(), r3->encoding(), Q)); }

  // The 8-bit immediate forms. Note the immediate is the FIRST source:
  // sub_imm(r1, imm8, r3) computes imm8 - r3 (so negation is sub_imm(r1, 0, r3)).
  void and_imm(Register r1, int64_t imm8, Register r3, QP) { assert(ia64::is_simm8(imm8), "imm8"); emit_m(ia64::AndImm(r1->encoding(), imm8, r3->encoding(), Q)); }
  void or_imm(Register r1, int64_t imm8, Register r3, QP)  { assert(ia64::is_simm8(imm8), "imm8"); emit_m(ia64::OrImm(r1->encoding(), imm8, r3->encoding(), Q)); }
  void xor_imm(Register r1, int64_t imm8, Register r3, QP) { assert(ia64::is_simm8(imm8), "imm8"); emit_m(ia64::XorImm(r1->encoding(), imm8, r3->encoding(), Q)); }
  void sub_imm(Register r1, int64_t imm8, Register r3, QP) { assert(ia64::is_simm8(imm8), "imm8"); emit_m(ia64::SubImm(r1->encoding(), imm8, r3->encoding(), Q)); }

  // r1 = (r2 << count) + r3, count 1..4: base + index * scale in one step.
  void shladd(Register r1, Register r2, int count, Register r3, QP) {
    emit_m(ia64::Shladd(r1->encoding(), r2->encoding(), count, r3->encoding(), Q));
  }

  // adds is also the register move (imm == 0) and the only way to add a small
  // constant; anything wider than 14 bits signed needs movl + add.
  void adds(Register r1, int64_t imm14, Register r3, QP) {
    assert(ia64::is_simm14(imm14), "immediate too wide for adds -- use movl + add");
    emit_m(ia64::Adds(r1->encoding(), imm14, r3->encoding(), Q));
  }
  void mov(Register r1, Register r3, QP) { emit_m(ia64::MovReg(r1->encoding(), r3->encoding(), Q)); }

  // ---- compare (A unit) --------------------------------------------------
  //
  // Each writes p1 = relation and p2 = !relation. The 64-bit forms compare
  // whole registers; the cmp4 forms only the low 32 bits, which is what Java
  // int comparisons want. The immediate forms take the 8-bit immediate as the
  // FIRST operand: cmp_lt_imm(p1, p2, 5, r) tests 5 < r.

  void cmp_eq(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_m(ia64::CmpEq(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q)); }
  void cmp_ne(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_m(ia64::CmpNe(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q)); }
  void cmp_lt(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_m(ia64::CmpLt(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q)); }
  void cmp_ltu(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP) { emit_m(ia64::CmpLtu(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q)); }

  void cmp4_eq(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_m(ia64::Cmp4Eq(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q)); }
  void cmp4_ne(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_m(ia64::Cmp4Ne(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q)); }
  void cmp4_lt(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_m(ia64::Cmp4Lt(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q)); }
  void cmp4_ltu(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP) { emit_m(ia64::Cmp4Ltu(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q)); }

#define CMP_IMM(name, Enc) \
  void name(PredicateRegister p1, PredicateRegister p2, int64_t imm8, Register r3, QP) { \
    assert(ia64::is_simm8(imm8), "imm8"); \
    emit_m(ia64::Enc(p1.encoding(), p2.encoding(), imm8, r3->encoding(), Q)); \
  }
  CMP_IMM(cmp_eq_imm,   CmpEqImm)
  CMP_IMM(cmp_ne_imm,   CmpNeImm)
  CMP_IMM(cmp_lt_imm,   CmpLtImm)
  CMP_IMM(cmp_ltu_imm,  CmpLtuImm)
  CMP_IMM(cmp4_eq_imm,  Cmp4EqImm)
  CMP_IMM(cmp4_ne_imm,  Cmp4NeImm)
  CMP_IMM(cmp4_lt_imm,  Cmp4LtImm)
  CMP_IMM(cmp4_ltu_imm, Cmp4LtuImm)
#undef CMP_IMM

  // p1 = (bit pos of r3 is clear) / (is set); p2 the complement.
  void tbit_z(PredicateRegister p1, PredicateRegister p2, Register r3, int pos, QP)  { emit_i(ia64::TbitZ(p1.encoding(), p2.encoding(), r3->encoding(), pos, Q)); }
  void tbit_nz(PredicateRegister p1, PredicateRegister p2, Register r3, int pos, QP) { emit_i(ia64::TbitNz(p1.encoding(), p2.encoding(), r3->encoding(), pos, Q)); }

  // ---- shifts, extends (I unit) ------------------------------------------

  void shl(Register r1, Register value, Register count, QP)  { emit_i(ia64::Shl(r1->encoding(), value->encoding(), count->encoding(), Q)); }
  void shr(Register r1, Register value, Register count, QP)  { emit_i(ia64::Shr(r1->encoding(), value->encoding(), count->encoding(), Q)); }
  void shru(Register r1, Register value, Register count, QP) { emit_i(ia64::ShrU(r1->encoding(), value->encoding(), count->encoding(), Q)); }

  void shl_imm(Register r1, Register r2, uint32_t count, QP)  { emit_i(ia64::ShlImm(r1->encoding(), r2->encoding(), count, Q)); }
  void shr_imm(Register r1, Register r3, uint32_t count, QP)  { emit_i(ia64::ShrImm(r1->encoding(), r3->encoding(), count, Q)); }
  void shru_imm(Register r1, Register r3, uint32_t count, QP) { emit_i(ia64::ShrUImm(r1->encoding(), r3->encoding(), count, Q)); }

  void extr_u(Register r1, Register r3, uint32_t pos, uint32_t len, QP) { emit_i(ia64::ExtrU(r1->encoding(), r3->encoding(), pos, len, Q)); }
  void extr(Register r1, Register r3, uint32_t pos, uint32_t len, QP)   { emit_i(ia64::Extr(r1->encoding(), r3->encoding(), pos, len, Q)); }
  void dep_z(Register r1, Register r2, uint32_t pos, uint32_t len, QP)  { emit_i(ia64::DepZ(r1->encoding(), r2->encoding(), pos, len, Q)); }

  void sxt1(Register r1, Register r3, QP) { emit_i(ia64::Sxt1(r1->encoding(), r3->encoding(), Q)); }
  void sxt2(Register r1, Register r3, QP) { emit_i(ia64::Sxt2(r1->encoding(), r3->encoding(), Q)); }
  void sxt4(Register r1, Register r3, QP) { emit_i(ia64::Sxt4(r1->encoding(), r3->encoding(), Q)); }
  void zxt1(Register r1, Register r3, QP) { emit_i(ia64::Zxt1(r1->encoding(), r3->encoding(), Q)); }
  void zxt2(Register r1, Register r3, QP) { emit_i(ia64::Zxt2(r1->encoding(), r3->encoding(), Q)); }
  void zxt4(Register r1, Register r3, QP) { emit_i(ia64::Zxt4(r1->encoding(), r3->encoding(), Q)); }

  void popcnt(Register r1, Register r3, QP) { emit_i(ia64::Popcnt(r1->encoding(), r3->encoding(), Q)); }

  // r1 = address of the bundle holding this instruction.
  void mov_from_ip(Register r1, QP) { emit_i(ia64::MovFromIp(r1->encoding(), Q)); }

  // ---- atomics (M unit) --------------------------------------------------
  //
  // cmpxchg takes its comparand from ar.ccv, whose exact bit pattern is
  // compared -- a narrow cmpxchg must be given a comparand zero-extended to
  // the access width, or it can never succeed.

  void mov_to_ar_ccv(Register r2, QP) { emit_m(ia64::MovToArCcv(r2->encoding(), Q)); }

  void cmpxchg4_acq(Register r1, Register r3, Register r2, QP) { emit_m(ia64::Cmpxchg4Acq(r1->encoding(), r3->encoding(), r2->encoding(), Q)); }
  void cmpxchg8_acq(Register r1, Register r3, Register r2, QP) { emit_m(ia64::Cmpxchg8Acq(r1->encoding(), r3->encoding(), r2->encoding(), Q)); }
  void cmpxchg4_rel(Register r1, Register r3, Register r2, QP) { emit_m(ia64::Cmpxchg4Rel(r1->encoding(), r3->encoding(), r2->encoding(), Q)); }
  void cmpxchg8_rel(Register r1, Register r3, Register r2, QP) { emit_m(ia64::Cmpxchg8Rel(r1->encoding(), r3->encoding(), r2->encoding(), Q)); }
  void xchg4(Register r1, Register r3, Register r2, QP)        { emit_m(ia64::Xchg4(r1->encoding(), r3->encoding(), r2->encoding(), Q)); }
  void xchg8(Register r1, Register r3, Register r2, QP)        { emit_m(ia64::Xchg8(r1->encoding(), r3->encoding(), r2->encoding(), Q)); }

  void mf() { emit_m(ia64::Mf()); }

  // ---- application and branch registers ----------------------------------

  void mov_from_ar(Register r1, uint32_t ar, QP) {
    // ar.pfs and ar.lc are I-unit registers; ar.ccv/unat/fpsr are M-unit.
    if (ia64::ar_is_m_unit(ar)) emit_m(ia64::MovFromAr(r1->encoding(), ar, Q));
    else                        emit_i(ia64::MovFromAr(r1->encoding(), ar, Q));
  }
  void mov_to_ar(uint32_t ar, Register r2, QP) {
    if (ia64::ar_is_m_unit(ar)) emit_m(ia64::MovToAr(ar, r2->encoding(), Q));
    else                        emit_i(ia64::MovToAr(ar, r2->encoding(), Q));
  }

  void mov_from_pfs(Register r1) { mov_from_ar(r1, ia64::kArPfs); }
  void mov_to_pfs(Register r2)   { mov_to_ar(ia64::kArPfs, r2); }

  void mov_to_br(BranchRegister b1, Register r2, QP)   { emit_i(ia64::MovToBr(b1.encoding(), r2->encoding(), Q)); }
  void mov_from_br(Register r1, BranchRegister b2, QP) { emit_i(ia64::MovFromBr(r1->encoding(), b2.encoding(), Q)); }

  // The one and only alloc, in StubRoutines::call_stub(). See FRAME-DESIGN.md
  // section 1: a second one anywhere invalidates the CFM invariant that makes
  // br.ret safe in generated code. Debug builds assert it is emitted once.
  void alloc(Register r1, uint32_t ins, uint32_t locals, uint32_t outs, uint32_t rot = 0);

  // ---- branches (B unit) --------------------------------------------------

  void br_cond(BranchRegister b2, QP)                    { emit_b(ia64::BrCond(b2.encoding(), Q)); }
  void br_ret(BranchRegister b2 = breturn, QP)           { emit_b(ia64::BrRet(b2.encoding(), Q)); }
  void br_call(BranchRegister b1, BranchRegister b2, QP) { emit_b(ia64::BrCall(b1.encoding(), b2.encoding(), Q)); }

  // IP-relative, to a label: +/-16 MiB, measured in bundles from this one.
  // Enough for any branch within one blob; anything that may be farther (a
  // call into another blob, a stub) goes through a branch register instead.
  void br_cond(Label& L, QP) {
    address dest = target(L);
    emit_b(ia64::BrCondRel(bundle_disp(dest), Q));
  }
  void br_call(BranchRegister b1, Label& L, QP) {
    address dest = target(L);
    emit_b(ia64::BrCallRel(b1.encoding(), bundle_disp(dest), Q));
  }
  // An unconditional jump is just a branch on p0.
  void br(Label& L) { br_cond(L); }

  void brk(uint32_t imm21, QP) { emit_b(ia64::BreakB(imm21, Q)); }

  // ---- 64-bit immediates (MLX) -------------------------------------------
  //
  // The port has no gp-relative addressing, and IP-relative branches reach
  // only +/-16 MiB, so every absolute address is materialised here.
  void movl(Register r1, uint64_t imm, QP) { emit_bundle(ia64::MovlBundle(r1->encoding(), imm, Q)); }
  void movl(Register r1, address a, QP)    { movl(r1, (uint64_t)(uintptr_t)a, qp); }

  // Load the address of a label, position-independently:
  //     mov  r1 = ip              // the address of this bundle, B
  //     movl tmp = L - B          // patched when L is bound
  //     add  r1 = r1, tmp
  // No relocation is needed, so the code may be copied out of its CodeBuffer
  // freely. Three bundles; used for return addresses (set_last_Java_frame)
  // and exception-handler addresses, never on a hot path.
  void la(Register r1, Label& L, Register tmp);

  // ---- floating point (F unit) -------------------------------------------

  void fma_d(FloatRegister f1, FloatRegister f3, FloatRegister f4, FloatRegister f2, QP) {
    emit_f(ia64::FmaD(f1->encoding(), f3->encoding(), f4->encoding(), f2->encoding(), ia64::sf0, Q));
  }
  void fadd_d(FloatRegister f1, FloatRegister f3, FloatRegister f2, QP) { emit_f(ia64::FaddD(f1->encoding(), f3->encoding(), f2->encoding(), ia64::sf0, Q)); }
  void fsub_d(FloatRegister f1, FloatRegister f3, FloatRegister f2, QP) { emit_f(ia64::FsubD(f1->encoding(), f3->encoding(), f2->encoding(), ia64::sf0, Q)); }
  void fmpy_d(FloatRegister f1, FloatRegister f3, FloatRegister f4, QP) { emit_f(ia64::FmpyD(f1->encoding(), f3->encoding(), f4->encoding(), ia64::sf0, Q)); }
  void fmov_d(FloatRegister f1, FloatRegister f3, QP)                   { emit_f(ia64::FmovD(f1->encoding(), f3->encoding(), Q)); }

  void getf_d(Register r1, FloatRegister f2, QP)   { emit_m(ia64::GetfD(r1->encoding(), f2->encoding(), Q)); }
  void setf_d(FloatRegister f1, Register r2, QP)   { emit_m(ia64::SetfD(f1->encoding(), r2->encoding(), Q)); }
  void getf_s(Register r1, FloatRegister f2, QP)   { emit_m(ia64::GetfS(r1->encoding(), f2->encoding(), Q)); }
  void setf_s(FloatRegister f1, Register r2, QP)   { emit_m(ia64::SetfS(f1->encoding(), r2->encoding(), Q)); }
  void getf_sig(Register r1, FloatRegister f2, QP) { emit_m(ia64::GetfSig(r1->encoding(), f2->encoding(), Q)); }
  void setf_sig(FloatRegister f1, Register r2, QP) { emit_m(ia64::SetfSig(f1->encoding(), r2->encoding(), Q)); }

  // Integer multiply goes through the FP significand path: the integer units
  // have no multiplier.
  void xma_l(FloatRegister f1, FloatRegister f3, FloatRegister f4, FloatRegister f2, QP) {
    emit_f(ia64::XmaL(f1->encoding(), f3->encoding(), f4->encoding(), f2->encoding(), Q));
  }

#undef QP
#undef Q

 protected:
  // Displacement from the bundle being emitted to |dest|, in bundles.
  int32_t bundle_disp(address dest) {
    intptr_t d = dest - pc();
    assert(is_aligned(d, BytesPerBundle), "branch target must be bundle-aligned");
    d /= BytesPerBundle;
    guarantee(ia64::BranchDispInRange((int32_t)d), "branch out of +/-16 MiB range");
    return (int32_t)d;
  }
};

#endif // CPU_IA64_ASSEMBLER_IA64_HPP
