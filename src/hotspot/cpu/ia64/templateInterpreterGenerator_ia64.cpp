/*
 * Copyright (c) 2003, 2025, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, 2020, Red Hat Inc. All rights reserved.
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

#include "asm/macroAssembler.inline.hpp"
#include "classfile/javaClasses.hpp"
#include "compiler/disassembler.hpp"
#include "gc/shared/barrierSetAssembler.hpp"
#include "interpreter/bytecodeHistogram.hpp"
#include "interpreter/bytecodeTracer.hpp"
#include "interpreter/interp_masm.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "interpreter/templateInterpreterGenerator.hpp"
#include "interpreter/templateTable.hpp"
#include "memory/resourceArea.hpp"
#include "oops/arrayOop.hpp"
#include "oops/method.inline.hpp"
#include "oops/methodData.hpp"
#include "oops/oop.inline.hpp"
#include "oops/resolvedIndyEntry.hpp"
#include "oops/resolvedMethodEntry.hpp"
#include "prims/jvmtiExport.hpp"
#include "prims/jvmtiThreadState.hpp"
#include "runtime/arguments.hpp"
#include "runtime/deoptimization.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/jniHandles.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/synchronizer.hpp"
#include "runtime/timer.hpp"
#include "runtime/vframeArray.hpp"
#include "utilities/checkedCast.hpp"
#include "utilities/debug.hpp"
#include "utilities/powerOfTwo.hpp"
#include <sys/types.h>

// Derived from cpu/riscv/templateInterpreterGenerator_riscv.cpp. Register
// mapping as in interp_masm_ia64.cpp: x10/f10 -> Rtos/Ftos (r8/f8),
// x19_sender_sp -> Rsender_sp, ra -> b0, riscv's x11-x15/x28 scratch ->
// r20-r27. The IA-64 departures, each marked "IA-64:" where it happens:
//
// * The return address arrives in b0 and is stored by generate_fixed_frame.
// * 16 bytes of psABI scratch are kept free below every expression stack and
//   outgoing-argument area, since any C callee may write [sp, sp+16).
// * The native entry resolves the native function *before* running the
//   signature handler: the handler leaves the C arguments in out0-out7 and
//   f8-f15, and a VM call in between would clobber them.
// * Native functions are called through their descriptors (call_c); result
//   handlers and signature handlers are generated code and are called as
//   code.
// * Intrinsic entries (math, CRC32, Reference.get, currentThread, float16)
//   are not generated: the shared generator then uses the normal or native
//   entry, which is correct, if slower.

#define __ Disassembler::hook<InterpreterMacroAssembler>(__FILE__, __LINE__, _masm)->

// Size of interpreter code. riscv uses 256K; every IA-64 instruction is a
// 16-byte bundle in this port and most memory accesses need an extra adds,
// so allow about five times that. Increase if too small: the generator
// reports the shortfall at startup.
int TemplateInterpreter::InterpreterCodeSize = 1536 * K;

// Interpreter-internal scratch registers (j_rarg0-7 are free here).
static constexpr Register Rconst_method = r21;
static constexpr Register Rconstants    = r22;
static constexpr Register Rsize_params  = r23;
static constexpr Register Rextra_locals = r24;
static constexpr Register Rtmp1         = r25;
static constexpr Register Rtmp2         = r26;
static constexpr Register Rtmp3         = r27;

// The ArrayIndexOutOfBounds handler's register convention, shared with
// TemplateTable::index_check (riscv: x11 / x13).
static constexpr Register Raioobe_index = r21;
static constexpr Register Raioobe_array = r22;

// IA-64: words kept free below an expression stack or argument area for the
// psABI scratch area of any C callee.
static const int psabi_scratch_words = 2;

//-----------------------------------------------------------------------------

// Large signatures are handed to InterpreterRuntime::slow_signature_handler
// on other ports. Milestone 1 generates handlers for every signature
// (SignatureHandlerGenerator); this path traps if it is ever taken.
address TemplateInterpreterGenerator::generate_slow_signature_handler() {
  address entry = __ pc();
  __ unimplemented("IA-64: slow signature handler");
  return entry;
}

// Intrinsics: none yet. A null entry makes the shared generator fall back to
// the normal (or native) entry.
address TemplateInterpreterGenerator::generate_math_entry(AbstractInterpreter::MethodKind kind) {
  return nullptr;
}

address TemplateInterpreterGenerator::generate_Reference_get_entry(void) { return nullptr; }
address TemplateInterpreterGenerator::generate_CRC32_update_entry() { return nullptr; }
address TemplateInterpreterGenerator::generate_CRC32_updateBytes_entry(AbstractInterpreter::MethodKind kind) { return nullptr; }
address TemplateInterpreterGenerator::generate_CRC32C_updateBytes_entry(AbstractInterpreter::MethodKind kind) { return nullptr; }
address TemplateInterpreterGenerator::generate_Float_float16ToFloat_entry() { return nullptr; }
address TemplateInterpreterGenerator::generate_Float_floatToFloat16_entry() { return nullptr; }
address TemplateInterpreterGenerator::generate_currentThread() { return nullptr; }

// Abstract method entry
// Attempt to execute abstract method. Throw exception
address TemplateInterpreterGenerator::generate_abstract_entry(void) {
  // Rmethod: Method*
  // Rsender_sp: sender SP

  address entry_point = __ pc();

  // abstract method entry

  //  pop return address, reset last_sp to null
  __ empty_expression_stack();
  __ restore_bcp();      // bcp must be correct for exception handler   (was destroyed)
  __ restore_locals();   // make sure locals pointer is correct as well (was destroyed)

  // throw exception
  __ call_VM(noreg, CAST_FROM_FN_PTR(address,
                                     InterpreterRuntime::throw_AbstractMethodErrorWithMethod),
                                     Rmethod);
  // the call_VM checks for exception, so we should never return here.
  __ should_not_reach_here();

  return entry_point;
}

address TemplateInterpreterGenerator::generate_StackOverflowError_handler() {
  address entry = __ pc();

#ifdef ASSERT
  {
    Label L;
    __ ld8(t2, Address(fp, frame::interpreter_frame_monitor_block_top_offset * wordSize));
    __ shladd(t2, t2, LogBytesPerWord, fp);
    // maximal sp for current fp (stack grows negative)
    // check if frame is complete
    __ bgeu(t2, sp, L);
    __ stop ("interpreter frame not set up");
    __ bind(L);
  }
#endif // ASSERT
  // Restore bcp under the assumption that the current frame is still
  // interpreted
  __ restore_bcp();

  // expression stack must be empty before entering the VM if an
  // exception happened
  __ empty_expression_stack();
  // throw exception
  __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_StackOverflowError));
  return entry;
}

address TemplateInterpreterGenerator::generate_ArrayIndexOutOfBounds_handler() {
  address entry = __ pc();
  // expression stack must be empty before entering the VM if an
  // exception happened
  __ empty_expression_stack();
  // setup parameters

  // convention: expect aberrant index in register Raioobe_index
  __ zxt4(c_rarg2, Raioobe_index);
  // convention: expect array in register Raioobe_array
  __ mov(c_rarg1, Raioobe_array);
  __ call_VM(noreg,
             CAST_FROM_FN_PTR(address,
                              InterpreterRuntime::
                              throw_ArrayIndexOutOfBoundsException),
             c_rarg1, c_rarg2);
  return entry;
}

address TemplateInterpreterGenerator::generate_ClassCastException_handler() {
  address entry = __ pc();

  // object is at TOS
  __ pop_ptr(c_rarg1);

  // expression stack must be empty before entering the VM if an
  // exception happened
  __ empty_expression_stack();

  __ call_VM(noreg,
             CAST_FROM_FN_PTR(address,
                              InterpreterRuntime::
                              throw_ClassCastException),
             c_rarg1);
  return entry;
}

address TemplateInterpreterGenerator::generate_exception_handler_common(
  const char* name, const char* message, bool pass_oop) {
  assert(!pass_oop || message == nullptr, "either oop or message but not both");
  address entry = __ pc();
  if (pass_oop) {
    // object is at TOS
    __ pop_ptr(c_rarg2);
  }
  // expression stack must be empty before entering the VM if an
  // exception happened
  __ empty_expression_stack();
  // setup parameters
  __ movl(c_rarg1, (address)name);
  if (pass_oop) {
    __ call_VM(Rexception, CAST_FROM_FN_PTR(address,
                                            InterpreterRuntime::
                                            create_klass_exception),
               c_rarg1, c_rarg2);
  } else {
    if (message != nullptr) {
      __ movl(c_rarg2, (address)message);
    } else {
      __ mov(c_rarg2, zr);
    }
    __ call_VM(Rexception,
               CAST_FROM_FN_PTR(address, InterpreterRuntime::create_exception),
               c_rarg1, c_rarg2);
  }
  // throw exception
  __ far_jump(Interpreter::throw_exception_entry());
  return entry;
}

address TemplateInterpreterGenerator::generate_return_entry_for(TosState state, int step, size_t index_size) {
  address entry = __ pc();

  // The callee's result is in Rtos/Ftos: nothing below may touch r8 or f8.

  // Restore stack bottom in case i2c adjusted stack
  __ ld8(t2, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));
  __ shladd(Resp, t2, LogBytesPerWord, fp);
  // and null it as marker that esp is now tos until next java call
  __ st8(Address(fp, frame::interpreter_frame_last_sp_offset * wordSize), zr);
  __ restore_bcp();
  __ restore_locals();
  __ restore_constant_pool_cache();
  __ get_method(Rmethod);

  const Register cache = Rtmp1;
  const Register index = Rtmp2;

  if (index_size == sizeof(u4)) {
    __ load_resolved_indy_entry(cache, index);
    __ ld2(cache, Address(cache, in_bytes(ResolvedIndyEntry::num_parameters_offset())));
    __ shladd(Resp, cache, LogBytesPerWord, Resp);
  } else {
    // Pop N words from the stack
    assert(index_size == sizeof(u2), "Can only be u2");
    __ load_method_entry(cache, index);
    __ ld2(cache, Address(cache, in_bytes(ResolvedMethodEntry::num_parameters_offset())));
    __ shladd(Resp, cache, LogBytesPerWord, Resp);
  }

  // Restore machine SP
  __ restore_sp_after_call();

  __ check_and_handle_popframe(Rthread);
  __ check_and_handle_earlyret(Rthread);

  __ dispatch_next(state, step);

  return entry;
}

address TemplateInterpreterGenerator::generate_deopt_entry_for(TosState state,
                                                               int step,
                                                               address continuation) {
  address entry = __ pc();
  __ restore_bcp();
  __ restore_locals();
  __ restore_constant_pool_cache();
  __ get_method(Rmethod);

  __ restore_sp_after_call();  // Restore SP to extended SP

  // Restore expression stack pointer
  __ ld8(t2, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));
  __ shladd(Resp, t2, LogBytesPerWord, fp);
  // null last_sp until next java call
  __ st8(Address(fp, frame::interpreter_frame_last_sp_offset * wordSize), zr);

  // handle exceptions
  {
    Label L;
    __ ld8(t2, Address(Rthread, Thread::pending_exception_offset()));
    __ beqz(t2, L);
    __ call_VM(noreg,
               CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_pending_exception));
    __ should_not_reach_here();
    __ bind(L);
  }

  if (continuation == nullptr) {
    __ dispatch_next(state, step);
  } else {
    __ jump_to_entry(continuation);
  }
  return entry;
}

// Called (br.call) from the native entry with the C result in r8 / f8;
// returns with it converted to the Java type.
address TemplateInterpreterGenerator::generate_result_handler_for(BasicType type) {
  address entry = __ pc();
  switch (type) {
    case T_OBJECT:
      // retrieve result from frame
      __ ld8(Rret, Address(fp, frame::interpreter_frame_oop_temp_offset * wordSize));
      // and verify it
      __ verify_oop(Rret);
      break;
    case T_BOOLEAN:
      // C's jboolean is one byte; any non-zero low byte is true
      __ zxt1(Rret, Rret);
      __ cmp_eq(ptmp0, ptmp1, Rret, zr);
      __ mov(Rret, 1L);
      __ mov(Rret, zr, ptmp0);
      break;
    case T_CHAR   : __ zxt2(Rret, Rret); break;
    case T_BYTE   : __ sxt1(Rret, Rret); break;
    case T_SHORT  : __ sxt2(Rret, Rret); break;
    case T_INT    : __ sxt4(Rret, Rret); break;
    case T_LONG   : /* nothing to do */ break;
    case T_VOID   : /* nothing to do */ break;
    case T_FLOAT  : /* nothing to do: in f8 */ break;
    case T_DOUBLE : /* nothing to do: in f8 */ break;
    default       : ShouldNotReachHere();
  }
  __ ret();                                  // return from result handler
  return entry;
}

address TemplateInterpreterGenerator::generate_safept_entry_for(TosState state,
                                                                address runtime_entry) {
  assert(runtime_entry != nullptr, "runtime entry must exist");
  address entry = __ pc();
  __ push(state);
  __ call_VM(noreg, runtime_entry);
  __ membar(MacroAssembler::AnyAny);
  __ dispatch_via(vtos, Interpreter::_normal_table.table_for(vtos));
  return entry;
}

// VMContinuations is false on IA-64.
address TemplateInterpreterGenerator::generate_cont_resume_interpreter_adapter() {
  return nullptr;
}


// Helpers for commoning out cases in the various type of method entries.
//


// increment invocation count & check for overflow
//
// Note: checking for negative value instead of overflow
//       so we have a 'sticky' overflow test
//
// Rmethod: method
//
void TemplateInterpreterGenerator::generate_counter_incr(Label* overflow) {
  Label done;
  int increment = InvocationCounter::count_increment;
  guarantee(!ProfileInterpreter, "IA-64: interpreter profiling arrives with C1");
  // Increment counter in MethodCounters
  const Register mcs = Rtmp1;
  const Address invocation_counter(mcs,
                                   MethodCounters::invocation_counter_offset() +
                                   InvocationCounter::counter_offset());
  __ get_method_counters(Rmethod, mcs, done);
  const Address mask(mcs, in_bytes(MethodCounters::invoke_mask_offset()));
  __ increment_mask_and_jump(invocation_counter, increment, mask, Rtmp2, Rtmp3, false, overflow);
  __ bind(done);
}

void TemplateInterpreterGenerator::generate_counter_overflow(Label& do_continue) {
  __ mov(c_rarg1, zr);
  __ call_VM(noreg,
             CAST_FROM_FN_PTR(address, InterpreterRuntime::frequency_counter_overflow), c_rarg1);
  __ j(do_continue);
}

// See if we've got enough room on the stack for locals plus overhead
// below JavaThread::stack_overflow_limit(). If not, throw a StackOverflowError
// without going through the signal handler, i.e., reserved and yellow zones
// will not be made usable. The shadow zone must suffice to handle the
// overflow.
// The expression stack grows down incrementally, so the normal guard
// page mechanism will work for that.
//
// NOTE: Since the additional locals are also always pushed (wasn't
// obvious in generate_method_entry) so the guard should work for them
// too.
//
// Args:
//      Rextra_locals: number of additional locals this frame needs (what we must check)
//      Rmethod: Method*
//
// Kills:
//      Rtmp1
void TemplateInterpreterGenerator::generate_stack_overflow_check(void) {

  // monitor entry size: see picture of stack set
  // (generate_method_entry) and frame_ia64.hpp
  const int entry_size = frame::interpreter_frame_monitor_size_in_bytes();

  // total overhead size: entry_size + (saved fp through expr stack
  // bottom).  be sure to change this if you add/subtract anything
  // to/from the overhead area
  const int overhead_size =
    -(frame::interpreter_frame_initial_sp_offset * wordSize) + entry_size;

  const int page_size = (int)os::vm_page_size();

  Label after_frame_check;

  // see if the frame is greater than one page in size. If so,
  // then we need to verify there is enough stack space remaining
  // for the additional locals.
  __ mov_immediate(t2, (page_size - overhead_size) / Interpreter::stackElementSize);
  __ bleu(Rextra_locals, t2, after_frame_check);

  // compute sp as if this were going to be the last frame on
  // the stack before the red zone

  // locals + overhead, in bytes
  __ mov_immediate(Rtmp1, overhead_size);
  __ shladd(Rtmp1, Rextra_locals, Interpreter::logStackElementSize, Rtmp1);

  const Address stack_limit(Rthread, JavaThread::stack_overflow_limit_offset());
  __ ld8(t2, stack_limit);

#ifdef ASSERT
  Label limit_okay;
  // Verify that thread stack limit is non-zero.
  __ bnez(t2, limit_okay);
  __ stop("stack overflow limit is zero");
  __ bind(limit_okay);
#endif

  // Add stack limit to locals.
  __ add(Rtmp1, Rtmp1, t2);

  // Check against the current stack bottom.
  __ bgtu(sp, Rtmp1, after_frame_check);

  // Remove the incoming args, peeling the machine SP back to where it
  // was in the caller.
  __ and_imm(sp, -16, Rsender_sp);

  // Note: the restored frame is not necessarily interpreted.
  // Use the shared runtime version of the StackOverflowError.
  assert(SharedRuntime::throw_StackOverflowError_entry() != nullptr, "stub not yet generated");
  __ far_jump(SharedRuntime::throw_StackOverflowError_entry());

  // all done with frame size check
  __ bind(after_frame_check);
}

// Allocate monitor and lock method (asm interpreter)
//
// Args:
//      Rmethod: Method*
//      Rlocals: locals
//
// Kills:
//      Rtos
//      c_rarg0, c_rarg1, c_rarg2, c_rarg3, ...(param regs)
//      t0, t1 (temporary regs)
void TemplateInterpreterGenerator::lock_method() {
  // synchronize method
  const Address access_flags(Rmethod, Method::access_flags_offset());
  const Address monitor_block_top(fp, frame::interpreter_frame_monitor_block_top_offset * wordSize);
  const int entry_size = frame::interpreter_frame_monitor_size_in_bytes();

#ifdef ASSERT
  __ ld4(Rtos, access_flags);
  __ verify_access_flags(Rtos, JVM_ACC_SYNCHRONIZED, "method doesn't need synchronization", false);
#endif // ASSERT

  // get synchronization object
  {
    Label done;
    __ ld4(Rtmp1, access_flags);
    // get receiver (assume this is frequent case)
    __ ld8(Rtos, Address(Rlocals, Interpreter::local_offset_in_bytes(0)));
    __ tbit_z(ptmp0, ptmp1, Rtmp1, exact_log2(JVM_ACC_STATIC));
    __ br_cond(done, ptmp0);
    __ load_mirror(Rtos, Rmethod, Rtmp2, Rtmp3);

#ifdef ASSERT
    {
      Label L;
      __ bnez(Rtos, L);
      __ stop("synchronization object is null");
      __ bind(L);
    }
#endif // ASSERT

    __ bind(done);
  }

  // add space for monitor & lock
  __ check_extended_sp();
  __ adds(sp, -entry_size, sp); // add space for a monitor entry
  __ adds(Resp, -entry_size, Resp);
  __ sub(t2, sp, fp);
  __ shr_imm(t2, t2, Interpreter::logStackElementSize);
  __ st8(Address(fp, frame::interpreter_frame_extended_sp_offset * wordSize), t2);
  __ sub(t2, Resp, fp);
  __ shr_imm(t2, t2, Interpreter::logStackElementSize);
  __ st8(monitor_block_top, t2);  // set new monitor block top
  // store object
  __ st8(Address(Resp, BasicObjectLock::obj_offset()), Rtos);
  __ mov(c_rarg1, Resp); // object address
  __ lock_object(c_rarg1);
}

// Generate a fixed interpreter frame. This is identical setup for
// interpreted methods and for native methods hence the shared code.
//
// Args:
//      b0: return address
//      Rmethod: Method*
//      Rlocals: pointer to locals
//      Rsender_sp: sender's sp
//      sp: the frame's top (all locals allocated)
void TemplateInterpreterGenerator::generate_fixed_frame(bool native_call) {
  __ ld8(Rconst_method, Address(Rmethod, Method::const_offset()));

  // initialize fixed part of activation frame. sp moves down before
  // anything is stored into the new words (no red zone).
  if (native_call) {
    __ adds(Resp, -14 * wordSize, sp);
    __ mov(Rbcp, zr);
    __ adds(sp, -14 * wordSize, sp);
    // add 2 zero-initialized slots for native calls (oop temp, result handler)
    __ st8(Address(sp, 13 * wordSize), zr);
    __ st8(Address(sp, 12 * wordSize), zr);
  } else {
    __ adds(Resp, -12 * wordSize, sp);
    __ adds(Rbcp, in_bytes(ConstMethod::codes_offset()), Rconst_method); // get codebase
    __ adds(sp, -12 * wordSize, sp);
  }
  __ st8(Address(sp, wordSize), Rbcp);
  __ mov_immediate(t2, frame::interpreter_frame_initial_sp_offset);
  __ st8(Address(sp, 0), t2);

  __ st8(Address(sp, 7 * wordSize), Rmethod);
  __ st8(Address(sp, 6 * wordSize), zr);                      // mdp: no profiling

  // IA-64: the return address is in b0, not in memory.
  __ mov_from_br(t2, breturn);
  __ st8(Address(sp, 11 * wordSize), t2);
  __ st8(Address(sp, 10 * wordSize), fp);
  __ adds(fp, 12 * wordSize, sp); // include return address & fp

  __ ld8(Rconstants, Address(Rconst_method, ConstMethod::constants_offset()));
  __ ld8(Rcpool, Address(Rconstants, ConstantPool::cache_offset()));
  __ st8(Address(sp, 3 * wordSize), Rcpool);
  __ sub(t2, Rlocals, fp);
  __ shr_imm(t2, t2, Interpreter::logStackElementSize);   // t2 = Rlocals - fp();
  // Store relativized Rlocals, see frame::interpreter_frame_locals().
  __ st8(Address(sp, 2 * wordSize), t2);

  // set sender sp
  // leave last_sp as null
  __ st8(Address(sp, 9 * wordSize), Rsender_sp);
  __ st8(Address(sp, 8 * wordSize), zr);

  // Get mirror, Resolve ConstantPool* -> InstanceKlass* -> Java mirror
  // and store it in the frame as GC root for this Method*
  __ ld8(t2, Address(Rconstants, ConstantPool::pool_holder_offset()));
  __ ld8(t2, Address(t2, in_bytes(Klass::java_mirror_offset())));
  __ resolve_oop_handle(t2, Rtmp1, Rtmp2);
  __ st8(Address(sp, 4 * wordSize), t2);

  if (!native_call) {
    // IA-64: room for max_stack + the reserved extra slots, plus the psABI
    // scratch area below them.
    __ ld2(t2, Address(Rconst_method, ConstMethod::max_stack_offset()));
    __ adds(t2, MAX2(3, Method::extra_stack_entries()) + psabi_scratch_words, t2);
    __ shl_imm(t2, t2, LogBytesPerWord);
    __ sub(t2, sp, t2);
    __ and_imm(t2, -16, t2);
    __ sub(t3, t2, fp);
    __ shr_imm(t3, t3, Interpreter::logStackElementSize);
    // Store extended SP
    __ st8(Address(sp, 5 * wordSize), t3);
    // Move SP out of the way
    __ mov(sp, t2);
  } else {
    // Make sure there is room for the exception oop pushed in case method throws
    // an exception (see TemplateInterpreterGenerator::generate_throw_exception()),
    // IA-64: plus the psABI scratch area.
    __ adds(t2, -(2 + psabi_scratch_words) * wordSize, sp);
    __ sub(t3, t2, fp);
    __ shr_imm(t3, t3, Interpreter::logStackElementSize);
    __ st8(Address(sp, 5 * wordSize), t3);
    __ mov(sp, t2);
  }
}

// End of helpers

void TemplateInterpreterGenerator::bang_stack_shadow_pages(bool native_call) {
  // See more discussion in stackOverflow.hpp.

  const int shadow_zone_size = checked_cast<int>(StackOverflow::stack_shadow_zone_size());
  const int page_size = (int)os::vm_page_size();
  const int n_shadow_pages = shadow_zone_size / page_size;

#ifdef ASSERT
  Label L_good_limit;
  __ ld8(t2, Address(Rthread, JavaThread::shadow_zone_safe_limit()));
  __ bnez(t2, L_good_limit);
  __ stop("shadow zone safe limit is not initialized");
  __ bind(L_good_limit);

  Label L_good_watermark;
  __ ld8(t2, Address(Rthread, JavaThread::shadow_zone_growth_watermark()));
  __ bnez(t2, L_good_watermark);
  __ stop("shadow zone growth watermark is not initialized");
  __ bind(L_good_watermark);
#endif

  Label L_done;

  __ ld8(t2, Address(Rthread, JavaThread::shadow_zone_growth_watermark()));
  __ bgtu(sp, t2, L_done);

  // One bang per 16 KiB page: stepping by 4096, as some ports' figures
  // assume, would skip three of every four guard pages.
  for (int p = 1; p <= n_shadow_pages; p++) {
    __ bang_stack_with_offset(p * page_size);
  }

  // Record the new watermark, but only if the update is above the safe limit.
  // Otherwise, the next time around the check above would pass the safe limit.
  __ ld8(t2, Address(Rthread, JavaThread::shadow_zone_safe_limit()));
  __ bleu(sp, t2, L_done);
  __ st8(Address(Rthread, JavaThread::shadow_zone_growth_watermark()), sp);

  __ bind(L_done);
}

// Interpreter stub for calling a native method. (asm interpreter)
// This sets up a somewhat different looking stack for calling the
// native method than the typical interpreter frame setup.
address TemplateInterpreterGenerator::generate_native_entry(bool synchronized) {
  // determine code generation flags
  bool inc_counter = UseCompiler || CountCompiledCalls;

  // Rmethod: Method*
  // Rsender_sp: sender sp
  // b0: return address

  address entry_point = __ pc();

  const Address constMethod       (Rmethod, Method::const_offset());
  const Address access_flags      (Rmethod, Method::access_flags_offset());

  // get parameter size (always needed)
  __ ld8(Rconst_method, constMethod);
  __ ld2(Rsize_params, Address(Rconst_method, ConstMethod::size_of_parameters_offset()));

  // Native calls don't need the stack size check since they have no
  // expression stack and the arguments are already on the stack and
  // we only add a handful of words to the stack.

  // for natives the size of locals is zero

  // compute beginning of parameters (Rlocals)
  __ shladd(Rlocals, Rsize_params, LogBytesPerWord, Resp);
  __ adds(Rlocals, -wordSize, Rlocals);

  // Pull SP back to minimum size: this avoids holes in the stack
  __ and_imm(sp, -16, Resp);

  // initialize fixed part of activation frame
  generate_fixed_frame(true);

  // make sure method is native & not abstract
#ifdef ASSERT
  __ ld4(Rtos, access_flags);
  __ verify_access_flags(Rtos, JVM_ACC_NATIVE, "tried to execute non-native method as native", false);
  __ verify_access_flags(Rtos, JVM_ACC_ABSTRACT, "tried to execute abstract method in interpreter");
#endif

  // Since at this point in the method invocation the exception
  // handler would try to exit the monitor of synchronized methods
  // which hasn't been entered yet, we set the thread local variable
  // _do_not_unlock_if_synchronized to true. The remove_activation
  // will check this flag.

  const Address do_not_unlock_if_synchronized(Rthread,
                                              in_bytes(JavaThread::do_not_unlock_if_synchronized_offset()));
  __ mov_immediate(t2, 1);
  __ st1(do_not_unlock_if_synchronized, t2);

  // increment invocation count & check for overflow
  Label invocation_counter_overflow;
  if (inc_counter) {
    generate_counter_incr(&invocation_counter_overflow);
  }

  Label continue_after_compile;
  __ bind(continue_after_compile);

  bang_stack_shadow_pages(true);

  // reset the _do_not_unlock_if_synchronized flag
  __ st1(do_not_unlock_if_synchronized, zr);

  // check for synchronized methods
  // Must happen AFTER invocation_counter check and stack overflow check,
  // so method is not locked if overflows.
  if (synchronized) {
    lock_method();
  } else {
    // no synchronization necessary
#ifdef ASSERT
    __ ld4(Rtos, access_flags);
    __ verify_access_flags(Rtos, JVM_ACC_SYNCHRONIZED, "method needs synchronization");
#endif
  }

  // start execution
#ifdef ASSERT
  __ verify_frame_setup();
#endif

  // jvmti support
  __ notify_method_entry();

  // IA-64: link the native function and get the signature handler first --
  // both may call into the VM -- and run the handler last, so that nothing
  // clobbers the argument registers it fills.
  {
    Label L;
    __ ld8(t2, Address(Rmethod, Method::native_function_offset()));
    __ movl(t3, SharedRuntime::native_method_throw_unsatisfied_link_error_entry());
    __ bne(t2, t3, L);
    __ call_VM(noreg,
               CAST_FROM_FN_PTR(address,
                                InterpreterRuntime::prepare_native_call),
               Rmethod);
    __ bind(L);
  }
  {
    Label L;
    __ ld8(t2, Address(Rmethod, Method::signature_handler_offset()));
    __ bnez(t2, L);
    __ call_VM(noreg,
               CAST_FROM_FN_PTR(address,
                                InterpreterRuntime::prepare_native_call),
               Rmethod);
    __ bind(L);
  }

  // allocate space for parameters: enough for the outgoing stack arguments,
  // plus the psABI scratch area below them.
  __ ld8(t2, Address(Rmethod, Method::const_offset()));
  __ ld2(t2, Address(t2, ConstMethod::size_of_parameters_offset()));
  __ shl_imm(t2, t2, Interpreter::logStackElementSize);
  __ sub(Resp, Resp, t2);
  __ adds(t2, -psabi_scratch_words * wordSize, Resp);
  __ and_imm(sp, -16, t2);

  // call signature handler
  assert(InterpreterRuntime::SignatureHandlerGenerator::from() == Rlocals,
         "adjust this code");
  assert(InterpreterRuntime::SignatureHandlerGenerator::to() == sp,
         "adjust this code");
  assert(InterpreterRuntime::SignatureHandlerGenerator::temp() == t2,
         "adjust this code");

  // The generated handlers do not touch Rmethod. From here to the native
  // call nothing may write out0-out7 or f8-f15.
  __ ld8(t3, Address(Rmethod, Method::signature_handler_offset()));
  __ jalr(t3);

  // result handler is in r8; keep it in the frame (no callee-saved register
  // is free to hold it across the calls below)
  __ st8(Address(fp, frame::interpreter_frame_result_handler_offset * wordSize), Rret);

  // pass mirror handle if static call
  {
    Label L;
    __ ld4(t2, Address(Rmethod, Method::access_flags_offset()));
    __ tbit_z(ptmp0, ptmp1, t2, exact_log2(JVM_ACC_STATIC));
    __ br_cond(L, ptmp0);
    // get mirror
    __ load_mirror(t2, Rmethod, t3, noreg);
    // copy mirror into activation frame
    __ st8(Address(fp, frame::interpreter_frame_oop_temp_offset * wordSize), t2);
    // pass handle to mirror
    __ adds(c_rarg1, frame::interpreter_frame_oop_temp_offset * wordSize, fp);
    __ bind(L);
  }

  // pass JNIEnv
  __ lea(c_rarg0, Address(Rthread, JavaThread::jni_environment_offset()));

  // It is enough that the pc() points into the right code
  // segment. It does not have to be the correct return pc.
  Label native_return;
  __ set_last_Java_frame(Resp, fp, native_return, t2);

  // change thread state
#ifdef ASSERT
  {
    Label L;
    __ ld4(t2, Address(Rthread, JavaThread::thread_state_offset()));
    __ cmp4_eq_imm(ptmp0, ptmp1, _thread_in_Java, t2);
    __ br_cond(L, ptmp0);
    __ stop("Wrong thread state in native stub");
    __ bind(L);
  }
#endif

  // Change state to native
  __ mov_immediate(t2, _thread_in_native);
  __ membar(MacroAssembler::LoadStore | MacroAssembler::StoreStore);
  __ st4(Address(Rthread, JavaThread::thread_state_offset()), t2);

  // Call the native method, through its function descriptor.
  __ ld8(t3, Address(Rmethod, Method::native_function_offset()));
  __ call_c(t3);
  __ bind(native_return);

  // result potentially in r8 or f8

  // make room for the pushes we're about to do, and the psABI scratch area
  // below them
  __ adds(t2, -(4 + psabi_scratch_words) * wordSize, Resp);
  __ and_imm(sp, -16, t2);

  // NOTE: The order of these pushes is known to frame::interpreter_frame_result
  // in order to extract the result of a method call. If the order of these
  // pushes change or anything else is added to the stack then the code in
  // interpreter_frame_result must also change.
  __ push(dtos);
  __ push(ltos);

  // change thread state
  // Force all preceding writes to be observed prior to thread state change
  __ membar(MacroAssembler::LoadStore | MacroAssembler::StoreStore);

  __ mov_immediate(t2, _thread_in_native_trans);
  __ st4(Address(Rthread, JavaThread::thread_state_offset()), t2);

  // Force this write out before the read below
  if (!UseSystemMemoryBarrier) {
    __ membar(MacroAssembler::AnyAny);
  }

  // check for safepoint operation in progress and/or pending suspend requests
  {
    Label L, Continue;

    // We need an acquire here to ensure that any subsequent load of the
    // global SafepointSynchronize::_state flag is ordered after this load
    // of the thread-local polling word.
    __ safepoint_poll(L, true /* at_return */, true /* acquire */, false /* in_nmethod */);
    __ ld4(t2, Address(Rthread, JavaThread::suspend_flags_offset()));
    __ beqz(t2, Continue);
    __ bind(L);

    // Don't use call_VM as it will see a possible pending exception
    // and forward it and never return here preventing us from
    // clearing _last_native_pc down below. So we do a runtime call by
    // hand.
    __ mov(c_rarg0, Rthread);
    __ call_c(CAST_FROM_FN_PTR(address, JavaThread::check_special_condition_for_native_trans));
    __ bind(Continue);
  }

  // change thread state
  // Force all preceding writes to be observed prior to thread state change
  __ membar(MacroAssembler::LoadStore | MacroAssembler::StoreStore);

  __ mov_immediate(t2, _thread_in_Java);
  __ st4(Address(Rthread, JavaThread::thread_state_offset()), t2);

  // reset_last_Java_frame
  __ reset_last_Java_frame(true);

  if (CheckJNICalls) {
    // clear_pending_jni_exception_check
    __ st8(Address(Rthread, JavaThread::pending_jni_exception_check_fn_offset()), zr);
  }

  // reset handle block
  __ ld8(t2, Address(Rthread, JavaThread::active_handles_offset()));
  __ st4(Address(t2, JNIHandleBlock::top_offset()), zr);

  // If result is an oop unbox and store it in frame where gc will see it
  // and result handler will pick it up

  {
    Label no_oop;
    __ ld8(t2, Address(fp, frame::interpreter_frame_result_handler_offset * wordSize));
    __ movl(t3, AbstractInterpreter::result_handler(T_OBJECT));
    __ bne(t2, t3, no_oop);
    // Unbox oop result, e.g. JNIHandles::resolve result.
    __ pop(ltos);
    __ resolve_jobject(Rtos, t2, t3);
    __ st8(Address(fp, frame::interpreter_frame_oop_temp_offset * wordSize), Rtos);
    // keep stack depth as expected by pushing oop which will eventually be discarded
    __ push(ltos);
    __ bind(no_oop);
  }

  {
    Label no_reguard;
    __ ld4(t2, Address(Rthread, in_bytes(JavaThread::stack_guard_state_offset())));
    __ cmp4_eq_imm(ptmp0, ptmp1, StackOverflow::stack_guard_yellow_reserved_disabled, t2);
    __ br_cond(no_reguard, ptmp1);
    // The result is already on the expression stack, so the call may
    // clobber r8 / f8.
    __ call_c(CAST_FROM_FN_PTR(address, SharedRuntime::reguard_yellow_pages));
    __ bind(no_reguard);
  }

  // The method register is junk from after the thread_in_native transition
  // until here.  Also can't call_VM until the bcp has been
  // restored.  Need bcp for throwing exception below so get it now.
  __ get_method(Rmethod);

  // restore bcp to have legal interpreter frame, i.e., bci == 0 <=>
  // Rbcp == code_base()
  __ ld8(Rbcp, Address(Rmethod, Method::const_offset()));   // get ConstMethod*
  __ adds(Rbcp, in_bytes(ConstMethod::codes_offset()), Rbcp);  // get codebase
  // handle exceptions (exception handling will handle unlocking!)
  {
    Label L;
    __ ld8(t2, Address(Rthread, Thread::pending_exception_offset()));
    __ beqz(t2, L);
    // Note: At some point we may want to unify this with the code
    // used in call_VM_base(); i.e., we should use the
    // StubRoutines::forward_exception code. For now this doesn't work
    // here because the sp is not correctly set at this point.
    __ MacroAssembler::call_VM(noreg,
                               CAST_FROM_FN_PTR(address,
                               InterpreterRuntime::throw_pending_exception));
    __ should_not_reach_here();
    __ bind(L);
  }

  // do unlocking if necessary
  {
    Label L;
    __ ld4(t2, Address(Rmethod, Method::access_flags_offset()));
    __ tbit_z(ptmp0, ptmp1, t2, exact_log2(JVM_ACC_SYNCHRONIZED));
    __ br_cond(L, ptmp0);
    // the code below should be shared with interpreter macro
    // assembler implementation
    {
      Label unlock;
      // BasicObjectLock will be first in list, since this is a
      // synchronized method. However, need to check that the object
      // has not been unlocked by an explicit monitorexit bytecode.

      // monitor expect in c_rarg1 for slow unlock path
      __ lea(c_rarg1, Address(fp,   // address of first monitor
                              (intptr_t)(frame::interpreter_frame_initial_sp_offset *
                                         wordSize - sizeof(BasicObjectLock))));

      __ ld8(t2, Address(c_rarg1, BasicObjectLock::obj_offset()));
      __ bnez(t2, unlock);

      // Entry already unlocked, need to throw exception
      __ MacroAssembler::call_VM(noreg,
                                 CAST_FROM_FN_PTR(address,
                                                  InterpreterRuntime::throw_illegal_monitor_state_exception));
      __ should_not_reach_here();

      __ bind(unlock);
      __ unlock_object(c_rarg1);
    }
    __ bind(L);
  }

#if INCLUDE_JFR
  __ enter_jfr_critical_section();

  // This poll test is to uphold the invariant that a JFR sampled frame
  // must not return to its caller without a prior safepoint poll check.
  // The earlier poll check in this routine is insufficient for this purpose
  // because the thread has transitioned back to Java.

  Label slow_path;
  Label fast_path;
  __ safepoint_poll(slow_path, true /* at_return */, false /* acquire */, false /* in_nmethod */);
  __ j(fast_path);

  __ bind(slow_path);
  __ push(dtos);
  __ push(ltos);
  __ set_last_Java_frame(Resp, fp, (address)__ pc(), t2);
  __ super_call_VM_leaf(CAST_FROM_FN_PTR(address, InterpreterRuntime::at_unwind), Rthread);
  __ reset_last_Java_frame(true);
  __ pop(ltos);
  __ pop(dtos);
  __ bind(fast_path);
#endif // INCLUDE_JFR

  // jvmti support
  // Note: This must happen _after_ handling/throwing any exceptions since
  //       the exception handler code notifies the runtime of method exits
  //       too. If this happens before, method entry/exit notifications are
  //       not properly paired (was bug - gri 11/22/99).
  __ notify_method_exit(vtos, InterpreterMacroAssembler::NotifyJVMTI);

  __ pop(ltos);
  __ pop(dtos);

  // call the result handler (generated code, so a plain call)
  __ ld8(t2, Address(fp, frame::interpreter_frame_result_handler_offset * wordSize));
  __ jalr(t2);

  // remove activation
  // get sender sp
  __ ld8(Resp, Address(fp, frame::interpreter_frame_sender_sp_offset * wordSize));
  // remove frame anchor; leaves the return address in b0
  __ leave();

  JFR_ONLY(__ leave_jfr_critical_section();)

  // restore sender sp
  __ mov(sp, Resp);

  __ ret();

  if (inc_counter) {
    // Handle overflow of counter and compile method
    __ bind(invocation_counter_overflow);
    generate_counter_overflow(continue_after_compile);
  }

  return entry_point;
}

//
// Generic interpreted method entry to (asm) interpreter
//
address TemplateInterpreterGenerator::generate_normal_entry(bool synchronized) {

  // determine code generation flags
  const bool inc_counter  = UseCompiler || CountCompiledCalls;

  // Rmethod: Method*
  // Rsender_sp: sender sp
  // Resp: the last argument pushed
  // b0: return address
  address entry_point = __ pc();

  const Address constMethod(Rmethod, Method::const_offset());
  const Address access_flags(Rmethod, Method::access_flags_offset());

  // get parameter size (always needed)
  // need to load the const method first
  __ ld8(Rconst_method, constMethod);
  __ ld2(Rsize_params, Address(Rconst_method, ConstMethod::size_of_parameters_offset()));

  // Rsize_params: size of parameters

  __ ld2(Rextra_locals, Address(Rconst_method, ConstMethod::size_of_locals_offset())); // get size of locals in words
  __ sub(Rextra_locals, Rextra_locals, Rsize_params); // Rextra_locals = no. of additional locals

  // see if we've got enough room on the stack for locals plus overhead.
  generate_stack_overflow_check();

  // compute beginning of parameters (Rlocals)
  __ shladd(Rlocals, Rsize_params, LogBytesPerWord, Resp);
  __ adds(Rlocals, -wordSize, Rlocals);

  // Make room for additional locals
  __ shl_imm(t2, Rextra_locals, LogBytesPerWord);
  __ sub(t3, Resp, t2);

  // Padding between locals and fixed part of activation frame to ensure
  // SP is always 16-byte aligned. sp moves before the locals are written.
  __ and_imm(sp, -16, t3);

  // Rextra_locals - # of additional locals
  // allocate space for locals
  // explicitly initialize locals
  {
    Label exit, loop;
    __ cmp_lt(ptmp0, ptmp1, zr, Rextra_locals);
    __ br_cond(exit, ptmp1);        // do nothing if Rextra_locals <= 0
    __ bind(loop);
    __ st8_inc(t3, zr, wordSize);
    __ adds(Rextra_locals, -1, Rextra_locals); // until everything initialized
    __ bnez(Rextra_locals, loop);
    __ bind(exit);
  }

  // initialize fixed part of activation frame
  generate_fixed_frame(false);

  // make sure method is not native & not abstract
#ifdef ASSERT
  __ ld4(Rtos, access_flags);
  __ verify_access_flags(Rtos, JVM_ACC_NATIVE, "tried to execute native method as non-native");
  __ verify_access_flags(Rtos, JVM_ACC_ABSTRACT, "tried to execute abstract method in interpreter");
#endif

  // Since at this point in the method invocation the exception
  // handler would try to exit the monitor of synchronized methods
  // which hasn't been entered yet, we set the thread local variable
  // _do_not_unlock_if_synchronized to true. The remove_activation
  // will check this flag.

  const Address do_not_unlock_if_synchronized(Rthread,
                                              in_bytes(JavaThread::do_not_unlock_if_synchronized_offset()));
  __ mov_immediate(t2, 1);
  __ st1(do_not_unlock_if_synchronized, t2);

  // increment invocation count & check for overflow
  Label invocation_counter_overflow;
  if (inc_counter) {
    generate_counter_incr(&invocation_counter_overflow);
  }

  Label continue_after_compile;
  __ bind(continue_after_compile);

  bang_stack_shadow_pages(false);

  // reset the _do_not_unlock_if_synchronized flag
  __ st1(do_not_unlock_if_synchronized, zr);

  // check for synchronized methods
  // Must happen AFTER invocation_counter check and stack overflow check,
  // so method is not locked if overflows.
  if (synchronized) {
    // Allocate monitor and lock method
    lock_method();
  } else {
    // no synchronization necessary
#ifdef ASSERT
    __ ld4(Rtos, access_flags);
    __ verify_access_flags(Rtos, JVM_ACC_SYNCHRONIZED, "method needs synchronization");
#endif
  }

  // start execution
#ifdef ASSERT
  __ verify_frame_setup();
#endif

  // jvmti support
  __ notify_method_entry();

  __ dispatch_next(vtos);

  // invocation counter overflow
  if (inc_counter) {
    // Handle overflow of counter and compile method
    __ bind(invocation_counter_overflow);
    generate_counter_overflow(continue_after_compile);
  }

  return entry_point;
}

//-----------------------------------------------------------------------------
// Exceptions

void TemplateInterpreterGenerator::generate_throw_exception() {
  // Entry point in previous activation (i.e., if the caller was
  // interpreted)
  Interpreter::_rethrow_exception_entry = __ pc();
  // Restore sp to interpreter_frame_last_sp even though we are going
  // to empty the expression stack for the exception processing.
  __ st8(Address(fp, frame::interpreter_frame_last_sp_offset * wordSize), zr);
  // Rexception: exception
  // Rexception_pc: return address/pc that threw exception
  __ restore_bcp();    // Rbcp points to call/send
  __ restore_locals();
  __ restore_constant_pool_cache();

  // Entry point for exceptions thrown within interpreter code
  Interpreter::_throw_exception_entry = __ pc();
  // expression stack is undefined here
  // Rexception: exception
  // Rbcp: exception bcp
  __ get_method(Rmethod);
  __ verify_oop(Rexception);
  __ mov(c_rarg1, Rexception);

  // expression stack must be empty before entering the VM in case of
  // an exception
  __ empty_expression_stack();
  // find exception handler address and preserve exception oop
  const Register preserved_exception = Rtmp1;
  __ call_VM(preserved_exception,
             CAST_FROM_FN_PTR(address,
                          InterpreterRuntime::exception_handler_for_exception),
             c_rarg1);

  // Restore machine SP
  __ restore_sp_after_call();

  // r8: exception handler entry point
  // preserved_exception: preserved exception oop
  // Rbcp: bcp for exception handler
  __ push_ptr(preserved_exception); // push exception which is now the only value on the stack
  __ jr(Rret); // jump to exception handler (may be _remove_activation_entry!)

  // If the exception is not handled in the current frame the frame is
  // removed and the exception is rethrown (i.e. exception
  // continuation is _rethrow_exception).
  //
  // Note: At this point the bci is still the bxi for the instruction
  // which caused the exception and the expression stack is
  // empty. Thus, for any VM calls at this point, GC will find a legal
  // oop map (with empty expression stack).

  //
  // JVMTI PopFrame support
  //

  Interpreter::_remove_activation_preserving_args_entry = __ pc();
  __ empty_expression_stack();
  __ restore_bcp(); // We could have returned from deoptimizing this frame, so restore Rbcp.
  // Set the popframe_processing bit in pending_popframe_condition
  // indicating that we are currently handling popframe, so that
  // call_VMs that may happen later do not trigger new popframe
  // handling cycles.
  __ ld4(t2, Address(Rthread, JavaThread::popframe_condition_offset()));
  __ or_imm(t2, JavaThread::popframe_processing_bit, t2);
  __ st4(Address(Rthread, JavaThread::popframe_condition_offset()), t2);

  {
    // Check to see whether we are returning to a deoptimized frame.
    // (The PopFrame call ensures that the caller of the popped frame is
    // either interpreted or compiled and deoptimizes it if compiled.)
    // In this case, we can't call dispatch_next() after the frame is
    // popped, but instead must save the incoming arguments and restore
    // them after deoptimization has occurred.
    //
    // Note that we don't compare the return PC against the
    // deoptimization blob's unpack entry because of the presence of
    // adapter frames in C2.
    Label caller_not_deoptimized;
    __ ld8(c_rarg1, Address(fp, frame::return_addr_offset * wordSize));
    __ super_call_VM_leaf(CAST_FROM_FN_PTR(address, InterpreterRuntime::interpreter_contains), c_rarg1);
    __ bnez(Rret, caller_not_deoptimized);

    // Compute size of arguments for saving when returning to
    // deoptimized caller
    __ get_method(Rtmp1);
    __ ld8(Rtmp1, Address(Rtmp1, Method::const_offset()));
    __ ld2(Rtmp1, Address(Rtmp1, in_bytes(ConstMethod::size_of_parameters_offset())));
    __ shl_imm(Rtmp1, Rtmp1, Interpreter::logStackElementSize);
    __ restore_locals();
    __ sub(Rlocals, Rlocals, Rtmp1);
    __ adds(Rlocals, wordSize, Rlocals);
    // Save these arguments
    __ super_call_VM_leaf(CAST_FROM_FN_PTR(address,
                                           Deoptimization::
                                           popframe_preserve_args),
                          Rthread, Rtmp1, Rlocals);

    __ remove_activation(vtos,
                         /* throw_monitor_exception */ false,
                         /* install_monitor_exception */ false,
                         /* notify_jvmdi */ false);

    // Inform deoptimization that it is responsible for restoring
    // these arguments
    __ mov_immediate(t2, JavaThread::popframe_force_deopt_reexecution_bit);
    __ st4(Address(Rthread, JavaThread::popframe_condition_offset()), t2);

    // Continue in deoptimization handler
    __ ret();

    __ bind(caller_not_deoptimized);
  }

  __ remove_activation(vtos,
                       /* throw_monitor_exception */ false,
                       /* install_monitor_exception */ false,
                       /* notify_jvmdi */ false);

  // Restore the last_sp and null it out
  __ ld8(t2, Address(fp, frame::interpreter_frame_last_sp_offset * wordSize));
  __ shladd(Resp, t2, LogBytesPerWord, fp);
  __ st8(Address(fp, frame::interpreter_frame_last_sp_offset * wordSize), zr);

  __ restore_bcp();
  __ restore_locals();
  __ restore_constant_pool_cache();
  __ get_method(Rmethod);

  // Clear the popframe condition flag
  __ st4(Address(Rthread, JavaThread::popframe_condition_offset()), zr);
  assert(JavaThread::popframe_inactive == 0, "fix popframe_inactive");

#if INCLUDE_JVMTI
  {
    Label L_done;

    __ ld1(t2, Rbcp);
    __ cmp_eq_imm(ptmp0, ptmp1, Bytecodes::_invokestatic, t2);
    __ br_cond(L_done, ptmp1);

    // The member name argument must be restored if _invokestatic is re-executed after a PopFrame call.
    // Detect such a case in the InterpreterRuntime function and return the member name argument,or null.

    __ ld8(c_rarg1, Address(Rlocals, 0));
    __ mov(c_rarg2, Rmethod);
    __ mov(c_rarg3, Rbcp);
    __ call_VM(Rret, CAST_FROM_FN_PTR(address, InterpreterRuntime::member_name_arg_or_null), c_rarg1, c_rarg2, c_rarg3);

    __ beqz(Rret, L_done);

    __ st8(Address(Resp, 0), Rret);
    __ bind(L_done);
  }
#endif // INCLUDE_JVMTI

  // Restore machine SP
  __ restore_sp_after_call();

  __ dispatch_next(vtos);
  // end of PopFrame support

  Interpreter::_remove_activation_entry = __ pc();

  // preserve exception over this code sequence
  __ pop_ptr(Rexception);
  __ st8(Address(Rthread, JavaThread::vm_result_oop_offset()), Rexception);
  // remove the activation (without doing throws on illegalMonitorExceptions)
  __ remove_activation(vtos, false, true, false);
  // restore exception
  __ get_vm_result_oop(Rexception, Rthread);

  // In between activations - previous activation type unknown yet
  // compute continuation point - the continuation point expects the
  // following registers set up:
  //
  // Rexception: exception
  // b0: return address/pc that threw exception
  // sp: expression stack of caller
  // fp: fp of caller
  //
  // IA-64: b0 does not survive the VM call, and no callee-saved register is
  // free, so the exception and the return address are kept in a small frame
  // of our own: two words above the 16-byte psABI scratch area.
  __ adds(sp, -32, sp);
  __ adds(t2, 16, sp);
  __ st8(t2, Rexception);
  __ mov_from_br(t3, breturn);
  __ adds(t2, 24, sp);
  __ st8(t2, t3);
  __ super_call_VM_leaf(CAST_FROM_FN_PTR(address,
                                         SharedRuntime::exception_handler_for_return_address),
                        Rthread, t3);
  __ mov(Rtmp1, Rret);                          // save exception handler
  __ adds(t2, 16, sp);
  __ ld8(Rexception, t2);                       // restore exception
  __ adds(t2, 24, sp);
  __ ld8(Rexception_pc, t2);                    // restore return address
  __ adds(sp, 32, sp);
  __ mov_to_br(breturn, Rexception_pc);
  // Note that an "issuing PC" is actually the next PC after the call
  __ jr(Rtmp1);                                 // jump to exception
                                                // handler of caller
}

//
// JVMTI ForceEarlyReturn support
//
address TemplateInterpreterGenerator::generate_earlyret_entry_for(TosState state)  {
  address entry = __ pc();

  __ restore_bcp();
  __ restore_locals();
  __ empty_expression_stack();
  __ load_earlyret_value(state);

  __ ld8(t2, Address(Rthread, JavaThread::jvmti_thread_state_offset()));
  Address cond_addr(t2, JvmtiThreadState::earlyret_state_offset());

  // Clear the earlyret state
  assert(JvmtiThreadState::earlyret_inactive == 0, "should be");
  __ st4(cond_addr, zr, t3);

  __ remove_activation(state,
                       false, /* throw_monitor_exception */
                       false, /* install_monitor_exception */
                       true); /* notify_jvmdi */
  __ ret();

  return entry;
}
// end of ForceEarlyReturn support

//-----------------------------------------------------------------------------
// Helper for vtos entry point generation

void TemplateInterpreterGenerator::set_vtos_entry_points(Template* t,
                                                         address& bep,
                                                         address& cep,
                                                         address& sep,
                                                         address& aep,
                                                         address& iep,
                                                         address& lep,
                                                         address& fep,
                                                         address& dep,
                                                         address& vep) {
  assert(t != nullptr && t->is_valid() && t->tos_in() == vtos, "illegal template");
  Label L;
  aep = __ pc();     // atos entry point
      __ push_ptr();
      __ j(L);
  fep = __ pc();     // ftos entry point
      __ push_f();
      __ j(L);
  dep = __ pc();     // dtos entry point
      __ push_d();
      __ j(L);
  lep = __ pc();     // ltos entry point
      __ push_l();
      __ j(L);
  bep = cep = sep = iep = __ pc();     // [bcsi]tos entry point
      __ push_i();
  vep = __ pc();     // vtos entry point
  __ bind(L);
  generate_and_dispatch(t);
}

//-----------------------------------------------------------------------------

// Non-product code. Bytecode tracing, counting and histograms are all
// diagnostic flags that are off by default; their code is only generated
// when one is turned on, and on IA-64 it traps for now.
#ifndef PRODUCT
address TemplateInterpreterGenerator::generate_trace_code(TosState state) {
  address entry = __ pc();
  __ unimplemented("IA-64: TraceBytecodes");
  return entry;
}

void TemplateInterpreterGenerator::count_bytecode() {
  __ unimplemented("IA-64: CountBytecodes");
}

void TemplateInterpreterGenerator::histogram_bytecode(Template* t) {
  __ unimplemented("IA-64: PrintBytecodeHistogram");
}

void TemplateInterpreterGenerator::histogram_bytecode_pair(Template* t) {
  __ unimplemented("IA-64: PrintBytecodePairHistogram");
}

void TemplateInterpreterGenerator::trace_bytecode(Template* t) {
  __ unimplemented("IA-64: TraceBytecodes");
}

void TemplateInterpreterGenerator::stop_interpreter_at() {
  __ unimplemented("IA-64: StopInterpreterAt");
}
#endif // !PRODUCT
