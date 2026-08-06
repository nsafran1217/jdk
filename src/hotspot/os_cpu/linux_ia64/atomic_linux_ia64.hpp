/*
 * Copyright (c) 2003, 2025, Oracle and/or its affiliates. All rights reserved.
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

#ifndef OS_CPU_LINUX_IA64_ATOMIC_LINUX_IA64_HPP
#define OS_CPU_LINUX_IA64_ATOMIC_LINUX_IA64_HPP

#include "orderAccess_linux_ia64.hpp"

// Implementation of class Atomic.
//
// This is deliberately the same shape as atomic_linux_zero.hpp: GCC's
// __atomic_* builtins bracketed by full fences. That code is not a placeholder
// -- it is the implementation the Zero port has been running on rx2800, so it
// is the only IA-64 atomic implementation in this tree with evidence behind it,
// and milestone 1 keeps it rather than trading proven behaviour for speed.
//
// It is conservative in a specific way worth recording: IA-64 provides
// cmpxchg.acq / cmpxchg.rel / fetchadd.acq / fetchadd.rel, so the ordering can
// ride on the instruction itself instead of a surrounding `mf`. Passing the
// real memory order through to the builtins would let GCC emit those completers
// and drop the fences. That is the optimisation to make once the port is
// correct, ideally together with the ld.acq/st.rel work noted in
// orderAccess_linux_ia64.hpp -- and it must be measured against the
// java/util/concurrent/atomic tests on real hardware, not reasoned about.

template<size_t byte_size>
struct Atomic::PlatformAdd {
  template<typename D, typename I>
  D add_then_fetch(D volatile* dest, I add_value, atomic_memory_order order) const;

  template<typename D, typename I>
  D fetch_then_add(D volatile* dest, I add_value, atomic_memory_order order) const {
    return add_then_fetch(dest, add_value, order) - add_value;
  }
};

template<>
template<typename D, typename I>
inline D Atomic::PlatformAdd<4>::add_then_fetch(D volatile* dest, I add_value,
                                                atomic_memory_order order) const {
  STATIC_ASSERT(4 == sizeof(I));
  STATIC_ASSERT(4 == sizeof(D));

  D res = __atomic_add_fetch(dest, add_value, __ATOMIC_RELEASE);
  FULL_MEM_BARRIER;
  return res;
}

template<>
template<typename D, typename I>
inline D Atomic::PlatformAdd<8>::add_then_fetch(D volatile* dest, I add_value,
                                                atomic_memory_order order) const {
  STATIC_ASSERT(8 == sizeof(I));
  STATIC_ASSERT(8 == sizeof(D));

  D res = __atomic_add_fetch(dest, add_value, __ATOMIC_RELEASE);
  FULL_MEM_BARRIER;
  return res;
}

template<>
template<typename T>
inline T Atomic::PlatformXchg<4>::operator()(T volatile* dest,
                                             T exchange_value,
                                             atomic_memory_order order) const {
  STATIC_ASSERT(4 == sizeof(T));
  FULL_MEM_BARRIER;
  T result = __atomic_exchange_n(dest, exchange_value, __ATOMIC_RELAXED);
  FULL_MEM_BARRIER;
  return result;
}

template<>
template<typename T>
inline T Atomic::PlatformXchg<8>::operator()(T volatile* dest,
                                             T exchange_value,
                                             atomic_memory_order order) const {
  STATIC_ASSERT(8 == sizeof(T));
  FULL_MEM_BARRIER;
  T result = __atomic_exchange_n(dest, exchange_value, __ATOMIC_RELAXED);
  FULL_MEM_BARRIER;
  return result;
}

// IA-64 has cmpxchg1/2/4/8 but the byte form takes the comparand from ar.ccv,
// whose *exact* bit pattern is compared -- a narrow cmpxchg fed a comparand
// that is not zero-extended to the access width silently never succeeds. That
// hazard belongs in the generated-code path (see the note on MovToArCcv in the
// assembler); here the shared CmpxchgByteUsingInt emulation sidesteps it
// entirely, as it does on Zero.
template<>
struct Atomic::PlatformCmpxchg<1> : Atomic::CmpxchgByteUsingInt {};

template<>
template<typename T>
inline T Atomic::PlatformCmpxchg<4>::operator()(T volatile* dest,
                                                T compare_value,
                                                T exchange_value,
                                                atomic_memory_order order) const {
  STATIC_ASSERT(4 == sizeof(T));

  T value = compare_value;
  FULL_MEM_BARRIER;
  __atomic_compare_exchange(dest, &value, &exchange_value, /*weak*/false,
                            __ATOMIC_RELAXED, __ATOMIC_RELAXED);
  FULL_MEM_BARRIER;
  return value;
}

template<>
template<typename T>
inline T Atomic::PlatformCmpxchg<8>::operator()(T volatile* dest,
                                                T compare_value,
                                                T exchange_value,
                                                atomic_memory_order order) const {
  STATIC_ASSERT(8 == sizeof(T));

  T value = compare_value;
  FULL_MEM_BARRIER;
  __atomic_compare_exchange(dest, &value, &exchange_value, /*weak*/false,
                            __ATOMIC_RELAXED, __ATOMIC_RELAXED);
  FULL_MEM_BARRIER;
  return value;
}

// Atomically copy 64 bits of data. Naturally atomic here: IA-64 is 64-bit and
// an aligned ld8/st8 pair cannot tear (SUPPORTS_NATIVE_CX8 is defined for the
// same reason). Unaligned access, by contrast, *traps* on this architecture --
// callers must respect alignment.
inline void atomic_copy64(const volatile void *src, volatile void *dst) {
  int64_t tmp;
  __atomic_load(reinterpret_cast<const volatile int64_t*>(src), &tmp, __ATOMIC_RELAXED);
  __atomic_store(reinterpret_cast<volatile int64_t*>(dst), &tmp, __ATOMIC_RELAXED);
}

template<>
template<typename T>
inline T Atomic::PlatformLoad<8>::operator()(T const volatile* src) const {
  STATIC_ASSERT(8 == sizeof(T));
  T dest;
  __atomic_load(const_cast<T*>(src), &dest, __ATOMIC_RELAXED);
  return dest;
}

template<>
template<typename T>
inline void Atomic::PlatformStore<8>::operator()(T volatile* dest,
                                                 T store_value) const {
  STATIC_ASSERT(8 == sizeof(T));
  __atomic_store(dest, &store_value, __ATOMIC_RELAXED);
}

#endif // OS_CPU_LINUX_IA64_ATOMIC_LINUX_IA64_HPP
