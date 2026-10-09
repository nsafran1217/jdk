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
#include "ci/ciArrayKlass.hpp"
#include "oops/objArrayKlass.hpp"
#include "runtime/stubRoutines.hpp"

#define __ _masm->


void LIR_Assembler::generic_arraycopy(Register src, Register src_pos, Register length,
                                      Register dst, Register dst_pos, CodeStub *stub) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::generic_arraycopy)
}

void LIR_Assembler::arraycopy_simple_check(Register src, Register src_pos, Register length,
                                           Register dst, Register dst_pos, Register tmp,
                                           CodeStub *stub, int flags) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::arraycopy_simple_check)
}

void LIR_Assembler::arraycopy_checkcast(Register src, Register src_pos, Register length,
                                        Register dst, Register dst_pos, Register tmp,
                                        CodeStub *stub, BasicType basic_type,
                                        address copyfunc_addr, int flags) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::arraycopy_checkcast)
}

void LIR_Assembler::arraycopy_type_check(Register src, Register src_pos, Register length,
                                         Register dst, Register dst_pos, Register tmp,
                                         CodeStub *stub, BasicType basic_type, int flags) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::arraycopy_type_check)
}

void LIR_Assembler::arraycopy_assert(Register src, Register dst, Register tmp, ciArrayKlass *default_type, int flags) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::arraycopy_assert)
}

void LIR_Assembler::emit_arraycopy(LIR_OpArrayCopy* op) {
  // Every arraycopy takes the stub's path for now: a Java call to
  // System.arraycopy, whose operands the LIRGenerator already put in the
  // Java argument registers (ArrayCopyStub). The inline checks and the
  // arraycopy stubs come later (JIT-SCOPE.md C1-3).
  ArrayCopyStub* stub = op->stub();
  __ j(*stub->entry());
  __ bind(*stub->continuation());
}


void LIR_Assembler::arraycopy_prepare_params(Register src, Register src_pos, Register length,
                                             Register dst, Register dst_pos, BasicType basic_type) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::arraycopy_prepare_params)
}

void LIR_Assembler::arraycopy_checkcast_prepare_params(Register src, Register src_pos, Register length,
                                                       Register dst, Register dst_pos, BasicType basic_type) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::arraycopy_checkcast_prepare_params)
}

void LIR_Assembler::arraycopy_store_args(Register src, Register src_pos, Register length,
                                         Register dst, Register dst_pos) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::arraycopy_store_args)
}

void LIR_Assembler::arraycopy_load_args(Register src, Register src_pos, Register length,
                                        Register dst, Register dst_pos) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::arraycopy_load_args)
}

#undef __
