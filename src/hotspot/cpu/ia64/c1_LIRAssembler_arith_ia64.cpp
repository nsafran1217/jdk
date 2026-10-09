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

#ifndef PRODUCT
#define COMMENT(x)   do { __ block_comment(x); } while (0)
#else
#define COMMENT(x)
#endif

#define __ _masm->

void LIR_Assembler::arithmetic_idiv(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr illegal,
                                    LIR_Opr result, CodeEmitInfo* info) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: while)
}

void LIR_Assembler::arith_op_single_cpu_right_constant(LIR_Code code, LIR_Opr left, LIR_Opr right,
                                                       Register lreg, Register dreg) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::arith_op_single_cpu_right_constant)
}

void LIR_Assembler::arith_op_single_cpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::arith_op_single_cpu)
}

void LIR_Assembler::arith_op_double_cpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::arith_op_double_cpu)
}

void LIR_Assembler::arith_op_single_fpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::arith_op_single_fpu)
}

void LIR_Assembler::arith_op_double_fpu(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::arith_op_double_fpu)
}

void LIR_Assembler::arith_op(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest,
                             CodeEmitInfo* info) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::arith_op)
}

#undef __
