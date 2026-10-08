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
#include "code/codeBlob.hpp"
#include "code/compiledIC.hpp"
#include "code/debugInfoRec.hpp"
#include "code/vtableStubs.hpp"
#include "compiler/oopMap.hpp"
#include "gc/shared/barrierSetAssembler.hpp"
#include "interpreter/interp_masm.hpp"
#include "interpreter/interpreter.hpp"
#include "logging/log.hpp"
#include "memory/resourceArea.hpp"
#include "nativeInst_ia64.hpp"
#include "oops/klass.inline.hpp"
#include "oops/method.inline.hpp"
#include "prims/methodHandles.hpp"
#include "runtime/deoptimization.hpp"
#include "runtime/globals.hpp"
#include "runtime/jniHandles.hpp"
#include "runtime/safepointMechanism.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/signature.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/timerTrace.hpp"
#include "runtime/vframeArray.hpp"
#include "utilities/align.hpp"
#include "utilities/formatBuffer.hpp"
#include "vmreg_ia64.inline.hpp"

#define __ masm->

#ifdef PRODUCT
#define BLOCK_COMMENT(str) /* nothing */
#else
#define BLOCK_COMMENT(str) __ block_comment(str)
#endif

// Shared runtime for IA-64, milestone 1 (core variant).
//
// What the template interpreter needs is real: the calling conventions and
// generate_throw_exception (the interpreter's stack-overflow check jumps to
// throw_StackOverflowError). Everything that exists only for compiled code
// -- the i2c/c2i adapters' bodies, the deoptimization, safepoint-poll and
// call-resolution blobs, the JFR stubs, native wrappers -- is generated as a
// trap that names itself, so the blobs exist (shared code compares against
// and walks them at startup) but executing one stops the VM with a clear
// message. They arrive with C1.
//
// Every stub here is generated in a buffer and then copied into the code
// cache, so it must not record its own address absolutely: pcs are taken
// with la() (mov r = ip), never with movl.

// Emit a named trap: generation succeeds, execution stops the VM.
static void trap(MacroAssembler* masm, const char* what) {
  __ stop(what);
}

// ---------------------------------------------------------------------------
// Calling conventions

// The Java convention is this port's own choice (FRAME-DESIGN.md 6.2):
// integer and FP arguments counted independently, j_rarg0-7 = r20-r27 and
// j_farg0-7 = f8-f15. Values in the VMRegPair regs array refer to 4-byte
// quantities; stack slots are based off the stack pointer.
int SharedRuntime::java_calling_convention(const BasicType *sig_bt,
                                           VMRegPair *regs,
                                           int total_args_passed) {
  static const Register INT_ArgReg[Argument::n_int_register_parameters_j] = {
    j_rarg0, j_rarg1, j_rarg2, j_rarg3,
    j_rarg4, j_rarg5, j_rarg6, j_rarg7
  };
  static const FloatRegister FP_ArgReg[Argument::n_float_register_parameters_j] = {
    j_farg0, j_farg1, j_farg2, j_farg3,
    j_farg4, j_farg5, j_farg6, j_farg7
  };

  uint int_args = 0;
  uint fp_args = 0;
  uint stk_args = 0;

  for (int i = 0; i < total_args_passed; i++) {
    switch (sig_bt[i]) {
      case T_BOOLEAN: // fall through
      case T_CHAR:    // fall through
      case T_BYTE:    // fall through
      case T_SHORT:   // fall through
      case T_INT:
        if (int_args < Argument::n_int_register_parameters_j) {
          regs[i].set1(INT_ArgReg[int_args++]->as_VMReg());
        } else {
          stk_args = align_up(stk_args, 2);
          regs[i].set1(VMRegImpl::stack2reg(stk_args));
          stk_args += 1;
        }
        break;
      case T_VOID:
        // halves of T_LONG or T_DOUBLE
        assert(i != 0 && (sig_bt[i - 1] == T_LONG || sig_bt[i - 1] == T_DOUBLE), "expecting half");
        regs[i].set_bad();
        break;
      case T_LONG:      // fall through
        assert((i + 1) < total_args_passed && sig_bt[i + 1] == T_VOID, "expecting half");
      case T_OBJECT:    // fall through
      case T_ARRAY:     // fall through
      case T_ADDRESS:
        if (int_args < Argument::n_int_register_parameters_j) {
          regs[i].set2(INT_ArgReg[int_args++]->as_VMReg());
        } else {
          stk_args = align_up(stk_args, 2);
          regs[i].set2(VMRegImpl::stack2reg(stk_args));
          stk_args += 2;
        }
        break;
      case T_FLOAT:
        if (fp_args < Argument::n_float_register_parameters_j) {
          regs[i].set1(FP_ArgReg[fp_args++]->as_VMReg());
        } else {
          stk_args = align_up(stk_args, 2);
          regs[i].set1(VMRegImpl::stack2reg(stk_args));
          stk_args += 1;
        }
        break;
      case T_DOUBLE:
        assert((i + 1) < total_args_passed && sig_bt[i + 1] == T_VOID, "expecting half");
        if (fp_args < Argument::n_float_register_parameters_j) {
          regs[i].set2(FP_ArgReg[fp_args++]->as_VMReg());
        } else {
          stk_args = align_up(stk_args, 2);
          regs[i].set2(VMRegImpl::stack2reg(stk_args));
          stk_args += 2;
        }
        break;
      default:
        ShouldNotReachHere();
    }
  }

  return stk_args;
}

// The psABI C convention (see interpreterRT_ia64.hpp for the rule): every
// argument consumes the next of eight positional slots, out0-out7; an FP
// argument within them travels in the next unused FP register, f8-f15, and
// leaves its GR slot unused. Beyond eight, 8-byte stack slots. The stack
// slots returned here are relative to the outgoing area *above* the psABI
// scratch area, which out_preserve_stack_slots() accounts for.
int SharedRuntime::c_calling_convention(const BasicType *sig_bt,
                                         VMRegPair *regs,
                                         int total_args_passed) {
  static const Register INT_ArgReg[8] = {
    c_rarg0, c_rarg1, c_rarg2, c_rarg3,
    c_rarg4, c_rarg5, c_rarg6, c_rarg7
  };
  static const FloatRegister FP_ArgReg[8] = {
    c_farg0, c_farg1, c_farg2, c_farg3,
    c_farg4, c_farg5, c_farg6, c_farg7
  };

  uint slot = 0;      // positional parameter slot
  uint fp_args = 0;   // FP argument registers used
  uint stk_args = 0;  // 4-byte stack slots, two per parameter slot

  for (int i = 0; i < total_args_passed; i++) {
    switch (sig_bt[i]) {
      case T_BOOLEAN:  // fall through
      case T_CHAR:     // fall through
      case T_BYTE:     // fall through
      case T_SHORT:    // fall through
      case T_INT:
        if (slot < 8) {
          regs[i].set1(INT_ArgReg[slot++]->as_VMReg());
        } else {
          slot++;
          regs[i].set1(VMRegImpl::stack2reg(stk_args));
          stk_args += 2;
        }
        break;
      case T_LONG:      // fall through
        assert((i + 1) < total_args_passed && sig_bt[i + 1] == T_VOID, "expecting half");
      case T_OBJECT:    // fall through
      case T_ARRAY:     // fall through
      case T_ADDRESS:   // fall through
      case T_METADATA:
        if (slot < 8) {
          regs[i].set2(INT_ArgReg[slot++]->as_VMReg());
        } else {
          slot++;
          regs[i].set2(VMRegImpl::stack2reg(stk_args));
          stk_args += 2;
        }
        break;
      case T_FLOAT:
        if (slot < 8) {
          slot++;
          regs[i].set1(FP_ArgReg[fp_args++]->as_VMReg());
        } else {
          slot++;
          regs[i].set1(VMRegImpl::stack2reg(stk_args));
          stk_args += 2;
        }
        break;
      case T_DOUBLE:
        assert((i + 1) < total_args_passed && sig_bt[i + 1] == T_VOID, "expecting half");
        if (slot < 8) {
          slot++;
          regs[i].set2(FP_ArgReg[fp_args++]->as_VMReg());
        } else {
          slot++;
          regs[i].set2(VMRegImpl::stack2reg(stk_args));
          stk_args += 2;
        }
        break;
      case T_VOID: // Halves of longs and doubles
        assert(i != 0 && (sig_bt[i - 1] == T_LONG || sig_bt[i - 1] == T_DOUBLE), "expecting half");
        regs[i].set_bad();
        break;
      default:
        ShouldNotReachHere();
    }
  }

  return stk_args;
}

uint SharedRuntime::in_preserve_stack_slots() {
  return 2 * VMRegImpl::slots_per_word;
}

// The psABI scratch area at the bottom of every outgoing area: 16 bytes,
// four 4-byte slots.
uint SharedRuntime::out_preserve_stack_slots() {
  return 16 / VMRegImpl::stack_slot_size;
}

VMReg SharedRuntime::thread_register() {
  return Rthread->as_VMReg();
}

bool SharedRuntime::is_wide_vector(int size) {
  return false;
}

int SharedRuntime::vector_calling_convention(VMRegPair *regs,
                                             uint num_bits,
                                             uint total_args_passed) {
  Unimplemented();
  return 0;
}

// ---------------------------------------------------------------------------
// Adapters. The core variant has no compiled code: the interpreter never
// calls through an i2c adapter and nothing calls a c2i one. Each entry is a
// distinct trap so the AdapterHandlerEntry is well-formed.

void SharedRuntime::generate_i2c2i_adapters(MacroAssembler *masm,
                                            int total_args_passed,
                                            int comp_args_on_stack,
                                            const BasicType *sig_bt,
                                            const VMRegPair *regs,
                                            AdapterHandlerEntry* handler) {
  address i2c_entry = __ pc();
  trap(masm, "IA-64: i2c adapter (no compiled code yet)");
  address c2i_unverified_entry = __ pc();
  trap(masm, "IA-64: c2i unverified adapter (no compiled code yet)");
  address c2i_entry = __ pc();
  trap(masm, "IA-64: c2i adapter (no compiled code yet)");
  address c2i_no_clinit_check_entry = nullptr;
  handler->set_entry_points(i2c_entry, c2i_entry, c2i_unverified_entry, c2i_no_clinit_check_entry);
}

// Native wrappers are nmethods. With -Xint the only one the VM asks for is
// linkToNative's, which FFM (unsupported) needs; without -Xint the method-
// handle intrinsics need them too. Arrives with C1.
nmethod* SharedRuntime::generate_native_wrapper(MacroAssembler* masm,
                                                const methodHandle& method,
                                                int compile_id,
                                                BasicType* in_sig_bt,
                                                VMRegPair* in_regs,
                                                BasicType ret_type) {
  Unimplemented();
  return nullptr;
}

// this function returns the adjust size (in number of words) to a c2i adapter
// activation for use during deoptimization
int Deoptimization::last_frame_adjust(int callee_parameters, int callee_locals) {
  assert(callee_locals >= callee_parameters,
         "test and remove; got more parms than locals");
  if (callee_locals < callee_parameters) {
    return 0;                   // No adjustment for negative locals
  }
  int diff = (callee_locals - callee_parameters) * Interpreter::stackElementWords;
  // diff is counted in stack words
  return align_up(diff, 2);
}

// ---------------------------------------------------------------------------
// Blobs for compiled code: present, but traps.

void SharedRuntime::generate_deopt_blob() {
  ResourceMark rm;
  const char* name = SharedRuntime::stub_name(SharedStubId::deopt_id);
  CodeBuffer buffer(name, 2048, 1024);
  MacroAssembler* masm = new MacroAssembler(&buffer);
  OopMapSet* oop_maps = new OopMapSet();

  int unpack_offset = __ offset();
  trap(masm, "IA-64: deoptimization blob (unpack)");
  int reexecute_offset = __ offset();
  trap(masm, "IA-64: deoptimization blob (reexecute)");
  int exception_offset = __ offset();
  trap(masm, "IA-64: deoptimization blob (exception)");
  int exception_in_tls_offset = __ offset();
  trap(masm, "IA-64: deoptimization blob (exception in TLS)");

  masm->flush();
  _deopt_blob = DeoptimizationBlob::create(&buffer, oop_maps, unpack_offset, exception_offset,
                                           reexecute_offset, MacroAssembler::enter_frame_words());
  _deopt_blob->set_unpack_with_exception_in_tls_offset(exception_in_tls_offset);
}

SafepointBlob* SharedRuntime::generate_handler_blob(SharedStubId id, address call_ptr) {
  assert(is_polling_page_id(id), "expected a polling page stub id");
  ResourceMark rm;
  const char* name = SharedRuntime::stub_name(id);
  CodeBuffer buffer(name, 1024, 512);
  MacroAssembler* masm = new MacroAssembler(&buffer);
  OopMapSet* oop_maps = new OopMapSet();
  // Polls in this port never fault -- they are thread-local loads -- so the
  // polling-page handler is reachable only from compiled code's return
  // polls, which do not exist yet.
  trap(masm, "IA-64: safepoint handler blob (no compiled code yet)");
  masm->flush();
  return SafepointBlob::create(&buffer, oop_maps, MacroAssembler::enter_frame_words());
}

static RuntimeStub* trap_runtime_stub(const char* name, const char* what) {
  ResourceMark rm;
  CodeBuffer code(name, 512, 64);
  OopMapSet* oop_maps = new OopMapSet();
  MacroAssembler* masm = new MacroAssembler(&code);
  trap(masm, what);
  masm->flush();
  return RuntimeStub::new_runtime_stub(name, &code, CodeOffsets::frame_never_safe,
                                       MacroAssembler::enter_frame_words(), oop_maps, false);
}

RuntimeStub* SharedRuntime::generate_resolve_blob(SharedStubId id, address destination) {
  return trap_runtime_stub(SharedRuntime::stub_name(id), "IA-64: call resolution blob (no compiled code yet)");
}

#if INCLUDE_JFR
RuntimeStub* SharedRuntime::generate_jfr_write_checkpoint() {
  return trap_runtime_stub(SharedRuntime::stub_name(SharedStubId::jfr_write_checkpoint_id),
                           "IA-64: JFR write_checkpoint stub (C2 only)");
}

RuntimeStub* SharedRuntime::generate_jfr_return_lease() {
  return trap_runtime_stub(SharedRuntime::stub_name(SharedStubId::jfr_return_lease_id),
                           "IA-64: JFR return_lease stub (C2 only)");
}
#endif // INCLUDE_JFR

// ---------------------------------------------------------------------------
// Continuation point for throwing of implicit exceptions that are
// not handled in the current activation. Fabricates an exception
// oop and initiates normal exception dispatching in this
// frame. The interpreter's stack-overflow check jumps here
// (throw_StackOverflowError), so this one is real.
//
// Entered by a jump with b0 holding the return address of the frame the
// exception is to appear thrown from. The frame laid down here is enter()'s:
// linkage plus psABI scratch, four words.

RuntimeStub* SharedRuntime::generate_throw_exception(SharedStubId id, address runtime_entry) {
  assert(is_throw_id(id), "expected a throw stub id");

  const char* name = SharedRuntime::stub_name(id);
  assert(runtime_entry != nullptr, "runtime entry must exist");

  const int framesize_in_words = MacroAssembler::enter_frame_words();
  const int framesize_in_slots = framesize_in_words * VMRegImpl::slots_per_word;

  const int insts_size = 4096;
  const int locs_size  = 64;

  ResourceMark rm;
  const char* timer_msg = "SharedRuntime generate_throw_exception";
  TraceTime timer(timer_msg, TRACETIME_LOG(Info, startuptime));

  CodeBuffer code(name, insts_size, locs_size);
  OopMapSet* oop_maps  = new OopMapSet();
  MacroAssembler* masm = new MacroAssembler(&code);

  address start = __ pc();

  // This is an inlined and slightly modified version of call_VM
  // which has the ability to fetch the return PC out of
  // thread-local storage and also sets up last_Java_sp slightly
  // differently than the real call_VM

  __ enter(); // Save fp and b0 before the call

  int frame_complete = __ pc() - start;

  // Set up last_Java_sp and last_Java_fp. The recorded pc is the call's
  // return address, taken position-independently: this code is copied.
  Label the_pc;
  __ set_last_Java_frame(sp, fp, the_pc, t2);

  // Call runtime
  __ mov(c_rarg0, Rthread);
  BLOCK_COMMENT("call runtime_entry");
  __ call_c(runtime_entry);
  int the_pc_offset = __ offset();
  __ bind(the_pc);

  // Generate oop map
  OopMap* map = new OopMap(framesize_in_slots, 0);
  oop_maps->add_gc_map(the_pc_offset, map);

  __ reset_last_Java_frame(true);

  __ leave();

  // check for pending exceptions
#ifdef ASSERT
  Label L;
  __ ld8(t2, Address(Rthread, Thread::pending_exception_offset()));
  __ bnez(t2, L);
  __ should_not_reach_here();
  __ bind(L);
#endif // ASSERT
  __ far_jump(StubRoutines::forward_exception_entry());

  // codeBlob framesize is in words (not VMRegImpl::slot_size)
  RuntimeStub* stub =
    RuntimeStub::new_runtime_stub(name,
                                  &code,
                                  frame_complete,
                                  framesize_in_words,
                                  oop_maps, false);
  assert(stub != nullptr, "create runtime stub fail!");
  return stub;
}
