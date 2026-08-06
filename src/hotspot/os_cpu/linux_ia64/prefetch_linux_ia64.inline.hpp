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

#ifndef OS_CPU_LINUX_IA64_PREFETCH_LINUX_IA64_INLINE_HPP
#define OS_CPU_LINUX_IA64_PREFETCH_LINUX_IA64_INLINE_HPP

#include "runtime/prefetch.hpp"

// IA-64 has a real prefetch instruction, lfetch, with fault and hint
// completers (lfetch.nt1, lfetch.excl, ...). It is not wired up yet:
// vm_version_ia64.cpp sets AllocatePrefetchDistance to 0, so nothing asks for
// a prefetch, and an unused inline asm here would only be a way to get the
// hint completers wrong before anything could test them.
//
// When this is implemented, note that lfetch takes its address in a register
// like every other IA-64 memory operation -- there is no displacement form --
// so the interval has to be added first.

inline void Prefetch::read(const void* loc, intx interval) {}

inline void Prefetch::write(void* loc, intx interval) {}

#endif // OS_CPU_LINUX_IA64_PREFETCH_LINUX_IA64_INLINE_HPP
