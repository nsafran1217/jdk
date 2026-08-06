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

#ifndef OS_CPU_LINUX_IA64_ORDERACCESS_LINUX_IA64_HPP
#define OS_CPU_LINUX_IA64_ORDERACCESS_LINUX_IA64_HPP

// Included in orderAccess.hpp header file.

// IA-64 memory ordering
// ---------------------
// The architecture has exactly *one* standalone memory fence instruction, `mf`.
// Ordering is normally expressed not with fences but with completers on the
// individual accesses -- ld.acq, st.rel, cmpxchg.acq/.rel -- which is why
// there is no cheaper "light" barrier to reach for here: anything weaker than
// `mf` has to be attached to a specific load or store, and these entry points
// take no operand to attach it to.
//
// So LIGHT_MEM_BARRIER == FULL_MEM_BARRIER == mf, which is what the Zero port
// has been running on rx2800 (it falls through to the generic
// __sync_synchronize() default in orderAccess_linux_zero.hpp). Correct, and
// conservative.
//
// The place to recover the lost performance is the *access* sites -- teaching
// Access<>/the barrier assemblers to emit ld.acq and st.rel instead of a plain
// access plus a fence -- not here.

#define LIGHT_MEM_BARRIER __sync_synchronize()
#define FULL_MEM_BARRIER  __sync_synchronize()

inline void OrderAccess::loadload()   { LIGHT_MEM_BARRIER; }
inline void OrderAccess::storestore() { LIGHT_MEM_BARRIER; }
inline void OrderAccess::loadstore()  { LIGHT_MEM_BARRIER; }
inline void OrderAccess::storeload()  { FULL_MEM_BARRIER;  }

inline void OrderAccess::acquire()    { LIGHT_MEM_BARRIER; }
inline void OrderAccess::release()    { LIGHT_MEM_BARRIER; }

inline void OrderAccess::fence()      { FULL_MEM_BARRIER;  }

// Serialize instruction prefetch so this CPU sees code another CPU has just
// written (HotSpot calls this when a thread reaches a safepoint after code has
// been patched). x86 uses cpuid, aarch64 uses isb; the IA-64 equivalent is
// srlz.i, which serializes instruction fetch against prior operations.
//
// Note this is only the *reading* side. The writing side -- flushing the
// modified lines out of the data cache and into the instruction cache -- is
// ICache::invalidate_range's job (fc.i / sync.i), and is required because the
// IA-64 instruction cache is not coherent with stores.
inline void OrderAccess::cross_modify_fence_impl() {
  __asm__ volatile ("srlz.i" : : : "memory");
}

#endif // OS_CPU_LINUX_IA64_ORDERACCESS_LINUX_IA64_HPP
