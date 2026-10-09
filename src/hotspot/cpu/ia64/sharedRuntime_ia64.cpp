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

// Shared runtime for IA-64: the calling conventions, the i2c/c2i adapters,
// and the blobs compiled code needs (call resolution, safepoint polls,
// deoptimization, the method-handle intrinsics' wrappers). Still traps: the
// JFR stubs (C2 only) and JNI wrappers for real native methods, which run
// interpreted (PreferInterpreterNativeStubs).
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

// ---------------------------------------------------------------------------
// JNI native wrappers (after riscv). The wrapper is called with the compiled
// Java convention (r20-r27, f8-f15, then stack slots above the caller's
// psABI scratch area) and calls the native function with the C convention
// (out0-out7, f8-f15 by FP-argument count, then stack slots above our own
// scratch area). JNIEnv* (and the class, for a static method) are extra
// leading integer arguments, so an FP argument keeps its FP register --
// unless the extra positions push it past the eighth and onto the stack.
// Integer arguments always change register file (r20.. -> r32..), so no move
// can overwrite a source still to be read.
//
// Frame, from sp up (4-byte stack slots):
//
//   sp + 0         psABI scratch (out_preserve_stack_slots)
//                  outgoing C stack arguments
//   oop_handle     8 words: handles for the Java argument registers
//   klass          the class mirror's handle slot (static)
//   lock           the BasicLock (synchronized)
//   result         the native result across runtime calls: 16-byte stf.spill
//                  image of f8, then r8
//   fp - 16        caller's fp
//   fp - 8         return address
//
// r6/r7 (Rbcp/Resp: C-preserved, unused by compiled code) hold the oop
// handle and the BasicLock address across the native call.

static int reg2offset_in(VMReg r) {
  // An incoming stack argument, from fp (= the caller's sp).
  return (r->reg2stack() + SharedRuntime::out_preserve_stack_slots()) * VMRegImpl::stack_slot_size;
}

static int reg2offset_out(VMReg r) {
  // An outgoing stack argument, from sp.
  return (r->reg2stack() + SharedRuntime::out_preserve_stack_slots()) * VMRegImpl::stack_slot_size;
}

// Move a non-oop, non-FP Java argument to its C position. 32-bit values are
// kept sign-extended (as compiled code keeps ints), which is a valid C value.
static void move_int(MacroAssembler* masm, VMRegPair src, VMRegPair dst, bool is_long) {
  Register value;
  if (src.first()->is_stack()) {
    value = t2;
    if (is_long) {
      __ ld8(value, Address(fp, reg2offset_in(src.first())));
    } else {
      __ ld4s(value, Address(fp, reg2offset_in(src.first())));
    }
  } else {
    value = src.first()->as_Register();
  }
  if (dst.first()->is_stack()) {
    __ st8(Address(sp, reg2offset_out(dst.first())), value, t1);
  } else {
    __ mov(dst.first()->as_Register(), value);
  }
}

// FP arguments: a Java float is single-typed in its register, a double
// double-typed, so stfs/stfd store them exactly (ISA-NOTES.md).
static void move_fp(MacroAssembler* masm, VMRegPair src, VMRegPair dst, bool is_double) {
  FloatRegister value;
  if (src.first()->is_stack()) {
    value = f6;
    __ add_imm(t1, fp, reg2offset_in(src.first()));
    if (is_double) __ Assembler::ldfd(value, t1); else __ Assembler::ldfs(value, t1);
  } else {
    value = src.first()->as_FloatRegister();
  }
  if (dst.first()->is_stack()) {
    __ add_imm(t1, sp, reg2offset_out(dst.first()));
    if (is_double) __ Assembler::stfd(t1, value); else __ Assembler::stfs(t1, value);
  } else if (dst.first()->as_FloatRegister() != value) {
    __ fmov(dst.first()->as_FloatRegister(), value);
  }
}

// An oop argument is passed as a handle: the address of a stack word holding
// it (in the caller's frame if it arrived there, else in our handle area), or
// null for a null oop. The oop map describes the word.
static void object_move(MacroAssembler* masm, OopMap* map, int oop_handle_offset,
                        int framesize_in_slots, VMRegPair src, VMRegPair dst,
                        bool is_receiver, int* receiver_offset) {
  Register rHandle = dst.first()->is_stack() ? t3 : dst.first()->as_Register();

  if (src.first()->is_stack()) {
    // Oop is already on the stack as an argument
    int offset_in_older_frame = src.first()->reg2stack() + SharedRuntime::out_preserve_stack_slots();
    map->set_oop(VMRegImpl::stack2reg(offset_in_older_frame + framesize_in_slots));
    if (is_receiver) {
      *receiver_offset = (offset_in_older_frame + framesize_in_slots) * VMRegImpl::stack_slot_size;
    }
    __ add_imm(rHandle, fp, reg2offset_in(src.first()), t1);
    __ Assembler::ld8(t2, rHandle);
  } else {
    // Oop is in a register: store it in the handle area
    const Register rOop = src.first()->as_Register();
    int oop_slot = rOop->encoding() - j_rarg0->encoding();
    assert(0 <= oop_slot && oop_slot < 8, "wrong register");
    oop_slot = oop_slot * VMRegImpl::slots_per_word + oop_handle_offset;
    int offset = oop_slot * VMRegImpl::stack_slot_size;

    map->set_oop(VMRegImpl::stack2reg(oop_slot));
    if (is_receiver) {
      *receiver_offset = offset;
    }
    __ add_imm(rHandle, sp, offset, t1);
    __ Assembler::st8(rHandle, rOop);   // may be null
    __ mov(t2, rOop);
  }
  // a null oop passes a null handle
  __ cmp_eq(ptmp0, ptmp1, t2, zr);
  __ mov(rHandle, zr, ptmp0);
  if (dst.first()->is_stack()) {
    __ st8(Address(sp, reg2offset_out(dst.first())), rHandle, t1);
  }
}

// The native result, kept in the frame across runtime calls.
static void save_native_result(MacroAssembler* masm, int result_offset) {
  __ adds(t1, result_offset, sp);
  __ stf_spill(t1, f8);
  __ adds(t1, result_offset + 16, sp);
  __ Assembler::st8(t1, r8);
}

static void restore_native_result(MacroAssembler* masm, int result_offset) {
  __ adds(t1, result_offset, sp);
  __ ldf_fill(f8, t1);
  __ adds(t1, result_offset + 16, sp);
  __ Assembler::ld8(r8, t1);
}

// The C argument registers, around a C call made after the arguments are in
// place (the synchronized slow path). Below sp, in a temporary extension of
// the frame; the frame's last_Java_sp was recorded before, so stack walks
// are unaffected.
// 16 bytes of psABI scratch for the callee, out0-out7, f8-f15 (spill images).
static const int arg_save_bytes = 16 + 8 * wordSize + 8 * 16;

static void save_args(MacroAssembler* masm) {
  __ adds(sp, -arg_save_bytes, sp);
  for (int i = 0; i < 8; i++) {
    __ adds(t1, 16 + i * wordSize, sp);       // above the scratch area
    __ Assembler::st8(t1, as_Register(c_rarg0->encoding() + i));
  }
  for (int i = 0; i < 8; i++) {
    __ add_imm(t1, sp, 16 + 8 * wordSize + i * 16);
    __ stf_spill(t1, as_FloatRegister(8 + i));
  }
}

static void restore_args(MacroAssembler* masm) {
  for (int i = 0; i < 8; i++) {
    __ adds(t1, 16 + i * wordSize, sp);
    __ Assembler::ld8(as_Register(c_rarg0->encoding() + i), t1);
  }
  for (int i = 0; i < 8; i++) {
    __ add_imm(t1, sp, 16 + 8 * wordSize + i * 16);
    __ ldf_fill(as_FloatRegister(8 + i), t1);
  }
  __ adds(sp, arg_save_bytes, sp);
}

static nmethod* generate_jni_wrapper(MacroAssembler* masm,
                                     const methodHandle& method,
                                     int compile_id,
                                     BasicType* in_sig_bt,
                                     VMRegPair* in_regs,
                                     BasicType ret_type) {
  address native_func = method->native_function();
  assert(native_func != nullptr, "must have function");

  // An OopMap for lock (and class if static)
  OopMapSet* oop_maps = new OopMapSet();
  intptr_t start = (intptr_t)__ pc();

  // We have received a description of where all the java arg are located
  // on entry to the wrapper. We need to convert these args to where
  // the jni function will expect them. To figure out where they go
  // we convert the java signature to a C signature by inserting
  // the hidden arguments as arg[0] and possibly arg[1] (static method)
  const int total_in_args = method->size_of_parameters();
  int total_c_args = total_in_args + (method->is_static() ? 2 : 1);

  BasicType* out_sig_bt = NEW_RESOURCE_ARRAY(BasicType, total_c_args);
  VMRegPair* out_regs   = NEW_RESOURCE_ARRAY(VMRegPair, total_c_args);

  int argc = 0;
  out_sig_bt[argc++] = T_ADDRESS;
  if (method->is_static()) {
    out_sig_bt[argc++] = T_OBJECT;
  }
  for (int i = 0; i < total_in_args ; i++) {
    out_sig_bt[argc++] = in_sig_bt[i];
  }

  // Now figure out where the args must be stored and how much stack space
  // they require.
  int out_arg_slots = SharedRuntime::c_calling_convention(out_sig_bt, out_regs, total_c_args);

  // Compute framesize for the wrapper, in 4-byte stack slots.
  int stack_slots = SharedRuntime::out_preserve_stack_slots() + out_arg_slots;
  stack_slots = align_up(stack_slots, VMRegImpl::slots_per_word);

  // The inbound oop handle area: one word per Java argument register.
  int oop_handle_offset = stack_slots;
  stack_slots += 8 * VMRegImpl::slots_per_word;

  int klass_slot_offset = 0;
  int klass_offset = -1;
  int lock_slot_offset = 0;
  bool is_static = false;

  if (method->is_static()) {
    klass_slot_offset = stack_slots;
    stack_slots += VMRegImpl::slots_per_word;
    klass_offset = klass_slot_offset * VMRegImpl::stack_slot_size;
    is_static = true;
  }

  // Plus a lock if needed
  if (method->is_synchronized()) {
    lock_slot_offset = stack_slots;
    stack_slots += VMRegImpl::slots_per_word;
  }

  // The native result across runtime calls: a 16-byte-aligned spill image
  // of f8, then r8.
  stack_slots = align_up(stack_slots, 16 / VMRegImpl::stack_slot_size);
  int result_offset = stack_slots * VMRegImpl::stack_slot_size;
  stack_slots += (16 + wordSize) / VMRegImpl::stack_slot_size;

  // The return address and saved fp
  stack_slots += 2 * VMRegImpl::slots_per_word;

  stack_slots = align_up(stack_slots, StackAlignmentInBytes / VMRegImpl::stack_slot_size);
  int stack_size = stack_slots * VMRegImpl::stack_slot_size;

  // First thing make an ic check to see if we should even be here
  const Register receiver = j_rarg0;
  __ verify_oop(receiver);
  __ ic_check();

  int vep_offset = ((intptr_t)__ pc()) - start;

  // If we have to make this method not-entrant we'll overwrite its
  // first instruction (NativeJump::patch_verified_entry).
  __ nop();

  // Generate stack overflow check
  __ bang_stack_with_offset(checked_cast<int>(StackOverflow::stack_shadow_zone_size()));

  // Generate a new frame for the wrapper: enter()'s shape at stack_size
  // (C1_MacroAssembler::build_frame). t2/t3 are free at a method entry.
  __ mov_from_br(t2, breturn);
  __ mov(t3, sp);
  __ add_imm(sp, sp, -stack_size);
  __ adds(t1, frame::return_addr_offset * wordSize, t3);
  __ Assembler::st8(t1, t2);
  __ adds(t1, frame::link_offset * wordSize, t3);
  __ Assembler::st8(t1, fp);
  __ mov(fp, t3);

  BarrierSetAssembler* bs = BarrierSet::barrier_set()->barrier_set_assembler();
  bs->nmethod_entry_barrier(masm);

  // Frame is now completed as far as size and linkage.
  int frame_complete = ((intptr_t)__ pc()) - start;

  // r6 holds the handle of the receiver (or the class) across the native call.
  const Register oop_handle_reg = r6;

  // -----------------
  // The Grand Shuffle
  //
  // Arguments move from the Java convention to the C convention. Integer
  // arguments change register file (r20.. -> out0..), FP arguments keep their
  // register or go to the stack, so the order of the moves does not matter
  // (see the comment above).

  // Record esp-based slot for receiver on stack for non-static methods
  int receiver_offset = -1;

  // This is a trick. We double the stack slots so we can claim
  // the oops in the caller's frame. Since we are sure to have
  // more args than the caller doubling is enough to make
  // sure we can capture all the incoming oop args from the
  // caller.
  OopMap* map = new OopMap(stack_slots * 2, 0 /* arg_slots*/);

  for (int i = total_in_args - 1, c_arg = total_c_args - 1; i >= 0; i--, c_arg--) {
    switch (in_sig_bt[i]) {
      case T_ARRAY:
      case T_OBJECT:
        object_move(masm, map, oop_handle_offset, stack_slots, in_regs[i], out_regs[c_arg],
                    ((i == 0) && (!is_static)), &receiver_offset);
        break;
      case T_VOID:
        break;
      case T_FLOAT:
        move_fp(masm, in_regs[i], out_regs[c_arg], false);
        break;
      case T_DOUBLE:
        assert(i + 1 < total_in_args &&
               in_sig_bt[i + 1] == T_VOID &&
               out_sig_bt[c_arg + 1] == T_VOID, "bad arg list");
        move_fp(masm, in_regs[i], out_regs[c_arg], true);
        break;
      case T_LONG:
        move_int(masm, in_regs[i], out_regs[c_arg], true);
        break;
      case T_ADDRESS:
        assert(false, "found T_ADDRESS in java args");
        break;
      default:
        move_int(masm, in_regs[i], out_regs[c_arg], false);
    }
  }

  // Pre-load a static method's oop into c_rarg1.
  if (method->is_static()) {
    // load oop into a register
    __ movoop(c_rarg1, JNIHandles::make_local(method->method_holder()->java_mirror()));

    // Now handlize the static class mirror it's known not-null.
    __ st8(Address(sp, klass_offset), c_rarg1, t1);
    map->set_oop(VMRegImpl::stack2reg(klass_slot_offset));

    // Now get the handle
    __ add_imm(c_rarg1, sp, klass_offset, t1);
  }

  // Change state to native. The recorded pc only needs to point into this
  // code, where the oop map is: the same pc/oopMap serve every call out.
  {
    Label the_pc;
    __ bind(the_pc);
    oop_maps->add_gc_map((intptr_t)__ pc() - start, map);
    __ set_last_Java_frame(sp, noreg, the_pc, t2);
  }

  if (DTraceMethodProbes) {
    Unimplemented();   // IA-64: dtrace probes in native wrappers
  }

  // RedefineClasses() tracing support for obsolete method entry
  if (log_is_enabled(Trace, redefine, class, obsolete)) {
    save_args(masm);
    __ mov_metadata(c_rarg1, method());
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::rc_trace_method_entry),
                    Rthread, c_rarg1);
    restore_args(masm);
  }

  // Lock a synchronized method
  const Register lock_reg = r7;    // the BasicLock, preserved across the call
  const Register obj_reg  = r16;
  const Register ltmp1    = r17;
  const Register ltmp2    = r18;
  const Register ltmp3    = r19;

  Label slow_path_lock;
  Label lock_done;

  if (method->is_synchronized()) {
    assert(LockingMode == LM_LIGHTWEIGHT, "IA-64 supports lightweight locking only");

    // Get the handle (the 2nd argument)
    __ mov(oop_handle_reg, c_rarg1);

    // Get address of the box
    __ add_imm(lock_reg, sp, lock_slot_offset * VMRegImpl::stack_slot_size, t1);

    // Load the oop from the handle
    __ ld8(obj_reg, Address(oop_handle_reg, 0));

    __ lightweight_lock(lock_reg, obj_reg, ltmp1, ltmp2, ltmp3, slow_path_lock);

    // Slow path will re-enter here
    __ bind(lock_done);
  }

  // Finally just about ready to make the JNI call

  // get JNIEnv* which is first argument to native
  __ lea(c_rarg0, Address(Rthread, JavaThread::jni_environment_offset()));

  // Now set thread in native
  __ mov_immediate(t2, _thread_in_native);
  __ membar(MacroAssembler::LoadStore | MacroAssembler::StoreStore);
  __ st4(Address(Rthread, JavaThread::thread_state_offset()), t2);

  // Call the native method, through its function descriptor.
  __ call_c(native_func);

  // Unpack native results: the psABI leaves the upper bits of a narrow
  // integer undefined, and a float result may be in any register format.
  switch (ret_type) {
    case T_BOOLEAN: __ zxt1(r8, r8); __ cmp_eq(ptmp0, ptmp1, r8, zr); __ adds(r8, 1, zr, ptmp1); break;
    case T_CHAR:    __ zxt2(r8, r8); break;
    case T_BYTE:    __ sxt1(r8, r8); break;
    case T_SHORT:   __ sxt2(r8, r8); break;
    case T_INT:     __ sxt4(r8, r8); break;
    case T_FLOAT:
      __ fcmp_unord(ptmp0, ptmp1, f8, f8);
      __ fnorm_s(f8, f8, ptmp1);
      break;
    case T_DOUBLE:
      __ fcmp_unord(ptmp0, ptmp1, f8, f8);
      __ fnorm_d(f8, f8, ptmp1);
      break;
    default: break;
  }

  Label safepoint_in_progress, safepoint_in_progress_done;

  // Switch thread to "native transition" state before reading the
  // synchronization state (see riscv / the interpreter's native entry).
  __ membar(MacroAssembler::LoadStore | MacroAssembler::StoreStore);
  __ mov_immediate(t2, _thread_in_native_trans);
  __ st4(Address(Rthread, JavaThread::thread_state_offset()), t2);

  // Force this write out before the read below
  if (!UseSystemMemoryBarrier) {
    __ membar(MacroAssembler::AnyAny);
  }

  // check for safepoint operation in progress and/or pending suspend requests
  {
    // We need an acquire here to ensure that any subsequent load of the
    // global SafepointSynchronize::_state flag is ordered after this load
    // of the thread-local polling word.
    __ safepoint_poll(safepoint_in_progress, true /* at_return */, true /* acquire */, false /* in_nmethod */);
    __ ld4(t2, Address(Rthread, JavaThread::suspend_flags_offset()));
    __ bnez(t2, safepoint_in_progress);
    __ bind(safepoint_in_progress_done);
  }

  // change thread state
  __ membar(MacroAssembler::LoadStore | MacroAssembler::StoreStore);
  __ mov_immediate(t2, _thread_in_Java);
  __ st4(Address(Rthread, JavaThread::thread_state_offset()), t2);

  Label reguard;
  Label reguard_done;
  __ ld4(t2, Address(Rthread, JavaThread::stack_guard_state_offset()));
  __ cmp4_eq_imm(ptmp0, ptmp1, StackOverflow::stack_guard_yellow_reserved_disabled, t2);
  __ br_cond(reguard, ptmp0);
  __ bind(reguard_done);

  // native result if any is live

  // Unlock
  Label unlock_done;
  Label slow_path_unlock;
  if (method->is_synchronized()) {
    // Get locked oop from the handle we passed to jni
    __ ld8(obj_reg, Address(oop_handle_reg, 0));

    save_native_result(masm, result_offset);
    __ lightweight_unlock(obj_reg, ltmp1, ltmp2, ltmp3, slow_path_unlock);

    // slow path re-enters here
    __ bind(unlock_done);
    restore_native_result(masm, result_offset);
  }

  __ reset_last_Java_frame(false);

  // Unbox oop result, e.g. JNIHandles::resolve result.
  if (is_reference_type(ret_type)) {
    __ resolve_jobject(r8, t2, t3);
  }

  if (CheckJNICalls) {
    // clear_pending_jni_exception_check
    __ st8(Address(Rthread, JavaThread::pending_jni_exception_check_fn_offset()), zr);
  }

  // reset handle block
  __ ld8(t2, Address(Rthread, JavaThread::active_handles_offset()));
  __ st4(Address(t2, JNIHandleBlock::top_offset()), zr);

  __ leave();

#if INCLUDE_JFR
  // We need to do a poll test after unwind in case the sampler
  // managed to sample the native frame after returning to Java. The poll is
  // a predicated branch out of line, as in compiled code (FRAME-DESIGN.md
  // 11.7); the stub records its pc for the return handler blob.
  Label L_return, L_poll_stub, L_poll_pc;
  __ ld8(t2, Address(Rthread, JavaThread::polling_word_offset()));
  __ tbit_nz(ptmp0, ptmp1, t2, exact_log2(SafepointMechanism::poll_bit()));
  __ bind(L_poll_pc);
  __ relocate(relocInfo::poll_return_type);
  __ br_cond(L_poll_stub, ptmp0);
  __ bind(L_return);
#endif // INCLUDE_JFR

  // Any exception pending?
  Label exception_pending;
  __ ld8(t2, Address(Rthread, Thread::pending_exception_offset()));
  __ bnez(t2, exception_pending);

  // We're done
  __ ret();

  // Unexpected paths are out of line and go here

#if INCLUDE_JFR
  __ bind(L_poll_stub);
  assert(SharedRuntime::polling_page_return_handler_blob() != nullptr,
         "polling page return stub not created yet");
  __ la(t2, L_poll_pc, t0);
  __ st8(Address(Rthread, JavaThread::saved_exception_pc_offset()), t2);
  __ far_jump(SharedRuntime::polling_page_return_handler_blob()->entry_point());
#endif // INCLUDE_JFR

  // forward the exception (b0 = the return address, as after leave)
  __ bind(exception_pending);
  __ far_jump(StubRoutines::forward_exception_entry());

  // Slow path locking & unlocking
  if (method->is_synchronized()) {
    __ block_comment("Slow path lock {");
    __ bind(slow_path_lock);

    // has last_Java_frame setup. No exceptions so do vanilla call not call_VM
    // args are (oop obj, BasicLock* lock, JavaThread* thread)

    // protect the args we've loaded
    save_args(masm);

    __ mov(c_rarg0, obj_reg);
    __ mov(c_rarg1, lock_reg);
    __ mov(c_rarg2, Rthread);
    __ call_c(CAST_FROM_FN_PTR(address, SharedRuntime::complete_monitor_locking_C));
    restore_args(masm);

#ifdef ASSERT
    { Label L;
      __ ld8(t2, Address(Rthread, Thread::pending_exception_offset()));
      __ beqz(t2, L);
      __ stop("no pending exception allowed on exit from monitorenter");
      __ bind(L);
    }
#endif
    __ j(lock_done);

    __ block_comment("} Slow path lock");

    __ block_comment("Slow path unlock {");
    __ bind(slow_path_unlock);

    // the native result was saved before the fast path

    // Save pending exception around call to VM (which contains an EXCEPTION_MARK).
    // r6 held the handle, which is no longer needed: it is C-preserved.
    __ ld8(oop_handle_reg, Address(Rthread, Thread::pending_exception_offset()));
    __ st8(Address(Rthread, Thread::pending_exception_offset()), zr);

    __ mov(c_rarg2, Rthread);
    __ mov(c_rarg1, lock_reg);
    __ mov(c_rarg0, obj_reg);
    __ call_c(CAST_FROM_FN_PTR(address, SharedRuntime::complete_monitor_unlocking_C));

#ifdef ASSERT
    {
      Label L;
      __ ld8(t2, Address(Rthread, Thread::pending_exception_offset()));
      __ beqz(t2, L);
      __ stop("no pending exception allowed on exit complete_monitor_unlocking_C");
      __ bind(L);
    }
#endif /* ASSERT */

    __ st8(Address(Rthread, Thread::pending_exception_offset()), oop_handle_reg);

    __ j(unlock_done);

    __ block_comment("} Slow path unlock");
  } // synchronized

  // SLOW PATH Reguard the stack if needed

  __ bind(reguard);
  save_native_result(masm, result_offset);
  __ call_c(CAST_FROM_FN_PTR(address, SharedRuntime::reguard_yellow_pages));
  restore_native_result(masm, result_offset);
  // and continue
  __ j(reguard_done);

  // SLOW PATH safepoint
  {
    __ block_comment("safepoint {");
    __ bind(safepoint_in_progress);

    // Don't use call_VM as it will see a possible pending exception and forward it
    // and never return here preventing us from clearing _last_native_pc down below.
    save_native_result(masm, result_offset);
    __ mov(c_rarg0, Rthread);
    __ call_c(CAST_FROM_FN_PTR(address, JavaThread::check_special_condition_for_native_trans));
    restore_native_result(masm, result_offset);
    __ j(safepoint_in_progress_done);
    __ block_comment("} safepoint");
  }

  __ flush();

  nmethod *nm = nmethod::new_native_nmethod(method,
                                            compile_id,
                                            masm->code(),
                                            vep_offset,
                                            frame_complete,
                                            stack_slots / VMRegImpl::slots_per_word,
                                            (is_static ? in_ByteSize(klass_offset) : in_ByteSize(receiver_offset)),
                                            in_ByteSize(lock_slot_offset*VMRegImpl::stack_slot_size),
                                            oop_maps);
  assert(nm != nullptr, "create native nmethod fail!");
  return nm;
}

// Native wrappers are nmethods: the method-handle intrinsics' dispatch, and
// JNI wrappers for real native methods (generate_jni_wrapper).
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
  return generate_jni_wrapper(masm, method, compile_id, in_sig_bt, in_regs, ret_type);
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
// Blobs for compiled code.

// The deoptimization blob: replaces a compiled frame by the interpreter
// frames its debug info describes (after riscv). Entry points:
//
//   unpack                 the deopt handler of an nmethod whose frame was
//                          marked: b0 = the handler's own address
//                          (LIR_Assembler::emit_deopt_handler);
//   unpack_with_reexecution  a C1 runtime stub with the compiled frame
//                          current: b0 = the return address into it;
//   unpack_with_exception  r8 = exception oop, r28 = throwing pc;
//   unpack_with_exception_in_tls  the same two already in the JavaThread.
//
// Each lays down RegisterSaver's frame directly below the compiled frame's
// sp (its fp is that sp), which is where fetch_unroll_info looks for the
// frame being deoptimized and the registers its debug info names. r6/r7 --
// the interpreter's Rbcp/Resp, preserved by C and unused by compiled code,
// and reloaded by the interpreter's deopt entry -- carry the exec mode and
// the UnrollBlock across the C calls. The r8/f8 results travel bit-exactly
// (st8, stf.spill).
void SharedRuntime::generate_deopt_blob() {
  ResourceMark rm;
  const char* name = SharedRuntime::stub_name(SharedStubId::deopt_id);
  CodeBuffer buffer(name, 16384, 1024);
  MacroAssembler* masm = new MacroAssembler(&buffer);
  int frame_size_in_words = -1;
  OopMap* map = nullptr;
  OopMapSet* oop_maps = new OopMapSet();

  const Register exec_mode = r6;
  const Register unroll    = r7;
  const int r8_off = RegisterSaver::gr_offset_in_bytes(r8);
  const int f8_off = RegisterSaver::fr_spill_off;   // f8 is the first saved FPR

  int start = __ offset();
  Label cont;

  // Normal deoptimization.
  map = RegisterSaver::save_live_registers(masm, &frame_size_in_words);
  __ mov_immediate(exec_mode, Deoptimization::Unpack_deopt);
  __ j(cont);

  // Reexecute case: the return address is the pc whose bci is re-executed.
  int reexecute_offset = __ offset() - start;
  (void) RegisterSaver::save_live_registers(masm, &frame_size_in_words);
  __ mov_immediate(exec_mode, Deoptimization::Unpack_reexecute);
  __ j(cont);

  // Exception case: all registers are dead except the exception oop and pc,
  // which go to the JavaThread; then as unpack_with_exception_in_tls.
  int exception_offset = __ offset() - start;
  __ st8(Address(Rthread, JavaThread::exception_pc_offset()), Rexception_pc);
  __ st8(Address(Rthread, JavaThread::exception_oop_offset()), Rexception);

  int exception_in_tls_offset = __ offset() - start;
  // The return address enter() stores is patched below with the throwing pc.
  (void) RegisterSaver::save_live_registers(masm, &frame_size_in_words);
  __ mov_immediate(exec_mode, Deoptimization::Unpack_exception);
  __ ld8(t2, Address(Rthread, JavaThread::exception_pc_offset()));
  __ st8(Address(fp, frame::return_addr_offset * wordSize), t2);
  __ st8(Address(Rthread, JavaThread::exception_pc_offset()), zr);
#ifdef ASSERT
  {
    // verify that there is no pending exception
    Label no_pending_exception;
    __ ld8(t2, Address(Rthread, Thread::pending_exception_offset()));
    __ beqz(t2, no_pending_exception);
    __ stop("must not have pending exception here");
    __ bind(no_pending_exception);
  }
#endif

  __ bind(cont);

  // UnrollBlock* fetch_unroll_info(JavaThread* thread, int exec_mode).
  // It needs the last Java frame (no fp: the compiled frame is found from
  // sp) and cannot block, so no GC can happen.
  {
    Label retaddr;
    __ set_last_Java_frame(sp, noreg, retaddr, t2);
    __ mov(c_rarg0, Rthread);
    __ mov(c_rarg1, exec_mode);
    __ call_c(CAST_FROM_FN_PTR(address, Deoptimization::fetch_unroll_info));
    __ bind(retaddr);
    // An oopmap telling fetch_unroll_info where to find any register it
    // might need.
    oop_maps->add_gc_map(__ offset() - start, map);
  }
  __ reset_last_Java_frame(false);

  __ mov(unroll, r8);

  __ ld4(exec_mode, Address(unroll, Deoptimization::UnrollBlock::unpack_kind_offset()));
  Label noException;
  __ cmp4_eq_imm(ptmp0, ptmp1, Deoptimization::Unpack_exception, exec_mode);
  __ br_cond(noException, ptmp1);   // Was exception pending?
  __ ld8(Rexception, Address(Rthread, JavaThread::exception_oop_offset()));
  __ st8(Address(Rthread, JavaThread::exception_oop_offset()), zr);
  __ st8(Address(Rthread, JavaThread::exception_pc_offset()), zr);
  __ verify_oop(Rexception);
  // Overwrite the result register with the exception oop.
  __ st8(Address(sp, r8_off), Rexception);
  __ bind(noException);

  // Only register save data is on the stack. Restore the result registers;
  // everything else is either dead or captured in the vframeArray.
  __ adds(t2, f8_off, sp);
  __ ldf_fill(f8, t2);
  __ ld8(r8, Address(sp, r8_off));

  // Pop the self-frame and the deoptimized frame (youngest to oldest):
  //   1: self-frame (this blob's)
  //   2: deoptimized frame
  //   3: caller of the deoptimized frame (could be compiled/interpreted)
  // The deoptimized frame starts at our fp; its own linkage is at its top.
  // Everything is loaded before sp moves up: there is no red zone.
  __ ld4(t3, Address(unroll, Deoptimization::UnrollBlock::size_of_deoptimized_frame_offset()));
  __ add(t3, fp, t3);                                         // frame 3's sp
  __ ld8(t4, Address(t3, frame::return_addr_offset * wordSize));  // return into frame 3
  __ ld8(t2, Address(t3, frame::link_offset * wordSize));         // frame 3's fp
  __ mov(sp, t3);
  __ mov(fp, t2);

  // Load the arrays of frame pcs and frame sizes, and the frame count.
  const Register pcs    = r14;
  const Register sizes  = r15;
  const Register count  = r16;
  const Register sender_sp = r17;
  __ ld8(pcs,   Address(unroll, Deoptimization::UnrollBlock::frame_pcs_offset()));
  __ ld8(sizes, Address(unroll, Deoptimization::UnrollBlock::frame_sizes_offset()));
  __ ld4(count, Address(unroll, Deoptimization::UnrollBlock::number_of_frames_offset()));

  // Now adjust the caller's stack to make up for the extra locals but
  // record the original sp so that we can save it in the skeletal
  // interpreter frame and the stack walking of interpreter_sender will get
  // the unextended sp value and not the "real" sp value.
  __ mov(sender_sp, sp);
  __ ld4(t2, Address(unroll, Deoptimization::UnrollBlock::caller_adjustment_offset()));
  __ sub(sp, sp, t2);

  // Push interpreter frames in a loop. Each gets enter()'s linkage -- the
  // return address frame_pcs[i] and the current fp -- at its top, as an
  // interpreted frame's prologue would leave it (FRAME-DESIGN.md 4.3).
  Label loop;
  __ bind(loop);
  __ ld8_inc(t2, sizes, wordSize);          // frame size in bytes
  __ ld8_inc(t4, pcs, wordSize);            // its return address
  __ mov(t3, sp);                           // the new frame's fp
  __ sub(sp, sp, t2);
  __ adds(t2, frame::return_addr_offset * wordSize, t3);
  __ Assembler::st8(t2, t4);
  __ adds(t2, frame::link_offset * wordSize, t3);
  __ Assembler::st8(t2, fp);
  __ mov(fp, t3);
  // This value is corrected by layout_activation_impl
  __ st8(Address(fp, frame::interpreter_frame_last_sp_offset * wordSize), zr);
  __ st8(Address(fp, frame::interpreter_frame_sender_sp_offset * wordSize), sender_sp); // Make it walkable
  __ mov(sender_sp, sp);                    // Pass sender_sp to next frame
  __ adds(count, -1, count);
  __ bnez(count, loop);

  // Re-push the self-frame, returning to the youngest frame's continuation.
  __ ld8(t4, Address(pcs, 0));
  __ mov_to_br(breturn, t4);
  __ enter();
  // A full sized register save area
  __ add_imm(sp, sp, -(int)RegisterSaver::save_bytes);

  // Restore frame locals after moving the frame
  __ adds(t2, f8_off, sp);
  __ stf_spill(t2, f8);
  __ st8(Address(sp, r8_off), r8);

  // void Deoptimization::unpack_frames(JavaThread* thread, int exec_mode).
  // Fp is set because the frames look interpreted now. The recorded pc only
  // needs to point into this blob.
  {
    Label retaddr;
    __ set_last_Java_frame(sp, fp, retaddr, t2);
    __ mov(c_rarg0, Rthread);
    __ mov(c_rarg1, exec_mode);
    __ call_c(CAST_FROM_FN_PTR(address, Deoptimization::unpack_frames));
    __ bind(retaddr);
    // Set an oopmap for the call site
    oop_maps->add_gc_map(__ offset() - start,
                         new OopMap(frame_size_in_words * VMRegImpl::slots_per_word, 0));
  }

  // Clear fp AND pc
  __ reset_last_Java_frame(true);

  // Collect return values
  __ adds(t2, f8_off, sp);
  __ ldf_fill(f8, t2);
  __ ld8(r8, Address(sp, r8_off));

  // Pop self-frame and jump to the interpreter.
  __ leave();
  __ ret();

  // Make sure all code is generated
  masm->flush();

  _deopt_blob = DeoptimizationBlob::create(&buffer, oop_maps, 0, exception_offset, reexecute_offset,
                                           frame_size_in_words);
  assert(_deopt_blob != nullptr, "create deoptimization blob fail!");
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
