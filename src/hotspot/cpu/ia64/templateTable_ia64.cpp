/*
 * Copyright (c) 1997, 2025, Oracle and/or its affiliates. All rights reserved.
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
#include "interpreter/interp_masm.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "interpreter/templateTable.hpp"
#include "runtime/frame.inline.hpp"

// The bytecode templates.
//
// Bring-up state: every template emits a trap that names its bytecode
// (TemplateTable::unimplemented_bc, which stop()s with Bytecodes::name).
// All templates are generated when the VM starts, so a trap -- rather than a
// generation-time Unimplemented() -- lets the VM get as far as executing
// Java code, and the first bytecode it meets that is not yet written stops it
// with a message naming that bytecode. Templates are replaced with real
// implementations (ported from cpu/riscv) in the order execution needs them.
// See HANDOFF.md.

#define __ Disassembler::hook<InterpreterMacroAssembler>(__FILE__, __LINE__, _masm)->

// Shared hooks with a real (trivial) implementation.

void TemplateTable::shouldnotreachhere() {
  transition(vtos, vtos);
  __ stop("shouldnotreachhere bytecode");
}

void TemplateTable::nop() {
  transition(vtos, vtos);
  // nothing to do
}

// Not yet implemented: trap naming the bytecode.
void TemplateTable::_breakpoint() { unimplemented_bc(); }
void TemplateTable::_new() { unimplemented_bc(); }
void TemplateTable::_return(TosState arg0) { unimplemented_bc(); }
void TemplateTable::aaload() { unimplemented_bc(); }
void TemplateTable::aastore() { unimplemented_bc(); }
void TemplateTable::aconst_null() { unimplemented_bc(); }
void TemplateTable::aload() { unimplemented_bc(); }
void TemplateTable::aload(int arg0) { unimplemented_bc(); }
void TemplateTable::aload_0() { unimplemented_bc(); }
void TemplateTable::anewarray() { unimplemented_bc(); }
void TemplateTable::arraylength() { unimplemented_bc(); }
void TemplateTable::astore() { unimplemented_bc(); }
void TemplateTable::astore(int arg0) { unimplemented_bc(); }
void TemplateTable::athrow() { unimplemented_bc(); }
void TemplateTable::baload() { unimplemented_bc(); }
void TemplateTable::bastore() { unimplemented_bc(); }
void TemplateTable::bipush() { unimplemented_bc(); }
void TemplateTable::branch(bool arg0, bool arg1) { unimplemented_bc(); }
void TemplateTable::caload() { unimplemented_bc(); }
void TemplateTable::castore() { unimplemented_bc(); }
void TemplateTable::checkcast() { unimplemented_bc(); }
void TemplateTable::convert() { unimplemented_bc(); }
void TemplateTable::daload() { unimplemented_bc(); }
void TemplateTable::dastore() { unimplemented_bc(); }
void TemplateTable::dconst(int arg0) { unimplemented_bc(); }
void TemplateTable::dload() { unimplemented_bc(); }
void TemplateTable::dload(int arg0) { unimplemented_bc(); }
void TemplateTable::dneg() { unimplemented_bc(); }
void TemplateTable::dop2(Operation arg0) { unimplemented_bc(); }
void TemplateTable::dstore() { unimplemented_bc(); }
void TemplateTable::dstore(int arg0) { unimplemented_bc(); }
void TemplateTable::dup() { unimplemented_bc(); }
void TemplateTable::dup2() { unimplemented_bc(); }
void TemplateTable::dup2_x1() { unimplemented_bc(); }
void TemplateTable::dup2_x2() { unimplemented_bc(); }
void TemplateTable::dup_x1() { unimplemented_bc(); }
void TemplateTable::dup_x2() { unimplemented_bc(); }
void TemplateTable::faload() { unimplemented_bc(); }
void TemplateTable::fast_accessfield(TosState arg0) { unimplemented_bc(); }
void TemplateTable::fast_aldc(LdcType arg0) { unimplemented_bc(); }
void TemplateTable::fast_binaryswitch() { unimplemented_bc(); }
void TemplateTable::fast_icaload() { unimplemented_bc(); }
void TemplateTable::fast_iload() { unimplemented_bc(); }
void TemplateTable::fast_iload2() { unimplemented_bc(); }
void TemplateTable::fast_invokevfinal(int arg0) { unimplemented_bc(); }
void TemplateTable::fast_linearswitch() { unimplemented_bc(); }
void TemplateTable::fast_storefield(TosState arg0) { unimplemented_bc(); }
void TemplateTable::fast_xaccess(TosState arg0) { unimplemented_bc(); }
void TemplateTable::fastore() { unimplemented_bc(); }
void TemplateTable::fconst(int arg0) { unimplemented_bc(); }
void TemplateTable::fload() { unimplemented_bc(); }
void TemplateTable::fload(int arg0) { unimplemented_bc(); }
void TemplateTable::float_cmp(bool arg0, int arg1) { unimplemented_bc(); }
void TemplateTable::fneg() { unimplemented_bc(); }
void TemplateTable::fop2(Operation arg0) { unimplemented_bc(); }
void TemplateTable::fstore() { unimplemented_bc(); }
void TemplateTable::fstore(int arg0) { unimplemented_bc(); }
void TemplateTable::getfield(int arg0) { unimplemented_bc(); }
void TemplateTable::getstatic(int arg0) { unimplemented_bc(); }
void TemplateTable::iaload() { unimplemented_bc(); }
void TemplateTable::iastore() { unimplemented_bc(); }
void TemplateTable::iconst(int arg0) { unimplemented_bc(); }
void TemplateTable::idiv() { unimplemented_bc(); }
void TemplateTable::if_0cmp(Condition arg0) { unimplemented_bc(); }
void TemplateTable::if_acmp(Condition arg0) { unimplemented_bc(); }
void TemplateTable::if_icmp(Condition arg0) { unimplemented_bc(); }
void TemplateTable::if_nullcmp(Condition arg0) { unimplemented_bc(); }
void TemplateTable::iinc() { unimplemented_bc(); }
void TemplateTable::iload() { unimplemented_bc(); }
void TemplateTable::iload(int arg0) { unimplemented_bc(); }
void TemplateTable::ineg() { unimplemented_bc(); }
void TemplateTable::instanceof() { unimplemented_bc(); }
void TemplateTable::invokedynamic(int arg0) { unimplemented_bc(); }
void TemplateTable::invokehandle(int arg0) { unimplemented_bc(); }
void TemplateTable::invokeinterface(int arg0) { unimplemented_bc(); }
void TemplateTable::invokespecial(int arg0) { unimplemented_bc(); }
void TemplateTable::invokestatic(int arg0) { unimplemented_bc(); }
void TemplateTable::invokevirtual(int arg0) { unimplemented_bc(); }
void TemplateTable::iop2(Operation arg0) { unimplemented_bc(); }
void TemplateTable::irem() { unimplemented_bc(); }
void TemplateTable::istore() { unimplemented_bc(); }
void TemplateTable::istore(int arg0) { unimplemented_bc(); }
void TemplateTable::laload() { unimplemented_bc(); }
void TemplateTable::lastore() { unimplemented_bc(); }
void TemplateTable::lcmp() { unimplemented_bc(); }
void TemplateTable::lconst(int arg0) { unimplemented_bc(); }
void TemplateTable::ldc(LdcType arg0) { unimplemented_bc(); }
void TemplateTable::ldc2_w() { unimplemented_bc(); }
void TemplateTable::ldiv() { unimplemented_bc(); }
void TemplateTable::lload() { unimplemented_bc(); }
void TemplateTable::lload(int arg0) { unimplemented_bc(); }
void TemplateTable::lmul() { unimplemented_bc(); }
void TemplateTable::lneg() { unimplemented_bc(); }
void TemplateTable::lookupswitch() { unimplemented_bc(); }
void TemplateTable::lop2(Operation arg0) { unimplemented_bc(); }
void TemplateTable::lrem() { unimplemented_bc(); }
void TemplateTable::lshl() { unimplemented_bc(); }
void TemplateTable::lshr() { unimplemented_bc(); }
void TemplateTable::lstore() { unimplemented_bc(); }
void TemplateTable::lstore(int arg0) { unimplemented_bc(); }
void TemplateTable::lushr() { unimplemented_bc(); }
void TemplateTable::monitorenter() { unimplemented_bc(); }
void TemplateTable::monitorexit() { unimplemented_bc(); }
void TemplateTable::multianewarray() { unimplemented_bc(); }
void TemplateTable::newarray() { unimplemented_bc(); }
void TemplateTable::nofast_aload_0() { unimplemented_bc(); }
void TemplateTable::nofast_getfield(int arg0) { unimplemented_bc(); }
void TemplateTable::nofast_iload() { unimplemented_bc(); }
void TemplateTable::nofast_putfield(int arg0) { unimplemented_bc(); }
void TemplateTable::pop() { unimplemented_bc(); }
void TemplateTable::pop2() { unimplemented_bc(); }
void TemplateTable::putfield(int arg0) { unimplemented_bc(); }
void TemplateTable::putstatic(int arg0) { unimplemented_bc(); }
void TemplateTable::ret() { unimplemented_bc(); }
void TemplateTable::saload() { unimplemented_bc(); }
void TemplateTable::sastore() { unimplemented_bc(); }
void TemplateTable::sipush() { unimplemented_bc(); }
void TemplateTable::swap() { unimplemented_bc(); }
void TemplateTable::tableswitch() { unimplemented_bc(); }
void TemplateTable::wide() { unimplemented_bc(); }
void TemplateTable::wide_aload() { unimplemented_bc(); }
void TemplateTable::wide_astore() { unimplemented_bc(); }
void TemplateTable::wide_dload() { unimplemented_bc(); }
void TemplateTable::wide_dstore() { unimplemented_bc(); }
void TemplateTable::wide_fload() { unimplemented_bc(); }
void TemplateTable::wide_fstore() { unimplemented_bc(); }
void TemplateTable::wide_iinc() { unimplemented_bc(); }
void TemplateTable::wide_iload() { unimplemented_bc(); }
void TemplateTable::wide_istore() { unimplemented_bc(); }
void TemplateTable::wide_lload() { unimplemented_bc(); }
void TemplateTable::wide_lstore() { unimplemented_bc(); }
void TemplateTable::wide_ret() { unimplemented_bc(); }

// Platform-specific helpers declared in templateTable_ia64.hpp.
void TemplateTable::prepare_invoke(Register cache, Register recv) { Unimplemented(); }
void TemplateTable::invokevirtual_helper(Register index, Register recv, Register flags) { Unimplemented(); }
void TemplateTable::index_check(Register array, Register index) { Unimplemented(); }
