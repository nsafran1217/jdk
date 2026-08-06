/*
 * Copyright (c) 2000, 2025, Oracle and/or its affiliates. All rights reserved.
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

#ifndef OS_CPU_LINUX_IA64_GLOBALS_LINUX_IA64_HPP
#define OS_CPU_LINUX_IA64_GLOBALS_LINUX_IA64_HPP

// Sets the default values for platform dependent flags used by the runtime system.
// (see globals.hpp)

// A thread's allocation is split between the memory stack (growing down) and
// the RSE register backing store (growing up); os::current_stack_base_and_size()
// reserves the lower half for the backing store, so only half of what is
// requested is usable as memory stack. Ask for twice the usual figure so the
// effective size matches the other 64-bit Linux ports: 1024 (x86), 2040
// (aarch64), 2048 (ppc, riscv).
//
// This carries over unchanged from the Zero port, where it was needed because
// Zero's recursive C++ interpreter consumed the register stack fast. It is
// still needed here for a different reason: generated code no longer touches
// the register stack at all (FRAME-DESIGN.md section 1), but the C++ runtime it
// calls into does, and that is what the reservation protects.
define_pd_global(intx,  ThreadStackSize,          4096);  // 2048 usable
define_pd_global(intx,  VMThreadStackSize,        2048);  // 1024 usable
define_pd_global(intx,  CompilerThreadStackSize,  4096);  // 2048 usable

define_pd_global(size_t, JVMInvokeMethodSlack,    8192);

// Used on 64 bit platforms for UseCompressedOops base address.
//
// Note this hint is not honoured on IA-64 Linux, which hands out high mmap
// addresses; milestone 1 accordingly runs with -XX:-UseCompressedOops
// -XX:-UseCompressedClassPointers (FRAME-DESIGN.md section 2.4). The value is
// kept at the conventional 2 GiB so that enabling compressed oops later is a
// one-flag change rather than a hunt for why the base is wrong.
define_pd_global(size_t, HeapBaseMinAddress,      2 * G);

#endif // OS_CPU_LINUX_IA64_GLOBALS_LINUX_IA64_HPP
