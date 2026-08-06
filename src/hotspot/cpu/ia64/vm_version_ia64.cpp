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

#include "asm/assembler.inline.hpp"
#include "runtime/globals_extension.hpp"
#include "runtime/java.hpp"
#include "runtime/os.hpp"
#include "runtime/vm_version.hpp"

void VM_Version::initialize() {
  // IA-64 traps on unaligned access rather than fixing it up in hardware. The
  // kernel can emulate the trap, but at a cost that makes it worse than the
  // split accesses HotSpot emits when it knows alignment is not guaranteed.
  if (!FLAG_IS_DEFAULT(UseUnalignedAccesses)) {
    warning("Unaligned memory access is not available on this CPU");
  }
  FLAG_SET_DEFAULT(UseUnalignedAccesses, false);

  // Every intrinsic below needs a hand-written stub in stubGenerator_ia64.cpp.
  // JIT-SCOPE.md phase 3 deliberately skips all of them: of cpu/riscv's 62 stub
  // generators, 13 are optional crypto/vector intrinsics, and dropping them is
  // most of the difference between ~2,000 lines of stubs and ~6,900.
  //
  // They are switched off here rather than left to fail at generation time so
  // that an explicitly requested intrinsic produces a clear warning instead of
  // a crash inside a stub that was never written.
  if (UseAES) {
    warning("AES instructions are not available on this CPU");
    FLAG_SET_DEFAULT(UseAES, false);
  }
  if (UseAESIntrinsics) {
    warning("AES intrinsics are not available on this CPU");
    FLAG_SET_DEFAULT(UseAESIntrinsics, false);
  }
  if (UseAESCTRIntrinsics) {
    warning("AES/CTR intrinsics are not available on this CPU");
    FLAG_SET_DEFAULT(UseAESCTRIntrinsics, false);
  }
  if (UseMD5Intrinsics) {
    warning("MD5 intrinsics are not available on this CPU");
    FLAG_SET_DEFAULT(UseMD5Intrinsics, false);
  }
  if (UseSHA) {
    warning("SHA instructions are not available on this CPU");
    FLAG_SET_DEFAULT(UseSHA, false);
  }
  if (UseSHA1Intrinsics) {
    warning("Intrinsics for SHA-1 crypto hash functions not available on this CPU.");
    FLAG_SET_DEFAULT(UseSHA1Intrinsics, false);
  }
  if (UseSHA256Intrinsics) {
    warning("Intrinsics for SHA-224 and SHA-256 crypto hash functions not available on this CPU.");
    FLAG_SET_DEFAULT(UseSHA256Intrinsics, false);
  }
  if (UseSHA512Intrinsics) {
    warning("Intrinsics for SHA-384 and SHA-512 crypto hash functions not available on this CPU.");
    FLAG_SET_DEFAULT(UseSHA512Intrinsics, false);
  }
  if (UseSHA3Intrinsics) {
    warning("Intrinsics for SHA3-224, SHA3-256, SHA3-384 and SHA3-512 crypto hash functions not available on this CPU.");
    FLAG_SET_DEFAULT(UseSHA3Intrinsics, false);
  }
  if (UseCRC32Intrinsics) {
    warning("CRC32 intrinsics are not available on this CPU");
    FLAG_SET_DEFAULT(UseCRC32Intrinsics, false);
  }
  if (UseAdler32Intrinsics) {
    warning("Adler32 intrinsics are not available on this CPU");
    FLAG_SET_DEFAULT(UseAdler32Intrinsics, false);
  }
  if (UseVectorizedMismatchIntrinsic) {
    warning("vectorizedMismatch intrinsic is not available on this CPU.");
    FLAG_SET_DEFAULT(UseVectorizedMismatchIntrinsic, false);
  }

  // UseFMA is deliberately left alone. IA-64's only FP arithmetic instruction
  // *is* the fused multiply-add -- plain add and multiply are fma with f1 (1.0)
  // and f0 (0.0) as identity operands -- so the hardware support is total. It
  // simply has no effect until there is a compiler to emit the intrinsic.

  // Prefetching (lfetch) exists but no stub uses it yet; see
  // prefetch_linux_ia64.inline.hpp.
  FLAG_SET_DEFAULT(AllocatePrefetchDistance, 0);
}

void VM_Version::initialize_cpu_information(void) {
  // do nothing if cpu info has been initialized
  if (_initialized) {
    return;
  }

  _no_of_cores   = os::processor_count();
  _no_of_threads = _no_of_cores;
  _no_of_sockets = _no_of_cores;
  snprintf(_cpu_name, CPU_TYPE_DESC_BUF_SIZE - 1, "IA-64 (Itanium)");
  snprintf(_cpu_desc, CPU_DETAILED_DESC_BUF_SIZE, "%s", _cpu_info_string);
  _initialized = true;
}
