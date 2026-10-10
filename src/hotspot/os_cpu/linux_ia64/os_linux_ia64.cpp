/*
 * Copyright (c) 1999, 2025, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2020, 2022, Huawei Technologies Co., Ltd. All rights reserved.
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

#include "asm/macroAssembler.hpp"
#include "classfile/vmSymbols.hpp"
#include "code/codeCache.hpp"
#include "code/nativeInst.hpp"
#include "code/vtableStubs.hpp"
#include "interpreter/interpreter.hpp"
#include "jvm.h"
#include "memory/allocation.inline.hpp"
#include "os_linux.hpp"
#include "os_posix.hpp"
#include "prims/jniFastGetField.hpp"
#include "prims/jvm_misc.hpp"
#include "runtime/arguments.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/java.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/javaThread.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/osThread.hpp"
#include "runtime/safepointMechanism.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/timer.hpp"
#include "signals_posix.hpp"
#include "utilities/debug.hpp"
#include "utilities/events.hpp"
#include "utilities/vmError.hpp"

// put OS-includes here
# include <dlfcn.h>
# include <errno.h>
# include <pthread.h>
# include <signal.h>
# include <stdio.h>
# include <stdlib.h>
# include <sys/mman.h>
# include <sys/resource.h>
# include <sys/types.h>
# include <ucontext.h>
# include <unistd.h>

// Derived from os_linux_riscv.cpp, with the IA-64 ucontext accessors that
// the Zero port already established (os_linux_zero.cpp).
//
// The signal context is the kernel's struct sigcontext. Three properties
// matter here:
//
// * sc_gr[] holds only the static registers r0-r31; the stacked registers
//   live in the register backing store. That is enough, because everything
//   HotSpot needs from generated code is static: fp = r4, Rbcp = r6,
//   sp = r12 (FRAME-DESIGN.md 2.2). But the kernel does not save the
//   preserved r4-r7 at all (a C handler preserves them anyway); the VM's
//   handlers are entered through signalEntry_linux_ia64.S, which fills them
//   in. A context from any other handler has garbage in sc_gr[4..7].
// * sc_ip carries the slot number of the interrupted instruction in its low
//   two bits (the kernel stores cr_iip + psr.ri, arch/ia64/kernel/signal.c).
//   A faulting M-unit load -- slot 0 in this port's one-instruction bundles
//   -- reports the bundle address exactly; a break.b, in slot 2, reports
//   bundle + 2. Writing sc_ip back sets the slot from the same bits, so a
//   bundle-aligned continuation resumes at slot 0.
// * C frames keep no fp chain (unwinding is table-driven), so a C frame
//   cannot be walked by link() the way riscv walks it.

// sc_gr[4..7] are only meaningful because signalEntry_linux_ia64.S stores
// r4-r7 there before the VM's handlers run; the kernel leaves them out of the
// signal context. That file hard-codes this offset.
STATIC_ASSERT(offsetof(ucontext_t, uc_mcontext.sc_gr[4]) == 0xe8);

#define IA64_REG_FP   4   // fp, HotSpot's frame linkage
#define IA64_REG_BCP  6   // Rbcp
#define IA64_REG_SP   12

NOINLINE address os::current_stack_pointer() {
  address sp_value;
  __asm__ volatile ("mov %0 = sp" : "=r" (sp_value));
  return sp_value;
}

char* os::non_memory_address_word() {
  // Must never look like an address returned by reserve_memory,
  // even in its subfields (as defined by the CPU immediate fields,
  // if the CPU splits constants across multiple instructions).
  // IA-64 user addresses have region bits 63:61 of 0-4; region 7 is never
  // handed out to user space.
  return (char*) -1;
}

address os::Posix::ucontext_get_pc(const ucontext_t * uc) {
  return (address)uc->uc_mcontext.sc_ip;
}

void os::Posix::ucontext_set_pc(ucontext_t * uc, address pc) {
  uc->uc_mcontext.sc_ip = (unsigned long)pc;
}

intptr_t* os::Linux::ucontext_get_sp(const ucontext_t * uc) {
  return (intptr_t*)uc->uc_mcontext.sc_gr[IA64_REG_SP];
}

intptr_t* os::Linux::ucontext_get_fp(const ucontext_t * uc) {
  return (intptr_t*)uc->uc_mcontext.sc_gr[IA64_REG_FP];
}

address os::fetch_frame_from_context(const void* ucVoid,
                                     intptr_t** ret_sp, intptr_t** ret_fp) {
  address epc;
  const ucontext_t* uc = (const ucontext_t*)ucVoid;

  if (uc != nullptr) {
    epc = os::Posix::ucontext_get_pc(uc);
    if (ret_sp != nullptr) {
      *ret_sp = os::Linux::ucontext_get_sp(uc);
    }
    if (ret_fp != nullptr) {
      *ret_fp = os::Linux::ucontext_get_fp(uc);
    }
  } else {
    epc = nullptr;
    if (ret_sp != nullptr) {
      *ret_sp = (intptr_t *)nullptr;
    }
    if (ret_fp != nullptr) {
      *ret_fp = (intptr_t *)nullptr;
    }
  }

  return epc;
}

frame os::fetch_compiled_frame_from_context(const void* ucVoid) {
  const ucontext_t* uc = (const ucontext_t*)ucVoid;
  // In compiled code, the stack banging is performed before b0 has been
  // saved in the frame. b0 is live, and sp and fp belong to the caller.
  intptr_t* frame_fp = os::Linux::ucontext_get_fp(uc);
  intptr_t* frame_sp = os::Linux::ucontext_get_sp(uc);
  address frame_pc = (address)(uc->uc_mcontext.sc_br[0]
                         - NativeInstruction::instruction_size);
  return frame(frame_sp, frame_fp, frame_pc);
}

frame os::fetch_frame_from_context(const void* ucVoid) {
  intptr_t* frame_sp = nullptr;
  intptr_t* frame_fp = nullptr;
  address epc = fetch_frame_from_context(ucVoid, &frame_sp, &frame_fp);
  if (!is_readable_pointer(epc)) {
    // Try to recover from calling into bad memory
    // Assume new frame has not been set up, the same as
    // compiled frame stack bang
    return fetch_compiled_frame_from_context(ucVoid);
  }
  return frame(frame_sp, frame_fp, epc);
}

intptr_t* os::fetch_bcp_from_context(const void* ucVoid) {
  assert(ucVoid != nullptr, "invariant");
  const ucontext_t* uc = (const ucontext_t*)ucVoid;
  assert(os::Posix::ucontext_is_interpreter(uc), "invariant");
  return reinterpret_cast<intptr_t*>(uc->uc_mcontext.sc_gr[IA64_REG_BCP]);
}

// C frames cannot be walked through a frame-pointer chain on IA-64 (see the
// note at the top). Returning an empty frame ends the walk cleanly; error
// reporting then prints what it has. A table-driven walk through GCC's
// unwinder (_Unwind_Backtrace) is the eventual answer.
frame os::get_sender_for_C_frame(frame* fr) {
  return frame();
}

NOINLINE frame os::current_frame() {
  // As on Zero: the callers are VMError's stack printing steps, which use
  // the sp (to report free stack) and stop at the null pc.
  frame dummy = frame();
  dummy.set_sp((intptr_t*) current_stack_pointer());
  return dummy;
}

// Utility functions
bool PosixSignals::pd_hotspot_signal_handler(int sig, siginfo_t* info,
                                             ucontext_t* uc, JavaThread* thread) {

  // decide if this trap can be handled by a stub
  address stub = nullptr;

  address pc = nullptr;

  //%note os_trap_1
  if (info != nullptr && uc != nullptr && thread != nullptr) {
    pc = (address) os::Posix::ucontext_get_pc(uc);

    address addr = (address) info->si_addr;

    // Handle ALL stack overflow variations here
    if (sig == SIGSEGV) {
      // check if fault address is within thread stack
      if (thread->is_in_full_stack(addr)) {
        if (os::Posix::handle_stack_overflow(thread, addr, pc, uc, &stub)) {
          return true; // continue
        }
      }
    }

    if (thread->thread_state() == _thread_in_Java) {
      // Java thread running in Java code => find exception handler if any
      // a fault inside compiled code, the interpreter, or a stub

      if (sig == SIGSEGV && SafepointMechanism::is_poll_address((address)info->si_addr)) {
        stub = SharedRuntime::get_poll_stub(pc);
      } else if (sig == SIGBUS /* && info->si_code == BUS_OBJERR */) {
        // BugId 4454115: A read from a MappedByteBuffer can fault
        // here if the underlying file has been truncated.
        // Do not crash the VM in such a case.
        CodeBlob* cb = CodeCache::find_blob(pc);
        nmethod* nm = (cb != nullptr) ? cb->as_nmethod_or_null() : nullptr;
        bool is_unsafe_memory_access = (thread->doing_unsafe_access() && UnsafeMemoryAccess::contains_pc(pc));
        if ((nm != nullptr && nm->has_unsafe_access()) || is_unsafe_memory_access) {
          address next_pc = Assembler::locate_next_instruction(pc);
          if (is_unsafe_memory_access) {
            next_pc = UnsafeMemoryAccess::page_error_continue_pc(pc);
          }
          stub = SharedRuntime::handle_unsafe_access(thread, next_pc);
        }
      } else if (sig == SIGILL && MacroAssembler::is_stop(pc)) {
        // A MacroAssembler::stop(): the message pointer follows the bundle.
        const char* detail_msg = MacroAssembler::stop_message(pc);
        const char* msg = "stop";
        if (TraceTraps) {
          tty->print_cr("trap: %s: (SIGILL)", msg);
        }

        // End life with a fatal error, message and detail message and the context.
        // Note: no need to do any post-processing here (e.g. signal chaining)
        VMError::report_and_die(thread, uc, nullptr, 0, msg, "%s", detail_msg);

        ShouldNotReachHere();
      } else if (sig == SIGILL && nativeInstruction_at(align_down(pc, BytesPerBundle))->is_sigill_not_entrant()) {
        // Not entrant: the verified entry was patched to a break.m
        // (NativeJump::patch_verified_entry). Re-dispatch the call; b0 still
        // holds the caller's return address.
        if (TraceTraps) {
          tty->print_cr("trap: not_entrant (SIGILL)");
        }
        stub = SharedRuntime::get_handle_wrong_method_stub();
      } else if (sig == SIGSEGV &&
                 MacroAssembler::uses_implicit_null_check((void*)addr)) {
          // Determination of interpreter/vtable stub/compiled code null exception.
          // The kernel reports the faulting instruction as its bundle plus the
          // slot number (cr.iip + psr.ri), and code records implicit-exception
          // pcs as bundle addresses: C2's memory nodes are one bundle,
          // { adds ;; ld/st }, and fault in slot 1.
          stub = SharedRuntime::continuation_for_implicit_exception(thread, align_down(pc, BytesPerBundle),
                                                                    SharedRuntime::IMPLICIT_NULL);
      }
      // IA-64 has no integer divide instruction, so there is no hardware
      // divide-by-zero trap to map: generated code tests the divisor itself.
    } else if ((thread->thread_state() == _thread_in_vm ||
                thread->thread_state() == _thread_in_native) &&
                sig == SIGBUS && /* info->si_code == BUS_OBJERR && */
                thread->doing_unsafe_access()) {
      address next_pc = Assembler::locate_next_instruction(pc);
      if (UnsafeMemoryAccess::contains_pc(pc)) {
        next_pc = UnsafeMemoryAccess::page_error_continue_pc(pc);
      }
      stub = SharedRuntime::handle_unsafe_access(thread, next_pc);
    }

    // jni_fast_Get<Primitive>Field can trap at certain pc's if a GC kicks in
    // and the heap gets shrunk before the field access.
    if ((sig == SIGSEGV) || (sig == SIGBUS)) {
      address addr_slow = JNI_FastGetField::find_slowcase_pc(pc);
      if (addr_slow != (address)-1) {
        stub = addr_slow;
      }
    }
  }

  if (stub != nullptr) {
    // save all thread context in case we need to restore it
    if (thread != nullptr) {
      thread->set_saved_exception_pc(pc);
    }

    // stub is bundle-aligned, so this also resumes at slot 0.
    assert(is_aligned(stub, BytesPerBundle), "continuation must be bundle-aligned");
    os::Posix::ucontext_set_pc(uc, stub);
    return true;
  }

  return false; // Mute compiler
}

void os::Linux::init_thread_fpu_state(void) {
}

int os::Linux::get_fpu_control_word(void) {
  return 0;
}

void os::Linux::set_fpu_control_word(int fpu_control) {
}

////////////////////////////////////////////////////////////////////////////////
// thread stack

// Minimum usable stack sizes required to get to user code. Space for
// HotSpot guard pages is added later. These are the memory-stack figures;
// the register backing store takes the other half of the allocation
// (os::current_stack_base_and_size), which ThreadStackSize already accounts
// for (globals_linux_ia64.hpp).
size_t os::_compiler_thread_min_stack_allowed = 128 * K;
size_t os::_java_thread_min_stack_allowed = 128 * K;
size_t os::_vm_internal_thread_min_stack_allowed = 128 * K;

// return default stack size for thr_type
size_t os::Posix::default_stack_size(os::ThreadType thr_type) {
  // default stack size (compiler thread needs larger stack)
  size_t s = (thr_type == os::compiler_thread ? 4 * M : 2 * M);
  return s;
}

/////////////////////////////////////////////////////////////////////////////
// helper functions for fatal error handler

// Static register names, using this port's roles (FRAME-DESIGN.md 2.2).
static const char* gr_names[32] = {
  "r0",       "r1(gp)",      "r2(t0)",    "r3(t1)",
  "r4(fp)",   "r5(Rthread)", "r6(Rbcp)",  "r7(Resp)",
  "r8(Rtos)", "r9(t2)",      "r10(t3)",   "r11(t4)",
  "r12(sp)",  "r13(tp)",     "r14(Rmethod)", "r15(Rcpool)",
  "r16",      "r17",         "r18(Rsender_sp)", "r19(Rlocals)",
  "r20",      "r21",         "r22",       "r23",
  "r24",      "r25",         "r26",       "r27",
  "r28",      "r29",         "r30",       "r31"
};

void os::print_context(outputStream *st, const void *context) {
  if (context == nullptr) return;

  const ucontext_t *uc = (const ucontext_t*)context;
  const mcontext_t& mc = uc->uc_mcontext;

  st->print_cr("Registers:");
  st->print_cr("ip=" INTPTR_FORMAT " (bundle " INTPTR_FORMAT ", slot %d)",
               (uintptr_t)mc.sc_ip, (uintptr_t)(mc.sc_ip & ~(uintptr_t)0xf), (int)(mc.sc_ip & 0x3));
  st->print_cr("cfm=" INTPTR_FORMAT "  pr=" INTPTR_FORMAT "  nat=" INTPTR_FORMAT,
               (uintptr_t)mc.sc_cfm, (uintptr_t)mc.sc_pr, (uintptr_t)mc.sc_nat);
  st->print_cr("ar.bsp=" INTPTR_FORMAT "  ar.pfs=" INTPTR_FORMAT "  ar.unat=" INTPTR_FORMAT,
               (uintptr_t)mc.sc_ar_bsp, (uintptr_t)mc.sc_ar_pfs, (uintptr_t)mc.sc_ar_unat);
  st->print_cr("ar.rsc=" INTPTR_FORMAT "  ar.ccv=" INTPTR_FORMAT "  ar.fpsr=" INTPTR_FORMAT,
               (uintptr_t)mc.sc_ar_rsc, (uintptr_t)mc.sc_ar_ccv, (uintptr_t)mc.sc_ar_fpsr);
  for (int b = 0; b < 8; b++) {
    st->print_cr("b%d=" INTPTR_FORMAT, b, (uintptr_t)mc.sc_br[b]);
  }
  // sc_gr[0] doubles as uc_link and is not r0's value.
  for (int r = 1; r < 32; r++) {
    st->print_cr("%-*.*s=" INTPTR_FORMAT, 16, 16, gr_names[r], (uintptr_t)mc.sc_gr[r]);
  }
  st->cr();
  st->print_cr("Floating point registers (f2-f31, raw 82-bit spill format):");
  for (int f = 2; f < 32; f++) {
    st->print_cr("f%d=" INTPTR_FORMAT ":" INTPTR_FORMAT, f,
                 (uintptr_t)mc.sc_fr[f].__u.__bits[1], (uintptr_t)mc.sc_fr[f].__u.__bits[0]);
  }
  st->cr();
}

void os::print_register_info(outputStream *st, const void *context, int& continuation) {
  const int register_count = 31;   // r1-r31
  int n = continuation;
  assert(n >= 0 && n <= register_count, "Invalid continuation value");
  if (context == nullptr || n == register_count) {
    return;
  }

  const ucontext_t *uc = (const ucontext_t*)context;
  while (n < register_count) {
    // Update continuation with next index before printing location
    continuation = n + 1;
    int r = n + 1;
    st->print("%-*.*s=", 16, 16, gr_names[r]);
    print_location(st, (intptr_t)uc->uc_mcontext.sc_gr[r]);
    ++n;
  }
}

void os::setup_fpu() {
}

#ifndef PRODUCT
void os::verify_stack_alignment() {
  assert(((intptr_t)os::current_stack_pointer() & (StackAlignmentInBytes-1)) == 0, "incorrect stack alignment");
}
#endif

int os::extra_bang_size_in_bytes() {
  return 0;
}

extern "C" {
  int SpinPause() {
    return 0;
  }

  // The element-atomic copies, as on riscv: IA-64 aligned 2/4/8-byte
  // accesses are single-copy atomic, and atomic_copy64 is a plain aligned
  // 8-byte load/store (atomic_linux_ia64.hpp).
  void _Copy_conjoint_jshorts_atomic(const jshort* from, jshort* to, size_t count) {
    if (from > to) {
      const jshort *end = from + count;
      while (from < end) {
        *(to++) = *(from++);
      }
    } else if (from < to) {
      const jshort *end = from;
      from += count - 1;
      to   += count - 1;
      while (from >= end) {
        *(to--) = *(from--);
      }
    }
  }
  void _Copy_conjoint_jints_atomic(const jint* from, jint* to, size_t count) {
    if (from > to) {
      const jint *end = from + count;
      while (from < end) {
        *(to++) = *(from++);
      }
    } else if (from < to) {
      const jint *end = from;
      from += count - 1;
      to   += count - 1;
      while (from >= end) {
        *(to--) = *(from--);
      }
    }
  }

  void _Copy_conjoint_jlongs_atomic(const jlong* from, jlong* to, size_t count) {
    if (from > to) {
      const jlong *end = from + count;
      while (from < end) {
        atomic_copy64(from++, to++);
      }
    } else if (from < to) {
      const jlong *end = from;
      from += count - 1;
      to   += count - 1;
      while (from >= end) {
        atomic_copy64(from--, to--);
      }
    }
  }

  void _Copy_arrayof_conjoint_bytes(const HeapWord* from,
                                    HeapWord* to,
                                    size_t    count) {
    memmove(to, from, count);
  }
  void _Copy_arrayof_conjoint_jshorts(const HeapWord* from,
                                      HeapWord* to,
                                      size_t    count) {
    memmove(to, from, count * 2);
  }
  void _Copy_arrayof_conjoint_jints(const HeapWord* from,
                                    HeapWord* to,
                                    size_t    count) {
    memmove(to, from, count * 4);
  }
  void _Copy_arrayof_conjoint_jlongs(const HeapWord* from,
                                     HeapWord* to,
                                     size_t    count) {
    memmove(to, from, count * 8);
  }
};
