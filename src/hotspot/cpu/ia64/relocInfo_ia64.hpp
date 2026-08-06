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

#ifndef CPU_IA64_RELOCINFO_IA64_HPP
#define CPU_IA64_RELOCINFO_IA64_HPP

  // machine-dependent parts of class relocInfo
 private:
  enum {
    // Byte-aligned, not bundle-aligned.
    //
    // It is tempting to use 16 here the way cpu/ppc uses 4 for its whole-word
    // instructions -- with one instruction per bundle (FRAME-DESIGN.md and the
    // assembler's [SMDOC]) every *code* relocation site really is 16-byte
    // aligned. But relocInfo offsets also address the constant and stub
    // sections, where oop and metadata slots are only 8-byte aligned, so
    // anything coarser than a byte would be unable to name them.
    offset_unit        =  1,

    // Must be at least 1 for RelocInfo::narrow_oop_in_const.
    // Must be at least 2 for ZGC GC barrier patching.
    //
    // If bundle packing is ever implemented (JIT-SCOPE.md phase 7), a
    // relocation will additionally have to say *which* of a bundle's three
    // slots it applies to. Two more format bits would carry that; the field is
    // deliberately left at the common width until there is something to encode.
    format_width       =  2
  };

 public:

  // This platform has no oops in the code that are not also
  // listed in the oop section.
  static bool mustIterateImmediateOopsInCode() { return false; }

#endif // CPU_IA64_RELOCINFO_IA64_HPP
