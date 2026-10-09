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
#include "code/codeCache.hpp"
#include "code/compiledIC.hpp"
#include "nativeInst_ia64.hpp"
#include "runtime/safepoint.hpp"
#include "runtime/atomic.hpp"
#include "runtime/sharedRuntime.hpp"
#include "utilities/ostream.hpp"

// The sequences recognised here are exactly the ones MacroAssembler emits:
//
//   call:  br.cond L ; <cell> ; L: mov t0 = ip ; adds t0 = -16, t0 ;
//          ld8 t0 = [t0] ; mov b6 = t0 ; br.call.sptk.many b0 = b6   (7 bundles)
//   jump:  movl t0 = target ; mov b6 = t0 ; br.cond.sptk b6          (3 bundles)
//
// (MacroAssembler::far_call / far_jump.)

static bool bundle_equals(address a, ia64::Bundle expected) {
  const ia64::Bundle* b = (const ia64::Bundle*)a;
  return b->lo == expected.lo && b->hi == expected.hi;
}

static bool is_movl_t0_at(address a) {
  // Compare against a movl of the same register with the immediate the
  // bundle actually carries, so only the opcode and register are checked.
  uint64_t imm = ia64::ReadMovlImm((const ia64::Bundle*)a);
  return bundle_equals(a, ia64::MovlBundle(t0->encoding(), imm));
}

static bool is_mov_b6_t0_at(address a) {
  return bundle_equals(a, ia64::BundleI(ia64::MovToBr(btmp.encoding(), t0->encoding())));
}

bool NativeInstruction::is_nop() const {
  return bundle_equals(addr_at(0), ia64::BundleNop());
}

bool NativeInstruction::is_jump() const {
  return is_movl_t0_at(addr_at(0)) &&
         is_mov_b6_t0_at(addr_at(BytesPerBundle)) &&
         bundle_equals(addr_at(2 * BytesPerBundle), ia64::BundleB(ia64::BrCond(btmp.encoding())));
}

bool NativeInstruction::is_jump_or_nop() {
  return is_nop() || is_jump();
}

// This port polls for safepoints through a thread-local word (a load and a
// tbit), never through a polling page, so no instruction is a "poll" in
// the page-fault sense.
bool NativeInstruction::is_safepoint_poll() {
  return false;
}

bool NativeInstruction::is_call_at(address addr) {
  return NativeCall::is_at(addr);
}

// ---- NativeCall ---------------------------------------------------------------

bool NativeCall::is_at(address addr) {
  return bundle_equals(addr + branch_offset, ia64::BundleB(ia64::BrCondRel(2))) &&
         bundle_equals(addr + mov_ip_offset, ia64::BundleI(ia64::MovFromIp(t0->encoding()))) &&
         bundle_equals(addr + adds_offset, ia64::BundleM(ia64::Adds(t0->encoding(), -(int)BytesPerBundle, t0->encoding()))) &&
         bundle_equals(addr + ld8_offset, ia64::BundleM(ia64::Ld8(t0->encoding(), t0->encoding()))) &&
         is_mov_b6_t0_at(addr + mov_to_br_offset) &&
         bundle_equals(addr + br_call_offset, ia64::BundleB(ia64::BrCall(breturn.encoding(), btmp.encoding())));
}

NativeCall* nativeCall_before(address return_address) {
  NativeCall* call = (NativeCall*)(return_address - NativeCall::return_address_offset);
  DEBUG_ONLY(call->verify());
  return call;
}

address NativeCall::destination() const {
  return (address)Atomic::load((volatile uint64_t*)addr_at(cell_offset));
}

// The cell is data: no instruction-cache flush. The 8-byte aligned store is
// atomic, so a thread executing the call sees either the old or the new
// destination, never a mixture.
void NativeCall::set_destination(address dest) {
  assert(is_aligned(addr_at(cell_offset), BytesPerWord), "cell must be 8-byte aligned");
  Atomic::store((volatile uint64_t*)addr_at(cell_offset), (uint64_t)dest);
}

void NativeCall::set_destination_mt_safe(address dest) {
  assert(SafepointSynchronize::is_at_safepoint() || CodeCache_lock->owned_by_self() ||
         CompiledICLocker::is_safe(addr_at(0)),
         "IA-64: call patching outside a safepoint must hold the IC lock");
  set_destination(dest);
}

void NativeCall::verify() {
  assert(NativeCall::is_at(addr_at(0)), "unexpected code at call site: " PTR_FORMAT, p2i(addr_at(0)));
}

void NativeCall::print() {
  tty->print_cr(PTR_FORMAT ": call " PTR_FORMAT, p2i(instruction_address()), p2i(destination()));
}

// ---- NativeMovConstReg ----------------------------------------------------------

void NativeMovConstReg::verify() {
  uint8_t tmpl = (uint8_t)(bundle_at(0)->lo & 0x1f);
  assert(tmpl == ia64::tMLX || tmpl == ia64::tMLX_, "not a movl at " PTR_FORMAT, p2i(addr_at(0)));
}

void NativeMovConstReg::print() {
  tty->print_cr(PTR_FORMAT ": movl " INTPTR_FORMAT, p2i(instruction_address()), data());
}

// ---- NativeJump -----------------------------------------------------------------

address NativeJump::jump_destination() const {
  return (address)ia64::ReadMovlImm(bundle_at(0));
}

void NativeJump::set_jump_destination(address dest) {
  ia64::WriteMovlImm(bundle_at(0), (uint64_t)dest);
  ICache::invalidate_range(addr_at(0), BytesPerBundle);
}

void NativeJump::verify() {
  assert(is_jump(), "not a jump at " PTR_FORMAT, p2i(addr_at(0)));
}

// Making an nmethod not entrant patches its verified entry, which is a nop
// bundle (C1_MacroAssembler::verified_entry, generate_native_wrapper), into
// the same bundle with break.m in slot 0. Only slot 0's bits change, and
// they lie in the bundle's low eight bytes, so one aligned 8-byte store makes
// the change atomically: a thread fetching the bundle sees either the nop or
// the break. The break arrives as SIGILL, and the signal handler sends the
// caller to dest, the handle_wrong_method stub (os_linux_ia64.cpp,
// NativeInstruction::is_sigill_not_entrant).
static ia64::Bundle not_entrant_bundle() {
  return ia64::MakeBundle(ia64::tMII_, ia64::BreakM(NativeInstruction::not_entrant_break_imm),
                          ia64::NopI(), ia64::NopI());
}

bool NativeInstruction::is_sigill_not_entrant() {
  return bundle_equals(addr_at(0), not_entrant_bundle());
}

void NativeJump::patch_verified_entry(address entry, address verified_entry, address dest) {
  assert(dest == SharedRuntime::get_handle_wrong_method_stub(), "expected fixed destination of patch");
  assert(is_aligned(verified_entry, BytesPerBundle), "bundle-aligned");
  ia64::Bundle* b = (ia64::Bundle*)verified_entry;
  ia64::Bundle trap = not_entrant_bundle();
  assert(bundle_equals(verified_entry, ia64::BundleNop()) || bundle_equals(verified_entry, trap),
         "verified entry must be the nop bundle at " PTR_FORMAT, p2i(verified_entry));
  assert(trap.hi == ia64::BundleNop().hi, "only the low half may change");
  Atomic::store(&b->lo, trap.lo);
  ICache::invalidate_range(verified_entry, BytesPerBundle);
}

void NativeGeneralJump::insert_unconditional(address code_pos, address entry) {
  Unimplemented();
}

void NativeGeneralJump::replace_mt_safe(address instr_addr, address code_buffer) {
  Unimplemented();
}

void NativeIllegalInstruction::insert(address code_pos) {
  Unimplemented();
}

// ---- continuations and deoptimization ---------------------------------------------
//
// Post-call nops carry data only for continuations (VMContinuations is false
// here) and deoptimization instructions only exist in compiled code.

void NativePostCallNop::make_deopt() {
  Unimplemented();
}

bool NativeDeoptInstruction::is_deopt_at(address instr) {
  return false;
}

void NativeDeoptInstruction::insert(address code_pos) {
  Unimplemented();
}

void NativeDeoptInstruction::verify() {
}
