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

#include "c1/c1_FrameMap.hpp"
#include "c1/c1_LIR.hpp"
#include "runtime/sharedRuntime.hpp"
#include "vmreg_ia64.inline.hpp"

LIR_Opr FrameMap::map_to_opr(BasicType type, VMRegPair* reg, bool) {
  LIR_Opr opr = LIR_OprFact::illegalOpr;
  VMReg r_1 = reg->first();
  VMReg r_2 = reg->second();
  if (r_1->is_stack()) {
    // Convert stack slot to an SP offset
    // The calling convention does not count the SharedRuntime::out_preserve_stack_slots() value
    // so we must add it in here.
    int st_off = (r_1->reg2stack() + SharedRuntime::out_preserve_stack_slots()) * VMRegImpl::stack_slot_size;
    opr = LIR_OprFact::address(new LIR_Address(sp_opr, st_off, type));
  } else if (r_1->is_Register()) {
    Register reg1 = r_1->as_Register();
    if (r_2->is_Register() && (type == T_LONG || type == T_DOUBLE)) {
      Register reg2 = r_2->as_Register();
      assert(reg2 == reg1, "must be same register");
      opr = as_long_opr(reg1);
    } else if (is_reference_type(type)) {
      opr = as_oop_opr(reg1);
    } else if (type == T_METADATA) {
      opr = as_metadata_opr(reg1);
    } else if (type == T_ADDRESS) {
      opr = as_address_opr(reg1);
    } else {
      opr = as_opr(reg1);
    }
  } else if (r_1->is_FloatRegister()) {
    assert(type == T_DOUBLE || type == T_FLOAT, "wrong type");
    int num = r_1->as_FloatRegister()->encoding();
    if (type == T_FLOAT) {
      opr = LIR_OprFact::single_fpu(num);
    } else {
      opr = LIR_OprFact::double_fpu(num);
    }
  } else {
    ShouldNotReachHere();
  }
  return opr;
}

LIR_Opr FrameMap::_gr_opr[Register::number_of_registers];
LIR_Opr FrameMap::_gr_oop_opr[Register::number_of_registers];
LIR_Opr FrameMap::_gr_metadata_opr[Register::number_of_registers];

LIR_Opr FrameMap::receiver_opr;
LIR_Opr FrameMap::fp_opr;
LIR_Opr FrameMap::sp_opr;
LIR_Opr FrameMap::t0_opr;
LIR_Opr FrameMap::t1_opr;
LIR_Opr FrameMap::t0_long_opr;
LIR_Opr FrameMap::t1_long_opr;
LIR_Opr FrameMap::r8_opr;
LIR_Opr FrameMap::r8_oop_opr;
LIR_Opr FrameMap::r8_long_opr;
LIR_Opr FrameMap::f8_float_opr;
LIR_Opr FrameMap::f8_double_opr;

LIR_Opr FrameMap::_caller_save_cpu_regs[] = {};
LIR_Opr FrameMap::_caller_save_fpu_regs[] = {};

// C1 register numbers. The allocatable set comes first, in the order the
// allocator prefers them; it must stay exactly pd_nof_caller_save_cpu_regs_frame_map
// long (c1_Defs_ia64.hpp).
static const int allocatable_gr[] = { 8, 16, 17, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
// Everything else, mapped so that platform code can name it but never allocated.
static const int fixed_gr[] = { 0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 18, 19,
                                32, 33, 34, 35, 36, 37, 38, 39 };

void FrameMap::initialize() {
  assert(!_init_done, "once");
  STATIC_ASSERT(sizeof(allocatable_gr) / sizeof(int) == pd_nof_caller_save_cpu_regs_frame_map);
  STATIC_ASSERT(sizeof(allocatable_gr) / sizeof(int) + sizeof(fixed_gr) / sizeof(int) == pd_nof_cpu_regs_frame_map);

  int i = 0;
  for (int enc : allocatable_gr) {
    map_register(i, as_Register(enc));
    _gr_opr[enc] = LIR_OprFact::single_cpu(i);
    _caller_save_cpu_regs[i] = _gr_opr[enc];
    i++;
  }
  for (int enc : fixed_gr) {
    map_register(i, as_Register(enc));
    _gr_opr[enc] = LIR_OprFact::single_cpu(i);
    i++;
  }
  _init_done = true;
  // LinearScan processes out0-out7 by their C1 numbers (c1_LinearScan_ia64.hpp).
  assert(cpu_reg2rnr(out0) == pd_nof_cpu_regs_frame_map - 8 &&
         cpu_reg2rnr(out7) == pd_nof_cpu_regs_frame_map - 1, "out registers must be numbered last");

  for (int enc = 0; enc < Register::number_of_registers; enc++) {
    _gr_oop_opr[enc]      = as_oop_opr(as_Register(enc));
    _gr_metadata_opr[enc] = as_metadata_opr(as_Register(enc));
  }

  t0_opr      = _gr_opr[t0->encoding()];
  t1_opr      = _gr_opr[t1->encoding()];
  t0_long_opr = as_long_opr(t0);
  t1_long_opr = as_long_opr(t1);
  r8_opr      = _gr_opr[8];
  r8_oop_opr  = _gr_oop_opr[8];
  r8_long_opr = as_long_opr(r8);
  f8_float_opr  = LIR_OprFact::single_fpu(8);
  f8_double_opr = LIR_OprFact::double_fpu(8);

  sp_opr = as_pointer_opr(sp);
  fp_opr = as_pointer_opr(fp);

  VMRegPair regs;
  BasicType sig_bt = T_OBJECT;
  SharedRuntime::java_calling_convention(&sig_bt, &regs, 1);
  receiver_opr = as_oop_opr(regs.first()->as_Register());

  // f8-f31: the allocatable FP registers (c1_Defs_ia64.hpp).
  for (int f = 0; f < nof_caller_save_fpu_regs; f++) {
    _caller_save_fpu_regs[f] = LIR_OprFact::single_fpu(8 + f);
  }
}


Address FrameMap::make_new_address(ByteSize sp_offset) const {
  return Address(sp, in_bytes(sp_offset));
}


// ----------------mapping-----------------------
// all mapping is based on fp addressing, except for simple leaf methods where we access
// the locals sp based (and no frame is built)

VMReg FrameMap::fpu_regname(int n) {
  // Return the OptoReg name for the fpu stack slot "n"
  return as_FloatRegister(n)->as_VMReg();
}

LIR_Opr FrameMap::stack_pointer() {
  return FrameMap::sp_opr;
}

// JSR 292
LIR_Opr FrameMap::method_handle_invoke_SP_save_opr() {
  return LIR_OprFact::illegalOpr;  // Not needed on IA-64
}

bool FrameMap::validate_frame() {
  return true;
}

const int FrameMap::pd_c_runtime_reserved_arg_size = 0;
