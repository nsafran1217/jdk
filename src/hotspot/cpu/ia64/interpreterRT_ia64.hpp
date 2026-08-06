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

#ifndef CPU_IA64_INTERPRETERRT_IA64_HPP
#define CPU_IA64_INTERPRETERRT_IA64_HPP

// This is included in the middle of class Interpreter.
// Do not include files here.

// Native method calls.
//
// Note for the implementation (JIT-SCOPE.md phase 5): the psABI numbers
// arguments positionally 1-8, and each position has BOTH a general slot
// (out0-out7) and a floating-point slot (f8-f15). An argument uses whichever
// matches its type but consumes both, so an FP argument in position N leaves
// out(N-1) unused. Tracking only one counter is the classic way to get this
// wrong. cpu/ppc's and the SpiderMonkey backend's ABIArgGenerator both
// implement the rule correctly and are worth reading first.
//
// The generated handler must also be preceded by a {entry, gp} function
// descriptor so C++ can call it by pointer, exactly as
// interpreterRT_ppc.cpp:140 patches one in for PPC64 ELFv1.

class SignatureHandlerGenerator: public NativeSignatureIterator {
 private:
  MacroAssembler* _masm;
  unsigned int _num_reg_fp_args;
  unsigned int _num_reg_int_args;
  int _stack_offset;

  void pass_int();
  void pass_long();
  void pass_float();
  void pass_double();
  void pass_object();

  Register next_gpr();
  FloatRegister next_fpr();
  int next_stack_offset();

 public:
  // Creation
  SignatureHandlerGenerator(const methodHandle& method, CodeBuffer* buffer);

  // Code generation
  void generate(uint64_t fingerprint);

  // Code generation support
  static Register from();
  static Register to();
  static Register temp();
};

#endif // CPU_IA64_INTERPRETERRT_IA64_HPP
