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
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/barrierSetAssembler.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "interpreter/interp_masm.hpp"
#include "runtime/javaThread.hpp"
#include "runtime/jniHandles.hpp"

#define __ masm->

void BarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                  Register dst, Address src, Register tmp1, Register tmp2) {
  // b0 is live. It must be saved around calls.

  bool in_heap = (decorators & IN_HEAP) != 0;
  bool in_native = (decorators & IN_NATIVE) != 0;
  switch (type) {
    case T_OBJECT:  // fall through
    case T_ARRAY: {
      assert(in_heap || in_native, "why else?");
      assert(!in_heap || !UseCompressedOops, "IA-64 milestone 1 runs without compressed oops");
      __ ld8(dst, src);
      break;
    }
    case T_BOOLEAN: __ ld1 (dst, src); break;
    case T_BYTE:    __ ld1s(dst, src); break;
    case T_CHAR:    __ ld2 (dst, src); break;
    case T_SHORT:   __ ld2s(dst, src); break;
    case T_INT:     __ ld4s(dst, src); break;
    case T_LONG:    __ ld8 (dst, src); break;
    case T_ADDRESS: __ ld8 (dst, src); break;
    case T_FLOAT:   __ ldfs(f8, src, tmp1 != noreg ? tmp1 : t0); break;
    case T_DOUBLE:  __ ldfd(f8, src, tmp1 != noreg ? tmp1 : t0); break;
    default: Unimplemented();
  }
}

void BarrierSetAssembler::store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                   Address dst, Register val, Register tmp1, Register tmp2, Register tmp3) {
  bool in_heap = (decorators & IN_HEAP) != 0;
  bool in_native = (decorators & IN_NATIVE) != 0;
  switch (type) {
    case T_OBJECT: // fall through
    case T_ARRAY: {
      val = val == noreg ? zr : val;
      assert(in_heap || in_native, "why else?");
      assert(!in_heap || !UseCompressedOops, "IA-64 milestone 1 runs without compressed oops");
      __ st8(dst, val);
      break;
    }
    case T_BOOLEAN:
      __ and_imm(val, 0x1, val);  // boolean is true if LSB is 1
      __ st1(dst, val);
      break;
    case T_BYTE:    __ st1(dst, val); break;
    case T_CHAR:    __ st2(dst, val); break;
    case T_SHORT:   __ st2(dst, val); break;
    case T_INT:     __ st4(dst, val); break;
    case T_LONG:    __ st8(dst, val); break;
    case T_ADDRESS: __ st8(dst, val); break;
    case T_FLOAT:   __ stfs(dst, f8); break;
    case T_DOUBLE:  __ stfd(dst, f8); break;
    default: Unimplemented();
  }
}

void BarrierSetAssembler::try_resolve_jobject_in_native(MacroAssembler* masm, Register jni_env,
                                                        Register obj, Register tmp, Label& slowpath) {
  // If mask changes we need to ensure that the inverse is still encodable as an immediate
  STATIC_ASSERT(JNIHandles::tag_mask == 3);
  __ and_imm(obj, ~JNIHandles::tag_mask, obj);
  __ ld8(obj, Address(obj, 0));             // *obj
}

// Defines obj, preserves var_size_in_bytes, okay for tmp2 == var_size_in_bytes.
void BarrierSetAssembler::tlab_allocate(MacroAssembler* masm, Register obj,
                                        Register var_size_in_bytes,
                                        int con_size_in_bytes,
                                        Register tmp1,
                                        Register tmp2,
                                        Label& slow_case) {
  assert_different_registers(obj, tmp2);
  assert_different_registers(obj, var_size_in_bytes);
  Register end = tmp2;

  __ ld8(obj, Address(Rthread, JavaThread::tlab_top_offset()));
  if (var_size_in_bytes == noreg) {
    __ lea(end, Address(obj, con_size_in_bytes));
  } else {
    __ add(end, obj, var_size_in_bytes);
  }
  __ ld8(t1, Address(Rthread, JavaThread::tlab_end_offset()));
  __ bgtu(end, t1, slow_case);

  // update the tlab top pointer
  __ st8(Address(Rthread, JavaThread::tlab_top_offset()), end);

  // recover var_size_in_bytes if necessary
  if (var_size_in_bytes == end) {
    __ sub(var_size_in_bytes, var_size_in_bytes, obj);
  }
}

// Milestone 1 (core variant) creates no nmethod that needs an entry barrier:
// the only nmethods are method-handle intrinsic wrappers, and
// BarrierSetNMethod::supports_entry_barrier() excludes those. Both barriers
// arrive with C1.
void BarrierSetAssembler::nmethod_entry_barrier(MacroAssembler* masm) {
  Unimplemented();
}

void BarrierSetAssembler::c2i_entry_barrier(MacroAssembler* masm) {
  // Nothing to do: with no nmethod entry barriers there is no concurrent
  // class unloading for the c2i entry to race with (riscv's check guards a
  // Method* whose holder may be unloaded concurrently).
}
