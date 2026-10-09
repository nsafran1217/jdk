/*
 * Copyright (c) 2000, 2024, Oracle and/or its affiliates. All rights reserved.
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

#ifndef CPU_IA64_C1_LIRASSEMBLER_IA64_HPP
#define CPU_IA64_C1_LIRASSEMBLER_IA64_HPP

// ArrayCopyStub needs access to bailout
friend class ArrayCopyStub;

private:

#include "c1_LIRAssembler_arith_ia64.hpp"
#include "c1_LIRAssembler_arraycopy_ia64.hpp"

  int array_element_size(BasicType type) const;

  static Register as_reg(LIR_Opr op) {
    return op->is_double_cpu() ? op->as_register_lo() : op->as_register();
  }

  // The condition register pair of the flags-style LIR (JIT-SCOPE.md, C1-0):
  // comp_op sets pcond to the compare's condition and pncond to its
  // complement; a float compare also sets punord when either operand is NaN.
  // emit_opBranch and cmove read them. No MacroAssembler helper touches
  // p10-p13, so the moves LinearScan inserts between a compare and its
  // branch preserve them.
  static constexpr PredicateRegister pcond  = p10;
  static constexpr PredicateRegister pncond = p11;
  static constexpr PredicateRegister punord = p12;
  static constexpr PredicateRegister pord   = p13;

  // IA-64 has no displacement addressing: the effective address of addr in a
  // register -- addr's base itself when there is nothing to add, else tmp
  // (and nothing else). Emitting this before
  // the access lets an implicit null check record the access's own pc.
  Register addr_reg(LIR_Address* addr, Register tmp);
  Register stack_slot_addr_reg(int index, int adjust = 0);

  address float_constant(float f);
  address double_constant(double d);
  address int_constant(jlong n);

  // Record the type of the receiver in ReceiverTypeData
  void type_profile_helper(Register mdo,
                           ciMethodData *md, ciProfileData *data,
                           Register recv, Label* update_done);

  void deoptimize_trap(CodeEmitInfo *info);

  // Sizes in bytes; every IA-64 instruction is a 16-byte bundle, a movl one
  // bundle too.
  enum {
    // static call stub: movl Rmethod; movl t0; mov b6 = t0; br b6
    _call_stub_size = 8 * BytesPerBundle,
    // See emit_exception_handler for detail
    _exception_handler_size = DEBUG_ONLY(64) NOT_DEBUG(16) * BytesPerBundle,
    // See emit_deopt_handler for detail
    _deopt_handler_size = 8 * BytesPerBundle
  };

  // emit_typecheck_helper sub functions
  void typecheck_helper_slowcheck(ciKlass* k, Register obj, Register Rtmp1,
                                  Register k_RInfo, Register klass_RInfo,
                                  Label* failure_target, Label* success_target);
  void typecheck_loaded(LIR_OpTypeCheck* op, ciKlass* k, Register k_RInfo);
  void slow_subtype_check(Register sub_klass, Register super_klass, Label* failure_target);

  // emit_opTypeCheck sub functions
  void typecheck_lir_store(LIR_OpTypeCheck* op);

  // The value of a constant operand in a register: r0 for zero, else t1.
  Register const_reg(LIR_Opr opr);

  void logic_op_reg(Register dst, Register left, Register right, LIR_Code code);

public:

  void emit_cmove(LIR_Op4* op);

  void store_parameter(Register r, int offset_from_rsp_in_words);
  void store_parameter(jint c, int offset_from_rsp_in_words);

#endif // CPU_IA64_C1_LIRASSEMBLER_IA64_HPP
