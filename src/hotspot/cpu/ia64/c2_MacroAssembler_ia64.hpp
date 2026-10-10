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

#ifndef CPU_IA64_C2_MACROASSEMBLER_IA64_HPP
#define CPU_IA64_C2_MACROASSEMBLER_IA64_HPP

// C2_MacroAssembler contains high-level macros for C2

 public:
  // ---- memory access (C2-DESIGN.md section 5) -------------------------------
  //
  // The access kinds an .ad memory node emits.
  enum MemOp {
    op_ld1, op_ld2, op_ld4, op_ld8, op_ld1_acq, op_ld2_acq, op_ld4_acq, op_ld8_acq,
    op_st1, op_st2, op_st4, op_st8, op_st1_rel, op_st2_rel, op_st4_rel, op_st8_rel,
    op_ldfs, op_ldfd, op_stfs, op_stfd
  };

  // One access at base + disp, as exactly one bundle: { op [base] } or, for a
  // non-zero 14-bit displacement, the M;;MI bundle { adds t0 = disp, base ;;
  // op [t0] ; nop.i }. The node therefore starts at its faulting bundle, which
  // is what makes implicit null checks work: the signal handler looks the
  // fault up at the bundle address. data is the register loaded or stored
  // (a GR or FR encoding, by op).
  //
  // packable: none of that is needed (the node is not an implicit null
  // check's faulting access, and the method has no Unsafe accesses, whose
  // fault handler resumes at the next bundle): emit ordinary instructions
  // the packer may share bundles with, the address in the loaded register
  // (t0 for FP loads and for stores). Acquire/release forms always use the
  // single-bundle form.
  void access(MemOp op, int data, Register base, int disp, bool packable = false);

  // ---- compare and branch (C2-DESIGN.md section 4) ---------------------------
  //
  // The relation is a BoolTest::mask ($cop$$cmpcode).
  enum CmpKind { cmp_int, cmp_uint, cmp_long, cmp_ulong };

  // Branch to L if (a <cmpcode> b), comparing as kind. Clobbers p6/p7.
  void cmp_branch(int cmpcode, CmpKind kind, Register a, Register b, Label& L);
  // Floating point: unordered counts as less, as everywhere in C2 (lt, le and
  // ne are taken for a NaN operand; gt, ge and eq are not).
  void float_cmp_branch(int cmpcode, FloatRegister a, FloatRegister b, Label& L);

  // Set (pt, pf) to (a <cmpcode> b, its complement).
  void cmp_preds(int cmpcode, CmpKind kind, Register a, Register b,
                 PredicateRegister pt, PredicateRegister pf);
  void float_cmp_preds(int cmpcode, FloatRegister a, FloatRegister b,
                       PredicateRegister pt, PredicateRegister pf);

  // ---- locking ---------------------------------------------------------------
  //
  // FastLock/FastUnlock (lightweight locking). The result is the RFLAGS pair:
  // p10 set (and p11 clear) on success, the reverse when the runtime must
  // finish the job. Inflated monitors always take the slow path.
  void fast_lock_lightweight(Register obj, Register box, Register tmp1, Register tmp2, Register tmp3);
  void fast_unlock_lightweight(Register obj, Register box, Register tmp1, Register tmp2, Register tmp3);

  // ---- String and array intrinsics -------------------------------------------
  //
  // Word loops (ld8 with post-increment) where both operands start 8-byte
  // aligned -- element 0 of a byte[] or char[] (base offset 16) -- since an
  // unaligned ld8 traps. Scratch: t1, p6/p7.

  // The inputs are only read; the routines work on copies in their temps.
  // StrEquals (LL): cnt bytes at str1 and str2. result = 1 if equal, else 0.
  void string_equals(Register str1, Register str2, Register cnt, Register result,
                     Register tmp1, Register tmp2, Register tmp3, Register tmp4, Register tmp5);
  // AryEq: two byte[] (elem_size 1) or char[] (2) oops, either may be null.
  void arrays_equals(Register ary1, Register ary2, Register result,
                     Register tmp1, Register tmp2, Register tmp3, Register tmp4, Register tmp5,
                     int elem_size);
  // StrComp: cnt1/cnt2 are byte lengths; ae is a StrIntrinsicNode::ArgEnc.
  void string_compare(Register str1, Register cnt1, Register str2, Register cnt2, Register result,
                      Register tmp1, Register tmp2, Register tmp3, Register tmp4, Register tmp5,
                      int ae);
  // StrIndexOfChar: cnt chars at str (Latin-1 bytes if isL, else UTF-16
  // chars), any alignment; ch fits the encoding. result = the index of the
  // first ch, or -1.
  void string_indexof_char(Register str, Register cnt, Register ch, Register result,
                           Register tmp1, Register tmp2, Register tmp3, Register tmp4, bool isL);
  // CountPositives: len bytes at ary (any alignment). result = len if none
  // is negative, else at most the index of the first negative byte.
  void count_positives(Register ary, Register len, Register result,
                       Register tmp1, Register tmp2, Register tmp3, Register tmp4);

 private:
  void equal_bytes(Register a1, Register a2, Register cnt, Register result,
                   Register tmp1, Register tmp2);
 public:

#endif // CPU_IA64_C2_MACROASSEMBLER_IA64_HPP
