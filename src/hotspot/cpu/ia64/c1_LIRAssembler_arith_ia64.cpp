/*
 * Copyright (c) 2000, 2025, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2020, 2022, Huawei Technologies Co., Ltd. All rights reserved.
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
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "utilities/powerOfTwo.hpp"

#ifndef PRODUCT
#define COMMENT(x)   do { __ block_comment(x); } while (0)
#else
#define COMMENT(x)
#endif

#define __ _masm->

// Integer arithmetic follows the int model (c1_LIRAssembler_ia64.cpp): an
// int result that can carry out of the low 32 bits goes through int_result
// (re-extended unless C1LazyIntExtension).
// IA-64 has no integer divide: division by a power-of-two constant is shifts,
// anything else MacroAssembler::java_div_rem's inline FP-unit sequence. The
// divisor-is-zero check comes before, from the LIRGenerator.

// dst = a / c or a % c for c = 2^k > 0, rounding toward zero. For a
// sign-extended int the 64-bit arithmetic gives the int result. Clobbers
// t1-t3.
static void div_rem_by_power_of_2(MacroAssembler* _masm, Register dst, Register a,
                                  int64_t c, bool is_rem) {
  assert(c > 0 && is_power_of_2(c), "must be");
  int k = exact_log2(c);
  if (k == 0) {
    if (is_rem) {
      __ mov(dst, zr);
    } else {
      __ mov(dst, a);
    }
    return;
  }
  // bias = c - 1 for negative a, else 0
  __ shr_imm(t1, a, 63);
  __ shru_imm(t1, t1, 64 - k);
  __ add(t2, a, t1);
  if (is_rem) {
    __ mov_immediate(t3, c - 1);
    __ and_(t2, t2, t3);
    __ sub(dst, t2, t1);
  } else {
    __ shr_imm(dst, t2, k);
  }
}

void LIR_Assembler::arithmetic_idiv(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr illegal,
                                    LIR_Opr result, CodeEmitInfo* info) {
  Register lreg = left->as_register();
  Register dreg = result->as_register();
  bool is_irem = (code == lir_irem);
  // Both paths divide 64-bit values.
  int_operand(lreg);
  if (right->is_constant()) {
    int64_t c = right->as_constant_ptr()->as_jint();
    div_rem_by_power_of_2(_masm, dreg, lreg, c, is_irem);
  } else {
    Register rreg = int_operand(right->as_register());
    __ java_div_rem(dreg, lreg, rreg, is_irem);
    // MIN_VALUE / -1 is 2^31 here; as an int it is MIN_VALUE again.
    int_result(dreg);
  }
}

void LIR_Assembler::arith_op_single_cpu_right_constant(LIR_Code code, LIR_Opr left, LIR_Opr right,
                                                       Register lreg, Register dreg) {
  int64_t c = right->as_constant_ptr()->as_jint();
  switch (code) {
    case lir_add: __ add_imm(dreg, lreg, c, t1); int_result(dreg); break;
    case lir_sub: __ add_imm(dreg, lreg, -c, t1); int_result(dreg); break;
    case lir_mul:
      __ mov_immediate(t1, c);
      __ mul(dreg, lreg, t1);
      int_result(dreg);
      break;
    default: ShouldNotReachHere();
  }
}

void LIR_Assembler::arith_op_single_cpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest) {
  Register lreg = left->as_register();
  Register dreg = as_reg(dest);

  if (right->is_single_cpu()) {
    Register rreg = right->as_register();
    switch (code) {
      case lir_add: __ add(dreg, lreg, rreg); break;
      case lir_sub: __ sub(dreg, lreg, rreg); break;
      case lir_mul: __ mul(dreg, lreg, rreg); break;
      default:      ShouldNotReachHere();
    }
    if (dest->type() == T_INT) {
      int_result(dreg);
    }
  } else if (right->is_constant()) {
    arith_op_single_cpu_right_constant(code, left, right, lreg, dreg);
  } else if (right->is_single_stack()) {
    Address a = frame_map()->address_for_slot(right->single_stack_ix());
    __ ld4s(t2, a);
    switch (code) {
      case lir_add: __ add(dreg, lreg, t2); break;
      case lir_sub: __ sub(dreg, lreg, t2); break;
      case lir_mul: __ mul(dreg, lreg, t2); break;
      default:      ShouldNotReachHere();
    }
    int_result(dreg);
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::arith_op_double_cpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest) {
  Register lreg_lo = left->as_register_lo();
  Register dest_lo = dest->as_register_lo();

  if (right->is_double_cpu()) {
    Register rreg_lo = right->as_register_lo();
    switch (code) {
      case lir_add: __ add(dest_lo, lreg_lo, rreg_lo); break;
      case lir_sub: __ sub(dest_lo, lreg_lo, rreg_lo); break;
      case lir_mul: __ mul(dest_lo, lreg_lo, rreg_lo); break;
      case lir_div: __ java_div_rem(dest_lo, lreg_lo, rreg_lo, false); break;
      case lir_rem: __ java_div_rem(dest_lo, lreg_lo, rreg_lo, true); break;
      default:      ShouldNotReachHere();
    }
  } else if (right->is_constant()) {
    int64_t c = right->as_constant_ptr()->as_jlong();
    switch (code) {
      case lir_add: __ add_imm(dest_lo, lreg_lo, c, t1); break;
      case lir_sub: __ add_imm(dest_lo, lreg_lo, -c, t1); break;
      case lir_mul:
        __ mov_immediate(t1, c);
        __ mul(dest_lo, lreg_lo, t1);
        break;
      case lir_div: div_rem_by_power_of_2(_masm, dest_lo, lreg_lo, c, false); break;
      case lir_rem: div_rem_by_power_of_2(_masm, dest_lo, lreg_lo, c, true); break;
      default:      ShouldNotReachHere();
    }
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::arith_op_single_fpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest) {
  assert(right->is_single_fpu(), "right hand side of float arithmetics needs to be float register");
  FloatRegister a = left->as_float_reg(), b = right->as_float_reg(), d = dest->as_float_reg();
  switch (code) {
    case lir_add: __ fadd_s(d, a, b); break;
    case lir_sub: __ fsub_s(d, a, b); break;
    case lir_mul: __ fmpy_s(d, a, b); break;
    case lir_div: __ fdiv_s(d, a, b); break;
    default:      ShouldNotReachHere();
  }
}

void LIR_Assembler::arith_op_double_fpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest) {
  assert(right->is_double_fpu(), "right hand side of double arithmetics needs to be double register");
  FloatRegister a = left->as_double_reg(), b = right->as_double_reg(), d = dest->as_double_reg();
  switch (code) {
    case lir_add: __ fadd_d(d, a, b); break;
    case lir_sub: __ fsub_d(d, a, b); break;
    case lir_mul: __ fmpy_d(d, a, b); break;
    case lir_div: __ fdiv_d(d, a, b); break;
    default:      ShouldNotReachHere();
  }
}

void LIR_Assembler::arith_op(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest,
                             CodeEmitInfo* info) {
  assert(info == nullptr, "should never be used, idiv/irem and ldiv/lrem not handled by this method");

  if (left->is_single_cpu()) {
    arith_op_single_cpu(code, left, right, dest);
  } else if (left->is_double_cpu()) {
    arith_op_double_cpu(code, left, right, dest);
  } else if (left->is_single_fpu()) {
    arith_op_single_fpu(code, left, right, dest);
  } else if (left->is_double_fpu()) {
    arith_op_double_fpu(code, left, right, dest);
  } else {
    ShouldNotReachHere();
  }
}

#undef __
