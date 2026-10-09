/*
 * Copyright (c) 2022, 2025, Oracle and/or its affiliates. All rights reserved.
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

#include "opto/c2_CodeStubs.hpp"
#include "opto/c2_MacroAssembler.hpp"
#include "runtime/javaThread.hpp"
#include "runtime/sharedRuntime.hpp"

#define __ masm.

// mov ip; add_imm (at most movl + add); adds + st8; far_jump (movl, mov b6, br).
int C2SafepointPollStub::max_size() const {
  return 8 * BytesPerBundle;
}

// The return poll (MachEpilogNode): the frame is already gone and b0 holds
// the return address. As C1SafepointPollStub: record the poll's pc,
// position-independently (the code is copied), for the handler blob.
void C2SafepointPollStub::emit(C2_MacroAssembler& masm) {
  assert(SharedRuntime::polling_page_return_handler_blob() != nullptr,
         "polling page return stub not created yet");
  __ bind(entry());
  __ mov_from_ip(t0);
  __ add_imm(t0, t0, _safepoint_offset - __ offset() + (int)BytesPerBundle, t1);
  __ st8(Address(Rthread, JavaThread::saved_exception_pc_offset()), t0, t1);
  __ far_jump(SharedRuntime::polling_page_return_handler_blob()->entry_point());
}

// No C2EntryBarrierStub: the nmethod entry barrier is emitted inline, as on
// ppc, by BarrierSetAssembler::nmethod_entry_barrier (C2-DESIGN.md section 8).

#undef __
