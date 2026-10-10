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
 *
 */

#include "asm/macroAssembler.inline.hpp"
#include "c1/c1_CodeStubs.hpp"
#include "c1/c1_FrameMap.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "c1/c1_Runtime1.hpp"
#include "code/compiledIC.hpp"
#include "classfile/javaClasses.hpp"
#include "nativeInst_ia64.hpp"
#include "runtime/sharedRuntime.hpp"
#include "vmreg_ia64.inline.hpp"


#define __ ce->masm()->

void C1SafepointPollStub::emit_code(LIR_Assembler* ce) {
  // The return poll (LIR_Assembler::return_op): the frame is already gone
  // and b0 holds the return address. Record the poll's pc, position-
  // independently, for the handler blob.
  __ bind(_entry);
  __ mov_from_ip(t0);
  __ add_imm(t0, t0, safepoint_offset() - __ offset() + (int)BytesPerBundle, t1);
  __ st8(Address(Rthread, JavaThread::saved_exception_pc_offset()), t0, t1);
  __ far_jump(SharedRuntime::polling_page_return_handler_blob()->entry_point());
}

void CounterOverflowStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);
  Metadata *m = _method->as_constant_ptr()->as_metadata();
  __ mov_metadata(C1_MacroAssembler::stub_arg1, m);
  __ mov_immediate(C1_MacroAssembler::stub_arg0, _bci);
  __ far_call(Runtime1::entry_for(C1StubId::counter_overflow_id));
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  __ j(_continuation);
}

void RangeCheckStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);
  if (_info->deoptimize_on_exception()) {
    address a = Runtime1::entry_for(C1StubId::predicate_failed_trap_id);
    __ far_call(a);
    ce->add_call_info_here(_info);
    ce->verify_oop_map(_info);
    DEBUG_ONLY(__ should_not_reach_here());
    return;
  }

  // The arguments go in stub_arg0/1 (t2/t3): never allocated, so free here.
  if (_index->is_cpu_register()) {
    __ mov(C1_MacroAssembler::stub_arg0, _index->as_register());
  } else {
    __ mov_immediate(C1_MacroAssembler::stub_arg0, _index->as_jint());
  }
  C1StubId stub_id;
  if (_throw_index_out_of_bounds_exception) {
    stub_id = C1StubId::throw_index_exception_id;
  } else {
    assert(_array != LIR_Opr::nullOpr(), "sanity");
    __ mov(C1_MacroAssembler::stub_arg1, _array->as_pointer_register());
    stub_id = C1StubId::throw_range_check_failed_id;
  }
  __ far_call(Runtime1::entry_for(stub_id));
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  DEBUG_ONLY(__ should_not_reach_here());
}

PredicateFailedStub::PredicateFailedStub(CodeEmitInfo* info) {
  _info = new CodeEmitInfo(info);
}

void PredicateFailedStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);
  address a = Runtime1::entry_for(C1StubId::predicate_failed_trap_id);
  __ far_call(a);
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  DEBUG_ONLY(__ should_not_reach_here());
}

void DivByZeroStub::emit_code(LIR_Assembler* ce) {
  if (_offset != -1) {
    ce->compilation()->implicit_exception_table()->append(_offset, __ offset());
  }
  __ bind(_entry);
  __ far_call(Runtime1::entry_for(C1StubId::throw_div0_exception_id));
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
#ifdef ASSERT
  __ should_not_reach_here();
#endif
}

// Implementation of NewInstanceStub
NewInstanceStub::NewInstanceStub(LIR_Opr klass_reg, LIR_Opr result, ciInstanceKlass* klass, CodeEmitInfo* info, C1StubId stub_id) {
  _result = result;
  _klass = klass;
  _klass_reg = klass_reg;
  _info = new CodeEmitInfo(info);
  assert(stub_id == C1StubId::new_instance_id                 ||
         stub_id == C1StubId::fast_new_instance_id            ||
         stub_id == C1StubId::fast_new_instance_init_check_id,
         "need new_instance id");
  _stub_id = stub_id;
}

void NewInstanceStub::emit_code(LIR_Assembler* ce) {
  assert(__ rsp_offset() == 0, "frame size should be fixed");
  __ bind(_entry);
  __ mov(as_Register(FrameMap::stub_klass_reg), _klass_reg->as_register());
  __ far_call(Runtime1::entry_for(_stub_id));
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  assert(_result->as_register() == r8, "result must in r8");
  __ j(_continuation);
}

// Implementation of NewTypeArrayStub
NewTypeArrayStub::NewTypeArrayStub(LIR_Opr klass_reg, LIR_Opr length, LIR_Opr result, CodeEmitInfo* info) {
  _klass_reg = klass_reg;
  _length = length;
  _result = result;
  _info = new CodeEmitInfo(info);
}

void NewTypeArrayStub::emit_code(LIR_Assembler* ce) {
  assert(__ rsp_offset() == 0, "frame size should be fixed");
  __ bind(_entry);
  assert(_length->as_register() == as_Register(FrameMap::stub_length_reg), "length must in stub_length_reg");
  assert(_klass_reg->as_register() == as_Register(FrameMap::stub_klass_reg), "klass_reg must in stub_klass_reg");
  __ far_call(Runtime1::entry_for(C1StubId::new_type_array_id));
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  assert(_result->as_register() == r8, "result must in r8");
  __ j(_continuation);
}

// Implementation of NewObjectArrayStub
NewObjectArrayStub::NewObjectArrayStub(LIR_Opr klass_reg, LIR_Opr length, LIR_Opr result, CodeEmitInfo* info) {
  _klass_reg = klass_reg;
  _result = result;
  _length = length;
  _info = new CodeEmitInfo(info);
}

void NewObjectArrayStub::emit_code(LIR_Assembler* ce) {
  assert(__ rsp_offset() == 0, "frame size should be fixed");
  __ bind(_entry);
  assert(_length->as_register() == as_Register(FrameMap::stub_length_reg), "length must in stub_length_reg");
  assert(_klass_reg->as_register() == as_Register(FrameMap::stub_klass_reg), "klass_reg must in stub_klass_reg");
  __ far_call(Runtime1::entry_for(C1StubId::new_object_array_id));
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  assert(_result->as_register() == r8, "result must in r8");
  __ j(_continuation);
}

void MonitorEnterStub::emit_code(LIR_Assembler* ce) {
  assert(__ rsp_offset() == 0, "frame size should be fixed");
  __ bind(_entry);
  __ mov(C1_MacroAssembler::stub_arg0, _obj_reg->as_register());
  __ mov(C1_MacroAssembler::stub_arg1, _lock_reg->as_register());
  C1StubId enter_id;
  if (ce->compilation()->has_fpu_code()) {
    enter_id = C1StubId::monitorenter_id;
  } else {
    enter_id = C1StubId::monitorenter_nofpu_id;
  }
  __ far_call(Runtime1::entry_for(enter_id));
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  __ j(_continuation);
}

void MonitorExitStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);
  if (_compute_lock) {
    // lock_reg was destroyed by fast unlocking attempt => recompute it
    ce->monitor_address(_monitor_ix, _lock_reg);
  }
  __ mov(C1_MacroAssembler::stub_arg0, _lock_reg->as_register());
  // note: non-blocking leaf routine => no call info needed
  C1StubId exit_id;
  if (ce->compilation()->has_fpu_code()) {
    exit_id = C1StubId::monitorexit_id;
  } else {
    exit_id = C1StubId::monitorexit_nofpu_id;
  }
  __ far_call(Runtime1::entry_for(exit_id));
  __ j(_continuation);
}

// Implementation of patching:
// - Copy the code at given offset to an inlined buffer (first the bytes, then the number of bytes)
// - Replace original code with a call to the stub
// At Runtime:
// - call to stub, jump to runtime
// - in runtime: preserve all registers (rspecially objects, i.e., source and destination object)
// - in runtime: after initializing class, restore original code, reexecute instruction

int PatchingStub::_patch_info_offset = -NativeGeneralJump::instruction_size;

void PatchingStub::align_patch_site(MacroAssembler* masm) {
  // IA-64 does not patch: it deoptimizes instead (deoptimize_trap).
}

void PatchingStub::emit_code(LIR_Assembler* ce) {
  assert(false, "IA-64 should not use C1 runtime patching");
}

void DeoptimizeStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);
  __ mov_immediate(C1_MacroAssembler::stub_arg0, _trap_request);
  __ far_call(Runtime1::entry_for(C1StubId::deoptimize_id));
  ce->add_call_info_here(_info);
  DEBUG_ONLY(__ should_not_reach_here());
}

void ImplicitNullCheckStub::emit_code(LIR_Assembler* ce) {
  address a = nullptr;
  if (_info->deoptimize_on_exception()) {
    // Deoptimize, do not throw the exception, because it is probably wrong to do it here.
    a = Runtime1::entry_for(C1StubId::predicate_failed_trap_id);
  } else {
    a = Runtime1::entry_for(C1StubId::throw_null_pointer_exception_id);
  }

  ce->compilation()->implicit_exception_table()->append(_offset, __ offset());
  __ bind(_entry);
  __ far_call(a);
  ce->add_call_info_here(_info);
  ce->verify_oop_map(_info);
  DEBUG_ONLY(__ should_not_reach_here());
}

void SimpleExceptionStub::emit_code(LIR_Assembler* ce) {
  assert(__ rsp_offset() == 0, "frame size should be fixed");

  __ bind(_entry);
  // pass the object in stub_arg0, never allocated, because all other
  // registers must be preserved
  if (_obj->is_cpu_register()) {
    __ mov(C1_MacroAssembler::stub_arg0, _obj->as_register());
  }
  __ far_call(Runtime1::entry_for(_stub));
  ce->add_call_info_here(_info);
  DEBUG_ONLY(__ should_not_reach_here());
}

void ArrayCopyStub::emit_code(LIR_Assembler* ce) {
  // ---------------slow case: call to native-----------------
  __ bind(_entry);
  // Figure out where the args should go
  // This should really convert the IntrinsicID to the Method* and signature
  // but I don't know how to do that.
  const int args_num = 5;
  VMRegPair args[args_num];
  BasicType signature[args_num] = { T_OBJECT, T_INT, T_OBJECT, T_INT, T_INT };
  SharedRuntime::java_calling_convention(signature, args, args_num);

  // push parameters
  Register r[args_num];
  r[0] = src()->as_register();
  r[1] = src_pos()->as_register();
  r[2] = dst()->as_register();
  r[3] = dst_pos()->as_register();
  r[4] = length()->as_register();

  // next registers will get stored on the stack, above the psABI scratch area
  for (int j = 0; j < args_num; j++) {
    VMReg r_1 = args[j].first();
    if (r_1->is_stack()) {
      int st_off = (r_1->reg2stack() + SharedRuntime::out_preserve_stack_slots()) * VMRegImpl::stack_slot_size;
      __ st8(Address(sp, st_off), r[j]);
    } else {
      assert(r[j] == args[j].first()->as_Register(), "Wrong register for arg");
    }
  }

  ce->align_call(lir_static_call);

  ce->emit_static_call_stub();
  if (ce->compilation()->bailed_out()) {
    return; // CodeCache is full
  }
  if (__ trampoline_call(SharedRuntime::get_resolve_static_call_stub(), static_call_Relocation::spec()) == nullptr) {
    ce->bailout("trampoline stub overflow");
    return;
  }
  ce->add_call_info_here(info());
  __ set_poll_word_register();       // a Java call: the callee may be interpreted

  __ j(_continuation);
}

#undef __
