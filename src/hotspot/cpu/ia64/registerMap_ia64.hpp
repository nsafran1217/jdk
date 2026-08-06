/*
 * Copyright (c) 1998, 2023, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2020, 2021, Huawei Technologies Co., Ltd. All rights reserved.
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

#ifndef CPU_IA64_REGISTERMAP_IA64_HPP
#define CPU_IA64_REGISTERMAP_IA64_HPP

// Ported from cpu/riscv unchanged apart from renaming.
//
// That is safe here, and checked rather than assumed: these files contain no
// register references at all (verified by grep before porting) -- they are
// pure frame-layout arithmetic, and frame_ia64.hpp deliberately uses the same
// slot numbering as frame_riscv.hpp so that exactly this kind of shared
// reasoning transfers. If the frame layout is ever changed away from riscv's,
// every file carrying this notice must be re-derived, not re-renamed.
//
// Continuations are disabled for now (VMContinuations is false in
// globals_ia64.hpp); this code exists so the tree compiles and so that
// enabling them later is a matter of testing rather than of writing.

// machine-dependent implementation for register maps
  friend class frame;

 private:
  // This is the hook for finding a register in an "well-known" location,
  // such as a register block of a predetermined format.
  address pd_location(VMReg reg) const { return nullptr; }
  address pd_location(VMReg base_reg, int slot_idx) const;

  // no PD state to clear or copy:
  void pd_clear() {}
  void pd_initialize() {}
  void pd_initialize_from(const RegisterMap* map) {}

#endif // CPU_IA64_REGISTERMAP_IA64_HPP
