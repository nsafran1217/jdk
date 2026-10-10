/*
 * Copyright (c) 2000, 2025, Oracle and/or its affiliates. All rights reserved.
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
 */

#include "asm/assembler.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "c1/c1_CodeStubs.hpp"
#include "c1/c1_Compilation.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "c1/c1_Runtime1.hpp"
#include "c1/c1_ValueStack.hpp"
#include "ci/ciArrayKlass.hpp"
#include "ci/ciInstance.hpp"
#include "code/compiledIC.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "nativeInst_ia64.hpp"
#include "oops/objArrayKlass.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/safepointMechanism.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "utilities/powerOfTwo.hpp"
#include "vmreg_ia64.inline.hpp"

// The C1 back end for IA-64 (JIT-SCOPE.md milestone 2; FRAME-DESIGN.md 4.5,
// 11). The departures from riscv, which this file is otherwise shaped after:
//
// * Flags-style LIR, as x86/aarch64: lir_cmp sets the predicate pair p10/p11
//   (pcond/pncond, plus punord for floats) and the following branch or cmove
//   tests it (c1_LIRAssembler_ia64.hpp).
// * Ints: only the low 32 bits of an int register are defined when
//   C1LazyIntExtension is on; otherwise ints are kept sign-extended, as the
//   interpreter keeps them (an operation that can carry out of the low 32
//   bits is followed by sxt4 -- int_result -- and int loads sign-extend).
//   Either way every consumer that reads all 64 bits extends first: int
//   compares use cmp4; an int array index, i2l, i2f/i2d, int division,
//   arithmetic right shift, arraycopy and array-allocation lengths sign-
//   extend explicitly; an int result returned to a caller is sign-extended,
//   since the interpreter takes r8 as its sign-extended top of stack. Values
//   reaching the interpreter through memory are re-read with ld4s, and C
//   callees extend their own narrow arguments (GCC's IA-64 PROMOTE_MODE stops
//   at SImode).
// * No displacement addressing: an access computes its address first
//   (addr_reg), so an implicit null check records the pc of the load or store
//   itself, not of the adds before it.
// * t0/t1 (r2/r3) belong to the MacroAssembler and t2-t4 (r9-r11) are free
//   scratch: C1 never allocates any of them, nor f2-f7 (FRAME-DESIGN.md 2.2).
// * Polls are thread-local tests branching to an out-of-line stub, never a
//   faulting load (FRAME-DESIGN.md 11.4).

#ifndef PRODUCT
#define COMMENT(x)   do { __ block_comment(x); } while (0)
#else
#define COMMENT(x)
#endif

NEEDS_CLEANUP // remove this definitions ?
const Register SYNC_header = r8;    // synchronization header
const Register SHIFT_count = r8;    // where count for shift operations must be

#define __ _masm->

void LIR_Assembler::int_result(Register r) {
  if (!C1LazyIntExtension) {
    __ sxt4(r, r);
  }
}

Register LIR_Assembler::int_operand(Register r) {
  if (C1LazyIntExtension) {
    __ sxt4(r, r);
  }
  return r;
}

bool LIR_Assembler::is_small_constant(LIR_Opr opr) { Unimplemented(); return false; }

void LIR_Assembler::clinit_barrier(ciMethod* method) {
  // VM_Version::supports_fast_class_init_checks() is false on IA-64.
  ShouldNotReachHere();
}

LIR_Opr LIR_Assembler::receiverOpr() {
  return FrameMap::receiver_opr;
}

LIR_Opr LIR_Assembler::osrBufferPointer() {
  return FrameMap::as_pointer_opr(receiverOpr()->as_register());
}

void LIR_Assembler::breakpoint() { Unimplemented(); }

void LIR_Assembler::push(LIR_Opr opr) { Unimplemented(); }

void LIR_Assembler::pop(LIR_Opr opr) { Unimplemented(); }

static jlong as_long(LIR_Opr data) {
  jlong result;
  switch (data->type()) {
    case T_INT:
      result = (data->as_jint());
      break;
    case T_LONG:
      result = (data->as_jlong());
      break;
    default:
      ShouldNotReachHere();
      result = 0;  // unreachable
  }
  return result;
}

// ---- addresses --------------------------------------------------------------

Register LIR_Assembler::addr_reg(LIR_Address* addr, Register tmp) {
  intptr_t disp = addr->disp();
  if (addr->base()->is_illegal()) {
    assert(addr->index()->is_illegal(), "must be illegal too");
    __ movl(tmp, (uint64_t)disp);
    return tmp;
  }
  Register base = addr->base()->as_pointer_register();
  LIR_Opr index_opr = addr->index();
  int scale = addr->scale();
  if (index_opr->is_constant()) {
    disp += ((intptr_t)index_opr->as_constant_ptr()->as_jint()) << scale;
  } else if (index_opr->is_cpu_register()) {
    Register index = as_reg(index_opr);
    if (index_opr->type() == T_INT) {
      int_operand(index);
    }
    if (!ia64::is_simm14(disp)) {
      __ movl(tmp, (uint64_t)disp);
      __ add(tmp, tmp, base);
      base = tmp;
      disp = 0;
    }
    if (scale == 0) {
      __ add(tmp, base, index);
    } else {
      __ shladd(tmp, index, scale, base);
    }
    base = tmp;
  } else {
    assert(index_opr->is_illegal(), "unexpected index");
  }
  if (disp == 0) {
    return base;
  }
  if (ia64::is_simm14(disp)) {
    __ adds(tmp, disp, base);
  } else {
    __ movl(tmp, (uint64_t)disp);
    __ add(tmp, tmp, base);
  }
  return tmp;
}

Address LIR_Assembler::as_Address(LIR_Address* addr) {
  return Address(addr_reg(addr, t0), 0);
}

Address LIR_Assembler::as_Address_hi(LIR_Address* addr) {
  ShouldNotReachHere();
  return Address();
}

Address LIR_Assembler::as_Address_lo(LIR_Address* addr) {
  return as_Address(addr);
}

// The address of a spill slot, in t0.
Register LIR_Assembler::stack_slot_addr_reg(int index, int adjust) {
  Address addr = frame_map()->address_for_slot(index, adjust);
  assert(addr.base() == sp, "spill slots are sp-relative");
  // Only t0: a constant being stored may be waiting in t1 (const_reg).
  if (ia64::is_simm14(addr.offset())) {
    __ adds(t0, addr.offset(), sp);
  } else {
    __ movl(t0, (uint64_t)addr.offset());
    __ add(t0, t0, sp);
  }
  return t0;
}

// ---- entries and exits ------------------------------------------------------

void LIR_Assembler::osr_entry() {
  offsets()->set_value(CodeOffsets::OSR_Entry, code_offset());
  BlockBegin* osr_entry = compilation()->hir()->osr_entry();
  guarantee(osr_entry != nullptr, "null osr_entry!");
  ValueStack* entry_state = osr_entry->state();
  int number_of_locks = entry_state->locks_size();

  // we jump here if osr happens with the interpreter
  // state set up to continue at the beginning of the
  // loop that triggered osr - in particular, we have
  // the following registers setup:
  //
  // j_rarg0: osr buffer
  //   (TemplateTable::branch; b0 = the return address into the
  //   interpreted frame's caller)

  //build frame
  __ build_frame(initial_frame_size_in_bytes(), bang_size_in_bytes());

  // OSR buffer is
  //
  // locals[nlocals-1..0]
  // monitors[0..number_of_locks]
  //
  // locals is a direct copy of the interpreter frame so in the osr buffer
  // so first slot in the local array is the last local from the interpreter
  // and last slot is local[0] (receiver) from the interpreter
  //
  // Similarly with locks. The first lock slot in the osr buffer is the nth lock
  // from the interpreter frame, the nth lock slot in the osr buffer is 0th lock
  // in the interpreter frame (the method lock if a sync method)

  // Initialize monitors in the compiled activation.
  // All other registers are dead at this point and the locals will be
  // copied into place by code emitted in the IR.

  Register OSR_buf = osrBufferPointer()->as_pointer_register();
  {
    assert(frame::interpreter_frame_monitor_size() == BasicObjectLock::size(), "adjust code below");
    int monitor_offset = BytesPerWord * method()->max_locals() +
      (2 * BytesPerWord) * (number_of_locks - 1);
    // SharedRuntime::OSR_migration_begin() packs BasicObjectLocks in
    // the OSR buffer using 2 word entries: first the lock and then
    // the oop.
    for (int i = 0; i < number_of_locks; i++) {
      int slot_offset = monitor_offset - ((i * 2) * BytesPerWord);
#ifdef ASSERT
      // verify the interpreter's monitor has a non-null object
      {
        Label L;
        __ ld8(t2, Address(OSR_buf, slot_offset + 1 * BytesPerWord));
        __ bnez(t2, L);
        __ stop("locked object is null");
        __ bind(L);
      }
#endif // ASSERT
      __ ld8(t2, Address(OSR_buf, slot_offset + 0));
      __ st8(frame_map()->address_for_monitor_lock(i), t2);
      __ ld8(t2, Address(OSR_buf, slot_offset + 1 * BytesPerWord));
      __ st8(frame_map()->address_for_monitor_object(i), t2);
    }
  }
}

// inline cache check; done before the frame is built.
int LIR_Assembler::check_icache() {
  return __ ic_check(CodeEntryAlignment);
}

void LIR_Assembler::jobject2reg(jobject o, Register reg) {
  if (o == nullptr) {
    __ mov(reg, zr);
  } else {
    __ movoop(reg, o);
  }
}

void LIR_Assembler::jobject2reg_with_patching(Register reg, CodeEmitInfo *info) {
  deoptimize_trap(info);
}

// This specifies the rsp decrement needed to build the frame
int LIR_Assembler::initial_frame_size_in_bytes() const {
  // if rounding, must let FrameMap know!
  return in_bytes(frame_map()->framesize_in_bytes());
}

int LIR_Assembler::emit_exception_handler() {
  // generate code for exception handler
  address handler_base = __ start_a_stub(exception_handler_size());
  if (handler_base == nullptr) {
    // not enough space left for the handler
    bailout("exception handler overflow");
    return -1;
  }

  int offset = code_offset();

  // the exception oop and pc are in r8 and r28; no other registers need to
  // be preserved. Check that there is really an exception.
  __ verify_not_null_oop(Rexception);

  // search an exception handler (r8: exception oop, r28: throwing pc)
  __ far_call(Runtime1::entry_for(C1StubId::handle_exception_from_callee_id));
  __ should_not_reach_here();
  guarantee(code_offset() - offset <= exception_handler_size(), "overflow");
  __ end_a_stub();

  return offset;
}

// Emit the code to remove the frame from the stack in the exception
// unwind path.
int LIR_Assembler::emit_unwind_handler() {
#ifndef PRODUCT
  if (CommentedAssembly) {
    _masm->block_comment("Unwind handler");
  }
#endif // PRODUCT

  int offset = code_offset();

  // Fetch the exception from TLS and clear out exception related thread state
  __ ld8(Rexception, Address(Rthread, JavaThread::exception_oop_offset()));
  __ st8(Address(Rthread, JavaThread::exception_oop_offset()), zr);
  __ st8(Address(Rthread, JavaThread::exception_pc_offset()), zr);

  __ bind(_unwind_handler_entry);
  __ verify_not_null_oop(Rexception);
  // r19 is never allocated: it holds the exception across the unlocking.
  const Register saved_exception = r19;
  if (method()->is_synchronized() || compilation()->env()->dtrace_method_probes()) {
    __ mov(saved_exception, Rexception);   // Preserve the exception
  }

  // Perform needed unlocking
  MonitorExitStub* stub = nullptr;
  if (method()->is_synchronized()) {
    LIR_Opr lock = FrameMap::gr_opr(FrameMap::stub_tmp2_reg);
    monitor_address(0, lock);
    stub = new MonitorExitStub(lock, true, 0);
    if (LockingMode == LM_MONITOR) {
      __ j(*stub->entry());
    } else {
      __ unlock_object(as_Register(FrameMap::stub_tmp1_reg), as_Register(FrameMap::stub_tmp3_reg),
                       lock->as_register(), t4, *stub->entry());
    }
    __ bind(*stub->continuation());
  }

  if (compilation()->env()->dtrace_method_probes()) {
    __ mov_metadata(c_rarg1, method()->constant_encoding());
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::dtrace_method_exit), Rthread, c_rarg1);
  }

  if (method()->is_synchronized() || compilation()->env()->dtrace_method_probes()) {
    __ mov(Rexception, saved_exception);   // Restore the exception
  }

  // remove the activation and dispatch to the unwind handler
  __ block_comment("remove_frame and dispatch to the unwind handler");
  __ remove_frame(initial_frame_size_in_bytes());
  __ far_jump(Runtime1::entry_for(C1StubId::unwind_exception_id));

  // Emit the slow path assembly
  if (stub != nullptr) {
    stub->emit_code(this);
  }

  return offset;
}

int LIR_Assembler::emit_deopt_handler() {
  // generate code for exception handler
  address handler_base = __ start_a_stub(deopt_handler_size());
  if (handler_base == nullptr) {
    // not enough space left for the handler
    bailout("deopt handler overflow");
    return -1;
  }

  int offset = code_offset();

  // The deopt blob is entered with b0 = this handler's own address, which is
  // how it recognises the frame being deoptimized (riscv: auipc ra, 0).
  __ mov_from_ip(t2);
  __ mov_to_br(breturn, t2);
  __ far_jump(SharedRuntime::deopt_blob()->unpack());
  guarantee(code_offset() - offset <= deopt_handler_size(), "overflow");
  __ end_a_stub();

  return offset;
}

void LIR_Assembler::return_op(LIR_Opr result, C1SafepointPollStub* code_stub) {
  assert(result->is_illegal() || !result->is_single_cpu() || result->as_register() == r8, "word returns are in r8");
  if (result->is_single_cpu() && result->type() == T_INT && C1LazyIntExtension) {
    // An interpreted caller takes r8 as its sign-extended int top of stack.
    __ sxt4(r8, r8);
  }

  // Pop the stack before the safepoint code
  __ remove_frame(initial_frame_size_in_bytes());

  if (StackReservedPages > 0 && compilation()->has_reserved_stack_access()) {
    // The frame is gone and b0 holds the return address: an access to the
    // reserved zone enables it and throws as though from the caller.
    Label no_reserved_zone_enabling;
    __ ld8(t2, Address(Rthread, JavaThread::reserved_stack_activation_offset()));
    __ bltu(sp, t2, no_reserved_zone_enabling);
    __ enter();
    __ mov(c_rarg0, Rthread);
    __ call_c(CAST_FROM_FN_PTR(address, SharedRuntime::enable_stack_reserved_zone));
    __ leave();
    __ far_jump(SharedRuntime::throw_delayed_StackOverflowError_entry());
    __ bind(no_reserved_zone_enabling);
  }

  code_stub->set_safepoint_offset(__ offset());
  __ relocate(relocInfo::poll_return_type);
  __ safepoint_poll(*code_stub->entry(), true /* at_return */, false /* acquire */, true /* in_nmethod */);
  __ ret();
}

// A loop safepoint poll: the thread-local poll bit tested in a register and a
// predicated branch to an out-of-line stub. The branch is the poll
// instruction -- debug info and the poll relocation are at its pc -- and the
// stub records that pc for the safepoint handler blob, which resumes at the
// following bundle (SharedRuntime::generate_handler_blob).
class IA64LoopSafepointPollStub : public CodeStub {
 private:
  int _poll_offset;
 public:
  IA64LoopSafepointPollStub(int poll_offset) : _poll_offset(poll_offset) {}
  virtual void emit_code(LIR_Assembler* ce) {
    MacroAssembler* masm = ce->masm();
    masm->bind(_entry);
    // The poll's pc, position-independently (the code is copied).
    masm->mov_from_ip(t0);
    masm->add_imm(t0, t0, _poll_offset - masm->offset() + (int)BytesPerBundle, t1);
    masm->st8(Address(Rthread, JavaThread::saved_exception_pc_offset()), t0, t1);
    masm->far_jump(SharedRuntime::polling_page_safepoint_handler_blob()->entry_point());
  }
  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_slow_case();
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("IA64LoopSafepointPollStub"); }
#endif // PRODUCT
};

int LIR_Assembler::safepoint_poll(LIR_Opr tmp, CodeEmitInfo* info) {
  guarantee(info != nullptr, "Shouldn't be null");
  if (UsePollWordRegister) {
#ifdef ASSERT
    Label ok;
    __ adds(t2, in_bytes(JavaThread::polling_word_offset()), Rthread);
    __ beq(t2, C1_MacroAssembler::Rpoll_word, ok);
    __ stop("IA-64: r7 does not hold &JavaThread::_poll_word at a C1 loop poll");
    __ bind(ok);
#endif
    __ Assembler::ld8(t1, C1_MacroAssembler::Rpoll_word);
  } else {
    __ ld8(t1, Address(Rthread, JavaThread::polling_word_offset()));
  }
  __ tbit_nz(ptmp0, ptmp1, t1, exact_log2(SafepointMechanism::poll_bit()));
  int poll_offset = __ offset();
  IA64LoopSafepointPollStub* stub = new IA64LoopSafepointPollStub(poll_offset);
  append_code_stub(stub);
  add_debug_info_for_branch(info);  // This isn't just debug info:
                                    // it's the oop map
  __ relocate(relocInfo::poll_type);
  __ br_cond(*stub->entry(), ptmp0);
  return poll_offset;
}

// ---- moves ------------------------------------------------------------------

void LIR_Assembler::move_regs(Register from_reg, Register to_reg) {
  __ mov(to_reg, from_reg);
}

void LIR_Assembler::swap_reg(Register a, Register b) { Unimplemented(); }

void LIR_Assembler::const2reg(LIR_Opr src, LIR_Opr dest, LIR_PatchCode patch_code, CodeEmitInfo* info) {
  assert(src->is_constant(), "should not call otherwise");
  assert(dest->is_register(), "should not call otherwise");
  LIR_Const* c = src->as_constant_ptr();

  switch (c->type()) {
    case T_INT:
      assert(patch_code == lir_patch_none, "no patching handled here");
      __ mov_immediate(dest->as_register(), c->as_jint());
      break;

    case T_ADDRESS:
      assert(patch_code == lir_patch_none, "no patching handled here");
      __ mov_immediate(dest->as_register(), c->as_jint());
      break;

    case T_LONG:
      assert(patch_code == lir_patch_none, "no patching handled here");
      __ mov_immediate(dest->as_register_lo(), (int64_t)c->as_jlong());
      break;

    case T_OBJECT:
    case T_ARRAY:
      if (patch_code == lir_patch_none) {
        jobject2reg(c->as_jobject(), dest->as_register());
      } else {
        jobject2reg_with_patching(dest->as_register(), info);
      }
      break;

    case T_METADATA:
      if (patch_code != lir_patch_none) {
        klass2reg_with_patching(dest->as_register(), info);
      } else {
        __ mov_metadata(dest->as_register(), c->as_metadata());
      }
      break;

    // FP constants travel through t1 and setf, which gives a register value
    // of exactly the right type (single for setf.s, double for setf.d).
    case T_FLOAT:
      __ mov_immediate(t1, (int64_t)(juint)c->as_jint_bits());
      __ setf_s(dest->as_float_reg(), t1);
      break;

    case T_DOUBLE:
      __ mov_immediate(t1, (int64_t)c->as_jlong_bits());
      __ setf_d(dest->as_double_reg(), t1);
      break;

    default:
      ShouldNotReachHere();
  }
}

// A constant in a register: r0 for zero, else materialised in t1.
Register LIR_Assembler::const_reg(LIR_Opr opr) {
  LIR_Const* c = opr->as_constant_ptr();
  switch (c->type()) {
    case T_INT:
    case T_FLOAT:
      if (c->as_jint_bits() == 0) return zr;
      __ mov_immediate(t1, (int64_t)c->as_jint_bits());   // sign-extended
      return t1;
    case T_LONG:
    case T_DOUBLE:
      if (c->as_jlong_bits() == 0) return zr;
      __ mov_immediate(t1, (int64_t)c->as_jlong_bits());
      return t1;
    case T_ADDRESS:
      if (c->as_jint() == 0) return zr;
      __ mov_immediate(t1, (int64_t)c->as_jint());
      return t1;
    case T_OBJECT:
    case T_ARRAY:
      if (c->as_jobject() == nullptr) return zr;
      jobject2reg(c->as_jobject(), t1);
      return t1;
    case T_METADATA:
      if (c->as_metadata() == nullptr) return zr;
      __ mov_metadata(t1, c->as_metadata());
      return t1;
    default:
      ShouldNotReachHere();
      return noreg;
  }
}

void LIR_Assembler::const2stack(LIR_Opr src, LIR_Opr dest) {
  assert(src->is_constant(), "should not call otherwise");
  assert(dest->is_stack(), "should not call otherwise");
  LIR_Const* c = src->as_constant_ptr();
  Register value = const_reg(src);
  switch (c->type()) {
    case T_INT:
    case T_FLOAT:
      __ Assembler::st4(stack_slot_addr_reg(dest->single_stack_ix()), value);
      break;
    case T_OBJECT:
    case T_ARRAY:
    case T_ADDRESS:
    case T_METADATA:
      __ Assembler::st8(stack_slot_addr_reg(dest->single_stack_ix()), value);
      break;
    case T_LONG:
    case T_DOUBLE:
      __ Assembler::st8(stack_slot_addr_reg(dest->double_stack_ix(), lo_word_offset_in_bytes), value);
      break;
    default:
      ShouldNotReachHere();
  }
}

void LIR_Assembler::const2mem(LIR_Opr src, LIR_Opr dest, BasicType type, CodeEmitInfo* info, bool wide) {
  assert(src->is_constant(), "should not call otherwise");
  assert(dest->is_address(), "should not call otherwise");
  LIR_Address* to_addr = dest->as_address_ptr();
  Register value = const_reg(src);
  Register a = addr_reg(to_addr, t0);
  if (info != nullptr) {
    add_debug_info_for_null_check_here(info);
  }
  switch (type) {
    case T_ADDRESS:
    case T_LONG:
    case T_DOUBLE:
    case T_OBJECT:
    case T_ARRAY:
    case T_METADATA:
      __ Assembler::st8(a, value); break;
    case T_INT:
    case T_FLOAT:
      __ Assembler::st4(a, value); break;
    case T_CHAR:
    case T_SHORT:
      __ Assembler::st2(a, value); break;
    case T_BOOLEAN:
    case T_BYTE:
      __ Assembler::st1(a, value); break;
    default:
      ShouldNotReachHere();
  }
}

void LIR_Assembler::reg2reg(LIR_Opr src, LIR_Opr dest) {
  assert(src->is_register(), "should not call otherwise");
  assert(dest->is_register(), "should not call otherwise");

  // move between cpu-registers
  if (dest->is_single_cpu()) {
    if (src->type() == T_LONG) {
      // Can do LONG -> OBJECT
      move_regs(src->as_register_lo(), dest->as_register());
      return;
    }
    assert(src->is_single_cpu(), "must match");
    if (src->type() == T_OBJECT) {
      __ verify_oop(src->as_register());
    }
    move_regs(src->as_register(), dest->as_register());
  } else if (dest->is_double_cpu()) {
    if (is_reference_type(src->type())) {
      __ verify_oop(src->as_register());
      move_regs(src->as_register(), dest->as_register_lo());
      return;
    }
    assert(src->is_double_cpu(), "must match");
    move_regs(src->as_register_lo(), dest->as_register_lo());
  } else if (dest->is_single_fpu()) {
    assert(src->is_single_fpu(), "expect single fpu");
    if (dest->as_float_reg() != src->as_float_reg()) {
      __ fmov(dest->as_float_reg(), src->as_float_reg());
    }
  } else if (dest->is_double_fpu()) {
    assert(src->is_double_fpu(), "expect double fpu");
    if (dest->as_double_reg() != src->as_double_reg()) {
      __ fmov(dest->as_double_reg(), src->as_double_reg());
    }
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::reg2stack(LIR_Opr src, LIR_Opr dest, BasicType type) {
  precond(src->is_register() && dest->is_stack());

  if (src->is_single_cpu()) {
    Register a = stack_slot_addr_reg(dest->single_stack_ix());
    if (is_reference_type(type) || type == T_METADATA || type == T_DOUBLE || type == T_ADDRESS) {
      __ Assembler::st8(a, src->as_register());
    } else {
      __ Assembler::st4(a, src->as_register());
    }
  } else if (src->is_double_cpu()) {
    Register a = stack_slot_addr_reg(dest->double_stack_ix(), lo_word_offset_in_bytes);
    __ Assembler::st8(a, src->as_register_lo());
  } else if (src->is_single_fpu()) {
    // A C1 float value is single-typed in its register (ISA-NOTES.md).
    __ Assembler::stfs(stack_slot_addr_reg(dest->single_stack_ix()), src->as_float_reg());
  } else if (src->is_double_fpu()) {
    __ Assembler::stfd(stack_slot_addr_reg(dest->double_stack_ix()), src->as_double_reg());
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::reg2mem(LIR_Opr src, LIR_Opr dest, BasicType type, LIR_PatchCode patch_code, CodeEmitInfo* info, bool wide) {
  LIR_Address* to_addr = dest->as_address_ptr();

  if (patch_code != lir_patch_none) {
    deoptimize_trap(info);
    return;
  }

  if (is_reference_type(type)) {
    __ verify_oop(src->as_register());
    assert(!UseCompressedOops, "IA-64: compressed oops are off (FRAME-DESIGN.md 2.4)");
  }

  Register a = addr_reg(to_addr, t0);
  int null_check_here = code_offset();

  switch (type) {
    case T_FLOAT:
      __ Assembler::stfs(a, src->as_float_reg());
      break;

    case T_DOUBLE:
      __ Assembler::stfd(a, src->as_double_reg());
      break;

    case T_ARRAY:      // fall through
    case T_OBJECT:
    case T_ADDRESS:
      __ Assembler::st8(a, src->as_register());
      break;
    case T_METADATA:
      // We get here to store a method pointer to the stack to pass to
      // a dtrace runtime call. This can't work on 64 bit with
      // compressed klass ptrs: T_METADATA can be compressed klass
      // ptr or a 64 bit method pointer.
      ShouldNotReachHere();
      break;
    case T_INT:
      __ Assembler::st4(a, src->as_register());
      break;
    case T_LONG:
      __ Assembler::st8(a, src->as_register_lo());
      break;
    case T_BYTE:    // fall through
    case T_BOOLEAN:
      __ Assembler::st1(a, src->as_register());
      break;
    case T_CHAR:    // fall through
    case T_SHORT:
      __ Assembler::st2(a, src->as_register());
      break;
    default:
      ShouldNotReachHere();
  }

  if (info != nullptr) {
    add_debug_info_for_null_check(null_check_here, info);
  }
}

void LIR_Assembler::stack2reg(LIR_Opr src, LIR_Opr dest, BasicType type) {
  precond(src->is_stack() && dest->is_register());

  if (dest->is_single_cpu()) {
    Register a = stack_slot_addr_reg(src->single_stack_ix());
    Register d = dest->as_register();
    if (type == T_INT) {
      __ Assembler::ld4(d, a);
      int_result(d);
    } else if (is_reference_type(type) || type == T_METADATA || type == T_ADDRESS) {
      __ Assembler::ld8(d, a);
      __ verify_oop(d);
    } else {
      __ Assembler::ld4(d, a);
    }
  } else if (dest->is_double_cpu()) {
    Register a = stack_slot_addr_reg(src->double_stack_ix(), lo_word_offset_in_bytes);
    __ Assembler::ld8(dest->as_register_lo(), a);
  } else if (dest->is_single_fpu()) {
    __ Assembler::ldfs(dest->as_float_reg(), stack_slot_addr_reg(src->single_stack_ix()));
  } else if (dest->is_double_fpu()) {
    __ Assembler::ldfd(dest->as_double_reg(), stack_slot_addr_reg(src->double_stack_ix()));
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::klass2reg_with_patching(Register reg, CodeEmitInfo* info) {
  deoptimize_trap(info);
}

void LIR_Assembler::stack2stack(LIR_Opr src, LIR_Opr dest, BasicType type) {
  // t2 is never allocated; the slot addresses use t0.
  LIR_Opr temp = (type == T_LONG || type == T_DOUBLE) ? FrameMap::as_long_opr(t2)
                                                       : FrameMap::as_opr(t2);
  if (type == T_DOUBLE) {
    __ Assembler::ld8(t2, stack_slot_addr_reg(src->double_stack_ix()));
    __ Assembler::st8(stack_slot_addr_reg(dest->double_stack_ix()), t2);
    return;
  }
  if (type == T_FLOAT) {
    __ Assembler::ld4(t2, stack_slot_addr_reg(src->single_stack_ix()));
    __ Assembler::st4(stack_slot_addr_reg(dest->single_stack_ix()), t2);
    return;
  }
  stack2reg(src, temp, src->type());
  reg2stack(temp, dest, dest->type());
}

void LIR_Assembler::mem2reg(LIR_Opr src, LIR_Opr dest, BasicType type, LIR_PatchCode patch_code, CodeEmitInfo* info, bool wide) {
  assert(src->is_address(), "should not call otherwise");
  assert(dest->is_register(), "should not call otherwise");

  LIR_Address* from_addr = src->as_address_ptr();

  if (from_addr->base()->type() == T_OBJECT) {
    __ verify_oop(from_addr->base()->as_pointer_register());
  }

  if (patch_code != lir_patch_none) {
    deoptimize_trap(info);
    return;
  }

  Register a = addr_reg(from_addr, t0);
  if (info != nullptr) {
    add_debug_info_for_null_check_here(info);
  }

  switch (type) {
    case T_FLOAT:
      __ Assembler::ldfs(dest->as_float_reg(), a);
      break;
    case T_DOUBLE:
      __ Assembler::ldfd(dest->as_double_reg(), a);
      break;
    case T_ARRAY:     // fall through
    case T_OBJECT:
      assert(!UseCompressedOops, "IA-64: compressed oops are off (FRAME-DESIGN.md 2.4)");
      __ Assembler::ld8(dest->as_register(), a);
      break;
    case T_METADATA:
      // We get here to store a method pointer to the stack to pass to
      // a dtrace runtime call. This can't work on 64 bit with
      // compressed klass ptrs: T_METADATA can be a compressed klass
      // ptr or a 64 bit method pointer.
      ShouldNotReachHere();
      break;
    case T_ADDRESS:
      __ Assembler::ld8(dest->as_register(), a);
      break;
    case T_INT:
      __ Assembler::ld4(dest->as_register(), a);
      int_result(dest->as_register());
      break;
    case T_LONG:
      __ Assembler::ld8(dest->as_register_lo(), a);
      break;
    case T_BYTE:
      __ Assembler::ld1(dest->as_register(), a);
      __ sxt1(dest->as_register(), dest->as_register());
      break;
    case T_BOOLEAN:
      __ Assembler::ld1(dest->as_register(), a);
      break;
    case T_CHAR:
      __ Assembler::ld2(dest->as_register(), a);
      break;
    case T_SHORT:
      __ Assembler::ld2(dest->as_register(), a);
      __ sxt2(dest->as_register(), dest->as_register());
      break;
    default:
      ShouldNotReachHere();
  }

  if (is_reference_type(type)) {
    __ verify_oop(dest->as_register());
  }
}

// ---- three-operand ops, compares, branches, cmoves -------------------------

void LIR_Assembler::emit_op3(LIR_Op3* op) {
  switch (op->code()) {
    case lir_idiv: // fall through
    case lir_irem:
      arithmetic_idiv(op->code(),
                      op->in_opr1(),
                      op->in_opr2(),
                      op->in_opr3(),
                      op->result_opr(),
                      op->info());
      break;
    case lir_fmad:
      __ fma_d(op->result_opr()->as_double_reg(),
               op->in_opr1()->as_double_reg(),
               op->in_opr2()->as_double_reg(),
               op->in_opr3()->as_double_reg(), ia64::sf0);
      break;
    case lir_fmaf:
      __ fma_s(op->result_opr()->as_float_reg(),
               op->in_opr1()->as_float_reg(),
               op->in_opr2()->as_float_reg(),
               op->in_opr3()->as_float_reg(), ia64::sf0);
      break;
    default:
      ShouldNotReachHere();
  }
}

// pcond = (opr1 condition opr2), pncond = its complement; for floats also
// punord = unordered. The relation is chosen here, from the compare's own
// condition, which shared code keeps equal to the condition of the branch
// or cmove that follows (ControlFlowOptimizer negates both together).
void LIR_Assembler::comp_op(LIR_Condition condition, LIR_Opr opr1, LIR_Opr opr2, LIR_Op2* op) {
  if (opr1->is_single_fpu() || opr1->is_double_fpu()) {
    FloatRegister a = opr1->is_single_fpu() ? opr1->as_float_reg() : opr1->as_double_reg();
    FloatRegister b = opr2->is_single_fpu() ? opr2->as_float_reg() : opr2->as_double_reg();
    switch (condition) {
      case lir_cond_equal:        __ fcmp_eq(pcond, pncond, a, b); break;
      case lir_cond_notEqual:     __ fcmp_eq(pncond, pcond, a, b); break;
      case lir_cond_less:         __ fcmp_lt(pcond, pncond, a, b); break;
      case lir_cond_lessEqual:    __ fcmp_le(pcond, pncond, a, b); break;
      case lir_cond_greater:      __ fcmp_lt(pcond, pncond, b, a); break;
      case lir_cond_greaterEqual: __ fcmp_le(pcond, pncond, b, a); break;
      default:                    ShouldNotReachHere();
    }
    __ fcmp_unord(punord, pord, a, b);
    return;
  }

  assert(opr1->is_single_cpu() || opr1->is_double_cpu(), "unexpected compare operand");
  Register a = as_reg(opr1);
  Register b;
  if (opr2->is_constant()) {
    b = const_reg(opr2);
  } else if (opr2->is_single_cpu() || opr2->is_double_cpu()) {
    b = as_reg(opr2);
  } else if (opr2->is_stack()) {
    // Not produced by this port's LIRGenerator, but cheap to support.
    if (opr2->is_single_stack()) {
      __ Assembler::ld4(t1, stack_slot_addr_reg(opr2->single_stack_ix()));
      if (opr2->type() == T_INT) __ sxt4(t1, t1);
    } else {
      __ Assembler::ld8(t1, stack_slot_addr_reg(opr2->double_stack_ix()));
    }
    b = t1;
  } else {
    ShouldNotReachHere();
    b = noreg;
  }
  if (opr1->type() == T_INT) {
    // cmp4 reads only the low 32 bits: the int compare under either model.
    switch (condition) {
      case lir_cond_equal:        __ cmp4_eq (pcond,  pncond, a, b); break;
      case lir_cond_notEqual:     __ cmp4_eq (pncond, pcond,  a, b); break;
      case lir_cond_less:         __ cmp4_lt (pcond,  pncond, a, b); break;
      case lir_cond_greaterEqual: __ cmp4_lt (pncond, pcond,  a, b); break;
      case lir_cond_greater:      __ cmp4_lt (pcond,  pncond, b, a); break;
      case lir_cond_lessEqual:    __ cmp4_lt (pncond, pcond,  b, a); break;
      case lir_cond_belowEqual:   __ cmp4_ltu(pncond, pcond,  b, a); break;
      case lir_cond_aboveEqual:   __ cmp4_ltu(pncond, pcond,  a, b); break;
      default:                    ShouldNotReachHere();
    }
    return;
  }
  switch (condition) {
    case lir_cond_equal:        __ cmp_eq (pcond,  pncond, a, b); break;
    case lir_cond_notEqual:     __ cmp_eq (pncond, pcond,  a, b); break;
    case lir_cond_less:         __ cmp_lt (pcond,  pncond, a, b); break;
    case lir_cond_greaterEqual: __ cmp_lt (pncond, pcond,  a, b); break;
    case lir_cond_greater:      __ cmp_lt (pcond,  pncond, b, a); break;
    case lir_cond_lessEqual:    __ cmp_lt (pncond, pcond,  b, a); break;
    case lir_cond_belowEqual:   __ cmp_ltu(pncond, pcond,  b, a); break;
    case lir_cond_aboveEqual:   __ cmp_ltu(pncond, pcond,  a, b); break;
    default:                    ShouldNotReachHere();
  }
}

void LIR_Assembler::emit_opBranch(LIR_OpBranch* op) {
  if (op->cond() == lir_cond_always) {
    if (op->info() != nullptr) {
      add_debug_info_for_branch(op->info());
    }
    __ br(*(op->label()));
    return;
  }
  if (op->ublock() != nullptr) {
    // A float compare: an unordered result goes to ublock whatever the
    // condition says.
    __ br_cond(*(op->ublock()->label()), punord);
  }
  __ br_cond(*(op->label()), pcond);
}

// result = pcond ? opr1 : opr2. Both moves are predicated, opr2's first, so
// result may be either operand's register.
void LIR_Assembler::cmove(LIR_Condition condition, LIR_Opr opr1, LIR_Opr opr2, LIR_Opr result, BasicType type,
                          LIR_Opr cmp_opr1, LIR_Opr cmp_opr2) {
  assert(cmp_opr1->is_illegal() && cmp_opr2->is_illegal(), "flags-style LIR");
  if (result->is_single_fpu() || result->is_double_fpu()) {
    FloatRegister dst = result->is_single_fpu() ? result->as_float_reg() : result->as_double_reg();
    LIR_Opr ops[2] = { opr2, opr1 };
    PredicateRegister preds[2] = { pncond, pcond };
    for (int i = 0; i < 2; i++) {
      LIR_Opr o = ops[i];
      FloatRegister src;
      if (o->is_single_fpu() || o->is_double_fpu()) {
        src = o->is_single_fpu() ? o->as_float_reg() : o->as_double_reg();
      } else {
        // a constant or a spilled value: into f7, never allocated
        src = f7;
        if (o->is_constant()) {
          if (o->type() == T_FLOAT) {
            __ mov_immediate(t1, (int64_t)(juint)o->as_constant_ptr()->as_jint_bits());
            __ setf_s(src, t1);
          } else {
            __ mov_immediate(t1, (int64_t)o->as_constant_ptr()->as_jlong_bits());
            __ setf_d(src, t1);
          }
        } else if (o->is_single_stack()) {
          __ Assembler::ldfs(src, stack_slot_addr_reg(o->single_stack_ix()));
        } else {
          __ Assembler::ldfd(src, stack_slot_addr_reg(o->double_stack_ix()));
        }
      }
      if (src != dst) {
        __ fmov(dst, src, preds[i]);
      }
    }
    return;
  }

  Register dst = as_reg(result);
  LIR_Opr ops[2] = { opr2, opr1 };
  PredicateRegister preds[2] = { pncond, pcond };
  for (int i = 0; i < 2; i++) {
    LIR_Opr o = ops[i];
    PredicateRegister p = preds[i];
    if (o->is_constant()) {
      LIR_Const* c = o->as_constant_ptr();
      jlong v;
      switch (c->type()) {
        case T_INT:     v = c->as_jint(); break;
        case T_LONG:    v = c->as_jlong(); break;
        case T_ADDRESS: v = c->as_jint(); break;
        case T_OBJECT:
        case T_ARRAY:
        case T_METADATA: {
          Register r = const_reg(o);
          __ Assembler::mov(dst, r, p);
          continue;
        }
        default: ShouldNotReachHere(); v = 0;
      }
      if (ia64::is_simm14(v)) {
        __ adds(dst, v, zr, p);
      } else {
        __ movl(dst, (uint64_t)v, p);
      }
    } else if (o->is_single_cpu() || o->is_double_cpu()) {
      Register src = as_reg(o);
      if (src != dst) {
        __ Assembler::mov(dst, src, p);
      }
    } else if (o->is_single_stack() || o->is_double_stack()) {
      Register a = o->is_single_stack() ? stack_slot_addr_reg(o->single_stack_ix())
                                        : stack_slot_addr_reg(o->double_stack_ix());
      if (o->is_double_stack() || is_reference_type(o->type()) || o->type() == T_METADATA || o->type() == T_ADDRESS) {
        __ Assembler::ld8(t1, a);
      } else {
        __ Assembler::ld4(t1, a);
        __ sxt4(t1, t1);
      }
      __ Assembler::mov(dst, t1, p);
    } else {
      ShouldNotReachHere();
    }
  }
}

// ---- conversions --------------------------------------------------------------

void LIR_Assembler::emit_opConvert(LIR_OpConvert* op) {
  LIR_Opr src  = op->in_opr();
  LIR_Opr dest = op->result_opr();

  switch (op->bytecode()) {
    // integer -> FP: the significand path, exact in register format, then
    // one rounding to the result type.
    case Bytecodes::_i2f:
      __ setf_sig(dest->as_float_reg(), int_operand(src->as_register()));
      __ fcvt_xf(dest->as_float_reg(), dest->as_float_reg());
      __ fnorm_s(dest->as_float_reg(), dest->as_float_reg());
      break;
    case Bytecodes::_i2d:
      __ setf_sig(dest->as_double_reg(), int_operand(src->as_register()));
      __ fcvt_xf(dest->as_double_reg(), dest->as_double_reg());
      __ fnorm_d(dest->as_double_reg(), dest->as_double_reg());
      break;
    case Bytecodes::_l2d:
      __ setf_sig(dest->as_double_reg(), src->as_register_lo());
      __ fcvt_xf(dest->as_double_reg(), dest->as_double_reg());
      __ fnorm_d(dest->as_double_reg(), dest->as_double_reg());
      break;
    case Bytecodes::_l2f:
      __ setf_sig(dest->as_float_reg(), src->as_register_lo());
      __ fcvt_xf(dest->as_float_reg(), dest->as_float_reg());
      __ fnorm_s(dest->as_float_reg(), dest->as_float_reg());
      break;
    // f2d is exact; fnorm.d gives the value the double type stfd needs
    // (ISA-NOTES.md). d2f rounds.
    case Bytecodes::_f2d:
      __ fnorm_d(dest->as_double_reg(), src->as_float_reg()); break;
    case Bytecodes::_d2f:
      __ fnorm_s(dest->as_float_reg(), src->as_double_reg()); break;
    case Bytecodes::_i2c:
      __ zxt2(dest->as_register(), src->as_register()); break;
    case Bytecodes::_i2l:
      if (C1LazyIntExtension) {
        __ sxt4(dest->as_register_lo(), src->as_register());
      } else {
        __ mov(dest->as_register_lo(), src->as_register());   // already sign-extended
      }
      break;
    case Bytecodes::_i2s:
      __ sxt2(dest->as_register(), src->as_register()); break;
    case Bytecodes::_i2b:
      __ sxt1(dest->as_register(), src->as_register()); break;
    case Bytecodes::_l2i:
      if (C1LazyIntExtension) {
        __ mov(dest->as_register(), src->as_register_lo());
      } else {
        __ sxt4(dest->as_register(), src->as_register_lo());
      }
      break;
    case Bytecodes::_d2l:
      __ java_fp_to_long(dest->as_register_lo(), src->as_double_reg()); break;
    case Bytecodes::_f2i:
      __ java_fp_to_int(dest->as_register(), src->as_float_reg()); break;
    case Bytecodes::_f2l:
      __ java_fp_to_long(dest->as_register_lo(), src->as_float_reg()); break;
    case Bytecodes::_d2i:
      __ java_fp_to_int(dest->as_register(), src->as_double_reg()); break;
    default:
      ShouldNotReachHere();
  }
}

// ---- allocation ---------------------------------------------------------------

void LIR_Assembler::emit_alloc_obj(LIR_OpAllocObj* op) {
  if (op->init_check()) {
    __ adds(t1, in_bytes(InstanceKlass::init_state_offset()), op->klass()->as_register());
    add_debug_info_for_null_check_here(op->stub()->info());
    __ Assembler::ld1(t0, t1);
    __ mf();   // the class's initialization is visible before its instances
    __ cmp_eq_imm(ptmp0, ptmp1, (int64_t)InstanceKlass::fully_initialized, t0);
    __ br_cond(*op->stub()->entry(), ptmp1);
  }

  __ allocate_object(op->obj()->as_register(),
                     op->tmp1()->as_register(),
                     op->tmp2()->as_register(),
                     op->header_size(),
                     op->object_size(),
                     op->klass()->as_register(),
                     *op->stub()->entry());

  __ bind(*op->stub()->continuation());
}

void LIR_Assembler::emit_alloc_array(LIR_OpAllocArray* op) {
  Register len = op->len()->as_register();

  if (UseSlowPath ||
      (!UseFastNewObjectArray && is_reference_type(op->type())) ||
      (!UseFastNewTypeArray   && !is_reference_type(op->type()))) {
    __ j(*op->stub()->entry());
  } else {
    Register tmp1 = op->tmp1()->as_register();
    Register tmp2 = op->tmp2()->as_register();
    Register tmp3 = op->tmp3()->as_register();
    if (len == tmp1) {
      tmp1 = tmp3;
    } else if (len == tmp2) {
      tmp2 = tmp3;
    } else if (len == tmp3) {
      // everything is ok
    } else {
      __ mov(tmp3, len);
    }
    __ allocate_array(op->obj()->as_register(),
                      len,
                      tmp1,
                      tmp2,
                      arrayOopDesc::base_offset_in_bytes(op->type()),
                      array_element_size(op->type()),
                      op->klass()->as_register(),
                      *op->stub()->entry(),
                      op->zero_array());
  }
  __ bind(*op->stub()->continuation());
}

// ---- type checks --------------------------------------------------------------

// ---- profiling (tiered levels 2 and 3), after riscv ------------------------------
//
// MDO updates use t0/t1 only. Counters are bumped with a plain load, add and
// store: profiles are racy on every port.

void LIR_Assembler::increment_mdo(Register mdo, int offset) {
  assert_different_registers(mdo, t0, t1);
  __ add_imm(t1, mdo, offset, t0);
  __ Assembler::ld8(t0, t1);
  __ adds(t0, DataLayout::counter_increment, t0);
  __ Assembler::st8(t1, t0);
}

void LIR_Assembler::type_profile_helper(Register mdo, ciMethodData *md, ciProfileData *data,
                                        Register recv, Label* update_done) {
  for (uint i = 0; i < ReceiverTypeData::row_limit(); i++) {
    Label next_test;
    // See if the receiver is receiver[n].
    __ ld8(t1, Address(mdo, md->byte_offset_of_slot(data, ReceiverTypeData::receiver_offset(i))));
    __ bne(recv, t1, next_test);
    increment_mdo(mdo, md->byte_offset_of_slot(data, ReceiverTypeData::receiver_count_offset(i)));
    __ j(*update_done);
    __ bind(next_test);
  }

  // Didn't find receiver; find next empty slot and fill it in
  for (uint i = 0; i < ReceiverTypeData::row_limit(); i++) {
    Label next_test;
    Address recv_addr(mdo, md->byte_offset_of_slot(data, ReceiverTypeData::receiver_offset(i)));
    __ ld8(t1, recv_addr);
    __ bnez(t1, next_test);
    __ st8(recv_addr, recv, t0);
    __ mov_immediate(t1, DataLayout::counter_increment);
    __ st8(Address(mdo, md->byte_offset_of_slot(data, ReceiverTypeData::receiver_count_offset(i))), t1, t0);
    __ j(*update_done);
    __ bind(next_test);
  }
}

void LIR_Assembler::data_check(LIR_OpTypeCheck *op, ciMethodData **md, ciProfileData **data) {
  ciMethod* method = op->profiled_method();
  assert(method != nullptr, "Should have method");
  int bci = op->profiled_bci();
  *md = method->method_data_or_null();
  guarantee(*md != nullptr, "Sanity");
  *data = ((*md)->bci_to_data(bci));
  assert(*data != nullptr, "need data for type check");
  assert((*data)->is_ReceiverTypeData(), "need ReceiverTypeData for type check");
}

// Record null_seen for a null obj and branch to obj_is_null; otherwise record
// obj's klass. Clobbers k_RInfo and klass_RInfo.
void LIR_Assembler::profile_object(ciMethodData* md, ciProfileData* data, Register obj,
                                   Register k_RInfo, Register klass_RInfo, Label* obj_is_null) {
  Register mdo = klass_RInfo;
  __ mov_metadata(mdo, md->constant_encoding());
  Label not_null;
  __ bnez(obj, not_null);
  // Object is null, update MDO and exit
  __ add_imm(t1, mdo, md->byte_offset_of_slot(data, DataLayout::flags_offset()), t0);
  __ Assembler::ld1(t0, t1);
  __ or_imm(t0, BitData::null_seen_byte_constant(), t0);
  __ Assembler::st1(t1, t0);
  __ j(*obj_is_null);
  __ bind(not_null);

  Label update_done;
  Register recv = k_RInfo;
  __ load_klass(recv, obj);
  type_profile_helper(mdo, md, data, recv, &update_done);
  increment_mdo(mdo, md->byte_offset_of_slot(data, CounterData::count_offset()));

  __ bind(update_done);
}

// The out-of-line slow subtype check (Runtime1 slow_subtype_check: sub
// klass in t2, super klass in t3, result in t2). Branches to failure_target
// on a miss; falls through on a hit.
void LIR_Assembler::slow_subtype_check(Register sub_klass, Register super_klass, Label* failure_target) {
  assert_different_registers(sub_klass, t3);
  __ mov(t3, super_klass);
  __ mov(t2, sub_klass);
  __ far_call(Runtime1::entry_for(C1StubId::slow_subtype_check_id));
  __ beqz(t2, *failure_target);
}

void LIR_Assembler::typecheck_helper_slowcheck(ciKlass *k, Register obj, Register Rtmp1,
                                               Register k_RInfo, Register klass_RInfo,
                                               Label *failure_target, Label *success_target) {
  // get object class
  // not a safepoint as obj null check happens earlier
  __ load_klass(klass_RInfo, obj);
  if (k->is_loaded()) {
    // See if we get an immediate positive hit
    __ ld8(t2, Address(klass_RInfo, int64_t(k->super_check_offset())));
    if ((juint)in_bytes(Klass::secondary_super_cache_offset()) != k->super_check_offset()) {
      __ bne(k_RInfo, t2, *failure_target);
      // successful cast, fall through to profile or jump
    } else {
      // See if we get an immediate positive hit
      __ beq(k_RInfo, t2, *success_target);
      // check for self
      __ beq(klass_RInfo, k_RInfo, *success_target);
      slow_subtype_check(klass_RInfo, k_RInfo, failure_target);
      // successful cast, fall through to profile or jump
    }
  } else {
    // perform the fast part of the checking logic
    __ check_klass_subtype_fast_path(klass_RInfo, k_RInfo, Rtmp1, success_target, failure_target, nullptr);
    // call out-of-line instance of __ check_klass_subtype_slow_path(...)
    slow_subtype_check(klass_RInfo, k_RInfo, failure_target);
    // successful cast, fall through to profile or jump
  }
}

void LIR_Assembler::typecheck_loaded(LIR_OpTypeCheck *op, ciKlass* k, Register k_RInfo) {
  if (!k->is_loaded()) {
    klass2reg_with_patching(k_RInfo, op->info_for_patch());
  } else {
    __ mov_metadata(k_RInfo, k->constant_encoding());
  }
}

static void select_different_registers(Register preserve,
                                       Register extra,
                                       Register &tmp1,
                                       Register &tmp2,
                                       Register &tmp3) {
  if (tmp1 == preserve) {
    assert_different_registers(tmp1, tmp2, tmp3, extra);
    tmp1 = extra;
  } else if (tmp2 == preserve) {
    assert_different_registers(tmp1, tmp2, tmp3, extra);
    tmp2 = extra;
  } else if (tmp3 == preserve) {
    assert_different_registers(tmp1, tmp2, tmp3, extra);
    tmp3 = extra;
  }
  assert_different_registers(preserve, tmp1, tmp2, tmp3);
}

void LIR_Assembler::emit_typecheck_helper(LIR_OpTypeCheck *op, Label* success, Label* failure, Label* obj_is_null) {
  Register obj = op->object()->as_register();
  Register k_RInfo = op->tmp1()->as_register();
  Register klass_RInfo = op->tmp2()->as_register();
  Register dst = op->result_opr()->as_register();
  ciKlass* k = op->klass();
  Register Rtmp1 = op->tmp3()->as_register();

  // check if it needs to be profiled
  ciMethodData* md = nullptr;
  ciProfileData* data = nullptr;
  const bool should_profile = op->should_profile();
  if (should_profile) {
    data_check(op, &md, &data);
  }
  Label* success_target = success;
  Label* failure_target = failure;

  if (obj == k_RInfo) {
    k_RInfo = dst;
  } else if (obj == klass_RInfo) {
    klass_RInfo = dst;
  }
  select_different_registers(obj, dst, k_RInfo, klass_RInfo, Rtmp1);

  assert_different_registers(obj, k_RInfo, klass_RInfo);

  if (should_profile) {
    profile_object(md, data, obj, k_RInfo, klass_RInfo, obj_is_null);
  } else {
    __ beqz(obj, *obj_is_null);
  }

  typecheck_loaded(op, k, k_RInfo);
  __ verify_oop(obj);

  if (op->fast_check()) {
    // get object class
    // not a safepoint as obj null check happens earlier
    __ load_klass(t2, obj);
    __ bne(t2, k_RInfo, *failure_target);
    // successful cast, fall through to profile or jump
  } else {
    typecheck_helper_slowcheck(k, obj, Rtmp1, k_RInfo, klass_RInfo, failure_target, success_target);
  }

  __ j(*success);
}

void LIR_Assembler::emit_opTypeCheck(LIR_OpTypeCheck* op) {
  LIR_Code code = op->code();
  if (code == lir_store_check) {
    typecheck_lir_store(op);
  } else if (code == lir_checkcast) {
    Register obj = op->object()->as_register();
    Register dst = op->result_opr()->as_register();
    Label success;
    emit_typecheck_helper(op, &success, op->stub()->entry(), &success);
    __ bind(success);
    if (dst != obj) {
      __ mov(dst, obj);
    }
  } else if (code == lir_instanceof) {
    Register dst = op->result_opr()->as_register();
    Label success, failure, done;
    emit_typecheck_helper(op, &success, &failure, &failure);
    __ bind(failure);
    __ mov(dst, zr);
    __ j(done);
    __ bind(success);
    __ mov(dst, (int64_t)1);
    __ bind(done);
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::typecheck_lir_store(LIR_OpTypeCheck* op) {
  Register value = op->object()->as_register();
  Register array = op->array()->as_register();
  Register k_RInfo = op->tmp1()->as_register();
  Register klass_RInfo = op->tmp2()->as_register();
  Register Rtmp1 = op->tmp3()->as_register();

  CodeStub* stub = op->stub();

  // check if it needs to be profiled
  ciMethodData* md = nullptr;
  ciProfileData* data = nullptr;
  const bool should_profile = op->should_profile();
  if (should_profile) {
    data_check(op, &md, &data);
  }

  Label  done;
  Label* success_target = &done;
  Label* failure_target = stub->entry();

  if (should_profile) {
    profile_object(md, data, value, k_RInfo, klass_RInfo, &done);
  } else {
    __ beqz(value, done);
  }

  // The array's klass load is the null check of the array.
  __ adds(t2, oopDesc::klass_offset_in_bytes(), array);
  add_debug_info_for_null_check_here(op->info_for_exception());
  if (UseCompressedClassPointers) {
    __ Assembler::ld4(k_RInfo, t2);
    __ decode_klass_not_null(k_RInfo);
  } else {
    __ Assembler::ld8(k_RInfo, t2);
  }
  __ load_klass(klass_RInfo, value);

  // get instance klass (it's already uncompressed)
  __ ld8(k_RInfo, Address(k_RInfo, ObjArrayKlass::element_klass_offset()));
  // perform the fast part of the checking logic
  __ check_klass_subtype_fast_path(klass_RInfo, k_RInfo, Rtmp1, success_target, failure_target, nullptr);
  // call out-of-line instance of __ check_klass_subtype_slow_path(...)
  slow_subtype_check(klass_RInfo, k_RInfo, failure_target);

  __ bind(done);
}

// ---- atomics ------------------------------------------------------------------

// Java's compare-and-set is a full two-way fence: mf before (nothing earlier
// may pass the store), cmpxchg.acq (nothing later may pass the access).
// cmpxchg compares the zero-extended memory value with ar.ccv, so an int's
// expected value is zero-extended first. result = 1 on success, else 0.
void LIR_Assembler::emit_compare_and_swap(LIR_OpCompareAndSwap* op) {
  Register addr;
  if (op->addr()->is_register()) {
    addr = as_reg(op->addr());
  } else {
    assert(op->addr()->is_address(), "what else?");
    LIR_Address* addr_ptr = op->addr()->as_address_ptr();
    addr = addr_reg(addr_ptr, t0);
  }
  Register newval = as_reg(op->new_value());
  Register cmpval = as_reg(op->cmp_value());

  __ mf();
  if (op->code() == lir_cas_int) {
    __ zxt4(t2, cmpval);
    __ mov_to_ar_ccv(t2);
    __ cmpxchg4_acq(t1, addr, newval);
  } else {
    assert(!UseCompressedOops, "IA-64: compressed oops are off");
    __ mov(t2, cmpval);
    __ mov_to_ar_ccv(t2);
    __ cmpxchg8_acq(t1, addr, newval);
  }
  __ cmp_eq(ptmp0, ptmp1, t1, t2);
  if (op->result_opr()->is_valid()) {
    Register res = as_reg(op->result_opr());
    __ adds(res, 1, zr, ptmp0);
    __ mov(res, zr, ptmp1);
  }
}

void LIR_Assembler::atomic_op(LIR_Code code, LIR_Opr src, LIR_Opr data, LIR_Opr dest, LIR_Opr tmp_op) {
  BasicType type = src->type();
  bool is_int = (type == T_INT);
  assert(!(is_reference_type(type) && UseCompressedOops), "IA-64: compressed oops are off");
  Register addr = addr_reg(src->as_address_ptr(), t0);
  Register dst = as_reg(dest);

  __ mf();
  switch (code) {
    case lir_xadd: {
      // A compare-and-exchange loop: fetchadd takes only +-1, 4, 8 or 16.
      Label retry;
      __ bind(retry);
      if (is_int) {
        __ Assembler::ld4(t2, addr);
      } else {
        __ Assembler::ld8(t2, addr);
      }
      if (data->is_constant()) {
        __ add_imm(t3, t2, as_long(data), t1);
      } else {
        __ add(t3, t2, as_reg(data));
      }
      __ mov_to_ar_ccv(t2);
      if (is_int) {
        __ cmpxchg4_acq(t4, addr, t3);
      } else {
        __ cmpxchg8_acq(t4, addr, t3);
      }
      __ bne(t4, t2, retry);
      __ mov(dst, t2);
      if (is_int) {
        int_result(dst);
      }
      break;
    }
    case lir_xchg: {
      Register obj = as_reg(data);
      assert_different_registers(obj, addr);
      if (is_int) {
        __ xchg4(dst, addr, obj);
        int_result(dst);
      } else {
        __ xchg8(dst, addr, obj);
      }
      break;
    }
    default:
      ShouldNotReachHere();
  }
  __ mf();
}

// ---- intrinsics, logic, shifts ----------------------------------------------

void LIR_Assembler::intrinsic_op(LIR_Code code, LIR_Opr value, LIR_Opr unused, LIR_Opr dest, LIR_Op* op) {
  switch (code) {
    case lir_abs:  __ fabs(dest->as_double_reg(), value->as_double_reg()); break;
    case lir_sqrt: __ fsqrt_d(dest->as_double_reg(), value->as_double_reg()); break;
    default:       ShouldNotReachHere();
  }
}

void LIR_Assembler::logic_op_reg(Register dst, Register left, Register right, LIR_Code code) {
  switch (code) {
    case lir_logic_and: __ and_(dst, left, right); break;
    case lir_logic_or:  __ or_ (dst, left, right); break;
    case lir_logic_xor: __ xor_(dst, left, right); break;
    default:            ShouldNotReachHere();
  }
}

// Bitwise operations of sign-extended ints are sign-extended.
void LIR_Assembler::logic_op(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dst) {
  assert(left->is_single_cpu() || left->is_double_cpu(), "expect single or double register");
  Register Rleft = as_reg(left);
  Register Rdst = as_reg(dst);
  if (right->is_constant()) {
    int64_t c = dst->is_single_cpu() ? (int64_t)right->as_jint() : (int64_t)right->as_jlong();
    switch (code) {
      case lir_logic_and: __ and_imm(Rdst, c, Rleft); break;
      case lir_logic_or:  __ or_imm (Rdst, c, Rleft); break;
      case lir_logic_xor: __ xor_imm(Rdst, c, Rleft); break;
      default:            ShouldNotReachHere();
    }
  } else {
    logic_op_reg(Rdst, Rleft, as_reg(right), code);
  }
}

void LIR_Assembler::comp_fl2i(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dst, LIR_Op2* op) {
  if (code == lir_cmp_fd2i || code == lir_ucmp_fd2i) {
    bool is_unordered_less = (code == lir_ucmp_fd2i);
    if (left->is_single_fpu()) {
      __ float_cmp(true, is_unordered_less ? -1 : 1,
                   left->as_float_reg(), right->as_float_reg(), dst->as_register());
    } else if (left->is_double_fpu()) {
      __ float_cmp(false, is_unordered_less ? -1 : 1,
                   left->as_double_reg(), right->as_double_reg(), dst->as_register());
    } else {
      ShouldNotReachHere();
    }
  } else if (code == lir_cmp_l2i) {
    // -1, 0, 1; the predicates are computed before dst is written.
    Register a = left->as_register_lo();
    Register b = right->as_register_lo();
    Register d = dst->as_register();
    __ cmp_lt(p8, p9, a, b);
    __ cmp_lt(p6, p7, b, a);
    __ mov(d, zr);
    __ adds(d, -1, zr, p8);
    __ adds(d, 1, zr, p6);
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::shift_op(LIR_Code code, LIR_Opr left, LIR_Opr count, LIR_Opr dest, LIR_Opr tmp) {
  Register left_reg = as_reg(left);
  Register dest_reg = as_reg(dest);
  Register count_reg = count->as_register();
  if (dest->is_single_cpu()) {
    assert (dest->type() == T_INT, "unexpected result type");
    assert (left->type() == T_INT, "unexpected left type");
    __ and_imm(t1, 31, count_reg);
    switch (code) {
      case lir_shl:  __ shl(dest_reg, left_reg, t1); int_result(dest_reg); break;
      case lir_shr:  __ shr(dest_reg, int_operand(left_reg), t1); break;
      case lir_ushr: __ zxt4(t2, left_reg); __ shru(dest_reg, t2, t1); int_result(dest_reg); break;
      default: ShouldNotReachHere();
    }
  } else if (dest->is_double_cpu()) {
    __ and_imm(t1, 63, count_reg);
    switch (code) {
      case lir_shl:  __ shl(dest_reg, left_reg, t1); break;
      case lir_shr:  __ shr(dest_reg, left_reg, t1); break;
      case lir_ushr: __ shru(dest_reg, left_reg, t1); break;
      default: ShouldNotReachHere();
    }
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::shift_op(LIR_Code code, LIR_Opr left, jint count, LIR_Opr dest) {
  Register left_reg = as_reg(left);
  Register dest_reg = as_reg(dest);
  if (dest->is_single_cpu()) {
    assert (dest->type() == T_INT, "unexpected result type");
    assert (left->type() == T_INT, "unexpected left type");
    count &= 0x1f;
    if (count != 0) {
      switch (code) {
        case lir_shl:  __ shl_imm(dest_reg, left_reg, count); int_result(dest_reg); break;
        case lir_shr:  __ extr(dest_reg, left_reg, count, 32 - count); break;   // reads bits 0-31 only
        case lir_ushr: __ extr_u(dest_reg, left_reg, count, 32 - count); break;
        default: ShouldNotReachHere();
      }
    } else {
      move_regs(left_reg, dest_reg);
    }
  } else if (dest->is_double_cpu()) {
    count &= 0x3f;
    if (count != 0) {
      switch (code) {
        case lir_shl:  __ shl_imm(dest_reg, left_reg, count); break;
        case lir_shr:  __ shr_imm(dest_reg, left_reg, count); break;
        case lir_ushr: __ shru_imm(dest_reg, left_reg, count); break;
        default: ShouldNotReachHere();
      }
    } else {
      move_regs(left_reg, dest_reg);
    }
  } else {
    ShouldNotReachHere();
  }
}

// ---- calls --------------------------------------------------------------------

void LIR_Assembler::align_call(LIR_Code code) {
  // Every IA-64 instruction is a bundle, and a call's destination lives in
  // an aligned data cell (MacroAssembler::far_call): nothing to align.
}

void LIR_Assembler::call(LIR_OpJavaCall* op, relocInfo::relocType rtype) {
  RelocationHolder rspec;
  switch (rtype) {
    case relocInfo::static_call_type:      rspec = static_call_Relocation::spec(); break;
    case relocInfo::opt_virtual_call_type: rspec = opt_virtual_call_Relocation::spec(); break;
    default: ShouldNotReachHere();
  }
  __ far_call(op->addr(), rspec);
  add_call_info(code_offset(), op->info());
  __ set_poll_word_register();
}

// The inline cache: the CompiledICData* in a movl to t1 (nmethod::
// finalize_relocations fills it in before the nmethod is published), the
// call through its cell. The callee's UEP checks the receiver against it
// (MacroAssembler::ic_check).
void LIR_Assembler::ic_call(LIR_OpJavaCall* op) {
  address movl_pc = __ pc();
  __ movl(t1, (uint64_t)(uintptr_t)Universe::non_oop_word());
  __ far_call(op->addr(), virtual_call_Relocation::spec(movl_pc));
  add_call_info(code_offset(), op->info());
  __ set_poll_word_register();
}

void LIR_Assembler::emit_static_call_stub() {
  address call_pc = __ pc();
  address stub = __ start_a_stub(call_stub_size());
  if (stub == nullptr) {
    bailout("static call stub overflow");
    return;
  }

  int start = __ offset();

  __ relocate(static_stub_Relocation::spec(call_pc));
  __ emit_static_call_stub();

  assert(__ offset() - start <= call_stub_size(), "stub too big");
  __ end_a_stub();
}

void LIR_Assembler::throw_op(LIR_Opr exceptionPC, LIR_Opr exceptionOop, CodeEmitInfo* info) {
  assert(exceptionOop->as_register() == Rexception, "must match");
  assert(exceptionPC->as_register() == Rexception_pc, "must match");

  // exception object is not added to oop map by LinearScan
  // (LinearScan assumes that no oops are in fixed registers)
  info->add_register_oop(exceptionOop);
  C1StubId unwind_id;

  // get current pc information
  // pc is only needed if the method has an exception handler, the unwind code does not need it.
  if (compilation()->debug_info_recorder()->last_pc_offset() == __ offset()) {
    // As no instructions have been generated yet for this LIR node it's
    // possible that an oop map already exists for the current offset.
    // In that case insert an dummy NOP here to ensure all oop map PCs
    // are unique. See JDK-8237483.
    __ nop();
  }
  int pc_for_athrow_offset = __ offset();
  __ mov_from_ip(exceptionPC->as_register());   // this bundle's own address
  add_call_info(pc_for_athrow_offset, info); // for exception handler

  __ verify_not_null_oop(Rexception);
  // search an exception handler (r8: exception oop, r28: throwing pc)
  if (compilation()->has_fpu_code()) {
    unwind_id = C1StubId::handle_exception_id;
  } else {
    unwind_id = C1StubId::handle_exception_nofpu_id;
  }
  __ far_call(Runtime1::entry_for(unwind_id));
  __ nop();
}

void LIR_Assembler::unwind_op(LIR_Opr exceptionOop) {
  assert(exceptionOop->as_register() == Rexception, "must match");
  __ j(_unwind_handler_entry);
}

// ---- monitors, klass loads, profiling --------------------------------------------

void LIR_Assembler::emit_lock(LIR_OpLock* op) {
  Register obj = op->obj_opr()->as_register();  // may not be an oop
  Register hdr = op->hdr_opr()->as_register();
  Register lock = op->lock_opr()->as_register();
  Register temp = op->scratch_opr()->as_register();
  if (LockingMode == LM_MONITOR) {
    if (op->info() != nullptr) {
      add_debug_info_for_null_check_here(op->info());
      __ null_check(obj, -1);
    }
    __ j(*op->stub()->entry());
  } else if (op->code() == lir_lock) {
    assert(BasicLock::displaced_header_offset_in_bytes() == 0, "lock_reg must point to the displaced header");
    // add debug info for NullPointerException only if one is possible
    int null_check_offset = __ lock_object(hdr, obj, lock, temp, *op->stub()->entry());
    if (op->info() != nullptr) {
      add_debug_info_for_null_check(null_check_offset, op->info());
    }
  } else if (op->code() == lir_unlock) {
    assert(BasicLock::displaced_header_offset_in_bytes() == 0, "lock_reg must point to the displaced header");
    __ unlock_object(hdr, obj, lock, temp, *op->stub()->entry());
  } else {
    Unimplemented();
  }
  __ bind(*op->stub()->continuation());
}

void LIR_Assembler::emit_load_klass(LIR_OpLoadKlass* op) {
  Register obj = op->obj()->as_pointer_register();
  Register result = op->result_opr()->as_pointer_register();
  assert(!UseCompactObjectHeaders, "IA-64: compact object headers not yet supported");

  // The address first, so the null check is the load's own pc.
  __ adds(t2, oopDesc::klass_offset_in_bytes(), obj);
  CodeEmitInfo* info = op->info();
  if (info != nullptr) {
    add_debug_info_for_null_check_here(info);
  }
  if (UseCompressedClassPointers) {
    __ Assembler::ld4(result, t2);
    __ decode_klass_not_null(result);
  } else {
    __ Assembler::ld8(result, t2);
  }
}

void LIR_Assembler::emit_profile_call(LIR_OpProfileCall* op) {
  ciMethod* method = op->profiled_method();
  int bci          = op->profiled_bci();

  // Update counter for all call types
  ciMethodData* md = method->method_data_or_null();
  guarantee(md != nullptr, "Sanity");
  ciProfileData* data = md->bci_to_data(bci);
  assert(data != nullptr && data->is_CounterData(), "need CounterData for calls");
  assert(op->mdo()->is_single_cpu(),  "mdo must be allocated");
  Register mdo  = op->mdo()->as_register();
  __ mov_metadata(mdo, md->constant_encoding());
  int counter_offset = md->byte_offset_of_slot(data, CounterData::count_offset());
  // Perform additional virtual call profiling for invokevirtual and
  // invokeinterface bytecodes
  if (op->should_profile_receiver_type()) {
    assert(op->recv()->is_single_cpu(), "recv must be allocated");
    Register recv = op->recv()->as_register();
    assert_different_registers(mdo, recv);
    assert(data->is_VirtualCallData(), "need VirtualCallData for virtual calls");
    ciKlass* known_klass = op->known_holder();
    if (C1OptimizeVirtualCallProfiling && known_klass != nullptr) {
      // We know the type that will be seen at this call site; we can
      // statically update the MethodData* rather than needing to do
      // dynamic tests on the receiver type
      ciVirtualCallData* vc_data = (ciVirtualCallData*) data;
      uint i;
      for (i = 0; i < VirtualCallData::row_limit(); i++) {
        ciKlass* receiver = vc_data->receiver(i);
        if (known_klass->equals(receiver)) {
          increment_mdo(mdo, md->byte_offset_of_slot(data, VirtualCallData::receiver_count_offset(i)));
          return;
        }
      }

      // Receiver type not found in profile data; select an empty slot
      // Note that this is less efficient than it should be because it
      // always does a write to the receiver part of the
      // VirtualCallData rather than just the first time
      for (i = 0; i < VirtualCallData::row_limit(); i++) {
        ciKlass* receiver = vc_data->receiver(i);
        if (receiver == nullptr) {
          __ mov_metadata(t1, known_klass->constant_encoding());
          __ st8(Address(mdo, md->byte_offset_of_slot(data, VirtualCallData::receiver_offset(i))), t1, t0);
          increment_mdo(mdo, md->byte_offset_of_slot(data, VirtualCallData::receiver_count_offset(i)));
          return;
        }
      }
    } else {
      __ load_klass(recv, recv);
      Label update_done;
      type_profile_helper(mdo, md, data, recv, &update_done);
      // Receiver did not match any saved receiver and there is no empty row for it.
      // Increment total counter to indicate polymorphic case.
      increment_mdo(mdo, counter_offset);

      __ bind(update_done);
    }
  } else {
    // Static call
    increment_mdo(mdo, counter_offset);
  }
}

void LIR_Assembler::emit_delay(LIR_OpDelay*) { Unimplemented(); }

void LIR_Assembler::monitor_address(int monitor_no, LIR_Opr dst) {
  __ lea(dst->as_register(), frame_map()->address_for_monitor_lock(monitor_no));
}

void LIR_Assembler::emit_updatecrc32(LIR_OpUpdateCRC32* op) {
  // UseCRC32Intrinsics is off on IA-64.
  Unimplemented();
}

// Argument/return/parameter type profiling (TypeProfileLevel), after riscv.
// The type entry's address is materialised once (mdo, t2), so every access
// below is a plain [mdo]; t0/t1 are the scratch registers.

// [mdo] |= bits
static void or_type_entry(MacroAssembler* masm, Register mdo, int bits) {
  masm->Assembler::ld8(t1, mdo);
  masm->or_imm(t1, bits, t1);
  masm->Assembler::st8(mdo, t1);
}

void LIR_Assembler::check_conflict(ciKlass* exact_klass, intptr_t current_klass,
                                   Register tmp, Label &next, Label &none, Register mdo) {
  if (exact_klass == nullptr || TypeEntries::is_type_none(current_klass)) {
    if (exact_klass != nullptr) {
      __ mov_metadata(tmp, exact_klass->constant_encoding());
    } else {
      __ load_klass(tmp, tmp);
    }

    __ Assembler::ld8(t1, mdo);
    __ xor_(tmp, tmp, t1);
    __ and_imm(t0, TypeEntries::type_klass_mask, tmp);
    // klass seen before, nothing to do. The unknown bit may have been
    // set already but no need to check.
    __ beqz(t0, next);

    // already unknown. Nothing to do anymore.
    __ tbit_nz(ptmp0, ptmp1, tmp, exact_log2(TypeEntries::type_unknown));
    __ br_cond(next, ptmp0);

    if (TypeEntries::is_type_none(current_klass)) {
      __ beqz(t1, none);
      __ cmp_eq_imm(ptmp0, ptmp1, TypeEntries::null_seen, t1);
      __ br_cond(none, ptmp0);
      // There is a chance that the checks above
      // fail if another thread has just set the
      // profiling to this obj's klass
      __ mf();
      __ xor_(tmp, tmp, t1); // get back original value before XOR
      __ Assembler::ld8(t1, mdo);
      __ xor_(tmp, tmp, t1);
      __ and_imm(t0, TypeEntries::type_klass_mask, tmp);
      __ beqz(t0, next);
    }
  } else {
    assert(ciTypeEntries::valid_ciklass(current_klass) != nullptr &&
           ciTypeEntries::valid_ciklass(current_klass) != exact_klass, "conflict only");

    __ Assembler::ld8(tmp, mdo);
    // already unknown. Nothing to do anymore.
    __ tbit_nz(ptmp0, ptmp1, tmp, exact_log2(TypeEntries::type_unknown));
    __ br_cond(next, ptmp0);
  }

  // different than before. Cannot keep accurate profile.
  or_type_entry(_masm, mdo, TypeEntries::type_unknown);

  if (TypeEntries::is_type_none(current_klass)) {
    __ j(next);

    __ bind(none);
    // first time here. Set profile type.
    __ Assembler::st8(mdo, tmp);
  }
}

void LIR_Assembler::check_no_conflict(ciKlass* exact_klass, intptr_t current_klass, Register tmp,
                                      Register mdo, Label &next) {
  // There's a single possible klass at this profile point
  assert(exact_klass != nullptr, "should be");
  if (TypeEntries::is_type_none(current_klass)) {
    __ mov_metadata(tmp, exact_klass->constant_encoding());
    __ Assembler::ld8(t1, mdo);
    __ xor_(tmp, tmp, t1);
    __ and_imm(t0, TypeEntries::type_klass_mask, tmp);
    __ beqz(t0, next);
#ifdef ASSERT
    {
      Label ok;
      __ Assembler::ld8(t0, mdo);
      __ beqz(t0, ok);
      __ cmp_eq_imm(ptmp0, ptmp1, TypeEntries::null_seen, t0);
      __ br_cond(ok, ptmp0);
      // may have been set by another thread
      __ mf();
      __ mov_metadata(t0, exact_klass->constant_encoding());
      __ Assembler::ld8(t1, mdo);
      __ xor_(t1, t0, t1);
      __ and_imm(t1, TypeEntries::type_mask, t1);
      __ beqz(t1, ok);

      __ stop("unexpected profiling mismatch");
      __ bind(ok);
    }
#endif
    // first time here. Set profile type.
    __ Assembler::st8(mdo, tmp);
  } else {
    assert(ciTypeEntries::valid_ciklass(current_klass) != nullptr &&
           ciTypeEntries::valid_ciklass(current_klass) != exact_klass, "inconsistent");

    __ Assembler::ld8(tmp, mdo);
    // already unknown. Nothing to do anymore.
    __ tbit_nz(ptmp0, ptmp1, tmp, exact_log2(TypeEntries::type_unknown));
    __ br_cond(next, ptmp0);

    __ or_imm(tmp, TypeEntries::type_unknown, tmp);
    __ Assembler::st8(mdo, tmp);
  }
}

void LIR_Assembler::check_exact_klass(Register tmp, ciKlass* exact_klass) {
  Label ok;
  __ load_klass(tmp, tmp);
  __ mov_metadata(t1, exact_klass->constant_encoding());
  __ beq(tmp, t1, ok);
  __ stop("exact klass and actual klass differ");
  __ bind(ok);
}

void LIR_Assembler::check_null(Register tmp, Label &update, intptr_t current_klass,
                               Register mdo, bool do_update, Label &next) {
  __ bnez(tmp, update);
  if (!TypeEntries::was_null_seen(current_klass)) {
    or_type_entry(_masm, mdo, TypeEntries::null_seen);
  }
  if (do_update) {
    __ j(next);
  }
}

void LIR_Assembler::emit_profile_type(LIR_OpProfileType* op) {
  COMMENT("emit_profile_type {");
  Register obj = op->obj()->as_register();
  Register tmp = op->tmp()->as_pointer_register();
  ciKlass* exact_klass = op->exact_klass();
  intptr_t current_klass = op->current_klass();
  bool not_null = op->not_null();
  bool no_conflict = op->no_conflict();

  Label update, next, none;

  bool do_null = !not_null;
  bool exact_klass_set = exact_klass != nullptr && ciTypeEntries::valid_ciklass(current_klass) == exact_klass;
  bool do_update = !TypeEntries::is_type_unknown(current_klass) && !exact_klass_set;

  assert(do_null || do_update, "why are we here?");
  assert(!TypeEntries::was_null_seen(current_klass) || do_update, "why are we here?");
  assert_different_registers(tmp, t0, t1, t2);

  // The type entry's address, once.
  Register mdo = addr_reg(op->mdp()->as_address_ptr(), t2);
  if (mdo != t2) {
    __ mov(t2, mdo);
    mdo = t2;
  }

  __ verify_oop(obj);

  if (tmp != obj) {
    __ mov(tmp, obj);
  }
  if (do_null) {
    check_null(tmp, update, current_klass, mdo, do_update, next);
#ifdef ASSERT
  } else {
    __ bnez(tmp, update);
    __ stop("unexpected null obj");
#endif
  }

  __ bind(update);

  if (do_update) {
#ifdef ASSERT
    if (exact_klass != nullptr) {
      // Leaves tmp holding the klass; the code below reloads it.
      check_exact_klass(tmp, exact_klass);
    }
#endif
    if (!no_conflict) {
      check_conflict(exact_klass, current_klass, tmp, next, none, mdo);
    } else {
      check_no_conflict(exact_klass, current_klass, tmp, mdo, next);
    }

    __ bind(next);
  }
  COMMENT("} emit_profile_type");
}

void LIR_Assembler::align_backward_branch_target() { }

void LIR_Assembler::negate(LIR_Opr left, LIR_Opr dest, LIR_Opr tmp) {
  // tmp must be unused
  assert(tmp->is_illegal(), "wasting a register if tmp is allocated");

  if (left->is_single_cpu()) {
    assert(dest->is_single_cpu(), "expect single result reg");
    __ neg(dest->as_register(), left->as_register());
    int_result(dest->as_register());
  } else if (left->is_double_cpu()) {
    assert(dest->is_double_cpu(), "expect double result reg");
    __ neg(dest->as_register_lo(), left->as_register_lo());
  } else if (left->is_single_fpu()) {
    assert(dest->is_single_fpu(), "expect single float result reg");
    __ fneg(dest->as_float_reg(), left->as_float_reg());
  } else {
    assert(left->is_double_fpu(), "expect double float operand reg");
    assert(dest->is_double_fpu(), "expect double float result reg");
    __ fneg(dest->as_double_reg(), left->as_double_reg());
  }
}


void LIR_Assembler::leal(LIR_Opr addr, LIR_Opr dest, LIR_PatchCode patch_code, CodeEmitInfo* info) {
  if (patch_code != lir_patch_none) {
    deoptimize_trap(info);
    return;
  }
  Register dst = dest->as_register_lo();
  Register a = addr_reg(addr->as_address_ptr(), t0);
  __ mov(dst, a);
}

// A Runtime1 stub or other generated code is called through a cell; a C
// function through its descriptor (FRAME-DESIGN.md 3.2). C leaves the upper
// bits of a narrow integer result undefined and may return a float in any
// register format, so both are normalised to what compiled code keeps.
void LIR_Assembler::rt_call(LIR_Opr result, address dest, const LIR_OprList* args, LIR_Opr tmp, CodeEmitInfo* info) {
  assert(!tmp->is_valid(), "don't need temporary");

  bool is_c = !CodeCache::contains(dest);
  if (is_c) {
    __ call_c(dest);
  } else {
    __ far_call(dest);
  }

  if (info != nullptr) {
    add_call_info_here(info);
  }

  if (is_c && result->is_valid()) {
    if (result->is_single_cpu() && result->type() == T_INT) {
      int_result(result->as_register());
    } else if (result->is_single_fpu()) {
      __ fnorm_s(result->as_float_reg(), result->as_float_reg());
    }
  }
}

void LIR_Assembler::volatile_move_op(LIR_Opr src, LIR_Opr dest, BasicType type, CodeEmitInfo* info) {
  if (dest->is_address() || src->is_address()) {
    move_op(src, dest, type, lir_patch_none, info, /* wide */ false);
  } else {
    ShouldNotReachHere();
  }
}

#ifdef ASSERT
// emit run-time assertion
void LIR_Assembler::emit_assert(LIR_OpAssert* op) {
  assert(op->code() == lir_assert, "must be");

  Label ok;
  if (op->in_opr1()->is_valid()) {
    assert(op->in_opr2()->is_valid(), "both operands must be valid");
    comp_op(op->condition(), op->in_opr1(), op->in_opr2(), op);
    __ br_cond(ok, pcond);
  } else {
    assert(op->in_opr2()->is_illegal(), "both operands must be illegal");
    assert(op->condition() == lir_cond_always, "no other conditions allowed");
  }

  if (op->halt()) {
    const char* str = __ code_string(op->msg());
    __ stop(str);
  } else {
    breakpoint();
  }
  __ bind(ok);
}
#endif

// IA-64's only standalone fence is mf (ISA-NOTES.md); MacroAssembler::membar
// makes every non-empty mask an mf.
void LIR_Assembler::membar() {
  COMMENT("membar");
  __ membar(MacroAssembler::AnyAny);
}

void LIR_Assembler::membar_acquire() {
  __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);
}

void LIR_Assembler::membar_release() {
  __ membar(MacroAssembler::LoadStore | MacroAssembler::StoreStore);
}

void LIR_Assembler::membar_loadload() {
  __ membar(MacroAssembler::LoadLoad);
}

void LIR_Assembler::membar_storestore() {
  __ membar(MacroAssembler::StoreStore);
}

void LIR_Assembler::membar_loadstore() { __ membar(MacroAssembler::LoadStore); }

void LIR_Assembler::membar_storeload() { __ membar(MacroAssembler::StoreLoad); }

void LIR_Assembler::on_spin_wait() {
  __ nop();
}

void LIR_Assembler::get_thread(LIR_Opr result_reg) {
  __ mov(result_reg->as_register(), Rthread);
}

// Not a peephole optimizer: the per-method packing decision, made before
// each block is emitted. A method with unsafe accesses is not packed -- the
// SIGBUS handler resumes an unsafe access at the next bundle
// (handle_unsafe_access), which would skip instructions packed after it.
void LIR_Assembler::peephole(LIR_List *lir) {
  _masm->set_pack_default(!compilation()->has_unsafe_access());
}

int LIR_Assembler::array_element_size(BasicType type) const {
  int elem_size = type2aelembytes(type);
  return exact_log2(elem_size);
}

// FP constants are materialised with setf (const2reg); the constant section
// is not used.
address LIR_Assembler::float_constant(float f) { ShouldNotCallThis(); return nullptr; }
address LIR_Assembler::double_constant(double d) { ShouldNotCallThis(); return nullptr; }
address LIR_Assembler::int_constant(jlong n) { ShouldNotCallThis(); return nullptr; }

void LIR_Assembler::deoptimize_trap(CodeEmitInfo *info) {
  address target = nullptr;

  switch (patching_id(info)) {
    case PatchingStub::access_field_id:
      target = Runtime1::entry_for(C1StubId::access_field_patching_id);
      break;
    case PatchingStub::load_klass_id:
      target = Runtime1::entry_for(C1StubId::load_klass_patching_id);
      break;
    case PatchingStub::load_mirror_id:
      target = Runtime1::entry_for(C1StubId::load_mirror_patching_id);
      break;
    case PatchingStub::load_appendix_id:
      target = Runtime1::entry_for(C1StubId::load_appendix_patching_id);
      break;
    default: ShouldNotReachHere();
  }

  __ far_call(target);
  add_call_info_here(info);
}

// The C1 runtime stubs take their arguments in registers on IA-64
// (C1_MacroAssembler::stub_arg0/1); nothing stores stub parameters.
void LIR_Assembler::store_parameter(Register r, int offset_from_rsp_in_words) {
  ShouldNotCallThis();
}

void LIR_Assembler::store_parameter(jint c, int offset_from_rsp_in_words) {
  ShouldNotCallThis();
}

#undef __
