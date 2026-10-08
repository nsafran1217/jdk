//
// Copyright (c) 2026, IA-64 port contributors. All rights reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License version 2 only, as
// published by the Free Software Foundation.
//
// This code is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// version 2 for more details (a copy is included in the LICENSE file that
// accompanied this code).
//
// You should have received a copy of the GNU General Public License version
// 2 along with this work; if not, write to the Free Software Foundation,
// Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
//
// Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
// or visit www.oracle.com if you need additional information or have any
// questions.
//

// SafeFetch32 / SafeFetchN for Linux/IA-64.
//
// The protocol is the static-assembly one (safefetch_static_posix.cpp): if
// the load at _SafeFetchXX_fault faults, the signal handler compares the
// faulting pc against that label and resumes at _SafeFetchXX_continuation,
// which returns the default value instead.
//
// Two IA-64 details make that comparison work:
//
// * The kernel reports the pc as bundle address + slot number. The load is
//   therefore placed in slot 0 of its bundle, so the reported pc equals the
//   label exactly; the continuation is a bundle start too, so resuming there
//   resumes at slot 0.
//
// * These are leaf procedures entered from C++ by br.call without an alloc of
//   their own: the arguments are in the caller's output registers, which are
//   in0/in1 = r32/r33 here, and br.ret restores the caller's frame from the
//   ar.pfs that br.call set. Only the function symbols are typed @function
//   (so C++ calls them through linker-built descriptors); the fault and
//   continuation labels are plain code addresses, as C++ declares them
//   char[].
//
// Explicit bundles throughout, so the assembler cannot move the load out of
// slot 0.

        .text

// int SafeFetch32_impl(int* adr, int errValue)
//   r32: adr, r33: errValue, r8: result
        .align 32
        .globl  SafeFetch32_impl
        .hidden SafeFetch32_impl
        .type   SafeFetch32_impl, @function
        .proc   SafeFetch32_impl
SafeFetch32_impl:
        .globl  _SafeFetch32_fault
        .hidden _SafeFetch32_fault
_SafeFetch32_fault:
        { .mii
          ld4 r8 = [r32]
          nop.i 0
          nop.i 0 ;;
        }
        { .mib
          nop.m 0
          sxt4 r8 = r8
          br.ret.sptk.many b0 ;;
        }
        .globl  _SafeFetch32_continuation
        .hidden _SafeFetch32_continuation
_SafeFetch32_continuation:
        { .mib
          mov r8 = r33
          nop.i 0
          br.ret.sptk.many b0 ;;
        }
        .endp   SafeFetch32_impl

// intptr_t SafeFetchN_impl(intptr_t* adr, intptr_t errValue)
//   r32: adr, r33: errValue, r8: result
        .align 32
        .globl  SafeFetchN_impl
        .hidden SafeFetchN_impl
        .type   SafeFetchN_impl, @function
        .proc   SafeFetchN_impl
SafeFetchN_impl:
        .globl  _SafeFetchN_fault
        .hidden _SafeFetchN_fault
_SafeFetchN_fault:
        { .mib
          ld8 r8 = [r32]
          nop.i 0
          br.ret.sptk.many b0 ;;
        }
        .globl  _SafeFetchN_continuation
        .hidden _SafeFetchN_continuation
_SafeFetchN_continuation:
        { .mib
          mov r8 = r33
          nop.i 0
          br.ret.sptk.many b0 ;;
        }
        .endp   SafeFetchN_impl

        .section .note.GNU-stack,"",@progbits
