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

#ifndef CPU_IA64_GLOBALDEFINITIONS_IA64_HPP
#define CPU_IA64_GLOBALDEFINITIONS_IA64_HPP

// The Itanium psABI requires sp to be 16-byte aligned at all times, not merely
// at call boundaries.
const int StackAlignmentInBytes = 16;

const size_t pd_segfault_address = 1024;

// The psABI promotes every integer argument narrower than a register to 64 bits
// (see CC_IA64 in the LLVM backend: CCIfType<[i1,i8,i16,i32], CCPromoteToType<i64>>).
// PPC64 and s390 are the other two ports in this position.
const bool CCallingConventionRequiresIntsAsLongs = true;

// CPU_MULTI_COPY_ATOMIC is deliberately NOT defined.
//
// Leaving it undefined is the conservative choice: shared code then emits the
// extra fences needed on a machine where two observers may disagree about the
// order of independent writes (the IRIW pattern). Defining it wrongly produces
// failures that are intermittent, unreproducible and essentially undebuggable.
//
// IA-64 is among the weakest-ordered architectures the JVM has ever targeted,
// and this port's whole reason for existing is to find ordering bugs that x86's
// TSO hides -- so it should not start by asserting a strong property it has not
// verified. Revisit only with evidence from the Itanium SDM's memory-ordering
// chapter, not from analogy with another port.
// [OPEN] FRAME-DESIGN.md section 9.

// IA-64 has cmpxchg8 / cmpxchg8.acq / .rel, so 64-bit compare-and-swap is a
// single instruction and jlong accesses are naturally atomic.
#define SUPPORTS_NATIVE_CX8

// Code patching is not atomic with respect to concurrent execution here: an
// instruction lives in a 16-byte bundle, the I-cache is not coherent with
// stores, and a patch may have to rewrite more than one bundle. Deoptimize
// rather than patch in place.
#define DEOPTIMIZE_WHEN_PATCHING

#define SUPPORT_MONITOR_COUNT

#define SUPPORT_RESERVED_STACK_AREA

// register_ia64.hpp uses the all_RegisterImpls array form (as cpu/riscv does).
#define USE_POINTERS_TO_REGISTER_IMPL_ARRAY

// No CODE_CACHE_SIZE_LIMIT. IP-relative branches reach only +/-16 MiB (a signed
// 21-bit displacement scaled by the 16-byte bundle), but every far call and
// jump goes through a 64-bit movl into a branch register, so nothing in the
// port constrains where in the address space the code cache may live.

// Measured on rx2800 (Itanium 9340, Tukwila) via
// /sys/devices/system/cpu/cpu0/cache/index*/coherency_line_size:
//
//   L1 data        16K   line  64
//   L1 instruction 16K   line  64
//   L2 data       256K   line 128
//   L2 instruction 512K  line 128
//   L3 unified   5120K   line 128
//
// False sharing is governed by the largest coherency line, so 128 -- matching
// PPC64, and twice what the 64-byte ports use.
#define DEFAULT_CACHE_LINE_SIZE 128

// The default padding size for data structures to avoid false sharing.
#define DEFAULT_PADDING_SIZE DEFAULT_CACHE_LINE_SIZE

#endif // CPU_IA64_GLOBALDEFINITIONS_IA64_HPP
