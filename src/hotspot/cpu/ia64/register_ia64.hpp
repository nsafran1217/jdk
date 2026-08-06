/*
 * Copyright (c) 2000, 2025, Oracle and/or its affiliates. All rights reserved.
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

#ifndef CPU_IA64_REGISTER_IA64_HPP
#define CPU_IA64_REGISTER_IA64_HPP

#include "asm/register.hpp"
#include "utilities/checkedCast.hpp"
#include "utilities/count_trailing_zeros.hpp"

// IA-64 register model. See FRAME-DESIGN.md sections 1 and 2 for the reasoning;
// the short version:
//
// The architecture has 128 general registers, but r32-r127 are the *register
// stack*: their number and identity depend on the `alloc` executed by the
// current procedure, and the Register Stack Engine spills them asynchronously
// to a backing store growing up from ar.bsp -- a second stack, whose overflow
// was measured on rx2800 to be fatal and *uncatchable* on a pthread
// (tools/gate/FINDINGS.md finding 3).
//
// Therefore generated code allocates only from the 32 static registers r0-r31,
// which every procedure sees identically. The register stack is used solely by
// ABI glue: StubRoutines::call_stub() executes
//
//     alloc rN = ar.pfs, 0, 0, 8, 0
//
// once, to obtain the outgoing-argument window out0-out7 (r32-r39 in that
// frame) needed to call C functions, and nothing else ever refers to r32+.
// Because ins = locals = 0, a later br.call leaves CFM unchanged, so that one
// window stays valid across arbitrarily nested calls.
//
// So this port models 40 general registers: r0-r31 plus the eight-register
// outgoing window. Encoding anything above r39 would name a stacked register
// outside the current frame, which raises an Illegal Operation fault
// (SIGILL/ILL_ILLOPC) rather than faulting where the mistake was made.
//
// Only the low 32 floating-point registers are exposed, for the same reason:
// f32-f127 are the rotating region.

class VMRegImpl;
typedef VMRegImpl* VMReg;

class Register {
 private:
  int _encoding;

  constexpr explicit Register(int encoding) : _encoding(encoding) {}

 public:
  enum {
    number_of_registers      = 40,
    max_slots_per_register   = 2,

    // r0-r31 are the static registers, visible identically to every procedure.
    // Only these may be allocated to values in generated code.
    number_of_static_registers = 32,
    // r32-r39 alias out0-out7 in call_stub's frame.
    number_of_out_registers    = 8,
  };

  class RegisterImpl: public AbstractRegisterImpl {
    friend class Register;

    static constexpr const RegisterImpl* first();

   public:
    // accessors
    constexpr int raw_encoding() const { return checked_cast<int>(this - first()); }
    constexpr int     encoding() const { assert(is_valid(), "invalid register"); return raw_encoding(); }
    constexpr bool    is_valid() const { return 0 <= raw_encoding() && raw_encoding() < number_of_registers; }

    // True for r0-r31, the registers generated code may allocate. r32-r39 are
    // owned by the outgoing-call sequence and must never reach the allocator.
    constexpr bool is_static() const {
      return 0 <= raw_encoding() && raw_encoding() < number_of_static_registers;
    }

    // derived registers, offsets, and addresses
    inline Register successor() const;

    VMReg as_VMReg() const;

    const char* name() const;
  };

  inline friend constexpr Register as_Register(int encoding);

  constexpr Register() : _encoding(-1) {} // noreg

  int operator==(const Register r) const { return _encoding == r._encoding; }
  int operator!=(const Register r) const { return _encoding != r._encoding; }

  constexpr const RegisterImpl* operator->() const { return RegisterImpl::first() + _encoding; }
};

extern Register::RegisterImpl all_RegisterImpls[Register::number_of_registers + 1] INTERNAL_VISIBILITY;

inline constexpr const Register::RegisterImpl* Register::RegisterImpl::first() {
  return all_RegisterImpls + 1;
}

constexpr Register noreg = Register();

inline constexpr Register as_Register(int encoding) {
  if (0 <= encoding && encoding < Register::number_of_registers) {
    return Register(encoding);
  }
  return noreg;
}

inline Register Register::RegisterImpl::successor() const {
  assert(is_valid(), "sanity");
  return as_Register(encoding() + 1);
}

// The general registers of the IA-64 architecture. Roles per the Itanium psABI:
//
//   r0        hardwired 0; writes are illegal
//   r1        gp, the global pointer. Caller-saved: generated code keeps no gp
//             of its own and treats r1 as scratch (FRAME-DESIGN.md 3.4).
//   r2, r3    scratch -- reserved here as the MacroAssembler temporaries
//   r4-r7     PRESERVED. The only four callee-saved static general registers
//             the architecture has, which is why the interpreter has to reload
//             most of its state from the frame after a VM call (2.3).
//   r8-r11    return values and scratch; r8 is the first return value and also
//             carries the buffer address for a large aggregate return
//   r12       sp; 16-byte aligned at all times, grows down
//   r13       tp, the thread pointer -- reserved by the ABI, never touch
//   r14-r31   scratch (caller-saved)
//   r32-r39   out0-out7, the outgoing-argument window
constexpr Register r0   = as_Register( 0);
constexpr Register r1   = as_Register( 1);
constexpr Register r2   = as_Register( 2);
constexpr Register r3   = as_Register( 3);
constexpr Register r4   = as_Register( 4);
constexpr Register r5   = as_Register( 5);
constexpr Register r6   = as_Register( 6);
constexpr Register r7   = as_Register( 7);
constexpr Register r8   = as_Register( 8);
constexpr Register r9   = as_Register( 9);
constexpr Register r10  = as_Register(10);
constexpr Register r11  = as_Register(11);
constexpr Register r12  = as_Register(12);
constexpr Register r13  = as_Register(13);
constexpr Register r14  = as_Register(14);
constexpr Register r15  = as_Register(15);
constexpr Register r16  = as_Register(16);
constexpr Register r17  = as_Register(17);
constexpr Register r18  = as_Register(18);
constexpr Register r19  = as_Register(19);
constexpr Register r20  = as_Register(20);
constexpr Register r21  = as_Register(21);
constexpr Register r22  = as_Register(22);
constexpr Register r23  = as_Register(23);
constexpr Register r24  = as_Register(24);
constexpr Register r25  = as_Register(25);
constexpr Register r26  = as_Register(26);
constexpr Register r27  = as_Register(27);
constexpr Register r28  = as_Register(28);
constexpr Register r29  = as_Register(29);
constexpr Register r30  = as_Register(30);
constexpr Register r31  = as_Register(31);

// The outgoing-argument window created by call_stub's single `alloc`. Never
// allocated to a value; written only by the call sequence.
constexpr Register out0 = as_Register(32);
constexpr Register out1 = as_Register(33);
constexpr Register out2 = as_Register(34);
constexpr Register out3 = as_Register(35);
constexpr Register out4 = as_Register(36);
constexpr Register out5 = as_Register(37);
constexpr Register out6 = as_Register(38);
constexpr Register out7 = as_Register(39);

// Floating-point registers. f0 and f1 are hardwired to +0.0 and +1.0, which is
// what lets the F-unit pseudo-ops (fadd, fmpy, fnorm) assemble to a bare fma:
// IA-64 has no plain add or multiply in the FP unit, only the fused
// multiply-add fma f1 = f3 * f4 + f2.
//
// A double and a single occupy the same architectural register -- registers
// hold an 82-bit extended value and the precision is a property of the
// instruction, not of the register -- so there is no single/double aliasing to
// model. Note that this is also why call_stub must save f2-f5 and f16-f31 with
// stf.spill rather than stfd: a plain double round-trip truncates a C caller's
// long double (FRAME-DESIGN.md 5.1).
//
//   f0, f1    hardwired +0.0 and +1.0
//   f2-f5     PRESERVED -- reserved here for the multi-step divide, sqrt and
//             64x64 multiply sequences, which need this many live at once
//   f6, f7    scratch -- the MacroAssembler FP temporaries
//   f8-f15    argument and return registers; scratch
//   f16-f31   PRESERVED
class FloatRegister {
 private:
  int _encoding;

  constexpr explicit FloatRegister(int encoding) : _encoding(encoding) {}

 public:
  inline friend constexpr FloatRegister as_FloatRegister(int encoding);

  enum {
    number_of_registers     = 32,
    max_slots_per_register  = 2
  };

  class FloatRegisterImpl: public AbstractRegisterImpl {
    friend class FloatRegister;

    static constexpr const FloatRegisterImpl* first();

   public:
    // accessors
    constexpr int raw_encoding() const { return checked_cast<int>(this - first()); }
    constexpr int     encoding() const { assert(is_valid(), "invalid register"); return raw_encoding(); }
    constexpr bool    is_valid() const { return 0 <= raw_encoding() && raw_encoding() < number_of_registers; }

    // derived registers, offsets, and addresses
    inline FloatRegister successor() const;

    VMReg as_VMReg() const;

    const char* name() const;
  };

  constexpr FloatRegister() : _encoding(-1) {} // fnoreg

  int operator==(const FloatRegister r) const { return _encoding == r._encoding; }
  int operator!=(const FloatRegister r) const { return _encoding != r._encoding; }

  constexpr const FloatRegisterImpl* operator->() const { return FloatRegisterImpl::first() + _encoding; }
};

extern FloatRegister::FloatRegisterImpl all_FloatRegisterImpls[FloatRegister::number_of_registers + 1] INTERNAL_VISIBILITY;

inline constexpr const FloatRegister::FloatRegisterImpl* FloatRegister::FloatRegisterImpl::first() {
  return all_FloatRegisterImpls + 1;
}

constexpr FloatRegister fnoreg = FloatRegister();

inline constexpr FloatRegister as_FloatRegister(int encoding) {
  if (0 <= encoding && encoding < FloatRegister::number_of_registers) {
    return FloatRegister(encoding);
  }
  return fnoreg;
}

inline FloatRegister FloatRegister::FloatRegisterImpl::successor() const {
  assert(is_valid(), "sanity");
  return as_FloatRegister(encoding() + 1);
}

constexpr FloatRegister f0     = as_FloatRegister( 0);
constexpr FloatRegister f1     = as_FloatRegister( 1);
constexpr FloatRegister f2     = as_FloatRegister( 2);
constexpr FloatRegister f3     = as_FloatRegister( 3);
constexpr FloatRegister f4     = as_FloatRegister( 4);
constexpr FloatRegister f5     = as_FloatRegister( 5);
constexpr FloatRegister f6     = as_FloatRegister( 6);
constexpr FloatRegister f7     = as_FloatRegister( 7);
constexpr FloatRegister f8     = as_FloatRegister( 8);
constexpr FloatRegister f9     = as_FloatRegister( 9);
constexpr FloatRegister f10    = as_FloatRegister(10);
constexpr FloatRegister f11    = as_FloatRegister(11);
constexpr FloatRegister f12    = as_FloatRegister(12);
constexpr FloatRegister f13    = as_FloatRegister(13);
constexpr FloatRegister f14    = as_FloatRegister(14);
constexpr FloatRegister f15    = as_FloatRegister(15);
constexpr FloatRegister f16    = as_FloatRegister(16);
constexpr FloatRegister f17    = as_FloatRegister(17);
constexpr FloatRegister f18    = as_FloatRegister(18);
constexpr FloatRegister f19    = as_FloatRegister(19);
constexpr FloatRegister f20    = as_FloatRegister(20);
constexpr FloatRegister f21    = as_FloatRegister(21);
constexpr FloatRegister f22    = as_FloatRegister(22);
constexpr FloatRegister f23    = as_FloatRegister(23);
constexpr FloatRegister f24    = as_FloatRegister(24);
constexpr FloatRegister f25    = as_FloatRegister(25);
constexpr FloatRegister f26    = as_FloatRegister(26);
constexpr FloatRegister f27    = as_FloatRegister(27);
constexpr FloatRegister f28    = as_FloatRegister(28);
constexpr FloatRegister f29    = as_FloatRegister(29);
constexpr FloatRegister f30    = as_FloatRegister(30);
constexpr FloatRegister f31    = as_FloatRegister(31);

// ---------------------------------------------------------------------------
// Predicate and branch registers.
//
// Neither is allocated by any register allocator here, so neither needs a VMReg
// representation or an AbstractRegisterImpl -- they exist only so the assembler
// can name them with some type safety.
//
// Predicates: p0 is hardwired true, which is what makes an unpredicated
// instruction simply "predicated on p0". p1-p5 are preserved and unused here;
// p6-p15 are scratch; p16-p63 rotate and are banned for the same reason
// r32-r127 are.
//
// Branch registers: b0 is the return pointer written by br.call. b1-b5 are
// preserved and unused here; b6 and b7 are scratch, and b6 is the indirect
// branch and call target throughout.
// ---------------------------------------------------------------------------

class PredicateRegister {
 private:
  int _encoding;

 public:
  enum { number_of_registers = 64, number_of_usable_registers = 16 };

  constexpr explicit PredicateRegister(int encoding) : _encoding(encoding) {}
  constexpr PredicateRegister() : _encoding(-1) {}

  constexpr int  encoding() const { return _encoding; }
  constexpr bool is_valid()  const { return 0 <= _encoding && _encoding < number_of_registers; }
  // p16-p63 rotate; generated code must not name them.
  constexpr bool is_usable() const { return 0 <= _encoding && _encoding < number_of_usable_registers; }

  int operator==(const PredicateRegister r) const { return _encoding == r._encoding; }
  int operator!=(const PredicateRegister r) const { return _encoding != r._encoding; }

  const char* name() const;
};

constexpr PredicateRegister p0  = PredicateRegister( 0);   // hardwired true
constexpr PredicateRegister p1  = PredicateRegister( 1);
constexpr PredicateRegister p2  = PredicateRegister( 2);
constexpr PredicateRegister p3  = PredicateRegister( 3);
constexpr PredicateRegister p4  = PredicateRegister( 4);
constexpr PredicateRegister p5  = PredicateRegister( 5);
constexpr PredicateRegister p6  = PredicateRegister( 6);
constexpr PredicateRegister p7  = PredicateRegister( 7);
constexpr PredicateRegister p8  = PredicateRegister( 8);
constexpr PredicateRegister p9  = PredicateRegister( 9);
constexpr PredicateRegister p10 = PredicateRegister(10);
constexpr PredicateRegister p11 = PredicateRegister(11);
constexpr PredicateRegister p12 = PredicateRegister(12);
constexpr PredicateRegister p13 = PredicateRegister(13);
constexpr PredicateRegister p14 = PredicateRegister(14);
constexpr PredicateRegister p15 = PredicateRegister(15);

class BranchRegister {
 private:
  int _encoding;

 public:
  enum { number_of_registers = 8 };

  constexpr explicit BranchRegister(int encoding) : _encoding(encoding) {}
  constexpr BranchRegister() : _encoding(-1) {}

  constexpr int  encoding() const { return _encoding; }
  constexpr bool is_valid() const { return 0 <= _encoding && _encoding < number_of_registers; }

  int operator==(const BranchRegister r) const { return _encoding == r._encoding; }
  int operator!=(const BranchRegister r) const { return _encoding != r._encoding; }

  const char* name() const;
};

constexpr BranchRegister b0 = BranchRegister(0);   // return pointer (rp)
constexpr BranchRegister b1 = BranchRegister(1);
constexpr BranchRegister b2 = BranchRegister(2);
constexpr BranchRegister b3 = BranchRegister(3);
constexpr BranchRegister b4 = BranchRegister(4);
constexpr BranchRegister b5 = BranchRegister(5);
constexpr BranchRegister b6 = BranchRegister(6);   // indirect branch/call target
constexpr BranchRegister b7 = BranchRegister(7);

// Application register numbers used by this port. ar.pfs holds the previous
// frame marker written by `alloc` and restored before call_stub's br.ret;
// ar.ccv supplies the comparand for every cmpxchg; ar.unat carries the NaT bits
// of registers saved with st8.spill.
enum ApplicationRegister {
  ar_ccv  = 32,
  ar_unat =  36,
  ar_fpsr =  40,
  ar_pfs  =  64,
  ar_lc   =  65,
  ar_ec   =  66
};

// Need to know the total number of registers of all sorts for SharedInfo.
// Define a class that exports it.
class ConcreteRegisterImpl : public AbstractRegisterImpl {
 public:
  enum {
    max_gpr = Register::number_of_registers * Register::max_slots_per_register,
    max_fpr = max_gpr + FloatRegister::number_of_registers * FloatRegister::max_slots_per_register,

    // Predicate and branch registers are deliberately absent: nothing allocates
    // them, so they need no OptoReg/VMReg numbering.
    number_of_registers = max_fpr
  };
};

typedef AbstractRegSet<Register> RegSet;
typedef AbstractRegSet<FloatRegister> FloatRegSet;

// Note: these count over the full 64-bit _bitset rather than narrowing to
// uint32_t as cpu/riscv does. Narrowing is safe there only because riscv has
// exactly 32 general registers; this port has 40, so out0-out7 would be
// silently dropped from any RegSet. cpu/ppc and cpu/aarch64 use this form.
template <>
inline Register AbstractRegSet<Register>::first() {
  if (_bitset == 0) { return noreg; }
  return as_Register(count_trailing_zeros(_bitset));
}

template <>
inline FloatRegister AbstractRegSet<FloatRegister>::first() {
  if (_bitset == 0) { return fnoreg; }
  return as_FloatRegister(count_trailing_zeros(_bitset));
}

#endif // CPU_IA64_REGISTER_IA64_HPP
