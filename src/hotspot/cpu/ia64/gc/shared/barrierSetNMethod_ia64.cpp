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
#include "code/nmethod.hpp"
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/barrierSetAssembler.hpp"
#include "gc/shared/barrierSetNMethod.hpp"
#include "logging/log.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/atomic.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/javaThread.hpp"
#include "runtime/registerMap.hpp"
#include "runtime/sharedRuntime.hpp"
#include "utilities/debug.hpp"

// The guard is a 4-byte word inside the immediate of a nop.x in the barrier
// sequence, which ends at the nmethod's frame-complete offset
// (BarrierSetAssembler::nmethod_entry_barrier). Reading and writing it is a
// plain acquire load and release store; whatever it holds, the bundle
// executes the same, so no instruction is ever patched.

static int* guard_addr(nmethod* nm) {
  address barrier = nm->code_begin() + nm->frame_complete_offset()
                    - BarrierSetAssembler::entry_barrier_size;
  address guard = barrier + BarrierSetAssembler::entry_barrier_guard_offset;
  // The barrier starts with an MLX bundle (no stop) and its guard bundle is
  // MLX with a stop. Anything emitted between the barrier and frame-complete
  // would move this window onto code -- and the guard store would corrupt it.
  assert((barrier[0] & 0x1f) == ia64::tMLX && (guard[-8] & 0x1f) == ia64::tMLX_,
         "nmethod entry barrier not where frame-complete says: " PTR_FORMAT, p2i(barrier));
  return (int*)guard;
}

// Called from the method_entry_barrier stub when the nmethod may not be
// entered. Discard its frame and make it look as though the caller had called
// handle_wrong_method: the stub loads these four words (stubGenerator_ia64.cpp,
// generate_method_entry_barrier) and jumps.
void BarrierSetNMethod::deoptimize(nmethod* nm, address* return_address_ptr) {
  typedef struct {
    intptr_t* sp; intptr_t* fp; address ra; address pc;
  } frame_pointers_t;

  frame_pointers_t* new_frame = (frame_pointers_t*)(return_address_ptr - 5);

  JavaThread* thread = JavaThread::current();
  RegisterMap reg_map(thread,
                      RegisterMap::UpdateMap::skip,
                      RegisterMap::ProcessFrames::include,
                      RegisterMap::WalkContinuation::skip);
  frame frame = thread->last_frame();

  assert(frame.is_compiled_frame() || frame.is_native_frame(), "must be");
  assert(frame.cb() == nm, "must be");
  frame = frame.sender(&reg_map);

  LogTarget(Trace, nmethod, barrier) out;
  if (out.is_enabled()) {
    ResourceMark mark;
    log_trace(nmethod, barrier)("deoptimize(nmethod: %s(%p), return_addr: %p, osr: %d, thread: %p(%s), making rsp: %p) -> %p",
                                nm->method()->name_and_sig_as_C_string(),
                                nm, *(address *) return_address_ptr, nm->is_osr_method(), thread,
                                thread->name(), frame.sp(), nm->verified_entry_point());
  }

  new_frame->sp = frame.sp();
  new_frame->fp = frame.fp();
  new_frame->ra = frame.pc();
  new_frame->pc = SharedRuntime::get_handle_wrong_method_stub();
}

void BarrierSetNMethod::set_guard_value(nmethod* nm, int value) {
  if (!supports_entry_barrier(nm)) {
    return;
  }
  Atomic::release_store(guard_addr(nm), value);
}

int BarrierSetNMethod::guard_value(nmethod* nm) {
  if (!supports_entry_barrier(nm)) {
    return disarmed_guard_value();
  }
  return Atomic::load_acquire(guard_addr(nm));
}
