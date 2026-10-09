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
#include "asm/macroAssembler.hpp"
#include "code/relocInfo.hpp"
#include "nativeInst_ia64.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/safepoint.hpp"

// Every relocatable value this port emits is absolute, so moving code changes
// none of them. Data values (oops, Metadata*, addresses) live in a movl
// immediate (NativeMovConstReg); call destinations live in a NativeCall's
// data cell; jump destinations in the movl of a far_jump.

static bool is_movl_at(address a) {
  uint8_t tmpl = (uint8_t)(((const ia64::Bundle*)a)->lo & 0x1f);
  return tmpl == ia64::tMLX || tmpl == ia64::tMLX_;
}

void Relocation::pd_set_data_value(address x, bool verify_only) {
  guarantee(is_movl_at(addr()), "IA-64: relocated value must be a movl immediate at " PTR_FORMAT, p2i(addr()));
  if (verify_only) {
    assert(ia64::ReadMovlImm((const ia64::Bundle*)addr()) == (uint64_t)x, "instructions must match");
    return;
  }
  ia64::WriteMovlImm((ia64::Bundle*)addr(), (uint64_t)x);
  ICache::invalidate_range(addr(), BytesPerBundle);
}

// A call relocation sits at the start of either a NativeCall (the cell form,
// FRAME-DESIGN.md 11.2: the destination is in the cell) or a movl-form jump
// (far_jump: the destination is the movl immediate). Both are absolute, so the
// value read at the old location is the value to write at the new one.
address Relocation::pd_call_destination(address orig_addr) {
  assert(is_call(), "should be an address instruction here");
  address site = (orig_addr != nullptr) ? orig_addr : addr();
  if (NativeCall::is_at(site)) {
    return nativeCall_at(site)->destination();
  }
  guarantee(is_movl_at(site), "IA-64: unrecognised call site at " PTR_FORMAT, p2i(site));
  return (address)ia64::ReadMovlImm((const ia64::Bundle*)site);
}

void Relocation::pd_set_call_destination(address x) {
  assert(is_call(), "should be an address instruction here");
  if (NativeCall::is_at(addr())) {
    nativeCall_at(addr())->set_destination(x);
    return;
  }
  guarantee(is_movl_at(addr()), "IA-64: unrecognised call site at " PTR_FORMAT, p2i(addr()));
  ia64::WriteMovlImm((ia64::Bundle*)addr(), (uint64_t)x);
  ICache::invalidate_range(addr(), BytesPerBundle);
}

// No relocation in this port stores an address as a plain word in the
// instruction stream; a movl immediate is split across the bundle.
address* Relocation::pd_address_in_code() {
  ShouldNotReachHere();
  return nullptr;
}

address Relocation::pd_get_address_from_code() {
  guarantee(is_movl_at(addr()), "IA-64: relocated value must be a movl immediate");
  return (address)ia64::ReadMovlImm((const ia64::Bundle*)addr());
}

void poll_Relocation::fix_relocation_after_move(const CodeBuffer* src, CodeBuffer* dest) {
  // Polls are thread-local loads through Rthread: nothing position-dependent.
}

void metadata_Relocation::pd_fix_value(address x) {
}
