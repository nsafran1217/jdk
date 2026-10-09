/*
 * Copyright (c) 1999, 2025, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, Red Hat Inc. All rights reserved.
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

#include "c1/c1_LIR.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "c1/c1_Runtime1.hpp"
#include "classfile/systemDictionary.hpp"
#include "gc/shared/barrierSetAssembler.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "interpreter/interpreter.hpp"
#include "oops/arrayOop.hpp"
#include "oops/markWord.hpp"
#include "runtime/basicLock.hpp"
#include "runtime/os.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"

void C1_MacroAssembler::float_cmp(bool is_float, int unordered_result,
                                  FloatRegister freg0, FloatRegister freg1,
                                  Register result) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::float_cmp)
}

int C1_MacroAssembler::lock_object(Register hdr, Register obj, Register disp_hdr, Register temp, Label& slow_case) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::lock_object)
}

void C1_MacroAssembler::unlock_object(Register hdr, Register obj, Register disp_hdr, Register temp, Label& slow_case) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::unlock_object)
}

// Defines obj, preserves var_size_in_bytes
void C1_MacroAssembler::try_allocate(Register obj, Register var_size_in_bytes, int con_size_in_bytes, Register tmp1, Register tmp2, Label& slow_case) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::try_allocate)
}

void C1_MacroAssembler::initialize_header(Register obj, Register klass, Register len, Register tmp1, Register tmp2) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::initialize_header)
}

// preserves obj, destroys len_in_bytes
void C1_MacroAssembler::initialize_body(Register obj, Register len_in_bytes, int hdr_size_in_bytes, Register tmp) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::initialize_body)
}

void C1_MacroAssembler::allocate_object(Register obj, Register tmp1, Register tmp2, int header_size, int object_size, Register klass, Label& slow_case) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::allocate_object)
}

void C1_MacroAssembler::initialize_object(Register obj, Register klass, Register var_size_in_bytes, int con_size_in_bytes, Register tmp1, Register tmp2, bool is_tlab_allocated) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::initialize_object)
}

void C1_MacroAssembler::allocate_array(Register obj, Register len, Register tmp1, Register tmp2, int base_offset_in_bytes, int f, Register klass, Label& slow_case, bool zero_array) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::allocate_array)
}

void C1_MacroAssembler::build_frame(int framesize, int bang_size_in_bytes) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::build_frame)
}

void C1_MacroAssembler::remove_frame(int framesize) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::remove_frame)
}


void C1_MacroAssembler::verified_entry(bool breakAtEntry) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::verified_entry)
}

void C1_MacroAssembler::load_parameter(int offset_in_words, Register reg) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::load_parameter)
}

#ifndef PRODUCT

void C1_MacroAssembler::verify_stack_oop(int stack_offset) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::verify_stack_oop)
}

void C1_MacroAssembler::verify_not_null_oop(Register r) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::verify_not_null_oop)
}

void C1_MacroAssembler::invalidate_registers(bool inv_x10, bool inv_x9, bool inv_x12, bool inv_x13, bool inv_x14, bool inv_x15) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: C1_MacroAssembler::invalidate_registers)
}
#endif // ifndef PRODUCT

// riscv's c1_cmp_branch/c1_float_cmp_branch (compare fused into the branch,
// because riscv has no condition flags) are not used here: on IA-64 lir_cmp
// sets a predicate pair that the following branch or cmove reads, so C1 uses
// the shared flags-style LIR (as on x86 and aarch64). See JIT-SCOPE.md, C1.
