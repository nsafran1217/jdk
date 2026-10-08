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
#include "oops/compressedKlass.hpp"
#include "utilities/globalDefinitions.hpp"

// Any encoding base costs IA-64 exactly one movl to materialise -- there is
// no cheaper "small immediate" class of base as riscv has -- so the only
// placement worth asking for is one that needs no base at all: below 4 GB,
// for an unscaled encoding with base 0 and shift 0. Failing that, the caller
// reserves anywhere.
//
// [UNVERIFIED] Whether a low mapping is obtainable at all on IA-64 Linux is
// open: CLAUDE.md records that the HeapBaseMinAddress hint is not honoured
// there.
char* CompressedKlassPointers::reserve_address_space_for_compressed_classes(size_t size, bool aslr, bool optimize_for_zero_base) {
  return reserve_address_space_for_unscaled_encoding(size, aslr);
}
