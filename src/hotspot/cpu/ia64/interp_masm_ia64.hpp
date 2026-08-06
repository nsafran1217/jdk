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

#ifndef CPU_IA64_INTERP_MASM_IA64_HPP
#define CPU_IA64_INTERP_MASM_IA64_HPP

#include "asm/macroAssembler.hpp"
#include "interpreter/invocationCounter.hpp"
#include "runtime/frame.hpp"

// Interpreter-specific specialization of the assembler.
//
// Bring-up state: declarations only, bodies in interp_masm_ia64.cpp are
// Unimplemented(). This is JIT-SCOPE.md phase 4 work and is where most of the
// port's remaining volume lives (157 bytecode templates on top of this).
//
// One IA-64 consequence to keep in mind while filling these in: only r4-r7 are
// preserved across a C call, so Rmethod, Rcpool, Rmonitors, Rdispatch and
// Rsender_sp do NOT survive call_VM and must be reloaded from the frame
// afterwards. Rthread, Rbcp, Rlocals and Resp do survive, which is why they
// were given the four preserved registers. See FRAME-DESIGN.md section 2.3.

typedef ByteSize (*OffsetFunction)(uint);

class InterpreterMacroAssembler : public MacroAssembler {
 protected:
  using MacroAssembler::call_VM_leaf_base;

  // Base routine for all dispatches.
  void dispatch_base(TosState state, address* table, bool verifyoop = true,
                     bool generate_poll = false, Register Rs = t0);

 public:
  InterpreterMacroAssembler(CodeBuffer* code) : MacroAssembler(code) {}

  void get_method(Register reg);

  // Interpreter state reload after a call into C++ (see the note above).
  void restore_bcp();
  void restore_locals();
  void restore_constant_pool_cache();
  void restore_method();

  void dispatch_next(TosState state, int step = 0, bool generate_poll = false);
  void dispatch_only(TosState state, bool generate_poll = false, Register Rs = t0);
  void dispatch_via(TosState state, address* table);

  void remove_activation(TosState state,
                         bool throw_monitor_exception = true,
                         bool install_monitor_exception = true,
                         bool notify_jvmdi = true);
};

#endif // CPU_IA64_INTERP_MASM_IA64_HPP
