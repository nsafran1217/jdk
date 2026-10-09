/*
 * Copyright (c) 1999, 2025, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, Red Hat Inc. All rights reserved.
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
#include "c1/c1_CodeStubs.hpp"
#include "c1/c1_Defs.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "c1/c1_Runtime1.hpp"
#include "classfile/javaClasses.hpp"
#include "compiler/disassembler.hpp"
#include "compiler/oopMap.hpp"
#include "gc/shared/cardTable.hpp"
#include "gc/shared/cardTableBarrierSet.hpp"
#include "interpreter/interpreter.hpp"
#include "memory/universe.hpp"
#include "nativeInst_ia64.hpp"
#include "oops/oop.inline.hpp"
#include "prims/jvmtiExport.hpp"
#include "registerSaver_ia64.hpp"
#include "register_ia64.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/signature.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/vframe.hpp"
#include "runtime/vframeArray.hpp"
#include "utilities/powerOfTwo.hpp"
#include "vmreg_ia64.inline.hpp"

// The C1 runtime stubs for IA-64.
//
// Register conventions on entry (FRAME-DESIGN.md 11.4):
//   - stub_arg0/stub_arg1 (t2/t3, r9/r10): the arguments riscv passes on the
//     stack or in t0/t1 (c1_MacroAssembler_ia64.hpp);
//   - FrameMap::stub_klass_reg / stub_length_reg / stub_tmp1_reg: klass,
//     length or rank, and varargs of the allocation stubs, as
//     c1_LIRGenerator_ia64.cpp loads them;
//   - out0, out1: stubs reached through call_runtime (C convention);
//   - r8 / r28 (Rexception / Rexception_pc): exception oop and throwing pc.
// Results come back in r8.
//
// A stub with a frame lays down enter()'s linkage (StubFrame, or an explicit
// enter()) and then RegisterSaver's save area (registerSaver_ia64.hpp): every
// register C1 can allocate, plus the predicates, is saved and restored around
// the call into the runtime. f8-f31 are always spilled bit-exactly; they are
// described to the oop map (with memory-format double copies) unless the
// stub is a *_nofpu variant, whose callers keep nothing in FPRs.

// Implementation of StubAssembler

int StubAssembler::call_RT(Register oop_result, Register metadata_result, address entry, int args_size) {
  // setup registers
  assert(!(oop_result->is_valid() || metadata_result->is_valid()) || oop_result != metadata_result,
         "registers must be different");
  assert(oop_result != Rthread && metadata_result != Rthread, "registers must be different");
  assert(args_size >= 0, "illegal args_size");

  mov(c_rarg0, Rthread);
  set_num_rt_args(0); // Nothing on stack

  // The recorded pc is the call's return address, taken position-
  // independently: this code is generated in a buffer and copied.
  Label retaddr;
  set_last_Java_frame(sp, fp, retaddr, t2);

  // do the call
  call_c(entry);
  bind(retaddr);
  int call_offset = offset();
  reset_last_Java_frame(true);

  // check for pending exceptions
  { Label L;
    ld8(t1, Address(Rthread, Thread::pending_exception_offset()));
    beqz(t1, L);
    // exception pending => remove activation and forward to exception handler
    // make sure that the vm_results are cleared
    if (oop_result->is_valid()) {
      st8(Address(Rthread, JavaThread::vm_result_oop_offset()), zr);
    }
    if (metadata_result->is_valid()) {
      st8(Address(Rthread, JavaThread::vm_result_metadata_offset()), zr);
    }
    if (frame_size() == no_frame_size) {
      leave();
      far_jump(StubRoutines::forward_exception_entry());
    } else if (_stub_id == (int)C1StubId::forward_exception_id) {
      should_not_reach_here();
    } else {
      far_jump(Runtime1::entry_for(C1StubId::forward_exception_id));
    }
    bind(L);
  }
  // get oop results if there are any and reset the values in the thread
  if (oop_result->is_valid()) {
    get_vm_result_oop(oop_result, Rthread);
  }
  if (metadata_result->is_valid()) {
    get_vm_result_metadata(metadata_result, Rthread);
  }
  return call_offset;
}

// The moves run in order, before c_rarg0 is set: an argument may sit in an
// out register only where no earlier move overwrites it (register_finalizer
// passes c_rarg0 itself). The C1 stubs' own argument registers are never out
// registers -- C1 does not allocate r32 and up.

int StubAssembler::call_RT(Register oop_result, Register metadata_result, address entry, Register arg1) {
  mov(c_rarg1, arg1);
  return call_RT(oop_result, metadata_result, entry, 1);
}

int StubAssembler::call_RT(Register oop_result, Register metadata_result, address entry, Register arg1, Register arg2) {
  assert(arg2 != c_rarg1, "clobbered by the first move");
  mov(c_rarg1, arg1);
  mov(c_rarg2, arg2);
  return call_RT(oop_result, metadata_result, entry, 2);
}

int StubAssembler::call_RT(Register oop_result, Register metadata_result, address entry, Register arg1, Register arg2, Register arg3) {
  assert(arg2 != c_rarg1 && arg3 != c_rarg1 && arg3 != c_rarg2, "clobbered by an earlier move");
  mov(c_rarg1, arg1);
  mov(c_rarg2, arg2);
  mov(c_rarg3, arg3);
  return call_RT(oop_result, metadata_result, entry, 3);
}

enum return_state_t {
  does_not_return, requires_return, requires_pop_epilogue_return
};

// Implementation of StubFrame

class StubFrame: public StackObj {
 private:
  StubAssembler* _sasm;
  return_state_t _return_state;

 public:
  StubFrame(StubAssembler* sasm, const char* name, bool must_gc_arguments, return_state_t return_state=requires_return);
  void load_argument(int offset_in_words, Register reg);

  ~StubFrame();
};;

void StubAssembler::prologue(const char* name, bool must_gc_arguments) {
  set_info(name, must_gc_arguments);
  enter();
}

// use_pop exists for frames a continuation may have frozen; continuations
// are disabled on IA-64 (VMContinuations is false), so it is plain leave().
void StubAssembler::epilogue(bool use_pop) {
  leave();
  ret();
}

#define __ _sasm->

StubFrame::StubFrame(StubAssembler* sasm, const char* name, bool must_gc_arguments, return_state_t return_state) {
  _sasm = sasm;
  _return_state = return_state;
  __ prologue(name, must_gc_arguments);
}

// load parameters that were stored with LIR_Assembler::store_parameter
// Note: offsets for store_parameter and load_argument must match
void StubFrame::load_argument(int offset_in_words, Register reg) {
  __ load_parameter(offset_in_words, reg);
}


StubFrame::~StubFrame() {
  if (_return_state == does_not_return) {
    __ should_not_reach_here();
  } else {
    __ epilogue(_return_state == requires_pop_epilogue_return);
  }
  _sasm = nullptr;
}

#undef __


// Implementation of Runtime1

#define __ sasm->

// The register save area is RegisterSaver's (registerSaver_ia64.hpp), laid
// below a frame whose linkage the stub has already built with enter().

static OopMap* generate_oop_map(StubAssembler* sasm, bool describe_fpu_registers) {
  sasm->set_frame_size(RegisterSaver::frame_size_in_words());
  return RegisterSaver::oop_map(describe_fpu_registers);
}

static OopMap* save_live_registers(StubAssembler* sasm,
                                   bool save_fpu_registers = true) {
  __ block_comment("save_live_registers");
  int frame_words;
  OopMap* map = RegisterSaver::save_live_registers(sasm, &frame_words,
                                                   save_fpu_registers, /* with_enter */ false);
  sasm->set_frame_size(frame_words);
  return map;
}

static void restore_live_registers(StubAssembler* sasm, bool restore_fpu_registers = true) {
  __ block_comment("restore_live_registers");
  RegisterSaver::restore_live_registers(sasm, /* with_leave */ false);
}

static void restore_live_registers_except_r8(StubAssembler* sasm, bool restore_fpu_registers = true) {
  __ block_comment("restore_live_registers_except_r8");
  RegisterSaver::restore_live_registers(sasm, /* with_leave */ false, r8);
}

void Runtime1::initialize_pd() {
  // Nothing to compute: RegisterSaver's layout is static.
}

// return: offset in 64-bit words.
uint Runtime1::runtime_blob_current_thread_offset(frame f) {
  // Only continuation freezing asks, and continuations are disabled on IA-64.
  // (Rthread is r5, preserved by C, so the stubs never save it.)
  ShouldNotReachHere();
  return 0;
}

// target: the entry point of the method that creates and posts the exception oop
// has_argument: true if the exception needs arguments (passed in stub_arg0 and stub_arg1)

OopMapSet* Runtime1::generate_exception_throw(StubAssembler* sasm, address target, bool has_argument) {
  // make a frame and preserve the caller's caller-save registers
  OopMap* oop_map = save_live_registers(sasm);
  int call_offset = 0;
  if (!has_argument) {
    call_offset = __ call_RT(noreg, noreg, target);
  } else {
    call_offset = __ call_RT(noreg, noreg, target, C1_MacroAssembler::stub_arg0, C1_MacroAssembler::stub_arg1);
  }
  OopMapSet* oop_maps = new OopMapSet();
  oop_maps->add_gc_map(call_offset, oop_map);

  return oop_maps;
}

OopMapSet* Runtime1::generate_handle_exception(C1StubId id, StubAssembler *sasm) {
  __ block_comment("generate_handle_exception");

  // incoming parameters
  const Register exception_oop = Rexception;      // r8
  const Register exception_pc  = Rexception_pc;   // r28

  OopMapSet* oop_maps = new OopMapSet();
  OopMap* oop_map = nullptr;

  switch (id) {
    case C1StubId::forward_exception_id:
      // We're handling an exception in the context of a compiled frame.
      // The registers have been saved in the standard places.  Perform
      // an exception lookup in the caller and dispatch to the handler
      // if found.  Otherwise unwind and dispatch to the callers
      // exception handler.
      oop_map = generate_oop_map(sasm, true /* describe FPRs */);

      // load and clear pending exception oop into r8
      __ ld8(exception_oop, Address(Rthread, Thread::pending_exception_offset()));
      __ st8(Address(Rthread, Thread::pending_exception_offset()), zr);

      // load issuing PC (the return address for this stub) into r28
      __ ld8(exception_pc, Address(fp, frame::return_addr_offset * BytesPerWord));

      // make sure that the vm_results are cleared (may be unnecessary)
      __ st8(Address(Rthread, JavaThread::vm_result_oop_offset()), zr);
      __ st8(Address(Rthread, JavaThread::vm_result_metadata_offset()), zr);
      break;
    case C1StubId::handle_exception_nofpu_id:
    case C1StubId::handle_exception_id:
      // At this point all registers MAY be live.
      oop_map = save_live_registers(sasm, id != C1StubId::handle_exception_nofpu_id);
      break;
    case C1StubId::handle_exception_from_callee_id: {
      // At this point all registers except exception oop (r8) and
      // exception pc (r28) are dead.
      const int frame_size = MacroAssembler::enter_frame_words();
      oop_map = new OopMap(frame_size * VMRegImpl::slots_per_word, 0);
      sasm->set_frame_size(frame_size);
      break;
    }
    default: ShouldNotReachHere();
  }

  // verify that r8 contains a valid exception
  __ verify_not_null_oop(exception_oop);

#ifdef ASSERT
  // check that fields in JavaThread for exception oop and issuing pc are
  // empty before writing to them
  Label oop_empty;
  __ ld8(t2, Address(Rthread, JavaThread::exception_oop_offset()));
  __ beqz(t2, oop_empty);
  __ stop("exception oop already set");
  __ bind(oop_empty);

  Label pc_empty;
  __ ld8(t2, Address(Rthread, JavaThread::exception_pc_offset()));
  __ beqz(t2, pc_empty);
  __ stop("exception pc already set");
  __ bind(pc_empty);
#endif

  // save exception oop and issuing pc into JavaThread
  // (exception handler will load it from here)
  __ st8(Address(Rthread, JavaThread::exception_oop_offset()), exception_oop);
  __ st8(Address(Rthread, JavaThread::exception_pc_offset()), exception_pc);

  // patch throwing pc into return address (has bci & oop map)
  __ st8(Address(fp, frame::return_addr_offset * BytesPerWord), exception_pc);

  // compute the exception handler.
  // the exception oop and the throwing pc are read from the fields in JavaThread
  int call_offset = __ call_RT(noreg, noreg, CAST_FROM_FN_PTR(address, exception_handler_for_pc));
  guarantee(oop_map != nullptr, "null oop_map!");
  oop_maps->add_gc_map(call_offset, oop_map);

  // r8: handler address
  //     will be the deopt blob if nmethod was deoptimized while we looked up
  //     handler regardless of whether handler existed in the nmethod.

  // patch the return address, this stub will directly return to the exception handler
  __ st8(Address(fp, frame::return_addr_offset * BytesPerWord), r8);

  switch (id) {
    case C1StubId::forward_exception_id:
    case C1StubId::handle_exception_nofpu_id:
    case C1StubId::handle_exception_id:
      // Restore the registers that were saved at the beginning.
      restore_live_registers(sasm, id != C1StubId::handle_exception_nofpu_id);
      break;
    case C1StubId::handle_exception_from_callee_id:
      break;
    default: ShouldNotReachHere();
  }

  return oop_maps;
}


// Entered by a jump from a compiled method's unwind handler, its frame
// already removed: r8 = exception oop, b0 = the return address into the
// caller. Finds the caller's handler and continues there with r8 = exception
// oop, r28 = throwing pc (the return address) -- the same state
// StubRoutines::forward_exception leaves.
void Runtime1::generate_unwind_exception(StubAssembler *sasm) {
  // incoming parameters
  const Register exception_oop = Rexception;
  // other registers used in this stub
  const Register handler_addr = t2;

  if (AbortVMOnException) {
    int frame_words;
    RegisterSaver::save_live_registers(sasm, &frame_words, false, /* with_enter */ true);
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, check_abort_on_vm_exception), exception_oop);
    RegisterSaver::restore_live_registers(sasm, /* with_leave */ true);
  }

#ifdef ASSERT
  // check that fields in JavaThread for exception oop and issuing pc are empty
  Label oop_empty;
  __ ld8(t2, Address(Rthread, JavaThread::exception_oop_offset()));
  __ beqz(t2, oop_empty);
  __ stop("exception oop must be empty");
  __ bind(oop_empty);

  Label pc_empty;
  __ ld8(t2, Address(Rthread, JavaThread::exception_pc_offset()));
  __ beqz(t2, pc_empty);
  __ stop("exception pc must be empty");
  __ bind(pc_empty);
#endif

  // Save our return address because exception_handler_for_return_address
  // will destroy it (b0, like everything caller-saved). We also save
  // exception_oop. A small frame of our own, as in
  // StubRoutines::forward_exception: 16 bytes of psABI scratch for the callee,
  // then the two slots.
  __ mov_from_br(t2, breturn);
  __ adds(sp, -32, sp);
  __ st8(Address(sp, 16), t2);
  __ st8(Address(sp, 24), exception_oop);

  // search the exception handler address of the caller (using the return address)
  __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::exception_handler_for_return_address), Rthread, t2);
  // r8: exception handler address of the caller

  // move result of call into correct register
  __ mov(handler_addr, r8);

  // get throwing pc (= return address).
  // b0 has been destroyed by the call
  __ ld8(Rexception_pc, Address(sp, 16));
  __ ld8(exception_oop, Address(sp, 24));
  __ adds(sp, 32, sp);
  __ mov_to_br(breturn, Rexception_pc);

  __ verify_not_null_oop(exception_oop);

  // continue at exception handler (return address removed)
  // note: do *not* remove arguments when unwinding the
  //       activation since the caller assumes having
  //       all arguments on the stack when entering the
  //       runtime to determine the exception handler
  //       (GC happens at call site with arguments!)
  // r8:  exception oop
  // r28: throwing pc
  // t2:  exception handler
  __ jr(handler_addr);
}

OopMapSet* Runtime1::generate_patching(StubAssembler* sasm, address target) {
  // use the maximum number of runtime-arguments here because it is difficult to
  // distinguish each RT-Call.
  // Note: This number affects also the RT-Call in generate_handle_exception because
  //       the oop-map is shared for all calls.
  DeoptimizationBlob* deopt_blob = SharedRuntime::deopt_blob();
  assert(deopt_blob != nullptr, "deoptimization blob must have been created");

  OopMap* oop_map = save_live_registers(sasm);

  __ mov(c_rarg0, Rthread);
  Label retaddr;
  __ set_last_Java_frame(sp, fp, retaddr, t2);
  // do the call
  __ call_c(target);
  __ bind(retaddr);
  OopMapSet* oop_maps = new OopMapSet();
  oop_maps->add_gc_map(__ offset(), oop_map);
  __ reset_last_Java_frame(true);

#ifdef ASSERT
  // Check that fields in JavaThread for exception oop and issuing pc are empty
  Label oop_empty;
  __ ld8(t2, Address(Rthread, Thread::pending_exception_offset()));
  __ beqz(t2, oop_empty);
  __ stop("exception oop must be empty");
  __ bind(oop_empty);

  Label pc_empty;
  __ ld8(t2, Address(Rthread, JavaThread::exception_pc_offset()));
  __ beqz(t2, pc_empty);
  __ stop("exception pc must be empty");
  __ bind(pc_empty);
#endif

  // Runtime will return true if the nmethod has been deoptimized, this is the
  // expected scenario and anything else is an error. Note that we maintain a
  // check on the result purely as a defensive measure. The result is an int:
  // only its low 32 bits are defined.
  Label no_deopt;
  __ beqzw(r8, no_deopt);                                // Have we deoptimized?

  // Perform a re-execute. The proper return address is already on the stack,
  // we just need to restore registers, pop all of our frames but the return
  // address and jump to the deopt blob.

  restore_live_registers(sasm);
  __ leave();
  __ far_jump(deopt_blob->unpack_with_reexecution());

  __ bind(no_deopt);
  __ stop("deopt not performed");

  return oop_maps;
}

OopMapSet* Runtime1::generate_code_for(C1StubId id, StubAssembler* sasm) {
  // for better readability
  const bool dont_gc_arguments = false;

  // default value; overwritten for some optimized stubs that are called from methods that do not use the fpu
  bool save_fpu_registers = true;

  const Register arg0 = C1_MacroAssembler::stub_arg0;
  const Register arg1 = C1_MacroAssembler::stub_arg1;

  // stub code & info for the different stubs
  OopMapSet* oop_maps = nullptr;
  switch (id) {
    {
    case C1StubId::forward_exception_id:
      {
        oop_maps = generate_handle_exception(id, sasm);
        __ leave();
        __ ret();
      }
      break;

    case C1StubId::throw_div0_exception_id:
      {
        StubFrame f(sasm, "throw_div0_exception", dont_gc_arguments, does_not_return);
        oop_maps = generate_exception_throw(sasm, CAST_FROM_FN_PTR(address, throw_div0_exception), false);
      }
      break;

    case C1StubId::throw_null_pointer_exception_id:
      { StubFrame f(sasm, "throw_null_pointer_exception", dont_gc_arguments, does_not_return);
        oop_maps = generate_exception_throw(sasm, CAST_FROM_FN_PTR(address, throw_null_pointer_exception), false);
      }
      break;

    case C1StubId::new_instance_id:
    case C1StubId::fast_new_instance_id:
    case C1StubId::fast_new_instance_init_check_id:
      {
        Register klass = as_Register(FrameMap::stub_klass_reg); // Incoming
        Register obj   = r8;                                    // Result

        if (id == C1StubId::new_instance_id) {
          __ set_info("new_instance", dont_gc_arguments);
        } else if (id == C1StubId::fast_new_instance_id) {
          __ set_info("fast new_instance", dont_gc_arguments);
        } else {
          assert(id == C1StubId::fast_new_instance_init_check_id, "bad C1StubId");
          __ set_info("fast new_instance init check", dont_gc_arguments);
        }

        __ enter();
        OopMap* map = save_live_registers(sasm);
        int call_offset = __ call_RT(obj, noreg, CAST_FROM_FN_PTR(address, new_instance), klass);
        oop_maps = new OopMapSet();
        oop_maps->add_gc_map(call_offset, map);
        restore_live_registers_except_r8(sasm);
        __ verify_oop(obj);
        __ leave();
        __ ret();

        // r8: new instance
      }

      break;

    case C1StubId::counter_overflow_id:
      {
        // arg0: bci, arg1: Method*
        __ enter();
        OopMap* map = save_live_registers(sasm);
        int call_offset = __ call_RT(noreg, noreg, CAST_FROM_FN_PTR(address, counter_overflow), arg0, arg1);
        oop_maps = new OopMapSet();
        oop_maps->add_gc_map(call_offset, map);
        restore_live_registers(sasm);
        __ leave();
        __ ret();
      }
      break;

    case C1StubId::new_type_array_id:
    case C1StubId::new_object_array_id:
      {
        Register length   = as_Register(FrameMap::stub_length_reg); // Incoming
        Register klass    = as_Register(FrameMap::stub_klass_reg);  // Incoming
        Register obj      = r8;                                     // Result

        if (id == C1StubId::new_type_array_id) {
          __ set_info("new_type_array", dont_gc_arguments);
        } else {
          __ set_info("new_object_array", dont_gc_arguments);
        }

#ifdef ASSERT
        // assert object type is really an array of the proper kind
        {
          Label ok;
          Register tmp = t2;   // never allocated: free at a stub entry
          __ ld4(tmp, Address(klass, Klass::layout_helper_offset()));
          // the tag is the top two bits of the int, as a signed value
          __ extr(tmp, tmp, Klass::_lh_array_tag_shift, BitsPerInt - Klass::_lh_array_tag_shift);
          int tag = ((id == C1StubId::new_type_array_id) ? Klass::_lh_array_tag_type_value : Klass::_lh_array_tag_obj_value);
          __ mov(t3, (int64_t)tag);
          __ beq(t3, tmp, ok);
          __ stop("assert(is an array klass)");
          __ should_not_reach_here();
          __ bind(ok);
        }
#endif // ASSERT

        __ enter();
        OopMap* map = save_live_registers(sasm);
        int call_offset = 0;
        if (id == C1StubId::new_type_array_id) {
          call_offset = __ call_RT(obj, noreg, CAST_FROM_FN_PTR(address, new_type_array), klass, length);
        } else {
          call_offset = __ call_RT(obj, noreg, CAST_FROM_FN_PTR(address, new_object_array), klass, length);
        }

        oop_maps = new OopMapSet();
        oop_maps->add_gc_map(call_offset, map);
        restore_live_registers_except_r8(sasm);

        __ verify_oop(obj);
        __ leave();
        __ ret();

        // r8: new array
      }
      break;

    case C1StubId::new_multi_array_id:
      {
        StubFrame f(sasm, "new_multi_array", dont_gc_arguments);
        // stub_klass_reg: klass
        // stub_length_reg: rank
        // stub_tmp1_reg: address of 1st dimension
        OopMap* map = save_live_registers(sasm);
        int call_offset = __ call_RT(r8, noreg, CAST_FROM_FN_PTR(address, new_multi_array),
                                     as_Register(FrameMap::stub_klass_reg),
                                     as_Register(FrameMap::stub_length_reg),
                                     as_Register(FrameMap::stub_tmp1_reg));

        oop_maps = new OopMapSet();
        oop_maps->add_gc_map(call_offset, map);
        restore_live_registers_except_r8(sasm);

        // r8: new multi array
        __ verify_oop(r8);
      }
      break;

    case C1StubId::register_finalizer_id:
      {
        __ set_info("register_finalizer", dont_gc_arguments);

        // This is called via call_runtime so the arguments
        // will be place in C abi locations
        __ verify_oop(c_rarg0);

        // load the klass and check the has finalizer flag
        Label register_finalizer;
        Register t = t2;
        __ load_klass(t, c_rarg0);
        __ ld1(t, Address(t, Klass::misc_flags_offset()));
        __ tbit_nz(ptmp0, ptmp1, t, exact_log2(KlassFlags::_misc_has_finalizer));
        __ br_cond(register_finalizer, ptmp0);
        __ ret();

        __ bind(register_finalizer);
        __ enter();
        OopMap* oop_map = save_live_registers(sasm);
        int call_offset = __ call_RT(noreg, noreg, CAST_FROM_FN_PTR(address, SharedRuntime::register_finalizer), c_rarg0);
        oop_maps = new OopMapSet();
        oop_maps->add_gc_map(call_offset, oop_map);

        // Now restore all the live registers
        restore_live_registers(sasm);

        __ leave();
        __ ret();
      }
      break;

    case C1StubId::throw_class_cast_exception_id:
      {
        // arg0: the object
        StubFrame f(sasm, "throw_class_cast_exception", dont_gc_arguments, does_not_return);
        oop_maps = generate_exception_throw(sasm, CAST_FROM_FN_PTR(address, throw_class_cast_exception), true);
      }
      break;

    case C1StubId::throw_incompatible_class_change_error_id:
      {
        StubFrame f(sasm, "throw_incompatible_class_cast_exception", dont_gc_arguments, does_not_return);
        oop_maps = generate_exception_throw(sasm,
                                            CAST_FROM_FN_PTR(address, throw_incompatible_class_change_error), false);
      }
      break;

    case C1StubId::slow_subtype_check_id:
      {
        // A leaf, called from compiled code with no frame:
        //   arg0 (t2): sub klass   -> result: 1 if a subtype, else 0
        //   arg1 (t3): super klass
        // riscv passes these on the stack; here they travel in registers C1
        // never allocates, and the stub's temporaries (t4, r15) are likewise
        // never allocated, so nothing live needs saving. Clobbers t0, t1,
        // t4, r15 and p6/p7.
        __ set_info("slow_subtype_check", dont_gc_arguments);

        Label miss;
        __ check_klass_subtype_slow_path(arg0,    /*sub_klass*/
                                         arg1,    /*super_klass*/
                                         t4,      /*tmp1_reg*/
                                         r15,     /*tmp2_reg*/
                                         nullptr, /*L_success*/
                                         &miss    /*L_failure*/);

        // fallthrough on success:
        __ mov(arg0, (int64_t)1);
        __ ret();

        __ bind(miss);
        __ mov(arg0, zr);
        __ ret();
      }
      break;

    case C1StubId::monitorenter_nofpu_id:
      save_fpu_registers = false;
      // fall through
    case C1StubId::monitorenter_id:
      {
        // arg0: object, arg1: lock address
        StubFrame f(sasm, "monitorenter", dont_gc_arguments, requires_pop_epilogue_return);
        OopMap* map = save_live_registers(sasm, save_fpu_registers);

        int call_offset = __ call_RT(noreg, noreg, CAST_FROM_FN_PTR(address, monitorenter), arg0, arg1);

        oop_maps = new OopMapSet();
        oop_maps->add_gc_map(call_offset, map);
        restore_live_registers(sasm, save_fpu_registers);
      }
      break;

    case C1StubId::is_instance_of_id:
      {
        // Called through call_runtime as a leaf (C convention):
        // Mirror: out0
        // Object: out1
        // Result: r8
        // Everything caller-saved is dead here, as at any call.
        Register mirror = c_rarg0, obj = c_rarg1, result = r8;
        Register klass = t3, obj_klass = t4;

        Label fail, success;

        // Get the Klass*
        __ ld8(klass, Address(mirror, java_lang_Class::klass_offset()));
        __ beqz(klass, fail); // Klass is null
        __ beqz(obj, fail);   // obj is null

        __ load_klass(obj_klass, obj);
        __ check_klass_subtype(obj_klass, klass, r14, r15, success);

        __ bind(fail);
        __ mov(result, zr);
        __ ret();

        __ bind(success);
        __ mov(result, (int64_t)1);
        __ ret();
      }
      break;

    case C1StubId::monitorexit_nofpu_id:
      save_fpu_registers = false;
      // fall through
    case C1StubId::monitorexit_id:
      {
        // arg0: lock address
        StubFrame f(sasm, "monitorexit", dont_gc_arguments);
        OopMap* map = save_live_registers(sasm, save_fpu_registers);

        // note: really a leaf routine but must setup last java sp
        //       => use call_RT for now (speed can be improved by
        //       doing last java sp setup manually)
        int call_offset = __ call_RT(noreg, noreg, CAST_FROM_FN_PTR(address, monitorexit), arg0);

        oop_maps = new OopMapSet();
        oop_maps->add_gc_map(call_offset, map);
        restore_live_registers(sasm, save_fpu_registers);
      }
      break;

    case C1StubId::deoptimize_id:
      {
        // arg0: trap request
        StubFrame f(sasm, "deoptimize", dont_gc_arguments, does_not_return);
        OopMap* oop_map = save_live_registers(sasm);
        int call_offset = __ call_RT(noreg, noreg, CAST_FROM_FN_PTR(address, deoptimize), arg0);

        oop_maps = new OopMapSet();
        oop_maps->add_gc_map(call_offset, oop_map);
        restore_live_registers(sasm);
        DeoptimizationBlob* deopt_blob = SharedRuntime::deopt_blob();
        assert(deopt_blob != nullptr, "deoptimization blob must have been created");
        __ leave();
        __ far_jump(deopt_blob->unpack_with_reexecution());
      }
      break;

    case C1StubId::throw_range_check_failed_id:
      {
        // arg0: index, arg1: array
        StubFrame f(sasm, "range_check_failed", dont_gc_arguments, does_not_return);
        oop_maps = generate_exception_throw(sasm, CAST_FROM_FN_PTR(address, throw_range_check_exception), true);
      }
      break;

    case C1StubId::unwind_exception_id:
      {
        __ set_info("unwind_exception", dont_gc_arguments);
        // note: no stubframe since we are about to leave the current
        //       activation and we are calling a leaf VM function only.
        generate_unwind_exception(sasm);
      }
      break;

    case C1StubId::access_field_patching_id:
      {
        StubFrame f(sasm, "access_field_patching", dont_gc_arguments, does_not_return);
        // we should set up register map
        oop_maps = generate_patching(sasm, CAST_FROM_FN_PTR(address, access_field_patching));
      }
      break;

    case C1StubId::load_klass_patching_id:
      {
        StubFrame f(sasm, "load_klass_patching", dont_gc_arguments, does_not_return);
        // we should set up register map
        oop_maps = generate_patching(sasm, CAST_FROM_FN_PTR(address, move_klass_patching));
      }
      break;

    case C1StubId::load_mirror_patching_id:
      {
        StubFrame f(sasm, "load_mirror_patching", dont_gc_arguments, does_not_return);
        // we should set up register map
        oop_maps = generate_patching(sasm, CAST_FROM_FN_PTR(address, move_mirror_patching));
      }
      break;

    case C1StubId::load_appendix_patching_id:
      {
        StubFrame f(sasm, "load_appendix_patching", dont_gc_arguments, does_not_return);
        // we should set up register map
        oop_maps = generate_patching(sasm, CAST_FROM_FN_PTR(address, move_appendix_patching));
      }
      break;

    case C1StubId::handle_exception_nofpu_id:
    case C1StubId::handle_exception_id:
      {
        StubFrame f(sasm, "handle_exception", dont_gc_arguments);
        oop_maps = generate_handle_exception(id, sasm);
      }
      break;

    case C1StubId::handle_exception_from_callee_id:
      {
        StubFrame f(sasm, "handle_exception_from_callee", dont_gc_arguments);
        oop_maps = generate_handle_exception(id, sasm);
      }
      break;

    case C1StubId::throw_index_exception_id:
      {
        // arg0: index
        StubFrame f(sasm, "index_range_check_failed", dont_gc_arguments, does_not_return);
        oop_maps = generate_exception_throw(sasm, CAST_FROM_FN_PTR(address, throw_index_exception), true);
      }
      break;

    case C1StubId::throw_array_store_exception_id:
      {
        // arg0: the object
        StubFrame f(sasm, "throw_array_store_exception", dont_gc_arguments, does_not_return);
        oop_maps = generate_exception_throw(sasm, CAST_FROM_FN_PTR(address, throw_array_store_exception), true);
      }
      break;

    case C1StubId::predicate_failed_trap_id:
      {
        StubFrame f(sasm, "predicate_failed_trap", dont_gc_arguments, does_not_return);

        OopMap* map = save_live_registers(sasm);

        int call_offset = __ call_RT(noreg, noreg, CAST_FROM_FN_PTR(address, predicate_failed_trap));
        oop_maps = new OopMapSet();
        oop_maps->add_gc_map(call_offset, map);
        restore_live_registers(sasm);
        __ leave();
        DeoptimizationBlob* deopt_blob = SharedRuntime::deopt_blob();
        assert(deopt_blob != nullptr, "deoptimization blob must have been created");

        __ far_jump(deopt_blob->unpack_with_reexecution());
      }
      break;

    case C1StubId::dtrace_object_alloc_id:
      { // c_rarg0: object
        StubFrame f(sasm, "dtrace_object_alloc", dont_gc_arguments);
        save_live_registers(sasm);

        __ call_VM_leaf(CAST_FROM_FN_PTR(address, static_cast<int (*)(oopDesc*)>(SharedRuntime::dtrace_object_alloc)), c_rarg0);

        restore_live_registers(sasm);
      }
      break;

    default:
      {
        StubFrame f(sasm, "unimplemented entry", dont_gc_arguments, does_not_return);
        __ mov(arg0, (int64_t)(int)id);
        __ call_RT(noreg, noreg, CAST_FROM_FN_PTR(address, unimplemented_entry), arg0);
        __ should_not_reach_here();
      }
      break;
    }
  }
  return oop_maps;
}

#undef __

const char *Runtime1::pd_name_for_address(address entry) {
  return "<unknown function>";
}
