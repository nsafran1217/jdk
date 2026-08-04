/*
 * Copyright (c) 2003, 2020, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2010 Red Hat, Inc.
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

#ifndef CPU_ZERO_STACK_ZERO_INLINE_HPP
#define CPU_ZERO_STACK_ZERO_INLINE_HPP

#include "runtime/javaThread.hpp"
#include "stack_zero.hpp"

inline void ZeroStack::overflow_check(int required_words, TRAPS) {
  // Check the Zero stack
  if (available_words() < required_words) {
    handle_overflow(THREAD);
    return;
  }

  // Check the ABI stack
  if (abi_stack_available(THREAD) < 0) {
    handle_overflow(THREAD);
    return;
  }
}

// This method returns the amount of ABI stack available for us
// to use under normal circumstances.  Note that the returned
// value can be negative.
inline int ZeroStack::abi_stack_available(Thread *thread) const {
  assert(Thread::current() == thread, "should run in the same thread");
  size_t reserved =
    StackOverflow::stack_guard_zone_size() + StackOverflow::stack_shadow_zone_size();
  int stack_used = thread->stack_base() - (address) &stack_used + reserved;
  int stack_free = thread->stack_size() - stack_used;

#ifdef IA64
  // IA-64 keeps most locals in the stacked register file rather than on the
  // memory stack, and the RSE spills them to the register backing store. Deep
  // interpreter recursion therefore consumes the register stack far faster than
  // the memory stack, and the figure computed above is not a safe proxy for it:
  // the register stack overflows first, fatally, and -- unlike a memory stack
  // overflow -- it cannot be caught, because the SIGSEGV handler does not run
  // (the faulting PC lands in the kernel gate page). Without this check, deep
  // Java recursion dies with SIGSEGV and no hs_err instead of throwing
  // StackOverflowError.
  //
  // The register backing store grows upwards from below the memory stack.
  // os::current_stack_base_and_size() reserves the lower half of the thread's
  // allocation for it, which conveniently makes the reported stack bottom
  // exactly the register stack's limit, so no per-thread bookkeeping is needed.
  address rse_limit = thread->stack_base() - thread->stack_size();
  address rse_sp;
  __asm__ __volatile__("mov %0 = ar.bsp" : "=r"(rse_sp));
  int rse_free = (int) (rse_limit - rse_sp) - (int) reserved;

  stack_free = MIN2(stack_free, rse_free);
#endif // IA64

  return stack_free;
}

#endif // CPU_ZERO_STACK_ZERO_INLINE_HPP
