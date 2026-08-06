/*
 * Copyright (c) 2008, 2025, Oracle and/or its affiliates. All rights reserved.
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

#ifndef CPU_IA64_DISASSEMBLER_IA64_HPP
#define CPU_IA64_DISASSEMBLER_IA64_HPP

// Instructions live in 16-byte bundles and are never split across one, so a
// disassembly listing is only meaningful at bundle boundaries. Reporting 16
// here (rather than 1, as the variable-length-instruction ports do) keeps
// hsdis from trying to decode from the middle of a bundle, which would produce
// convincing nonsense rather than an obvious failure.
static int pd_instruction_alignment() {
  return 16;
}

static const char* pd_cpu_opts() {
  return "";
}

// Special-case instruction decoding. There may be cases where the binutils
// disassembler doesn't do a perfect job; decode_instruction0 can kick in and
// do it right. If nothing had to be done, return "here", otherwise return
// "here + instr_len(here)".
static address decode_instruction0(address here, outputStream* st, address virtual_begin = nullptr) {
  return here;
}

// Platform-specific instruction annotations (like the value of loaded
// constants). A useful thing to add later: this port materialises every
// absolute address with movl, and ia64::ReadMovlImm can recover the immediate,
// so an annotation could name the oop or runtime routine a movl is loading.
static void annotate(address pc, outputStream* st) {}

#endif // CPU_IA64_DISASSEMBLER_IA64_HPP
