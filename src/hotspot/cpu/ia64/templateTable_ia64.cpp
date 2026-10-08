/*
 * Copyright (c) 2003, 2025, Oracle and/or its affiliates. All rights reserved.
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
#include "compiler/disassembler.hpp"
#include "gc/shared/barrierSetAssembler.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "gc/shared/tlab_globals.hpp"
#include "interpreter/interp_masm.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "interpreter/templateTable.hpp"
#include "memory/universe.hpp"
#include "oops/method.inline.hpp"
#include "oops/methodData.hpp"
#include "oops/objArrayKlass.hpp"
#include "oops/oop.inline.hpp"
#include "oops/resolvedFieldEntry.hpp"
#include "oops/resolvedIndyEntry.hpp"
#include "oops/resolvedMethodEntry.hpp"
#include "prims/jvmtiExport.hpp"
#include "prims/methodHandles.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/synchronizer.hpp"
#include "utilities/powerOfTwo.hpp"

// The bytecode templates, derived from cpu/riscv/templateTable_riscv.cpp.
//
// Register mapping (the same in interp_masm_ia64.cpp and
// templateInterpreterGenerator_ia64.cpp): riscv x10 -> Rtos (r8), f10 ->
// Ftos (f8), and riscv's scratch x1N -> r2N, here named R11..R17; x28/x29 ->
// R28/R29 (r30/r31). R11 / R13 are also the ArrayIndexOutOfBounds handler's
// index / array registers.
//
// The IA-64 departures, marked "IA-64:" where they happen:
//
// * Ints are kept sign-extended in registers, as riscv keeps them; IA-64 has
//   no 32-bit ALU forms, so int results are re-extended with sxt4 where an
//   operation can carry out of the low 32 bits.
// * There is no integer divide instruction: idiv/irem/ldiv/lrem call C
//   helpers with Java semantics. Float/double divide, remainder and the
//   floating-to-integer conversions call C too (SharedRuntime's helpers where
//   they exist): the hardware sequences differ from Java on NaN and overflow.
//   The FP top-of-stack f8 is already the first C FP argument, and the C
//   return register r8 the integer top-of-stack, so these calls are cheap.
// * Float arithmetic uses the .s completer on every operation (registers are
//   82 bits wide).
// * Bytecode operands are read a byte at a time where unaligned; the
//   4-byte-aligned switch tables are loaded whole and byte-reversed.
// * The return address of an invoke goes in b0, not ra.

#define __ Disassembler::hook<InterpreterMacroAssembler>(__FILE__, __LINE__, _masm)->

static constexpr Register R11 = r21;   // also Raioobe_index
static constexpr Register R12 = r22;
static constexpr Register R13 = r23;   // also Raioobe_array
static constexpr Register R14 = r24;
static constexpr Register R15 = r25;
static constexpr Register R16 = r26;
static constexpr Register R17 = r27;
static constexpr Register R28 = r30;
static constexpr Register R29 = r31;

// Address computation: local variables

static inline Address iaddress(int n) {
  return Address(Rlocals, Interpreter::local_offset_in_bytes(n));
}

static inline Address laddress(int n) {
  return iaddress(n + 1);
}

static inline Address faddress(int n) {
  return iaddress(n);
}

static inline Address daddress(int n) {
  return laddress(n);
}

static inline Address aaddress(int n) {
  return iaddress(n);
}

// r holds the negated local index (locals_index).
static inline Address iaddress(Register r, Register temp, InterpreterMacroAssembler* _masm) {
  _masm->shladd(temp, r, LogBytesPerWord, Rlocals);
  return Address(temp, 0);
}

static inline Address laddress(Register r, Register temp, InterpreterMacroAssembler* _masm) {
  _masm->shladd(temp, r, LogBytesPerWord, Rlocals);
  return Address(temp, Interpreter::local_offset_in_bytes(1));
}

static inline Address faddress(Register r, Register temp, InterpreterMacroAssembler* _masm) {
  return iaddress(r, temp, _masm);
}

static inline Address daddress(Register r, Register temp, InterpreterMacroAssembler* _masm) {
  return laddress(r, temp, _masm);
}

static inline Address aaddress(Register r, Register temp, InterpreterMacroAssembler* _masm) {
  return iaddress(r, temp, _masm);
}

// At top of Java expression stack which may be different than esp().  It
// isn't for category 1 objects.
static inline Address at_tos   () {
  return Address(Resp,  Interpreter::expr_offset_in_bytes(0));
}

static inline Address at_tos_p1() {
  return Address(Resp, Interpreter::expr_offset_in_bytes(1));
}

static inline Address at_tos_p2() {
  return Address(Resp, Interpreter::expr_offset_in_bytes(2));
}

static inline Address at_tos_p3() {
  return Address(Resp, Interpreter::expr_offset_in_bytes(3));
}

static inline Address at_tos_p4() {
  return Address(Resp, Interpreter::expr_offset_in_bytes(4));
}

static inline Address at_tos_p5() {
  return Address(Resp, Interpreter::expr_offset_in_bytes(5));
}

Address TemplateTable::at_bcp(int offset) {
  assert(_desc->uses_bcp(), "inconsistent uses_bcp information");
  return Address(Rbcp, offset);
}

// Push a register value onto the expression stack (riscv's push_reg).
static void push_word(InterpreterMacroAssembler* _masm, Register r) {
  _masm->adds(Resp, -wordSize, Resp);
  _masm->st8(Resp, r);
}

// Branch to a far interpreter entry (an exception entry, say).
static void jump_to(InterpreterMacroAssembler* _masm, address entry, PredicateRegister qp = pTrue) {
  _masm->far_jump(entry, qp);
}

void TemplateTable::patch_bytecode(Bytecodes::Code bc, Register bc_reg,
                                   Register temp_reg, bool load_bc_into_bc_reg /*=true*/,
                                   int byte_no) {
  if (!RewriteBytecodes) { return; }
  Label L_patch_done;

  switch (bc) {
    case Bytecodes::_fast_aputfield:  // fall through
    case Bytecodes::_fast_bputfield:  // fall through
    case Bytecodes::_fast_zputfield:  // fall through
    case Bytecodes::_fast_cputfield:  // fall through
    case Bytecodes::_fast_dputfield:  // fall through
    case Bytecodes::_fast_fputfield:  // fall through
    case Bytecodes::_fast_iputfield:  // fall through
    case Bytecodes::_fast_lputfield:  // fall through
    case Bytecodes::_fast_sputfield: {
      // We skip bytecode quickening for putfield instructions when
      // the put_code written to the constant pool cache is zero.
      // This is required so that every execution of this instruction
      // calls out to InterpreterRuntime::resolve_get_put to do
      // additional, required work.
      assert(byte_no == f1_byte || byte_no == f2_byte, "byte_no out of range");
      assert(load_bc_into_bc_reg, "we use bc_reg as temp");
      __ load_field_entry(temp_reg, bc_reg);
      if (byte_no == f1_byte) {
        __ lea(temp_reg, Address(temp_reg, in_bytes(ResolvedFieldEntry::get_code_offset())));
      } else {
        __ lea(temp_reg, Address(temp_reg, in_bytes(ResolvedFieldEntry::put_code_offset())));
      }
      // Load-acquire the bytecode to match store-release in ResolvedFieldEntry::fill_in()
      __ ld1(temp_reg, temp_reg);
      __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);
      __ mov_immediate(bc_reg, bc);
      __ beqz(temp_reg, L_patch_done);
      break;
    }
    default:
      assert(byte_no == -1, "sanity");
      // the pair bytecodes have already done the load.
      if (load_bc_into_bc_reg) {
        __ mov_immediate(bc_reg, bc);
      }
  }

  if (JvmtiExport::can_post_breakpoint()) {
    Label L_fast_patch;
    // if a breakpoint is present we can't rewrite the stream directly
    __ ld1(temp_reg, at_bcp(0));
    __ cmp_eq_imm(ptmp0, ptmp1, Bytecodes::_breakpoint, temp_reg);
    __ br_cond(L_fast_patch, ptmp1);
    // Let breakpoint table handling rewrite to quicker bytecode
    __ get_method(c_rarg1);
    __ mov(c_rarg2, Rbcp);
    __ mov(c_rarg3, bc_reg);
    __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::set_original_bytecode_at), c_rarg1, c_rarg2, c_rarg3);
    __ j(L_patch_done);
    __ bind(L_fast_patch);
  }

#ifdef ASSERT
  Label L_okay;
  __ ld1(temp_reg, at_bcp(0));
  __ beq(temp_reg, bc_reg, L_okay);
  __ cmp_eq_imm(ptmp0, ptmp1, (int)Bytecodes::java_code(bc), temp_reg);
  __ br_cond(L_okay, ptmp0);
  __ stop("patching the wrong bytecode");
  __ bind(L_okay);
#endif

  // patch bytecode
  __ st1(at_bcp(0), bc_reg);
  __ bind(L_patch_done);
}

// Individual instructions

void TemplateTable::nop() {
  transition(vtos, vtos);
  // nothing to do
}

void TemplateTable::shouldnotreachhere() {
  transition(vtos, vtos);
  __ stop("should not reach here bytecode");
}

void TemplateTable::aconst_null() {
  transition(vtos, atos);
  __ mov(Rtos, zr);
}

void TemplateTable::iconst(int value) {
  transition(vtos, itos);
  __ mov_immediate(Rtos, value);
}

void TemplateTable::lconst(int value) {
  transition(vtos, ltos);
  __ mov_immediate(Rtos, value);
}

// IA-64: f0 and f1 read as +0.0 and +1.0, and 1 + 1 is exact.
void TemplateTable::fconst(int value) {
  transition(vtos, ftos);
  switch (value) {
    case 0: __ fmov_d(Ftos, f0);       break;
    case 1: __ fmov_d(Ftos, f1);       break;
    case 2: __ fadd_s(Ftos, f1, f1);   break;
    default: ShouldNotReachHere();
  }
}

void TemplateTable::dconst(int value) {
  transition(vtos, dtos);
  switch (value) {
    case 0: __ fmov_d(Ftos, f0);       break;
    case 1: __ fmov_d(Ftos, f1);       break;
    default: ShouldNotReachHere();
  }
}

void TemplateTable::bipush() {
  transition(vtos, itos);
  __ ld1s(Rtos, at_bcp(1));
}

void TemplateTable::sipush() {
  transition(vtos, itos);
  __ load_unaligned_be(Rtos, Rbcp, 1, 2, t2);
  __ sxt2(Rtos, Rtos);
}

void TemplateTable::ldc(LdcType type) {
  transition(vtos, vtos);
  Label call_ldc, notFloat, notClass, notInt, Done;

  if (is_ldc_wide(type)) {
   __ get_unsigned_2_byte_index_at_bcp(R11, 1);
  } else {
   __ ld1(R11, at_bcp(1));
  }
  __ get_cpool_and_tags(R12, Rtos);

  const int base_offset = ConstantPool::header_size() * wordSize;
  const int tags_offset = Array<u1>::base_offset_in_bytes();

  // get type
  __ add(R13, Rtos, R11);
  __ ld1(R13, Address(R13, tags_offset));
  __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);

  // unresolved class - get the resolved class
  __ cmp_eq_imm(ptmp0, ptmp1, JVM_CONSTANT_UnresolvedClass, R13);
  __ br_cond(call_ldc, ptmp0);

  // unresolved class in error state - call into runtime to throw the error
  // from the first resolution attempt
  __ cmp_eq_imm(ptmp0, ptmp1, JVM_CONSTANT_UnresolvedClassInError, R13);
  __ br_cond(call_ldc, ptmp0);

  // resolved class - need to call vm to get java mirror of the class
  __ cmp_eq_imm(ptmp0, ptmp1, JVM_CONSTANT_Class, R13);
  __ br_cond(notClass, ptmp1);

  __ bind(call_ldc);
  __ mov_immediate(c_rarg1, is_ldc_wide(type) ? 1 : 0);
  call_VM(Rtos, CAST_FROM_FN_PTR(address, InterpreterRuntime::ldc), c_rarg1);
  __ push_ptr(Rtos);
  __ verify_oop(Rtos);
  __ j(Done);

  __ bind(notClass);
  __ cmp_eq_imm(ptmp0, ptmp1, JVM_CONSTANT_Float, R13);
  __ br_cond(notFloat, ptmp1);

  // ftos
  __ shladd(R11, R11, LogBytesPerWord, R12);
  __ ldfs(Ftos, Address(R11, base_offset));
  __ push_f(Ftos);
  __ j(Done);

  __ bind(notFloat);

  __ cmp_eq_imm(ptmp0, ptmp1, JVM_CONSTANT_Integer, R13);
  __ br_cond(notInt, ptmp1);

  // itos
  __ shladd(R11, R11, LogBytesPerWord, R12);
  __ ld4s(Rtos, Address(R11, base_offset));
  __ push_i(Rtos);
  __ j(Done);

  __ bind(notInt);
  condy_helper(Done);

  __ bind(Done);
}

// Fast path for caching oop constants.
void TemplateTable::fast_aldc(LdcType type) {
  transition(vtos, atos);

  const Register result = Rtos;
  const Register tmp = R11;
  const Register rarg = R12;

  const int index_size = is_ldc_wide(type) ? sizeof(u2) : sizeof(u1);

  Label resolved;

  // We are resolved if the resolved reference cache entry contains a
  // non-null object (String, MethodType, etc.)
  assert_different_registers(result, tmp);
  // register result is trashed by next load, let's use it as temporary register
  __ get_cache_index_at_bcp(tmp, result, 1, index_size);
  __ load_resolved_reference_at_index(result, tmp, R15);
  __ bnez(result, resolved);

  const address entry = CAST_FROM_FN_PTR(address, InterpreterRuntime::resolve_ldc);

  // first time invocation - must resolve first
  __ mov_immediate(rarg, (int)bytecode());
  __ call_VM(result, entry, rarg);

  __ bind(resolved);

  { // Check for the null sentinel.
    // If we just called the VM, it already did the mapping for us,
    // but it's harmless to retry.
    Label notNull;

    // Stash null_sentinel address to get its value later
    __ movl(rarg, (address)Universe::the_null_sentinel_addr());
    __ ld8(tmp, rarg);
    __ resolve_oop_handle(tmp, R15, noreg);
    __ bne(result, tmp, notNull);
    __ mov(result, zr);  // null object reference
    __ bind(notNull);
  }

  if (VerifyOops) {
    // Safe to call with 0 result
    __ verify_oop(result);
  }
}

void TemplateTable::ldc2_w() {
    transition(vtos, vtos);
    Label notDouble, notLong, Done;
    __ get_unsigned_2_byte_index_at_bcp(Rtos, 1);

    __ get_cpool_and_tags(R11, R12);
    const int base_offset = ConstantPool::header_size() * wordSize;
    const int tags_offset = Array<u1>::base_offset_in_bytes();

    // get type
    __ add(R12, R12, Rtos);
    __ ld1(R12, Address(R12, tags_offset));
    __ cmp_eq_imm(ptmp0, ptmp1, JVM_CONSTANT_Double, R12);
    __ br_cond(notDouble, ptmp1);

    // dtos
    __ shladd(R12, Rtos, LogBytesPerWord, R11);
    __ ldfd(Ftos, Address(R12, base_offset));
    __ push_d(Ftos);
    __ j(Done);

    __ bind(notDouble);
    __ cmp_eq_imm(ptmp0, ptmp1, JVM_CONSTANT_Long, R12);
    __ br_cond(notLong, ptmp1);

    // ltos
    __ shladd(Rtos, Rtos, LogBytesPerWord, R11);
    __ ld8(Rtos, Address(Rtos, base_offset));
    __ push_l(Rtos);
    __ j(Done);

    __ bind(notLong);
    condy_helper(Done);
    __ bind(Done);
}

void TemplateTable::condy_helper(Label& Done) {
  const Register obj = Rtos;
  const Register rarg = R11;
  const Register flags = R12;
  const Register off = R13;

  const address entry = CAST_FROM_FN_PTR(address, InterpreterRuntime::resolve_ldc);

  __ mov_immediate(rarg, (int) bytecode());
  __ call_VM(obj, entry, rarg);

  __ get_vm_result_metadata(flags, Rthread);

  // VMr = obj = base address to find primitive value to push
  // VMr2 = flags = (tos, off) using format of CPCE::_flags
  __ mov_immediate(t2, ConstantPoolCache::field_index_mask);
  __ and_(off, flags, t2);
  __ zxt4(off, off);

  __ add(off, obj, off);
  const Address field(off, 0); // base + R---->base + offset

  __ extr_u(flags, flags, ConstantPoolCache::tos_state_shift, ConstantPoolCache::tos_state_bits);

  switch (bytecode()) {
    case Bytecodes::_ldc:   // fall through
    case Bytecodes::_ldc_w: {
      // tos in (itos, ftos, stos, btos, ctos, ztos)
      Label notInt, notFloat, notShort, notByte, notChar, notBool;
      __ cmp_eq_imm(ptmp0, ptmp1, itos, flags);
      __ br_cond(notInt, ptmp1);
      // itos
      __ ld4s(Rtos, field);
      __ push(itos);
      __ j(Done);

      __ bind(notInt);
      __ cmp_eq_imm(ptmp0, ptmp1, ftos, flags);
      __ br_cond(notFloat, ptmp1);
      // ftos
      __ load_float(field);
      __ push(ftos);
      __ j(Done);

      __ bind(notFloat);
      __ cmp_eq_imm(ptmp0, ptmp1, stos, flags);
      __ br_cond(notShort, ptmp1);
      // stos
      __ ld2s(Rtos, field);
      __ push(stos);
      __ j(Done);

      __ bind(notShort);
      __ cmp_eq_imm(ptmp0, ptmp1, btos, flags);
      __ br_cond(notByte, ptmp1);
      // btos
      __ ld1s(Rtos, field);
      __ push(btos);
      __ j(Done);

      __ bind(notByte);
      __ cmp_eq_imm(ptmp0, ptmp1, ctos, flags);
      __ br_cond(notChar, ptmp1);
      // ctos
      __ ld2(Rtos, field);
      __ push(ctos);
      __ j(Done);

      __ bind(notChar);
      __ cmp_eq_imm(ptmp0, ptmp1, ztos, flags);
      __ br_cond(notBool, ptmp1);
      // ztos
      __ ld1s(Rtos, field);
      __ push(ztos);
      __ j(Done);

      __ bind(notBool);
      break;
    }

    case Bytecodes::_ldc2_w: {
      Label notLong, notDouble;
      __ cmp_eq_imm(ptmp0, ptmp1, ltos, flags);
      __ br_cond(notLong, ptmp1);
      // ltos
      __ ld8(Rtos, field);
      __ push(ltos);
      __ j(Done);

      __ bind(notLong);
      __ cmp_eq_imm(ptmp0, ptmp1, dtos, flags);
      __ br_cond(notDouble, ptmp1);
      // dtos
      __ load_double(field);
      __ push(dtos);
      __ j(Done);

      __ bind(notDouble);
      break;
    }

    default:
      ShouldNotReachHere();
  }

  __ stop("bad ldc/condy");
}

// reg = -(the local index at bcp + offset)
void TemplateTable::locals_index(Register reg, int offset) {
  __ ld1(reg, at_bcp(offset));
  __ neg(reg, reg);
}

void TemplateTable::iload() {
  iload_internal();
}

void TemplateTable::nofast_iload() {
  iload_internal(may_not_rewrite);
}

void TemplateTable::iload_internal(RewriteControl rc) {
  transition(vtos, itos);
  if (RewriteFrequentPairs && rc == may_rewrite) {
    Label rewrite, done;
    const Register bc = R14;

    // get next bytecode
    __ ld1(R11, at_bcp(Bytecodes::length_for(Bytecodes::_iload)));

    // if _iload, wait to rewrite to iload2.  We only want to rewrite the
    // last two iloads in a pair.  Comparing against fast_iload means that
    // the next bytecode is neither an iload or a caload, and therefore
    // an iload pair.
    __ cmp_eq_imm(ptmp0, ptmp1, Bytecodes::_iload, R11);
    __ br_cond(done, ptmp0);

    // if _fast_iload rewrite to _fast_iload2
    __ mov_immediate(bc, Bytecodes::_fast_iload2);
    __ cmp_eq_imm(ptmp0, ptmp1, Bytecodes::_fast_iload, R11);
    __ br_cond(rewrite, ptmp0);

    // if _caload rewrite to _fast_icaload
    __ mov_immediate(bc, Bytecodes::_fast_icaload);
    __ cmp_eq_imm(ptmp0, ptmp1, Bytecodes::_caload, R11);
    __ br_cond(rewrite, ptmp0);

    // else rewrite to _fast_iload
    __ mov_immediate(bc, Bytecodes::_fast_iload);

    // rewrite
    // bc: new bytecode
    __ bind(rewrite);
    patch_bytecode(Bytecodes::_iload, bc, R11, false);
    __ bind(done);

  }

  // do iload, get the local value into tos
  locals_index(R11);
  __ ld4s(Rtos, iaddress(R11, Rtos, _masm));
}

void TemplateTable::fast_iload2() {
  transition(vtos, itos);
  locals_index(R11);
  __ ld4s(Rtos, iaddress(R11, Rtos, _masm));
  __ push(itos);
  locals_index(R11, 3);
  __ ld4s(Rtos, iaddress(R11, Rtos, _masm));
}

void TemplateTable::fast_iload() {
  transition(vtos, itos);
  locals_index(R11);
  __ ld4s(Rtos, iaddress(R11, Rtos, _masm));
}

void TemplateTable::lload() {
  transition(vtos, ltos);
  __ ld1(R11, at_bcp(1));
  __ shl_imm(R11, R11, LogBytesPerWord);
  __ sub(R11, Rlocals, R11);
  __ ld8(Rtos, Address(R11, Interpreter::local_offset_in_bytes(1)));
}

void TemplateTable::fload() {
  transition(vtos, ftos);
  locals_index(R11);
  __ ldfs(Ftos, faddress(R11, t2, _masm));
}

void TemplateTable::dload() {
  transition(vtos, dtos);
  __ ld1(R11, at_bcp(1));
  __ shl_imm(R11, R11, LogBytesPerWord);
  __ sub(R11, Rlocals, R11);
  __ ldfd(Ftos, Address(R11, Interpreter::local_offset_in_bytes(1)));
}

void TemplateTable::aload() {
  transition(vtos, atos);
  locals_index(R11);
  __ ld8(Rtos, iaddress(R11, Rtos, _masm));
}

// reg = -(the 2-byte big-endian local index at bcp + 2)
void TemplateTable::locals_index_wide(Register reg) {
  __ load_unaligned_be(reg, Rbcp, 2, 2, t2);
  __ neg(reg, reg);
}

void TemplateTable::wide_iload() {
  transition(vtos, itos);
  locals_index_wide(R11);
  __ ld4s(Rtos, iaddress(R11, t3, _masm));
}

void TemplateTable::wide_lload() {
  transition(vtos, ltos);
  __ load_unaligned_be(R11, Rbcp, 2, 2, t2);
  __ shl_imm(R11, R11, LogBytesPerWord);
  __ sub(R11, Rlocals, R11);
  __ ld8(Rtos, Address(R11, Interpreter::local_offset_in_bytes(1)));
}

void TemplateTable::wide_fload() {
  transition(vtos, ftos);
  locals_index_wide(R11);
  __ ldfs(Ftos, faddress(R11, t3, _masm));
}

void TemplateTable::wide_dload() {
  transition(vtos, dtos);
  __ load_unaligned_be(R11, Rbcp, 2, 2, t2);
  __ shl_imm(R11, R11, LogBytesPerWord);
  __ sub(R11, Rlocals, R11);
  __ ldfd(Ftos, Address(R11, Interpreter::local_offset_in_bytes(1)));
}

void TemplateTable::wide_aload() {
  transition(vtos, atos);
  locals_index_wide(R11);
  __ ld8(Rtos, aaddress(R11, t3, _masm));
}

// ---------------------------------------------------------------------------
// Arrays

// Index in R11 (the AIOOBE handler expects it there), array in |array|.
// On failure, jumps to the AIOOBE entry with the array in R13. Kills t2.
void TemplateTable::index_check(Register array, Register index) {
  // ints are already sign-extended in registers
  const Register length = t2;
  __ ld4(length, Address(array, arrayOopDesc::length_offset_in_bytes()));
  if (index != R11) {
    assert(R11 != array, "different registers");
    __ mov(R11, index);
  }
  Label ok;
  // An unsigned compare catches negative indices too.
  __ bltu(index, length, ok);
  __ mov(R13, array);
  jump_to(_masm, Interpreter::_throw_ArrayIndexOutOfBoundsException_entry);
  __ bind(ok);
}

// dst = array + index << shift (the element address minus the base offset)
static void element_address(InterpreterMacroAssembler* _masm, Register dst,
                            Register array, Register index, int shift) {
  if (shift == 0) {
    _masm->add(dst, array, index);
  } else {
    _masm->shladd(dst, index, shift, array);
  }
}

void TemplateTable::iaload() {
  transition(itos, itos);
  __ mov(R11, Rtos);
  __ pop_ptr(Rtos);
  // Rtos: array
  // R11: index
  index_check(Rtos, R11); // leaves index in R11
  element_address(_masm, Rtos, Rtos, R11, 2);
  __ access_load_at(T_INT, IN_HEAP | IS_ARRAY, Rtos, Address(Rtos, arrayOopDesc::base_offset_in_bytes(T_INT)), noreg, noreg);
}

void TemplateTable::laload() {
  transition(itos, ltos);
  __ mov(R11, Rtos);
  __ pop_ptr(Rtos);
  index_check(Rtos, R11);
  element_address(_masm, Rtos, Rtos, R11, 3);
  __ access_load_at(T_LONG, IN_HEAP | IS_ARRAY, Rtos, Address(Rtos, arrayOopDesc::base_offset_in_bytes(T_LONG)), noreg, noreg);
}

void TemplateTable::faload() {
  transition(itos, ftos);
  __ mov(R11, Rtos);
  __ pop_ptr(Rtos);
  index_check(Rtos, R11);
  element_address(_masm, Rtos, Rtos, R11, 2);
  __ access_load_at(T_FLOAT, IN_HEAP | IS_ARRAY, Rtos, Address(Rtos, arrayOopDesc::base_offset_in_bytes(T_FLOAT)), t2, noreg);
}

void TemplateTable::daload() {
  transition(itos, dtos);
  __ mov(R11, Rtos);
  __ pop_ptr(Rtos);
  index_check(Rtos, R11);
  element_address(_masm, Rtos, Rtos, R11, 3);
  __ access_load_at(T_DOUBLE, IN_HEAP | IS_ARRAY, Rtos, Address(Rtos, arrayOopDesc::base_offset_in_bytes(T_DOUBLE)), t2, noreg);
}

void TemplateTable::aaload() {
  transition(itos, atos);
  __ mov(R11, Rtos);
  __ pop_ptr(Rtos);
  index_check(Rtos, R11);
  element_address(_masm, Rtos, Rtos, R11, LogBytesPerHeapOop);
  __ load_heap_oop(Rtos, Address(Rtos, arrayOopDesc::base_offset_in_bytes(T_OBJECT)), R28, R29, IS_ARRAY);
}

void TemplateTable::baload() {
  transition(itos, itos);
  __ mov(R11, Rtos);
  __ pop_ptr(Rtos);
  index_check(Rtos, R11);
  element_address(_masm, Rtos, Rtos, R11, 0);
  __ access_load_at(T_BYTE, IN_HEAP | IS_ARRAY, Rtos, Address(Rtos, arrayOopDesc::base_offset_in_bytes(T_BYTE)), noreg, noreg);
}

void TemplateTable::caload() {
  transition(itos, itos);
  __ mov(R11, Rtos);
  __ pop_ptr(Rtos);
  index_check(Rtos, R11);
  element_address(_masm, Rtos, Rtos, R11, 1);
  __ access_load_at(T_CHAR, IN_HEAP | IS_ARRAY, Rtos, Address(Rtos, arrayOopDesc::base_offset_in_bytes(T_CHAR)), noreg, noreg);
}

// iload followed by caload frequent pair
void TemplateTable::fast_icaload() {
  transition(vtos, itos);
  // load index out of locals
  locals_index(R12);
  __ ld4s(R11, iaddress(R12, R11, _masm));
  __ pop_ptr(Rtos);
  index_check(Rtos, R11);
  element_address(_masm, Rtos, Rtos, R11, 1);
  __ access_load_at(T_CHAR, IN_HEAP | IS_ARRAY, Rtos, Address(Rtos, arrayOopDesc::base_offset_in_bytes(T_CHAR)), noreg, noreg);
}

void TemplateTable::saload() {
  transition(itos, itos);
  __ mov(R11, Rtos);
  __ pop_ptr(Rtos);
  index_check(Rtos, R11);
  element_address(_masm, Rtos, Rtos, R11, 1);
  __ access_load_at(T_SHORT, IN_HEAP | IS_ARRAY, Rtos, Address(Rtos, arrayOopDesc::base_offset_in_bytes(T_SHORT)), noreg, noreg);
}

void TemplateTable::iload(int n) {
  transition(vtos, itos);
  __ ld4s(Rtos, iaddress(n));
}

void TemplateTable::lload(int n) {
  transition(vtos, ltos);
  __ ld8(Rtos, laddress(n));
}

void TemplateTable::fload(int n) {
  transition(vtos, ftos);
  __ ldfs(Ftos, faddress(n));
}

void TemplateTable::dload(int n) {
  transition(vtos, dtos);
  __ ldfd(Ftos, daddress(n));
}

void TemplateTable::aload(int n) {
  transition(vtos, atos);
  __ ld8(Rtos, iaddress(n));
}

void TemplateTable::aload_0() {
  aload_0_internal();
}

void TemplateTable::nofast_aload_0() {
  aload_0_internal(may_not_rewrite);
}

void TemplateTable::aload_0_internal(RewriteControl rc) {
  // According to bytecode histograms, the pairs:
  //
  // _aload_0, _fast_igetfield
  // _aload_0, _fast_agetfield
  // _aload_0, _fast_fgetfield
  //
  // occur frequently. If RewriteFrequentPairs is set, the (slow)
  // _aload_0 bytecode checks if the next bytecode is either
  // _fast_igetfield, _fast_agetfield or _fast_fgetfield and then
  // rewrites the current bytecode into a pair bytecode; otherwise it
  // rewrites the current bytecode into _fast_aload_0 that doesn't do
  // the pair check anymore.
  //
  // Note: If the next bytecode is _getfield, the rewrite must be
  //       delayed, otherwise we may miss an opportunity for a pair.
  if (RewriteFrequentPairs && rc == may_rewrite) {
    Label rewrite, done;
    const Register bc = R14;

    // get next bytecode
    __ ld1(R11, at_bcp(Bytecodes::length_for(Bytecodes::_aload_0)));

    // if _getfield then wait with rewrite
    __ cmp_eq_imm(ptmp0, ptmp1, Bytecodes::_getfield, R11);
    __ br_cond(done, ptmp0);

    // if _igetfield then rewrite to _fast_iaccess_0
    assert(Bytecodes::java_code(Bytecodes::_fast_iaccess_0) == Bytecodes::_aload_0, "fix bytecode definition");
    __ mov_immediate(bc, Bytecodes::_fast_iaccess_0);
    __ mov_immediate(t2, Bytecodes::_fast_igetfield);
    __ beq(R11, t2, rewrite);

    // if _agetfield then rewrite to _fast_aaccess_0
    assert(Bytecodes::java_code(Bytecodes::_fast_aaccess_0) == Bytecodes::_aload_0, "fix bytecode definition");
    __ mov_immediate(bc, Bytecodes::_fast_aaccess_0);
    __ mov_immediate(t2, Bytecodes::_fast_agetfield);
    __ beq(R11, t2, rewrite);

    // if _fgetfield then rewrite to _fast_faccess_0
    assert(Bytecodes::java_code(Bytecodes::_fast_faccess_0) == Bytecodes::_aload_0, "fix bytecode definition");
    __ mov_immediate(bc, Bytecodes::_fast_faccess_0);
    __ mov_immediate(t2, Bytecodes::_fast_fgetfield);
    __ beq(R11, t2, rewrite);

    // else rewrite to _fast_aload0
    assert(Bytecodes::java_code(Bytecodes::_fast_aload_0) == Bytecodes::_aload_0, "fix bytecode definition");
    __ mov_immediate(bc, Bytecodes::_fast_aload_0);

    // rewrite
    // bc: new bytecode
    __ bind(rewrite);
    patch_bytecode(Bytecodes::_aload_0, bc, R11, false);

    __ bind(done);
  }

  // Do actual aload_0 (must do this after patch_bytecode which might call VM and GC might change oop).
  aload(0);
}

void TemplateTable::istore() {
  transition(itos, vtos);
  locals_index(R11);
  __ st8(iaddress(R11, t2, _masm), Rtos);
}

void TemplateTable::lstore() {
  transition(ltos, vtos);
  locals_index(R11);
  __ st8(laddress(R11, t2, _masm), Rtos);
}

void TemplateTable::fstore() {
  transition(ftos, vtos);
  locals_index(R11);
  __ stfs(iaddress(R11, t2, _masm), Ftos);
}

void TemplateTable::dstore() {
  transition(dtos, vtos);
  locals_index(R11);
  __ stfd(daddress(R11, t2, _masm), Ftos);
}

void TemplateTable::astore() {
  transition(vtos, vtos);
  __ pop_ptr(Rtos);
  locals_index(R11);
  __ st8(aaddress(R11, t2, _masm), Rtos);
}

void TemplateTable::wide_istore() {
  transition(vtos, vtos);
  __ pop_i();
  locals_index_wide(R11);
  __ st8(iaddress(R11, t3, _masm), Rtos);
}

void TemplateTable::wide_lstore() {
  transition(vtos, vtos);
  __ pop_l();
  locals_index_wide(R11);
  __ st8(laddress(R11, t3, _masm), Rtos);
}

void TemplateTable::wide_fstore() {
  transition(vtos, vtos);
  __ pop_f();
  locals_index_wide(R11);
  __ stfs(faddress(R11, t3, _masm), Ftos);
}

void TemplateTable::wide_dstore() {
  transition(vtos, vtos);
  __ pop_d();
  locals_index_wide(R11);
  __ stfd(daddress(R11, t3, _masm), Ftos);
}

void TemplateTable::wide_astore() {
  transition(vtos, vtos);
  __ pop_ptr(Rtos);
  locals_index_wide(R11);
  __ st8(aaddress(R11, t3, _masm), Rtos);
}

void TemplateTable::iastore() {
  transition(itos, vtos);
  __ pop_i(R11);
  __ pop_ptr(R13);
  // Rtos: value
  // R11: index
  // R13: array
  index_check(R13, R11); // prefer index in R11
  element_address(_masm, t3, R13, R11, 2);
  __ access_store_at(T_INT, IN_HEAP | IS_ARRAY, Address(t3, arrayOopDesc::base_offset_in_bytes(T_INT)), Rtos, noreg, noreg, noreg);
}

void TemplateTable::lastore() {
  transition(ltos, vtos);
  __ pop_i(R11);
  __ pop_ptr(R13);
  index_check(R13, R11);
  element_address(_masm, t3, R13, R11, 3);
  __ access_store_at(T_LONG, IN_HEAP | IS_ARRAY, Address(t3, arrayOopDesc::base_offset_in_bytes(T_LONG)), Rtos, noreg, noreg, noreg);
}

void TemplateTable::fastore() {
  transition(ftos, vtos);
  __ pop_i(R11);
  __ pop_ptr(R13);
  index_check(R13, R11);
  element_address(_masm, t3, R13, R11, 2);
  __ access_store_at(T_FLOAT, IN_HEAP | IS_ARRAY, Address(t3, arrayOopDesc::base_offset_in_bytes(T_FLOAT)), noreg /* ftos */, noreg, noreg, noreg);
}

void TemplateTable::dastore() {
  transition(dtos, vtos);
  __ pop_i(R11);
  __ pop_ptr(R13);
  index_check(R13, R11);
  element_address(_masm, t3, R13, R11, 3);
  __ access_store_at(T_DOUBLE, IN_HEAP | IS_ARRAY, Address(t3, arrayOopDesc::base_offset_in_bytes(T_DOUBLE)), noreg /* dtos */, noreg, noreg, noreg);
}

void TemplateTable::aastore() {
  Label is_null, ok_is_subtype, done;
  transition(vtos, vtos);
  // stack: ..., array, index, value
  __ ld8(Rtos, at_tos());    // value
  __ ld4s(R12, at_tos_p1()); // index
  __ ld8(R13, at_tos_p2());  // array

  index_check(R13, R12);     // kills R11
  element_address(_masm, R14, R13, R12, LogBytesPerHeapOop);
  __ adds(R14, arrayOopDesc::base_offset_in_bytes(T_OBJECT), R14);

  Address element_addr(R14, 0);

  // do array store check - check for null value first
  __ beqz(Rtos, is_null);

  // Move subklass into R11
  __ load_klass(R11, Rtos);
  // Move superklass into Rtos
  __ load_klass(Rtos, R13);
  __ ld8(Rtos, Address(Rtos, ObjArrayKlass::element_klass_offset()));

  // Generate subtype check.  Blows R12, R15
  // Superklass in Rtos.  Subklass in R11.
  __ gen_subtype_check(R11, ok_is_subtype);

  // Come here on failure
  // object is at TOS
  jump_to(_masm, Interpreter::_throw_ArrayStoreException_entry);

  // Come here on success
  __ bind(ok_is_subtype);

  // Get the value we will store
  __ ld8(Rtos, at_tos());
  // Now store using the appropriate barrier
  __ store_heap_oop(element_addr, Rtos, R28, R29, R13, IS_ARRAY);
  __ j(done);

  // Have a null in Rtos, R13=array, R12=index.  Store null at ary[idx]
  __ bind(is_null);

  // Store a null
  __ store_heap_oop(element_addr, noreg, R28, R29, R13, IS_ARRAY);

  // Pop stack arguments
  __ bind(done);
  __ adds(Resp, 3 * Interpreter::stackElementSize, Resp);
}

void TemplateTable::bastore() {
  transition(itos, vtos);
  __ pop_i(R11);
  __ pop_ptr(R13);
  // Rtos: value
  // R11: index
  // R13: array
  index_check(R13, R11); // prefer index in R11

  // Need to check whether array is boolean or byte
  // since both types share the bastore bytecode.
  __ load_klass(R12, R13);
  __ ld4(R12, Address(R12, Klass::layout_helper_offset()));
  __ tbit_nz(ptmp0, ptmp1, R12, exact_log2(Klass::layout_helper_boolean_diffbit()));
  __ and_imm(Rtos, 1, Rtos, ptmp0);  // if it is a T_BOOLEAN array, mask the stored value to 0/1

  element_address(_masm, t3, R13, R11, 0);
  __ access_store_at(T_BYTE, IN_HEAP | IS_ARRAY, Address(t3, arrayOopDesc::base_offset_in_bytes(T_BYTE)), Rtos, noreg, noreg, noreg);
}

void TemplateTable::castore() {
  transition(itos, vtos);
  __ pop_i(R11);
  __ pop_ptr(R13);
  index_check(R13, R11);
  element_address(_masm, t3, R13, R11, 1);
  __ access_store_at(T_CHAR, IN_HEAP | IS_ARRAY, Address(t3, arrayOopDesc::base_offset_in_bytes(T_CHAR)), Rtos, noreg, noreg, noreg);
}

void TemplateTable::sastore() {
  castore();
}

void TemplateTable::istore(int n) {
  transition(itos, vtos);
  __ st8(iaddress(n), Rtos);
}

void TemplateTable::lstore(int n) {
  transition(ltos, vtos);
  __ st8(laddress(n), Rtos);
}

void TemplateTable::fstore(int n) {
  transition(ftos, vtos);
  __ stfs(faddress(n), Ftos);
}

void TemplateTable::dstore(int n) {
  transition(dtos, vtos);
  __ stfd(daddress(n), Ftos);
}

void TemplateTable::astore(int n) {
  transition(vtos, vtos);
  __ pop_ptr(Rtos);
  __ st8(iaddress(n), Rtos);
}

// ---------------------------------------------------------------------------
// Stack manipulation

void TemplateTable::pop() {
  transition(vtos, vtos);
  __ adds(Resp, Interpreter::stackElementSize, Resp);
}

void TemplateTable::pop2() {
  transition(vtos, vtos);
  __ adds(Resp, 2 * Interpreter::stackElementSize, Resp);
}

void TemplateTable::dup() {
  transition(vtos, vtos);
  __ ld8(Rtos, Resp);
  push_word(_masm, Rtos);
  // stack: ..., a, a
}

void TemplateTable::dup_x1() {
  transition(vtos, vtos);
  // stack: ..., a, b
  __ ld8(Rtos, at_tos());     // load b
  __ ld8(R12, at_tos_p1());   // load a
  __ st8(at_tos_p1(), Rtos);  // store b
  __ st8(at_tos(), R12);      // store a
  push_word(_masm, Rtos);     // push b
  // stack: ..., b, a, b
}

void TemplateTable::dup_x2() {
  transition(vtos, vtos);
  // stack: ..., a, b, c
  __ ld8(Rtos, at_tos());     // load c
  __ ld8(R12, at_tos_p2());   // load a
  __ st8(at_tos_p2(), Rtos);  // store c in a
  push_word(_masm, Rtos);     // push c
  // stack: ..., c, b, c, c
  __ ld8(Rtos, at_tos_p2());  // load b
  __ st8(at_tos_p2(), R12);   // store a in b
  // stack: ..., c, a, c, c
  __ st8(at_tos_p1(), Rtos);  // store b in c
  // stack: ..., c, a, b, c
}

void TemplateTable::dup2() {
  transition(vtos, vtos);
  // stack: ..., a, b
  __ ld8(Rtos, at_tos_p1());  // load a
  push_word(_masm, Rtos);     // push a
  __ ld8(Rtos, at_tos_p1());  // load b
  push_word(_masm, Rtos);     // push b
  // stack: ..., a, b, a, b
}

void TemplateTable::dup2_x1() {
  transition(vtos, vtos);
  // stack: ..., a, b, c
  __ ld8(R12, at_tos());      // load c
  __ ld8(Rtos, at_tos_p1());  // load b
  push_word(_masm, Rtos);     // push b
  push_word(_masm, R12);      // push c
  // stack: ..., a, b, c, b, c
  __ st8(at_tos_p3(), R12);   // store c in b
  // stack: ..., a, c, c, b, c
  __ ld8(R12, at_tos_p4());   // load a
  __ st8(at_tos_p2(), R12);   // store a in 2nd c
  // stack: ..., a, c, a, b, c
  __ st8(at_tos_p4(), Rtos);  // store b in a
  // stack: ..., b, c, a, b, c
}

void TemplateTable::dup2_x2() {
  transition(vtos, vtos);
  // stack: ..., a, b, c, d
  __ ld8(R12, at_tos());      // load d
  __ ld8(Rtos, at_tos_p1());  // load c
  push_word(_masm, Rtos);     // push c
  push_word(_masm, R12);      // push d
  // stack: ..., a, b, c, d, c, d
  __ ld8(Rtos, at_tos_p4());  // load b
  __ st8(at_tos_p2(), Rtos);  // store b in d
  __ st8(at_tos_p4(), R12);   // store d in b
  // stack: ..., a, d, c, b, c, d
  __ ld8(R12, at_tos_p5());   // load a
  __ ld8(Rtos, at_tos_p3());  // load c
  __ st8(at_tos_p3(), R12);   // store a in c
  __ st8(at_tos_p5(), Rtos);  // store c in a
  // stack: ..., c, d, a, b, c, d
}

void TemplateTable::swap() {
  transition(vtos, vtos);
  // stack: ..., a, b
  __ ld8(R12, at_tos_p1());   // load a
  __ ld8(Rtos, at_tos());     // load b
  __ st8(at_tos(), R12);      // store a in b
  __ st8(at_tos_p1(), Rtos);  // store b in a
  // stack: ..., b, a
}

// ---------------------------------------------------------------------------
// Arithmetic

// IA-64: no integer divide instruction. These implement Java semantics,
// including MIN_VALUE / -1 (which overflows in C); the templates check for a
// zero divisor before calling.
static jint  ia64_idiv(jint x, jint y)   { return (x == min_jint && y == -1) ? x : x / y; }
static jint  ia64_irem(jint x, jint y)   { return (x == min_jint && y == -1) ? 0 : x % y; }
static jlong ia64_ldiv(jlong x, jlong y) { return (x == min_jlong && y == -1) ? x : x / y; }
static jlong ia64_lrem(jlong x, jlong y) { return (x == min_jlong && y == -1) ? 0 : x % y; }

// IA-64: no FP divide instruction either; GCC's IEEE-correct sequence via C.
static jfloat  ia64_fdiv(jfloat x, jfloat y)   { return x / y; }
static jdouble ia64_ddiv(jdouble x, jdouble y) { return x / y; }

void TemplateTable::iop2(Operation op) {
  transition(itos, itos);
  // Rtos <== R11 op Rtos
  __ pop_i(R11);
  switch (op) {
    case add  : __ add(Rtos, R11, Rtos);  __ sxt4(Rtos, Rtos); break;
    case sub  : __ sub(Rtos, R11, Rtos);  __ sxt4(Rtos, Rtos); break;
    case mul  : __ mul(Rtos, R11, Rtos);  __ sxt4(Rtos, Rtos); break;
    case _and : __ and_(Rtos, R11, Rtos); break;
    case _or  : __ or_(Rtos, R11, Rtos);  break;
    case _xor : __ xor_(Rtos, R11, Rtos); break;
    case shl  :
      __ and_imm(Rtos, 0x1f, Rtos);
      __ shl(Rtos, R11, Rtos);
      __ sxt4(Rtos, Rtos);
      break;
    case shr  :
      // R11 is sign-extended, so a 64-bit arithmetic shift is the 32-bit one
      __ and_imm(Rtos, 0x1f, Rtos);
      __ shr(Rtos, R11, Rtos);
      break;
    case ushr :
      __ and_imm(Rtos, 0x1f, Rtos);
      __ zxt4(R11, R11);
      __ shru(Rtos, R11, Rtos);
      __ sxt4(Rtos, Rtos);
      break;
    default   : ShouldNotReachHere();
  }
}

void TemplateTable::lop2(Operation op) {
  transition(ltos, ltos);
  // Rtos <== R11 op Rtos
  __ pop_l(R11);
  switch (op) {
    case add  : __ add(Rtos, R11, Rtos);  break;
    case sub  : __ sub(Rtos, R11, Rtos);  break;
    case mul  : __ mul(Rtos, R11, Rtos);  break;
    case _and : __ and_(Rtos, R11, Rtos); break;
    case _or  : __ or_(Rtos, R11, Rtos);  break;
    case _xor : __ xor_(Rtos, R11, Rtos); break;
    default   : ShouldNotReachHere();
  }
}

// Divide-by-zero check: jump to the ArithmeticException entry if Rtos == 0.
static void check_div0(InterpreterMacroAssembler* _masm, bool is_long, address throw_entry) {
  if (is_long) {
    _masm->cmp_eq(ptmp0, ptmp1, Rtos, zr);
  } else {
    _masm->cmp4_eq(ptmp0, ptmp1, Rtos, zr);
  }
  jump_to(_masm, throw_entry, ptmp0);
}

void TemplateTable::idiv() {
  transition(itos, itos);
  check_div0(_masm, false, Interpreter::_throw_ArithmeticException_entry);
  __ pop_i(R11);
  // Rtos <== R11 idiv Rtos
  __ call_VM_leaf(CAST_FROM_FN_PTR(address, ia64_idiv), R11, Rtos);
  __ sxt4(Rtos, Rtos);
}

void TemplateTable::irem() {
  transition(itos, itos);
  check_div0(_masm, false, Interpreter::_throw_ArithmeticException_entry);
  __ pop_i(R11);
  // Rtos <== R11 irem Rtos
  __ call_VM_leaf(CAST_FROM_FN_PTR(address, ia64_irem), R11, Rtos);
  __ sxt4(Rtos, Rtos);
}

void TemplateTable::lmul() {
  transition(ltos, ltos);
  __ pop_l(R11);
  __ mul(Rtos, Rtos, R11);
}

void TemplateTable::ldiv() {
  transition(ltos, ltos);
  check_div0(_masm, true, Interpreter::_throw_ArithmeticException_entry);
  __ pop_l(R11);
  // Rtos <== R11 ldiv Rtos
  __ call_VM_leaf(CAST_FROM_FN_PTR(address, ia64_ldiv), R11, Rtos);
}

void TemplateTable::lrem() {
  transition(ltos, ltos);
  check_div0(_masm, true, Interpreter::_throw_ArithmeticException_entry);
  __ pop_l(R11);
  // Rtos <== R11 lrem Rtos
  __ call_VM_leaf(CAST_FROM_FN_PTR(address, ia64_lrem), R11, Rtos);
}

void TemplateTable::lshl() {
  transition(itos, ltos);
  // shift count is in Rtos
  __ and_imm(Rtos, 0x3f, Rtos);
  __ pop_l(R11);
  __ shl(Rtos, R11, Rtos);
}

void TemplateTable::lshr() {
  transition(itos, ltos);
  // shift count is in Rtos
  __ and_imm(Rtos, 0x3f, Rtos);
  __ pop_l(R11);
  __ shr(Rtos, R11, Rtos);
}

void TemplateTable::lushr() {
  transition(itos, ltos);
  // shift count is in Rtos
  __ and_imm(Rtos, 0x3f, Rtos);
  __ pop_l(R11);
  __ shru(Rtos, R11, Rtos);
}

// The FP binary operations: value1 is popped into f9, value2 is in f8; a C
// helper takes them as (f8, f9) = (value1, value2), so the operands are
// swapped into place first.
void TemplateTable::fop2(Operation op) {
  transition(ftos, ftos);
  switch (op) {
    case add:
      __ pop_f(f9);
      __ fadd_s(Ftos, f9, Ftos);
      break;
    case sub:
      __ pop_f(f9);
      __ fsub_s(Ftos, f9, Ftos);
      break;
    case mul:
      __ pop_f(f9);
      __ fmpy_s(Ftos, f9, Ftos);
      break;
    case div:
      __ fmov_d(f9, Ftos);
      __ pop_f(Ftos);
      __ call_VM_leaf(CAST_FROM_FN_PTR(address, ia64_fdiv));
      break;
    case rem:
      __ fmov_d(f9, Ftos);
      __ pop_f(Ftos);
      __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::frem));
      break;
    default:
      ShouldNotReachHere();
  }
}

void TemplateTable::dop2(Operation op) {
  transition(dtos, dtos);
  switch (op) {
    case add:
      __ pop_d(f9);
      __ fadd_d(Ftos, f9, Ftos);
      break;
    case sub:
      __ pop_d(f9);
      __ fsub_d(Ftos, f9, Ftos);
      break;
    case mul:
      __ pop_d(f9);
      __ fmpy_d(Ftos, f9, Ftos);
      break;
    case div:
      __ fmov_d(f9, Ftos);
      __ pop_d(Ftos);
      __ call_VM_leaf(CAST_FROM_FN_PTR(address, ia64_ddiv));
      break;
    case rem:
      __ fmov_d(f9, Ftos);
      __ pop_d(Ftos);
      __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::drem));
      break;
    default:
      ShouldNotReachHere();
  }
}

void TemplateTable::ineg() {
  transition(itos, itos);
  __ neg(Rtos, Rtos);
  __ sxt4(Rtos, Rtos);
}

void TemplateTable::lneg() {
  transition(ltos, ltos);
  __ neg(Rtos, Rtos);
}

void TemplateTable::fneg() {
  transition(ftos, ftos);
  __ fneg(Ftos, Ftos);
}

void TemplateTable::dneg() {
  transition(dtos, dtos);
  __ fneg(Ftos, Ftos);
}

void TemplateTable::iinc() {
  transition(vtos, vtos);
  __ ld1s(R11, at_bcp(2)); // get constant
  locals_index(R12);
  __ ld4s(Rtos, iaddress(R12, Rtos, _masm));
  __ add(Rtos, Rtos, R11);
  __ sxt4(Rtos, Rtos);
  __ st8(iaddress(R12, t3, _masm), Rtos);
}

void TemplateTable::wide_iinc() {
  transition(vtos, vtos);
  // get constant: 16 bits, big-endian, signed
  __ load_unaligned_be(R11, Rbcp, 4, 2, t2);
  __ sxt2(R11, R11);

  locals_index_wide(R12);
  __ ld4s(Rtos, iaddress(R12, t3, _masm));
  __ add(Rtos, Rtos, R11);
  __ sxt4(Rtos, Rtos);
  __ st8(iaddress(R12, t3, _masm), Rtos);
}

void TemplateTable::convert() {
  // Checking
#ifdef ASSERT
  {
    TosState tos_in  = ilgl;
    TosState tos_out = ilgl;
    switch (bytecode()) {
      case Bytecodes::_i2l: // fall through
      case Bytecodes::_i2f: // fall through
      case Bytecodes::_i2d: // fall through
      case Bytecodes::_i2b: // fall through
      case Bytecodes::_i2c: // fall through
      case Bytecodes::_i2s: tos_in = itos; break;
      case Bytecodes::_l2i: // fall through
      case Bytecodes::_l2f: // fall through
      case Bytecodes::_l2d: tos_in = ltos; break;
      case Bytecodes::_f2i: // fall through
      case Bytecodes::_f2l: // fall through
      case Bytecodes::_f2d: tos_in = ftos; break;
      case Bytecodes::_d2i: // fall through
      case Bytecodes::_d2l: // fall through
      case Bytecodes::_d2f: tos_in = dtos; break;
      default             : ShouldNotReachHere();
    }
    switch (bytecode()) {
      case Bytecodes::_l2i: // fall through
      case Bytecodes::_f2i: // fall through
      case Bytecodes::_d2i: // fall through
      case Bytecodes::_i2b: // fall through
      case Bytecodes::_i2c: // fall through
      case Bytecodes::_i2s: tos_out = itos; break;
      case Bytecodes::_i2l: // fall through
      case Bytecodes::_f2l: // fall through
      case Bytecodes::_d2l: tos_out = ltos; break;
      case Bytecodes::_i2f: // fall through
      case Bytecodes::_l2f: // fall through
      case Bytecodes::_d2f: tos_out = ftos; break;
      case Bytecodes::_i2d: // fall through
      case Bytecodes::_l2d: // fall through
      case Bytecodes::_f2d: tos_out = dtos; break;
      default             : ShouldNotReachHere();
    }
    transition(tos_in, tos_out);
  }
#endif // ASSERT

  // Conversion. IA-64: integer -> FP goes through the significand
  // (setf.sig, fcvt.xf, which is exact for 64 bits) and one rounding to the
  // target precision; FP -> integer uses SharedRuntime's helpers, which give
  // Java's NaN and saturation semantics.
  switch (bytecode()) {
    case Bytecodes::_i2l:
      // ints are kept sign-extended: nothing to do
      break;
    case Bytecodes::_i2f:
    case Bytecodes::_l2f:
      __ setf_sig(Ftos, Rtos);
      __ fcvt_xf(Ftos, Ftos);
      __ fnorm_s(Ftos, Ftos);
      break;
    case Bytecodes::_i2d:
    case Bytecodes::_l2d:
      __ setf_sig(Ftos, Rtos);
      __ fcvt_xf(Ftos, Ftos);
      __ fnorm_d(Ftos, Ftos);
      break;
    case Bytecodes::_i2b:
      __ sxt1(Rtos, Rtos);
      break;
    case Bytecodes::_i2c:
      __ zxt2(Rtos, Rtos);
      break;
    case Bytecodes::_i2s:
      __ sxt2(Rtos, Rtos);
      break;
    case Bytecodes::_l2i:
      __ sxt4(Rtos, Rtos);
      break;
    case Bytecodes::_f2i:
      __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::f2i));
      __ sxt4(Rtos, Rtos);
      break;
    case Bytecodes::_f2l:
      __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::f2l));
      break;
    case Bytecodes::_f2d:
      // exact: every float is a double
      __ fnorm_d(Ftos, Ftos);
      break;
    case Bytecodes::_d2i:
      __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::d2i));
      __ sxt4(Rtos, Rtos);
      break;
    case Bytecodes::_d2l:
      __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::d2l));
      break;
    case Bytecodes::_d2f:
      __ fnorm_s(Ftos, Ftos);
      break;
    default:
      ShouldNotReachHere();
  }
}

void TemplateTable::lcmp() {
  transition(ltos, itos);
  __ pop_l(R11);
  // Rtos = (R11 < Rtos) ? -1 : (R11 > Rtos) ? 1 : 0
  __ cmp_lt(ptmp0, ptmp1, R11, Rtos);
  __ cmp_lt(ptmp2, ptmp3, Rtos, R11);
  __ mov(Rtos, zr);
  __ adds(Rtos, -1, zr, ptmp0);
  __ adds(Rtos, 1, zr, ptmp2);
}

// value1 popped into f9, value2 in f8. An unordered pair (a NaN) makes all
// three comparisons false, which leaves the unordered result in place.
void TemplateTable::float_cmp(bool is_float, int unordered_result) {
  if (is_float) {
    __ pop_f(f9);
  } else {
    __ pop_d(f9);
  }
  __ fcmp_lt(p10, p11, f9, Ftos);       // value1 < value2
  __ fcmp_lt(p12, p13, Ftos, f9);       // value1 > value2
  __ fcmp_eq(p14, p15, f9, Ftos);       // value1 == value2
  __ mov_immediate(Rtos, unordered_result < 0 ? -1 : 1);
  __ adds(Rtos, -1, zr, p10);
  __ adds(Rtos, 1, zr, p12);
  __ adds(Rtos, 0, zr, p14);
}

// ---------------------------------------------------------------------------
// Control flow

// Byte-swap a 4-byte big-endian value loaded zero-extended into the low half
// of r, leaving it sign-extended. mux1 @rev reverses all eight bytes, which
// puts the four of interest in the high half, the right way round.
static void bswap32s(InterpreterMacroAssembler* _masm, Register r) {
  _masm->mux1_rev(r, r);
  _masm->shr_imm(r, r, 32);
}

void TemplateTable::branch(bool is_jsr, bool is_wide) {
  const ByteSize be_offset = MethodCounters::backedge_counter_offset() +
                             InvocationCounter::counter_offset();

  // load branch displacement: big-endian, signed, and unaligned
  if (!is_wide) {
    __ load_unaligned_be(R12, Rbcp, 1, 2, t2);
    __ sxt2(R12, R12);
  } else {
    __ load_unaligned_be(R12, Rbcp, 1, 4, t2);
    __ sxt4(R12, R12);
  }

  // Handle all the JSR stuff here, then exit.
  // It's much shorter and cleaner than intermingling with the non-JSR
  // normal-branch stuff occurring below.

  if (is_jsr) {
    // compute return address as bci
    __ ld8(t3, Address(Rmethod, Method::const_offset()));
    __ adds(t3, in_bytes(ConstMethod::codes_offset()) - (is_wide ? 5 : 3), t3);
    __ sub(R11, Rbcp, t3);
    __ push_i(R11);
    // Adjust the bcp by the displacement in R12
    __ add(Rbcp, Rbcp, R12);
    __ ld1(t0, Rbcp);
    // load the next target bytecode into t0, it is the argument of dispatch_only
    __ dispatch_only(vtos, /*generate_poll*/true);
    return;
  }

  // Normal (non-jsr) branch handling

  // Adjust the bcp by the displacement in R12
  __ add(Rbcp, Rbcp, R12);

  assert(UseLoopCounter || !UseOnStackReplacement,
         "on-stack-replacement requires loop counters");
  Label backedge_counter_overflow;
  Label dispatch;
  if (UseLoopCounter) {
    // increment backedge counter for backward branches
    // R12: target offset
    __ cmp_lt(ptmp0, ptmp1, zr, R12);
    __ br_cond(dispatch, ptmp0);  // count only if backward branch

    // check if MethodCounters exists
    Label has_counters;
    __ ld8(t2, Address(Rmethod, Method::method_counters_offset()));
    __ bnez(t2, has_counters);
    __ push_ptr(R12);
    __ call_VM(noreg, CAST_FROM_FN_PTR(address,
            InterpreterRuntime::build_method_counters), Rmethod);
    __ pop_ptr(R12);
    __ ld8(t2, Address(Rmethod, Method::method_counters_offset()));
    __ beqz(t2, dispatch); // No MethodCounters allocated, OutOfMemory
    __ bind(has_counters);

    guarantee(!ProfileInterpreter, "IA-64: interpreter profiling arrives with C1");
    int increment = InvocationCounter::count_increment;
    // Increment backedge counter in MethodCounters*
    __ ld8(t2, Address(Rmethod, Method::method_counters_offset()));
    const Address mask(t2, in_bytes(MethodCounters::backedge_mask_offset()));
    __ increment_mask_and_jump(Address(t2, be_offset), increment, mask,
                               Rtos, t3, false,
                               UseOnStackReplacement ? &backedge_counter_overflow : &dispatch);
    __ bind(dispatch);
  }

  // Pre-load the next target bytecode into t0
  __ ld1(t0, Rbcp);

  // continue with the bytecode @ target
  // t0: target bytecode
  // Rbcp: target bcp
  __ dispatch_only(vtos, /*generate_poll*/true);

  if (UseLoopCounter && UseOnStackReplacement) {
    // invocation counter overflow
    __ bind(backedge_counter_overflow);
    __ sub(R12, Rbcp, R12);     // branch bcp
    // IcoResult frequency_counter_overflow([JavaThread*], address branch_bcp)
    __ call_VM(noreg,
               CAST_FROM_FN_PTR(address,
                                InterpreterRuntime::frequency_counter_overflow),
               R12);
    // Rtos: osr nmethod (osr ok) or null (osr not possible). With no
    // compiler it is always null; on-stack replacement into compiled code
    // arrives with C1.
    __ beqz(Rtos, dispatch);
    __ stop("IA-64: on-stack replacement not yet supported");
  }
}

// Branch to not_taken unless "a cc b" holds (32-bit compare for ints).
static void branch_unless(InterpreterMacroAssembler* _masm, TemplateTable::Condition cc,
                          Register a, Register b, Label& not_taken, bool is_int) {
  // cmp p6 = a < b, or b < a, or a == b; then branch on the predicate that
  // means "not taken".
  PredicateRegister lt = ptmp0, ge = ptmp1;
  switch (cc) {
    case TemplateTable::equal:
    case TemplateTable::not_equal:
      if (is_int) _masm->cmp4_eq(lt, ge, a, b); else _masm->cmp_eq(lt, ge, a, b);
      // lt := equal, ge := not equal
      _masm->br_cond(not_taken, cc == TemplateTable::equal ? ge : lt);
      break;
    case TemplateTable::less:
    case TemplateTable::greater_equal:
      if (is_int) _masm->cmp4_lt(lt, ge, a, b); else _masm->cmp_lt(lt, ge, a, b);
      _masm->br_cond(not_taken, cc == TemplateTable::less ? ge : lt);
      break;
    case TemplateTable::greater:
    case TemplateTable::less_equal:
      // a > b is b < a
      if (is_int) _masm->cmp4_lt(lt, ge, b, a); else _masm->cmp_lt(lt, ge, b, a);
      _masm->br_cond(not_taken, cc == TemplateTable::greater ? ge : lt);
      break;
    default:
      ShouldNotReachHere();
  }
}

void TemplateTable::if_0cmp(Condition cc) {
  transition(itos, vtos);
  // assume branch is more often taken than not (loops use backward branches)
  Label not_taken;
  branch_unless(_masm, cc, Rtos, zr, not_taken, true);
  branch(false, false);
  __ bind(not_taken);
}

void TemplateTable::if_icmp(Condition cc) {
  transition(itos, vtos);
  // assume branch is more often taken than not (loops use backward branches)
  Label not_taken;
  __ pop_i(R11);
  branch_unless(_masm, cc, R11, Rtos, not_taken, true);
  branch(false, false);
  __ bind(not_taken);
}

void TemplateTable::if_nullcmp(Condition cc) {
  transition(atos, vtos);
  // assume branch is more often taken than not (loops use backward branches)
  Label not_taken;
  if (cc == equal) {
    __ bnez(Rtos, not_taken);
  } else {
    __ beqz(Rtos, not_taken);
  }
  branch(false, false);
  __ bind(not_taken);
}

void TemplateTable::if_acmp(Condition cc) {
  transition(atos, vtos);
  // assume branch is more often taken than not (loops use backward branches)
  Label not_taken;
  __ pop_ptr(R11);

  if (cc == equal) {
    __ bne(R11, Rtos, not_taken);
  } else if (cc == not_equal) {
    __ beq(R11, Rtos, not_taken);
  }
  branch(false, false);
  __ bind(not_taken);
}

void TemplateTable::ret() {
  transition(vtos, vtos);
  locals_index(R11);
  __ ld8(R11, aaddress(R11, t2, _masm)); // get return bci, compute return bcp
  __ ld8(Rbcp, Address(Rmethod, Method::const_offset()));
  __ add(Rbcp, Rbcp, R11);
  __ adds(Rbcp, in_bytes(ConstMethod::codes_offset()), Rbcp);
  __ dispatch_next(vtos, 0, /*generate_poll*/true);
}

void TemplateTable::wide_ret() {
  transition(vtos, vtos);
  locals_index_wide(R11);
  __ ld8(R11, aaddress(R11, t3, _masm)); // get return bci, compute return bcp
  __ ld8(Rbcp, Address(Rmethod, Method::const_offset()));
  __ add(Rbcp, Rbcp, R11);
  __ adds(Rbcp, in_bytes(ConstMethod::codes_offset()), Rbcp);
  __ dispatch_next(vtos, 0, /*generate_poll*/true);
}

// The switch tables are 4-byte aligned within the bytecode stream, so their
// entries can be loaded whole -- then byte-swapped, being big-endian.
void TemplateTable::tableswitch() {
  Label default_case, continue_execution;
  transition(itos, vtos);
  // align Rbcp
  __ adds(R11, BytesPerInt, Rbcp);
  __ and_imm(R11, -BytesPerInt, R11);
  // load lo & hi
  __ ld4(R12, Address(R11, BytesPerInt));
  __ ld4(R13, Address(R11, 2 * BytesPerInt));
  bswap32s(_masm, R12);
  bswap32s(_masm, R13);
  // check against lo & hi
  __ bltw(Rtos, R12, default_case);
  __ bltw(R13, Rtos, default_case);
  // lookup dispatch offset
  __ sub(Rtos, Rtos, R12);
  __ shladd(R13, Rtos, 2, R11);
  __ ld4(R13, Address(R13, 3 * BytesPerInt));
  // continue execution
  __ bind(continue_execution);
  bswap32s(_masm, R13);
  __ add(Rbcp, Rbcp, R13);
  __ ld1(t0, Rbcp);
  __ dispatch_only(vtos, /*generate_poll*/true);
  // handle default
  __ bind(default_case);
  __ ld4(R13, R11);
  __ j(continue_execution);
}

void TemplateTable::lookupswitch() {
  transition(itos, itos);
  __ stop("lookupswitch bytecode should have been rewritten");
}

void TemplateTable::fast_linearswitch() {
  transition(itos, vtos);
  Label loop_entry, loop, found, continue_execution;
  const Register table = R29;
  // align Rbcp
  __ adds(table, BytesPerInt, Rbcp);
  __ and_imm(table, -BytesPerInt, table);
  // set counter
  __ ld4(R11, Address(table, BytesPerInt));
  bswap32s(_masm, R11);
  __ j(loop_entry);
  // table search: compare native keys (match values are swapped on load)
  __ bind(loop);
  __ shladd(t2, R11, 3, table);
  __ ld4(t2, Address(t2, 2 * BytesPerInt));
  bswap32s(_masm, t2);
  __ beqw(Rtos, t2, found);
  __ bind(loop_entry);
  __ adds(R11, -1, R11);
  __ cmp_lt(ptmp0, ptmp1, R11, zr);
  __ br_cond(loop, ptmp1);       // while R11 >= 0
  // default case
  __ ld4(R13, table);
  __ j(continue_execution);
  // entry found -> get offset
  __ bind(found);
  __ shladd(t2, R11, 3, table);
  __ ld4(R13, Address(t2, 3 * BytesPerInt));
  // continue execution
  __ bind(continue_execution);
  bswap32s(_masm, R13);
  __ add(Rbcp, Rbcp, R13);
  __ ld1(t0, Rbcp);
  __ dispatch_only(vtos, /*generate_poll*/true);
}

void TemplateTable::fast_binaryswitch() {
  transition(itos, vtos);
  // Implementation using the following core algorithm:
  //
  // int binary_search(int key, LookupswitchPair* array, int n)
  //   binary_search start:
  //   #Binary search according to "Methodik des Programmierens" by
  //   # Edsger W. Dijkstra and W.H.J. Feijen, Addison Wesley Germany 1985.
  //   int i = 0;
  //   int j = n;
  //   while (i + 1 < j) do
  //     # invariant P: 0 <= i < j <= n and (a[i] <= key < a[j] or Q)
  //     # with      Q: for all i: 0 <= i < n: key < a[i]
  //     # where a stands for the array and assuming that the (inexisting)
  //     # element a[n] is infinitely big.
  //     int h = (i + j) >> 1
  //     # i < h < j
  //     if (key < array[h].fast_match())
  //     then [j = h]
  //     else [i = h]
  //   end
  //   # R: a[i] <= key < a[i+1] or Q
  //   # (i.e., if key is within array, i is the correct index)
  //   return i
  // binary_search end

  // Register allocation
  const Register key   = Rtos; // already set (tosca)
  const Register array = R11;
  const Register i     = R12;
  const Register j     = R13;
  const Register h     = R14;
  const Register temp  = R15;

  // Find array start
  __ adds(array, 3 * BytesPerInt, Rbcp);
  __ and_imm(array, -BytesPerInt, array);

  // Initialize i & j
  __ mov(i, zr);                            // i = 0
  __ ld4(j, Address(array, -BytesPerInt));  // j = length(array)
  bswap32s(_masm, j);

  // And start
  Label entry;
  __ j(entry);

  // binary search loop
  {
    Label loop;
    __ bind(loop);
    __ add(h, i, j);                        // h = i + j
    __ shr_imm(h, h, 1);                    // h = (i + j) >> 1
    // if [key < array[h].fast_match()]
    // then [j = h]
    // else [i = h]
    __ shladd(temp, h, 3, array);
    __ ld4(temp, temp);
    bswap32s(_masm, temp);

    __ cmp4_lt(ptmp0, ptmp1, key, temp);
    __ mov(j, h, ptmp0);                    // key <  match: j = h
    __ mov(i, h, ptmp1);                    // key >= match: i = h

    // while [i + 1 < j]
    __ bind(entry);
    __ adds(h, 1, i);                       // i + 1
    __ blt(h, j, loop);                     // i + 1 < j
  }

  // end of binary search, result index is i (must check again!)
  Label default_case;
  __ shladd(temp, i, 3, array);
  __ ld4(temp, temp);
  bswap32s(_masm, temp);
  __ bnew(key, temp, default_case);

  // entry found -> j = offset
  __ shladd(temp, i, 3, array);
  __ ld4(j, Address(temp, BytesPerInt));
  bswap32s(_masm, j);

  __ add(Rbcp, Rbcp, j);
  __ ld1(t0, Rbcp);
  __ dispatch_only(vtos, /*generate_poll*/true);

  // default case -> j = default offset
  __ bind(default_case);
  __ ld4(j, Address(array, -2 * BytesPerInt));
  bswap32s(_masm, j);

  __ add(Rbcp, Rbcp, j);
  __ ld1(t0, Rbcp);
  __ dispatch_only(vtos, /*generate_poll*/true);
}

void TemplateTable::_return(TosState state) {
  transition(state, state);
  assert(_desc->calls_vm(),
         "inconsistent calls_vm information"); // call in remove_activation

  if (_desc->bytecode() == Bytecodes::_return_register_finalizer) {
    assert(state == vtos, "only valid state");

    __ ld8(c_rarg1, aaddress(0));
    __ load_klass(R13, c_rarg1);
    __ ld1(R13, Address(R13, Klass::misc_flags_offset()));
    Label skip_register_finalizer;
    __ tbit_z(ptmp0, ptmp1, R13, exact_log2(KlassFlags::_misc_has_finalizer));
    __ br_cond(skip_register_finalizer, ptmp0);

    __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::register_finalizer), c_rarg1);

    __ bind(skip_register_finalizer);
  }

  // Issue a StoreStore barrier after all stores but before return
  // from any constructor for any class with a final field. We don't
  // know if this is a finalizer, so we always do so.
  if (_desc->bytecode() == Bytecodes::_return) {
    __ membar(MacroAssembler::StoreStore);
  }

  if (_desc->bytecode() != Bytecodes::_return_register_finalizer) {
    Label no_safepoint;
    __ ld8(t2, Address(Rthread, JavaThread::polling_word_offset()));
    __ tbit_z(ptmp0, ptmp1, t2, exact_log2(SafepointMechanism::poll_bit()));
    __ br_cond(no_safepoint, ptmp0);
    __ push(state);
    __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint));
    __ pop(state);
    __ bind(no_safepoint);
  }

  // Narrow result if state is itos but result type is smaller.
  // Need to narrow in the return bytecode rather than in generate_return_entry
  // since compiled code callers expect the result to already be narrowed.
  if (state == itos) {
    __ narrow(Rtos);
  }

  __ remove_activation(state);
  __ ret();
}

// ---------------------------------------------------------------------------
// Fields and invokes
//
// riscv's callee-saved x9 maps to r29 here, and riscv's x30 to r20. IA-64
// keeps nothing scratch across a call into the VM, so every riscv value that
// lived in x9 across a call_VM has been checked; where one did, it is kept on
// the expression stack instead (see _breakpoint).

static constexpr Register R9  = r29;
static constexpr Register R30 = r20;

// Volatile field barriers. riscv issues the trailing barrier after the
// bytecode has been patched; patch_bytecode may call into the VM (when a
// breakpoint is set), which on IA-64 clobbers the register holding the field
// flags. So the barrier is issued right after the access instead, before
// any patching.
static void volatile_barrier(InterpreterMacroAssembler* _masm, Register flags) {
  Label notVolatile;
  _masm->tbit_z(ptmp0, ptmp1, flags, ResolvedFieldEntry::is_volatile_shift);
  _masm->br_cond(notVolatile, ptmp0);
  _masm->mf();
  _masm->bind(notVolatile);
}

void TemplateTable::resolve_cache_and_index_for_method(int byte_no,
                                                       Register Rcache,
                                                       Register index) {
  const Register temp = R9;
  assert_different_registers(Rcache, index, temp);
  assert(byte_no == f1_byte || byte_no == f2_byte, "byte_no out of range");

  Label resolved;

  Bytecodes::Code code = bytecode();
  __ load_method_entry(Rcache, index);
  switch(byte_no) {
    case f1_byte:
      __ lea(temp, Address(Rcache, in_bytes(ResolvedMethodEntry::bytecode1_offset())));
      break;
    case f2_byte:
      __ lea(temp, Address(Rcache, in_bytes(ResolvedMethodEntry::bytecode2_offset())));
      break;
  }
  // Load-acquire the bytecode to match store-release in InterpreterRuntime
  __ ld1(temp, temp);
  __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);

  __ cmp_eq_imm(ptmp0, ptmp1, (int)code, temp);  // have we resolved this bytecode?
  __ br_cond(resolved, ptmp0);

  // resolve first time through
  address entry = CAST_FROM_FN_PTR(address, InterpreterRuntime::resolve_from_cache);
  __ mov_immediate(temp, (int) code);
  __ call_VM(noreg, entry, temp);

  // Update registers with resolved info
  __ load_method_entry(Rcache, index);
  __ bind(resolved);

  // VM_Version::supports_fast_class_init_checks() is false on IA-64: the
  // resolution above runs the class initialization barrier.
}

void TemplateTable::resolve_cache_and_index_for_field(int byte_no,
                                                      Register Rcache,
                                                      Register index) {
  const Register temp = R9;
  assert_different_registers(Rcache, index, temp);

  Label resolved;

  Bytecodes::Code code = bytecode();
  switch (code) {
  case Bytecodes::_nofast_getfield: code = Bytecodes::_getfield; break;
  case Bytecodes::_nofast_putfield: code = Bytecodes::_putfield; break;
  default: break;
  }

  assert(byte_no == f1_byte || byte_no == f2_byte, "byte_no out of range");
  __ load_field_entry(Rcache, index);
  if (byte_no == f1_byte) {
    __ lea(temp, Address(Rcache, in_bytes(ResolvedFieldEntry::get_code_offset())));
  } else {
    __ lea(temp, Address(Rcache, in_bytes(ResolvedFieldEntry::put_code_offset())));
  }
  // Load-acquire the bytecode to match store-release in ResolvedFieldEntry::fill_in()
  __ ld1(temp, temp);
  __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);
  __ cmp_eq_imm(ptmp0, ptmp1, (int)code, temp);  // have we resolved this bytecode?
  __ br_cond(resolved, ptmp0);

  // resolve first time through
  address entry = CAST_FROM_FN_PTR(address, InterpreterRuntime::resolve_from_cache);
  __ mov_immediate(temp, (int) code);
  __ call_VM(noreg, entry, temp);

  // Update registers with resolved info
  __ load_field_entry(Rcache, index);
  __ bind(resolved);
}

void TemplateTable::load_resolved_field_entry(Register obj,
                                              Register cache,
                                              Register tos_state,
                                              Register offset,
                                              Register flags,
                                              bool is_static = false) {
  assert_different_registers(cache, tos_state, flags, offset);

  // Field offset
  __ ld4s(offset, Address(cache, in_bytes(ResolvedFieldEntry::field_offset_offset())));

  // Flags
  __ ld1(flags, Address(cache, in_bytes(ResolvedFieldEntry::flags_offset())));

  // TOS state
  if (tos_state != noreg) {
    __ ld1(tos_state, Address(cache, in_bytes(ResolvedFieldEntry::type_offset())));
  }

  // Klass overwrite register
  if (is_static) {
    __ ld8(obj, Address(cache, ResolvedFieldEntry::field_holder_offset()));
    const int mirror_offset = in_bytes(Klass::java_mirror_offset());
    __ ld8(obj, Address(obj, mirror_offset));
    __ resolve_oop_handle(obj, R15, noreg);
  }
}

void TemplateTable::load_resolved_method_entry_special_or_static(Register cache,
                                                                 Register method,
                                                                 Register flags) {

  // setup registers
  const Register index = flags;
  assert_different_registers(method, cache, flags);

  // determine constant pool cache field offsets
  resolve_cache_and_index_for_method(f1_byte, cache, index);
  __ ld1(flags, Address(cache, in_bytes(ResolvedMethodEntry::flags_offset())));
  __ ld8(method, Address(cache, in_bytes(ResolvedMethodEntry::method_offset())));
}

void TemplateTable::load_resolved_method_entry_handle(Register cache,
                                                      Register method,
                                                      Register ref_index,
                                                      Register flags) {
  // setup registers
  const Register index = ref_index;
  assert_different_registers(method, flags);
  assert_different_registers(method, cache, index);

  // determine constant pool cache field offsets
  resolve_cache_and_index_for_method(f1_byte, cache, index);
  __ ld1(flags, Address(cache, in_bytes(ResolvedMethodEntry::flags_offset())));

  // maybe push appendix to arguments (just before return address)
  Label L_no_push;
  __ tbit_z(ptmp0, ptmp1, flags, ResolvedMethodEntry::has_appendix_shift);
  __ br_cond(L_no_push, ptmp0);
  // invokehandle uses an index into the resolved references array
  __ ld2(ref_index, Address(cache, in_bytes(ResolvedMethodEntry::resolved_references_index_offset())));
  // Push the appendix as a trailing parameter.
  // This must be done before we get the receiver,
  // since the parameter_size includes it.
  Register appendix = method;
  __ load_resolved_reference_at_index(appendix, ref_index, R15);
  push_word(_masm, appendix); // push appendix (MethodType, CallSite, etc.)
  __ bind(L_no_push);

  __ ld8(method, Address(cache, in_bytes(ResolvedMethodEntry::method_offset())));
}

void TemplateTable::load_resolved_method_entry_interface(Register cache,
                                                         Register klass,
                                                         Register method_or_table_index,
                                                         Register flags) {
  // setup registers
  const Register index = method_or_table_index;
  assert_different_registers(method_or_table_index, cache, flags);

  // determine constant pool cache field offsets
  resolve_cache_and_index_for_method(f1_byte, cache, index);
  __ ld1(flags, Address(cache, in_bytes(ResolvedMethodEntry::flags_offset())));

  // Invokeinterface can behave in different ways:
  // If calling a method from java.lang.Object, the forced virtual flag is true so the invocation will
  // behave like an invokevirtual call. The state of the virtual final flag will determine whether a method or
  // vtable index is placed in the register.
  // Otherwise, the registers will be populated with the klass and method.

  Label NotVirtual; Label NotVFinal; Label Done;
  __ tbit_z(ptmp0, ptmp1, flags, ResolvedMethodEntry::is_forced_virtual_shift);
  __ br_cond(NotVirtual, ptmp0);
  __ tbit_z(ptmp0, ptmp1, flags, ResolvedMethodEntry::is_vfinal_shift);
  __ br_cond(NotVFinal, ptmp0);
  __ ld8(method_or_table_index, Address(cache, in_bytes(ResolvedMethodEntry::method_offset())));
  __ j(Done);

  __ bind(NotVFinal);
  __ ld2(method_or_table_index, Address(cache, in_bytes(ResolvedMethodEntry::table_index_offset())));
  __ j(Done);

  __ bind(NotVirtual);
  __ ld8(method_or_table_index, Address(cache, in_bytes(ResolvedMethodEntry::method_offset())));
  __ ld8(klass, Address(cache, in_bytes(ResolvedMethodEntry::klass_offset())));
  __ bind(Done);
}

void TemplateTable::load_resolved_method_entry_virtual(Register cache,
                                                       Register method_or_table_index,
                                                       Register flags) {
  // setup registers
  const Register index = flags;
  assert_different_registers(method_or_table_index, cache, flags);

  // determine constant pool cache field offsets
  resolve_cache_and_index_for_method(f2_byte, cache, index);
  __ ld1(flags, Address(cache, in_bytes(ResolvedMethodEntry::flags_offset())));

  // method_or_table_index can either be an itable index or a method depending on the virtual final flag
  Label NotVFinal; Label Done;
  __ tbit_z(ptmp0, ptmp1, flags, ResolvedMethodEntry::is_vfinal_shift);
  __ br_cond(NotVFinal, ptmp0);
  __ ld8(method_or_table_index, Address(cache, in_bytes(ResolvedMethodEntry::method_offset())));
  __ j(Done);

  __ bind(NotVFinal);
  __ ld2(method_or_table_index, Address(cache, in_bytes(ResolvedMethodEntry::table_index_offset())));
  __ bind(Done);
}

// Put the interpreter's return entry for |code|, indexed by the callee's
// result tos state, into b0 -- where riscv puts it into ra.
static void load_return_address(InterpreterMacroAssembler* _masm, Bytecodes::Code code, Register tos_state) {
  const address table_addr = (address) Interpreter::invoke_return_entry_table_for(code);
  _masm->movl(t2, table_addr);
  _masm->shladd(t2, tos_state, LogBytesPerWord, t2);
  _masm->ld8(t2, t2);
  _masm->mov_to_br(breturn, t2);
}

// The Rmethod register is input and overwritten to be the adapter method for the
// indy call. The return address (b0) is set to the return address for the adapter and
// an appendix may be pushed to the stack. Registers Rtos, R12, R13 are clobbered.
void TemplateTable::load_invokedynamic_entry(Register method) {
  // setup registers
  const Register appendix = Rtos;
  const Register cache = R12;
  const Register index = R13;
  assert_different_registers(method, appendix, cache, index, Rcpool);

  __ save_bcp();

  Label resolved;

  __ load_resolved_indy_entry(cache, index);
  __ ld8(method, Address(cache, in_bytes(ResolvedIndyEntry::method_offset())));
  __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);

  // Compare the method to zero
  __ bnez(method, resolved);

  Bytecodes::Code code = bytecode();

  // Call to the interpreter runtime to resolve invokedynamic
  address entry = CAST_FROM_FN_PTR(address, InterpreterRuntime::resolve_from_cache);
  __ mov_immediate(method, code); // this is essentially Bytecodes::_invokedynamic
  __ call_VM(noreg, entry, method);
  // Update registers with resolved info
  __ load_resolved_indy_entry(cache, index);
  __ ld8(method, Address(cache, in_bytes(ResolvedIndyEntry::method_offset())));
  __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);

#ifdef ASSERT
  __ bnez(method, resolved);
  __ stop("Should be resolved by now");
#endif // ASSERT
  __ bind(resolved);

  Label L_no_push;
  // Check if there is an appendix
  __ ld1(index, Address(cache, in_bytes(ResolvedIndyEntry::flags_offset())));
  __ tbit_z(ptmp0, ptmp1, index, ResolvedIndyEntry::has_appendix_shift);
  __ br_cond(L_no_push, ptmp0);

  // Get appendix
  __ ld2(index, Address(cache, in_bytes(ResolvedIndyEntry::resolved_references_index_offset())));
  // Push the appendix as a trailing parameter
  // since the parameter_size includes it.
  __ load_resolved_reference_at_index(appendix, index, R15);
  __ verify_oop(appendix);
  push_word(_masm, appendix);  // push appendix (MethodType, CallSite, etc.)
  __ bind(L_no_push);

  // compute return type
  __ ld1(index, Address(cache, in_bytes(ResolvedIndyEntry::result_type_offset())));
  // load return address
  load_return_address(_masm, code, index);
}

// The registers cache and index expected to be set before call.
// Correct values of the cache and index registers are preserved.
void TemplateTable::jvmti_post_field_access(Register cache, Register index,
                                            bool is_static, bool has_tos) {
  // do the JVMTI work here to avoid disturbing the register state below
  // We use c_rarg registers here because we want to use the register used in
  // the call to the VM
  if (JvmtiExport::can_post_field_access()) {
    // Check to see if a field access watch has been set before we
    // take the time to call into the VM.
    Label L1;
    assert_different_registers(cache, index, Rtos);
    __ movl(Rtos, (address)JvmtiExport::get_field_access_count_addr());
    __ ld4(Rtos, Rtos);
    __ beqz(Rtos, L1);

    __ load_field_entry(c_rarg2, index);

    if (is_static) {
      __ mov(c_rarg1, zr); // null object reference
    } else {
      __ ld8(c_rarg1, at_tos()); // get object pointer without popping it
      __ verify_oop(c_rarg1);
    }
    // c_rarg1: object pointer or null
    // c_rarg2: cache entry pointer
    __ call_VM(noreg, CAST_FROM_FN_PTR(address,
                                       InterpreterRuntime::post_field_access),
                                       c_rarg1, c_rarg2);
    __ load_field_entry(cache, index);
    __ bind(L1);
  }
}

void TemplateTable::pop_and_check_object(Register r) {
  __ pop_ptr(r);
  __ null_check(r);  // for field access must check obj.
  __ verify_oop(r);
}

void TemplateTable::getfield_or_static(int byte_no, bool is_static, RewriteControl rc) {
  const Register cache     = R14;
  const Register obj       = R14;
  const Register index     = R13;
  const Register tos_state = R13;
  const Register off       = R9;
  const Register flags     = R16;
  const Register bc        = R14; // uses same reg as obj, so don't mix them

  resolve_cache_and_index_for_field(byte_no, cache, index);
  jvmti_post_field_access(cache, index, is_static, false);
  load_resolved_field_entry(obj, cache, tos_state, off, flags, is_static);

  if (!is_static) {
    // obj is on the stack
    pop_and_check_object(obj);
  }

  __ add(off, obj, off);
  const Address field(off);

  Label Done, notByte, notBool, notInt, notShort, notChar,
              notLong, notFloat, notObj, notDouble;

  assert(btos == 0, "change code, btos != 0");
  __ bnez(tos_state, notByte);

  // Don't rewrite getstatic, only getfield
  if (is_static) {
    rc = may_not_rewrite;
  }

  // btos
  __ access_load_at(T_BYTE, IN_HEAP, Rtos, field, noreg, noreg);
  __ push(btos);
  volatile_barrier(_masm, flags);
  // Rewrite bytecode to be faster
  if (rc == may_rewrite) {
    patch_bytecode(Bytecodes::_fast_bgetfield, bc, R11);
  }
  __ j(Done);

  __ bind(notByte);
  __ cmp_eq_imm(ptmp0, ptmp1, ztos, tos_state);
  __ br_cond(notBool, ptmp1);

  // ztos (same code as btos)
  __ access_load_at(T_BOOLEAN, IN_HEAP, Rtos, field, noreg, noreg);
  __ push(ztos);
  volatile_barrier(_masm, flags);
  // Rewrite bytecode to be faster
  if (rc == may_rewrite) {
    // uses btos rewriting, no truncating to t/f bit is needed for getfield
    patch_bytecode(Bytecodes::_fast_bgetfield, bc, R11);
  }
  __ j(Done);

  __ bind(notBool);
  __ cmp_eq_imm(ptmp0, ptmp1, atos, tos_state);
  __ br_cond(notObj, ptmp1);
  // atos
  __ load_heap_oop(Rtos, field, R28, R29, IN_HEAP);
  __ push(atos);
  volatile_barrier(_masm, flags);
  if (rc == may_rewrite) {
    patch_bytecode(Bytecodes::_fast_agetfield, bc, R11);
  }
  __ j(Done);

  __ bind(notObj);
  __ cmp_eq_imm(ptmp0, ptmp1, itos, tos_state);
  __ br_cond(notInt, ptmp1);
  // itos
  __ access_load_at(T_INT, IN_HEAP, Rtos, field, noreg, noreg);
  __ push(itos);
  volatile_barrier(_masm, flags);
  // Rewrite bytecode to be faster
  if (rc == may_rewrite) {
    patch_bytecode(Bytecodes::_fast_igetfield, bc, R11);
  }
  __ j(Done);

  __ bind(notInt);
  __ cmp_eq_imm(ptmp0, ptmp1, ctos, tos_state);
  __ br_cond(notChar, ptmp1);
  // ctos
  __ access_load_at(T_CHAR, IN_HEAP, Rtos, field, noreg, noreg);
  __ push(ctos);
  volatile_barrier(_masm, flags);
  // Rewrite bytecode to be faster
  if (rc == may_rewrite) {
    patch_bytecode(Bytecodes::_fast_cgetfield, bc, R11);
  }
  __ j(Done);

  __ bind(notChar);
  __ cmp_eq_imm(ptmp0, ptmp1, stos, tos_state);
  __ br_cond(notShort, ptmp1);
  // stos
  __ access_load_at(T_SHORT, IN_HEAP, Rtos, field, noreg, noreg);
  __ push(stos);
  volatile_barrier(_masm, flags);
  // Rewrite bytecode to be faster
  if (rc == may_rewrite) {
    patch_bytecode(Bytecodes::_fast_sgetfield, bc, R11);
  }
  __ j(Done);

  __ bind(notShort);
  __ cmp_eq_imm(ptmp0, ptmp1, ltos, tos_state);
  __ br_cond(notLong, ptmp1);
  // ltos
  __ access_load_at(T_LONG, IN_HEAP, Rtos, field, noreg, noreg);
  __ push(ltos);
  volatile_barrier(_masm, flags);
  // Rewrite bytecode to be faster
  if (rc == may_rewrite) {
    patch_bytecode(Bytecodes::_fast_lgetfield, bc, R11);
  }
  __ j(Done);

  __ bind(notLong);
  __ cmp_eq_imm(ptmp0, ptmp1, ftos, tos_state);
  __ br_cond(notFloat, ptmp1);
  // ftos
  __ access_load_at(T_FLOAT, IN_HEAP, noreg /* ftos */, field, t2, noreg);
  __ push(ftos);
  volatile_barrier(_masm, flags);
  // Rewrite bytecode to be faster
  if (rc == may_rewrite) {
    patch_bytecode(Bytecodes::_fast_fgetfield, bc, R11);
  }
  __ j(Done);

  __ bind(notFloat);
#ifdef ASSERT
  __ cmp_eq_imm(ptmp0, ptmp1, dtos, tos_state);
  __ br_cond(notDouble, ptmp1);
#endif
  // dtos
  __ access_load_at(T_DOUBLE, IN_HEAP, noreg /* ftos */, field, t2, noreg);
  __ push(dtos);
  volatile_barrier(_masm, flags);
  // Rewrite bytecode to be faster
  if (rc == may_rewrite) {
    patch_bytecode(Bytecodes::_fast_dgetfield, bc, R11);
  }
#ifdef ASSERT
  __ j(Done);

  __ bind(notDouble);
  __ stop("Bad state");
#endif

  __ bind(Done);
}

void TemplateTable::getfield(int byte_no) {
  getfield_or_static(byte_no, false);
}

void TemplateTable::nofast_getfield(int byte_no) {
  getfield_or_static(byte_no, false, may_not_rewrite);
}

void TemplateTable::getstatic(int byte_no)
{
  getfield_or_static(byte_no, true);
}

// The registers cache and index expected to be set before call.
// The function may destroy various registers, just not the cache and index registers.
void TemplateTable::jvmti_post_field_mod(Register cache, Register index, bool is_static) {
  transition(vtos, vtos);

  if (JvmtiExport::can_post_field_modification()) {
    // Check to see if a field modification watch has been set before
    // we take the time to call into the VM.
    Label L1;
    assert_different_registers(cache, index, Rtos);
    __ movl(Rtos, (address)JvmtiExport::get_field_modification_count_addr());
    __ ld4(Rtos, Rtos);
    __ beqz(Rtos, L1);

    __ mov(c_rarg2, cache);

    if (is_static) {
      // Life is simple. Null out the object pointer.
      __ mov(c_rarg1, zr);
    } else {
      // Life is harder. The stack holds the value on top, followed by
      // the object. We don't know the size of the value, though; it
      // could be one or two words depending on its type. As a result,
      // we must find the type to determine where the object is.
      __ ld1(c_rarg3, Address(c_rarg2, in_bytes(ResolvedFieldEntry::type_offset())));
      Label nope2, done, ok;
      __ ld8(c_rarg1, at_tos_p1());   // initially assume a one word jvalue
      __ cmp_eq_imm(ptmp0, ptmp1, ltos, c_rarg3);
      __ br_cond(ok, ptmp0);
      __ cmp_eq_imm(ptmp0, ptmp1, dtos, c_rarg3);
      __ br_cond(nope2, ptmp1);
      __ bind(ok);
      __ ld8(c_rarg1, at_tos_p2());  // ltos (two word jvalue);
      __ bind(nope2);
    }
    // object (tos)
    __ mov(c_rarg3, Resp);
    // c_rarg1: object pointer set up above (null if static)
    // c_rarg2: cache entry pointer
    // c_rarg3: jvalue object on  the stack
    __ call_VM(noreg,
               CAST_FROM_FN_PTR(address,
                                InterpreterRuntime::post_field_modification),
                                c_rarg1, c_rarg2, c_rarg3);
    __ load_field_entry(cache, index);
    __ bind(L1);
  }
}

void TemplateTable::putfield_or_static(int byte_no, bool is_static, RewriteControl rc) {
  transition(vtos, vtos);

  const Register cache     = R12;
  const Register index     = R13;
  const Register tos_state = R13;
  const Register obj       = R12;
  const Register off       = R9;
  const Register flags     = R15;
  const Register bc        = R14;

  resolve_cache_and_index_for_field(byte_no, cache, index);
  jvmti_post_field_mod(cache, index, is_static);
  load_resolved_field_entry(obj, cache, tos_state, off, flags, is_static);

  Label Done;

  // leading barrier (riscv: StoreStore | LoadStore)
  volatile_barrier(_masm, flags);

  Label notByte, notBool, notInt, notShort, notChar,
        notLong, notFloat, notObj, notDouble;

  assert(btos == 0, "change code, btos != 0");
  __ bnez(tos_state, notByte);

  // Don't rewrite putstatic, only putfield
  if (is_static) {
    rc = may_not_rewrite;
  }

  // btos
  {
    __ pop(btos);
    // field address
    if (!is_static) {
      pop_and_check_object(obj);
    }
    __ add(off, obj, off); // if static, obj from cache, else obj from stack.
    const Address field(off, 0);
    __ access_store_at(T_BYTE, IN_HEAP, field, Rtos, noreg, noreg, noreg);
    volatile_barrier(_masm, flags);
    if (rc == may_rewrite) {
      patch_bytecode(Bytecodes::_fast_bputfield, bc, R11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notByte);
  __ cmp_eq_imm(ptmp0, ptmp1, ztos, tos_state);
  __ br_cond(notBool, ptmp1);

  // ztos
  {
    __ pop(ztos);
    // field address
    if (!is_static) {
      pop_and_check_object(obj);
    }
    __ add(off, obj, off); // if static, obj from cache, else obj from stack.
    const Address field(off, 0);
    __ access_store_at(T_BOOLEAN, IN_HEAP, field, Rtos, noreg, noreg, noreg);
    volatile_barrier(_masm, flags);
    if (rc == may_rewrite) {
      patch_bytecode(Bytecodes::_fast_zputfield, bc, R11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notBool);
  __ cmp_eq_imm(ptmp0, ptmp1, atos, tos_state);
  __ br_cond(notObj, ptmp1);

  // atos
  {
    __ pop(atos);
    // field address
    if (!is_static) {
      pop_and_check_object(obj);
    }
    __ add(off, obj, off); // if static, obj from cache, else obj from stack.
    const Address field(off, 0);
    // Store into the field
    __ store_heap_oop(field, Rtos, R28, R29, R13, IN_HEAP);
    volatile_barrier(_masm, flags);
    if (rc == may_rewrite) {
      patch_bytecode(Bytecodes::_fast_aputfield, bc, R11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notObj);
  __ cmp_eq_imm(ptmp0, ptmp1, itos, tos_state);
  __ br_cond(notInt, ptmp1);

  // itos
  {
    __ pop(itos);
    // field address
    if (!is_static) {
      pop_and_check_object(obj);
    }
    __ add(off, obj, off); // if static, obj from cache, else obj from stack.
    const Address field(off, 0);
    __ access_store_at(T_INT, IN_HEAP, field, Rtos, noreg, noreg, noreg);
    volatile_barrier(_masm, flags);
    if (rc == may_rewrite) {
      patch_bytecode(Bytecodes::_fast_iputfield, bc, R11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notInt);
  __ cmp_eq_imm(ptmp0, ptmp1, ctos, tos_state);
  __ br_cond(notChar, ptmp1);

  // ctos
  {
    __ pop(ctos);
    // field address
    if (!is_static) {
      pop_and_check_object(obj);
    }
    __ add(off, obj, off); // if static, obj from cache, else obj from stack.
    const Address field(off, 0);
    __ access_store_at(T_CHAR, IN_HEAP, field, Rtos, noreg, noreg, noreg);
    volatile_barrier(_masm, flags);
    if (rc == may_rewrite) {
      patch_bytecode(Bytecodes::_fast_cputfield, bc, R11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notChar);
  __ cmp_eq_imm(ptmp0, ptmp1, stos, tos_state);
  __ br_cond(notShort, ptmp1);

  // stos
  {
    __ pop(stos);
    // field address
    if (!is_static) {
      pop_and_check_object(obj);
    }
    __ add(off, obj, off); // if static, obj from cache, else obj from stack.
    const Address field(off, 0);
    __ access_store_at(T_SHORT, IN_HEAP, field, Rtos, noreg, noreg, noreg);
    volatile_barrier(_masm, flags);
    if (rc == may_rewrite) {
      patch_bytecode(Bytecodes::_fast_sputfield, bc, R11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notShort);
  __ cmp_eq_imm(ptmp0, ptmp1, ltos, tos_state);
  __ br_cond(notLong, ptmp1);

  // ltos
  {
    __ pop(ltos);
    // field address
    if (!is_static) {
      pop_and_check_object(obj);
    }
    __ add(off, obj, off); // if static, obj from cache, else obj from stack.
    const Address field(off, 0);
    __ access_store_at(T_LONG, IN_HEAP, field, Rtos, noreg, noreg, noreg);
    volatile_barrier(_masm, flags);
    if (rc == may_rewrite) {
      patch_bytecode(Bytecodes::_fast_lputfield, bc, R11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notLong);
  __ cmp_eq_imm(ptmp0, ptmp1, ftos, tos_state);
  __ br_cond(notFloat, ptmp1);

  // ftos
  {
    __ pop(ftos);
    // field address
    if (!is_static) {
      pop_and_check_object(obj);
    }
    __ add(off, obj, off); // if static, obj from cache, else obj from stack.
    const Address field(off, 0);
    __ access_store_at(T_FLOAT, IN_HEAP, field, noreg /* ftos */, noreg, noreg, noreg);
    volatile_barrier(_masm, flags);
    if (rc == may_rewrite) {
      patch_bytecode(Bytecodes::_fast_fputfield, bc, R11, true, byte_no);
    }
    __ j(Done);
  }

  __ bind(notFloat);
#ifdef ASSERT
  __ cmp_eq_imm(ptmp0, ptmp1, dtos, tos_state);
  __ br_cond(notDouble, ptmp1);
#endif

  // dtos
  {
    __ pop(dtos);
    // field address
    if (!is_static) {
      pop_and_check_object(obj);
    }
    __ add(off, obj, off); // if static, obj from cache, else obj from stack.
    const Address field(off, 0);
    __ access_store_at(T_DOUBLE, IN_HEAP, field, noreg /* dtos */, noreg, noreg, noreg);
    volatile_barrier(_masm, flags);
    if (rc == may_rewrite) {
      patch_bytecode(Bytecodes::_fast_dputfield, bc, R11, true, byte_no);
    }
  }

#ifdef ASSERT
  __ j(Done);

  __ bind(notDouble);
  __ stop("Bad state");
#endif

  __ bind(Done);
}

void TemplateTable::putfield(int byte_no) {
  putfield_or_static(byte_no, false);
}

void TemplateTable::nofast_putfield(int byte_no) {
  putfield_or_static(byte_no, false, may_not_rewrite);
}

void TemplateTable::putstatic(int byte_no) {
  putfield_or_static(byte_no, true);
}

void TemplateTable::jvmti_post_fast_field_mod() {
  if (JvmtiExport::can_post_field_modification()) {
    // Check to see if a field modification watch has been set before
    // we take the time to call into the VM.
    Label L2;
    __ movl(c_rarg3, (address)JvmtiExport::get_field_modification_count_addr());
    __ ld4(c_rarg3, c_rarg3);
    __ beqz(c_rarg3, L2);

    __ pop_ptr(R9);                  // copy the object pointer from tos
    __ verify_oop(R9);
    __ push_ptr(R9);                 // put the object pointer back on tos
    // Save tos values before call_VM() clobbers them. Since we have
    // to do it for every data type, we use the saved values as the
    // jvalue object.
    switch (bytecode()) {          // load values into the jvalue object
      case Bytecodes::_fast_aputfield: __ push_ptr(Rtos); break;
      case Bytecodes::_fast_bputfield: // fall through
      case Bytecodes::_fast_zputfield: // fall through
      case Bytecodes::_fast_sputfield: // fall through
      case Bytecodes::_fast_cputfield: // fall through
      case Bytecodes::_fast_iputfield: __ push_i(Rtos); break;
      case Bytecodes::_fast_dputfield: __ push_d(); break;
      case Bytecodes::_fast_fputfield: __ push_f(); break;
      case Bytecodes::_fast_lputfield: __ push_l(Rtos); break;

      default:
        ShouldNotReachHere();
    }
    __ mov(c_rarg3, Resp);             // points to jvalue on the stack
    // access constant pool cache entry
    __ load_field_entry(c_rarg2, Rtos);
    __ verify_oop(R9);
    // R9: object pointer copied above
    // c_rarg2: cache entry pointer
    // c_rarg3: jvalue object on the stack
    __ call_VM(noreg,
               CAST_FROM_FN_PTR(address,
                                InterpreterRuntime::post_field_modification),
               R9, c_rarg2, c_rarg3);

    switch (bytecode()) {             // restore tos values
      case Bytecodes::_fast_aputfield: __ pop_ptr(Rtos); break;
      case Bytecodes::_fast_bputfield: // fall through
      case Bytecodes::_fast_zputfield: // fall through
      case Bytecodes::_fast_sputfield: // fall through
      case Bytecodes::_fast_cputfield: // fall through
      case Bytecodes::_fast_iputfield: __ pop_i(Rtos); break;
      case Bytecodes::_fast_dputfield: __ pop_d(); break;
      case Bytecodes::_fast_fputfield: __ pop_f(); break;
      case Bytecodes::_fast_lputfield: __ pop_l(Rtos); break;
      default: break;
    }
    __ bind(L2);
  }
}

void TemplateTable::fast_storefield(TosState state) {
  transition(state, vtos);

  jvmti_post_fast_field_mod();

  // access constant pool cache
  __ load_field_entry(R12, R11);

  // R11: field offset, R12: field holder, R13: flags
  load_resolved_field_entry(R12, R12, noreg, R11, R13);

  volatile_barrier(_masm, R13);

  // Get object from stack
  pop_and_check_object(R12);

  // field address
  __ add(R11, R12, R11);
  const Address field(R11, 0);

  // access field, must not clobber R13 - flags
  switch (bytecode()) {
    case Bytecodes::_fast_aputfield:
      __ store_heap_oop(field, Rtos, R28, R29, R15, IN_HEAP);
      break;
    case Bytecodes::_fast_lputfield:
      __ access_store_at(T_LONG, IN_HEAP, field, Rtos, noreg, noreg, noreg);
      break;
    case Bytecodes::_fast_iputfield:
      __ access_store_at(T_INT, IN_HEAP, field, Rtos, noreg, noreg, noreg);
      break;
    case Bytecodes::_fast_zputfield:
      __ access_store_at(T_BOOLEAN, IN_HEAP, field, Rtos, noreg, noreg, noreg);
      break;
    case Bytecodes::_fast_bputfield:
      __ access_store_at(T_BYTE, IN_HEAP, field, Rtos, noreg, noreg, noreg);
      break;
    case Bytecodes::_fast_sputfield:
      __ access_store_at(T_SHORT, IN_HEAP, field, Rtos, noreg, noreg, noreg);
      break;
    case Bytecodes::_fast_cputfield:
      __ access_store_at(T_CHAR, IN_HEAP, field, Rtos, noreg, noreg, noreg);
      break;
    case Bytecodes::_fast_fputfield:
      __ access_store_at(T_FLOAT, IN_HEAP, field, noreg /* ftos */, noreg, noreg, noreg);
      break;
    case Bytecodes::_fast_dputfield:
      __ access_store_at(T_DOUBLE, IN_HEAP, field, noreg /* dtos */, noreg, noreg, noreg);
      break;
    default:
      ShouldNotReachHere();
  }

  volatile_barrier(_masm, R13);
}

void TemplateTable::fast_accessfield(TosState state) {
  transition(atos, state);
  // Do the JVMTI work here to avoid disturbing the register state below
  if (JvmtiExport::can_post_field_access()) {
    // Check to see if a field access watch has been set before we
    // take the time to call into the VM.
    Label L1;
    __ movl(R12, (address)JvmtiExport::get_field_access_count_addr());
    __ ld4(R12, R12);
    __ beqz(R12, L1);

    // access constant pool cache entry
    __ load_field_entry(c_rarg2, t2);
    __ verify_oop(Rtos);
    __ push_ptr(Rtos);  // save object pointer before call_VM() clobbers it
    __ mov(c_rarg1, Rtos);
    // c_rarg1: object pointer copied above
    // c_rarg2: cache entry pointer
    __ call_VM(noreg,
               CAST_FROM_FN_PTR(address,
                                InterpreterRuntime::post_field_access),
               c_rarg1, c_rarg2);
    __ pop_ptr(Rtos); // restore object pointer
    __ bind(L1);
  }

  // access constant pool cache
  __ load_field_entry(R12, R11);

  __ ld4s(R11, Address(R12, in_bytes(ResolvedFieldEntry::field_offset_offset())));
  __ ld1(R13, Address(R12, in_bytes(ResolvedFieldEntry::flags_offset())));

  // Rtos: object
  __ verify_oop(Rtos);
  __ null_check(Rtos);
  __ add(R11, Rtos, R11);
  const Address field(R11, 0);

  // access field
  switch (bytecode()) {
    case Bytecodes::_fast_agetfield:
      __ load_heap_oop(Rtos, field, R28, R29, IN_HEAP);
      __ verify_oop(Rtos);
      break;
    case Bytecodes::_fast_lgetfield:
      __ access_load_at(T_LONG, IN_HEAP, Rtos, field, noreg, noreg);
      break;
    case Bytecodes::_fast_igetfield:
      __ access_load_at(T_INT, IN_HEAP, Rtos, field, noreg, noreg);
      break;
    case Bytecodes::_fast_bgetfield:
      __ access_load_at(T_BYTE, IN_HEAP, Rtos, field, noreg, noreg);
      break;
    case Bytecodes::_fast_sgetfield:
      __ access_load_at(T_SHORT, IN_HEAP, Rtos, field, noreg, noreg);
      break;
    case Bytecodes::_fast_cgetfield:
      __ access_load_at(T_CHAR, IN_HEAP, Rtos, field, noreg, noreg);
      break;
    case Bytecodes::_fast_fgetfield:
      __ access_load_at(T_FLOAT, IN_HEAP, noreg /* ftos */, field, t2, noreg);
      break;
    case Bytecodes::_fast_dgetfield:
      __ access_load_at(T_DOUBLE, IN_HEAP, noreg /* dtos */, field, t2, noreg);
      break;
    default:
      ShouldNotReachHere();
  }
  volatile_barrier(_masm, R13);
}

void TemplateTable::fast_xaccess(TosState state) {
  transition(vtos, state);

  // get receiver
  __ ld8(Rtos, aaddress(0));
  // access constant pool cache
  __ load_field_entry(R12, R13, 2);
  __ ld4s(R11, Address(R12, in_bytes(ResolvedFieldEntry::field_offset_offset())));

  // make sure exception is reported in correct bcp range (getfield is
  // next instruction)
  __ adds(Rbcp, 1, Rbcp);
  __ null_check(Rtos);
  switch (state) {
    case itos:
      __ add(Rtos, Rtos, R11);
      __ access_load_at(T_INT, IN_HEAP, Rtos, Address(Rtos, 0), noreg, noreg);
      break;
    case atos:
      __ add(Rtos, Rtos, R11);
      __ load_heap_oop(Rtos, Address(Rtos, 0), R28, R29, IN_HEAP);
      __ verify_oop(Rtos);
      break;
    case ftos:
      __ add(Rtos, Rtos, R11);
      __ access_load_at(T_FLOAT, IN_HEAP, noreg /* ftos */, Address(Rtos, 0), t2, noreg);
      break;
    default:
      ShouldNotReachHere();
  }

  __ ld1(R13, Address(R12, in_bytes(ResolvedFieldEntry::flags_offset())));
  volatile_barrier(_masm, R13);

  __ adds(Rbcp, -1, Rbcp);
}

//-----------------------------------------------------------------------------
// Calls

// Loads the receiver (if any) into recv and the return address into b0.
void TemplateTable::prepare_invoke(Register cache, Register recv) {

  Bytecodes::Code code = bytecode();
  const bool load_receiver       = (code != Bytecodes::_invokestatic) && (code != Bytecodes::_invokedynamic);

  // save 'interpreter return address'
  __ save_bcp();

  // Load TOS state for later
  __ ld1(t3, Address(cache, in_bytes(ResolvedMethodEntry::type_offset())));

  // load receiver if needed (note: no return address pushed yet)
  if (load_receiver) {
    __ ld2(recv, Address(cache, in_bytes(ResolvedMethodEntry::num_parameters_offset())));
    __ shladd(t2, recv, LogBytesPerWord, Resp);
    __ ld8(recv, Address(t2, -Interpreter::expr_offset_in_bytes(1)));
    __ verify_oop(recv);
  }

  // load return address
  load_return_address(_masm, code, t3);
}

void TemplateTable::invokevirtual_helper(Register index,
                                         Register recv,
                                         Register flags) {
  // Uses temporary registers Rtos, R13
  assert_different_registers(index, recv, Rtos, R13);
  // Test for an invoke of a final method
  Label notFinal;
  __ tbit_z(ptmp0, ptmp1, flags, ResolvedMethodEntry::is_vfinal_shift);
  __ br_cond(notFinal, ptmp0);

  const Register method = index;  // method must be Rmethod
  assert(method == Rmethod, "Method must be Rmethod for interpreter calling convention");

  // do the call - the index is actually the method to call
  // that is, f2 is a vtable index if !is_vfinal, else f2 is a Method*

  // It's final, need a null check here!
  __ null_check(recv);

  __ jump_from_interpreted(method);

  __ bind(notFinal);

  // get receiver klass
  __ load_klass(Rtos, recv);

  // get target Method & entry point
  __ lookup_virtual_method(Rtos, index, method);
  __ jump_from_interpreted(method);
}

void TemplateTable::invokevirtual(int byte_no) {
  transition(vtos, vtos);
  assert(byte_no == f2_byte, "use this argument");

  load_resolved_method_entry_virtual(R12,      // ResolvedMethodEntry*
                                     Rmethod,  // Method* or itable index
                                     R13);     // flags
  prepare_invoke(R12, R12); // recv

  // Rmethod: index (actually a Method*)
  // R12: receiver
  // R13: flags

  invokevirtual_helper(Rmethod, R12, R13);
}

void TemplateTable::invokespecial(int byte_no) {
  transition(vtos, vtos);
  assert(byte_no == f1_byte, "use this argument");

  load_resolved_method_entry_special_or_static(R12,      // ResolvedMethodEntry*
                                               Rmethod,  // Method*
                                               R13);     // flags
  prepare_invoke(R12, R12);  // get receiver also for null check

  __ verify_oop(R12);
  __ null_check(R12);
  // do the call
  __ jump_from_interpreted(Rmethod);
}

void TemplateTable::invokestatic(int byte_no) {
  transition(vtos, vtos);
  assert(byte_no == f1_byte, "use this argument");

  load_resolved_method_entry_special_or_static(R12,      // ResolvedMethodEntry*
                                               Rmethod,  // Method*
                                               R13);     // flags
  prepare_invoke(R12, R12);  // get receiver also for null check

  // do the call
  __ jump_from_interpreted(Rmethod);
}

void TemplateTable::fast_invokevfinal(int byte_no) {
  // Not generated by the rewriter on this port (as on riscv).
  unimplemented_bc();
}

void TemplateTable::invokeinterface(int byte_no) {
  transition(vtos, vtos);
  assert(byte_no == f1_byte, "use this argument");

  load_resolved_method_entry_interface(R12,      // ResolvedMethodEntry*
                                       Rtos,     // Klass*
                                       Rmethod,  // Method* or itable/vtable index
                                       R13);     // flags
  prepare_invoke(R12, R12); // receiver

  // Rtos: interface klass (from f1)
  // Rmethod: method (from f2)
  // R12: receiver
  // R13: flags

  // First check for Object case, then private interface method,
  // then regular interface method.

  // Special case of invokeinterface called for virtual method of
  // java.lang.Object. See cpCache.cpp for details
  Label notObjectMethod;
  __ tbit_z(ptmp0, ptmp1, R13, ResolvedMethodEntry::is_forced_virtual_shift);
  __ br_cond(notObjectMethod, ptmp0);

  invokevirtual_helper(Rmethod, R12, R13);
  __ bind(notObjectMethod);

  Label no_such_interface;

  // Check for private method invocation - indicated by vfinal
  Label notVFinal;
  __ tbit_z(ptmp0, ptmp1, R13, ResolvedMethodEntry::is_vfinal_shift);
  __ br_cond(notVFinal, ptmp0);

  // Check receiver klass into R13
  __ load_klass(R13, R12);

  Label subtype;
  __ check_klass_subtype(R13, Rtos, R14, R15, subtype);
  // If we get here the typecheck failed
  __ j(no_such_interface);
  __ bind(subtype);

  __ jump_from_interpreted(Rmethod);

  __ bind(notVFinal);

  // Get receiver klass into R13
  __ restore_locals();
  __ load_klass(R13, R12);

  Label no_such_method;

  // Preserve method for the throw_AbstractMethodErrorVerbose.
  __ mov(R28, Rmethod);
  // Receiver subtype check against REFC.
  // Superklass in Rtos. Subklass in R13.
  __ lookup_interface_method(// inputs: rec. class, interface, itable index
                             R13, Rtos, noreg,
                             // outputs: scan temp. reg, scan temp. reg
                             R14, R30,
                             no_such_interface,
                             /*return_method=*/false);

  // Get declaring interface class from method, and itable index
  __ load_method_holder(Rtos, Rmethod);
  __ ld4(Rmethod, Address(Rmethod, Method::itable_index_offset()));
  __ mov_immediate(t2, Method::itable_index_max);
  __ sub(Rmethod, t2, Rmethod);           // itable_index_max - itable_index
  __ sxt4(Rmethod, Rmethod);

  // Preserve recvKlass for throw_AbstractMethodErrorVerbose
  __ mov(Rlocals, R13);
  __ lookup_interface_method(// inputs: rec. class, interface, itable index
                             Rlocals, Rtos, Rmethod,
                             // outputs: method, scan temp. reg
                             Rmethod, R30,
                             no_such_interface);

  // Rmethod: Method to call
  // R12: receiver
  // Check for abstract method error
  // Note: This should be done more efficiently via a throw_abstract_method_error
  //       interpreter entry point and a conditional jump to it in case of a null
  //       method.
  __ beqz(Rmethod, no_such_method);

  // do the call
  // R12: receiver
  // Rmethod: Method
  __ jump_from_interpreted(Rmethod);
  __ should_not_reach_here();

  // exception handling code follows ...
  // note: must restore interpreter registers to canonical
  //       state for exception handling to work correctly!

  __ bind(no_such_method);
  // throw exception
  __ restore_bcp();    // bcp must be correct for exception handler   (was destroyed)
  __ restore_locals(); // make sure locals pointer is correct as well (was destroyed)
  // Pass arguments for generating a verbose error message.
  __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_AbstractMethodErrorVerbose), R13, R28);
  // the call_VM checks for exception, so we should never return here.
  __ should_not_reach_here();

  __ bind(no_such_interface);
  // throw exceptiong
  __ restore_bcp();    // bcp must be correct for exception handler   (was destroyed)
  __ restore_locals(); // make sure locals pointer is correct as well (was destroyed)
  // Pass arguments for generating a verbose error message.
  __ call_VM(noreg, CAST_FROM_FN_PTR(address,
                                     InterpreterRuntime::throw_IncompatibleClassChangeErrorVerbose), R13, Rtos);
  // the call_VM checks for exception, so we should never return here.
  __ should_not_reach_here();
  return;
}

void TemplateTable::invokehandle(int byte_no) {
  transition(vtos, vtos);
  assert(byte_no == f1_byte, "use this argument");

  load_resolved_method_entry_handle(R12,      // ResolvedMethodEntry*
                                    Rmethod,  // Method*
                                    Rtos,     // Resolved reference
                                    R13);     // flags
  prepare_invoke(R12, R12);

  __ verify_oop(R12);
  __ null_check(R12);

  __ jump_from_interpreted(Rmethod);
}

void TemplateTable::invokedynamic(int byte_no) {
  transition(vtos, vtos);
  assert(byte_no == f1_byte, "use this argument");

  load_invokedynamic_entry(Rmethod);

  // Rtos: CallSite object (from cpool->resolved_references[])
  // Rmethod: MH.linkToCallSite method

  // Note: Rtos_callsite is already pushed

  __ jump_from_interpreted(Rmethod);
}

//-----------------------------------------------------------------------------
// Allocation

// IA-64 milestone 1: always the runtime path. riscv's inline TLAB fast path
// depends on fast class-initialization checks (clinit_barrier), which this
// port does not claim yet; InterpreterRuntime::_new is correct, if slower.
void TemplateTable::_new() {
  transition(vtos, atos);
  __ get_constant_pool(c_rarg1);
  __ get_unsigned_2_byte_index_at_bcp(c_rarg2, 1);
  call_VM(Rtos, CAST_FROM_FN_PTR(address, InterpreterRuntime::_new), c_rarg1, c_rarg2);
  __ verify_oop(Rtos);
  // Must prevent reordering of stores for object initialization with stores that publish the new object.
  __ membar(MacroAssembler::StoreStore);
}

void TemplateTable::newarray() {
  transition(itos, atos);
  __ ld1(c_rarg1, at_bcp(1));
  __ mov(c_rarg2, Rtos);
  call_VM(Rtos, CAST_FROM_FN_PTR(address, InterpreterRuntime::newarray),
          c_rarg1, c_rarg2);
  // Must prevent reordering of stores for object initialization with stores that publish the new object.
  __ membar(MacroAssembler::StoreStore);
}

void TemplateTable::anewarray() {
  transition(itos, atos);
  __ get_unsigned_2_byte_index_at_bcp(c_rarg2, 1);
  __ get_constant_pool(c_rarg1);
  __ mov(c_rarg3, Rtos);
  call_VM(Rtos, CAST_FROM_FN_PTR(address, InterpreterRuntime::anewarray),
          c_rarg1, c_rarg2, c_rarg3);
  // Must prevent reordering of stores for object initialization with stores that publish the new object.
  __ membar(MacroAssembler::StoreStore);
}

void TemplateTable::arraylength() {
  transition(atos, itos);
  // A null array faults in the first page: an implicit null check.
  __ ld4s(Rtos, Address(Rtos, arrayOopDesc::length_offset_in_bytes()));
}

void TemplateTable::checkcast() {
  transition(atos, atos);
  Label done, is_null, ok_is_subtype, quicked, resolved;
  __ beqz(Rtos, is_null);

  // Get cpool & tags index
  __ get_cpool_and_tags(R12, R13); // R12=cpool, R13=tags array
  __ get_unsigned_2_byte_index_at_bcp(R9, 1); // R9=index
  // See if bytecode has already been quicked
  __ add(R11, R13, R9);
  __ ld1(R11, Address(R11, Array<u1>::base_offset_in_bytes()));
  __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);
  __ cmp_eq_imm(ptmp0, ptmp1, JVM_CONSTANT_Class, R11);
  __ br_cond(quicked, ptmp0);

  __ push(atos); // save receiver for result, and for GC
  call_VM(Rtos, CAST_FROM_FN_PTR(address, InterpreterRuntime::quicken_io_cc));
  __ get_vm_result_metadata(Rtos, Rthread);
  __ pop_ptr(R13); // restore receiver
  __ j(resolved);

  // Get superklass in Rtos and subklass in R13
  __ bind(quicked);
  __ mov(R13, Rtos); // Save object in R13; Rtos needed for subtype check
  __ load_resolved_klass_at_offset(R12, R9, Rtos, t2); // Rtos = klass

  __ bind(resolved);
  __ load_klass(R9, R13);

  // Generate subtype check.  Blows R12, R15.  Object in R13.
  // Superklass in Rtos.  Subklass in R9.
  __ gen_subtype_check(R9, ok_is_subtype);

  // Come here on failure
  __ push_ptr(R13);
  // object is at TOS
  jump_to(_masm, Interpreter::_throw_ClassCastException_entry);

  // Come here on success
  __ bind(ok_is_subtype);
  __ mov(Rtos, R13); // Restore object in R13

  __ bind(is_null);   // same as 'done'
  __ bind(done);
}

void TemplateTable::instanceof() {
  transition(atos, itos);
  Label done, is_null, ok_is_subtype, quicked, resolved;
  __ beqz(Rtos, is_null);

  // Get cpool & tags index
  __ get_cpool_and_tags(R12, R13); // R12=cpool, R13=tags array
  __ get_unsigned_2_byte_index_at_bcp(R9, 1); // R9=index
  // See if bytecode has already been quicked
  __ add(R11, R13, R9);
  __ ld1(R11, Address(R11, Array<u1>::base_offset_in_bytes()));
  __ membar(MacroAssembler::LoadLoad | MacroAssembler::LoadStore);
  __ cmp_eq_imm(ptmp0, ptmp1, JVM_CONSTANT_Class, R11);
  __ br_cond(quicked, ptmp0);

  __ push(atos); // save receiver for result, and for GC
  call_VM(Rtos, CAST_FROM_FN_PTR(address, InterpreterRuntime::quicken_io_cc));
  __ get_vm_result_metadata(Rtos, Rthread);
  __ pop_ptr(R13); // restore receiver
  __ verify_oop(R13);
  __ load_klass(R13, R13);
  __ j(resolved);

  // Get superklass in Rtos and subklass in R13
  __ bind(quicked);
  __ load_klass(R13, Rtos);
  __ load_resolved_klass_at_offset(R12, R9, Rtos, t2);

  __ bind(resolved);

  // Generate subtype check.  Blows R12, R15
  // Superklass in Rtos.  Subklass in R13.
  __ gen_subtype_check(R13, ok_is_subtype);

  // Come here on failure
  __ mov(Rtos, zr);
  __ j(done);
  // Come here on success
  __ bind(ok_is_subtype);
  __ mov_immediate(Rtos, 1);

  __ bind(is_null);   // same as 'done'
  __ bind(done);
  // Rtos = 0: obj is    null or  obj is not an instanceof the specified klass
  // Rtos = 1: obj isn't null and obj is     an instanceof the specified klass
}

//-----------------------------------------------------------------------------
// Breakpoints

void TemplateTable::_breakpoint() {
  // Note: We get here even if we are single stepping..
  // jbug inists on setting breakpoints at every bytecode
  // even if we are in single step mode.

  transition(vtos, vtos);

  // get the unpatched byte code
  __ get_method(c_rarg1);
  __ mov(c_rarg2, Rbcp);
  __ call_VM(noreg,
             CAST_FROM_FN_PTR(address,
                              InterpreterRuntime::get_original_bytecode_at),
             c_rarg1, c_rarg2);
  // IA-64: riscv keeps the bytecode in callee-saved x9 across the next call;
  // nothing scratch survives one here, so it waits on the expression stack.
  push_word(_masm, Rret);

  // post the breakpoint event
  __ mov(c_rarg1, Rmethod);
  __ mov(c_rarg2, Rbcp);
  __ call_VM(noreg,
             CAST_FROM_FN_PTR(address, InterpreterRuntime::_breakpoint),
             c_rarg1, c_rarg2);

  // complete the execution of original bytecode
  __ ld8_inc(t0, Resp, wordSize);
  __ dispatch_only_normal(vtos);
}

//-----------------------------------------------------------------------------
// Exceptions

void TemplateTable::athrow() {
  transition(atos, vtos);
  __ null_check(Rtos);
  // Rexception is Rtos
  jump_to(_masm, Interpreter::throw_exception_entry());
}

//-----------------------------------------------------------------------------
// Synchronization
//
// Note: monitorenter & exit are symmetric routines; which is reflected
//       in the assembly code structure as well
//
// Stack layout:
//
// [expressions  ] <--- Resp              = expression stack top
// ..
// [expressions  ]
// [monitor entry] <--- monitor block top = expression stack bot
// ..
// [monitor entry]
// [frame data   ] <--- monitor block bot
// ...
// [saved fp     ] <--- fp

void TemplateTable::monitorenter() {
  transition(atos, vtos);

   // check for null object
   __ null_check(Rtos);

   const Address monitor_block_top(
         fp, frame::interpreter_frame_monitor_block_top_offset * wordSize);
   const Address monitor_block_bot(
         fp, frame::interpreter_frame_initial_sp_offset * wordSize);
   const int entry_size = frame::interpreter_frame_monitor_size_in_bytes();

   Label allocated;

   // initialize entry pointer
   __ mov(c_rarg1, zr); // points to free slot or null

   // find a free slot in the monitor block (result in c_rarg1)
   {
     Label entry, loop, exit, notUsed;
     __ ld8(c_rarg3, monitor_block_top); // derelativize pointer
     __ shladd(c_rarg3, c_rarg3, LogBytesPerWord, fp);
     // Now c_rarg3 points to current entry, starting with top-most entry

     __ lea(c_rarg2, monitor_block_bot); // points to word before bottom

     __ j(entry);

     __ bind(loop);
     // check if current entry is used
     // if not used then remember entry in c_rarg1
     __ ld8(t2, Address(c_rarg3, BasicObjectLock::obj_offset()));
     __ cmp_eq(ptmp0, ptmp1, t2, zr);
     __ mov(c_rarg1, c_rarg3, ptmp0);
     // check if current entry is for same object
     // if same object then stop searching
     __ beq(Rtos, t2, exit);
     // otherwise advance to next entry
     __ adds(c_rarg3, entry_size, c_rarg3);
     __ bind(entry);
     // check if bottom reached
     // if not at bottom then check this entry
     __ bne(c_rarg3, c_rarg2, loop);
     __ bind(exit);
   }

   __ bnez(c_rarg1, allocated); // check if a slot has been found and
                                // if found, continue with that on

   // allocate one if there's no free slot
   {
     Label entry, loop;
     // 1. compute new pointers            // Resp: old expression stack top

     __ check_extended_sp();
     __ adds(sp, -entry_size, sp);         // make room for the monitor
     __ sub(t2, sp, fp);
     __ shr_imm(t2, t2, Interpreter::logStackElementSize);
     __ st8(Address(fp, frame::interpreter_frame_extended_sp_offset * wordSize), t2);

     __ ld8(c_rarg1, monitor_block_bot);   // derelativize pointer
     __ shladd(c_rarg1, c_rarg1, LogBytesPerWord, fp);
     // Now c_rarg1 points to the old expression stack bottom

     __ adds(Resp, -entry_size, Resp);     // move expression stack top
     __ adds(c_rarg1, -entry_size, c_rarg1); // move expression stack bottom
     __ mov(c_rarg3, Resp);                // set start value for copy loop
     __ sub(t2, c_rarg1, fp);              // relativize pointer
     __ shr_imm(t2, t2, Interpreter::logStackElementSize);
     __ st8(monitor_block_bot, t2);        // set new monitor block bottom

     __ j(entry);
     // 2. move expression stack contents
     __ bind(loop);
     __ ld8(c_rarg2, Address(c_rarg3, entry_size)); // load expression stack
                                                    // word from old location
     __ st8_inc(c_rarg3, c_rarg2, wordSize);        // and store it at new location
                                                    // and advance to next word
     __ bind(entry);
     __ bne(c_rarg3, c_rarg1, loop);    // check if bottom reached.if not at bottom
                                        // then copy next word
   }

   // call run-time routine
   // c_rarg1: points to monitor entry
   __ bind(allocated);

   // Increment bcp to point to the next bytecode, so exception
   // handling for async. exceptions work correctly.
   // The object has already been popped from the stack, so the
   // expression stack looks correct.
   __ adds(Rbcp, 1, Rbcp);

   // store object
   __ st8(Address(c_rarg1, BasicObjectLock::obj_offset()), Rtos);
   __ lock_object(c_rarg1);

   // check to make sure this monitor doesn't cause stack overflow after locking
   __ save_bcp();  // in case of exception
   __ generate_stack_overflow_check(0);

   // The bcp has already been incremented. Just need to dispatch to
   // next instruction.
   __ dispatch_next(vtos);
}

void TemplateTable::monitorexit() {
  transition(atos, vtos);

  // check for null object
  __ null_check(Rtos);

  const Address monitor_block_top(
        fp, frame::interpreter_frame_monitor_block_top_offset * wordSize);
  const Address monitor_block_bot(
        fp, frame::interpreter_frame_initial_sp_offset * wordSize);
  const int entry_size = frame::interpreter_frame_monitor_size_in_bytes();

  Label found;

  // find matching slot
  {
    Label entry, loop;
    __ ld8(c_rarg1, monitor_block_top); // derelativize pointer
    __ shladd(c_rarg1, c_rarg1, LogBytesPerWord, fp);
    // Now c_rarg1 points to current entry, starting with top-most entry

    __ lea(c_rarg2, monitor_block_bot); // points to word before bottom
                                        // of monitor block
    __ j(entry);

    __ bind(loop);
    // check if current entry is for same object
    __ ld8(t2, Address(c_rarg1, BasicObjectLock::obj_offset()));
    // if same object then stop searching
    __ beq(Rtos, t2, found);
    // otherwise advance to next entry
    __ adds(c_rarg1, entry_size, c_rarg1);
    __ bind(entry);
    // check if bottom reached
    // if not at bottom then check this entry
    __ bne(c_rarg1, c_rarg2, loop);
  }

  // error handling. Unlocking was not block-structured
  __ call_VM(noreg, CAST_FROM_FN_PTR(address,
             InterpreterRuntime::throw_illegal_monitor_state_exception));
  __ should_not_reach_here();

  // call run-time routine
  __ bind(found);
  __ push_ptr(Rtos); // make sure object is on stack (contract with oopMaps)
  __ unlock_object(c_rarg1);
  __ pop_ptr(Rtos); // discard object
}

// Wide instructions
void TemplateTable::wide() {
  __ ld1(R9, at_bcp(1));
  __ movl(t2, (address)Interpreter::_wentry_point);
  __ shladd(t2, R9, LogBytesPerWord, t2);
  __ ld8(t2, t2);
  __ jr(t2);
}

// Multi arrays
void TemplateTable::multianewarray() {
  transition(vtos, atos);
  __ ld1(Rtos, at_bcp(3)); // get number of dimensions
  // last dim is on top of stack; we want address of first one:
  // first_addr = last_addr + (ndims - 1) * wordSize
  __ shladd(c_rarg1, Rtos, LogBytesPerWord, Resp);
  __ adds(c_rarg1, -wordSize, c_rarg1);
  call_VM(Rtos,
          CAST_FROM_FN_PTR(address, InterpreterRuntime::multianewarray),
          c_rarg1);
  __ ld1(R11, at_bcp(3));
  __ shladd(Resp, R11, LogBytesPerWord, Resp);
}
