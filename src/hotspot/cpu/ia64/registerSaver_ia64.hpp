/*
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

#ifndef CPU_IA64_REGISTERSAVER_IA64_HPP
#define CPU_IA64_REGISTERSAVER_IA64_HPP

#include "asm/macroAssembler.hpp"
#include "compiler/oopMap.hpp"
#include "utilities/debug.hpp"

// RegisterSaver (FRAME-DESIGN.md 11.1)
//
// Saves every register that can hold a live value of compiled code across a
// call into the runtime, describes them in an OopMap, and restores them. The
// frame is enter()'s linkage plus a save area below it:
//
//   fp ->  caller's sp
//          return address, caller's fp        (enter(): 4 words incl. scratch)
//          ...
//   sp + fr_dbl_off   f8-f31 as memory-format doubles, 8 bytes each: what the
//                     OopMap points at (deoptimization reads saved FPRs as
//                     doubles -- pd_float_saved_as_double). Written only when
//                     the FPRs are described.
//   sp + fr_spill_off f8-f31 as 16-byte stf.spill images: what is restored,
//                     bit-exact for any register value
//   sp + gr_off       r6-r11, r14-r31
//   sp + pr_off       all predicates (a safepoint may fall between a C1
//                     compare and the branch reading p10/p11)
//   sp + 0            16-byte psABI scratch
//
// r6/r7 are preserved by C callees, but C2 allocates them (C2-DESIGN.md
// section 1), so they are saved and described like the others: GC must find
// and update an oop held there, and deoptimization must read a value from
// there.
//
// Not saved: r0, r1 (gp, scratch and never live in generated code), r2/r3 (the
// MacroAssembler temporaries this code uses), r4/r5 (fp and Rthread), r12 sp,
// r13 tp, out0-out7, f2-f7 (internal temporaries), f0/f1.
//
// Shared by sharedRuntime_ia64.cpp (resolve, safepoint and deopt blobs) and
// c1_Runtime1_ia64.cpp (the C1 runtime stubs).
class RegisterSaver {
 public:
  static const int gr_count = 24;
  static const int fr_first = 8;
  static const int fr_count = 24;   // f8-f31

  enum {
    pr_off       = 16,
    gr_off       = 24,
    fr_spill_off = 224,                                  // align_up(gr_off + 24 * 8, 16)
    fr_dbl_off   = fr_spill_off + fr_count * 16,         // 608
    save_bytes   = fr_dbl_off + fr_count * 8             // 800
  };

  static int gr_at(int i) {
    static const int grs[gr_count] = { 6, 7, 8, 9, 10, 11, 14, 15, 16, 17, 18, 19,
                                       20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
    return grs[i];
  }
  static int gr_offset_in_bytes(Register r) {
    for (int i = 0; i < gr_count; i++) {
      if (gr_at(i) == r->encoding()) return gr_off + i * wordSize;
    }
    ShouldNotReachHere();
    return -1;
  }
  static int fr_dbl_offset_in_bytes(FloatRegister f) {
    int i = f->encoding() - fr_first;
    assert(0 <= i && i < fr_count, "not a saved FPR");
    return fr_dbl_off + i * wordSize;
  }

  // Frame size in words, including enter()'s linkage.
  static int frame_size_in_words() {
    return MacroAssembler::enter_frame_words() + save_bytes / wordSize;
  }

  // describe_fprs: also store the memory-format copies and describe the FPRs
  // in the map (needed wherever deoptimization may read them). with_enter:
  // lay down enter()'s linkage first; without it the caller has already done
  // so (a C1 StubFrame).
  static OopMap* save_live_registers(MacroAssembler* masm, int* total_frame_words,
                                     bool describe_fprs = true, bool with_enter = true);
  // The map save_live_registers returns, for a further call site in the same
  // frame.
  static OopMap* oop_map(bool describe_fprs = true);
  // keep: a saved GR not reloaded, so a result computed in the frame survives.
  static void restore_live_registers(MacroAssembler* masm, bool with_leave = true,
                                     Register keep = noreg);
};

STATIC_ASSERT(RegisterSaver::fr_spill_off >= RegisterSaver::gr_off + RegisterSaver::gr_count * wordSize);
STATIC_ASSERT(RegisterSaver::fr_spill_off % 16 == 0);
STATIC_ASSERT(RegisterSaver::save_bytes % 16 == 0);

#endif // CPU_IA64_REGISTERSAVER_IA64_HPP
