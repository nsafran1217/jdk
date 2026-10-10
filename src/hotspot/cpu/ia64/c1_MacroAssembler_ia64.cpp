/*
 * Copyright (c) 1999, 2025, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, Red Hat Inc. All rights reserved.
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

#include "c1/c1_LIR.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "c1/c1_Runtime1.hpp"
#include "classfile/systemDictionary.hpp"
#include "gc/shared/barrierSetAssembler.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "interpreter/interpreter.hpp"
#include "oops/arrayOop.hpp"
#include "oops/markWord.hpp"
#include "runtime/basicLock.hpp"
#include "runtime/os.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"

// result = -1, 0 or 1 as freg0 <, ==, > freg1; unordered_result if either is
// NaN (fcmpl / fcmpg). The predicates are computed before result is written.
// Clobbers p6-p9.
void C1_MacroAssembler::float_cmp(bool is_float, int unordered_result,
                                  FloatRegister freg0, FloatRegister freg1,
                                  Register result) {
  fcmp_lt(p6, p7, freg0, freg1);
  fcmp_eq(p8, p9, freg0, freg1);
  adds(result, 1, zr);
  adds(result, -1, zr, p6);
  mov(result, zr, p8);
  fcmp_unord(p6, p7, freg0, freg1);
  adds(result, unordered_result, zr, p6);
}

int C1_MacroAssembler::lock_object(Register hdr, Register obj, Register disp_hdr, Register temp, Label& slow_case) {
  assert_different_registers(hdr, obj, disp_hdr, temp, t0, t1, t2, t3);
  assert(LockingMode == LM_LIGHTWEIGHT, "IA-64 supports lightweight locking only");

  verify_oop(obj);

  // save object being locked into the BasicObjectLock
  st8(Address(disp_hdr, BasicObjectLock::obj_offset()), obj);

  // lightweight_lock's first instruction loads obj's mark word: the null check.
  int null_check_offset = offset();
  lightweight_lock(disp_hdr, obj, hdr, temp, t3, slow_case);
  return null_check_offset;
}

void C1_MacroAssembler::unlock_object(Register hdr, Register obj, Register disp_hdr, Register temp, Label& slow_case) {
  assert_different_registers(hdr, obj, disp_hdr, temp, t0, t1, t2, t3);
  assert(LockingMode == LM_LIGHTWEIGHT, "IA-64 supports lightweight locking only");

  // load object
  ld8(obj, Address(disp_hdr, BasicObjectLock::obj_offset()));
  verify_oop(obj);

  lightweight_unlock(obj, hdr, temp, t3, slow_case);
}

// Defines obj, preserves var_size_in_bytes
void C1_MacroAssembler::try_allocate(Register obj, Register var_size_in_bytes, int con_size_in_bytes, Register tmp1, Register tmp2, Label& slow_case) {
  if (UseTLAB) {
    tlab_allocate(obj, var_size_in_bytes, con_size_in_bytes, tmp1, tmp2, slow_case);
  } else {
    j(slow_case);
  }
}

void C1_MacroAssembler::initialize_header(Register obj, Register klass, Register len, Register tmp1, Register tmp2) {
  assert_different_registers(obj, klass, len, tmp1, tmp2);
  assert(!UseCompactObjectHeaders, "IA-64: compact object headers not yet supported");
  // This assumes that all prototype bits fit in an int32_t
  mov_immediate(tmp1, checked_cast<int32_t>(markWord::prototype().value()));
  st8(Address(obj, oopDesc::mark_offset_in_bytes()), tmp1);
  if (UseCompressedClassPointers) { // Take care not to kill klass
    encode_klass_not_null(tmp1, klass);
    st4(Address(obj, oopDesc::klass_offset_in_bytes()), tmp1);
  } else {
    st8(Address(obj, oopDesc::klass_offset_in_bytes()), klass);
  }

  if (len->is_valid()) {
    st4(Address(obj, arrayOopDesc::length_offset_in_bytes()), len);
    int base_offset = arrayOopDesc::length_offset_in_bytes() + BytesPerInt;
    if (!is_aligned(base_offset, BytesPerWord)) {
      assert(is_aligned(base_offset, BytesPerInt), "must be 4-byte aligned");
      // Clear gap/first 4 bytes following the length field.
      st4(Address(obj, base_offset), zr);
    }
  } else if (UseCompressedClassPointers) {
    // the klass gap
    st4(Address(obj, oopDesc::klass_offset_in_bytes() + BytesPerInt), zr);
  }
}

// preserves obj, destroys len_in_bytes
void C1_MacroAssembler::initialize_body(Register obj, Register len_in_bytes, int hdr_size_in_bytes, Register tmp) {
  assert(hdr_size_in_bytes >= 0, "header size must be positive or 0");
  assert_different_registers(obj, len_in_bytes, tmp);
  Label done, loop;

  // len_in_bytes is positive and ptr sized
  add_imm(len_in_bytes, len_in_bytes, -hdr_size_in_bytes, t1);
  beqz(len_in_bytes, done);

  // A word at a time, post-incrementing the store address.
  add_imm(tmp, obj, hdr_size_in_bytes, t1);
  bind(loop);
  st8_inc(tmp, zr, BytesPerWord);
  adds(len_in_bytes, -BytesPerWord, len_in_bytes);
  bnez(len_in_bytes, loop);

  bind(done);
}

void C1_MacroAssembler::allocate_object(Register obj, Register tmp1, Register tmp2, int header_size, int object_size, Register klass, Label& slow_case) {
  assert_different_registers(obj, tmp1, tmp2);
  assert(header_size >= 0 && object_size >= header_size, "illegal sizes");

  try_allocate(obj, noreg, object_size * BytesPerWord, tmp1, tmp2, slow_case);

  initialize_object(obj, klass, noreg, object_size * HeapWordSize, tmp1, tmp2, UseTLAB);
}

void C1_MacroAssembler::initialize_object(Register obj, Register klass, Register var_size_in_bytes, int con_size_in_bytes, Register tmp1, Register tmp2, bool is_tlab_allocated) {
  assert((con_size_in_bytes & MinObjAlignmentInBytesMask) == 0,
         "con_size_in_bytes is not multiple of alignment");
  const int hdr_size_in_bytes = instanceOopDesc::header_size() * HeapWordSize;

  initialize_header(obj, klass, noreg, tmp1, tmp2);

  if (!(UseTLAB && ZeroTLAB && is_tlab_allocated)) {
    // clear rest of allocated space
    if (var_size_in_bytes != noreg) {
      mov(tmp2, var_size_in_bytes);
      initialize_body(obj, tmp2, hdr_size_in_bytes, tmp1);
    } else if (con_size_in_bytes > hdr_size_in_bytes) {
      // explicit stores, post-incrementing the address
      add_imm(tmp1, obj, hdr_size_in_bytes, t1);
      for (int i = hdr_size_in_bytes; i < con_size_in_bytes; i += BytesPerWord) {
        st8_inc(tmp1, zr, BytesPerWord);
      }
    }
  }

  // The new object's contents are visible before the reference to it.
  membar(MacroAssembler::StoreStore);

  if (CURRENT_ENV->dtrace_alloc_probes()) {
    mov(c_rarg0, obj);
    far_call(Runtime1::entry_for(C1StubId::dtrace_object_alloc_id));
  }

  verify_oop(obj);
}

void C1_MacroAssembler::allocate_array(Register obj, Register len, Register tmp1, Register tmp2, int base_offset_in_bytes, int f, Register klass, Label& slow_case, bool zero_array) {
  assert_different_registers(obj, len, tmp1, tmp2, klass);

  // check for negative or excessive length: sign-extended, a negative length
  // is a huge unsigned value (extended in place under C1LazyIntExtension; the
  // int value is unchanged)
  if (C1LazyIntExtension) {
    sxt4(len, len);
  }
  mov_immediate(t2, (int32_t)max_array_allocation_length);
  bgeu(len, t2, slow_case);

  const Register arr_size = tmp2; // okay to be the same
  // align object end
  mov_immediate(arr_size, (int64_t)base_offset_in_bytes + MinObjAlignmentInBytesMask);
  if (f == 0) {
    add(arr_size, len, arr_size);
  } else {
    shladd(arr_size, len, f, arr_size);
  }
  and_imm(arr_size, ~MinObjAlignmentInBytesMask, arr_size);

  try_allocate(obj, arr_size, 0, tmp1, tmp2, slow_case);

  initialize_header(obj, klass, len, tmp1, tmp2);

  // Align-up to word boundary, because we clear the 4 bytes potentially
  // following the length field in initialize_header().
  int base_offset = align_up(base_offset_in_bytes, BytesPerWord);

  // clear rest of allocated space
  const Register len_zero = len;
  if (zero_array) {
    initialize_body(obj, arr_size, base_offset, len_zero);
  }

  // The new array's contents are visible before the reference to it.
  membar(MacroAssembler::StoreStore);

  if (CURRENT_ENV->dtrace_alloc_probes()) {
    mov(c_rarg0, obj);
    far_call(Runtime1::entry_for(C1StubId::dtrace_object_alloc_id));
  }

  verify_oop(obj);
}

// The compiled frame (FRAME-DESIGN.md 4.5, 11.4) has enter()'s shape at any
// size: return address and the caller's fp in the two words below the
// caller's sp, fp = the caller's sp, and sp lowered by framesize, whose
// bottom 16 bytes are the psABI scratch area (FrameMap::
// first_available_sp_in_frame). sp moves before anything is stored below
// it: there is no red zone (MacroAssembler::build_frame_linkage).
void C1_MacroAssembler::set_poll_word_register() {
  if (UsePollWordRegister) {
    adds(Rpoll_word, in_bytes(JavaThread::polling_word_offset()), Rthread);
  }
}

void C1_MacroAssembler::build_frame(int framesize, int bang_size_in_bytes) {
  assert(bang_size_in_bytes >= framesize, "stack bang size incorrect");
  assert(is_aligned(framesize, StackAlignmentInBytes), "frame size must keep sp aligned");
  // Make sure there is enough stack space for this method's activation.
  // Note that we do this before creating a frame.
  generate_stack_overflow_check(bang_size_in_bytes);

  build_frame_linkage(framesize);

  // Before the entry barrier, which must end the prologue: BarrierSetNMethod
  // finds its guard at a fixed distance before the frame-complete offset.
  // (After it, disarming wrote the guard over the barrier's own code.)
  set_poll_word_register();

  // Insert nmethod entry barrier into frame.
  BarrierSetAssembler* bs = BarrierSet::barrier_set()->barrier_set_assembler();
  bs->nmethod_entry_barrier(this);
}

// fp is the caller's sp throughout compiled code, so removing the frame is
// leave(): the return address is left in b0.
void C1_MacroAssembler::remove_frame(int framesize) {
  leave();
}


// The verified entry starts with a nop bundle, which make_not_entrant
// replaces (NativeJump::patch_verified_entry).
void C1_MacroAssembler::verified_entry(bool breakAtEntry) {
  nop();
}

// A parameter LIR_Assembler::store_parameter left at the caller's sp, read
// from inside a stub frame, where fp is the caller's sp (enter()):
//   fp + -2: link
//      + -1: return address
//      +  0: argument with offset 0
//      +  1: argument with offset 1
// The C1 runtime stubs take their arguments in registers instead
// (c1_Runtime1_ia64.cpp); this is for code shaped like the other ports.
void C1_MacroAssembler::load_parameter(int offset_in_words, Register reg) {
  ld8(reg, Address(fp, offset_in_words * BytesPerWord));
}

#ifndef PRODUCT

void C1_MacroAssembler::verify_stack_oop(int stack_offset) {
  // verify_oop is not implemented on IA-64 yet (macroAssembler_ia64.hpp).
}

void C1_MacroAssembler::verify_not_null_oop(Register r) {
  if (!VerifyOops) return;
  Label not_null;
  bnez(r, not_null);
  stop("non-null oop required");
  bind(not_null);
  verify_oop(r);
}

// The flags are named for riscv's registers in the shared declaration; the
// IA-64 runtime stubs do not call this.
void C1_MacroAssembler::invalidate_registers(bool inv_x10, bool inv_x9, bool inv_x12, bool inv_x13, bool inv_x14, bool inv_x15) {
}
#endif // ifndef PRODUCT

// riscv's c1_cmp_branch/c1_float_cmp_branch (compare fused into the branch,
// because riscv has no condition flags) are not used here: on IA-64 lir_cmp
// sets a predicate pair that the following branch or cmove reads, so C1 uses
// the shared flags-style LIR (as on x86 and aarch64). See JIT-SCOPE.md, C1.
