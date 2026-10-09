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

#ifndef CPU_IA64_C1_DEFS_IA64_HPP
#define CPU_IA64_C1_DEFS_IA64_HPP

// native word offsets from memory address (little endian)
enum {
  pd_lo_word_offset_in_bytes = 0,
  pd_hi_word_offset_in_bytes = BytesPerWord
};

// C1 register numbers (FrameMap::initialize, c1_FrameMap_ia64.cpp).
//
// CPU: all 40 modelled general registers are mapped (r0-r31 plus out0-out7),
// the first 15 being the allocatable set: r8, r16, r17, r20-r31
// (FRAME-DESIGN.md 2.2). Not allocatable: r0, r1 (gp), r2/r3 and r9-r11 (the
// MacroAssembler temporaries), r4 fp, r5 Rthread, r6/r7 (interpreter Rbcp/Resp,
// preserved by the C ABI), r12 sp, r13 tp, r14/r15/r18/r19 (interpreter
// state), and out0-out7 (the C argument window). C1 treats every allocatable
// register as caller-saved.
//
// FPU: C1 FPU register numbers are FloatRegister encodings, f0-f31. Only f8-f31
// are allocatable (LinearScanWalker::pd_init_regs_for_alloc): f0/f1 are
// hardwired, f2-f5 belong to the divide sequences, f6/f7 are MacroAssembler
// temporaries. call_stub saves the C caller's f16-f31, so compiled Java code may
// use them freely; C1 treats them as caller-saved too.
enum {
  pd_nof_cpu_regs_frame_map = Register::number_of_registers,       // 40
  pd_nof_fpu_regs_frame_map = FloatRegister::number_of_registers,  // 32

  // caller saved
  pd_nof_caller_save_cpu_regs_frame_map = 15, // number of registers killed by calls
  pd_nof_caller_save_fpu_regs_frame_map = 24, // f8-f31

  // No callee-saved range: an empty interval [15, 14].
  pd_first_callee_saved_reg = pd_nof_caller_save_cpu_regs_frame_map,
  pd_last_callee_saved_reg  = pd_nof_caller_save_cpu_regs_frame_map - 1,

  pd_last_allocatable_cpu_reg = pd_nof_caller_save_cpu_regs_frame_map - 1,

  pd_nof_cpu_regs_reg_alloc = pd_nof_caller_save_cpu_regs_frame_map,  // visible to the register allocator
  pd_nof_fpu_regs_reg_alloc = 24,                                     // f8-f31

  pd_nof_cpu_regs_linearscan = pd_nof_cpu_regs_frame_map,  // visible to linear scan
  pd_nof_fpu_regs_linearscan = pd_nof_fpu_regs_frame_map,
  pd_nof_xmm_regs_linearscan = 0,                          // no vector registers

  pd_first_cpu_reg  = 0,
  pd_last_cpu_reg   = pd_nof_cpu_regs_reg_alloc - 1,
  pd_first_byte_reg = 0,
  pd_last_byte_reg  = pd_nof_cpu_regs_reg_alloc - 1,

  pd_first_fpu_reg  = pd_nof_cpu_regs_frame_map,
  pd_last_fpu_reg   = pd_first_fpu_reg + 31,
  pd_first_allocatable_fpu_reg = pd_first_fpu_reg + 8      // f8
};

// A register saver cannot know whether an FPR holds a float or a double, and
// stfs/stfd need the value to have the store's type, so every saved FPR is
// stored -- and described to deoptimization -- as a double (RegisterSaver,
// FRAME-DESIGN.md 11.1). C1's own spills still use stfs for floats.
enum {
  pd_float_saved_as_double = true
};

enum {
  pd_two_operand_lir_form = false
};

// the number of stack required by ArrayCopyStub
enum {
  pd_arraycopystub_reserved_argument_area_size = 2
};

#endif // CPU_IA64_C1_DEFS_IA64_HPP
