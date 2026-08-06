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

#ifndef CPU_IA64_BYTES_IA64_HPP
#define CPU_IA64_BYTES_IA64_HPP

#include "memory/allStatic.hpp"

// IA-64 *traps* on unaligned access -- there is no hardware fixup, and the
// kernel's emulation of the trap is slow enough that it is not a viable
// fallback. These accessors are used on class-file and constant-pool data with
// no alignment guarantee at all, so they must never issue a wide load against a
// possibly-unaligned address.
//
// The packed union below is how the Zero port already solves this, and it is
// the reason the java/nio/Buffer tests pass on rx2800 today (see PORTING.md):
// GCC sees the packed attribute, knows the target cannot fault-free load
// unaligned, and emits the byte-wise sequence itself. Writing the shifts by
// hand as the big-endian path below does would work too, but this way the
// compiler is free to use a wide load whenever it can prove the address is
// aligned.
typedef union unaligned {
  u4 u;
  u2 us;
  u8 ul;
} __attribute__((packed)) unaligned;

class Bytes: AllStatic {
 public:
  // Efficient reading and writing of unaligned unsigned data in
  // platform-specific byte ordering.
  static inline u2 get_native_u2(address p) {
    unaligned *up = (unaligned *) p;
    return up->us;
  }

  static inline u4 get_native_u4(address p) {
    unaligned *up = (unaligned *) p;
    return up->u;
  }

  static inline u8 get_native_u8(address p) {
    unaligned *up = (unaligned *) p;
    return up->ul;
  }

  static inline void put_native_u2(address p, u2 x) {
    unaligned *up = (unaligned *) p;
    up->us = x;
  }

  static inline void put_native_u4(address p, u4 x) {
    unaligned *up = (unaligned *) p;
    up->u = x;
  }

  static inline void put_native_u8(address p, u8 x) {
    unaligned *up = (unaligned *) p;
    up->ul = x;
  }

  // Efficient reading and writing of unaligned unsigned data in Java byte
  // ordering (i.e. big-endian ordering).
  //
  // IA-64 is bi-endian, but Linux runs it little-endian (make/autoconf/platform.m4
  // maps ia64 to 64-bit little-endian), so VM_LITTLE_ENDIAN is always set here
  // and the reversal path below is the live one. The #else is kept so this file
  // stays a faithful sibling of the other ports' versions.
#ifdef VM_LITTLE_ENDIAN
  // Byte-order reversal is needed
  static inline u2 get_Java_u2(address p) {
    return (u2(p[0]) << 8) |
           (u2(p[1])     );
  }
  static inline u4 get_Java_u4(address p) {
    return (u4(p[0]) << 24) |
           (u4(p[1]) << 16) |
           (u4(p[2]) <<  8) |
           (u4(p[3])      );
  }
  static inline u8 get_Java_u8(address p) {
    u4 hi, lo;
    hi = (u4(p[0]) << 24) |
         (u4(p[1]) << 16) |
         (u4(p[2]) <<  8) |
         (u4(p[3])      );
    lo = (u4(p[4]) << 24) |
         (u4(p[5]) << 16) |
         (u4(p[6]) <<  8) |
         (u4(p[7])      );
    return u8(lo) | (u8(hi) << 32);
  }

  static inline void put_Java_u2(address p, u2 x) {
    p[0] = x >> 8;
    p[1] = x;
  }
  static inline void put_Java_u4(address p, u4 x) {
    p[0] = x >> 24;
    p[1] = x >> 16;
    p[2] = x >> 8;
    p[3] = x;
  }
  static inline void put_Java_u8(address p, u8 x) {
    u4 hi, lo;
    lo = x;
    hi = x >> 32;
    p[0] = hi >> 24;
    p[1] = hi >> 16;
    p[2] = hi >> 8;
    p[3] = hi;
    p[4] = lo >> 24;
    p[5] = lo >> 16;
    p[6] = lo >> 8;
    p[7] = lo;
  }
#else
  // No byte-order reversal is needed
  static inline u2 get_Java_u2(address p) { return get_native_u2(p); }
  static inline u4 get_Java_u4(address p) { return get_native_u4(p); }
  static inline u8 get_Java_u8(address p) { return get_native_u8(p); }

  static inline void put_Java_u2(address p, u2 x) { put_native_u2(p, x); }
  static inline void put_Java_u4(address p, u4 x) { put_native_u4(p, x); }
  static inline void put_Java_u8(address p, u8 x) { put_native_u8(p, x); }
#endif // VM_LITTLE_ENDIAN
};

#endif // CPU_IA64_BYTES_IA64_HPP
