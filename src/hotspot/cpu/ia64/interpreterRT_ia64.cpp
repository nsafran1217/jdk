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
#include "interpreter/interp_masm.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/universe.hpp"
#include "oops/method.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/icache.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/signature.hpp"

#define __ _masm->

// See interpreterRT_ia64.hpp for the argument-passing rule.

static const Register slot_regs[8] = { out0, out1, out2, out3, out4, out5, out6, out7 };
static const FloatRegister fp_arg_regs[8] = { f8, f9, f10, f11, f12, f13, f14, f15 };

// Psabi: stack arguments start above the 16-byte scratch area.
static const int first_stack_arg_offset = 16;

// Implementation of SignatureHandlerGenerator
Register InterpreterRuntime::SignatureHandlerGenerator::from() { return Rlocals; }
Register InterpreterRuntime::SignatureHandlerGenerator::to()   { return sp; }
Register InterpreterRuntime::SignatureHandlerGenerator::temp() { return t2; }

InterpreterRuntime::SignatureHandlerGenerator::SignatureHandlerGenerator(
  const methodHandle& method, CodeBuffer* buffer) : NativeSignatureIterator(method) {
  _masm = new MacroAssembler(buffer); // allocate on resourse area by default
  // slot 0 is the JNIEnv*, slot 1 the class mirror of a static method
  _next_slot = method->is_static() ? 2 : 1;
  _num_fp_regs = 0;
  _stack_offset = first_stack_arg_offset;
}

Register InterpreterRuntime::SignatureHandlerGenerator::next_slot(int* stack_off) {
  if (_next_slot < 8) {
    return slot_regs[_next_slot++];
  }
  _next_slot++;
  *stack_off = _stack_offset;
  _stack_offset += wordSize;
  return noreg;
}

// Ints are passed sign-extended to 64 bits, as Java's own values are kept.
void InterpreterRuntime::SignatureHandlerGenerator::pass_int() {
  const Address src(from(), Interpreter::local_offset_in_bytes(offset()));
  int stack_off;
  Register reg = next_slot(&stack_off);
  if (reg != noreg) {
    __ ld4s(reg, src);
  } else {
    __ ld4s(temp(), src);
    __ st8(Address(to(), stack_off), temp());
  }
}

void InterpreterRuntime::SignatureHandlerGenerator::pass_long() {
  const Address src(from(), Interpreter::local_offset_in_bytes(offset() + 1));
  int stack_off;
  Register reg = next_slot(&stack_off);
  if (reg != noreg) {
    __ ld8(reg, src);
  } else {
    __ ld8(temp(), src);
    __ st8(Address(to(), stack_off), temp());
  }
}

void InterpreterRuntime::SignatureHandlerGenerator::pass_float() {
  const Address src(from(), Interpreter::local_offset_in_bytes(offset()));
  int stack_off;
  Register reg = next_slot(&stack_off);
  if (reg != noreg) {
    // Within the first eight slots there is always an FP register left:
    // each FP argument takes one slot and one register.
    assert(_num_fp_regs < 8, "FP argument registers exhausted before slots");
    __ ldfs(fp_arg_regs[_num_fp_regs++], src, temp());
  } else {
    // On the stack in memory format: the single in the slot's low 4 bytes.
    __ ld4(temp(), src);
    __ st4(Address(to(), stack_off), temp());
  }
}

void InterpreterRuntime::SignatureHandlerGenerator::pass_double() {
  const Address src(from(), Interpreter::local_offset_in_bytes(offset() + 1));
  int stack_off;
  Register reg = next_slot(&stack_off);
  if (reg != noreg) {
    assert(_num_fp_regs < 8, "FP argument registers exhausted before slots");
    __ ldfd(fp_arg_regs[_num_fp_regs++], src, temp());
  } else {
    __ ld8(temp(), src);
    __ st8(Address(to(), stack_off), temp());
  }
}

// An object is passed as the address of its local slot (a handle), or null
// if the slot holds null.
void InterpreterRuntime::SignatureHandlerGenerator::pass_object() {
  int stack_off;
  Register reg = next_slot(&stack_off);
  Register handle = (reg != noreg) ? reg : temp();
  __ lea(handle, Address(from(), Interpreter::local_offset_in_bytes(offset())));
  __ ld8(t1, handle);
  __ cmp_eq(ptmp0, ptmp1, t1, zr);
  __ mov(handle, zr, ptmp0);              // null oop -> null handle
  if (reg == noreg) {
    __ st8(Address(to(), stack_off), handle);
  }
}

void InterpreterRuntime::SignatureHandlerGenerator::generate(uint64_t fingerprint) {
  // generate code to handle arguments
  iterate(fingerprint);

  // return result handler
  __ movl(Rret, Interpreter::result_handler(method()->result_type()));
  __ ret();

  __ flush();
}


// Implementation of SignatureHandlerLibrary

// The handlers are generated code called from generated code; nothing to
// patch in front of them (PPC64 ELFv1 adds a descriptor here).
void SignatureHandlerLibrary::pd_set_handler(address handler) {}
