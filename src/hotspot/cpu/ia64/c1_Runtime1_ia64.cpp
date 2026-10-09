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

#include "asm/assembler.hpp"
#include "c1/c1_CodeStubs.hpp"
#include "c1/c1_Defs.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "c1/c1_Runtime1.hpp"
#include "compiler/disassembler.hpp"
#include "compiler/oopMap.hpp"
#include "gc/shared/cardTable.hpp"
#include "gc/shared/cardTableBarrierSet.hpp"
#include "interpreter/interpreter.hpp"
#include "memory/universe.hpp"
#include "nativeInst_ia64.hpp"
#include "oops/oop.inline.hpp"
#include "prims/jvmtiExport.hpp"
#include "register_ia64.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/signature.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/vframe.hpp"
#include "runtime/vframeArray.hpp"
#include "utilities/powerOfTwo.hpp"
#include "vmreg_ia64.inline.hpp"


// Implementation of StubAssembler

int StubAssembler::call_RT(Register oop_result, Register metadata_result, address entry, int args_size) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: StubAssembler::call_RT)
}

int StubAssembler::call_RT(Register oop_result, Register metadata_result, address entry, Register arg1) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: StubAssembler::call_RT)
}

int StubAssembler::call_RT(Register oop_result, Register metadata_result, address entry, Register arg1, Register arg2) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: StubAssembler::call_RT)
}

int StubAssembler::call_RT(Register oop_result, Register metadata_result, address entry, Register arg1, Register arg2, Register arg3) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: StubAssembler::call_RT)
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
  Unimplemented(); // IA-64 C1: not yet ported (riscv: StubAssembler::prologue)
}

void StubAssembler::epilogue(bool use_pop) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: StubAssembler::epilogue)
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
  Unimplemented(); // IA-64 C1: not yet ported (riscv: StubFrame::load_argument)
}


StubFrame::~StubFrame() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: StubFrame::~StubFrame)
}

#undef __


// Implementation of Runtime1

#define __ sasm->

// Stack layout for saving/restoring  all the registers needed during a runtime
// call (this includes deoptimization)
// Note: note that users of this frame may well have arguments to some runtime
// while these values are on the stack. These positions neglect those arguments
// but the code in save_live_registers will take the argument count into
// account.
//

enum reg_save_layout {
  reg_save_frame_size = 32 /* float */ + 30 /* integer excluding x3, x4 */
};

// Save off registers which might be killed by calls into the runtime.
// Tries to smart of about FPU registers.  In particular we separate
// saving and describing the FPU registers for deoptimization since we
// have to save the FPU registers twice if we describe them.  The
// deopt blob is the only thing which needs to describe FPU registers.
// In all other cases it should be sufficient to simply save their
// current value.

static int cpu_reg_save_offsets[FrameMap::nof_cpu_regs];
static int fpu_reg_save_offsets[FrameMap::nof_fpu_regs];

static OopMap* generate_oop_map(StubAssembler* sasm, bool save_fpu_registers) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: generate_oop_map)
}

static OopMap* save_live_registers(StubAssembler* sasm,
                                   bool save_fpu_registers = true) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: save_live_registers)
}

static void restore_live_registers(StubAssembler* sasm, bool restore_fpu_registers = true) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: restore_live_registers)
}

static void restore_live_registers_except_r10(StubAssembler* sasm, bool restore_fpu_registers = true) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: restore_live_registers_except_r10)
}

void Runtime1::initialize_pd() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: Runtime1::initialize_pd)
}

// return: offset in 64-bit words.
uint Runtime1::runtime_blob_current_thread_offset(frame f) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: Runtime1::runtime_blob_current_thread_offset)
}

// target: the entry point of the method that creates and posts the exception oop
// has_argument: true if the exception needs arguments (passed in t0 and t1)

OopMapSet* Runtime1::generate_exception_throw(StubAssembler* sasm, address target, bool has_argument) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: Runtime1::generate_exception_throw)
}

OopMapSet* Runtime1::generate_handle_exception(C1StubId id, StubAssembler *sasm) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: Runtime1::generate_handle_exception)
}


void Runtime1::generate_unwind_exception(StubAssembler *sasm) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: Runtime1::generate_unwind_exception)
}

OopMapSet* Runtime1::generate_patching(StubAssembler* sasm, address target) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: Runtime1::generate_patching)
}

OopMapSet* Runtime1::generate_code_for(C1StubId id, StubAssembler* sasm) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: Runtime1::generate_code_for)
}

#undef __

const char *Runtime1::pd_name_for_address(address entry) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: Runtime1::pd_name_for_address)
}
