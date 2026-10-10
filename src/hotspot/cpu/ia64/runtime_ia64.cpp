/*
 * Copyright (c) 2003, 2025, Oracle and/or its affiliates. All rights reserved.
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

#ifdef COMPILER2
#include "asm/macroAssembler.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "code/vmreg.hpp"
#include "interpreter/interpreter.hpp"
#include "opto/runtime.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/vframeArray.hpp"
#include "utilities/globalDefinitions.hpp"
#include "vmreg_ia64.inline.hpp"

#define __ masm->

// The two blobs' frames are enter()'s: the return address and the caller's fp
// in the two words below the caller's sp, fp = the caller's sp, and the
// 16-byte psABI scratch area below (MacroAssembler::enter_frame_words()).
// Neither holds anything else, so their oop maps are empty.
static const int blob_frame_words = MacroAssembler::enter_frame_words();

//------------------------------generate_uncommon_trap_blob--------------------
// Entered by a Java-convention call from C2 code (b0: the return address
// into the method being deoptimized) with the trap request in j_rarg0.
// Unpacks the compiled frame into interpreter frames, as the deopt blob does
// (SharedRuntime::generate_deopt_blob), and returns into the interpreter.
UncommonTrapBlob* OptoRuntime::generate_uncommon_trap_blob() {
  // Allocate space for the code
  ResourceMark rm;
  // Setup code generation tools
  const char* name = OptoRuntime::stub_name(OptoStubId::uncommon_trap_id);
  CodeBuffer buffer(name, 4096, 1024);
  if (buffer.blob() == nullptr) {
    return nullptr;
  }
  MacroAssembler* masm = new MacroAssembler(&buffer);

  const Register unroll = r7;   // preserved by the C calls below

  int start = __ offset();

  // Push self-frame. fp becomes the compiled frame's sp.
  __ enter();

  // The compiler left the trap request in j_rarg0: an int, its upper half
  // undefined (C2-DESIGN.md section 3).
  __ sxt4(c_rarg1, j_rarg0);

  // UnrollBlock* uncommon_trap(JavaThread* thread, jint unloaded_class_index, jint exec_mode)
  //
  // It needs the last Java frame (no fp: the compiled frame is found from
  // sp) and cannot block, so no GC can happen.
  {
    Label retaddr;
    __ set_last_Java_frame(sp, noreg, retaddr, t2);
    __ mov(c_rarg0, Rthread);
    __ mov_immediate(c_rarg2, Deoptimization::Unpack_uncommon_trap);
    __ call_c(CAST_FROM_FN_PTR(address, Deoptimization::uncommon_trap));
    __ bind(retaddr);
  }

  // Set an oopmap for the call site
  OopMapSet* oop_maps = new OopMapSet();
  oop_maps->add_gc_map(__ offset() - start, new OopMap(blob_frame_words * VMRegImpl::slots_per_word, 0));

  __ reset_last_Java_frame(false);

  __ mov(unroll, r8);

#ifdef ASSERT
  { Label L;
    __ ld4(t2, Address(unroll, Deoptimization::UnrollBlock::unpack_kind_offset()));
    __ cmp4_eq_imm(ptmp0, ptmp1, Deoptimization::Unpack_uncommon_trap, t2);
    __ br_cond(L, ptmp0);
    __ stop("OptoRuntime::generate_uncommon_trap_blob: expected Unpack_uncommon_trap");
    __ bind(L);
  }
#endif

  // Pop the self-frame and the deoptimized frame (youngest to oldest):
  //   1: self-frame (this blob's)
  //   2: deoptimized frame
  //   3: caller of the deoptimized frame (could be compiled/interpreted)
  // The deoptimized frame starts at our fp; its own linkage is at its top.
  // Everything is loaded before sp moves up: there is no red zone.
  __ ld4(t3, Address(unroll, Deoptimization::UnrollBlock::size_of_deoptimized_frame_offset()));
  __ add(t3, fp, t3);                                              // frame 3's sp
  __ ld8(t4, Address(t3, frame::return_addr_offset * wordSize));  // return into frame 3
  __ ld8(t2, Address(t3, frame::link_offset * wordSize));         // frame 3's fp
  __ mov(sp, t3);
  __ mov(fp, t2);

  // Load the arrays of frame pcs and frame sizes, and the frame count.
  const Register pcs       = r14;
  const Register sizes     = r15;
  const Register count     = r16;
  const Register sender_sp = r17;
  __ ld8(pcs,   Address(unroll, Deoptimization::UnrollBlock::frame_pcs_offset()));
  __ ld8(sizes, Address(unroll, Deoptimization::UnrollBlock::frame_sizes_offset()));
  __ ld4(count, Address(unroll, Deoptimization::UnrollBlock::number_of_frames_offset()));

  // Now adjust the caller's stack to make up for the extra locals but
  // record the original sp so that we can save it in the skeletal
  // interpreter frame and the stack walking of interpreter_sender will get
  // the unextended sp value and not the "real" sp value.
  __ mov(sender_sp, sp);
  __ ld4(t2, Address(unroll, Deoptimization::UnrollBlock::caller_adjustment_offset()));
  __ sub(sp, sp, t2);

  // Push interpreter frames in a loop, as the deopt blob does.
  Label loop;
  __ bind(loop);
  __ ld8_inc(t2, sizes, wordSize);          // frame size in bytes
  __ ld8_inc(t4, pcs, wordSize);            // its return address
  __ mov(t3, sp);                           // the new frame's fp
  __ sub(sp, sp, t2);
  __ adds(t2, frame::return_addr_offset * wordSize, t3);
  __ Assembler::st8(t2, t4);
  __ adds(t2, frame::link_offset * wordSize, t3);
  __ Assembler::st8(t2, fp);
  __ mov(fp, t3);
  // This value is corrected by layout_activation_impl
  __ st8(Address(fp, frame::interpreter_frame_last_sp_offset * wordSize), zr);
  __ st8(Address(fp, frame::interpreter_frame_sender_sp_offset * wordSize), sender_sp); // Make it walkable
  __ mov(sender_sp, sp);                    // Pass sender_sp to next frame
  __ adds(count, -1, count);
  __ bnez(count, loop);

  // Re-push the self-frame, returning to the youngest frame's continuation.
  __ ld8(t4, Address(pcs, 0));
  __ mov_to_br(breturn, t4);
  __ enter();

  // void Deoptimization::unpack_frames(JavaThread* thread, int exec_mode).
  // fp is set because the frames look interpreted now. The recorded pc only
  // needs to point into this blob.
  {
    Label retaddr;
    __ set_last_Java_frame(sp, fp, retaddr, t2);
    __ mov(c_rarg0, Rthread);
    __ mov_immediate(c_rarg1, Deoptimization::Unpack_uncommon_trap);
    __ call_c(CAST_FROM_FN_PTR(address, Deoptimization::unpack_frames));
    __ bind(retaddr);
    // Set an oopmap for the call site
    oop_maps->add_gc_map(__ offset() - start, new OopMap(blob_frame_words * VMRegImpl::slots_per_word, 0));
  }

  // Clear fp AND pc
  __ reset_last_Java_frame(true);

  // Pop self-frame and jump to the interpreter.
  __ leave();
  __ ret();

  // Make sure all code is generated
  masm->flush();

  return UncommonTrapBlob::create(&buffer, oop_maps, blob_frame_words);
}

//------------------------------generate_exception_blob---------------------------
// Jumped to from a C2 nmethod's exception handler (HandlerImpl::
// emit_exception_handler), with the nmethod's frame live.
//
// Arguments:
//   r8:  exception oop
//   r28: exception pc (in the nmethod; a call's return address)
//
// Results:
//   r8:  exception oop
//   r28: exception pc
//   destination: the exception handler OptoRuntime::handle_exception_C found
//                (in this nmethod, or the deopt blob)
//
// Note: the exception pc MUST be at a call (precise debug information).
ExceptionBlob* OptoRuntime::generate_exception_blob() {
  // Allocate space for the code
  ResourceMark rm;
  // Setup code generation tools
  const char* name = OptoRuntime::stub_name(OptoStubId::exception_id);
  CodeBuffer buffer(name, 2048, 1024);
  if (buffer.blob() == nullptr) {
    return nullptr;
  }
  MacroAssembler* masm = new MacroAssembler(&buffer);

  int start = __ offset();

  // Push a frame by hand, enter()'s shape but with the exception pc as its
  // return address: the stack walker then finds the nmethod's frame at the
  // pc that has the debug information. sp moves first (no red zone).
  __ adds(sp, -blob_frame_words * wordSize, sp);
  __ adds(t1, (blob_frame_words - 1) * wordSize, sp);   // caller_sp[-1]
  __ Assembler::st8(t1, Rexception_pc);                  //   return address
  __ adds(t1, (blob_frame_words - 2) * wordSize, sp);   // caller_sp[-2]
  __ Assembler::st8(t1, fp);                             //   caller's fp
  __ adds(fp, blob_frame_words * wordSize, sp);         // fp = caller's sp

  // Store exception in Thread object. We cannot pass any arguments to the
  // handle_exception call, since we do not want to make any assumption
  // about the size of the frame where the exception happened in.
  __ st8(Address(Rthread, JavaThread::exception_oop_offset()), Rexception);
  __ st8(Address(Rthread, JavaThread::exception_pc_offset()), Rexception_pc);

  // This call does all the hard work.  It checks if an exception handler
  // exists in the method.
  // If so, it returns the handler address.
  // If not, it prepares for stack-unwinding, restoring the callee-save
  // registers of the frame being removed.
  //
  // address OptoRuntime::handle_exception_C(JavaThread* thread)
  OopMapSet* oop_maps = new OopMapSet();
  {
    Label retaddr;
    __ set_last_Java_frame(sp, noreg, retaddr, t2);
    __ mov(c_rarg0, Rthread);
    __ call_c(CAST_FROM_FN_PTR(address, OptoRuntime::handle_exception_C));
    __ bind(retaddr);
    // Set an oopmap for the call site.  This oopmap will only be used if we
    // are unwinding the stack.  Hence, all locations will be dead.
    // Callee-saved registers will be the same as the frame above (i.e.,
    // handle_exception_stub), since they were restored when we got the
    // exception.
    oop_maps->add_gc_map(__ offset() - start, new OopMap(blob_frame_words * VMRegImpl::slots_per_word, 0));
  }

  __ reset_last_Java_frame(false);

  // Pop the frame, loading the caller's fp before sp moves.
  __ adds(t1, (blob_frame_words - 2) * wordSize, sp);
  __ Assembler::ld8(fp, t1);
  __ adds(sp, blob_frame_words * wordSize, sp);

  // r8: the exception handler (could be the deopt blob)
  __ mov(t2, r8);

  // Get the exception oop
  __ ld8(Rexception, Address(Rthread, JavaThread::exception_oop_offset()));
  // Get the exception pc in case we are deoptimized
  __ ld8(Rexception_pc, Address(Rthread, JavaThread::exception_pc_offset()));
#ifdef ASSERT
  __ st8(Address(Rthread, JavaThread::exception_handler_pc_offset()), zr);
  __ st8(Address(Rthread, JavaThread::exception_pc_offset()), zr);
#endif
  // Clear the exception oop so GC no longer processes it as a root.
  __ st8(Address(Rthread, JavaThread::exception_oop_offset()), zr);

  // r8:  exception oop
  // t2:  exception handler
  // r28: exception pc
  // Jump to handler
  __ jr(t2);

  // Make sure all code is generated
  masm->flush();

  // Set exception blob
  return ExceptionBlob::create(&buffer, oop_maps, blob_frame_words);
}
#endif // COMPILER2
