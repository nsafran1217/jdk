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
#ifndef CPU_IA64_MACROASSEMBLER_IA64_HPP
#define CPU_IA64_MACROASSEMBLER_IA64_HPP

#include "asm/assembler.hpp"
#include "asm/assembler.inline.hpp"
#include "code/vmreg.hpp"
#include "metaprogramming/enableIf.hpp"
#include "oops/compressedOops.hpp"
#include "utilities/powerOfTwo.hpp"

// MacroAssembler for IA-64.
//
// Conventions every helper here follows (FRAME-DESIGN.md has the reasoning):
//
// * t0 and t1 (r2, r3) belong to the MacroAssembler. Any helper may clobber
//   them, so callers never pass them as operands to a helper that is
//   documented to use them, and never keep a value in them across one.
//   t2-t4 (r9-r11) are scratch for the caller.
// * ptmp0/ptmp1 (p6/p7) are the compare-and-branch predicates and are
//   likewise clobbered by any helper that branches on a condition.
// * btmp (b6) is the indirect call/jump register; br.call overwrites b0.
// * Calls into C clobber everything except r4-r7 (fp, Rthread, Rbcp, Resp),
//   sp, f2-f5, f16-f31 and p1-p5. In particular out0-out7 come back
//   clobbered: the callee's inputs *are* our outputs and it may write them.
// * There is no red zone. Linux/IA-64 builds a signal frame directly below sp,
//   so a word stored below sp can be overwritten asynchronously at any time.
//   Every prologue moves sp down *before* storing into the new frame, and
//   every epilogue loads what it needs *before* moving sp back up.
// * [sp, sp+16) is the psABI scratch area: any C callee may write it. Nothing
//   live is ever kept there (FRAME-DESIGN.md 4.1).

class MacroAssembler : public Assembler {
 public:
  MacroAssembler(CodeBuffer* code) : Assembler(code) {}

  // Every instruction occupies one 16-byte bundle. Shared code and nativeInst
  // use this to step through generated code.
  enum {
    instruction_size = BytesPerBundle
  };

  // ---- alignment ---------------------------------------------------------

  // Pad with whole nop bundles. There is no way to advance the instruction
  // stream by less than 16 bytes, and it is always bundle-aligned, so any
  // smaller power-of-two alignment (shared code asks for wordSize) already
  // holds.
  // Pad so that (offset() + target_size) is a multiple of modulus: code of
  // target_size bytes emitted next then ends aligned (ic_check's VEP).
  void align(int modulus, int target_size) {
    assert(is_aligned(target_size, BytesPerBundle), "IA-64 code comes in bundles");
    while ((offset() + target_size) % modulus != 0) {
      nop();
    }
  }
  // The inline-cache check at a compiled method's unverified entry
  // (FRAME-DESIGN.md 11.2): receiver in j_rarg0, CompiledICData* in t1 (the
  // register ic_call loads it into -- t0 belongs to the call sequence).
  static int ic_check_size();
  int ic_check(int end_alignment = BytesPerBundle);

  void align(int modulus) {
    assert(modulus % BytesPerBundle == 0 || BytesPerBundle % modulus == 0,
           "alignment must divide, or be a multiple of, the bundle size");
    while (offset() % modulus != 0) { nop(); }
  }

  // ---- immediates and addresses ------------------------------------------
  //
  // There is no gp-relative addressing in generated code and IP-relative
  // branches reach only +/-16 MiB, so any value that does not fit the 14-bit
  // `adds` immediate is materialised with a 64-bit movl.

  void mov_immediate64(Register dst, uint64_t imm) { movl(dst, imm); }

  void movptr(Register dst, address addr)   { movl(dst, (uint64_t)(uintptr_t)addr); }
  void movptr(Register dst, uintptr_t imm)  { movl(dst, (uint64_t)imm); }

  void mov_immediate(Register dst, int64_t imm) {
    if (ia64::is_simm14(imm)) {
      adds(dst, imm, zr);          // r0 reads as 0
    } else {
      movl(dst, (uint64_t)imm);
    }
  }
  void mov(Register dst, int64_t imm) { mov_immediate(dst, imm); }
  // A register move, elided when it would be a self-move.
  void mov(Register dst, Register src, PredicateRegister qp = pTrue) {
    if (dst != src) Assembler::mov(dst, src, qp);
  }

  // dst = src + imm, for any imm. Uses tmp (default t0) only when imm does not
  // fit adds' 14 bits.
  void add_imm(Register dst, Register src, int64_t imm, Register tmp = t0);
  void sub_imm(Register dst, Register src, int64_t imm, Register tmp = t0) { add_imm(dst, src, -imm, tmp); }
  // dst = -src (64-bit). sub_imm above hides Assembler's reverse subtract.
  void neg(Register dst, Register src) { Assembler::sub_imm(dst, 0, src); }

  // dst = src * imm. Powers of two shift; anything else goes through the FP
  // unit's xma.l (IA-64 integer units have no multiplier), using ftmp0/ftmp1
  // and tmp. Not for hot paths.
  void mul_imm(Register dst, Register src, int64_t imm, Register tmp = t0);
  // dst = a * b (low 64 bits), through xma.l. Clobbers ftmp0/ftmp1.
  void mul(Register dst, Register a, Register b);

  // Load an unaligned little-endian (native order) value of 2 or 4 bytes
  // from base + offset, zero-extended, one byte at a time: IA-64 faults on a
  // misaligned access and the kernel's fixup is slow and logs. Clobbers tmp.
  void load_unaligned_le(Register dst, Register base, int offset, int size, Register tmp);
  // The same for a big-endian value (bytecode operands in the classfile's
  // own order, before the rewriter replaces them).
  void load_unaligned_be(Register dst, Register base, int offset, int size, Register tmp);

  // Materialise the effective address of |adr| into |dst|. IA-64 has no
  // displacement addressing, so this is a real computation at every access
  // site rather than something folded into the load or store. Uses t0 only
  // when dst == adr.base() and the offset needs a movl.
  void lea(Register dst, const Address& adr);

  // ---- loads and stores through an Address --------------------------------
  //
  // The bare-register forms (ld8(dst, reg), st8(reg, src), ...) remain
  // available alongside these; Address's constructor is explicit, so the two
  // can never be confused by an implicit conversion.
  using Assembler::ld1;
  using Assembler::ld2;
  using Assembler::ld4;
  using Assembler::ld8;
  using Assembler::st1;
  using Assembler::st2;
  using Assembler::st4;
  using Assembler::st8;
  using Assembler::ldfs;
  using Assembler::ldfd;
  using Assembler::stfs;
  using Assembler::stfd;
  //
  // Loads compute the address into their own destination, so they need no
  // scratch register. Stores need one for the address; t0 by default. The
  // "s" forms sign-extend (IA-64 loads always zero-extend).

  void ld1(Register dst, const Address& a)  { Register r = addr_for_load(dst, a); Assembler::ld1(dst, r); }
  void ld2(Register dst, const Address& a)  { Register r = addr_for_load(dst, a); Assembler::ld2(dst, r); }
  void ld4(Register dst, const Address& a)  { Register r = addr_for_load(dst, a); Assembler::ld4(dst, r); }
  void ld8(Register dst, const Address& a)  { Register r = addr_for_load(dst, a); Assembler::ld8(dst, r); }
  void ld1s(Register dst, const Address& a) { ld1(dst, a); sxt1(dst, dst); }
  void ld2s(Register dst, const Address& a) { ld2(dst, a); sxt2(dst, dst); }
  void ld4s(Register dst, const Address& a) { ld4(dst, a); sxt4(dst, dst); }

  void st1(const Address& a, Register src, Register tmp = t0) { Assembler::st1(addr_for_store(a, src, tmp), src); }
  void st2(const Address& a, Register src, Register tmp = t0) { Assembler::st2(addr_for_store(a, src, tmp), src); }
  void st4(const Address& a, Register src, Register tmp = t0) { Assembler::st4(addr_for_store(a, src, tmp), src); }
  void st8(const Address& a, Register src, Register tmp = t0) { Assembler::st8(addr_for_store(a, src, tmp), src); }

  void ldfs(FloatRegister dst, const Address& a, Register tmp = t0) { Assembler::ldfs(dst, addr_for_store(a, noreg, tmp)); }
  void ldfd(FloatRegister dst, const Address& a, Register tmp = t0) { Assembler::ldfd(dst, addr_for_store(a, noreg, tmp)); }
  void stfs(const Address& a, FloatRegister src, Register tmp = t0) { Assembler::stfs(addr_for_store(a, noreg, tmp), src); }
  void stfd(const Address& a, FloatRegister src, Register tmp = t0) { Assembler::stfd(addr_for_store(a, noreg, tmp), src); }

  // Pointer-sized aliases, for code shaped like the other ports.
  void ld_ptr(Register dst, const Address& a)                 { ld8(dst, a); }
  void st_ptr(const Address& a, Register src, Register tmp = t0) { st8(a, src, tmp); }

  // ---- immediates of any width ---------------------------------------------
  //
  // The A-unit compare-immediate and logical-immediate forms take a signed
  // 8-bit immediate only. These hide the Assembler forms and fall back to
  // materialising a wider constant in t1 -- so they clobber t1 when (and only
  // when) the immediate does not fit. Bytecode numbers above 127 are the
  // common case that needs it.
#define IMM8_OR_REG_CMP(name, regform)                                                           \
  void name(PredicateRegister p1, PredicateRegister p2, int64_t imm, Register r3,                \
            PredicateRegister qp = pTrue) {                                                       \
    if (ia64::is_simm8(imm)) { Assembler::name(p1, p2, imm, r3, qp); return; }                    \
    assert_different_registers(r3, t1);                                                           \
    mov_immediate(t1, imm);                                                                       \
    regform(p1, p2, t1, r3, qp);                                                                  \
  }
  IMM8_OR_REG_CMP(cmp_eq_imm,   cmp_eq)
  IMM8_OR_REG_CMP(cmp_ne_imm,   cmp_ne)
  IMM8_OR_REG_CMP(cmp_lt_imm,   cmp_lt)
  IMM8_OR_REG_CMP(cmp_ltu_imm,  cmp_ltu)
  IMM8_OR_REG_CMP(cmp4_eq_imm,  cmp4_eq)
  IMM8_OR_REG_CMP(cmp4_ne_imm,  cmp4_ne)
  IMM8_OR_REG_CMP(cmp4_lt_imm,  cmp4_lt)
  IMM8_OR_REG_CMP(cmp4_ltu_imm, cmp4_ltu)
#undef IMM8_OR_REG_CMP

#define IMM8_OR_REG_LOGIC(name, regform)                                                         \
  void name(Register r1, int64_t imm, Register r3, PredicateRegister qp = pTrue) {               \
    if (ia64::is_simm8(imm)) { Assembler::name(r1, imm, r3, qp); return; }                        \
    assert_different_registers(r3, t1);                                                           \
    mov_immediate(t1, imm);                                                                       \
    regform(r1, t1, r3, qp);                                                                      \
  }
  IMM8_OR_REG_LOGIC(and_imm, and_)
  IMM8_OR_REG_LOGIC(or_imm,  or_)
  IMM8_OR_REG_LOGIC(xor_imm, xor_)
#undef IMM8_OR_REG_LOGIC

  // ---- compare and branch ------------------------------------------------
  //
  // Each emits a compare into ptmp0/ptmp1 and a branch predicated on the
  // result. The "w" forms compare only the low 32 bits (cmp4), for Java ints.

  void beqz(Register r, Label& L)                 { cmp_eq(ptmp0, ptmp1, r, zr); br_cond(L, ptmp0); }
  void bnez(Register r, Label& L)                 { cmp_eq(ptmp0, ptmp1, r, zr); br_cond(L, ptmp1); }
  void beq(Register a, Register b, Label& L)      { cmp_eq(ptmp0, ptmp1, a, b);  br_cond(L, ptmp0); }
  void bne(Register a, Register b, Label& L)      { cmp_eq(ptmp0, ptmp1, a, b);  br_cond(L, ptmp1); }
  void blt(Register a, Register b, Label& L)      { cmp_lt(ptmp0, ptmp1, a, b);  br_cond(L, ptmp0); }
  void bge(Register a, Register b, Label& L)      { cmp_lt(ptmp0, ptmp1, a, b);  br_cond(L, ptmp1); }
  void bltu(Register a, Register b, Label& L)     { cmp_ltu(ptmp0, ptmp1, a, b); br_cond(L, ptmp0); }
  void bgeu(Register a, Register b, Label& L)     { cmp_ltu(ptmp0, ptmp1, a, b); br_cond(L, ptmp1); }
  void bgt(Register a, Register b, Label& L)      { blt(b, a, L); }
  void ble(Register a, Register b, Label& L)      { bge(b, a, L); }
  void bgtu(Register a, Register b, Label& L)     { bltu(b, a, L); }
  void bleu(Register a, Register b, Label& L)     { bgeu(b, a, L); }

  void beqzw(Register r, Label& L)                { cmp4_eq(ptmp0, ptmp1, r, zr); br_cond(L, ptmp0); }
  void bnezw(Register r, Label& L)                { cmp4_eq(ptmp0, ptmp1, r, zr); br_cond(L, ptmp1); }
  void beqw(Register a, Register b, Label& L)     { cmp4_eq(ptmp0, ptmp1, a, b);  br_cond(L, ptmp0); }
  void bnew(Register a, Register b, Label& L)     { cmp4_eq(ptmp0, ptmp1, a, b);  br_cond(L, ptmp1); }
  void bltw(Register a, Register b, Label& L)     { cmp4_lt(ptmp0, ptmp1, a, b);  br_cond(L, ptmp0); }
  void bgew(Register a, Register b, Label& L)     { cmp4_lt(ptmp0, ptmp1, a, b);  br_cond(L, ptmp1); }

  void j(Label& L) { br(L); }

  // ---- calls and jumps ---------------------------------------------------

  // Call a C function through its psABI descriptor. A C function pointer is
  // the address of a two-word {entry, gp} descriptor, not a code address, so a
  // plain indirect branch would land in the descriptor's own data. Measured on
  // rx2800: a callee in another DSO carries a *different* gp, so the callee's
  // gp must come from the callee's own descriptor rather than from a value
  // captured once. See FRAME-DESIGN.md section 3.
  //
  // The address form dereferences the descriptor now, at generation time --
  // its contents are fixed by the dynamic linker before any code is generated
  // -- and emits both halves as immediates (4 bundles, no loads). The register
  // form loads them at run time. Both clobber t0, gp, b6, b0, ar.pfs and every
  // caller-saved register.
  void call_c(address function_descriptor);
  void call_c(Register function_descriptor);

  // Call or jump to *generated* code (a stub, the interpreter, an nmethod) at
  // an absolute address: movl t0, mov b6, br. Never IP-relative, because the
  // target may be in another blob more than 16 MiB away, and because code is
  // often generated in a buffer and copied. Clobbers t0 and b6 (and b0 for a
  // call). These are the sequences NativeCall / NativeJump describe.
  void far_call(address entry, PredicateRegister qp = pTrue);
  void far_call(address entry, const RelocationHolder& rspec);
  void emit_static_call_stub();
  static int static_call_stub_size();
  void far_jump(address entry, PredicateRegister qp = pTrue);
  void jr(Register r, PredicateRegister qp = pTrue)    { mov_to_br(btmp, r); br_cond(btmp, qp); }
  void jalr(Register r)                                 { mov_to_br(btmp, r); br_call(breturn, btmp); }
  void ret()                                            { br_ret(breturn); }

  // Emit a {entry, gp} descriptor at the current position whose entry points
  // just past it, so that C++ can call the following generated code by
  // pointer, and return the descriptor's address -- which is what must be
  // stored wherever C++ expects a function pointer (StubRoutines::call_stub()
  // and friends). The direct analogue of PPC ELFv1's function_entry(). The gp
  // stored is libjvm's own; generated code never uses gp itself (3.4).
  address function_entry();

  // ---- frames -------------------------------------------------------------
  //
  // enter() lays down HotSpot's linkage in the shape frame_ia64.hpp describes:
  // return address (from b0) at caller_sp[-1], caller's fp at caller_sp[-2],
  // fp = caller's sp, and sp lowered by 32 -- the two linkage words plus the
  // 16-byte psABI scratch area, so the frame can call C immediately.
  // leave() undoes it and leaves the return address in b0, ready for ret().
  void enter();
  void leave();

  static int enter_frame_words() { return 4; }   // linkage + psABI scratch

  // ---- the thread's frame anchor ------------------------------------------
  //
  // Every set_last_Java_frame records a pc: an IA-64 C callee never leaves its
  // return address at last_Java_sp[-1], so a missing pc cannot be recovered
  // later (frame_ia64.cpp, JavaFrameAnchor::make_walkable).
  void set_last_Java_frame(Register last_java_sp, Register last_java_fp, Label& L, Register tmp);
  void set_last_Java_frame(Register last_java_sp, Register last_java_fp, address last_java_pc, Register tmp);
  void reset_last_Java_frame(bool clear_fp);

  // ---- calls into the VM --------------------------------------------------
  //
  // Same contract as on the other ports. Arguments go in c_rarg1.. (out1..);
  // c_rarg0 is set to the current thread. entry_point is a C function pointer,
  // i.e. a descriptor address (call_c).

  void call_VM(Register oop_result,
               address entry_point,
               bool check_exceptions = true);
  void call_VM(Register oop_result,
               address entry_point,
               Register arg_1,
               bool check_exceptions = true);
  void call_VM(Register oop_result,
               address entry_point,
               Register arg_1, Register arg_2,
               bool check_exceptions = true);
  void call_VM(Register oop_result,
               address entry_point,
               Register arg_1, Register arg_2, Register arg_3,
               bool check_exceptions = true);

  // Overloadings with last_Java_sp
  void call_VM(Register oop_result,
               Register last_java_sp,
               address entry_point,
               int number_of_arguments = 0,
               bool check_exceptions = true);
  void call_VM(Register oop_result,
               Register last_java_sp,
               address entry_point,
               Register arg_1,
               bool check_exceptions = true);
  void call_VM(Register oop_result,
               Register last_java_sp,
               address entry_point,
               Register arg_1, Register arg_2,
               bool check_exceptions = true);
  void call_VM(Register oop_result,
               Register last_java_sp,
               address entry_point,
               Register arg_1, Register arg_2, Register arg_3,
               bool check_exceptions = true);

  void get_vm_result_oop(Register oop_result, Register java_thread);
  void get_vm_result_metadata(Register metadata_result, Register java_thread);

  // These always tightly bind to MacroAssembler::call_VM_leaf_base
  // bypassing the virtual implementation
  void super_call_VM_leaf(address entry_point);
  void super_call_VM_leaf(address entry_point, Register arg_0);
  void super_call_VM_leaf(address entry_point, Register arg_0, Register arg_1);
  void super_call_VM_leaf(address entry_point, Register arg_0, Register arg_1, Register arg_2);
  void super_call_VM_leaf(address entry_point, Register arg_0, Register arg_1, Register arg_2, Register arg_3);

  void call_VM_leaf(address entry_point,
                    int number_of_arguments = 0);
  void call_VM_leaf(address entry_point,
                    Register arg_0);
  void call_VM_leaf(address entry_point,
                    Register arg_0, Register arg_1);
  void call_VM_leaf(address entry_point,
                    Register arg_0, Register arg_1, Register arg_2);

  // Branch to StubRoutines::forward_exception_entry() if the thread has a
  // pending exception. Clobbers t0, t1, ptmp0/1 and b6.
  void check_pending_exception();

 protected:
  // Implementation of call_VM; the interpreter's InterpreterMacroAssembler
  // overrides both to save and restore its own state around the call.
  virtual void call_VM_base(Register oop_result,       // where an oop-result ends up if any; use noreg otherwise
                            Register java_thread,      // the thread if computed before; use noreg otherwise
                            Register last_java_sp,     // to set up last_Java_frame in stubs; use noreg otherwise
                            address  entry_point,      // the entry point
                            int      number_of_arguments, // the number of arguments (w/o thread) to pop after the call
                            bool     check_exceptions  // whether to check for pending exceptions after return
                            );

  virtual void call_VM_leaf_base(address entry_point, int number_of_arguments, Label* retaddr = nullptr);

  void call_VM_helper(Register oop_result, address entry_point, int number_of_arguments, bool check_exceptions = true);

  // Interpreter-only hooks, called by call_VM_base after the call returns.
  // Empty here; InterpreterMacroAssembler provides the real ones.
  virtual void check_and_handle_popframe(Register java_thread) {}
  virtual void check_and_handle_earlyret(Register java_thread) {}

  // Move arg into c_rarg, unless it is already there.
  void pass_arg(Register c_rarg, Register arg) { mov(c_rarg, arg); }

 private:
  // The base register for a load into dst: the address is computed into dst
  // itself when it is not already a bare register.
  Register addr_for_load(Register dst, const Address& a) {
    if (a.getMode() == Address::base_plus_offset && a.offset() == 0) return a.base();
    lea(dst, a);
    return dst;
  }
  // The base register for a store of src (noreg for FP): tmp, unless the
  // address is already a bare register.
  Register addr_for_store(const Address& a, Register src, Register tmp) {
    if (a.getMode() == Address::base_plus_offset && a.offset() == 0) return a.base();
    assert(tmp != src, "address scratch must differ from the stored value");
    lea(tmp, a);
    return tmp;
  }

 public:
  // ---- stack banging -----------------------------------------------------
  //
  // Touches the memory stack only: generated code cannot move ar.bsp, because
  // it executes a single `alloc` in call_stub and never another
  // (FRAME-DESIGN.md section 1). So unlike the Zero port there is no register
  // backing store to probe here.
  //
  // Callers stepping through a range must step by os::vm_page_size(), which is
  // 16384 on this machine. A loop stepping by 4096 -- the figure baked into
  // several ports -- would skip three of every four guard pages and miss the
  // guard entirely.
  void bang_stack_with_offset(int offset) {
    assert(offset > 0, "must bang below the stack pointer");
    lea(t0, Address(sp, -offset));
    Assembler::st8(t0, zr);
  }

  // ---- null checks -------------------------------------------------------

  static bool needs_explicit_null_check(intptr_t offset);
  static bool uses_implicit_null_check(void* address);

  // Load through reg so that a null reg faults in the first page and the
  // signal handler turns it into a NullPointerException -- unless offset is
  // too large to land there, in which case touch [reg] explicitly first.
  void null_check(Register reg, int offset = -1);

  // ---- memory barriers ----------------------------------------------------
  //
  // IA-64 is among the most weakly ordered machines HotSpot has run on. The
  // only standalone fence is mf, which orders everything; the cheaper
  // one-directional orderings come from ld.acq / st.rel at the access itself.
  // So every membar kind other than "none" is an mf here, deliberately
  // conservative (CPU_MULTI_COPY_ATOMIC is undefined for the same reason).
  enum Membar_mask_bits {
    LoadLoad   = 1 << 0,
    LoadStore  = 1 << 1,
    StoreLoad  = 1 << 2,
    StoreStore = 1 << 3,
    AnyAny     = LoadLoad | LoadStore | StoreLoad | StoreStore
  };
  void membar(int order_constraint) { if (order_constraint != 0) mf(); }

  // ---- heap and metadata access ---------------------------------------------
  //
  // Oop accesses go through the GC's BarrierSetAssembler, as on every port.
  // Compressed oops are off in milestone 1 (FRAME-DESIGN.md 2.4); compressed
  // class pointers are supported, since decoding one is a shift and a movl'd
  // base -- IA-64's high mmap addresses do not matter to an arbitrary base.

  void access_load_at(BasicType type, DecoratorSet decorators, Register dst, Address src,
                      Register tmp1 = noreg, Register tmp2 = noreg);
  void access_store_at(BasicType type, DecoratorSet decorators, Address dst, Register val,
                       Register tmp1 = noreg, Register tmp2 = noreg, Register tmp3 = noreg);

  void load_heap_oop(Register dst, Address src, Register tmp1 = noreg,
                     Register tmp2 = noreg, DecoratorSet decorators = 0);
  void load_heap_oop_not_null(Register dst, Address src, Register tmp1 = noreg,
                              Register tmp2 = noreg, DecoratorSet decorators = 0);
  void store_heap_oop(Address dst, Register val, Register tmp1 = noreg,
                      Register tmp2 = noreg, Register tmp3 = noreg, DecoratorSet decorators = 0);
  // Store a null; no barrier needed for the card table.
  void store_heap_oop_null(Address dst) { access_store_at(T_OBJECT, IN_HEAP, dst, noreg); }

  // result = *result, for an OopHandle.
  void resolve_oop_handle(Register result, Register tmp1 = noreg, Register tmp2 = noreg);
  // value = the oop a (possibly tagged, possibly null) jobject refers to.
  void resolve_jobject(Register value, Register tmp1 = noreg, Register tmp2 = noreg);

  void load_method_holder(Register holder, Register method);
  void load_mirror(Register dst, Register method, Register tmp1 = noreg, Register tmp2 = noreg);

  // dst = src->klass(). Clobbers t0 when class pointers are compressed with a
  // non-zero base.
  void load_klass(Register dst, Register src);
  void decode_klass_not_null(Register r);
  void encode_klass_not_null(Register dst, Register src);   // clobbers t1 if base != 0
  void tlab_allocate(Register obj, Register var_size_in_bytes, int con_size_in_bytes,
                     Register tmp1, Register tmp2, Label& slow_case);
  void lightweight_lock(Register basic_lock, Register obj, Register tmp1, Register tmp2, Register tmp3, Label& slow);
  void lightweight_unlock(Register obj, Register tmp1, Register tmp2, Register tmp3, Label& slow);

  // Branch to L_success if sub_klass is a subtype of super_klass, else fall
  // through. The fast path checks the primary-supers display and the
  // secondary-super cache; the slow path scans the secondary supers linearly
  // and updates the cache on a hit. Clobbers tmp1, tmp2, t0, t1, ptmp0/1.
  void check_klass_subtype(Register sub_klass, Register super_klass,
                           Register tmp1, Register tmp2, Label& L_success);
  void check_klass_subtype_fast_path(Register sub_klass, Register super_klass, Register tmp,
                                     Label* L_success, Label* L_failure, Label* L_slow_path);
  void check_klass_subtype_slow_path(Register sub_klass, Register super_klass,
                                     Register tmp1, Register tmp2,
                                     Label* L_success, Label* L_failure);

  // method_result = recv_klass->vtable()[vtable_index].method().
  void lookup_virtual_method(Register recv_klass, Register vtable_index, Register method_result);

  // Find intf_klass among recv_klass's itable entries; on a miss branch to
  // L_no_such_interface. With return_method, also load the method at
  // itable_index (a register, which must be method_result) into
  // method_result; recv_klass is then destroyed. Clobbers scan_tmp and t0.
  void lookup_interface_method(Register recv_klass, Register intf_klass, Register itable_index,
                               Register method_result, Register scan_tmp,
                               Label& L_no_such_interface, bool return_method = true);

  // Thread-local safepoint poll. at_return compares against the stack
  // watermark (fp, or sp in an nmethod) instead of testing the poll bit.
  void safepoint_poll(Label& slow_path, bool at_return, bool acquire, bool in_nmethod,
                      Register tmp = t1);

  // ---- floating-point division ---------------------------------------------
  //
  // IA-64 has no divide instruction. dst = a / b, correctly rounded to IEEE
  // single / double as Java requires: frcpa's reciprocal approximation refined
  // by Newton-Raphson on sf1, then one rounding step on sf0. These are GCC's
  // maximum-throughput sequences (gcc/config/ia64/div.md, divsf3_internal_thr
  // and divdf3_internal_thr), i.e. Intel's published IEEE-correct algorithms;
  // special operands (frcpa clears ptmp0) take frcpa's own result. dst may be
  // a or b. Clobbers f10-f13 (fdiv_s) or f10-f14 (fdiv_d) and ptmp0.
  void fdiv_s(FloatRegister dst, FloatRegister a, FloatRegister b);
  void fdiv_d(FloatRegister dst, FloatRegister a, FloatRegister b);

  // ---- debugging -----------------------------------------------------------

  void should_not_reach_here() { stop("should not reach here"); }
  // Used by the shared TemplateTable for bytecodes a port has not provided.
  void unimplemented(const char* what = "");
  // Trap with a message: a break.b with stop_break_imm, followed by a data
  // bundle holding the message pointer. The SIGILL handler
  // (os_linux_ia64.cpp) recognises the bundle with is_stop() and reports the
  // message.
  void stop(const char* msg);

  // A break.b's immediate never reaches the kernel: measured on rx2800
  // (tools/gate/breakprobe.c), every break.b arrives as SIGILL/ILL_ILLOPC,
  // the same as break 0 (GCC's __builtin_trap). The SIGILL handler therefore
  // recognises a stop by the bundle at the pc (is_stop), never by si_code;
  // the immediate only marks the bundle. (break.m immediates are reported:
  // [0x40000, 0x80000) as SIGILL/__ILL_BREAK, >= 0x80000 as SIGTRAP, and
  // 0x100000 is the system-call break -- never emit it.)
  static const uint32_t stop_break_imm = 0x40000 | 0x5709;
  // pc may carry a slot number in its low bits, as the kernel reports it.
  static bool is_stop(address pc);
  static const char* stop_message(address pc);

  void verify_oop(Register reg, const char* s = "broken oop") {}
  void verify_oop_msg(Register reg, const char* msg) {}

  static void debug64(char* msg, int64_t pc, int64_t regs[]);
};

#ifdef ASSERT
// inst_mark() is only used for relocation bookkeeping, never checked here.
inline bool AbstractAssembler::pd_check_instruction_mark() { return false; }
#endif

#endif // CPU_IA64_MACROASSEMBLER_IA64_HPP
