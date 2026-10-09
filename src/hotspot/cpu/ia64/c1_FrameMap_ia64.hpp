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

#ifndef CPU_IA64_C1_FRAMEMAP_IA64_HPP
#define CPU_IA64_C1_FRAMEMAP_IA64_HPP

//  On IA-64 the frame looks as follows (FRAME-DESIGN.md 4.5; same shape as
//  riscv, built by C1_MacroAssembler::build_frame):
//
//  +-----------------------------+---------+----------------------------------------+----------------+-----------
//  | size_arguments-nof_reg_args | 2 words | size_locals-size_arguments+numreg_args | _size_monitors | spilling .
//  +-----------------------------+---------+----------------------------------------+----------------+-----------
//
//  Operands are indexed by register *encoding* (gr_opr(20) is r20) rather than
//  being one named static per register, as riscv's are: IA-64 models 40
//  general registers, most of which C1 never names.

 public:
  static const int pd_c_runtime_reserved_arg_size;

  enum {
    first_available_sp_in_frame = 0,
    frame_pad_in_bytes = 16,
    nof_reg_args = 8
  };

 private:
  static LIR_Opr _gr_opr[Register::number_of_registers];
  static LIR_Opr _gr_oop_opr[Register::number_of_registers];
  static LIR_Opr _gr_metadata_opr[Register::number_of_registers];

 public:
  static LIR_Opr gr_opr(int enc)          { return _gr_opr[enc]; }
  static LIR_Opr gr_oop_opr(int enc)      { return _gr_oop_opr[enc]; }
  static LIR_Opr gr_metadata_opr(int enc) { return _gr_metadata_opr[enc]; }
  static LIR_Opr gr_long_opr(int enc)     { return as_long_opr(as_Register(enc)); }

  // Fixed registers of the C1 runtime-stub conventions, shared by
  // c1_LIRGenerator_ia64.cpp, c1_CodeStubs_ia64.cpp and c1_Runtime1_ia64.cpp
  // (riscv uses its a0-a5 the same way). All are in the allocatable set, which
  // LinearScan handles as fixed-register constraints.
  enum {
    stub_klass_reg   = 22,   // klass for new_instance/new_array/multianewarray
    stub_length_reg  = 21,   // array length; rank for multianewarray
    stub_tmp1_reg    = 24,   // allocation temporaries; varargs for multianewarray
    stub_tmp2_reg    = 25,
    stub_tmp3_reg    = 26,
    exception_pc_reg = 28    // Rexception_pc (assembler_ia64.hpp)
  };

  static LIR_Opr receiver_opr;   // j_rarg0
  static LIR_Opr fp_opr;
  static LIR_Opr sp_opr;
  static LIR_Opr t0_opr;         // MacroAssembler temporaries: never allocated,
  static LIR_Opr t1_opr;         // only named by platform code
  static LIR_Opr t0_long_opr;
  static LIR_Opr t1_long_opr;
  static LIR_Opr r8_opr;         // C and Java return value
  static LIR_Opr r8_oop_opr;
  static LIR_Opr r8_long_opr;
  static LIR_Opr f8_float_opr;   // FP return value
  static LIR_Opr f8_double_opr;

  static LIR_Opr as_long_opr(Register r) {
    return LIR_OprFact::double_cpu(cpu_reg2rnr(r), cpu_reg2rnr(r));
  }
  static LIR_Opr as_pointer_opr(Register r) {
    return LIR_OprFact::double_cpu(cpu_reg2rnr(r), cpu_reg2rnr(r));
  }

  // VMReg name for spilled physical FPU stack slot n
  static VMReg fpu_regname(int n);

  static bool is_caller_save_register(LIR_Opr opr) { return true; }
  static bool is_caller_save_register(Register r)  { return true; }

  static int nof_caller_save_cpu_regs() { return pd_nof_caller_save_cpu_regs_frame_map; }
  static int last_cpu_reg()             { return pd_last_cpu_reg; }

#endif // CPU_IA64_C1_FRAMEMAP_IA64_HPP
