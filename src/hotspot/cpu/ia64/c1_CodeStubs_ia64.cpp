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
#include "classfile/javaClasses.hpp"
#include "nativeInst_ia64.hpp"
#include "runtime/sharedRuntime.hpp"
#include "vmreg_ia64.inline.hpp"


#define __ ce->masm()->

void C1SafepointPollStub::emit_code(LIR_Assembler* ce) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: masm)
}

void CounterOverflowStub::emit_code(LIR_Assembler* ce) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: CounterOverflowStub::emit_code)
}

void RangeCheckStub::emit_code(LIR_Assembler* ce) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: RangeCheckStub::emit_code)
}

PredicateFailedStub::PredicateFailedStub(CodeEmitInfo* info) {
  _info = new CodeEmitInfo(info);
}

void PredicateFailedStub::emit_code(LIR_Assembler* ce) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: PredicateFailedStub::emit_code)
}

void DivByZeroStub::emit_code(LIR_Assembler* ce) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: DivByZeroStub::emit_code)
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
  Unimplemented(); // IA-64 C1: not yet ported (riscv: NewInstanceStub::emit_code)
}

// Implementation of NewTypeArrayStub
NewTypeArrayStub::NewTypeArrayStub(LIR_Opr klass_reg, LIR_Opr length, LIR_Opr result, CodeEmitInfo* info) {
  _klass_reg = klass_reg;
  _length = length;
  _result = result;
  _info = new CodeEmitInfo(info);
}

void NewTypeArrayStub::emit_code(LIR_Assembler* ce) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: NewTypeArrayStub::emit_code)
}

// Implementation of NewObjectArrayStub
NewObjectArrayStub::NewObjectArrayStub(LIR_Opr klass_reg, LIR_Opr length, LIR_Opr result, CodeEmitInfo* info) {
  _klass_reg = klass_reg;
  _result = result;
  _length = length;
  _info = new CodeEmitInfo(info);
}

void NewObjectArrayStub::emit_code(LIR_Assembler* ce) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: NewObjectArrayStub::emit_code)
}

void MonitorEnterStub::emit_code(LIR_Assembler* ce) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: MonitorEnterStub::emit_code)
}

void MonitorExitStub::emit_code(LIR_Assembler* ce) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: MonitorExitStub::emit_code)
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
  Unimplemented(); // IA-64 C1: not yet ported (riscv: PatchingStub::align_patch_site)
}

void PatchingStub::emit_code(LIR_Assembler* ce) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: PatchingStub::emit_code)
}

void DeoptimizeStub::emit_code(LIR_Assembler* ce) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: DeoptimizeStub::emit_code)
}

void ImplicitNullCheckStub::emit_code(LIR_Assembler* ce) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: ImplicitNullCheckStub::emit_code)
}

void SimpleExceptionStub::emit_code(LIR_Assembler* ce) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: SimpleExceptionStub::emit_code)
}

void ArrayCopyStub::emit_code(LIR_Assembler* ce) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: ArrayCopyStub::emit_code)
}

#undef __
