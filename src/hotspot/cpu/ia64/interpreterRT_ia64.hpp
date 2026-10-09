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
// The generated handler copies a native method's Java arguments into the C
// argument positions the psABI prescribes. The rule (LLVM's
// CC_IA64_FP_Common, GCC's ia64_function_arg) has two counters, and keeping
// them apart is the whole difficulty:
//
//  - every argument consumes the next of eight positional GR slots,
//    out0-out7, whatever its type; beyond eight, 8-byte stack slots from
//    sp+16 (above the psABI scratch area);
//  - a floating-point argument within the first eight slots travels in the
//    next *unused FP register*, f8-f15 in order of FP arguments -- not in
//    f(8 + slot) -- leaving its GR slot unused.
//
// Slot 0 is the JNIEnv*; for a static method slot 1 is the class mirror.
//
// The handler is called from generated code (the native entry), never from
// C++, so unlike PPC64 ELFv1 it needs no function descriptor in front of it.

// The slow signature handler's save area, relative to its `to` pointer (the
// outgoing stack arguments): the generated stub's frame lies directly below
// `to` - 16 (interpreterRT_ia64.cpp, generate_slow_signature_handler).
enum {
  slow_handler_frame_bytes = 176,   // 16 scratch, b0, mask, 8 GR, 8 FP words, pad
  slow_handler_gr_area_off = -(slow_handler_frame_bytes + 16) + 32,
  slow_handler_fp_mask_off = -(slow_handler_frame_bytes + 16) + 24,
  slow_handler_fp_area_off = -(slow_handler_frame_bytes + 16) + 96
};

class SignatureHandlerGenerator: public NativeSignatureIterator {
 private:
  MacroAssembler* _masm;
  unsigned int _next_slot;      // next positional GR slot, 0-7, then stack
  unsigned int _num_fp_regs;    // FP argument registers used so far
  int _stack_offset;            // next stack slot, from sp

  void pass_int();
  void pass_long();
  void pass_float();
  void pass_double();
  void pass_object();

  // Claims the next positional slot: its out register, or noreg when the
  // slot is on the stack (in which case *stack_off says where).
  Register next_slot(int* stack_off);

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
