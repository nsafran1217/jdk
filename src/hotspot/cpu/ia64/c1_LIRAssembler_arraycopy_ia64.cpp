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
#include "code/codeCache.hpp"
#include "oops/objArrayKlass.hpp"
#include "runtime/stubRoutines.hpp"

#define __ _masm->


// Arraycopy (after riscv). No arraycopy stubs are generated on IA-64, so
// StubRoutines' copy entries are the C++ defaults (StubRoutines::jbyte_copy
// and friends; oop_copy applies the GC's barriers): leaf C functions, called
// through their descriptors. The generic and per-element-checkcast cases have
// no stub and take the slow path, ArrayCopyStub's Java call to
// System.arraycopy. The op is a call for LinearScan: every caller-saved
// register is free here.

void LIR_Assembler::generic_arraycopy(Register src, Register src_pos, Register length,
                                      Register dst, Register dst_pos, CodeStub *stub) {
  // No generic arraycopy stub: the slow path.
  __ j(*stub->entry());
  __ bind(*stub->continuation());
}

void LIR_Assembler::arraycopy_simple_check(Register src, Register src_pos, Register length,
                                           Register dst, Register dst_pos, Register tmp,
                                           CodeStub *stub, int flags) {
  // test for null
  if (flags & LIR_OpArrayCopy::src_null_check) {
    __ beqz(src, *stub->entry());
  }
  if (flags & LIR_OpArrayCopy::dst_null_check) {
    __ beqz(dst, *stub->entry());
  }

  // If the compiler was not able to prove that exact type of the source or the destination
  // of the arraycopy is an array type, check at runtime if the source or the destination is
  // an instance type (a non-negative layout helper).
  if (flags & LIR_OpArrayCopy::type_check) {
    assert(Klass::_lh_neutral_value == 0, "or replace the compares below");
    if (!(flags & LIR_OpArrayCopy::dst_objarray)) {
      __ load_klass(tmp, dst);
      __ ld4s(t2, Address(tmp, in_bytes(Klass::layout_helper_offset())));
      __ bge(t2, zr, *stub->entry());
    }
    if (!(flags & LIR_OpArrayCopy::src_objarray)) {
      __ load_klass(tmp, src);
      __ ld4s(t2, Address(tmp, in_bytes(Klass::layout_helper_offset())));
      __ bge(t2, zr, *stub->entry());
    }
  }

  // check if negative (ints are sign-extended)
  if (flags & LIR_OpArrayCopy::src_pos_positive_check) {
    __ blt(src_pos, zr, *stub->entry());
  }
  if (flags & LIR_OpArrayCopy::dst_pos_positive_check) {
    __ blt(dst_pos, zr, *stub->entry());
  }
  if (flags & LIR_OpArrayCopy::length_positive_check) {
    __ blt(length, zr, *stub->entry());
  }

  if (flags & LIR_OpArrayCopy::src_range_check) {
    __ add(tmp, src_pos, length);
    __ ld4(t2, Address(src, arrayOopDesc::length_offset_in_bytes()));
    __ bgtu(tmp, t2, *stub->entry());
  }
  if (flags & LIR_OpArrayCopy::dst_range_check) {
    __ add(tmp, dst_pos, length);
    __ ld4(t2, Address(dst, arrayOopDesc::length_offset_in_bytes()));
    __ bgtu(tmp, t2, *stub->entry());
  }
}

void LIR_Assembler::arraycopy_checkcast(Register src, Register src_pos, Register length,
                                        Register dst, Register dst_pos, Register tmp,
                                        CodeStub *stub, BasicType basic_type,
                                        address copyfunc_addr, int flags) {
  // No checkcast arraycopy stub on IA-64 (arraycopy_type_check).
  ShouldNotReachHere();
}

void LIR_Assembler::arraycopy_type_check(Register src, Register src_pos, Register length,
                                         Register dst, Register dst_pos, Register tmp,
                                         CodeStub *stub, BasicType basic_type, int flags) {
  assert(!UseCompactObjectHeaders, "IA-64: compact object headers not yet supported");
  // We don't know the array types are compatible
  if (basic_type != T_OBJECT) {
    // Simple test for basic type arrays: the same klass
    if (UseCompressedClassPointers) {
      __ ld4(tmp, Address(src, oopDesc::klass_offset_in_bytes()));
      __ ld4(t2, Address(dst, oopDesc::klass_offset_in_bytes()));
    } else {
      __ ld8(tmp, Address(src, oopDesc::klass_offset_in_bytes()));
      __ ld8(t2, Address(dst, oopDesc::klass_offset_in_bytes()));
    }
    __ bne(tmp, t2, *stub->entry());
  } else {
    // For object arrays, if src is a sub class of dst then we can
    // safely do the copy; otherwise (no checkcast stub) the slow path.
    // r14/r15 are never allocated.
    Label cont;
    const Register src_klass = r14, dst_klass = r15;
    __ load_klass(src_klass, src);
    __ load_klass(dst_klass, dst);
    __ check_klass_subtype_fast_path(src_klass, dst_klass, tmp, &cont, stub->entry(), nullptr);
    slow_subtype_check(src_klass, dst_klass, stub->entry());
    __ bind(cont);
  }
}

void LIR_Assembler::arraycopy_assert(Register src, Register dst, Register tmp, ciArrayKlass *default_type, int flags) {
  assert(default_type != nullptr, "null default_type!");
  BasicType basic_type = default_type->element_type()->basic_type();
  if (basic_type == T_ARRAY) { basic_type = T_OBJECT; }
  if (basic_type != T_OBJECT || !(flags & LIR_OpArrayCopy::type_check)) {
    // Sanity check the known type with the incoming class.  For the
    // primitive case the types must match exactly with src.klass and
    // dst.klass each exactly matching the default type.  For the
    // object array case, if no type check is needed then either the
    // dst type is exactly the expected type and the src type is a
    // subtype which we can't check or src is the same array as dst
    // but not necessarily exactly of type default_type.
    Label known_ok, halt;
    __ mov_metadata(tmp, default_type->constant_encoding());
    __ load_klass(t2, dst);
    if (basic_type != T_OBJECT) {
      __ bne(t2, tmp, halt);
      __ load_klass(t2, src);
      __ beq(t2, tmp, known_ok);
    } else {
      __ beq(t2, tmp, known_ok);
      __ beq(src, dst, known_ok);
    }
    __ bind(halt);
    __ stop("incorrect type information in arraycopy");
    __ bind(known_ok);
  }
}

void LIR_Assembler::emit_arraycopy(LIR_OpArrayCopy* op) {
  ciArrayKlass *default_type = op->expected_type();
  Register src = op->src()->as_register();
  Register dst = op->dst()->as_register();
  Register src_pos = op->src_pos()->as_register();
  Register dst_pos = op->dst_pos()->as_register();
  Register length = op->length()->as_register();
  Register tmp = op->tmp()->as_register();

  // The checks and the address arithmetic below are 64-bit.
  int_operand(src_pos);
  int_operand(dst_pos);
  int_operand(length);

  CodeStub* stub = op->stub();
  int flags = op->flags();
  BasicType basic_type = default_type != nullptr ? default_type->element_type()->basic_type() : T_ILLEGAL;
  if (is_reference_type(basic_type)) { basic_type = T_OBJECT; }

  // if we don't know anything, just go through the generic arraycopy
  if (default_type == nullptr) {
    generic_arraycopy(src, src_pos, length, dst, dst_pos, stub);
    return;
  }

  assert(default_type != nullptr && default_type->is_array_klass() && default_type->is_loaded(),
         "must be true at this point");

  arraycopy_simple_check(src, src_pos, length, dst, dst_pos, tmp, stub, flags);

  if (flags & LIR_OpArrayCopy::type_check) {
    arraycopy_type_check(src, src_pos, length, dst, dst_pos, tmp, stub, basic_type, flags);
  }

#ifdef ASSERT
  arraycopy_assert(src, dst, tmp, default_type, flags);
#endif

  arraycopy_prepare_params(src, src_pos, length, dst, dst_pos, basic_type);

  bool disjoint = (flags & LIR_OpArrayCopy::overlapping) == 0;
  bool aligned = (flags & LIR_OpArrayCopy::unaligned) == 0;
  const char *name = nullptr;
  address entry = StubRoutines::select_arraycopy_function(basic_type, aligned, disjoint, name, false);

  // Every arraycopy entry is a function descriptor -- the generated stubs
  // start with one (StubGenerator::generate_copy) -- so a generated stub's
  // entry lies in the code cache yet must be called like C, not far_call'ed.
  // A zero-length copy skips the call altogether.
  __ beqz(c_rarg2, *stub->continuation());
  __ call_c(entry);

  __ bind(*stub->continuation());
}


void LIR_Assembler::arraycopy_prepare_params(Register src, Register src_pos, Register length,
                                             Register dst, Register dst_pos, BasicType basic_type) {
  int scale = array_element_size(basic_type);
  int base = arrayOopDesc::base_offset_in_bytes(basic_type);
  if (scale == 0) {
    __ add(c_rarg0, src_pos, src);
    __ add(c_rarg1, dst_pos, dst);
  } else {
    __ shladd(c_rarg0, src_pos, scale, src);
    __ shladd(c_rarg1, dst_pos, scale, dst);
  }
  __ add_imm(c_rarg0, c_rarg0, base);
  __ add_imm(c_rarg1, c_rarg1, base);
  __ mov(c_rarg2, length);
}

void LIR_Assembler::arraycopy_checkcast_prepare_params(Register src, Register src_pos, Register length,
                                                       Register dst, Register dst_pos, BasicType basic_type) {
  ShouldNotReachHere();
}

void LIR_Assembler::arraycopy_store_args(Register src, Register src_pos, Register length,
                                         Register dst, Register dst_pos) {
  ShouldNotReachHere();
}

void LIR_Assembler::arraycopy_load_args(Register src, Register src_pos, Register length,
                                        Register dst, Register dst_pos) {
  ShouldNotReachHere();
}

#undef __
