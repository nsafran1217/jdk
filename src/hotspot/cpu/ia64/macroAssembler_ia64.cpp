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

// The break.b immediate stop() uses. Linux/IA-64 (arch/ia64/kernel/traps.c,
// ia64_bad_break) delivers break immediates in [0x40000, 0x80000) as SIGILL
// with si_code __ILL_BREAK, distinct from break 0 (GCC's __builtin_trap) and
// break 1 (integer divide by zero, SIGFPE). [UNVERIFIED on rx2800 -- the
// kernel mapping is from the Linux source, not yet observed.]
static const uint32_t stop_break_imm = 0x40000 | 0x5709;

void MacroAssembler::add_imm(Register dst, Register src, int64_t imm, Register tmp) {
  if (ia64::is_simm14(imm)) {
    if (imm != 0 || dst != src) adds(dst, imm, src);
  } else {
    assert_different_registers(src, tmp);
    movl(tmp, (uint64_t)imm);
    add(dst, tmp, src);
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

void MacroAssembler::debug64(char* msg, int64_t pc, int64_t regs[]) {
  ttyLocker ttyl;
  tty->print_cr("IA-64 debug64: %s (pc " INT64_FORMAT_X ")", msg, pc);
  fatal("DEBUG MESSAGE: %s", msg);
}
