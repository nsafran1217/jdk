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

#ifndef CPU_IA64_C2_GLOBALS_IA64_HPP
#define CPU_IA64_C2_GLOBALS_IA64_HPP

#include "utilities/globalDefinitions.hpp"
#include "utilities/macros.hpp"

// Sets the default values for platform dependent flags used by the server compiler.
// (see c2_globals.hpp).
//
// Starting values from C2-SCOPE.md ("Starting flag defaults"), informed by
// what JDK 6u45's IA-64 server VM shipped (JDK6-IA64.md section 6). The code
// cache is sized as for C1 (c1_globals_ia64.hpp): IA-64 code is several times
// larger per bytecode than RISC-V's.

define_pd_global(bool, BackgroundCompilation,        true);
define_pd_global(bool, CICompileOSR,                 true);
define_pd_global(bool, InlineIntrinsics,             true);
define_pd_global(bool, PreferInterpreterNativeStubs, false);
define_pd_global(bool, ProfileTraps,                 true);
define_pd_global(bool, UseOnStackReplacement,        true);
define_pd_global(bool, ProfileInterpreter,           true);
define_pd_global(bool, TieredCompilation,            COMPILER1_PRESENT(true) NOT_COMPILER1(false));
define_pd_global(intx, CompileThreshold,             10000);

define_pd_global(intx, OnStackReplacePercentage,     140);
// Predicated moves are cheap (JDK 6: 4).
define_pd_global(intx, ConditionalMoveLimit,         4);
// JDK 6 shipped 100 here, against x86's 325; start with riscv's value and tune
// once C2 runs (C2-4).
define_pd_global(intx, FreqInlineSize,               325);
define_pd_global(intx, MinJumpTableSize,             10);
// A bundle.
define_pd_global(intx, InteriorEntryAlignment,       16);
define_pd_global(intx, NewSizeThreadIncrease,        ScaleForWordSize(4*K));
define_pd_global(intx, LoopUnrollLimit,              60);
define_pd_global(intx, LoopPercentProfileLimit,      10);
define_pd_global(intx, InitialCodeCacheSize,         2496*K); // Integral multiple of CodeCacheExpansionSize
define_pd_global(intx, CodeCacheExpansionSize,       64*K);

// Ergonomics related flags
define_pd_global(uint64_t, MaxRAM,                   128ULL*G);
define_pd_global(intx, RegisterCostAreaRatio,        16000);

// Peephole and CISC spilling both break the graph, and so make the
// scheduler sick.
define_pd_global(bool, OptoPeephole,                 false);
define_pd_global(bool, UseCISCSpill,                 false);
// No pipeline model yet; JDK 6 scheduled (C2-DESIGN.md section 11, C2-4).
define_pd_global(bool, OptoScheduling,               false);
define_pd_global(bool, OptoBundling,                 false);
define_pd_global(bool, OptoRegScheduling,            false);
// No vector unit is modelled (C2-DESIGN.md section 13).
define_pd_global(bool, SuperWordLoopUnrollAnalysis,  false);
define_pd_global(uint, SuperWordStoreToLoadForwardingFailureDetection, 16);
define_pd_global(bool, IdealizeClearArrayNode,       true);

// Tiered ergonomics multiply this by five, up to 240M (compilerDefinitions.cpp);
// with -XX:-TieredCompilation it stays 128M, JDK 6's value.
define_pd_global(intx, ReservedCodeCacheSize,        128*M);
define_pd_global(intx, NonProfiledCodeHeapSize,      48*M);
define_pd_global(intx, ProfiledCodeHeapSize,         48*M);
define_pd_global(intx, NonNMethodCodeHeapSize,       32*M);
define_pd_global(uintx, CodeCacheMinBlockLength,     6);
define_pd_global(uintx, CodeCacheMinimumUseSpace,    1600*K);

// Ergonomics related flags
define_pd_global(bool, NeverActAsServerClassMachine, false);

define_pd_global(bool, TrapBasedRangeChecks,         false); // Not needed.

#endif // CPU_IA64_C2_GLOBALS_IA64_HPP
