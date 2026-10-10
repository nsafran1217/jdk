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
#include "interpreter/interpreter.hpp"
#include "code/compiledIC.hpp"
#include "code/vtableStubs.hpp"
#include "oops/klassVtable.hpp"
#include "vmreg_ia64.inline.hpp"
#include "runtime/sharedRuntime.hpp"
#include "utilities/debug.hpp"

#define __ masm->

// vtable and itable stubs serve megamorphic calls from compiled code. They
// are entered by a compiled call site's br.call (b0 = the return address)
// with the receiver in j_rarg0 and, for an itable stub, the CompiledICData*
// in t1 (MacroAssembler::ic_check's convention). The faulting loads -- the
// receiver's klass (npe_addr) and the Method*'s entry (ame_addr) -- are
// recorded at the loads themselves: the adds that computes each address
// comes first, IA-64 having no displacement addressing.

#ifndef PRODUCT
extern "C" void bad_compiled_vtable_index(JavaThread* thread, oop receiver, int index);
#endif

// receiver klass = receiver->klass(), with npe_addr at the load.
static address load_receiver_klass(MacroAssembler* masm, Register dst) {
  assert(!UseCompactObjectHeaders, "IA-64: compact object headers not yet supported");
  __ adds(dst, oopDesc::klass_offset_in_bytes(), j_rarg0);
  address npe_addr = __ pc();
  if (UseCompressedClassPointers) {
    __ Assembler::ld4(dst, dst);
    __ decode_klass_not_null(dst);
  } else {
    __ Assembler::ld8(dst, dst);
  }
  return npe_addr;
}

// Jump to Rmethod's compiled entry, with ame_addr at the load (a null
// Method* is an AbstractMethodError).
static address jump_to_compiled_entry(MacroAssembler* masm) {
  __ adds(t1, in_bytes(Method::from_compiled_offset()), Rmethod);
  address ame_addr = __ pc();
  __ Assembler::ld8(t1, t1);
  __ jr(t1);
  return ame_addr;
}

VtableStub* VtableStubs::create_vtable_stub(int vtable_index) {
  // Read "A word on VtableStub sizing" in share/code/vtableStubs.hpp for details on stub sizing.
  const int stub_code_length = code_size_limit(true);
  VtableStub* s = new(stub_code_length) VtableStub(true, vtable_index);
  // Can be null if there is no free space in the code cache.
  if (s == nullptr) {
    return nullptr;
  }

  int slop_bytes = 0;

  ResourceMark    rm;
  CodeBuffer      cb(s->entry_point(), stub_code_length);
  MacroAssembler* masm = new MacroAssembler(&cb);
  // Packed like compiled code. The recorded fault pcs (npe_addr, ame_addr)
  // stay bundle starts: pc() closes the open bundle (BUNDLING.md, Stage 2).
  Assembler::PackScope pack(masm);

  assert(VtableStub::receiver_location() == j_rarg0->as_VMReg(), "receiver expected in j_rarg0");

  // get receiver klass
  address npe_addr = load_receiver_klass(masm, t2);

#ifndef PRODUCT
  if (DebugVtables) {
    Label L;
    // check offset vs vtable length
    __ ld4(t3, Address(t2, Klass::vtable_length_offset()));
    __ mov_immediate(t4, vtable_index * vtableEntry::size());
    __ blt(t4, t3, L);
    __ enter();
    __ mov_immediate(t4, vtable_index);
    __ call_VM(noreg, CAST_FROM_FN_PTR(address, bad_compiled_vtable_index), j_rarg0, t4);
    __ leave();
    __ bind(L);
  }
#endif // PRODUCT

  __ mov_immediate(t3, vtable_index);
  __ lookup_virtual_method(t2, t3, Rmethod);

#ifndef PRODUCT
  if (DebugVtables) {
    Label L;
    __ beqz(Rmethod, L);
    __ ld8(t1, Address(Rmethod, Method::from_compiled_offset()));
    __ bnez(t1, L);
    __ stop("Vtable entry is null");
    __ bind(L);
  }
#endif // PRODUCT

  // Rmethod: Method*
  // j_rarg0: receiver
  address ame_addr = jump_to_compiled_entry(masm);

  masm->flush();
  bookkeeping(masm, tty, s, npe_addr, ame_addr, true, vtable_index, slop_bytes, 0);

  return s;
}

VtableStub* VtableStubs::create_itable_stub(int itable_index) {
  // Read "A word on VtableStub sizing" in share/code/vtableStubs.hpp for details on stub sizing.
  const int stub_code_length = code_size_limit(false);
  VtableStub* s = new(stub_code_length) VtableStub(false, itable_index);
  // Can be null if there is no free space in the code cache.
  if (s == nullptr) {
    return nullptr;
  }

  int slop_bytes = 0;

  ResourceMark    rm;
  CodeBuffer      cb(s->entry_point(), stub_code_length);
  MacroAssembler* masm = new MacroAssembler(&cb);
  // Packed like compiled code. The recorded fault pcs (npe_addr, ame_addr)
  // stay bundle starts: pc() closes the open bundle (BUNDLING.md, Stage 2).
  Assembler::PackScope pack(masm);

  // This stub is called from compiled code, which has no callee-saved
  // registers: everything but the arguments (r20-r27, f8-f15) is free.
  const Register icdata_reg         = r19;
  const Register recv_klass_reg     = r16;
  const Register holder_klass_reg   = r17; // declaring interface klass (DEFC)
  const Register resolved_klass_reg = r18; // resolved interface klass (REFC)
  const Register temp_reg           = r28;

  // The CompiledICData arrives in t1, which every helper clobbers: move it
  // out before anything else.
  __ mov(icdata_reg, t1);

  assert(VtableStub::receiver_location() == j_rarg0->as_VMReg(), "receiver expected in j_rarg0");

  Label L_no_such_interface;

  __ ld8(resolved_klass_reg, Address(icdata_reg, CompiledICData::itable_refc_klass_offset()));
  __ ld8(holder_klass_reg,   Address(icdata_reg, CompiledICData::itable_defc_klass_offset()));

  // get receiver klass (also an implicit null-check)
  address npe_addr = load_receiver_klass(masm, recv_klass_reg);

  // Receiver subtype check against REFC.
  // (method_result is scratch here: t2.)
  __ lookup_interface_method(recv_klass_reg, resolved_klass_reg, noreg, t2, temp_reg,
                             L_no_such_interface, /* return_method */ false);

  // Get selected method from declaring class and itable index
  // (lookup_interface_method destroys the receiver klass: reload it).
  __ load_klass(recv_klass_reg, j_rarg0);
  __ mov_immediate(Rmethod, itable_index);
  __ lookup_interface_method(recv_klass_reg, holder_klass_reg, Rmethod, Rmethod, temp_reg,
                             L_no_such_interface);

#ifdef ASSERT
  if (DebugVtables) {
    Label L2;
    __ beqz(Rmethod, L2);
    __ ld8(t1, Address(Rmethod, Method::from_compiled_offset()));
    __ bnez(t1, L2);
    __ stop("compiler entrypoint is null");
    __ bind(L2);
  }
#endif // ASSERT

  // Rmethod: Method*
  // j_rarg0: receiver
  address ame_addr = jump_to_compiled_entry(masm);

  __ bind(L_no_such_interface);
  // Handle IncompatibleClassChangeError in itable stubs.
  // More detailed error message.
  // We force resolving of the call site by jumping to the "handle
  // wrong method" stub, and so let the interpreter runtime do all the
  // dirty work.
  assert(SharedRuntime::get_handle_wrong_method_stub() != nullptr, "check initialization order");
  __ far_jump(SharedRuntime::get_handle_wrong_method_stub());

  masm->flush();
  bookkeeping(masm, tty, s, npe_addr, ame_addr, false, itable_index, slop_bytes, 0);

  return s;
}

int VtableStub::pd_code_alignment() {
  // One instruction per 16-byte bundle.
  return BytesPerBundle;
}
