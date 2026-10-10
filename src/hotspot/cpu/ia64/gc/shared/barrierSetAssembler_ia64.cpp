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
#include "runtime/stubRoutines.hpp"

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

// nmethod entry barrier (FRAME-DESIGN.md 11.3), emitted at the end of a
// compiled method's prologue. Fixed length -- BarrierSetNMethod finds the
// guard from the frame-complete offset -- and the guard is a data word inline
// in the code, branched over like far_call's cell, so arming and disarming
// are plain aligned 4-byte stores with no instruction patching:
//
//     br over                       (bundle 0)
//     <guard: int32, 12 bytes pad>  (bundle 1)
//  over:
//     mov t0 = ip ; adds t0 = -16, t0 ; ld4.acq t0 = [t0]
//     adds t1 = <disarmed offset>, Rthread ; ld4 t1 = [t1]
//     cmp4.eq p6, p7 = t0, t1
//     (p6) br skip
//     far_call method_entry_barrier stub   (7 bundles)
//  skip:
//
// The guard is loaded with acquire, so the method's subsequent loads of oops
// (patched by the disarming thread before its release store of the guard)
// are ordered after it: conc_data_patch semantics. t0, t1 and p6/p7 are free
// at a method entry; the stub preserves everything else.
// Five hand-bundled bundles, four instruction groups, no taken branch:
//
//  B0  { adds t1 = disarmed_off, Rthread ; movl t2 = method_entry_barrier }
//  B1  { nop.m ; mov t0 = ip ; nop.i ;; }
//  B2  { ld4 t1 = [t1] ; adds t0 = guard - B1, t0 ; mov b7 = t2 ;; }
//  B3  { ld4.acq t0 = [t0] ; nop.x <guard> ;; }
//  B4  { cmp4.ne p6, p7 = t0, t1 ; nop.i ; (p6) br.call.spnt b0 = b7 ;; }
//
// The guard is bytes 8-11 of B3, inside nop.x's immediate (NopXBundle), so
// the bundle executes the same whatever the guard holds and nothing has to
// jump over it; the instruction cache never needs to see a new value. The
// slow path is a predicated call that returns to frame-complete. The stub's
// address is absolute, so the movl stays right when the code moves.
//
// The call goes through b7, not b6: Itanium 2 predicts an indirect branch
// from the branch register's value when it is fetched, so a caller's
// `br.call b6` is predicted right only while b6 still holds that call's
// target. Writing b6 here made every call into a compiled method mispredict
// (measured, rx2800: one wrong-target indirect branch per call). b7 is the
// interpreter's dispatch register, which it never trusts across a call.
void BarrierSetAssembler::nmethod_entry_barrier(MacroAssembler* masm) {
  BarrierSetNMethod* bs_nm = BarrierSet::barrier_set()->barrier_set_nmethod();
  if (bs_nm == nullptr) {
    return;
  }
  Assembler::NoPackScope no_pack(masm);   // fixed length: BarrierSetNMethod finds the guard
  int start = __ offset();
  using namespace ia64;
  const int disarmed = in_bytes(bs_nm->thread_disarmed_guard_value_offset());
  const int guard_from_b1 = entry_barrier_guard_offset - BytesPerBundle;
  __ emit_bundle(MovlBundleWith(Adds(t1->encoding(), disarmed, Rthread->encoding()),
                                t2->encoding(), (uint64_t)StubRoutines::method_entry_barrier(), 0, tMLX));
  __ emit_bundle(MakeBundle(tMII_, NopM(), MovFromIp(t0->encoding()), NopI()));
  __ emit_bundle(MakeBundle(tMMI_, Ld4(t1->encoding(), t1->encoding()),
                            Adds(t0->encoding(), guard_from_b1, t0->encoding()),
                            MovToBr(b7.encoding(), t2->encoding())));
  __ emit_bundle(NopXBundle(Ld4Acq(t0->encoding(), t0->encoding()), tMLX_));
  __ emit_bundle(MakeBundle(tMIB_, Cmp4Ne(ptmp0.encoding(), ptmp1.encoding(), t0->encoding(), t1->encoding()),
                            NopI(), BrCall(breturn.encoding(), b7.encoding(), ptmp0.encoding(), kIndSpnt)));
  assert(__ offset() - start == entry_barrier_size, "entry_barrier_size is wrong");
}

void BarrierSetAssembler::c2i_entry_barrier(MacroAssembler* masm) {
  // Nothing to do: with no nmethod entry barriers there is no concurrent
  // class unloading for the c2i entry to race with (riscv's check guards a
  // Method* whose holder may be unloaded concurrently).
}

#ifdef COMPILER2

// As ppc: a register's second (upper-half) OptoReg slot names no register of
// its own, so it refines to Bad.
OptoReg::Name BarrierSetAssembler::refine_register(const Node* node, OptoReg::Name opto_reg) const {
  if (!OptoReg::is_reg(opto_reg)) {
    return OptoReg::Bad;
  }

  VMReg vm_reg = OptoReg::as_VMReg(opto_reg);
  if ((vm_reg->is_Register() || vm_reg->is_FloatRegister()) && (opto_reg & 1) != 0) {
    return OptoReg::Bad;
  }

  return opto_reg;
}

#endif // COMPILER2
