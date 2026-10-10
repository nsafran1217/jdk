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

// The lock-stack fast path of MacroAssembler::lightweight_lock/unlock, plus
// inflated monitors (not with UseObjectMonitorTable), as riscv and x86:
// lock CASes the owner from null to this thread's owner id or counts a
// recursion; unlock counts a recursion down or releases the owner, and
// leaves the runtime only a monitor with waiters but no successor.
void C2_MacroAssembler::fast_lock_lightweight(Register obj, Register box, Register tmp1,
                                              Register tmp2, Register tmp3) {
  assert(LockingMode == LM_LIGHTWEIGHT, "must be");
  assert_different_registers(obj, box, tmp1, tmp2, tmp3, t0, t1, t2);
  Label slow, done, locked, inflated, push;
  const Register mark = tmp1, top = tmp2, t = tmp3;

  if (UseObjectMonitorTable) {
    // Clear the cache in case fast locking succeeds or we take the slow path.
    st8(Address(box, BasicLock::object_monitor_cache_offset_in_bytes()), zr, t);
  }
  if (DiagnoseSyncOnValueBasedClasses != 0) {
    load_klass(t, obj);
    ld1(t, Address(t, Klass::misc_flags_offset()));
    assert(ia64::is_simm8(KlassFlags::_misc_is_value_based_class), "imm8");
    and_imm(t, KlassFlags::_misc_is_value_based_class, t);
    bnez(t, slow);
  }

  assert(oopDesc::mark_offset_in_bytes() == 0, "the cmpxchg below addresses the mark word as obj");
  Assembler::ld8(mark, obj);

  // Lock-stack full?
  ld4(top, Address(Rthread, JavaThread::lock_stack_top_offset()));
  mov(t, (int64_t)LockStack::end_offset());
  bgeu(top, t, slow);

  // Recursive?
  add(t, Rthread, top);
  ld8(t, Address(t, -oopSize));
  beq(obj, t, push);

  // Monitor (0b10)?
  and_imm(t, markWord::monitor_value, mark);
  bnez(t, inflated);

  // Lock bits 0b01 => 0b00.
  or_imm(mark, markWord::unlocked_value, mark);        // expected: unlocked
  and_imm(t, ~(int64_t)markWord::unlocked_value, mark); // new: locked
  mov_to_ar_ccv(mark);
  cmpxchg8_acq(t2, obj, t);
  bne(mark, t2, slow);

  bind(push);
  add(t, Rthread, top);
  Assembler::st8(t, obj);
  adds(top, oopSize, top);
  st4(Address(Rthread, JavaThread::lock_stack_top_offset()), top, t);
  j(locked);

  bind(inflated);
  if (UseObjectMonitorTable) {
    j(slow);
  } else {
    // mark is the monitor pointer tagged with monitor_value.
    const int tag = checked_cast<int>(markWord::monitor_value);
    const Register owner_addr = top, tid = t;
    Label monitor_locked;
    add_imm(owner_addr, mark, in_bytes(ObjectMonitor::owner_offset()) - tag);
    ld8(tid, Address(Rthread, JavaThread::monitor_owner_id_offset()));
    mov_to_ar_ccv(zr);                                  // no owner
    cmpxchg8_acq(t2, owner_addr, tid);
    beqz(t2, locked);
    bne(t2, tid, slow);                                 // owned by another
    // Recursive.
    add_imm(owner_addr, mark, in_bytes(ObjectMonitor::recursions_offset()) - tag);
    Assembler::ld8(t2, owner_addr);
    adds(t2, 1, t2);
    Assembler::st8(owner_addr, t2);
  }

  bind(locked);
  set_flags(this, true);
  j(done);
  bind(slow);
  set_flags(this, false);
  bind(done);
}

void C2_MacroAssembler::fast_unlock_lightweight(Register obj, Register box, Register tmp1,
                                                Register tmp2, Register tmp3) {
  assert(LockingMode == LM_LIGHTWEIGHT, "must be");
  assert_different_registers(obj, box, tmp1, tmp2, tmp3, t0, t1, t2);
  Label slow, done, unlocked, inflated, inflated_load_mark, push_and_slow;
  const Register mark = tmp1, top = tmp2, t = tmp3;

  // Is obj the top of the lock-stack?
  ld4(top, Address(Rthread, JavaThread::lock_stack_top_offset()));
  adds(top, -oopSize, top);
  add(t, Rthread, top);
  Assembler::ld8(t, t);
  bne(obj, t, inflated_load_mark);

  // Pop the lock-stack.
  DEBUG_ONLY(add(t, Rthread, top);)
  DEBUG_ONLY(Assembler::st8(t, zr);)
  st4(Address(Rthread, JavaThread::lock_stack_top_offset()), top, t);

  // Recursive?
  add(t, Rthread, top);
  ld8(t, Address(t, -oopSize));
  beq(obj, t, unlocked);

  // Not recursive. A monitor here means the runtime must fix an anonymous
  // owner: push obj back first.
  assert(oopDesc::mark_offset_in_bytes() == 0, "the cmpxchg below addresses the mark word as obj");
  Assembler::ld8(mark, obj);
  and_imm(t, markWord::monitor_value, mark);
  bnez(t, UseObjectMonitorTable ? push_and_slow : inflated);

  // Lock bits 0b00 => 0b01.
  or_imm(t, markWord::unlocked_value, mark);
  mov_to_ar_ccv(mark);
  cmpxchg8_rel(t2, obj, t);
  beq(mark, t2, unlocked);

  bind(push_and_slow);
  DEBUG_ONLY(add(t, Rthread, top);)
  DEBUG_ONLY(Assembler::st8(t, obj);)
  adds(top, oopSize, top);
  st4(Address(Rthread, JavaThread::lock_stack_top_offset()), top, t);
  j(slow);

  bind(inflated_load_mark);
  Assembler::ld8(mark, obj);
  bind(inflated);
  if (UseObjectMonitorTable) {
    j(slow);
  } else {
#ifdef ASSERT
    Label is_monitor;
    and_imm(t, markWord::monitor_value, mark);
    bnez(t, is_monitor);
    stop("fast_unlock_lightweight: not a monitor");
    bind(is_monitor);
#endif
    const Register monitor = mark, addr = top;
    Label not_recursive;
    adds(monitor, -checked_cast<int>(markWord::monitor_value), mark);
    add_imm(addr, monitor, in_bytes(ObjectMonitor::recursions_offset()));
    Assembler::ld8(t, addr);
    beqz(t, not_recursive);
    adds(t, -1, t);
    Assembler::st8(addr, t);
    j(unlocked);

    bind(not_recursive);
    // Release the owner (st8.rel orders everything before it), then a full
    // fence so that the entry_list/succ loads cannot pass the store: the
    // StoreLoad that keeps a waiter from being stranded.
    add_imm(addr, monitor, in_bytes(ObjectMonitor::owner_offset()));
    st8_rel(addr, zr);
    mf();
    add_imm(addr, monitor, in_bytes(ObjectMonitor::entry_list_offset()));
    Assembler::ld8(t, addr);
    beqz(t, unlocked);                                  // nobody waiting
    add_imm(addr, monitor, in_bytes(ObjectMonitor::succ_offset()));
    Assembler::ld8(t, addr);
    bnez(t, unlocked);                                  // a successor will take it
    // Let SharedRuntime::monitor_exit_helper try to reacquire and hand off.
    st8(Address(Rthread, JavaThread::unlocked_inflated_monitor_offset()), monitor, t);
    j(slow);
  }

  bind(unlocked);
  set_flags(this, true);
  j(done);
  bind(slow);
  set_flags(this, false);
  bind(done);
}

// ---- String and array intrinsics -------------------------------------------

// Compare cnt bytes (a 64-bit count) at a1 and a2, both 8-byte aligned:
// result = 1 if equal, 0 if not. Kills a1, a2, cnt, tmp1, tmp2, t1.
void C2_MacroAssembler::equal_bytes(Register a1, Register a2, Register cnt, Register result,
                                    Register tmp1, Register tmp2) {
  Label loop, tail, tail2, tail1, equal, done;
  mov(result, zr);
  shru_imm(t1, cnt, 3);                 // whole words
  beqz(t1, tail);
  bind(loop);
  ld8_inc(tmp1, a1, 8);
  ld8_inc(tmp2, a2, 8);
  adds(t1, -1, t1);
  cmp_ne(ptmp0, ptmp1, tmp1, tmp2);
  br_cond(done, ptmp0);
  bnez(t1, loop);
  bind(tail);                           // 0-7 bytes left, still aligned
  and_imm(t1, 4, cnt);
  beqz(t1, tail2);
  ld4_inc(tmp1, a1, 4);
  ld4_inc(tmp2, a2, 4);
  cmp_ne(ptmp0, ptmp1, tmp1, tmp2);
  br_cond(done, ptmp0);
  bind(tail2);
  and_imm(t1, 2, cnt);
  beqz(t1, tail1);
  ld2_inc(tmp1, a1, 2);
  ld2_inc(tmp2, a2, 2);
  cmp_ne(ptmp0, ptmp1, tmp1, tmp2);
  br_cond(done, ptmp0);
  bind(tail1);
  and_imm(t1, 1, cnt);
  beqz(t1, equal);
  Assembler::ld1(tmp1, a1);
  Assembler::ld1(tmp2, a2);
  cmp_ne(ptmp0, ptmp1, tmp1, tmp2);
  br_cond(done, ptmp0);
  bind(equal);
  mov_immediate(result, 1);
  bind(done);
}

void C2_MacroAssembler::string_equals(Register str1, Register str2, Register cnt, Register result,
                                      Register tmp1, Register tmp2, Register tmp3, Register tmp4,
                                      Register tmp5) {
  mov(tmp3, str1);
  mov(tmp4, str2);
  sxt4(tmp5, cnt);                      // a lazy int
  equal_bytes(tmp3, tmp4, tmp5, result, tmp1, tmp2);
}

void C2_MacroAssembler::arrays_equals(Register ary1, Register ary2, Register result,
                                      Register tmp1, Register tmp2, Register tmp3, Register tmp4,
                                      Register tmp5, int elem_size) {
  const int length_offset = arrayOopDesc::length_offset_in_bytes();
  const int base_offset = arrayOopDesc::base_offset_in_bytes(elem_size == 1 ? T_BYTE : T_CHAR);
  guarantee(is_aligned(base_offset, BytesPerLong), "word loop needs aligned array data");
  Label same, done;
  mov(result, zr);
  cmp_eq(ptmp0, ptmp1, ary1, ary2);
  br_cond(same, ptmp0);
  beqz(ary1, done);
  beqz(ary2, done);
  adds(tmp1, length_offset, ary1);
  adds(tmp2, length_offset, ary2);
  Assembler::ld4(tmp1, tmp1);
  Assembler::ld4(tmp2, tmp2);
  cmp4_ne(ptmp0, ptmp1, tmp1, tmp2);
  br_cond(done, ptmp0);
  if (elem_size == 2) {
    add(tmp1, tmp1, tmp1);              // bytes (ld4 zero-extends)
  }
  adds(tmp4, base_offset, ary1);
  adds(tmp5, base_offset, ary2);
  equal_bytes(tmp4, tmp5, tmp1, result, tmp2, tmp3);
  j(done);
  bind(same);
  mov_immediate(result, 1);
  bind(done);
}

// The Java semantics (StringLatin1/StringUTF16.compareTo...): the first
// differing chars' difference, else the difference of the lengths in chars.
void C2_MacroAssembler::string_compare(Register str1_in, Register cnt1_in,
                                       Register str2_in, Register cnt2_in, Register result,
                                       Register tmp1, Register tmp2, Register tmp3, Register tmp4,
                                       Register tmp5, int ae) {
  const bool str1_isL = (ae == StrIntrinsicNode::LL || ae == StrIntrinsicNode::LU);
  const bool str2_isL = (ae == StrIntrinsicNode::LL || ae == StrIntrinsicNode::UL);
  const Register str1 = tmp4, str2 = tmp5, cnt1 = tmp1, cnt2 = tmp2;
  Label done, elem_loop, elem_diff, word_diff;
  mov(str1, str1_in);
  mov(str2, str2_in);
  sxt4(cnt1, cnt1_in);
  sxt4(cnt2, cnt2_in);
  if (!str1_isL) shr_imm(cnt1, cnt1, 1);    // chars
  if (!str2_isL) shr_imm(cnt2, cnt2, 1);
  sub(result, cnt1, cnt2);
  cmp_lt(ptmp0, ptmp1, cnt1, cnt2);         // tmp3 = min(cnt1, cnt2), in chars
  mov(tmp3, cnt1, ptmp0);
  mov(tmp3, cnt2, ptmp1);
  beqz(tmp3, done);

  if (str1_isL == str2_isL) {
    const int esize = str1_isL ? 1 : 2;
    Label word_loop, tail;
    if (esize == 2) add(tmp3, tmp3, tmp3);  // bytes
    shru_imm(t1, tmp3, 3);
    beqz(t1, tail);
    bind(word_loop);
    ld8_inc(tmp1, str1, 8);
    ld8_inc(tmp2, str2, 8);
    adds(t1, -1, t1);
    cmp_ne(ptmp0, ptmp1, tmp1, tmp2);
    br_cond(word_diff, ptmp0);
    bnez(t1, word_loop);
    bind(tail);
    and_imm(tmp3, 7, tmp3);                 // bytes left
    if (esize == 2) shru_imm(tmp3, tmp3, 1);  // chars left
    beqz(tmp3, done);
    bind(elem_loop);
    if (esize == 1) {
      ld1_inc(tmp1, str1, 1);
      ld1_inc(tmp2, str2, 1);
    } else {
      ld2_inc(tmp1, str1, 2);
      ld2_inc(tmp2, str2, 2);
    }
    adds(tmp3, -1, tmp3);
    cmp_ne(ptmp0, ptmp1, tmp1, tmp2);
    br_cond(elem_diff, ptmp0);
    bnez(tmp3, elem_loop);
    j(done);

    // The lowest differing element of two little-endian words: shift both
    // right to it and zero-extend.
    bind(word_diff);
    xor_(t1, tmp1, tmp2);
    adds(tmp3, -1, t1);
    andcm(tmp3, tmp3, t1);                  // the bits below the lowest set bit
    popcnt(tmp3, tmp3);
    and_imm(tmp3, esize == 1 ? -8 : -16, tmp3);
    shru(tmp1, tmp1, tmp3);
    shru(tmp2, tmp2, tmp3);
    if (esize == 1) {
      zxt1(tmp1, tmp1);
      zxt1(tmp2, tmp2);
    } else {
      zxt2(tmp1, tmp1);
      zxt2(tmp2, tmp2);
    }
  } else {
    // Mixed encodings: one char at a time (ld1 and ld2 zero-extend).
    bind(elem_loop);
    if (str1_isL) {
      ld1_inc(tmp1, str1, 1);
      ld2_inc(tmp2, str2, 2);
    } else {
      ld2_inc(tmp1, str1, 2);
      ld1_inc(tmp2, str2, 1);
    }
    adds(tmp3, -1, tmp3);
    cmp_ne(ptmp0, ptmp1, tmp1, tmp2);
    br_cond(elem_diff, ptmp0);
    bnez(tmp3, elem_loop);
    j(done);
  }
  bind(elem_diff);                          // word_diff falls in here
  sub(result, tmp1, tmp2);
  bind(done);
}

void C2_MacroAssembler::count_positives(Register ary_in, Register len_in, Register result,
                                        Register tmp1, Register tmp2, Register tmp3,
                                        Register tmp4) {
  const Register ary = tmp3, len = tmp4;
  Label head, words, word_loop, tail, tail_loop, done;
  mov(ary, ary_in);
  sxt4(len, len_in);
  mov(result, zr);
  // Bytes until ary is 8-byte aligned.
  bind(head);
  cmp_eq(ptmp0, ptmp1, result, len);
  br_cond(done, ptmp0);
  and_imm(t1, 7, ary);
  beqz(t1, words);
  ld1_inc(tmp1, ary, 1);
  and_imm(tmp1, -128, tmp1);                // the sign bit (zero-extended load)
  bnez(tmp1, done);
  adds(result, 1, result);
  j(head);
  // Whole words: stop at the first word with a negative byte (the count
  // then names the word's first byte, which the contract allows).
  bind(words);
  mov_immediate(tmp2, (int64_t)0x8080808080808080ULL);
  bind(word_loop);
  sub(t1, len, result);
  mov_immediate(tmp1, 8);
  cmp_lt(ptmp0, ptmp1, t1, tmp1);
  br_cond(tail, ptmp0);
  ld8_inc(tmp1, ary, 8);
  and_(tmp1, tmp1, tmp2);
  bnez(tmp1, done);
  adds(result, 8, result);
  j(word_loop);
  bind(tail);
  bind(tail_loop);
  cmp_eq(ptmp0, ptmp1, result, len);
  br_cond(done, ptmp0);
  ld1_inc(tmp1, ary, 1);
  and_imm(tmp1, -128, tmp1);
  bnez(tmp1, done);
  adds(result, 1, result);
  j(tail_loop);
  bind(done);
}

// Element by element up to an 8-byte boundary, then whole words: czx on the
// word xor the broadcast char finds the lowest matching element (czx's "none
// found" value, 8 or 4, is the elements per word), then the tail.
void C2_MacroAssembler::string_indexof_char(Register str_in, Register cnt_in, Register ch,
                                            Register result, Register tmp1, Register tmp2,
                                            Register tmp3, Register tmp4, bool isL) {
  const Register str = tmp3, cnt = tmp4;  // cnt: chars left
  const int per_word = isL ? 8 : 4;
  Label head, words, word_loop, found_word, tail, not_found, done;
  mov(str, str_in);
  sxt4(cnt, cnt_in);
  mov(result, zr);                        // the index of the next char
  // ch is a lazy int in [0, 0xff] or [0, 0xffff]: its low 32 bits are
  // exact, so cmp4 against a zero-extended load needs no extension.
  if (isL) {
    zxt1(tmp2, ch);
    mux1_brcst(tmp2, tmp2);
  } else {
    zxt2(tmp2, ch);
    mux2(tmp2, tmp2, 0);
  }
  bind(head);
  beqz(cnt, not_found);
  and_imm(t1, 7, str);
  beqz(t1, words);
  if (isL) ld1_inc(tmp1, str, 1); else ld2_inc(tmp1, str, 2);
  cmp4_eq(ptmp0, ptmp1, tmp1, ch);
  br_cond(done, ptmp0);
  adds(result, 1, result);
  adds(cnt, -1, cnt);
  j(head);

  bind(words);
  mov_immediate(t1, per_word);
  bind(word_loop);
  cmp_lt(ptmp0, ptmp1, cnt, t1);
  br_cond(tail, ptmp0);
  ld8_inc(tmp1, str, 8);
  xor_(tmp1, tmp1, tmp2);
  if (isL) czx1_r(tmp1, tmp1); else czx2_r(tmp1, tmp1);
  cmp_ne(ptmp0, ptmp1, tmp1, t1);
  br_cond(found_word, ptmp0);
  adds(result, per_word, result);
  adds(cnt, -per_word, cnt);
  j(word_loop);
  bind(found_word);
  add(result, result, tmp1);
  j(done);

  bind(tail);
  beqz(cnt, not_found);
  if (isL) ld1_inc(tmp1, str, 1); else ld2_inc(tmp1, str, 2);
  cmp4_eq(ptmp0, ptmp1, tmp1, ch);
  br_cond(done, ptmp0);
  adds(result, 1, result);
  adds(cnt, -1, cnt);
  j(tail);

  bind(not_found);
  mov_immediate(result, -1);
  bind(done);
}

// Chars one at a time until dst is 8-byte aligned; then, if src is 4-byte
// aligned too (the usual case: both offsets 0), four at a time with
// ld4, unpack1.l (zero-interleave) and st8; then the rest one at a time.
void C2_MacroAssembler::byte_array_inflate(Register src_in, Register dst_in, Register len_in,
                                           Register tmp1, Register tmp2, Register tmp3,
                                           Register tmp4) {
  const Register src = tmp3, dst = tmp4, len = tmp2;
  Label head, aligned, word_loop, tail, done;
  mov(src, src_in);
  mov(dst, dst_in);
  sxt4(len, len_in);
  bind(head);
  beqz(len, done);
  and_imm(t1, 7, dst);
  beqz(t1, aligned);
  ld1_inc(tmp1, src, 1);
  st2_inc(dst, tmp1, 2);
  adds(len, -1, len);
  j(head);
  bind(aligned);
  and_imm(t1, 3, src);
  bnez(t1, tail);
  bind(word_loop);
  mov_immediate(t1, 4);
  cmp_lt(ptmp0, ptmp1, len, t1);
  br_cond(tail, ptmp0);
  ld4_inc(tmp1, src, 4);
  unpack1_l(tmp1, zr, tmp1);
  st8_inc(dst, tmp1, 8);
  adds(len, -4, len);
  j(word_loop);
  bind(tail);
  beqz(len, done);
  ld1_inc(tmp1, src, 1);
  st2_inc(dst, tmp1, 2);
  adds(len, -1, len);
  j(tail);
  bind(done);
}

// Chars one at a time until dst is 8-byte aligned; then, if src is 8-byte
// aligned too, eight at a time: two ld8, a test that every char is <= 0xff,
// pack2.uss and st8. A word pair holding a wider char, and the rest, go one
// char at a time, which stops at the exact index.
void C2_MacroAssembler::char_array_compress(Register src_in, Register dst_in, Register len_in,
                                            Register result, Register tmp1, Register tmp2,
                                            Register tmp3, Register tmp4, Register tmp5,
                                            bool ascii) {
  const int bits = ascii ? 7 : 8;        // a char fits if char >> bits == 0
  const uint64_t wide = ascii ? 0xff80ff80ff80ff80ULL : 0xff00ff00ff00ff00ULL;
  const Register src = tmp4, dst = tmp5, len = tmp3;   // len: chars left
  Label head, aligned, word_loop, word_fail, tail, done;
  mov(src, src_in);
  mov(dst, dst_in);
  sxt4(len, len_in);
  mov(result, zr);                        // chars done
  bind(head);
  beqz(len, done);
  and_imm(t1, 7, dst);
  beqz(t1, aligned);
  ld2_inc(tmp1, src, 2);
  shru_imm(tmp2, tmp1, bits);
  bnez(tmp2, done);
  st1_inc(dst, tmp1, 1);
  adds(result, 1, result);
  adds(len, -1, len);
  j(head);
  bind(aligned);
  and_imm(t1, 7, src);
  bnez(t1, tail);
  mov_immediate(tmp2, (int64_t)wide);
  bind(word_loop);
  mov_immediate(t1, 8);
  cmp_lt(ptmp0, ptmp1, len, t1);
  br_cond(tail, ptmp0);
  ld8_inc(tmp1, src, 8);
  ld8_inc(t0, src, 8);
  or_(t1, tmp1, t0);
  and_(t1, t1, tmp2);
  bnez(t1, word_fail);
  pack2_uss(tmp1, tmp1, t0);
  st8_inc(dst, tmp1, 8);
  adds(result, 8, result);
  adds(len, -8, len);
  j(word_loop);
  bind(word_fail);
  adds(src, -16, src);
  bind(tail);
  beqz(len, done);
  ld2_inc(tmp1, src, 2);
  shru_imm(tmp2, tmp1, bits);
  bnez(tmp2, done);
  st1_inc(dst, tmp1, 1);
  adds(result, 1, result);
  adds(len, -1, len);
  j(tail);
  bind(done);
}

// Scan for the needle's first char as indexOf(char) does (czx on whole
// words of the haystack once aligned), then compare the rest of the needle
// element by element; on a mismatch resume the scan one char further on.
void C2_MacroAssembler::string_indexof(Register str1, Register cnt1, Register str2, Register cnt2,
                                       Register result, Register tmp1, Register tmp2,
                                       Register tmp3, Register tmp4, Register tmp5,
                                       Register tmp6, Register tmp7, Register tmp8,
                                       Register tmp9, int ae) {
  assert(ae == StrIntrinsicNode::LL || ae == StrIntrinsicNode::UU || ae == StrIntrinsicNode::UL, "encoding");
  const bool hL = (ae == StrIntrinsicNode::LL);   // Latin-1 haystack
  const bool nL = (ae != StrIntrinsicNode::UU);   // Latin-1 needle
  const int hs = hL ? 1 : 2, ns = nL ? 1 : 2;
  const int per_word = 8 / hs;
  // first (the needle's first char) lives in t0 throughout.
  const Register needle = tmp2, cnt = tmp3, cur = tmp4, m = tmp5, bc = tmp6, x = tmp7;
  const Register nc = tmp1, hc = tmp8, y = tmp9, k = t1;
  Label scan, words, word_loop, word_found, tail, candidate, cmp_loop, mismatch, not_found, done;

  mov(needle, str2);
  mov(cur, str1);
  sxt4(m, cnt2);
  sxt4(cnt, cnt1);
  sub(cnt, cnt, m);
  adds(cnt, 1, cnt);                        // possible starts left, >= 1
  if (nL) Assembler::ld1(t0, needle); else Assembler::ld2(t0, needle);
  if (hL) {
    mux1_brcst(bc, t0);                     // t0 is zero-extended by the load
  } else {
    mux2(bc, t0, 0);
  }
  mov(result, zr);                          // the index at cur

  // The scan: from cur, find the next start whose char is first.
  bind(scan);
  beqz(cnt, not_found);
  and_imm(t1, 7, cur);
  beqz(t1, words);
  if (hL) Assembler::ld1(x, cur); else Assembler::ld2(x, cur);
  cmp4_eq(ptmp0, ptmp1, x, t0);
  br_cond(candidate, ptmp0);
  adds(cur, hs, cur);
  adds(result, 1, result);
  adds(cnt, -1, cnt);
  j(scan);

  bind(words);
  mov_immediate(t1, per_word);
  bind(word_loop);
  cmp_lt(ptmp0, ptmp1, cnt, t1);
  br_cond(tail, ptmp0);
  Assembler::ld8(x, cur);
  xor_(x, x, bc);
  if (hL) czx1_r(x, x); else czx2_r(x, x);
  cmp_ne(ptmp0, ptmp1, x, t1);
  br_cond(word_found, ptmp0);
  adds(cur, 8, cur);
  adds(result, per_word, result);
  adds(cnt, -per_word, cnt);
  j(word_loop);
  bind(word_found);                         // x elements on is a match
  add(result, result, x);
  sub(cnt, cnt, x);
  if (hL) add(cur, cur, x); else shladd(cur, x, 1, cur);
  j(candidate);

  bind(tail);
  beqz(cnt, not_found);
  if (hL) Assembler::ld1(x, cur); else Assembler::ld2(x, cur);
  cmp4_eq(ptmp0, ptmp1, x, t0);
  br_cond(candidate, ptmp0);
  adds(cur, hs, cur);
  adds(result, 1, result);
  adds(cnt, -1, cnt);
  j(tail);

  // hay[result] == first: compare needle[1..m-1] with the chars after it.
  bind(candidate);
  adds(nc, ns, needle);
  adds(hc, hs, cur);
  adds(k, -1, m);
  bind(cmp_loop);
  beqz(k, done);                            // the whole needle matched
  if (nL) ld1_inc(x, nc, 1); else ld2_inc(x, nc, 2);
  if (hL) ld1_inc(y, hc, 1); else ld2_inc(y, hc, 2);
  adds(k, -1, k);
  cmp4_eq(ptmp0, ptmp1, x, y);
  br_cond(cmp_loop, ptmp0);
  bind(mismatch);                           // resume one char further on
  adds(cur, hs, cur);
  adds(result, 1, result);
  adds(cnt, -1, cnt);
  j(scan);

  bind(not_found);
  mov_immediate(result, -1);
  bind(done);
}
