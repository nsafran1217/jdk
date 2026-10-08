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

// Every relocatable value this port emits lives in the immediate of a movl:
// a constant (NativeMovConstReg), or the target of a call or jump sequence
// (NativeCall / NativeJump, whose first bundle is that movl). So setting or
// reading a relocated value is reading or rewriting one movl immediate, and
// targets are absolute: moving code changes none of them.

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

address Relocation::pd_call_destination(address orig_addr) {
  assert(is_call(), "should be an address instruction here");
  // Absolute target: the same whether read at the old or the new location.
  address site = (orig_addr != nullptr) ? orig_addr : addr();
  guarantee(is_movl_at(site), "IA-64: call site must start with a movl");
  return (address)ia64::ReadMovlImm((const ia64::Bundle*)site);
}

void Relocation::pd_set_call_destination(address x) {
  assert(is_call(), "should be an address instruction here");
  guarantee(is_movl_at(addr()), "IA-64: call site must start with a movl");
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
