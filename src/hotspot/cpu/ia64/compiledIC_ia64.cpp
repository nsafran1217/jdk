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
#include "code/compiledIC.hpp"
#include "code/nmethod.hpp"
#include "logging/log.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/safepoint.hpp"

// Static and opt-virtual calls from compiled code (FRAME-DESIGN.md 11.2).
//
// The call site is a NativeCall (an IP-relative br.call, through a trampoline
// if out of range). Its to-interpreter stub, in the stub section, is
//
//     movl Rmethod = <Method*>       NativeMovConstReg, 0 while clean
//     movl t0 = <c2i entry>          \
//     mov  b6 = t0                    > NativeJump (far_jump), -1 while clean
//     br.cond.sptk b6                /
//
// The stub's two movl immediates are rewritten non-atomically, which is safe
// only because the stub is unreachable while it is being filled in: the call
// site is repointed to the stub afterwards (set_to_interpreted), and
// cleaned stubs are not reachable from any call site (set_stub_to_clean runs
// with the IC lock held after the call has been redirected).

#define __ masm->

address CompiledDirectCall::emit_to_interp_stub(MacroAssembler *masm, address mark) {
  precond(__ code()->stubs()->start() != badAddress);
  precond(__ code()->stubs()->end() != badAddress);

  if (mark == nullptr) {
    mark = __ inst_mark();  // Get mark within main instrs section.
  }

  address base = __ start_a_stub(to_interp_stub_size());
  int offset = __ offset();
  if (base == nullptr) {
    return nullptr;  // CodeBuffer::expand failed
  }
  // static stub relocation stores the instruction address of the call
  __ relocate(static_stub_Relocation::spec(mark));
  __ emit_static_call_stub();
  assert((__ offset() - offset) <= (int)to_interp_stub_size(), "stub too big");
  __ end_a_stub();
  return base;
}

#undef __

int CompiledDirectCall::to_interp_stub_size() {
  return MacroAssembler::static_call_stub_size();
}

int CompiledDirectCall::to_trampoline_stub_size() {
  return MacroAssembler::max_trampoline_stub_size();
}

// Relocation entries for a call to the interpreter: the static_stub relocation
// in the stub plus the call relocation at the call site (with slack).
int CompiledDirectCall::reloc_to_interp_stub() {
  return 4;
}

void CompiledDirectCall::set_to_interpreted(const methodHandle& callee, address entry) {
  address stub = find_stub();
  guarantee(stub != nullptr, "stub not found");

  // Creation also verifies the object.
  NativeMovConstReg* method_holder = nativeMovConstReg_at(stub);
  NativeJump* jump = nativeJump_at(method_holder->next_instruction_address());

#ifdef ASSERT
  verify_mt_safe(callee, entry, method_holder, jump);
#endif

  // Update stub (unreachable until the call site below is repointed).
  method_holder->set_data((intptr_t)callee());
  jump->set_jump_destination(entry);
  ICache::invalidate_range(stub, to_interp_stub_size());

  // Repoint the call site to the stub (in range: the same nmethod).
  set_destination_mt_safe(stub);
}

void CompiledDirectCall::set_stub_to_clean(static_stub_Relocation* static_stub) {
  // Reset stub.
  address stub = static_stub->addr();
  assert(stub != nullptr, "stub not found");
  assert(CompiledICLocker::is_safe(stub), "mt unsafe call");
  // Creation also verifies the object.
  NativeMovConstReg* method_holder = nativeMovConstReg_at(stub);
  method_holder->set_data(0);
  NativeJump* jump = nativeJump_at(method_holder->next_instruction_address());
  jump->set_jump_destination((address)-1);
}

//-----------------------------------------------------------------------------
// Non-product mode code
#ifndef PRODUCT

void CompiledDirectCall::verify() {
  // Verify call.
  _call->verify();
  _call->verify_alignment();

  // Verify stub.
  address stub = find_stub();
  assert(stub != nullptr, "no stub found for static call");
  // Creation also verifies the object.
  NativeMovConstReg* method_holder = nativeMovConstReg_at(stub);
  NativeJump* jump = nativeJump_at(method_holder->next_instruction_address());
  DEBUG_ONLY(method_holder->verify(); jump->verify();)

  // Verify state.
  assert(is_clean() || is_call_to_compiled() || is_call_to_interpreted(), "sanity check");
}

#endif // !PRODUCT
