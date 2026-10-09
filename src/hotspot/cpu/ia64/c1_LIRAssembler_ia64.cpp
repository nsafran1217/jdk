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
 *
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
#include "runtime/sharedRuntime.hpp"
#include "utilities/powerOfTwo.hpp"
#include "vmreg_ia64.inline.hpp"

#ifndef PRODUCT
#define COMMENT(x)   do { __ block_comment(x); } while (0)
#else
#define COMMENT(x)
#endif

NEEDS_CLEANUP // remove this definitions ?
const Register SYNC_header = r8;    // synchronization header
const Register SHIFT_count = r8;    // where count for shift operations must be

#define __ _masm->

static void select_different_registers(Register preserve,
                                       Register extra,
                                       Register &tmp1,
                                       Register &tmp2) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: select_different_registers)
}

static void select_different_registers(Register preserve,
                                       Register extra,
                                       Register &tmp1,
                                       Register &tmp2,
                                       Register &tmp3) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: select_different_registers)
}

bool LIR_Assembler::is_small_constant(LIR_Opr opr) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::is_small_constant)
}

void LIR_Assembler::clinit_barrier(ciMethod* method) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::clinit_barrier)
}

LIR_Opr LIR_Assembler::receiverOpr() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::receiverOpr)
}

LIR_Opr LIR_Assembler::osrBufferPointer() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::osrBufferPointer)
}

void LIR_Assembler::breakpoint() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::breakpoint)
}

void LIR_Assembler::push(LIR_Opr opr) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::push)
}

void LIR_Assembler::pop(LIR_Opr opr) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::pop)
}

static jlong as_long(LIR_Opr data) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: as_long)
}

Address LIR_Assembler::as_Address(LIR_Address* addr, Register tmp) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::as_Address)
}

Address LIR_Assembler::as_Address_hi(LIR_Address* addr) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::as_Address_hi)
}

Address LIR_Assembler::as_Address(LIR_Address* addr) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::as_Address)
}

Address LIR_Assembler::as_Address_lo(LIR_Address* addr) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::as_Address_lo)
}

// Ensure a valid Address (base + offset) to a stack-slot. If stack access is
// not encodable as a base + (immediate) offset, generate an explicit address
// calculation to hold the address in t0.
Address LIR_Assembler::stack_slot_address(int index, uint size, int adjust) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: Address)
}

void LIR_Assembler::osr_entry() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::osr_entry)
}

// inline cache check; done before the frame is built.
int LIR_Assembler::check_icache() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::check_icache)
}

void LIR_Assembler::jobject2reg(jobject o, Register reg) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::jobject2reg)
}

void LIR_Assembler::jobject2reg_with_patching(Register reg, CodeEmitInfo *info) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::jobject2reg_with_patching)
}

// This specifies the rsp decrement needed to build the frame
int LIR_Assembler::initial_frame_size_in_bytes() const {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::initial_frame_size_in_bytes)
}

int LIR_Assembler::emit_exception_handler() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_exception_handler)
}

// Emit the code to remove the frame from the stack in the exception
// unwind path.
int LIR_Assembler::emit_unwind_handler() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_unwind_handler)
}

int LIR_Assembler::emit_deopt_handler() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_deopt_handler)
}

void LIR_Assembler::return_op(LIR_Opr result, C1SafepointPollStub* code_stub) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::return_op)
}

int LIR_Assembler::safepoint_poll(LIR_Opr tmp, CodeEmitInfo* info) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::safepoint_poll)
}

void LIR_Assembler::move_regs(Register from_reg, Register to_reg) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::move_regs)
}

void LIR_Assembler::swap_reg(Register a, Register b) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::swap_reg)
}

void LIR_Assembler::const2reg(LIR_Opr src, LIR_Opr dest, LIR_PatchCode patch_code, CodeEmitInfo* info) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::const2reg)
}

void LIR_Assembler::const2stack(LIR_Opr src, LIR_Opr dest) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::const2stack)
}

void LIR_Assembler::const2mem(LIR_Opr src, LIR_Opr dest, BasicType type, CodeEmitInfo* info, bool wide) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::const2mem)
}

void LIR_Assembler::reg2reg(LIR_Opr src, LIR_Opr dest) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::reg2reg)
}

void LIR_Assembler::reg2stack(LIR_Opr src, LIR_Opr dest, BasicType type) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::reg2stack)
}

void LIR_Assembler::reg2mem(LIR_Opr src, LIR_Opr dest, BasicType type, LIR_PatchCode patch_code, CodeEmitInfo* info, bool wide) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::reg2mem)
}

void LIR_Assembler::stack2reg(LIR_Opr src, LIR_Opr dest, BasicType type) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::stack2reg)
}

void LIR_Assembler::klass2reg_with_patching(Register reg, CodeEmitInfo* info) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::klass2reg_with_patching)
}

void LIR_Assembler::stack2stack(LIR_Opr src, LIR_Opr dest, BasicType type) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::stack2stack)
}

void LIR_Assembler::mem2reg(LIR_Opr src, LIR_Opr dest, BasicType type, LIR_PatchCode patch_code, CodeEmitInfo* info, bool wide) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::mem2reg)
}

void LIR_Assembler::emit_op3(LIR_Op3* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_op3)
}

// Consider using cmov (Zicond)
void LIR_Assembler::cmove(LIR_Condition condition, LIR_Opr opr1, LIR_Opr opr2, LIR_Opr result, BasicType type,
                          LIR_Opr cmp_opr1, LIR_Opr cmp_opr2) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: cmov)
}

void LIR_Assembler::emit_opBranch(LIR_OpBranch* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_opBranch)
}

void LIR_Assembler::emit_branch(LIR_Condition cmp_flag, LIR_Opr cmp1, LIR_Opr cmp2, Label& label,
                                bool is_far, bool is_unordered) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_branch)
}

void LIR_Assembler::emit_opConvert(LIR_OpConvert* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_opConvert)
}

void LIR_Assembler::emit_alloc_obj(LIR_OpAllocObj* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_alloc_obj)
}

void LIR_Assembler::emit_alloc_array(LIR_OpAllocArray* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_alloc_array)
}

void LIR_Assembler::type_profile_helper(Register mdo, ciMethodData *md, ciProfileData *data,
                                        Register recv, Label* update_done) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::type_profile_helper)
}

void LIR_Assembler::data_check(LIR_OpTypeCheck *op, ciMethodData **md, ciProfileData **data) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::data_check)
}

void LIR_Assembler::typecheck_helper_slowcheck(ciKlass *k, Register obj, Register Rtmp1,
                                               Register k_RInfo, Register klass_RInfo,
                                               Label *failure_target, Label *success_target) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::typecheck_helper_slowcheck)
}

void LIR_Assembler::profile_object(ciMethodData* md, ciProfileData* data, Register obj,
                                   Register k_RInfo, Register klass_RInfo, Label* obj_is_null) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::profile_object)
}

void LIR_Assembler::typecheck_loaded(LIR_OpTypeCheck *op, ciKlass* k, Register k_RInfo) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::typecheck_loaded)
}

void LIR_Assembler::emit_typecheck_helper(LIR_OpTypeCheck *op, Label* success, Label* failure, Label* obj_is_null) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_typecheck_helper)
}

void LIR_Assembler::emit_opTypeCheck(LIR_OpTypeCheck* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_opTypeCheck)
}

void LIR_Assembler::emit_compare_and_swap(LIR_OpCompareAndSwap* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_compare_and_swap)
}

void LIR_Assembler::intrinsic_op(LIR_Code code, LIR_Opr value, LIR_Opr unused, LIR_Opr dest, LIR_Op* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::intrinsic_op)
}

void LIR_Assembler::logic_op(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dst) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::logic_op)
}

void LIR_Assembler::comp_op(LIR_Condition condition, LIR_Opr src, LIR_Opr result, LIR_Op2* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::comp_op)
}

void LIR_Assembler::comp_fl2i(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dst, LIR_Op2* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::comp_fl2i)
}

void LIR_Assembler::align_call(LIR_Code code) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::align_call)
}

void LIR_Assembler::call(LIR_OpJavaCall* op, relocInfo::relocType rtype) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::call)
}

void LIR_Assembler::ic_call(LIR_OpJavaCall* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::ic_call)
}

void LIR_Assembler::emit_static_call_stub() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_static_call_stub)
}

void LIR_Assembler::throw_op(LIR_Opr exceptionPC, LIR_Opr exceptionOop, CodeEmitInfo* info) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::throw_op)
}

void LIR_Assembler::unwind_op(LIR_Opr exceptionOop) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::unwind_op)
}

void LIR_Assembler::shift_op(LIR_Code code, LIR_Opr left, LIR_Opr count, LIR_Opr dest, LIR_Opr tmp) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::shift_op)
}

void LIR_Assembler::shift_op(LIR_Code code, LIR_Opr left, jint count, LIR_Opr dest) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::shift_op)
}

void LIR_Assembler::emit_lock(LIR_OpLock* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_lock)
}

void LIR_Assembler::emit_load_klass(LIR_OpLoadKlass* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_load_klass)
}

void LIR_Assembler::emit_profile_call(LIR_OpProfileCall* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_profile_call)
}

void LIR_Assembler::emit_delay(LIR_OpDelay*) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_delay)
}

void LIR_Assembler::monitor_address(int monitor_no, LIR_Opr dst) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::monitor_address)
}

void LIR_Assembler::emit_updatecrc32(LIR_OpUpdateCRC32* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_updatecrc32)
}

void LIR_Assembler::check_conflict(ciKlass* exact_klass, intptr_t current_klass,
                                   Register tmp, Label &next, Label &none,
                                   Address mdo_addr) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::check_conflict)
}

void LIR_Assembler::check_no_conflict(ciKlass* exact_klass, intptr_t current_klass, Register tmp,
                                      Address mdo_addr, Label &next) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::check_no_conflict)
}

void LIR_Assembler::check_null(Register tmp, Label &update, intptr_t current_klass,
                               Address mdo_addr, bool do_update, Label &next) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::check_null)
}

void LIR_Assembler::emit_profile_type(LIR_OpProfileType* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_profile_type)
}

void LIR_Assembler::align_backward_branch_target() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::align_backward_branch_target)
}

void LIR_Assembler::negate(LIR_Opr left, LIR_Opr dest, LIR_Opr tmp) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::negate)
}


void LIR_Assembler::leal(LIR_Opr addr, LIR_Opr dest, LIR_PatchCode patch_code, CodeEmitInfo* info) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::leal)
}


void LIR_Assembler::rt_call(LIR_Opr result, address dest, const LIR_OprList* args, LIR_Opr tmp, CodeEmitInfo* info) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::rt_call)
}

void LIR_Assembler::volatile_move_op(LIR_Opr src, LIR_Opr dest, BasicType type, CodeEmitInfo* info) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::volatile_move_op)
}

#ifdef ASSERT
// emit run-time assertion
void LIR_Assembler::emit_assert(LIR_OpAssert* op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::emit_assert)
}
#endif

#ifndef PRODUCT
#define COMMENT(x)   do { __ block_comment(x); } while (0)
#else
#define COMMENT(x)
#endif

void LIR_Assembler::membar() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: while)
}

void LIR_Assembler::membar_acquire() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::membar_acquire)
}

void LIR_Assembler::membar_release() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::membar_release)
}

void LIR_Assembler::membar_loadload() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::membar_loadload)
}

void LIR_Assembler::membar_storestore() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::membar_storestore)
}

void LIR_Assembler::membar_loadstore() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::membar_loadstore)
}

void LIR_Assembler::membar_storeload() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::membar_storeload)
}

void LIR_Assembler::on_spin_wait() {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::on_spin_wait)
}

void LIR_Assembler::get_thread(LIR_Opr result_reg) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::get_thread)
}

void LIR_Assembler::peephole(LIR_List *lir) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::peephole)
}

void LIR_Assembler::atomic_op(LIR_Code code, LIR_Opr src, LIR_Opr data, LIR_Opr dest, LIR_Opr tmp_op) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::atomic_op)
}

int LIR_Assembler::array_element_size(BasicType type) const {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::array_element_size)
}

// helper functions which checks for overflow and sets bailout if it
// occurs.  Always returns a valid embeddable pointer but in the
// bailout case the pointer won't be to unique storage.
address LIR_Assembler::float_constant(float f) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::float_constant)
}

address LIR_Assembler::double_constant(double d) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::double_constant)
}

address LIR_Assembler::int_constant(jlong n) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::int_constant)
}

void LIR_Assembler::casw(Register addr, Register newval, Register cmpval) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::casw)
}

void LIR_Assembler::caswu(Register addr, Register newval, Register cmpval) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::caswu)
}

void LIR_Assembler::casl(Register addr, Register newval, Register cmpval) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::casl)
}

void LIR_Assembler::deoptimize_trap(CodeEmitInfo *info) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::deoptimize_trap)
}

void LIR_Assembler::check_exact_klass(Register tmp, ciKlass* exact_klass) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::check_exact_klass)
}

void LIR_Assembler::get_op(BasicType type) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::get_op)
}

// emit_opTypeCheck sub functions
void LIR_Assembler::typecheck_lir_store(LIR_OpTypeCheck* op, bool should_profile) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::typecheck_lir_store)
}

void LIR_Assembler::lir_store_slowcheck(Register k_RInfo, Register klass_RInfo, Register Rtmp1,
                                        Label* success_target, Label* failure_target) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::lir_store_slowcheck)
}

void LIR_Assembler::const2reg_helper(LIR_Opr src) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::const2reg_helper)
}

void LIR_Assembler::logic_op_reg32(Register dst, Register left, Register right, LIR_Code code) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::logic_op_reg32)
}

void LIR_Assembler::logic_op_reg(Register dst, Register left, Register right, LIR_Code code) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::logic_op_reg)
}

void LIR_Assembler::logic_op_imm(Register dst, Register left, int right, LIR_Code code) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::logic_op_imm)
}

void LIR_Assembler::store_parameter(Register r, int offset_from_rsp_in_words) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::store_parameter)
}

void LIR_Assembler::store_parameter(jint c, int offset_from_rsp_in_words) {
  Unimplemented(); // IA-64 C1: not yet ported (riscv: LIR_Assembler::store_parameter)
}

#undef __
