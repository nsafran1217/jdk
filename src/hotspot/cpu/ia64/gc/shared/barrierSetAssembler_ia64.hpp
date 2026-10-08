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
#ifndef CPU_IA64_GC_SHARED_BARRIERSETASSEMBLER_IA64_HPP
#define CPU_IA64_GC_SHARED_BARRIERSETASSEMBLER_IA64_HPP

#include "asm/macroAssembler.hpp"
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/barrierSetNMethod.hpp"
#include "memory/allocation.hpp"
#include "oops/access.hpp"

// Bring-up scope (JIT-SCOPE.md phase 5): only the GCs that need no barrier
// code of their own -- Serial, Parallel, Epsilon -- are built for the
// non-zero variants (tools/configure-jdk.sh), so this is the base class plus
// the card-table post barrier. Compressed oops are off in milestone 1
// (FRAME-DESIGN.md 2.4); the oop paths assert that rather than half-support
// it. nmethod entry barriers belong with C1 (milestone 2).
//
// Floating-point values travel in f8, the psABI FP argument/return register,
// which is also the interpreter's FP top-of-stack register.

class BarrierSetAssembler: public CHeapObj<mtGC> {
public:
  virtual void arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, bool is_oop,
                                  Register src, Register dst, Register count, RegSet saved_regs) {}
  virtual void arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, bool is_oop,
                                  Register start, Register count, Register tmp, RegSet saved_regs) {}

  virtual void load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                       Register dst, Address src, Register tmp1, Register tmp2);
  virtual void store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                        Address dst, Register val, Register tmp1, Register tmp2, Register tmp3);

  virtual void try_resolve_jobject_in_native(MacroAssembler* masm, Register jni_env,
                                             Register obj, Register tmp, Label& slowpath);

  virtual void tlab_allocate(MacroAssembler* masm,
    Register obj,                      // result: pointer to object after successful allocation
    Register var_size_in_bytes,        // object size in bytes if unknown at compile time; invalid otherwise
    int      con_size_in_bytes,        // object size in bytes if   known at compile time
    Register tmp1,                     // temp register
    Register tmp2,                     // temp register
    Label&   slow_case                 // continuation point if fast allocation fails
  );

  virtual void barrier_stubs_init() {}

  virtual void nmethod_entry_barrier(MacroAssembler* masm);
  virtual void c2i_entry_barrier(MacroAssembler* masm);
};

#endif // CPU_IA64_GC_SHARED_BARRIERSETASSEMBLER_IA64_HPP
