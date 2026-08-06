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

#include "register_ia64.hpp"

Register::RegisterImpl           all_RegisterImpls     [Register::number_of_registers      + 1];
FloatRegister::FloatRegisterImpl all_FloatRegisterImpls[FloatRegister::number_of_registers + 1];

// Registers are named for the role they carry in this port, so that assembly
// dumps and hs_err register displays read as HotSpot rather than as raw
// hardware. The ABI-fixed registers keep their psABI names.
// See FRAME-DESIGN.md section 2.2 for the assignment and its rationale.
const char* Register::RegisterImpl::name() const {
  static const char *const names[number_of_registers] = {
    "zr",       "gp",       "t0",        "t1",
    "Rthread",  "Rbcp",     "Rlocals",   "Resp",
    "r8",       "t2",       "t3",        "t4",
    "sp",       "tp",       "Rmethod",   "Rcpool",
    "Rmonitors", "Rdispatch", "Rsender_sp", "r19",
    "r20",      "r21",      "r22",       "r23",
    "r24",      "r25",      "r26",       "r27",
    "r28",      "r29",      "r30",       "r31",
    "out0",     "out1",     "out2",      "out3",
    "out4",     "out5",     "out6",      "out7"
  };
  return is_valid() ? names[encoding()] : "noreg";
}

const char* FloatRegister::FloatRegisterImpl::name() const {
  static const char *const names[number_of_registers] = {
    "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",
    "f8",  "f9",  "f10", "f11", "f12", "f13", "f14", "f15",
    "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
    "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31"
  };
  return is_valid() ? names[encoding()] : "fnoreg";
}

const char* PredicateRegister::name() const {
  static const char *const names[number_of_usable_registers] = {
    "p0",  "p1",  "p2",  "p3",  "p4",  "p5",  "p6",  "p7",
    "p8",  "p9",  "p10", "p11", "p12", "p13", "p14", "p15"
  };
  // p16-p63 rotate and are never named by generated code, so they have no
  // entry here; report them rather than indexing off the end.
  return is_usable() ? names[encoding()] : (is_valid() ? "p<rotating>" : "pnoreg");
}

const char* BranchRegister::name() const {
  static const char *const names[number_of_registers] = {
    "b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7"
  };
  return is_valid() ? names[encoding()] : "bnoreg";
}
