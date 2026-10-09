/*
 * Copyright (c) 2003, 2025, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, 2020, Red Hat Inc. All rights reserved.
 * Copyright (c) 2020, 2023, Huawei Technologies Co., Ltd. All rights reserved.
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

#include "asm/macroAssembler.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/barrierSetAssembler.hpp"
#include "gc/shared/barrierSetNMethod.hpp"
#include "interpreter/interpreter.hpp"
#include "memory/universe.hpp"
#include "nativeInst_ia64.hpp"
#include "oops/method.hpp"
#include "registerSaver_ia64.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/javaThread.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubCodeGenerator.hpp"
#include "runtime/stubRoutines.hpp"
#include "utilities/align.hpp"

// Stub generator for IA-64.
//
// Milestone 1 generates only the stubs the core variant needs: call_stub,
// catch_exception and forward_exception. There is no compiler to call the
// arraycopy, crypto or math stubs, SafeFetch is static assembly
// (os_cpu/linux_ia64/safefetch_linux_ia64.S), continuations are disabled
// (VMContinuations is false) and FFM upcalls are unsupported. See
// JIT-SCOPE.md phase 3.

#define __ _masm->

#ifdef PRODUCT
#define BLOCK_COMMENT(str) /* nothing */
#else
#define BLOCK_COMMENT(str) __ block_comment(str)
#endif

#define BIND(label) bind(label); BLOCK_COMMENT(#label ":")

class StubGenerator: public StubCodeGenerator {
 private:

  // ---------------------------------------------------------------------------
  // call_stub: the C -> Java boundary, and the only place generated code
  // touches the register stack. FRAME-DESIGN.md sections 1 and 5.
  //
  // C signature (StubRoutines::CallStub):
  //    c_rarg0:   call wrapper address                   address
  //    c_rarg1:   result                                 address
  //    c_rarg2:   result type                            BasicType
  //    c_rarg3:   method                                 Method*
  //    c_rarg4:   (interpreter) entry point              address
  //    c_rarg5:   parameters                             intptr_t*
  //    c_rarg6:   parameter size (in words)              int
  //    c_rarg7:   thread                                 Thread*
  //
  // The arguments arrive in the C caller's output registers, which a
  // br.call makes this procedure's r32-r39 -- the same numbers c_rarg0-7
  // name. The stub's single `alloc 0, 0, 8` keeps the base of the frame
  // where it is, so they stay put; it only fixes the frame at exactly eight
  // outputs (sol = 0), which is what lets every later br.call in generated
  // code leave CFM unchanged.
  //
  // C passes narrow ints with undefined upper bits, so result type and
  // parameter size are only ever used through their low 32 bits.
  //
  // The frame, in words from fp (fp = the C caller's sp, 16-byte aligned):
  //
  //   -1      return address (b0)
  //   -2      the C caller's r4, st8.spill'd   [the HotSpot link slot]
  //   -3      thread             c_rarg7
  //   -4      parameter size     c_rarg6
  //   -5      parameters         c_rarg5
  //   -6      entry point        c_rarg4
  //   -7      method             c_rarg3
  //   -8      result type        c_rarg2
  //   -9      result             c_rarg1
  //   -10     call wrapper       c_rarg0       [entry_frame_call_wrapper_offset]
  //   -11     the C caller's ar.pfs, as returned by our alloc
  //   -12     the C caller's ar.unat
  //   -13     ar.unat after the GPR spills -- what ld8.fill must see
  //   -14     the C caller's gp
  //   -15..-17  r5, r6, r7, st8.spill'd
  //   -18     (padding: the FP slots must be 16-byte aligned)
  //   -20..-57  f2-f5, f16-f31: twenty 16-byte stf.spill slots
  //   -60..-59  psABI scratch area                <-- sp_after_call
  //   below:    the Java arguments, pushed through Resp
  //
  // The C callee-saved state is saved bit-exactly: NaT bits through
  // st8.spill/ld8.fill and ar.unat, all 82 FP bits through stf.spill/ldf.fill
  // (FRAME-DESIGN.md 5.1). p1-p5 and b1-b5 are also callee-saved under the
  // psABI but are not saved: generated code never writes them (it uses
  // p6-p15 and b0/b6 only). ar.lc and ar.fpsr are likewise never changed.

  enum call_stub_layout {
    retaddr_off        = -1,
    link_off           = -2,
    thread_off         = -3,
    parameter_size_off = -4,
    parameters_off     = -5,
    entry_point_off    = -6,
    method_off         = -7,
    result_type_off    = -8,
    result_off         = -9,
    call_wrapper_off   = -10,
    pfs_off            = -11,
    caller_unat_off    = -12,
    spill_unat_off     = -13,
    gp_off             = -14,
    r5_off             = -15,
    r6_off             = -16,
    r7_off             = -17,
    fpr_first_off      = -20,   // f2; each later FPR is 2 words further down
    sp_after_call_off  = -60,
    frame_words        = -sp_after_call_off
  };

  static int fpr_off(int i) { return fpr_first_off - 2 * i; }

  // Store/load a word at fp_reg + off words, through the scratch register t3.
  void st_at(Register fp_reg, int off, Register val) {
    __ adds(t3, off * wordSize, fp_reg);
    __ st8(t3, val);
  }
  void ld_at(Register dst, Register fp_reg, int off) {
    __ adds(t3, off * wordSize, fp_reg);
    __ ld8(dst, t3);
  }

  address generate_call_stub(address& return_address) {
    assert((int)frame::entry_frame_after_call_words == -(int)sp_after_call_off + 1 &&
           (int)frame::entry_frame_call_wrapper_offset == (int)call_wrapper_off,
           "adjust this code");
    STATIC_ASSERT((frame_words * wordSize) % 16 == 0);

    // The preserved FP registers, in slot order.
    static const FloatRegister saved_fprs[] = {
      f2,  f3,  f4,  f5,
      f16, f17, f18, f19, f20, f21, f22, f23,
      f24, f25, f26, f27, f28, f29, f30, f31
    };
    const int n_saved_fprs = sizeof(saved_fprs) / sizeof(saved_fprs[0]);
    assert(fpr_off(n_saved_fprs - 1) > sp_after_call_off + 1, "FP save area overlaps the scratch area");

    StubGenStubId stub_id = StubGenStubId::call_stub_id;
    StubCodeMark mark(this, stub_id);

    // C++ calls this stub through a function pointer, so it must start with a
    // descriptor; what StubRoutines records is the descriptor's address.
    address start = __ function_entry();

    // The one and only alloc (FRAME-DESIGN.md section 1). t1 = the C
    // caller's ar.pfs, which br.ret will need back.
    __ alloc(t1, 0, 0, 8, 0);
    __ mov_from_br(t0, breturn);

    // Lower sp first: there is no red zone, so nothing may be stored below
    // sp even briefly. t2 is the C caller's sp, i.e. our fp-to-be.
    __ mov(t2, sp);
    __ adds(sp, -frame_words * wordSize, sp);

    st_at(t2, retaddr_off, t0);
    st_at(t2, pfs_off,     t1);
    st_at(t2, gp_off,      gp);

    // The C caller's preserved GPRs, NaT bits included. ar.unat first, since
    // the spills change it.
    __ mov_from_ar(t4, ia64::kArUnat);
    st_at(t2, caller_unat_off, t4);
    __ adds(t3, link_off * wordSize, t2);  __ st8_spill(t3, r4);
    __ adds(t3, r5_off   * wordSize, t2);  __ st8_spill(t3, r5);
    __ adds(t3, r6_off   * wordSize, t2);  __ st8_spill(t3, r6);
    __ adds(t3, r7_off   * wordSize, t2);  __ st8_spill(t3, r7);
    __ mov_from_ar(t4, ia64::kArUnat);
    st_at(t2, spill_unat_off, t4);

    // The C caller's preserved FPRs, all 82 bits.
    for (int i = 0; i < n_saved_fprs; i++) {
      __ adds(t3, fpr_off(i) * wordSize, t2);
      __ stf_spill(t3, saved_fprs[i]);
    }

    // The arguments.
    st_at(t2, call_wrapper_off,   c_rarg0);
    st_at(t2, result_off,         c_rarg1);
    st_at(t2, result_type_off,    c_rarg2);
    st_at(t2, method_off,         c_rarg3);
    st_at(t2, entry_point_off,    c_rarg4);
    st_at(t2, parameters_off,     c_rarg5);
    st_at(t2, parameter_size_off, c_rarg6);
    st_at(t2, thread_off,         c_rarg7);

    // r4-r7 are ours from here on.
    __ mov(fp, t2);
    __ mov(Rthread, c_rarg7);
    __ mov(Rmethod, c_rarg3);

#ifdef ASSERT
    // make sure we have no pending exceptions
    {
      Label L;
      __ ld8(t2, Address(Rthread, in_bytes(Thread::pending_exception_offset())));
      __ beqz(t2, L);
      __ stop("StubRoutines::call_stub: entered with pending exception");
      __ BIND(L);
    }
#endif

    // Pass parameters, if any. Resp starts at sp_after_call and the
    // arguments are pushed below it, parameters[0] first; sp drops below
    // them all, 16-byte aligned.
    BLOCK_COMMENT("pass parameters if any");
    const Register count = t2;
    const Register src   = t4;
    __ sxt4(count, c_rarg6);
    __ mov(src, c_rarg5);
    __ mov(Resp, sp);
    __ shl_imm(t3, count, LogBytesPerWord);
    __ sub(t3, sp, t3);
    __ and_imm(sp, -2 * wordSize, t3);

    Label parameters_done, loop;
    __ beqz(count, parameters_done);
    __ BIND(loop);
    __ ld8_inc(t3, src, wordSize);
    __ adds(Resp, -wordSize, Resp);
    __ Assembler::st8(Resp, t3);
    __ adds(count, -1, count);
    __ bnez(count, loop);
    __ BIND(parameters_done);

    // Call the Java entry: Rmethod = Method*, Rsender_sp = our sp, Resp =
    // the last argument pushed. The callee's prologue saves b0.
    BLOCK_COMMENT("call Java function");
    __ mov(Rsender_sp, sp);
    ld_at(t2, fp, entry_point_off);
    __ mov_to_br(btmp, t2);
    __ br_call(breturn, btmp);

    // save current address for use by exception handling code
    return_address = __ pc();

    // The callee leaves sp just below our pushed arguments (or anywhere
    // above sp_after_call minus them); put it back where the frame says.
    __ adds(sp, sp_after_call_off * wordSize, fp);

    // Store the result. Java returns integral values in r8 and floating
    // ones in f8. Write the int case unconditionally, then overwrite for the
    // wider or floating types: the result is a JavaValue, 8 bytes, so the
    // narrower write is always in bounds.
    BLOCK_COMMENT("store result");
    const Register result      = t2;
    const Register result_type = t4;
    ld_at(result, fp, result_off);
    ld_at(result_type, fp, result_type_off);
    __ st4(result, Rret);
    __ cmp4_eq_imm(ptmp0, ptmp1, T_OBJECT, result_type);
    __ st8(result, Rret, ptmp0);
    __ cmp4_eq_imm(ptmp0, ptmp1, T_LONG, result_type);
    __ st8(result, Rret, ptmp0);
    __ cmp4_eq_imm(ptmp0, ptmp1, T_FLOAT, result_type);
    __ stfs(result, f8, ptmp0);
    __ cmp4_eq_imm(ptmp0, ptmp1, T_DOUBLE, result_type);
    __ stfd(result, f8, ptmp0);

#ifdef ASSERT
    // verify that threads correspond
    {
      Label L;
      ld_at(t2, fp, thread_off);
      __ beq(Rthread, t2, L);
      __ stop("StubRoutines::call_stub: threads must correspond");
      __ BIND(L);
    }
#endif

    // Restore the C caller's state. Every load completes before sp moves
    // back up (no red zone), and r4 -- our fp -- is filled last, through an
    // address computed while fp was still valid.
    BLOCK_COMMENT("restore callee-saved state");
    for (int i = 0; i < n_saved_fprs; i++) {
      __ adds(t3, fpr_off(i) * wordSize, fp);
      __ ldf_fill(saved_fprs[i], t3);
    }

    ld_at(t4, fp, spill_unat_off);
    __ mov_to_ar(ia64::kArUnat, t4);

    ld_at(t0, fp, retaddr_off);
    ld_at(t1, fp, pfs_off);
    ld_at(gp, fp, gp_off);
    ld_at(t4, fp, caller_unat_off);
    __ mov(t2, fp);                                     // the C caller's sp
    __ adds(t3, r5_off   * wordSize, t2);  __ ld8_fill(r5, t3);
    __ adds(t3, r6_off   * wordSize, t2);  __ ld8_fill(r6, t3);
    __ adds(t3, r7_off   * wordSize, t2);  __ ld8_fill(r7, t3);
    __ adds(t3, link_off * wordSize, t2);  __ ld8_fill(r4, t3);
    __ mov_to_ar(ia64::kArUnat, t4);

    // ar.pfs must be back before br.ret: br.ret restores CFM from it, which
    // is what unwinds our register frame back to the C caller's.
    __ mov_to_pfs(t1);
    __ mov_to_br(breturn, t0);
    __ mov(sp, t2);
    __ ret();

    return start;
  }

  // Return point for a Java call if there's an exception thrown in
  // Java code.  The exception is caught and transformed into a
  // pending exception stored in JavaThread that can be tested from
  // within the VM.
  //
  // Note: Usually the parameters are removed by the callee. In case
  // of an exception crossing an activation frame boundary, that is
  // not the case if the callee is compiled code => need to setup the
  // sp. call_stub's return path resets sp from fp, so that is covered.
  //
  // Rexception (r8): exception oop; fp: call_stub's frame
  address generate_catch_exception() {
    StubGenStubId stub_id = StubGenStubId::catch_exception_id;
    StubCodeMark mark(this, stub_id);
    address start = __ pc();

#ifdef ASSERT
    // verify that threads correspond
    {
      Label L;
      ld_at(t2, fp, thread_off);
      __ beq(Rthread, t2, L);
      __ stop("StubRoutines::catch_exception: threads must correspond");
      __ bind(L);
    }
#endif

    // set pending exception
    __ verify_oop(Rexception);

    __ st8(Address(Rthread, Thread::pending_exception_offset()), Rexception);
    __ movl(t2, (address)__FILE__);
    __ st8(Address(Rthread, Thread::exception_file_offset()), t2);
    __ mov_immediate(t2, (int)__LINE__);
    __ st4(Address(Rthread, Thread::exception_line_offset()), t2);

    // complete return to VM
    assert(StubRoutines::_call_stub_return_address != nullptr,
           "_call_stub_return_address must have been generated before");
    __ far_jump(StubRoutines::_call_stub_return_address);

    return start;
  }

  // Continuation point for runtime calls returning with a pending
  // exception.  The pending exception check happened in the runtime
  // or native call stub.  The pending exception in Thread is
  // converted into a Java-level exception.
  //
  // Contract with Java-level exception handlers:
  //   Rexception    (r8):  exception
  //   Rexception_pc (r28): throwing pc
  //
  // NOTE: At entry of this stub, the exception pc must be in b0 -- the return
  // address of the VM call that left the exception pending. Every caller
  // reaches here by a jump (MacroAssembler::check_pending_exception), which
  // leaves b0 as that call set it.
  address generate_forward_exception() {
    StubGenStubId stub_id = StubGenStubId::forward_exception_id;
    StubCodeMark mark(this, stub_id);
    address start = __ pc();

#ifdef ASSERT
    // make sure this code is only executed if there is a pending exception
    {
      Label L;
      __ ld8(t2, Address(Rthread, Thread::pending_exception_offset()));
      __ bnez(t2, L);
      __ stop("StubRoutines::forward exception: no pending exception (1)");
      __ bind(L);
    }
#endif

    // Call the VM to find the handler for the caller's pc. b0 does not
    // survive the call, and no callee-saved register is free to hold it
    // (r4-r7 are all spoken for), so it is kept in a small frame of our own:
    // 16 bytes of psABI scratch for the callee plus one slot, rounded up.
    __ mov_from_br(t2, breturn);
    __ adds(sp, -32, sp);
    __ adds(t3, 16, sp);
    __ Assembler::st8(t3, t2);
    __ mov(c_rarg1, t2);
    BLOCK_COMMENT("call exception_handler_for_return_address");
    __ call_VM_leaf(CAST_FROM_FN_PTR(address,
                         SharedRuntime::exception_handler_for_return_address),
                    Rthread, c_rarg1);
    // r8: the handler address
    __ adds(t3, 16, sp);
    __ Assembler::ld8(Rexception_pc, t3);
    __ adds(sp, 32, sp);
    // Put b0 back as the throwing pc too, as riscv restores ra, for the
    // benefit of handlers that look at it.
    __ mov_to_br(breturn, Rexception_pc);

    // setup Rexception, Rexception_pc & clear pending exception
    __ mov(t2, Rret);                                   // handler
    __ ld8(Rexception, Address(Rthread, Thread::pending_exception_offset()));
    __ st8(Address(Rthread, Thread::pending_exception_offset()), zr);

#ifdef ASSERT
    // make sure exception is set
    {
      Label L;
      __ bnez(Rexception, L);
      __ stop("StubRoutines::forward exception: no pending exception (2)");
      __ bind(L);
    }
#endif

    // continue at exception handler
    __ verify_oop(Rexception);
    __ jr(t2);

    return start;
  }

  void generate_initial_stubs() {
    // Generate initial stubs and initializes the entry points

    // forward_exception first: call_VM's pending-exception check jumps to it,
    // and asserts it exists at generation time.
    StubRoutines::_forward_exception_entry = generate_forward_exception();

    StubRoutines::_call_stub_entry =
      generate_call_stub(StubRoutines::_call_stub_return_address);

    // is referenced by megamorphic call
    StubRoutines::_catch_exception_entry = generate_catch_exception();
  }

  void generate_continuation_stubs() {
    // VMContinuations is false on IA-64 (globals_ia64.hpp): nothing to do.
  }

  // The slow path of the nmethod entry barrier (BarrierSetAssembler::
  // nmethod_entry_barrier), called from a compiled method's prologue with
  // its frame built and its arguments live. Asks the GC whether the method
  // may be entered; if not, discards the method's frame and re-dispatches the
  // call through handle_wrong_method, as though the caller had called that.
  //
  // Frame, in words from fp (enter()'s linkage, then the slots
  // BarrierSetNMethod::deoptimize fills, then the register save area):
  //   -1  return address into the nmethod     <- return_address_ptr
  //   -2  the nmethod's fp
  //   -3  new pc (handle_wrong_method)        return_address_ptr[-2]
  //   -4  new return address                  return_address_ptr[-3]
  //   -5  new fp                              return_address_ptr[-4]
  //   -6  new sp                              return_address_ptr[-5]
  //   -7, -8  padding
  //   below: RegisterSaver's area
  address generate_method_entry_barrier() {
    __ align(CodeEntryAlignment);
    StubGenStubId stub_id = StubGenStubId::method_entry_barrier_id;
    StubCodeMark mark(this, stub_id);
    address start = __ pc();

    Label deoptimize;

    // The last Java frame is the nmethod's own: its sp and fp, and the
    // return address into it.
    __ mov_from_br(t2, breturn);
    __ st8(Address(Rthread, JavaThread::frame_anchor_offset() + JavaFrameAnchor::last_Java_pc_offset()), t2, t1);
    __ st8(Address(Rthread, JavaThread::last_Java_fp_offset()), fp, t1);
    __ lea(t1, Address(Rthread, JavaThread::last_Java_sp_offset()));
    __ st8_rel(t1, sp);

    __ enter();
    __ adds(sp, -4 * wordSize, sp);
    int frame_words;
    RegisterSaver::save_live_registers(_masm, &frame_words, /* describe_fprs */ false, /* with_enter */ false);

    __ adds(t2, frame::return_addr_offset * wordSize, fp);
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, BarrierSetNMethod::nmethod_stub_entry_barrier), t2);
    __ mov(t2, r8);

    __ reset_last_Java_frame(true);
    RegisterSaver::restore_live_registers(_masm, /* with_leave */ false, /* keep */ t2);
    __ bnezw(t2, deoptimize);

    __ leave();
    __ ret();

    __ bind(deoptimize);
    __ ld8(t2, Address(fp, -6 * wordSize));    // sp
    __ ld8(t3, Address(fp, -5 * wordSize));    // fp
    __ ld8(t4, Address(fp, -4 * wordSize));    // return address
    __ ld8(t1, Address(fp, -3 * wordSize));    // handle_wrong_method
    __ mov(sp, t2);
    __ mov(fp, t3);
    __ mov_to_br(breturn, t4);
    __ jr(t1);

    return start;
  }

  // ---- arraycopy ------------------------------------------------------------
  //
  // C1 calls these through StubRoutines::select_arraycopy_function, and the
  // C++ defaults they replace cost a fixed ~45 ns (int) to ~100 ns (oop) per
  // call on rx2800 whatever the length: most of a short copy. A stub copies
  // up to arraycopy_inline_elements itself, element by element (each element
  // one load and one store, so element atomicity holds), and tail-branches
  // to the C++ default for anything longer, which keeps its bulk throughput.
  //
  // Called like a C function: a descriptor (function_entry) first, so C++
  // can call it through a function pointer too; arguments src, dst, count
  // (elements) in out0-out2. A leaf with no alloc (FRAME-DESIGN.md 1): after
  // br.call the caller's outputs are this frame's r32-r34, br.ret restores
  // the caller's frame, and the tail branch leaves b0 and ar.pfs exactly as
  // the call set them, so the C++ function returns straight to our caller.
  // Clobbers only C-scratch registers (r14-r21, t0-t2, gp, p6/p7, b6).
  //
  // Where the inline loop stops paying, in elements. Measured (release, C1,
  // rx2800, 2026-10-09): the stub's own fixed cost is ~28 ns and its loop
  // ~0.6 ns per int, long or oop, against the C++ copies' ~70 ns fixed (~130
  // ns for oops, which go through the Access API) plus 0.03 (byte) to 0.29
  // (long, oop) ns per element. Byte and short stores are as cheap only for
  // the first ~16; beyond that they cost 1.5 (short) to 2.8 (byte) ns each
  // (sub-word stores drain slowly once the store path fills; unverified why),
  // so those hand over to memmove early.
  static int arraycopy_inline_elements(int shift, bool is_oop) {
    if (is_oop) return 256;
    switch (shift) {
      case 0:
      case 1:  return 16;
      case 2:  return 80;
      default: return 128;
    }
  }

  void load_inc(int shift, Register val, Register src, int step, PredicateRegister qp = pTrue) {
    switch (shift) {
      case 0: __ ld1_inc(val, src, step, qp); break;
      case 1: __ ld2_inc(val, src, step, qp); break;
      case 2: __ ld4_inc(val, src, step, qp); break;
      case 3: __ ld8_inc(val, src, step, qp); break;
      default: ShouldNotReachHere();
    }
  }

  void store_inc(int shift, Register dst, Register val, int step, PredicateRegister qp = pTrue) {
    switch (shift) {
      case 0: __ st1_inc(dst, val, step, qp); break;
      case 1: __ st2_inc(dst, val, step, qp); break;
      case 2: __ st4_inc(dst, val, step, qp); break;
      case 3: __ st8_inc(dst, val, step, qp); break;
      default: ShouldNotReachHere();
    }
  }

  // Copies count (>= 1) elements from src to dst, stepping by step (+-es),
  // starting with the elements src and dst point at. An odd element first,
  // then two per iteration through two pointer pairs, so both loads issue in
  // one instruction group and both stores in the next. Clobbers src, dst,
  // count, r14, r15, r19, r20, p6/p7.
  void copy_elements(int shift, Register src, Register dst, Register count, int step, Label& done) {
    const Register v0 = r14, v1 = r15, src1 = r19, dst1 = r20;
    Label loop;
    __ tbit_nz(ptmp0, ptmp1, count, 0);
    load_inc(shift, v0, src, step, ptmp0);
    store_inc(shift, dst, v0, step, ptmp0);
    __ shru_imm(count, count, 1);
    __ beqz(count, done);
    __ adds(src1, step, src);
    __ adds(dst1, step, dst);
    __ bind(loop);
    load_inc(shift, v0, src, 2 * step);
    load_inc(shift, v1, src1, 2 * step);
    __ adds(count, -1, count);
    store_inc(shift, dst, v0, 2 * step);
    store_inc(shift, dst1, v1, 2 * step);
    __ bnez(count, loop);
  }

  address generate_copy(StubGenStubId stub_id, int shift, bool is_oop, bool disjoint, address cpp_copy) {
    StubCodeMark mark(this, stub_id);
    address start = __ function_entry();
    Assembler::PackScope pack(_masm);

    const Register src = c_rarg0, dst = c_rarg1, count = c_rarg2;
    const Register oop_dst = r16, oop_count = r17, tmp = r18, bytes = r21;
    const int es = 1 << shift;
    DecoratorSet decorators = IN_HEAP | IS_ARRAY | (disjoint ? ARRAYCOPY_DISJOINT : 0);
    BarrierSetAssembler* bs = BarrierSet::barrier_set()->barrier_set_assembler();
    Label done, call_cpp, backward, copied;

    __ beqz(count, done);
    __ mov_immediate(t2, arraycopy_inline_elements(shift, is_oop));
    __ bltu(t2, count, call_cpp);

    if (is_oop) {
      __ mov(oop_dst, dst);
      __ mov(oop_count, count);
      bs->arraycopy_prologue(_masm, decorators, true, src, dst, count, RegSet());
    }
    if (!disjoint) {
      // Copy backwards iff dst starts inside [src, src + bytes): as an
      // unsigned difference, 0 <= dst - src < bytes.
      __ shl_imm(bytes, count, shift);
      __ sub(t2, dst, src);
      __ bltu(t2, bytes, backward);
    }
    copy_elements(shift, src, dst, count, es, copied);
    if (!disjoint) {
      __ br(copied);
      __ bind(backward);
      __ add(src, src, bytes);
      __ adds(src, -es, src);
      __ add(dst, dst, bytes);
      __ adds(dst, -es, dst);
      copy_elements(shift, src, dst, count, -es, copied);
    }
    __ bind(copied);
    if (is_oop) {
      bs->arraycopy_epilogue(_masm, decorators, true, oop_dst, oop_count, tmp, RegSet());
    }
    __ bind(done);
    __ ret();

    __ bind(call_cpp);
    const address* fd = (const address*)cpp_copy;
    __ movl(t0, (uint64_t)fd[0]);
    __ movl(gp, (uint64_t)fd[1]);
    __ jr(t0);

    return start;
  }

  void generate_arraycopy_stubs() {
    assert(!UseCompressedOops, "IA-64: compressed oops are off (FRAME-DESIGN.md 2.4)");
    const int oop_shift = LogBytesPerHeapOop;
#define COPY_STUBS(type, shift, is_oop, cpp)                                               \
    StubRoutines::_##type##_arraycopy =                                                     \
      generate_copy(StubGenStubId::type##_arraycopy_id, shift, is_oop, false,               \
                    CAST_FROM_FN_PTR(address, StubRoutines::cpp));                          \
    StubRoutines::_##type##_disjoint_arraycopy =                                            \
      generate_copy(StubGenStubId::type##_disjoint_arraycopy_id, shift, is_oop, true,       \
                    CAST_FROM_FN_PTR(address, StubRoutines::cpp));                          \
    /* The aligned variants share the inline loop but fall back to the */                   \
    /* arrayof_ C++ copies, which move whole words: ~3x faster when long. */                \
    StubRoutines::_arrayof_##type##_arraycopy =                                             \
      generate_copy(StubGenStubId::arrayof_##type##_arraycopy_id, shift, is_oop, false,     \
                    CAST_FROM_FN_PTR(address, StubRoutines::arrayof_##cpp));                \
    StubRoutines::_arrayof_##type##_disjoint_arraycopy =                                    \
      generate_copy(StubGenStubId::arrayof_##type##_disjoint_arraycopy_id, shift, is_oop,   \
                    true, CAST_FROM_FN_PTR(address, StubRoutines::arrayof_##cpp));
    COPY_STUBS(jbyte,  0,         false, jbyte_copy)
    COPY_STUBS(jshort, 1,         false, jshort_copy)
    COPY_STUBS(jint,   2,         false, jint_copy)
    COPY_STUBS(jlong,  3,         false, jlong_copy)
    COPY_STUBS(oop,    oop_shift, true,  oop_copy)
#undef COPY_STUBS
    // The dest_uninitialized variants keep their C++ defaults: C1 never
    // selects them.
  }

  void generate_final_stubs() {
    if (BarrierSet::barrier_set()->barrier_set_nmethod() != nullptr) {
      StubRoutines::_method_entry_barrier = generate_method_entry_barrier();
    }
    generate_arraycopy_stubs();
    StubRoutines::ia64::set_completed();
  }

  void generate_compiler_stubs() {
    // The core variant has no compiler.
  }

 public:
  StubGenerator(CodeBuffer* code, StubGenBlobId blob_id) : StubCodeGenerator(code, blob_id) {
    switch(blob_id) {
    case initial_id:
      generate_initial_stubs();
      break;
     case continuation_id:
      generate_continuation_stubs();
      break;
    case compiler_id:
      generate_compiler_stubs();
      break;
    case final_id:
      generate_final_stubs();
      break;
    default:
      fatal("unexpected blob id: %d", blob_id);
      break;
    };
  }
}; // end class declaration

void StubGenerator_generate(CodeBuffer* code, StubGenBlobId blob_id) {
  StubGenerator g(code, blob_id);
}
