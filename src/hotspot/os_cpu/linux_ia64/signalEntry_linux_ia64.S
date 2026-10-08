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

// Signal-handler entries for Linux/IA-64.
//
// Linux/IA-64 does not save the preserved registers r4-r7 in the signal
// context: sc_gr[4..7] hold whatever was there (measured on rx2800,
// tools/gate/sigctxprobe.c). The kernel can skip them because a C handler
// preserves r4-r7 under the psABI, so they are intact again at sigreturn.
// But HotSpot reads the interrupted frame out of the context, and the
// template interpreter keeps fp, Rthread, Rbcp and Resp in exactly r4-r7.
//
// So the VM's handlers are entered through these stubs. Each runs before any
// C code can have touched r4-r7, copies them into uc_mcontext.sc_gr[4..7],
// and then branches -- not calls -- to the real handler, which therefore
// returns straight to the kernel's signal trampoline.
//
// On entry: r32 = signal number, r33 = siginfo_t*, r34 = ucontext_t* (the
// trampoline's outputs; no alloc here, so they stay in place for the real
// handler), and gp = libjvm's gp, since the trampoline called us through our
// own function descriptor. The real handler is reached through the function
// pointer in the matching *_target variable, set by signals_posix.cpp; a
// function pointer is a descriptor {entry, gp}.
//
// SC_GR4 is offsetof(ucontext_t, uc_mcontext.sc_gr[4]); os_linux_ia64.cpp
// asserts the value.

#define SC_GR4 0xe8

#define SIGNAL_ENTRY(name, target)                                      \
        .text                                                         ; \
        .align 32                                                     ; \
        .globl  name                                                  ; \
        .hidden name                                                  ; \
        .type   name, @function                                       ; \
        .proc   name                                                  ; \
name:                                                                 ; \
        { .mmi                                                        ; \
          adds r2 = SC_GR4, r34                                       ; \
          addl r3 = @ltoff(target), gp                                ; \
          nop.i 0 ;;                                                  ; \
        }                                                             ; \
        { .mmi                                                        ; \
          st8 [r2] = r4, 8                                            ; \
          ld8 r3 = [r3]                  /* &target */                ; \
          nop.i 0 ;;                                                  ; \
        }                                                             ; \
        { .mmi                                                        ; \
          st8 [r2] = r5, 8                                            ; \
          ld8 r3 = [r3]                  /* the descriptor */         ; \
          nop.i 0 ;;                                                  ; \
        }                                                             ; \
        { .mmi                                                        ; \
          st8 [r2] = r6, 8                                            ; \
          ld8 r14 = [r3], 8              /* entry */                  ; \
          nop.i 0 ;;                                                  ; \
        }                                                             ; \
        { .mmi                                                        ; \
          st8 [r2] = r7                                               ; \
          ld8 gp = [r3]                  /* the handler's gp */       ; \
          mov b6 = r14 ;;                                             ; \
        }                                                             ; \
        { .mib                                                        ; \
          nop.m 0                                                     ; \
          nop.i 0                                                     ; \
          br.cond.sptk.many b6 ;;                                     ; \
        }                                                             ; \
        .endp   name

SIGNAL_ENTRY(ia64_javaSignalHandler_entry, ia64_javaSignalHandler_target)
SIGNAL_ENTRY(ia64_SR_handler_entry, ia64_SR_handler_target)

        .section .note.GNU-stack,"",@progbits
