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

#ifndef CPU_IA64_FRAME_IA64_HPP
#define CPU_IA64_FRAME_IA64_HPP

// A frame represents a physical stack frame (an activation). Frames can be C
// or Java frames, and the Java frames can be interpreted or compiled.
//
// The slot numbering below is deliberately identical to cpu/riscv, so that
// every assumption shared code makes about frame:: transfers unchanged. fp
// points at the caller's sp at the moment of the call, the two words below it
// hold the return address and the caller's fp, and the interpreter's state
// follows underneath.
//
// ------------------------------ Template interpreter -----------------------
//    [expression stack      ] * <- sp
//    [monitors[0]           ]   \
//     ...                        | monitor block
//    [monitors[k-1]         ]   /
//    [frame initial esp     ]  ( == &monitors[0], initially here)  initial_sp_offset
//    [byte code pointer     ]  = bcp()                             bcp_offset
//    [pointer to locals     ]  = locals()                          locals_offset
//    [constant pool cache   ]  = cache()                           cache_offset
//    [klass of method       ]  = mirror()                          mirror_offset
//    [extended SP           ]                                      extended_sp_offset
//    [methodData            ]  = mdp()                             mdp_offset
//    [Method                ]  = method()                          method_offset
//    [last esp              ]  = last_sp()                         last_sp_offset
//    [sender's SP           ]                                      sender_sp_offset
//    [old frame pointer     ]                                      link_offset
//    [return pc             ]                                      return_addr_offset
//                              <- fp
//    [locals and parameters ]
//                              <- sender sp
//
// ------------------------------ What differs on IA-64 ----------------------
//
// 1. The return address arrives in b0, not on the stack. Every prologue that
//    calls anything must move it to memory explicitly ("mov t0 = b0" then a
//    store); there is no push. See FRAME-DESIGN.md section 4.3.
//
// 2. Every frame reserves 16 bytes of psABI scratch at its *bottom*, [sp,
//    sp+16). Any callee may clobber that region without moving sp, and GCC
//    does. It is not described by any offset here -- it is simply space no
//    prologue may put anything live into, and outgoing stack arguments start
//    above it.
//
// 3. There is no register-stack component. Generated code executes a single
//    `alloc` in call_stub and otherwise confines itself to the static
//    registers, so a frame walk follows one stack, not two (FRAME-DESIGN.md
//    section 1). This is the reason frame:: needs no ar.bsp analogue.
//
// 4. There is no conventional frame pointer in the ABI sense -- IA-64
//    unwinding is table-driven -- so fp here is purely HotSpot's own linkage
//    chain, laid down by the prologues this port generates. PreserveFramePointer
//    is false and nothing outside generated code depends on it.

 public:
  enum {
    pc_return_offset                                 =  0,

    // All frames
    link_offset                                      = -2,
    return_addr_offset                               = -1,
    sender_sp_offset                                 =  0,

    // Interpreter frames
    interpreter_frame_result_handler_offset          =  1, // for native calls only
    interpreter_frame_oop_temp_offset                =  0, // for native calls only

    interpreter_frame_sender_sp_offset               = -3,
    // outgoing sp before a call to an invoked method
    interpreter_frame_last_sp_offset                 = interpreter_frame_sender_sp_offset - 1,
    interpreter_frame_method_offset                  = interpreter_frame_last_sp_offset - 1,
    interpreter_frame_mdp_offset                     = interpreter_frame_method_offset - 1,
    interpreter_frame_extended_sp_offset             = interpreter_frame_mdp_offset - 1,
    interpreter_frame_mirror_offset                  = interpreter_frame_extended_sp_offset - 1,
    interpreter_frame_cache_offset                   = interpreter_frame_mirror_offset - 1,
    interpreter_frame_locals_offset                  = interpreter_frame_cache_offset - 1,
    interpreter_frame_bcp_offset                     = interpreter_frame_locals_offset - 1,
    interpreter_frame_initial_sp_offset              = interpreter_frame_bcp_offset - 1,

    interpreter_frame_monitor_block_top_offset       = interpreter_frame_initial_sp_offset,
    interpreter_frame_monitor_block_bottom_offset    = interpreter_frame_initial_sp_offset,

    // Entry frames
    // n.b. these values are determined by the layout call_stub lays down in
    // stubGenerator_ia64.cpp and MUST be changed together with it. The IA-64
    // save area is larger than most ports' because f2-f5 and f16-f31 are saved
    // with stf.spill, 16 bytes each rather than 8. See FRAME-DESIGN.md 5.2.
    // TODO: fix these two up when call_stub is written (JIT-SCOPE.md phase 3).
    entry_frame_after_call_words                     =  62,
    entry_frame_call_wrapper_offset                  = -10,

    // we don't need a save area
    arg_reg_save_area_bytes                          =  0,

    // size, in words, of frame metadata (e.g. pc and link)
    metadata_words                                   =  2,
    // size, in words, of metadata at frame bottom, i.e. it is not part of the
    // caller/callee overlap
    metadata_words_at_bottom                         = metadata_words,
    // size, in words, of frame metadata at the frame top, i.e. it is located
    // between a callee frame and its stack arguments, where it is part
    // of the caller/callee overlap
    metadata_words_at_top                            = 0,
    // in bytes -- the psABI requires sp to be 16-byte aligned at all times,
    // not only at call boundaries
    frame_alignment                                  = 16,
    // size, in words, of maximum shift in frame position due to alignment
    align_wiggle                                     =  1
  };

  intptr_t ptr_at(int offset) const {
    return *ptr_at_addr(offset);
  }

  void ptr_at_put(int offset, intptr_t value) {
    *ptr_at_addr(offset) = value;
  }

 private:
  // an additional field beyond _sp and _pc:
  union {
    intptr_t*  _fp; // frame pointer
    int _offset_fp; // relative frame pointer for use in stack-chunk frames
  };
  // The interpreter and adapters will extend the frame of the caller.
  // Since oopMaps are based on the sp of the caller before extension
  // we need to know that value. However in order to compute the address
  // of the return address we need the real "raw" sp.
  union {
    intptr_t* _unextended_sp;
    int _offset_unextended_sp; // for use in stack-chunk frames
  };

  void adjust_unextended_sp() NOT_DEBUG_RETURN;

  intptr_t* ptr_at_addr(int offset) const {
    return (intptr_t*) addr_at(offset);
  }

#ifdef ASSERT
  // Used in frame::sender_for_{interpreter,compiled}_frame
  static void verify_deopt_original_pc(nmethod* nm, intptr_t* unextended_sp);
#endif

 public:
  // Constructors

  frame(intptr_t* ptr_sp, intptr_t* ptr_fp, address pc);

  frame(intptr_t* ptr_sp, intptr_t* unextended_sp, intptr_t* ptr_fp, address pc);

  frame(intptr_t* sp, intptr_t* unextended_sp, intptr_t* fp, address pc, CodeBlob* cb);
  // used for fast frame construction by continuations
  frame(intptr_t* sp, intptr_t* unextended_sp, intptr_t* fp, address pc, CodeBlob* cb,
        const ImmutableOopMap* oop_map, bool on_heap);

  frame(intptr_t* ptr_sp, intptr_t* ptr_fp);

  void init(intptr_t* ptr_sp, intptr_t* ptr_fp, address pc);
  void setup(address pc);

  // accessors for the instance variables
  // Note: not necessarily the real 'frame pointer' (see real_fp)

  intptr_t* fp() const          { assert_absolute(); return _fp; }
  void set_fp(intptr_t* newfp)  { _fp = newfp; }
  int offset_fp() const         { assert_offset(); return _offset_fp; }
  void set_offset_fp(int value) { assert_on_heap(); _offset_fp = value; }

  inline address* sender_pc_addr() const;

  // expression stack tos if we are nested in a java call
  intptr_t* interpreter_frame_last_sp() const;

  void interpreter_frame_set_extended_sp(intptr_t* sp);

  template <typename RegisterMapT>
  static void update_map_with_saved_link(RegisterMapT* map, intptr_t** link_addr);

  // deoptimization support
  void interpreter_frame_set_last_sp(intptr_t* last_sp);

  static jint interpreter_frame_expression_stack_direction() { return -1; }

  // returns the sending frame, without applying any barriers
  inline frame sender_raw(RegisterMap* map) const;

#endif // CPU_IA64_FRAME_IA64_HPP
