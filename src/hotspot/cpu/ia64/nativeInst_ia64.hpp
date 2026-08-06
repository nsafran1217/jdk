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

#ifndef CPU_IA64_NATIVEINST_IA64_HPP
#define CPU_IA64_NATIVEINST_IA64_HPP

#include "asm/assembler.hpp"
#include "runtime/icache.hpp"
#include "runtime/os.hpp"
#include "utilities/globalDefinitions.hpp"

// Accessors for inspecting and patching already-generated instructions.
//
// Two IA-64 properties shape everything here:
//
// 1. An "instruction" is a whole 16-byte bundle. Nothing in this port emits
//    more than one instruction per bundle, so stepping by instruction_size is
//    always correct, and a patch never has to preserve a neighbour sharing the
//    bundle. If bundle packing is ever implemented that stops being true and
//    every class below has to be revisited.
//
// 2. The instruction cache is *not* coherent with stores. Every patch here
//    must be followed by an ICache::invalidate_range; unlike x86, getting this
//    wrong produces a machine that keeps executing the old instruction. That
//    is also why globalDefinitions_ia64.hpp defines DEOPTIMIZE_WHEN_PATCHING:
//    a patch is not atomic against concurrent execution.

class NativeCall;

class NativeInstruction {
  friend class Relocation;
 public:
  enum {
    instruction_size = BytesPerBundle
  };

  bool is_nop() const;
  bool is_call() const   { return is_call_at(addr_at(0)); }
  bool is_jump() const;
  bool is_jump_or_nop();
  bool is_safepoint_poll();

  static bool is_call_at(address addr);

 protected:
  address addr_at(int offset) const { return address(this) + offset; }

  ia64::Bundle*       bundle_at(int offset)       { return (ia64::Bundle*)addr_at(offset); }
  const ia64::Bundle* bundle_at(int offset) const { return (const ia64::Bundle*)addr_at(offset); }

  jint     int_at(int offset) const { return (jint)    Bytes::get_native_u4(addr_at(offset)); }
  juint   uint_at(int offset) const { return           Bytes::get_native_u4(addr_at(offset)); }
  address  ptr_at(int offset) const { return (address) Bytes::get_native_u8(addr_at(offset)); }
  oop      oop_at(int offset) const { return cast_to_oop(Bytes::get_native_u8(addr_at(offset))); }

  void  set_int_at(int offset, jint i)      { Bytes::put_native_u4(addr_at(offset), i); }
  void set_uint_at(int offset, jint i)      { Bytes::put_native_u4(addr_at(offset), i); }
  void  set_ptr_at(int offset, address ptr) { Bytes::put_native_u8(addr_at(offset), (u8)ptr); }
  void  set_oop_at(int offset, oop o)       { Bytes::put_native_u8(addr_at(offset), cast_from_oop<u8>(o)); }

 public:
  inline friend NativeInstruction* nativeInstruction_at(address addr);

  static bool maybe_cpool_ref(address instr) { return false; }
};

inline NativeInstruction* nativeInstruction_at(address addr) {
  return (NativeInstruction*)addr;
}

NativeCall* nativeCall_at(address addr);
NativeCall* nativeCall_before(address return_address);

// A call site. The port's call sequence materialises the target with movl,
// moves it to a branch register and branches:
//
//     movl  t = <target>
//     mov   b6 = t
//     br.call.sptk.many b0 = b6
//
// Three bundles, and the target lives in the movl immediate, which is what
// destination()/set_destination() read and write.
class NativeCall : private NativeInstruction {
 private:
  enum {
    movl_offset      = 0,
    mov_to_br_offset = BytesPerBundle,
    br_call_offset   = 2 * BytesPerBundle,
    call_size        = 3 * BytesPerBundle
  };

 public:
  enum {
    instruction_size            = call_size,
    return_address_offset       = call_size,
    displacement_offset         = 0
  };

  address instruction_address() const { return addr_at(0); }
  address next_instruction_address() const { return addr_at(call_size); }
  address return_address() const { return addr_at(call_size); }

  address destination() const;
  void set_destination(address dest);
  void set_destination_mt_safe(address dest);

  void verify_alignment() {
    assert(is_aligned(addr_at(0), BytesPerBundle), "call must be bundle-aligned");
  }
  void verify();
  void print();

  static bool is_at(address addr);
  static bool is_call_before(address return_address) {
    return is_at(return_address - call_size);
  }
};

inline NativeCall* nativeCall_at(address addr) {
  return (NativeCall*)addr;
}

// A movl that materialises a constant -- an oop, a Metadata*, or any absolute
// address. Reading and writing it is exactly the encoder's ReadMovlImm and
// WriteMovlImm, so this one is fully implemented rather than stubbed: it is
// needed the moment any relocation is processed.
class NativeMovConstReg : public NativeInstruction {
 public:
  enum {
    movl_size        = BytesPerBundle,
    instruction_size = movl_size
  };

  address instruction_address() const { return addr_at(0); }
  address next_instruction_address() const { return addr_at(instruction_size); }

  intptr_t data() const {
    return (intptr_t)ia64::ReadMovlImm(bundle_at(0));
  }

  void set_data(intptr_t x) {
    ia64::WriteMovlImm(bundle_at(0), (uint64_t)x);
    // The I-cache is not coherent with stores on this architecture.
    ICache::invalidate_range(instruction_address(), instruction_size);
  }

  void flush() {
    ICache::invalidate_range(instruction_address(), instruction_size);
  }

  void verify();
  void print();
};

inline NativeMovConstReg* nativeMovConstReg_at(address addr) {
  return (NativeMovConstReg*)addr;
}

inline NativeMovConstReg* nativeMovConstReg_before(address addr) {
  return (NativeMovConstReg*)(addr - NativeMovConstReg::instruction_size);
}

// An unconditional jump, built the same way as a call but branching rather
// than linking.
class NativeJump : public NativeInstruction {
 public:
  enum {
    instruction_size = 3 * BytesPerBundle
  };

  address instruction_address() const { return addr_at(0); }
  address next_instruction_address() const { return addr_at(instruction_size); }

  address jump_destination() const;
  void set_jump_destination(address dest);

  static void check_verified_entry_alignment(address entry, address verified_entry) {}
  static void patch_verified_entry(address entry, address verified_entry, address dest);

  void verify();
};

inline NativeJump* nativeJump_at(address addr) {
  return (NativeJump*)addr;
}

class NativeGeneralJump : public NativeJump {
 public:
  enum {
    instruction_size = NativeJump::instruction_size
  };

  static void insert_unconditional(address code_pos, address entry);
  static void replace_mt_safe(address instr_addr, address code_buffer);
};

inline NativeGeneralJump* nativeGeneralJump_at(address addr) {
  return (NativeGeneralJump*)addr;
}

class NativeIllegalInstruction : public NativeInstruction {
 public:
  enum {
    instruction_size = BytesPerBundle
  };
  static void insert(address code_pos);
};

// A nop emitted after a call so that a later deoptimisation can overwrite it.
// break.b carries a 21-bit immediate, which is where the data lives.
class NativePostCallNop : public NativeInstruction {
 public:
  bool check() const { return is_nop(); }
  bool decode(int32_t& oopmap_slot, int32_t& cb_offset) const { return false; }
  bool patch(int32_t oopmap_slot, int32_t cb_offset) { return false; }
  void make_deopt();
};

inline NativePostCallNop* nativePostCallNop_at(address addr) {
  NativePostCallNop* nop = (NativePostCallNop*)addr;
  return nop->check() ? nop : nullptr;
}

class NativeDeoptInstruction : public NativeInstruction {
 public:
  enum {
    instruction_size = BytesPerBundle
  };

  address instruction_address() const { return addr_at(0); }
  address next_instruction_address() const { return addr_at(instruction_size); }

  void verify();

  static bool is_deopt_at(address instr);
  static void insert(address code_pos);
};

#endif // CPU_IA64_NATIVEINST_IA64_HPP
