/*
 * Copyright (c) 1999, 2024, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, 2015, Red Hat Inc. All rights reserved.
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

#ifndef CPU_IA64_C1_MACROASSEMBLER_IA64_HPP
#define CPU_IA64_C1_MACROASSEMBLER_IA64_HPP

using MacroAssembler::null_check;

// C1_MacroAssembler contains high-level macros for C1

 private:
  int _rsp_offset;    // track rsp changes
  // initialization
  // Compiled code is packed (UseBundlePacking, BUNDLING.md Stage 2).
  void pd_init() { _rsp_offset = 0; set_pack_default(true); }


 public:
  void try_allocate(
    Register obj,                      // result: pointer to object after successful allocation
    Register var_size_in_bytes,        // object size in bytes if unknown at compile time; invalid otherwise
    int      con_size_in_bytes,        // object size in bytes if known at compile time
    Register tmp1,                     // temp register
    Register tmp2,                     // temp register
    Label&   slow_case                 // continuation point if fast allocation fails
  );

  void initialize_header(Register obj, Register klass, Register len, Register tmp1, Register tmp2);
  void initialize_body(Register obj, Register len_in_bytes, int hdr_size_in_bytes, Register tmp);

  void float_cmp(bool is_float, int unordered_result,
                 FloatRegister f0, FloatRegister f1,
                 Register result);

  // locking
  // hdr     : must be x10, contents destroyed
  // obj     : must point to the object to lock, contents preserved
  // disp_hdr: must point to the displaced header location, contents preserved
  // temp : temporary register, must not be scratch register t0 or t1
  // returns code offset at which to add null check debug information
  int lock_object(Register swap, Register obj, Register disp_hdr, Register temp, Label& slow_case);

  // unlocking
  // hdr     : contents destroyed
  // obj     : must point to the object to lock, contents preserved
  // disp_hdr: must be x10 & must point to the displaced header location, contents destroyed
  // temp : temporary register, must not be scratch register t0 or t1
  void unlock_object(Register swap, Register obj, Register lock, Register temp, Label& slow_case);

  void initialize_object(
    Register obj,                      // result: pointer to object after successful allocation
    Register klass,                    // object klass
    Register var_size_in_bytes,        // object size in bytes if unknown at compile time; invalid otherwise
    int      con_size_in_bytes,        // object size in bytes if   known at compile time
    Register tmp1,                     // temp register
    Register tmp2,                     // temp register
    bool     is_tlab_allocated         // the object was allocated in a TLAB; relevant for the implementation of ZeroTLAB
  );

  // allocation of fixed-size objects
  // (can also be used to allocate fixed-size arrays, by setting
  // hdr_size correctly and storing the array length afterwards)
  // obj        : will contain pointer to allocated object
  // t1, t2     : temp registers - contents destroyed
  // header_size: size of object header in words
  // object_size: total size of object in words
  // slow_case  : exit to slow case implementation if fast allocation fails
  void allocate_object(Register obj, Register tmp1, Register tmp2, int header_size, int object_size, Register klass, Label& slow_case);

  enum {
    max_array_allocation_length = 0x00FFFFFF
  };

  // allocation of arrays
  // obj                 : will contain pointer to allocated object
  // len                 : array length in number of elements
  // t                   : temp register - contents destroyed
  // base_offset_in_bytes: offset of first array element, in bytes
  // f                   : element scale factor
  // slow_case           : exit to slow case implementation if fast allocation fails
  // zero_array          : zero the allocated array or not
  void allocate_array(Register obj, Register len, Register tmp1, Register tmp2, int base_offset_in_bytes, int f, Register klass, Label& slow_case, bool zero_array);

  int  rsp_offset() const { return _rsp_offset; }

  void invalidate_registers(bool inv_r0, bool inv_r19, bool inv_r2, bool inv_r3, bool inv_r4, bool inv_r5) PRODUCT_RETURN;

  // This platform only uses signal-based null checks. The Label is not needed.
  void null_check(Register r, Label *Lnull = nullptr) { MacroAssembler::null_check(r); }

  void load_parameter(int offset_in_words, Register reg);

  // Arguments of the C1 runtime stubs that riscv passes on the stack
  // (store_parameter) or in t0/t1: here they travel in t2/t3. C1 never
  // allocates r9-r11, so a CodeStub can load them without disturbing a live
  // value, and the stubs' register saver (RegisterSaver, which uses only t0/t1,
  // f6 and p6/p7) leaves them intact. See c1_Runtime1_ia64.cpp for each stub's
  // use; klass, length and varargs use FrameMap::stub_*_reg instead, and stubs
  // reached through call_runtime take C arguments in out0.. .
  static constexpr Register stub_arg0 = t2;   // r9
  static constexpr Register stub_arg1 = t3;   // r10

  void inline_cache_check(Register receiver, Register iCache, Label &L);

  // UsePollWordRegister: compiled C1 code keeps &JavaThread::_poll_word in
  // r7 (Resp, which compiled code otherwise leaves alone and C callees
  // preserve), so a loop poll is ld8 + tbit with no address computation.
  // Set at method and OSR entry (build_frame), after every Java call (an
  // interpreted callee uses r7 as Resp) and at exception-handler entry
  // (Runtime1 handle_exception). Debug builds check it at every loop poll.
  static constexpr Register Rpoll_word = r7;
  void set_poll_word_register();

#endif // CPU_IA64_C1_MACROASSEMBLER_IA64_HPP
