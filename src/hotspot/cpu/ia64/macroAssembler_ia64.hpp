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

#ifndef CPU_IA64_MACROASSEMBLER_IA64_HPP
#define CPU_IA64_MACROASSEMBLER_IA64_HPP

#include "asm/assembler.hpp"
#include "asm/assembler.inline.hpp"
#include "utilities/powerOfTwo.hpp"

// MacroAssembler for IA-64.
//
// Bring-up state: this is Phase 1a scaffolding (JIT-SCOPE.md). The sequences
// that are short and unambiguous are implemented; everything requiring a frame
// or calling-convention decision is Unimplemented() until the stubs and
// interpreter arrive in phases 3 and 4. The point of the file existing now is
// to keep the tree linking while those are filled in.

class MacroAssembler : public Assembler {
 public:
  MacroAssembler(CodeBuffer* code) : Assembler(code) {}

  // Every instruction occupies one 16-byte bundle. Shared code and nativeInst
  // use this to step through generated code.
  enum {
    instruction_size = BytesPerBundle
  };

  // ---- alignment ---------------------------------------------------------

  // Pad with whole nop bundles. modulus must be a multiple of the bundle size:
  // there is no way to advance the instruction stream by less than 16 bytes.
  void align(int modulus) {
    assert(modulus % BytesPerBundle == 0, "must be a multiple of the bundle size");
    while (offset() % modulus != 0) { nop(); }
  }

  // ---- immediates and addresses ------------------------------------------
  //
  // There is no gp-relative addressing in generated code and IP-relative
  // branches reach only +/-16 MiB, so any value that does not fit the 14-bit
  // `adds` immediate is materialised with a 64-bit movl.

  void mov_immediate64(Register dst, uint64_t imm) { movl(dst, imm); }

  void movptr(Register dst, address addr)   { movl(dst, (uint64_t)(uintptr_t)addr); }
  void movptr(Register dst, uintptr_t imm)  { movl(dst, (uint64_t)imm); }

  void mov_immediate(Register dst, int64_t imm) {
    if (ia64::is_simm14(imm)) {
      adds(dst, imm, zr);          // r0 reads as 0
    } else {
      movl(dst, (uint64_t)imm);
    }
  }

  // Materialise the effective address of |adr| into |dst|. IA-64 has no
  // displacement addressing, so this is a real computation at every access
  // site rather than something folded into the load or store.
  void lea(Register dst, const Address& adr) {
    switch (adr.getMode()) {
      case Address::base_plus_offset:
        if (adr.offset() == 0) {
          mov(dst, adr.base());
        } else if (ia64::is_simm14(adr.offset())) {
          adds(dst, adr.offset(), adr.base());
        } else {
          movl(dst, (uint64_t)adr.offset());
          add(dst, dst, adr.base());
        }
        break;
      case Address::literal:
        movl(dst, (uint64_t)(uintptr_t)adr.target());
        break;
      default:
        ShouldNotReachHere();
    }
  }

  // ---- stack banging -----------------------------------------------------
  //
  // Touches the memory stack only: generated code cannot move ar.bsp, because
  // it executes a single `alloc` in call_stub and never another
  // (FRAME-DESIGN.md section 1). So unlike the Zero port there is no register
  // backing store to probe here.
  //
  // Callers stepping through a range must step by os::vm_page_size(), which is
  // 16384 on this machine. A loop stepping by 4096 -- the figure baked into
  // several ports -- would skip three of every four guard pages and miss the
  // guard entirely.
  void bang_stack_with_offset(int offset) {
    assert(offset > 0, "must bang below the stack pointer");
    lea(t0, Address(sp, -offset));
    st8(t0, zr);
  }

  // ---- null checks -------------------------------------------------------

  static bool needs_explicit_null_check(intptr_t offset) {
    return !(offset >= 0 && offset < os::vm_page_size());
  }

  static bool uses_implicit_null_check(void* address) {
    uintptr_t addr = (uintptr_t)address;
    return addr < (uintptr_t)os::vm_page_size();
  }

  // ---- not yet decided ---------------------------------------------------
  //
  // Each of these needs the frame layout or calling convention to be settled
  // first; see FRAME-DESIGN.md and JIT-SCOPE.md phases 3-5.

  void should_not_reach_here() { brk(0); }

  // Call a C function through its psABI descriptor. A C function pointer is
  // the address of a two-word {entry, gp} descriptor, not a code address, so a
  // plain indirect branch would land in the descriptor's own data. Measured on
  // rx2800: a callee in another DSO carries a *different* gp, so the callee's
  // gp must come from the callee's own descriptor rather than from a value
  // captured once. See FRAME-DESIGN.md section 3.
  void call_c(address function_descriptor);
  void call_c(Register function_descriptor);

  // Emit a {entry, gp} descriptor at the current position whose entry points
  // just past it, so that C++ can call the following generated code by
  // pointer. The direct analogue of PPC ELFv1's emit_fd()/function_entry().
  address function_entry();

  void verify_oop(Register reg, const char* s = "broken oop") {}
  void verify_oop_msg(Register reg, const char* msg) {}
};

#endif // CPU_IA64_MACROASSEMBLER_IA64_HPP
