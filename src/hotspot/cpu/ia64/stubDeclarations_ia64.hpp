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

#ifndef CPU_IA64_STUBDECLARATIONS_HPP
#define CPU_IA64_STUBDECLARATIONS_HPP

// Blob size budgets and architecture-specific stub entries.
//
// cpu/riscv declares a long list of compiler-blob entries here (the long-string
// comparison helpers and friends). None of those exist for this port yet: the
// core variant has no compiler at all, and JIT-SCOPE.md phase 3 deliberately
// skips the optional crypto and vector intrinsics, which is most of the
// difference between ~2,000 lines of stubs and cpu/riscv's ~6,900.
//
// The blob sizes are generous rather than measured -- with one instruction per
// 16-byte bundle this port's generated code is roughly 4x the size of a
// conventional port's, so the usual figures would be too small. Revisit once
// stubGenerator_ia64.cpp actually emits something and the build reports real
// sizes.

#define STUBGEN_INITIAL_BLOBS_ARCH_DO(do_stub,                          \
                                      do_arch_blob,                     \
                                      do_arch_entry,                    \
                                      do_arch_entry_init)               \
  do_arch_blob(initial, 40000)                                          \


#define STUBGEN_CONTINUATION_BLOBS_ARCH_DO(do_stub,                     \
                                           do_arch_blob,                \
                                           do_arch_entry,               \
                                           do_arch_entry_init)          \
  do_arch_blob(continuation, 8000)                                      \


#define STUBGEN_COMPILER_BLOBS_ARCH_DO(do_stub,                         \
                                       do_arch_blob,                    \
                                       do_arch_entry,                   \
                                       do_arch_entry_init)              \
  do_arch_blob(compiler, 40000)                                         \


#define STUBGEN_FINAL_BLOBS_ARCH_DO(do_stub,                            \
                                    do_arch_blob,                       \
                                    do_arch_entry,                      \
                                    do_arch_entry_init)                 \
  do_arch_blob(final, 40000)                                            \

#endif // CPU_IA64_STUBDECLARATIONS_HPP
