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
#include "registerSaver_ia64.hpp"
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

// ---------------------------------------------------------------------------
// RegisterSaver (registerSaver_ia64.hpp, FRAME-DESIGN.md 11.1)

OopMap* RegisterSaver::save_live_registers(MacroAssembler* masm, int* total_frame_words,
                                           bool describe_fprs, bool with_enter) {
  if (with_enter) {
    __ enter();
  }
  __ adds(sp, -(int)save_bytes, sp);

  __ mov_from_pr(t0);
  __ adds(t1, pr_off, sp);
  __ Assembler::st8(t1, t0);

  for (int i = 0; i < gr_count; i++) {
    __ adds(t1, gr_off + i * wordSize, sp);
    __ Assembler::st8(t1, as_Register(gr_at(i)));
  }

  for (int i = 0; i < fr_count; i++) {
    FloatRegister f = as_FloatRegister(fr_first + i);
    __ adds(t1, fr_spill_off + i * 16, sp);
    __ stf_spill(t1, f);
    if (describe_fprs) {
      // Memory-format copy: fnorm.d makes any float or double value
      // double-typed (exact); NaNs are copied unchanged (fnorm would quieten
      // a signalling NaN). See FRAME-DESIGN.md 11.1.
      __ fmov(f6, f);
      __ fcmp_unord(ptmp0, ptmp1, f, f);
      __ fnorm_d(f6, f, ptmp1);
      __ adds(t1, fr_dbl_off + i * wordSize, sp);
      __ stfd(t1, f6);
    }
  }

  *total_frame_words = frame_size_in_words();
  return oop_map(describe_fprs);
}

OopMap* RegisterSaver::oop_map(bool describe_fprs) {
  OopMap* map = new OopMap(frame_size_in_words() * VMRegImpl::slots_per_word, 0);
  for (int i = 0; i < gr_count; i++) {
    Register r = as_Register(gr_at(i));
    int slot = (gr_off + i * wordSize) / VMRegImpl::stack_slot_size;
    map->set_callee_saved(VMRegImpl::stack2reg(slot), r->as_VMReg());
    map->set_callee_saved(VMRegImpl::stack2reg(slot + 1), r->as_VMReg()->next());
  }
  if (describe_fprs) {
    for (int i = 0; i < fr_count; i++) {
      FloatRegister f = as_FloatRegister(fr_first + i);
      int slot = (fr_dbl_off + i * wordSize) / VMRegImpl::stack_slot_size;
      map->set_callee_saved(VMRegImpl::stack2reg(slot), f->as_VMReg());
      map->set_callee_saved(VMRegImpl::stack2reg(slot + 1), f->as_VMReg()->next());
    }
  }
  return map;
}

void RegisterSaver::restore_live_registers(MacroAssembler* masm, bool with_leave, Register keep) {
  for (int i = 0; i < fr_count; i++) {
    __ adds(t1, fr_spill_off + i * 16, sp);
    __ ldf_fill(as_FloatRegister(fr_first + i), t1);
  }
  for (int i = 0; i < gr_count; i++) {
    if (gr_at(i) == keep->raw_encoding()) continue;
    __ adds(t1, gr_off + i * wordSize, sp);
    __ Assembler::ld8(as_Register(gr_at(i)), t1);
  }
  __ adds(t1, pr_off, sp);
  __ Assembler::ld8(t0, t1);
  __ mov_to_pr(t0, -1);
  __ adds(sp, (int)save_bytes, sp);
  if (with_leave) {
    __ leave();
  }
}

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

// ---------------------------------------------------------------------------
// i2c / c2i adapters
//
// Interpreter side: the arguments are on the caller's expression stack, the
// first at the highest address; Resp points at the last one (word i of n is at
// Resp + (n - i - 1) * 8, a long/double's value in its second word). Rmethod
// holds the callee, b0 the return address.
//
// Compiled side: java_calling_convention -- j_rarg0-7 (r20-r27), j_farg0-7
// (f8-f15), then stack slots, which start above the caller's 16-byte psABI
// scratch area: slot k is at sp + 16 + 4 * k (out_preserve_stack_slots()).

// Before entering the interpreter from compiled code, see whether the callee
// has since been compiled; if so repoint the caller's call site at it.
static void patch_callers_callsite(MacroAssembler *masm) {
  Label L;
  __ ld8(t0, Address(Rmethod, in_bytes(Method::code_offset())));
  __ beqz(t0, L);

  // The caller's call site is the NativeCall before b0. The register saver
  // keeps every argument register, Rmethod and b0 (through enter/leave).
  int frame_words;
  __ mov_from_br(t2, breturn);            // caller pc, before enter() spills b0
  OopMap* map = RegisterSaver::save_live_registers(masm, &frame_words);
  (void)map;
  __ mov(c_rarg0, Rmethod);
  __ mov(c_rarg1, t2);
  __ call_c(CAST_FROM_FN_PTR(address, SharedRuntime::fixup_callers_callsite));
  RegisterSaver::restore_live_registers(masm);
  __ bind(L);
}

static void gen_c2i_adapter(MacroAssembler *masm,
                            int total_args_passed,
                            int comp_args_on_stack,
                            const BasicType *sig_bt,
                            const VMRegPair *regs,
                            Label& skip_fixup) {
  // We've come from compiled code and are attempting to jump to the
  // interpreter, which means the caller made a static call to get here
  // (vcalls always get a compiled target if there is one). Check for a
  // compiled target. If there is one, we need to patch the caller's call.
  patch_callers_callsite(masm);

  __ bind(skip_fixup);

  // Since all args are passed on the stack, total_args_passed *
  // Interpreter::stackElementSize is the space we need.
  int extraspace = align_up(total_args_passed * Interpreter::stackElementSize, 16);

  __ mov(Rsender_sp, sp);
  if (extraspace != 0) {
    __ add_imm(sp, sp, -extraspace);
  }

  const int in_stack_base = extraspace + (int)SharedRuntime::out_preserve_stack_slots() * VMRegImpl::stack_slot_size;

  for (int i = 0; i < total_args_passed; i++) {
    if (sig_bt[i] == T_VOID) {
      assert(i > 0 && (sig_bt[i - 1] == T_LONG || sig_bt[i - 1] == T_DOUBLE), "missing half");
      continue;
    }

    // offset to start parameters
    int st_off   = (total_args_passed - i - 1) * Interpreter::stackElementSize;
    int next_off = st_off - Interpreter::stackElementSize;

    VMReg r_1 = regs[i].first();
    VMReg r_2 = regs[i].second();
    if (!r_1->is_valid()) {
      assert(!r_2->is_valid(), "");
      continue;
    }
    const bool two_words = (sig_bt[i] == T_LONG || sig_bt[i] == T_DOUBLE);
    if (r_1->is_stack()) {
      // memory to memory
      int ld_off = in_stack_base + r_1->reg2stack() * VMRegImpl::stack_slot_size;
      if (!r_2->is_valid()) {
        __ ld4(t2, Address(sp, ld_off));
      } else {
        __ ld8(t2, Address(sp, ld_off));
      }
      __ st8(Address(sp, two_words ? next_off : st_off), t2, t1);
    } else if (r_1->is_Register()) {
      Register r = r_1->as_Register();
      __ st8(Address(sp, (r_2->is_valid() && two_words) ? next_off : st_off), r, t1);
    } else {
      assert(r_1->is_FloatRegister(), "");
      FloatRegister f = r_1->as_FloatRegister();
      // A float argument is single-typed and a double double-typed in its
      // register, so stfs/stfd store them exactly (ISA-NOTES.md).
      if (!r_2->is_valid()) {
        __ add_imm(t1, sp, st_off);
        __ stfs(t1, f);
      } else {
        __ add_imm(t1, sp, next_off);
        __ stfd(t1, f);
      }
    }
  }

  __ mov(Resp, sp); // Interp expects args on caller's expression stack

  __ ld8(t1, Address(Rmethod, in_bytes(Method::interpreter_entry_offset())));
  __ mov_to_br(btmp, t1);
  __ br_cond(btmp);
}

void SharedRuntime::gen_i2c_adapter(MacroAssembler *masm,
                                    int total_args_passed,
                                    int comp_args_on_stack,
                                    const BasicType *sig_bt,
                                    const VMRegPair *regs) {
  // Room for the compiled callee's stack arguments, above the 16-byte psABI
  // scratch area, keeping sp 16-byte aligned. The interpreter's return entry
  // restores sp from the frame, so this adjustment needs no undoing here.
  if (comp_args_on_stack != 0) {
    int bytes = (int)SharedRuntime::out_preserve_stack_slots() * VMRegImpl::stack_slot_size +
                align_up(comp_args_on_stack * VMRegImpl::stack_slot_size, wordSize);
    __ add_imm(t0, sp, -bytes);
    __ and_imm(sp, -16, t0);
  }

  // Will jump to the compiled code just as if compiled code was doing it.
  __ ld8(t1, Address(Rmethod, in_bytes(Method::from_compiled_offset())));

  const int out_stack_base = (int)SharedRuntime::out_preserve_stack_slots() * VMRegImpl::stack_slot_size;

  // Now generate the shuffle code.
  for (int i = 0; i < total_args_passed; i++) {
    if (sig_bt[i] == T_VOID) {
      assert(i > 0 && (sig_bt[i - 1] == T_LONG || sig_bt[i - 1] == T_DOUBLE), "missing half");
      continue;
    }

    assert(!regs[i].second()->is_valid() || regs[i].first()->next() == regs[i].second(),
           "scrambled load targets?");
    // Load in argument order going down.
    int ld_off = (total_args_passed - i - 1) * Interpreter::stackElementSize;
    // Point to interpreter value (vs. tag)
    int next_off = ld_off - Interpreter::stackElementSize;

    VMReg r_1 = regs[i].first();
    VMReg r_2 = regs[i].second();
    if (!r_1->is_valid()) {
      assert(!r_2->is_valid(), "");
      continue;
    }
    const bool two_words = (sig_bt[i] == T_LONG || sig_bt[i] == T_DOUBLE);
    if (r_1->is_stack()) {
      // Convert stack slot to an SP offset
      int st_off = out_stack_base + r_1->reg2stack() * VMRegImpl::stack_slot_size;
      if (!r_2->is_valid()) {
        __ ld4s(t2, Address(Resp, ld_off));
      } else {
        __ ld8(t2, Address(Resp, two_words ? next_off : ld_off));
      }
      __ st8(Address(sp, st_off), t2, t0);
    } else if (r_1->is_Register()) {  // Register argument
      Register r = r_1->as_Register();
      if (r_2->is_valid()) {
        __ ld8(r, Address(Resp, two_words ? next_off : ld_off));
      } else {
        // Compiled code keeps ints sign-extended in registers, as the
        // interpreter does (ld4 would zero-extend a negative int).
        __ ld4s(r, Address(Resp, ld_off));
      }
    } else {
      FloatRegister f = r_1->as_FloatRegister();
      if (!r_2->is_valid()) {
        __ add_imm(t0, Resp, ld_off);
        __ ldfs(f, t0);
      } else {
        __ add_imm(t0, Resp, next_off);
        __ ldfd(f, t0);
      }
    }
  }

  // The callee may be deoptimized before it builds a frame; the VM finds it here.
  __ st8(Address(Rthread, JavaThread::callee_target_offset()), Rmethod, t0);

  __ mov_to_br(btmp, t1);
  __ br_cond(btmp);
}

void SharedRuntime::generate_i2c2i_adapters(MacroAssembler *masm,
                                            int total_args_passed,
                                            int comp_args_on_stack,
                                            const BasicType *sig_bt,
                                            const VMRegPair *regs,
                                            AdapterHandlerEntry* handler) {
  address i2c_entry = __ pc();
  gen_i2c_adapter(masm, total_args_passed, comp_args_on_stack, sig_bt, regs);

  // The unverified entry: an inline-cache call, CompiledICData* in t1.
  address c2i_unverified_entry = __ pc();
  Label skip_fixup;
  {
    __ block_comment("c2i_unverified_entry {");
    __ ic_check();
    __ adds(t2, in_bytes(CompiledICData::speculated_method_offset()), t1);
    __ Assembler::ld8(Rmethod, t2);
    __ ld8(t0, Address(Rmethod, in_bytes(Method::code_offset())));
    __ beqz(t0, skip_fixup);
    __ far_jump(SharedRuntime::get_ic_miss_stub());
    __ block_comment("} c2i_unverified_entry");
  }

  address c2i_entry = __ pc();
  // No fast class-initialization checks on IA-64 (see TemplateTable::_new):
  // there is no c2i_no_clinit_check entry.
  address c2i_no_clinit_check_entry = nullptr;

  BarrierSetAssembler* bs = BarrierSet::barrier_set()->barrier_set_assembler();
  bs->c2i_entry_barrier(masm);

  gen_c2i_adapter(masm, total_args_passed, comp_args_on_stack, sig_bt, regs, skip_fixup);

  handler->set_entry_points(i2c_entry, c2i_entry, c2i_unverified_entry, c2i_no_clinit_check_entry);
}

// The method-handle intrinsics' "native wrappers" (linkToStatic and
// friends, invokeBasic): no native call, just a dispatch on the trailing
// MemberName or the receiver's form, from the compiled calling convention.
static void gen_special_dispatch(MacroAssembler* masm,
                                 const methodHandle& method,
                                 const BasicType* sig_bt,
                                 const VMRegPair* regs) {
  vmIntrinsics::ID iid = method->intrinsic_id();

  // Now write the args into the outgoing interpreter space
  bool     has_receiver   = false;
  Register receiver_reg   = noreg;
  int      member_arg_pos = -1;
  Register member_reg     = noreg;
  int      ref_kind       = MethodHandles::signature_polymorphic_intrinsic_ref_kind(iid);
  if (ref_kind != 0) {
    member_arg_pos = method->size_of_parameters() - 1;  // trailing MemberName argument
    member_reg = r28;  // free at a call: not an argument, not a dispatch temp
    has_receiver = MethodHandles::ref_kind_has_receiver(ref_kind);
  } else if (iid == vmIntrinsics::_invokeBasic) {
    has_receiver = true;
  } else if (iid == vmIntrinsics::_linkToNative) {
    member_arg_pos = method->size_of_parameters() - 1;  // trailing NativeEntryPoint argument
    member_reg = r28;
  } else {
    fatal("unexpected intrinsic id %d", vmIntrinsics::as_int(iid));
  }

  if (member_reg != noreg) {
    // Load the member_arg into register, if necessary.
    SharedRuntime::check_member_name_argument_is_last_argument(method, sig_bt, regs);
    VMReg r = regs[member_arg_pos].first();
    if (r->is_stack()) {
      // No frame is built: the caller's stack arguments sit above its 16-byte
      // psABI scratch area, at our sp.
      int off = (r->reg2stack() + SharedRuntime::out_preserve_stack_slots()) * VMRegImpl::stack_slot_size;
      __ ld8(member_reg, Address(sp, off));
    } else {
      // no data motion is needed
      member_reg = r->as_Register();
    }
  }

  if (has_receiver) {
    // Make sure the receiver is loaded into a register.
    assert(method->size_of_parameters() > 0, "oob");
    assert(sig_bt[0] == T_OBJECT, "receiver argument must be an object");
    VMReg r = regs[0].first();
    assert(r->is_valid(), "bad receiver arg");
    if (r->is_stack()) {
      // Porting note:  This assumes that compiled calling conventions always
      // pass the receiver oop in a register.
      fatal("receiver always in a register");
    } else {
      // no data motion is needed
      receiver_reg = r->as_Register();
    }
  }

  // Figure out which address we are really jumping to:
  MethodHandles::generate_method_handle_dispatch(masm, iid,
                                                 receiver_reg, member_reg, /*for_compiler_entry:*/ true);
}

// Native wrappers are nmethods. Real native methods stay interpreted for now
// (PreferInterpreterNativeStubs, set in VM_Version::initialize; FRAME-DESIGN.md
// 11.5); the method-handle intrinsics are generated here.
nmethod* SharedRuntime::generate_native_wrapper(MacroAssembler* masm,
                                                const methodHandle& method,
                                                int compile_id,
                                                BasicType* in_sig_bt,
                                                VMRegPair* in_regs,
                                                BasicType ret_type) {
  if (method->is_method_handle_intrinsic()) {
    intptr_t start = (intptr_t)__ pc();
    int vep_offset = ((intptr_t)__ pc()) - start;

    // First instruction must be a nop as it may need to be patched on deoptimisation
    __ nop();
    gen_special_dispatch(masm,
                         method,
                         in_sig_bt,
                         in_regs);
    int frame_complete = ((intptr_t)__ pc()) - start;  // not complete, period
    __ flush();
    int stack_slots = SharedRuntime::out_preserve_stack_slots();  // no out slots at all, actually
    return nmethod::new_native_nmethod(method,
                                       compile_id,
                                       masm->code(),
                                       vep_offset,
                                       frame_complete,
                                       stack_slots / VMRegImpl::slots_per_word,
                                       in_ByteSize(-1),
                                       in_ByteSize(-1),
                                       (OopMapSet*)nullptr);
  }
  // A JNI wrapper for a real native method (C1-3).
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

// The safepoint handler for compiled code's polls (FRAME-DESIGN.md 11.4).
// Polls never fault here: a poll that finds the poll bit set branches to an
// out-of-line stub, which stores the poll's pc in the thread's
// saved_exception_pc and jumps here (LIR_Assembler::safepoint_poll,
// C1SafepointPollStub).
//
// A loop poll (!cause_return) arrives with the method's frame live; the
// frame laid down here takes the poll's pc as its return address, so the
// method's frame is its sender at that pc, where the debug info is. On return
// it resumes at the next bundle -- the poll is a predicated branch, and
// returning to it would branch straight back -- unless the return address
// was changed meanwhile (deoptimization). A return poll arrives with the
// method's frame already removed and b0 the return address into its caller.
SafepointBlob* SharedRuntime::generate_handler_blob(SharedStubId id, address call_ptr) {
  assert(is_polling_page_id(id), "expected a polling page stub id");
  ResourceMark rm;
  const char* name = SharedRuntime::stub_name(id);
  CodeBuffer buffer(name, 8192, 512);
  MacroAssembler* masm = new MacroAssembler(&buffer);
  OopMapSet* oop_maps = new OopMapSet();

  int start = __ offset();
  int frame_size_in_words = -1;
  bool cause_return = (id == SharedStubId::polling_page_return_handler_id);

  if (!cause_return) {
    __ ld8(t2, Address(Rthread, JavaThread::saved_exception_pc_offset()));
    __ mov_to_br(breturn, t2);
  }

  // Save Integer and Float registers.
  OopMap* map = RegisterSaver::save_live_registers(masm, &frame_size_in_words);

  // The following is basically a call_VM. However, we need the precise
  // address of the call in order to generate an oopmap.
  Label retaddr;
  __ set_last_Java_frame(sp, fp, retaddr, t2);
  __ mov(c_rarg0, Rthread);
  __ call_c(call_ptr);
  __ bind(retaddr);

  // Set an oopmap for the call site.  This oopmap will map all
  // oop-registers and debug-info registers as callee-saved.  This
  // will allow deoptimization at this safepoint to find all possible
  // debug-info recordings, as well as let GC find all oops.
  oop_maps->add_gc_map(__ offset() - start, map);

  __ reset_last_Java_frame(false);

  Label noException;
  __ ld8(t1, Address(Rthread, Thread::pending_exception_offset()));
  __ beqz(t1, noException);

  // Exception pending: forward it as thrown at the return address.
  RegisterSaver::restore_live_registers(masm);
  __ far_jump(StubRoutines::forward_exception_entry());

  // No exception case
  __ bind(noException);

  if (!cause_return) {
    Label no_adjust;
    // If our stashed return pc was modified by the runtime we avoid touching it
    __ ld8(t2, Address(fp, frame::return_addr_offset * wordSize));
    __ ld8(t3, Address(Rthread, JavaThread::saved_exception_pc_offset()));
    __ bne(t2, t3, no_adjust);
    // Step over the poll branch.
    __ adds(t2, (int)BytesPerBundle, t2);
    __ st8(Address(fp, frame::return_addr_offset * wordSize), t2);
    __ bind(no_adjust);
  }

  // Normal exit, restore registers and exit.
  RegisterSaver::restore_live_registers(masm);
  __ ret();

  // Make sure all code is generated
  masm->flush();

  // Fill-out other meta info
  return SafepointBlob::create(&buffer, oop_maps, frame_size_in_words);
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

// Call-resolution blob: entered by a compiled call site's br.call (b0 = the
// caller's return address) with the Java arguments in their registers.
// Resolves the call in the runtime, then jumps to the resolved entry with the
// callee Method* in Rmethod -- the arguments untouched, b0 restored, so the
// callee returns straight to the original call site.
RuntimeStub* SharedRuntime::generate_resolve_blob(SharedStubId id, address destination) {
  assert(StubRoutines::forward_exception_entry() != nullptr, "must be generated before");
  assert(is_resolve_id(id), "expected a resolve stub id");

  ResourceMark rm;
  const char* name = SharedRuntime::stub_name(id);
  CodeBuffer buffer(name, 8192, 512);
  MacroAssembler* masm = new MacroAssembler(&buffer);

  int frame_size_in_words = -1;
  OopMapSet* oop_maps = new OopMapSet();

  int start = __ offset();
  OopMap* map = RegisterSaver::save_live_registers(masm, &frame_size_in_words);
  int frame_complete = __ offset();

  {
    Label retaddr;
    __ set_last_Java_frame(sp, fp, retaddr, t2);
    __ mov(c_rarg0, Rthread);
    __ call_c(destination);
    __ bind(retaddr);
  }
  // An oopmap for the call site: the saved registers include live values of
  // the caller (its arguments) that GC must see.
  oop_maps->add_gc_map(__ offset() - start, map);

  // r8 holds the entry to jump to, assuming no exception got installed.
  __ reset_last_Java_frame(true);

  Label pending;
  __ ld8(t1, Address(Rthread, Thread::pending_exception_offset()));
  __ bnez(t1, pending);

  // The callee Method* goes to the saved Rmethod; the entry to the saved r9
  // (t2: a temporary, never live across a compiled call), which survives the
  // restore and is jumped through.
  __ get_vm_result_metadata(t2, Rthread);
  __ st8(Address(sp, RegisterSaver::gr_offset_in_bytes(Rmethod)), t2, t1);
  __ st8(Address(sp, RegisterSaver::gr_offset_in_bytes(t2)), r8, t1);

  RegisterSaver::restore_live_registers(masm);
  // Back to the state on entry, b0 = the caller's return address.
  __ mov_to_br(btmp, t2);
  __ br_cond(btmp);

  // Pending exception after the call.
  __ bind(pending);
  RegisterSaver::restore_live_registers(masm);
  // exception pending => remove activation and forward to exception handler
  __ st8(Address(Rthread, JavaThread::vm_result_oop_offset()), zr, t1);
  __ ld8(Rexception, Address(Rthread, Thread::pending_exception_offset()));
  __ far_jump(StubRoutines::forward_exception_entry());

  masm->flush();
  return RuntimeStub::new_runtime_stub(name, &buffer, frame_complete, frame_size_in_words, oop_maps, true);
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
