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

#ifndef CPU_IA64_GLOBALS_IA64_HPP
#define CPU_IA64_GLOBALS_IA64_HPP

#include "utilities/globalDefinitions.hpp"
#include "utilities/macros.hpp"

// Sets the default values for platform dependent flags used by the runtime system.
// (see globals.hpp)

define_pd_global(bool, ImplicitNullChecks,       true);  // Generate code for implicit null checks
define_pd_global(bool, TrapBasedNullChecks,      false);
define_pd_global(bool, UncommonNullCast,         true);  // Uncommon-trap nulls past to check cast

define_pd_global(bool, DelayCompilerStubsGeneration, COMPILER2_OR_JVMCI);

define_pd_global(uintx, CodeCacheSegmentSize,    64 COMPILER1_AND_COMPILER2_PRESENT(+64));
// Must be a multiple of the 16-byte bundle size. 64 gives four bundles, matching
// aarch64/riscv/s390.
define_pd_global(intx, CodeEntryAlignment,       64);
// One bundle.
define_pd_global(intx, OptoLoopAlignment,        16);

// ---------------------------------------------------------------------------
// Stack zones.
//
// **These are page counts, and this machine has 16 KiB pages** -- 4x the size
// every other port's numbers assume. Copying another port's counts would
// quadruple every zone in bytes. PORTING.md section 6 records what that class
// of mistake already cost once: at a 16 KiB page size the JDK's own process
// reaper thread (a 128 KiB stack) could no longer start, so child processes
// were never reaped and anything waiting on a subprocess hung forever.
//
// So these are derived from the *byte* budgets the 4 KiB ports settled on, not
// from their page counts:
//
//                       x86/aarch64/riscv/ppc      here
//   shadow    20 pages x 4 KiB =  80 KiB      6 x 16 KiB =  96 KiB
//   yellow     2 pages x 4 KiB =   8 KiB      1 x 16 KiB =  16 KiB
//   red        1 page  x 4 KiB =   4 KiB      1 x 16 KiB =  16 KiB
//   reserved   1 page  x 4 KiB =   4 KiB      1 x 16 KiB =  16 KiB
//
// Every zone is therefore at least as large in bytes as on the 4 KiB ports,
// while the guard zone (red + yellow + reserved = 48 KiB) stays small enough
// that os::set_minimum_stack_sizes() -- which on IA-64 doubles its result to
// cover the register backing store as well as the memory stack -- does not
// price small threads out of existence again.
//
// The shadow zone's floor is set by the deepest VM call chain reachable from a
// Java frame; the riscv comment names the concrete case, a 64 KiB stack buffer
// in Java_java_net_SocketOutputStream_socketWrite0. 96 KiB clears it.
// ---------------------------------------------------------------------------
#define DEFAULT_STACK_SHADOW_PAGES   (6 DEBUG_ONLY(+2))
#define DEFAULT_STACK_YELLOW_PAGES   (1)
#define DEFAULT_STACK_RED_PAGES      (1)
#define DEFAULT_STACK_RESERVED_PAGES (1)

#define MIN_STACK_SHADOW_PAGES   DEFAULT_STACK_SHADOW_PAGES
#define MIN_STACK_YELLOW_PAGES   DEFAULT_STACK_YELLOW_PAGES
#define MIN_STACK_RED_PAGES      DEFAULT_STACK_RED_PAGES
#define MIN_STACK_RESERVED_PAGES (0)

define_pd_global(intx, StackYellowPages,   DEFAULT_STACK_YELLOW_PAGES);
define_pd_global(intx, StackRedPages,      DEFAULT_STACK_RED_PAGES);
define_pd_global(intx, StackShadowPages,   DEFAULT_STACK_SHADOW_PAGES);
define_pd_global(intx, StackReservedPages, DEFAULT_STACK_RESERVED_PAGES);

// Continuations need frame walking and freeze/thaw support that this port does
// not have yet; enable once the template interpreter is stable.
define_pd_global(bool, VMContinuations, false);

define_pd_global(bool, RewriteBytecodes,     true);
define_pd_global(bool, RewriteFrequentPairs, true);

// There is no conventional frame pointer on IA-64 -- unwinding is table-driven
// -- and this port walks frames through the explicit link slot laid down by
// every prologue (FRAME-DESIGN.md section 4.2) rather than through a register.
define_pd_global(bool, PreserveFramePointer, false);

define_pd_global(uintx, TypeProfileLevel, 0);

define_pd_global(bool, CompactStrings, true);

// Clear short arrays bigger than one word in an arch-specific way
define_pd_global(intx, InitArrayShortSize, BytesPerLong);

define_pd_global(intx, InlineSmallCode, 1000);

#define ARCH_FLAGS(develop,                                                      \
                   product,                                                      \
                   range,                                                        \
                   constraint)                                                   \
                                                                                 \
  product(intx, CacheLineSize, DEFAULT_CACHE_LINE_SIZE,                          \
          "Size in bytes of a CPU cache line")                                   \
          range(wordSize, max_jint)                                              \
                                                                                 \
  product(bool, TraceTraps, false, DIAGNOSTIC,                                   \
          "Trace all traps the signal handler")

#endif // CPU_IA64_GLOBALS_IA64_HPP
