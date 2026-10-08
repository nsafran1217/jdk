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
#include "asm/assembler.hpp"
#include "asm/assembler.inline.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "code/codeCache.hpp"
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/barrierSetAssembler.hpp"
#include "oops/compressedKlass.inline.hpp"
#include "oops/klass.hpp"
#include "runtime/safepointMechanism.hpp"
#include "runtime/javaThread.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/ostream.hpp"

// libjvm's own gp, the value stored in every descriptor function_entry()
// emits. Read from inside libjvm, so it is exactly what libjvm's .opd entries
// carry -- measured identical on rx2800 (tools/gate/fdprobe.c,
// FRAME-DESIGN.md 3.0).
static address libjvm_gp() {
  address gp_value;
  __asm__ volatile ("mov %0 = gp" : "=r" (gp_value));
  return gp_value;
}

void MacroAssembler::add_imm(Register dst, Register src, int64_t imm, Register tmp) {
  if (ia64::is_simm14(imm)) {
    if (imm != 0 || dst != src) adds(dst, imm, src);
  } else {
    assert_different_registers(src, tmp);
    movl(tmp, (uint64_t)imm);
    add(dst, tmp, src);
  }
}

void MacroAssembler::mul(Register dst, Register a, Register b) {
  setf_sig(ftmp0, a);
  setf_sig(ftmp1, b);
  xma_l(ftmp0, ftmp0, ftmp1, f0);
  getf_sig(dst, ftmp0);
}

void MacroAssembler::mul_imm(Register dst, Register src, int64_t imm, Register tmp) {
  if (imm > 0 && is_power_of_2(imm)) {
    int shift = log2i_exact(imm);
    if (shift == 0) {
      mov(dst, src);
    } else {
      shl_imm(dst, src, shift);
    }
  } else {
    assert_different_registers(src, tmp);
    mov_immediate(tmp, imm);
    mul(dst, src, tmp);
  }
}

void MacroAssembler::load_unaligned_le(Register dst, Register base, int offset, int size, Register tmp) {
  assert(size == 2 || size == 4, "unsupported size");
  assert_different_registers(dst, base, tmp, t0);
  // dst = byte[size-1]; then for each lower byte: dst = (dst << 8) | byte[i].
  ld1(dst, Address(base, offset + size - 1));
  for (int i = size - 2; i >= 0; i--) {
    ld1(tmp, Address(base, offset + i));
    shl_imm(dst, dst, 8);
    or_(dst, dst, tmp);
  }
}

void MacroAssembler::load_unaligned_be(Register dst, Register base, int offset, int size, Register tmp) {
  assert(size == 2 || size == 4, "unsupported size");
  assert_different_registers(dst, base, tmp, t0);
  ld1(dst, Address(base, offset));
  for (int i = 1; i < size; i++) {
    ld1(tmp, Address(base, offset + i));
    shl_imm(dst, dst, 8);
    or_(dst, dst, tmp);
  }
}

void MacroAssembler::lea(Register dst, const Address& adr) {
  switch (adr.getMode()) {
    case Address::base_plus_offset: {
      Register base = adr.base();
      int64_t off = adr.offset();
      if (off == 0) {
        mov(dst, base);
      } else if (ia64::is_simm14(off)) {
        adds(dst, off, base);
      } else {
        // movl into dst would destroy base if they are the same register.
        Register tmp = (dst == base) ? t0 : dst;
        assert(tmp != base, "lea: no scratch left (base and dst are both t0)");
        movl(tmp, (uint64_t)off);
        add(dst, tmp, base);
      }
      break;
    }
    case Address::literal:
      movl(dst, (uint64_t)(uintptr_t)adr.target());
      break;
    default:
      ShouldNotReachHere();
  }
}

// ---- calls and jumps -------------------------------------------------------

void MacroAssembler::call_c(address function_descriptor) {
  // The descriptor is ordinary readable memory, fixed by the dynamic linker
  // before any code is generated, so read it now.
  const address* fd = (const address*)function_descriptor;
  address entry  = fd[0];
  address callee_gp = fd[1];
  // A raw code address passed where a descriptor was expected would read two
  // instruction words as {entry, gp}. This catches the common form of that
  // mistake (CAST_FROM_FN_PTR is right; a StubRoutines entry is not).
  assert(is_aligned(function_descriptor, wordSize) && is_aligned(entry, BytesPerBundle) &&
         callee_gp != nullptr,
         "IA-64: call_c needs a function descriptor, not a code address: " PTR_FORMAT,
         p2i(function_descriptor));
  movl(t0, entry);
  movl(gp, callee_gp);
  mov_to_br(btmp, t0);
  br_call(breturn, btmp);
}

void MacroAssembler::call_c(Register function_descriptor) {
  assert_different_registers(function_descriptor, t0, gp);
  // SpiderMonkey's callABIDescriptorIA64, in HotSpot terms.
  adds(t0, wordSize, function_descriptor);
  Assembler::ld8(gp, t0);                     // callee gp
  Assembler::ld8(t0, function_descriptor);    // entry
  mov_to_br(btmp, t0);
  br_call(breturn, btmp);
}

void MacroAssembler::far_call(address entry, PredicateRegister qp) {
  assert(entry != nullptr, "far_call to null");
  movl(t0, entry);
  mov_to_br(btmp, t0);
  br_call(breturn, btmp, qp);
}

void MacroAssembler::far_jump(address entry, PredicateRegister qp) {
  assert(entry != nullptr, "far_jump to null");
  movl(t0, entry);
  mov_to_br(btmp, t0);
  br_cond(btmp, qp);
}

address MacroAssembler::function_entry() {
  // The descriptor is data in the instruction stream; it is 16 bytes, so the
  // code that follows stays bundle-aligned. The entry it records is absolute,
  // which is only right because StubCodeGenerator stubs are generated in
  // place in the code cache rather than in a buffer that is later copied.
  assert(is_aligned(offset(), BytesPerBundle), "must be bundle-aligned");
  address fd = pc();
  emit_int64((int64_t)(uintptr_t)(fd + 2 * wordSize));
  emit_int64((int64_t)(uintptr_t)libjvm_gp());
  return fd;
}

// ---- frames ----------------------------------------------------------------

void MacroAssembler::enter() {
  // sp moves first: anything stored below sp could be overwritten by a signal
  // frame before sp covered it (no red zone on Linux/IA-64).
  mov_from_br(t0, breturn);
  adds(sp, -enter_frame_words() * wordSize, sp);
  adds(t1, (enter_frame_words() - 1) * wordSize, sp);   // caller_sp[-1]
  Assembler::st8(t1, t0);                               //   return address
  adds(t1, (enter_frame_words() - 2) * wordSize, sp);   // caller_sp[-2]
  Assembler::st8(t1, fp);                               //   caller's fp
  adds(fp, enter_frame_words() * wordSize, sp);         // fp = caller's sp
}

void MacroAssembler::leave() {
  // Load the linkage while it is still covered by sp, then pop.
  adds(t1, frame::return_addr_offset * wordSize, fp);
  Assembler::ld8(t0, t1);                               // return address
  adds(t1, frame::link_offset * wordSize, fp);
  Assembler::ld8(t1, t1);                               // caller's fp
  mov(sp, fp);
  mov(fp, t1);
  mov_to_br(breturn, t0);
}

// ---- the thread's frame anchor ----------------------------------------------
//
// Ordering follows JavaFrameAnchor's C++ accessors: last_Java_sp is what makes
// the anchor visible, so it is published last with release semantics when set,
// and cleared first when reset. IA-64 is weakly ordered enough that this is
// not a formality.

void MacroAssembler::set_last_Java_frame(Register last_java_sp, Register last_java_fp,
                                         address last_java_pc, Register tmp) {
  assert(last_java_pc != nullptr, "must provide a valid pc");
  assert_different_registers(last_java_sp, tmp, t0, t1);
  movl(tmp, last_java_pc);
  st8(Address(Rthread, JavaThread::frame_anchor_offset() + JavaFrameAnchor::last_Java_pc_offset()), tmp, t1);
  if (last_java_fp->is_valid()) {
    st8(Address(Rthread, JavaThread::last_Java_fp_offset()), last_java_fp, t1);
  }
  lea(t1, Address(Rthread, JavaThread::last_Java_sp_offset()));
  st8_rel(t1, last_java_sp);
}

void MacroAssembler::set_last_Java_frame(Register last_java_sp, Register last_java_fp,
                                         Label& L, Register tmp) {
  assert_different_registers(last_java_sp, tmp, t0, t1);
  la(tmp, L, t0);
  st8(Address(Rthread, JavaThread::frame_anchor_offset() + JavaFrameAnchor::last_Java_pc_offset()), tmp, t1);
  if (last_java_fp->is_valid()) {
    st8(Address(Rthread, JavaThread::last_Java_fp_offset()), last_java_fp, t1);
  }
  lea(t1, Address(Rthread, JavaThread::last_Java_sp_offset()));
  st8_rel(t1, last_java_sp);
}

void MacroAssembler::reset_last_Java_frame(bool clear_fp) {
  // we must set sp to zero to clear frame
  st8(Address(Rthread, JavaThread::last_Java_sp_offset()), zr, t1);
  // must clear fp, so that compiled frames are not confused; it is
  // possible that we need it only for debugging
  if (clear_fp) {
    lea(t1, Address(Rthread, JavaThread::last_Java_fp_offset()));
    st8_rel(t1, zr);
  }
  // Always clear the pc because it could have been set by make_walkable()
  lea(t1, Address(Rthread, JavaThread::last_Java_pc_offset()));
  st8_rel(t1, zr);
}

// ---- calls into the VM -------------------------------------------------------

void MacroAssembler::call_VM_base(Register oop_result,
                                  Register java_thread,
                                  Register last_java_sp,
                                  address  entry_point,
                                  int      number_of_arguments,
                                  bool     check_exceptions) {
  // determine java_thread register
  if (!java_thread->is_valid()) {
    java_thread = Rthread;
  }
  // determine last_java_sp register
  if (!last_java_sp->is_valid()) {
    last_java_sp = Resp;
  }

  // debugging support
  assert(number_of_arguments >= 0   , "cannot have negative number of arguments");
  assert(java_thread == Rthread, "unexpected register");
  assert(java_thread != oop_result  , "cannot use the same register for java_thread & oop_result");
  assert(java_thread != last_java_sp, "cannot use the same register for java_thread & last_java_sp");
  assert(last_java_sp != fp, "can't use fp");

  // the thread becomes the first argument of the C function
  mov(c_rarg0, java_thread);

  // set last Java frame before call; the recorded pc is the call's return
  // address, bound by call_VM_leaf_base
  Label l;
  set_last_Java_frame(last_java_sp, fp, l, t2);

  // do the call
  MacroAssembler::call_VM_leaf_base(entry_point, number_of_arguments, &l);

  // reset last Java frame
  // Only interpreter should have to clear fp
  reset_last_Java_frame(true);

  // C++ interp handles this in the interpreter
  check_and_handle_popframe(java_thread);
  check_and_handle_earlyret(java_thread);

  if (check_exceptions) {
    check_pending_exception();
  }

  // get oop result if there is one and reset the value in the thread
  if (oop_result->is_valid()) {
    get_vm_result_oop(oop_result, java_thread);
  }
}

void MacroAssembler::check_pending_exception() {
  address forward = StubRoutines::forward_exception_entry();
  assert(forward != nullptr, "forward_exception stub must be generated first");
  ld8(t1, Address(Rthread, Thread::pending_exception_offset()));
  cmp_eq(ptmp0, ptmp1, t1, zr);
  far_jump(forward, ptmp1);
}

void MacroAssembler::get_vm_result_oop(Register oop_result, Register java_thread) {
  ld8(oop_result, Address(java_thread, JavaThread::vm_result_oop_offset()));
  st8(Address(java_thread, JavaThread::vm_result_oop_offset()), zr);
  verify_oop_msg(oop_result, "broken oop in call_VM_base");
}

void MacroAssembler::get_vm_result_metadata(Register metadata_result, Register java_thread) {
  ld8(metadata_result, Address(java_thread, JavaThread::vm_result_metadata_offset()));
  st8(Address(java_thread, JavaThread::vm_result_metadata_offset()), zr);
}

void MacroAssembler::call_VM_leaf_base(address entry_point, int number_of_arguments, Label* retaddr) {
  // b0 is overwritten by the call: the frame this runs in must already have
  // saved its own return address (enter(), or the interpreter's prologue).
  call_c(entry_point);
  if (retaddr != nullptr) {
    bind(*retaddr);
  }
}

void MacroAssembler::call_VM_helper(Register oop_result, address entry_point, int number_of_arguments, bool check_exceptions) {
  call_VM_base(oop_result, noreg, noreg, entry_point, number_of_arguments, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result, address entry_point, bool check_exceptions) {
  call_VM_helper(oop_result, entry_point, 0, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result, address entry_point, Register arg_1, bool check_exceptions) {
  pass_arg(c_rarg1, arg_1);
  call_VM_helper(oop_result, entry_point, 1, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result, address entry_point, Register arg_1, Register arg_2, bool check_exceptions) {
  assert_different_registers(arg_1, c_rarg2);
  pass_arg(c_rarg2, arg_2);
  pass_arg(c_rarg1, arg_1);
  call_VM_helper(oop_result, entry_point, 2, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result, address entry_point, Register arg_1,
                             Register arg_2, Register arg_3, bool check_exceptions) {
  assert_different_registers(arg_1, c_rarg2, c_rarg3);
  assert_different_registers(arg_2, c_rarg3);
  pass_arg(c_rarg3, arg_3);
  pass_arg(c_rarg2, arg_2);
  pass_arg(c_rarg1, arg_1);
  call_VM_helper(oop_result, entry_point, 3, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result, Register last_java_sp, address entry_point,
                             int number_of_arguments, bool check_exceptions) {
  call_VM_base(oop_result, Rthread, last_java_sp, entry_point, number_of_arguments, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result, Register last_java_sp, address entry_point,
                             Register arg_1, bool check_exceptions) {
  pass_arg(c_rarg1, arg_1);
  call_VM(oop_result, last_java_sp, entry_point, 1, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result, Register last_java_sp, address entry_point,
                             Register arg_1, Register arg_2, bool check_exceptions) {
  assert_different_registers(arg_1, c_rarg2);
  pass_arg(c_rarg2, arg_2);
  pass_arg(c_rarg1, arg_1);
  call_VM(oop_result, last_java_sp, entry_point, 2, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result, Register last_java_sp, address entry_point,
                             Register arg_1, Register arg_2, Register arg_3, bool check_exceptions) {
  assert_different_registers(arg_1, c_rarg2, c_rarg3);
  assert_different_registers(arg_2, c_rarg3);
  pass_arg(c_rarg3, arg_3);
  pass_arg(c_rarg2, arg_2);
  pass_arg(c_rarg1, arg_1);
  call_VM(oop_result, last_java_sp, entry_point, 3, check_exceptions);
}

void MacroAssembler::call_VM_leaf(address entry_point, int number_of_arguments) {
  call_VM_leaf_base(entry_point, number_of_arguments);
}

void MacroAssembler::call_VM_leaf(address entry_point, Register arg_0) {
  pass_arg(c_rarg0, arg_0);
  call_VM_leaf_base(entry_point, 1);
}

void MacroAssembler::call_VM_leaf(address entry_point, Register arg_0, Register arg_1) {
  assert_different_registers(arg_0, c_rarg1);
  pass_arg(c_rarg1, arg_1);
  pass_arg(c_rarg0, arg_0);
  call_VM_leaf_base(entry_point, 2);
}

void MacroAssembler::call_VM_leaf(address entry_point, Register arg_0, Register arg_1, Register arg_2) {
  assert_different_registers(arg_0, c_rarg1, c_rarg2);
  assert_different_registers(arg_1, c_rarg2);
  pass_arg(c_rarg2, arg_2);
  pass_arg(c_rarg1, arg_1);
  pass_arg(c_rarg0, arg_0);
  call_VM_leaf_base(entry_point, 3);
}

void MacroAssembler::super_call_VM_leaf(address entry_point) {
  MacroAssembler::call_VM_leaf_base(entry_point, 0);
}

void MacroAssembler::super_call_VM_leaf(address entry_point, Register arg_0) {
  pass_arg(c_rarg0, arg_0);
  MacroAssembler::call_VM_leaf_base(entry_point, 1);
}

void MacroAssembler::super_call_VM_leaf(address entry_point, Register arg_0, Register arg_1) {
  assert_different_registers(arg_0, c_rarg1);
  pass_arg(c_rarg1, arg_1);
  pass_arg(c_rarg0, arg_0);
  MacroAssembler::call_VM_leaf_base(entry_point, 2);
}

void MacroAssembler::super_call_VM_leaf(address entry_point, Register arg_0, Register arg_1, Register arg_2) {
  assert_different_registers(arg_0, c_rarg1, c_rarg2);
  assert_different_registers(arg_1, c_rarg2);
  pass_arg(c_rarg2, arg_2);
  pass_arg(c_rarg1, arg_1);
  pass_arg(c_rarg0, arg_0);
  MacroAssembler::call_VM_leaf_base(entry_point, 3);
}

void MacroAssembler::super_call_VM_leaf(address entry_point, Register arg_0, Register arg_1, Register arg_2, Register arg_3) {
  assert_different_registers(arg_0, c_rarg1, c_rarg2, c_rarg3);
  assert_different_registers(arg_1, c_rarg2, c_rarg3);
  assert_different_registers(arg_2, c_rarg3);
  pass_arg(c_rarg3, arg_3);
  pass_arg(c_rarg2, arg_2);
  pass_arg(c_rarg1, arg_1);
  pass_arg(c_rarg0, arg_0);
  MacroAssembler::call_VM_leaf_base(entry_point, 4);
}

// ---- null checks -------------------------------------------------------------

void MacroAssembler::null_check(Register reg, int offset) {
  if (needs_explicit_null_check(offset)) {
    // provoke OS null exception if reg is null by accessing M[reg]. The
    // destination cannot be r0 as on riscv: writing r0 is an Illegal
    // Operation fault on IA-64, not a discarded result.
    Assembler::ld8(t0, reg);
  } else {
    // nothing to do, (later) access of M[reg + offset]
    // will provoke OS null exception if reg is null
  }
}

// ---- debugging ---------------------------------------------------------------

void MacroAssembler::stop(const char* msg) {
  // break.b, then the message pointer as a data bundle the trap never falls
  // into. The SIGILL handler recognises the immediate and reads the message
  // from the following bundle.
  brk(stop_break_imm);
  emit_int64((int64_t)(uintptr_t)msg);
  emit_int64(0);
}

bool MacroAssembler::is_stop(address pc) {
  const ia64::Bundle* b = (const ia64::Bundle*)align_down(pc, BytesPerBundle);
  ia64::Bundle expected = ia64::BundleB(ia64::BreakB(stop_break_imm));
  return b->lo == expected.lo && b->hi == expected.hi;
}

const char* MacroAssembler::stop_message(address pc) {
  assert(is_stop(pc), "not a stop()");
  return *(const char**)(align_down(pc, BytesPerBundle) + BytesPerBundle);
}

void MacroAssembler::debug64(char* msg, int64_t pc, int64_t regs[]) {
  ttyLocker ttyl;
  tty->print_cr("IA-64 debug64: %s (pc " INT64_FORMAT_X ")", msg, pc);
  fatal("DEBUG MESSAGE: %s", msg);
}

void MacroAssembler::unimplemented(const char* what) {
  const char* buf = nullptr;
  {
    ResourceMark rm;
    stringStream ss;
    ss.print("unimplemented: %s", what);
    buf = code_string(ss.as_string());
  }
  stop(buf);
}

// ---- heap and metadata access -------------------------------------------------

void MacroAssembler::access_load_at(BasicType type, DecoratorSet decorators,
                                    Register dst, Address src,
                                    Register tmp1, Register tmp2) {
  BarrierSetAssembler* bs = BarrierSet::barrier_set()->barrier_set_assembler();
  decorators = AccessInternal::decorator_fixup(decorators, type);
  bool as_raw = (decorators & AS_RAW) != 0;
  if (as_raw) {
    bs->BarrierSetAssembler::load_at(this, decorators, type, dst, src, tmp1, tmp2);
  } else {
    bs->load_at(this, decorators, type, dst, src, tmp1, tmp2);
  }
}

void MacroAssembler::access_store_at(BasicType type, DecoratorSet decorators,
                                     Address dst, Register val,
                                     Register tmp1, Register tmp2, Register tmp3) {
  BarrierSetAssembler* bs = BarrierSet::barrier_set()->barrier_set_assembler();
  decorators = AccessInternal::decorator_fixup(decorators, type);
  bool as_raw = (decorators & AS_RAW) != 0;
  if (as_raw) {
    bs->BarrierSetAssembler::store_at(this, decorators, type, dst, val, tmp1, tmp2, tmp3);
  } else {
    bs->store_at(this, decorators, type, dst, val, tmp1, tmp2, tmp3);
  }
}

void MacroAssembler::load_heap_oop(Register dst, Address src, Register tmp1,
                                   Register tmp2, DecoratorSet decorators) {
  access_load_at(T_OBJECT, IN_HEAP | decorators, dst, src, tmp1, tmp2);
}

void MacroAssembler::load_heap_oop_not_null(Register dst, Address src, Register tmp1,
                                            Register tmp2, DecoratorSet decorators) {
  access_load_at(T_OBJECT, IN_HEAP | IS_NOT_NULL | decorators, dst, src, tmp1, tmp2);
}

void MacroAssembler::store_heap_oop(Address dst, Register val, Register tmp1,
                                    Register tmp2, Register tmp3, DecoratorSet decorators) {
  access_store_at(T_OBJECT, IN_HEAP | decorators, dst, val, tmp1, tmp2, tmp3);
}

void MacroAssembler::resolve_oop_handle(Register result, Register tmp1, Register tmp2) {
  access_load_at(T_OBJECT, IN_NATIVE, result, Address(result, 0), tmp1, tmp2);
}

void MacroAssembler::load_method_holder(Register holder, Register method) {
  ld8(holder, Address(method, Method::const_offset()));                      // ConstMethod*
  ld8(holder, Address(holder, ConstMethod::constants_offset()));             // ConstantPool*
  ld8(holder, Address(holder, ConstantPool::pool_holder_offset()));          // InstanceKlass*
}

void MacroAssembler::load_mirror(Register dst, Register method, Register tmp1, Register tmp2) {
  const int mirror_offset = in_bytes(Klass::java_mirror_offset());
  load_method_holder(dst, method);
  ld8(dst, Address(dst, mirror_offset));
  resolve_oop_handle(dst, tmp1, tmp2);
}

void MacroAssembler::decode_klass_not_null(Register r) {
  assert(UseCompressedClassPointers, "should only be used for compressed headers");
  if (CompressedKlassPointers::shift() != 0) {
    shl_imm(r, r, CompressedKlassPointers::shift());
  }
  if (CompressedKlassPointers::base() != nullptr) {
    assert(r != t0, "decode_klass_not_null clobbers t0");
    movl(t0, (address)CompressedKlassPointers::base());
    add(r, r, t0);
  }
}

void MacroAssembler::load_klass(Register dst, Register src) {
  assert(!UseCompactObjectHeaders, "IA-64: compact object headers not yet supported");
  if (UseCompressedClassPointers) {
    ld4(dst, Address(src, oopDesc::klass_offset_in_bytes()));
    decode_klass_not_null(dst);
  } else {
    ld8(dst, Address(src, oopDesc::klass_offset_in_bytes()));
  }
}

void MacroAssembler::check_klass_subtype_fast_path(Register sub_klass, Register super_klass, Register tmp,
                                                   Label* L_success, Label* L_failure, Label* L_slow_path) {
  assert_different_registers(sub_klass, super_klass, tmp, t0, t1);
  Label L_fallthrough;
  int label_nulls = 0;
  if (L_success == nullptr)   { L_success   = &L_fallthrough; label_nulls++; }
  if (L_failure == nullptr)   { L_failure   = &L_fallthrough; label_nulls++; }
  if (L_slow_path == nullptr) { L_slow_path = &L_fallthrough; label_nulls++; }
  assert(label_nulls <= 1, "at most one null in batch");

  int sc_offset = in_bytes(Klass::secondary_super_cache_offset());
  int sco_offset = in_bytes(Klass::super_check_offset_offset());

  // If the pointers are equal, we are done (e.g., String[] elements).
  beq(sub_klass, super_klass, *L_success);

  // Check the supertype display, or the secondary super cache that aliases
  // into it: super_check_offset says which.
  ld4(tmp, Address(super_klass, sco_offset));
  add(t1, sub_klass, tmp);
  Assembler::ld8(t1, t1);                     // displayed supertype
  beq(super_klass, t1, *L_success);

  // A miss in the primary display is decisive; a miss in the cache is not.
  mov_immediate(t1, sc_offset);
  if (L_failure == &L_fallthrough) {
    beq(tmp, t1, *L_slow_path);
  } else {
    bne(tmp, t1, *L_failure);
    if (L_slow_path != &L_fallthrough) j(*L_slow_path);
  }
  bind(L_fallthrough);
}

void MacroAssembler::check_klass_subtype_slow_path(Register sub_klass, Register super_klass,
                                                   Register tmp1, Register tmp2,
                                                   Label* L_success, Label* L_failure) {
  // A linear scan of sub_klass->secondary_supers(). The array may also carry
  // the hashed-table layout UseSecondarySupersTable builds, but a linear scan
  // over all its elements finds any member regardless of order.
  assert_different_registers(sub_klass, super_klass, tmp1, tmp2, t0, t1);
  Label L_fallthrough, L_loop;
  int label_nulls = 0;
  if (L_success == nullptr) { L_success = &L_fallthrough; label_nulls++; }
  if (L_failure == nullptr) { L_failure = &L_fallthrough; label_nulls++; }
  assert(label_nulls <= 1, "at most one null in the batch");

  const Register array = tmp1;
  const Register count = tmp2;
  ld8(array, Address(sub_klass, in_bytes(Klass::secondary_supers_offset())));
  ld4(count, Address(array, Array<Klass*>::length_offset_in_bytes()));
  adds(array, Array<Klass*>::base_offset_in_bytes(), array);

  bind(L_loop);
  beqz(count, *L_failure);
  ld8_inc(t1, array, wordSize);
  adds(count, -1, count);
  bne(t1, super_klass, L_loop);

  // Hit: remember it in the cache.
  st8(Address(sub_klass, in_bytes(Klass::secondary_super_cache_offset())), super_klass);
  if (L_success != &L_fallthrough) j(*L_success);
  bind(L_fallthrough);
}

void MacroAssembler::check_klass_subtype(Register sub_klass, Register super_klass,
                                         Register tmp1, Register tmp2, Label& L_success) {
  Label L_failure;
  check_klass_subtype_fast_path(sub_klass, super_klass, tmp1, &L_success, &L_failure, nullptr);
  check_klass_subtype_slow_path(sub_klass, super_klass, tmp1, tmp2, &L_success, nullptr);
  bind(L_failure);
}

void MacroAssembler::safepoint_poll(Label& slow_path, bool at_return, bool acquire, bool in_nmethod, Register tmp) {
  if (acquire) {
    lea(tmp, Address(Rthread, JavaThread::polling_word_offset()));
    ld8_acq(tmp, tmp);
  } else {
    ld8(tmp, Address(Rthread, JavaThread::polling_word_offset()));
  }
  if (at_return) {
    bgtu(in_nmethod ? sp : fp, tmp, slow_path);
  } else {
    tbit_nz(ptmp0, ptmp1, tmp, exact_log2(SafepointMechanism::poll_bit()));
    br_cond(slow_path, ptmp0);
  }
}
