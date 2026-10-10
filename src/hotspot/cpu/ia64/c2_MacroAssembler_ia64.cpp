/*
 * Copyright (c) 2020, 2025, Oracle and/or its affiliates. All rights reserved.
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

#include "asm/assembler.hpp"
#include "asm/assembler.inline.hpp"
#include "opto/c2_MacroAssembler.hpp"
#include "opto/compile.hpp"
#include "opto/intrinsicnode.hpp"
#include "opto/output.hpp"
#include "opto/subnode.hpp"
#include "runtime/basicLock.hpp"
#include "runtime/stubRoutines.hpp"
#include "utilities/globalDefinitions.hpp"

#ifdef PRODUCT
#define BLOCK_COMMENT(str) /* nothing */
#define STOP(error) stop(error)
#else
#define BLOCK_COMMENT(str) block_comment(str)
#define STOP(error) block_comment(error); stop(error)
#endif

#define BIND(label) bind(label); BLOCK_COMMENT(#label ":")

// ---- memory access --------------------------------------------------------

static ia64::Insn mem_insn(C2_MacroAssembler::MemOp op, int data, int addr) {
  switch (op) {
    case C2_MacroAssembler::op_ld1:     return ia64::Ld1(data, addr);
    case C2_MacroAssembler::op_ld2:     return ia64::Ld2(data, addr);
    case C2_MacroAssembler::op_ld4:     return ia64::Ld4(data, addr);
    case C2_MacroAssembler::op_ld8:     return ia64::Ld8(data, addr);
    case C2_MacroAssembler::op_ld1_acq: return ia64::Ld1Acq(data, addr);
    case C2_MacroAssembler::op_ld2_acq: return ia64::Ld2Acq(data, addr);
    case C2_MacroAssembler::op_ld4_acq: return ia64::Ld4Acq(data, addr);
    case C2_MacroAssembler::op_ld8_acq: return ia64::Ld8Acq(data, addr);
    case C2_MacroAssembler::op_st1:     return ia64::St1(addr, data);
    case C2_MacroAssembler::op_st2:     return ia64::St2(addr, data);
    case C2_MacroAssembler::op_st4:     return ia64::St4(addr, data);
    case C2_MacroAssembler::op_st8:     return ia64::St8(addr, data);
    case C2_MacroAssembler::op_st1_rel: return ia64::St1Rel(addr, data);
    case C2_MacroAssembler::op_st2_rel: return ia64::St2Rel(addr, data);
    case C2_MacroAssembler::op_st4_rel: return ia64::St4Rel(addr, data);
    case C2_MacroAssembler::op_st8_rel: return ia64::St8Rel(addr, data);
    case C2_MacroAssembler::op_ldfs:    return ia64::Ldfs(data, addr);
    case C2_MacroAssembler::op_ldfd:    return ia64::Ldfd(data, addr);
    case C2_MacroAssembler::op_stfs:    return ia64::Stfs(addr, data);
    case C2_MacroAssembler::op_stfd:    return ia64::Stfd(addr, data);
    default: ShouldNotReachHere(); return 0;
  }
}

// Emitted as barrier bundles (no Deps): neither bundle may lose its stop to
// stop elision, so the access ends its instruction group and the next
// instruction -- which may read the loaded register -- starts a new one.
void C2_MacroAssembler::access(MemOp op, int data, Register base, int disp, bool packable) {
  if (packable) {
    Register addr = base;
    bool int_load = (op == op_ld1 || op == op_ld2 || op == op_ld4 || op == op_ld8);
    if (disp != 0) {
      assert(ia64::is_simm14(disp), "indOffset14");
      addr = int_load ? as_Register(data) : t0;
      adds(addr, disp, base);
    }
    switch (op) {
      case op_ld1:  Assembler::ld1(as_Register(data), addr); return;
      case op_ld2:  Assembler::ld2(as_Register(data), addr); return;
      case op_ld4:  Assembler::ld4(as_Register(data), addr); return;
      case op_ld8:  Assembler::ld8(as_Register(data), addr); return;
      case op_st1:  Assembler::st1(addr, as_Register(data)); return;
      case op_st2:  Assembler::st2(addr, as_Register(data)); return;
      case op_st4:  Assembler::st4(addr, as_Register(data)); return;
      case op_st8:  Assembler::st8(addr, as_Register(data)); return;
      case op_ldfs: Assembler::ldfs(as_FloatRegister(data), addr); return;
      case op_ldfd: Assembler::ldfd(as_FloatRegister(data), addr); return;
      case op_stfs: Assembler::stfs(addr, as_FloatRegister(data)); return;
      case op_stfd: Assembler::stfd(addr, as_FloatRegister(data)); return;
      default: ShouldNotReachHere();
    }
  }
  close_bundle();
  if (disp == 0) {
    emit_bundle(ia64::BundleM(mem_insn(op, data, base->encoding())));
  } else {
    assert(ia64::is_simm14(disp), "indOffset14");
    assert(base != t0, "t0 is the address temporary");
    emit_bundle(ia64::MakeBundle(ia64::tM_MI_,
                                 ia64::Adds(t0->encoding(), disp, base->encoding()),
                                 mem_insn(op, data, t0->encoding()),
                                 ia64::NopI()));
  }
}

// ---- compare and branch ---------------------------------------------------

// The six relations from the three IA-64 compares (eq, lt, ltu), swapping
// operands for gt/le and taking the complement predicate for ne/ge/le.
void C2_MacroAssembler::cmp_preds(int cmpcode, CmpKind kind, Register a, Register b,
                                  PredicateRegister pt, PredicateRegister pf) {
  bool is32 = (kind == cmp_int || kind == cmp_uint);
  bool is_unsigned = (kind == cmp_uint || kind == cmp_ulong);
  switch (cmpcode) {
    case BoolTest::eq:
    case BoolTest::ne: {
      PredicateRegister p1 = (cmpcode == BoolTest::eq) ? pt : pf;
      PredicateRegister p2 = (cmpcode == BoolTest::eq) ? pf : pt;
      if (is32) cmp4_eq(p1, p2, a, b); else cmp_eq(p1, p2, a, b);
      return;
    }
    case BoolTest::lt: case BoolTest::ge:
    case BoolTest::gt: case BoolTest::le: {
      // lt: a < b; ge: !(a < b); gt: b < a; le: !(b < a)
      bool swap = (cmpcode == BoolTest::gt || cmpcode == BoolTest::le);
      bool neg  = (cmpcode == BoolTest::ge || cmpcode == BoolTest::le);
      Register x = swap ? b : a;
      Register y = swap ? a : b;
      PredicateRegister p1 = neg ? pf : pt;
      PredicateRegister p2 = neg ? pt : pf;
      if (is32) {
        if (is_unsigned) cmp4_ltu(p1, p2, x, y); else cmp4_lt(p1, p2, x, y);
      } else {
        if (is_unsigned) cmp_ltu(p1, p2, x, y); else cmp_lt(p1, p2, x, y);
      }
      return;
    }
    default:
      ShouldNotReachHere();
  }
}

void C2_MacroAssembler::cmp_branch(int cmpcode, CmpKind kind, Register a, Register b, Label& L) {
  cmp_preds(cmpcode, kind, a, b, ptmp0, ptmp1);
  br_cond(L, ptmp0);
}

// fcmp.eq/lt/le are false when unordered and their complements true, so the
// relations that must hold for a NaN (lt, le, ne) are complements:
// lt = !(b <= a), le = !(b < a), ne = !(a == b).
void C2_MacroAssembler::float_cmp_preds(int cmpcode, FloatRegister a, FloatRegister b,
                                        PredicateRegister pt, PredicateRegister pf) {
  switch (cmpcode) {
    case BoolTest::eq: fcmp_eq(pt, pf, a, b); break;
    case BoolTest::ne: fcmp_eq(pf, pt, a, b); break;
    case BoolTest::gt: fcmp_lt(pt, pf, b, a); break;
    case BoolTest::ge: fcmp_le(pt, pf, b, a); break;
    case BoolTest::lt: fcmp_le(pf, pt, b, a); break;
    case BoolTest::le: fcmp_lt(pf, pt, b, a); break;
    default: ShouldNotReachHere();
  }
}

void C2_MacroAssembler::float_cmp_branch(int cmpcode, FloatRegister a, FloatRegister b, Label& L) {
  float_cmp_preds(cmpcode, a, b, ptmp0, ptmp1);
  br_cond(L, ptmp0);
}

// ---- locking -----------------------------------------------------------------

// RFLAGS = (p10, p11): "eq" (p10) means the fast path succeeded.
static void set_flags(C2_MacroAssembler* masm, bool success) {
  if (success) {
    masm->cmp_eq(p10, p11, zr, zr);
  } else {
    masm->cmp_eq(p11, p10, zr, zr);
  }
}

void C2_MacroAssembler::fast_lock_lightweight(Register obj, Register box, Register tmp1,
                                              Register tmp2, Register tmp3) {
  Label slow, done;
  if (UseObjectMonitorTable) {
    // Clear the cache in case fast locking succeeds or we take the slow path.
    st8(Address(box, BasicLock::object_monitor_cache_offset_in_bytes()), zr, tmp1);
  }
  lightweight_lock(noreg, obj, tmp1, tmp2, tmp3, slow);
  set_flags(this, true);
  j(done);
  bind(slow);
  set_flags(this, false);
  bind(done);
}

void C2_MacroAssembler::fast_unlock_lightweight(Register obj, Register box, Register tmp1,
                                                Register tmp2, Register tmp3) {
  Label slow, done;
  lightweight_unlock(obj, tmp1, tmp2, tmp3, slow);
  set_flags(this, true);
  j(done);
  bind(slow);
  set_flags(this, false);
  bind(done);
}
