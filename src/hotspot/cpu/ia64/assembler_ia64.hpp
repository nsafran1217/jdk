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

#ifndef CPU_IA64_ASSEMBLER_IA64_HPP
#define CPU_IA64_ASSEMBLER_IA64_HPP

#include "asm/register.hpp"
#include "assembler_ia64_core.hpp"
#include "utilities/align.hpp"
#include "utilities/powerOfTwo.hpp"

// ---------------------------------------------------------------------------
// Register roles. See FRAME-DESIGN.md section 2.2 for the full rationale.
//
// The governing constraint is that IA-64 has only *four* preserved static
// general registers, r4-r7 -- against twelve on riscv, ten on aarch64, six on
// x86-64. Every other port keeps its interpreter state in callee-saved
// registers and gets it back from a C call for free. Here only the four
// hottest can live there; everything else is caller-saved and must be reloaded
// from the frame after any call that crosses into C++, the way x86-32 does.
// ---------------------------------------------------------------------------

// ABI-fixed.
constexpr Register zr           = r0;    // hardwired zero
constexpr Register gp           = r1;    // global pointer; scratch here (3.4)
constexpr Register sp           = r12;   // stack pointer, 16-byte aligned
constexpr Register tp           = r13;   // thread pointer -- ABI reserved

// MacroAssembler scratch. Two are not enough for the multi-instruction
// sequences (large immediates, address synthesis, CAS retry loops), so four
// are reserved rather than taken from the allocatable set: the register
// allocator has no way to know a masm helper clobbered one.
constexpr Register t0           = r2;
constexpr Register t1           = r3;
constexpr Register t2           = r9;
constexpr Register t3           = r10;
constexpr Register t4           = r11;

// Preserved across a C call -- the only four the architecture gives us.
//
// fp is HotSpot's frame linkage (frame::fp(), the link slot, last_Java_fp),
// not an ABI frame pointer: IA-64 unwinding is table-driven and C code keeps
// no fp chain. It must be preserved because generated code reaches every
// other piece of interpreter state through it once a C call has clobbered
// the scratch registers. SpiderMonkey's IA-64 backend makes the same choice
// (FramePointer = r4). See FRAME-DESIGN.md section 2.2.
constexpr Register fp           = r4;    // frame pointer (HotSpot linkage)
constexpr Register Rthread      = r5;    // current JavaThread
constexpr Register Rbcp         = r6;    // bytecode pointer
constexpr Register Resp         = r7;    // Java expression stack pointer

// Caller-saved: reloaded from the frame after every VM call. Rlocals is
// constant for the life of a frame and lives in the locals slot, so it costs
// one fp-relative load to get back; the interpreter's call_VM_base and
// call_VM_leaf_base do that unconditionally.
constexpr Register Rlocals      = r19;   // locals base
constexpr Register Rmethod      = r14;
constexpr Register Rcpool       = r15;
constexpr Register Rsender_sp   = r18;
// r16 and r17 were Rmonitors and Rdispatch in the first design; neither earns
// a register. The monitor block top lives in the frame (as on riscv), and a
// dispatch table's address is a constant -- safepoints copy table *contents*
// into the active table -- so one movl rematerialises it with no load.
// FRAME-DESIGN.md 9.3, now closed.

// The interpreter's cached top-of-stack: r8 / f8, the psABI integer and FP
// return registers, so a C or Java call's result lands where the template
// expects it.
constexpr Register      Rtos  = r8;
constexpr FloatRegister Ftos  = f8;

// Exception dispatch (forward_exception, catch_exception, the interpreter's
// throw entries, the exception blob): the exception oop in r8, the throwing
// pc in r28. riscv uses x10/x13 the same way; r28 is chosen because it is
// neither an argument register nor MacroAssembler scratch.
constexpr Register Rexception    = r8;
constexpr Register Rexception_pc = r28;

// Return values. r8 also carries the buffer address for a large aggregate
// return, which does not consume out0.
constexpr Register Rret         = r8;

// The outgoing-argument window created by call_stub's single `alloc`. The
// psABI numbers arguments positionally 1-8; each position has both a GR slot
// here and an FP slot in f8-f15, and an argument consumes *both* whichever it
// uses. See ABIArgGenerator-equivalent handling in sharedRuntime_ia64.
constexpr Register c_rarg0 = out0;
constexpr Register c_rarg1 = out1;
constexpr Register c_rarg2 = out2;
constexpr Register c_rarg3 = out3;
constexpr Register c_rarg4 = out4;
constexpr Register c_rarg5 = out5;
constexpr Register c_rarg6 = out6;
constexpr Register c_rarg7 = out7;

constexpr FloatRegister c_farg0 = f8;
constexpr FloatRegister c_farg1 = f9;
constexpr FloatRegister c_farg2 = f10;
constexpr FloatRegister c_farg3 = f11;
constexpr FloatRegister c_farg4 = f12;
constexpr FloatRegister c_farg5 = f13;
constexpr FloatRegister c_farg6 = f14;
constexpr FloatRegister c_farg7 = f15;

// Java arguments in compiled code. Kept in the caller-saved static registers
// so that they survive a Java-to-Java call without touching r4-r7.
constexpr Register j_rarg0 = r20;
constexpr Register j_rarg1 = r21;
constexpr Register j_rarg2 = r22;
constexpr Register j_rarg3 = r23;
constexpr Register j_rarg4 = r24;
constexpr Register j_rarg5 = r25;
constexpr Register j_rarg6 = r26;
constexpr Register j_rarg7 = r27;

constexpr FloatRegister j_farg0 = f8;
constexpr FloatRegister j_farg1 = f9;
constexpr FloatRegister j_farg2 = f10;
constexpr FloatRegister j_farg3 = f11;
constexpr FloatRegister j_farg4 = f12;
constexpr FloatRegister j_farg5 = f13;
constexpr FloatRegister j_farg6 = f14;
constexpr FloatRegister j_farg7 = f15;

class Argument {
 public:
  enum {
    // The psABI numbers C arguments positionally, 1-8: each position has both
    // a GR slot (out0-out7) and an FR slot (f8-f15), and an argument consumes
    // the position whichever kind it uses. So there are 8 register positions
    // in total, not 8 + 8 -- c_calling_convention must count them jointly.
    n_int_register_parameters_c   = 8,  // out0 ... out7 (c_rarg0, c_rarg1, ...)
    n_float_register_parameters_c = 8,  // f8 ... f15    (c_farg0, c_farg1, ...)

    // The Java convention is this port's own choice (FRAME-DESIGN.md 6.2) and
    // is NOT positional: integer and FP arguments are counted independently.
    // SharedRuntime::java_calling_convention must agree with these counts;
    // shared code (signature.cpp) uses them to size the stack arguments.
    n_int_register_parameters_j   = 8,  // r20 ... r27   (j_rarg0, j_rarg1, ...)
    n_float_register_parameters_j = 8   // f8 ... f15    (j_farg0, j_farg1, ...)
  };
};

// FP scratch. f2-f5 are reserved separately as internal temporaries for the
// multi-step divide, sqrt and 64x64 multiply sequences and are never handed
// out; f6/f7 are the general-purpose pair.
constexpr FloatRegister ftmp0 = f6;
constexpr FloatRegister ftmp1 = f7;

// Predicate scratch. p0 is hardwired true, which is what makes an
// unpredicated instruction simply "predicated on p0".
constexpr PredicateRegister pTrue = p0;
constexpr PredicateRegister ptmp0 = p6;
constexpr PredicateRegister ptmp1 = p7;
constexpr PredicateRegister ptmp2 = p8;
constexpr PredicateRegister ptmp3 = p9;

// Branch registers. b0 is the return pointer written by br.call; b6 is the
// indirect branch and call target throughout.
constexpr BranchRegister breturn = b0;
constexpr BranchRegister btmp    = b6;

// Size of one instruction bundle. Every emitted instruction occupies exactly
// one, so this is also the instruction size everywhere in this port.
const int BytesPerBundle = 16;

// ---------------------------------------------------------------------------
// Address
//
// IA-64 has no displacement addressing: ld8/st8 take a bare register. So an
// Address is not something the hardware understands -- it is a request that
// MacroAssembler materialise base+offset into a scratch register before the
// access. Keeping the abstraction anyway is what lets the interpreter and C1
// be written in the same shape as the other ports.
// ---------------------------------------------------------------------------

class Address {
 public:
  enum mode { no_mode, base_plus_offset, literal };

 private:
  Register _base;
  int64_t  _offset;
  enum mode _mode;

  RelocationHolder _rspec;
  // If the target is far away or not yet known, the address is materialised
  // from a 64-bit literal instead of from a base register.
  address  _target;

 public:
  Address()
    : _base(noreg), _offset(0), _mode(no_mode), _target(nullptr) {}

  // explicit: st8(Register, Register) and st8(Address, Register) must never be
  // confused by an implicit conversion -- they take their operands in the
  // same order but mean different things if one is silently an Address.
  explicit Address(Register base, int64_t offset = 0)
    : _base(base), _offset(offset), _mode(base_plus_offset), _target(nullptr) {}
  Address(Register base, ByteSize offset)
    : _base(base), _offset(in_bytes(offset)), _mode(base_plus_offset), _target(nullptr) {}

  Address(address target, const RelocationHolder& rspec)
    : _base(noreg), _offset(0), _mode(literal), _rspec(rspec), _target(target) {}

  Register base()   const { assert(_mode == base_plus_offset, "wrong mode"); return _base; }
  int64_t  offset() const { assert(_mode == base_plus_offset, "wrong mode"); return _offset; }
  address  target() const { assert(_mode == literal, "wrong mode"); return _target; }
  enum mode getMode() const { return _mode; }

  const RelocationHolder& rspec() const { return _rspec; }

  // True when the offset fits the 14-bit signed immediate of `adds`, i.e. when
  // materialising the address costs one instruction rather than three.
  bool offset_is_simm14() const {
    return _mode == base_plus_offset && ia64::is_simm14(_offset);
  }
};

// ---------------------------------------------------------------------------
// Assembler
//
// A thin, typed layer over assembler_ia64_core.hpp. Its only jobs are to turn
// Register/FloatRegister objects into encodings and to push finished bundles
// into the CodeBuffer; all encoding knowledge lives in the core header, which
// is differential-tested against GNU as by tools/enc-difftest.sh.
// ---------------------------------------------------------------------------

class Assembler : public AbstractAssembler {
 private:
  // Counts every br.call emitted, and every write of b7 -- the events after
  // which a branch-register value computed earlier can no longer be trusted
  // (a call clobbers the scratch branch registers b6/b7). The interpreter's
  // early dispatch (dispatch_prolog/epilog) loads the next handler into b7
  // and uses it only if this count has not moved since.
  int _branch_reg_epoch = 0;

 public:
  Assembler(CodeBuffer* code) : AbstractAssembler(code) {}

  int branch_reg_epoch() const { return _branch_reg_epoch; }

  // Every instruction is a whole bundle, so instruction length is constant.
  static unsigned int instr_len(unsigned char* instr) { return BytesPerBundle; }
  static unsigned int instr_maxlen()                  { return BytesPerBundle; }

  // The instruction after the one at |inst|. A pc from a signal context may
  // carry a slot number in its low bits; one instruction per bundle makes the
  // next instruction simply the next bundle.
  static address locate_next_instruction(address inst) {
    return align_down(inst, BytesPerBundle) + BytesPerBundle;
  }

  // Resolve a label reference recorded at |branch| now that the label is
  // bound to |target|. Two kinds of site are ever registered:
  //   - an IP-relative branch or call in slot 2 of an MIB bundle, or of an
  //     MIB, MMB or MFB bundle it was packed into (pack_branch_slot);
  //   - the movl of an la() sequence (see below), an MLX bundle, which holds
  //     target minus the address of the `mov r = ip` bundle just before it.
  // pd_patch_instruction tells them apart by bundle template.
  void pd_patch_instruction(address branch, address target, const char* file, int line);

  // ---- raw bundle emission ------------------------------------------------

 private:
  void emit_raw_bundle(ia64::Bundle b) {
    assert(is_aligned(AbstractAssembler::offset(), BytesPerBundle),
           "code position must stay bundle-aligned");
    // Little-endian target, so emitting lo then hi lays the 128-bit bundle out
    // exactly as the hardware reads it.
    AbstractAssembler::emit_int64(b.lo);
    AbstractAssembler::emit_int64(b.hi);
  }

 public:
  // ---- instruction groups: stop elision (BUNDLING.md, Stage 1) -----------
  //
  // Every bundle holds one instruction and is emitted with a stop after it.
  // When the *next* instruction neither reads nor writes a register written
  // since the last stop, the previous bundle's stop is cleared in place, so the
  // two share an instruction group and can issue together. Layout never
  // changes: same sizes, addresses, labels and patch sites.
  //
  // The rule is SDM vol. 1 3.4 (1:40-1:43), applied as conservatively as LLVM's
  // IA64Bundling pass: no RAW or WAW on any register within a group (WAR is
  // allowed; memory dependencies are allowed in program order). r0, p0, f0 and
  // f1 are exempt. A qualifying predicate is a read. One special case is used:
  // a branch may read a predicate written by a non-FP instruction, and a branch
  // register, from its own group (1:42).
  //
  // ar.ccv is described as a pseudo-register (BR class, number 65: there are
  // only 8 branch registers), so a cmpxchg packs with the work around it and
  // only its RAW on the mov to ar.ccv needs a stop.
  //
  // Anything not described by a Deps -- other implicit resources (ar.unat,
  // ar.pfs/CFM, alloc, calls, mf, break), raw bundles, movl -- is a barrier:
  // its stop stays and the group restarts after it. Bundles that code
  // recognisers or patchers inspect by exact encoding (movl, mov to a branch
  // register, branches) always keep their own stop; only plain M/I/F bundles
  // ever lose one. Checked by tools/depcheck.py (as -xexplicit).
  struct Deps {
    enum Cls : uint8_t { GR = 0, FR = 1, PR = 2, BR = 3 };
    uint8_t  nrd = 0, nwr = 0;
    uint8_t  rd_cls[6], rd_num[6], rd_pos[6];
    uint8_t  wr_cls[3], wr_num[3], wr_pos[3];   // pos: bit of a 7-bit GR field, 0 if none
    bool     is_branch = false;   // see the special case above
    bool     fp_pred   = false;   // writes predicates as an FP instruction
    bool     keep_stop = false;   // this bundle keeps its own stop
    Deps& rd(Cls c, int n, int pos = 0) { assert(nrd < 6, "deps"); rd_cls[nrd] = c; rd_pos[nrd] = (uint8_t)pos; rd_num[nrd++] = (uint8_t)n; return *this; }
    Deps& wr(Cls c, int n, int pos = 0) { assert(nwr < 3, "deps"); wr_cls[nwr] = c; wr_pos[nwr] = (uint8_t)pos; wr_num[nwr++] = (uint8_t)n; return *this; }
    Deps& r(Register x)          { return x->encoding() == 0 ? *this : rd(GR, x->encoding()); }
    Deps& w(Register x)          { return wr(GR, x->encoding()); }
    // The same, naming the instruction's field (r1 6, r2 13, r3 20) that
    // holds the register, so the scheduling window may rename it (UseC2RenamePool).
    // Only for a field that holds nothing else (not a post-increment base).
    Deps& w6(Register x)         { return wr(GR, x->encoding(), 6); }
    Deps& r13(Register x)        { return x->encoding() == 0 ? *this : rd(GR, x->encoding(), 13); }
    Deps& r20(Register x)        { return x->encoding() == 0 ? *this : rd(GR, x->encoding(), 20); }
    Deps& r(FloatRegister x)     { return x->encoding() <= 1 ? *this : rd(FR, x->encoding()); }
    Deps& w(FloatRegister x)     { return wr(FR, x->encoding()); }
    Deps& r(PredicateRegister x) { return x.encoding() == 0 ? *this : rd(PR, x.encoding()); }
    Deps& w(PredicateRegister x) { return x.encoding() == 0 ? *this : wr(PR, x.encoding()); }
    Deps& r(BranchRegister x)    { return rd(BR, x.encoding()); }
    Deps& w(BranchRegister x)    { return wr(BR, x.encoding()); }
    static const int kCcv = 65;
    Deps& r_ccv()                { return rd(BR, kCcv); }
    Deps& w_ccv()                { return wr(BR, kCcv); }
    Deps& branch()               { is_branch = true; return *this; }
    Deps& fp_predicate()         { fp_pred = true; return *this; }
    Deps& keep()                 { keep_stop = true; return *this; }
  };
  static Deps D() { return Deps(); }

 private:
  uint64_t     _pend[4][2] = {};         // registers written in the open group
  uint64_t     _pend_fp_pr = 0;          // ... of which predicates by FP insns
  CodeSection* _last_sect  = nullptr;    // the bundle whose stop may be cleared
  int          _last_off   = -1;

  bool pending(int c, int n) const { return (_pend[c][n >> 6] >> (n & 63)) & 1; }
  void clear_pending() { memset(_pend, 0, sizeof(_pend)); _pend_fp_pr = 0; }
  bool conflicts(const Deps& d) const {
    for (int i = 0; i < d.nrd; i++) {
      int c = d.rd_cls[i], n = d.rd_num[i];
      if (!pending(c, n)) continue;
      if (d.is_branch && (c == Deps::BR || (c == Deps::PR && !((_pend_fp_pr >> n) & 1)))) continue;
      return true;                                    // RAW
    }
    for (int i = 0; i < d.nwr; i++) {
      if (pending(d.wr_cls[i], d.wr_num[i])) return true;   // WAW
    }
    return false;
  }
  void add_pending(const Deps& d) {
    for (int i = 0; i < d.nwr; i++) {
      int c = d.wr_cls[i], n = d.wr_num[i];
      _pend[c][n >> 6] |= (uint64_t)1 << (n & 63);
      if (c == Deps::PR && d.fp_pred) _pend_fp_pr |= (uint64_t)1 << n;
    }
  }

 public:
  // Emit a whole single-instruction bundle. |d| == nullptr makes it a
  // barrier. Closes any open packed bundle first.
  void emit_bundle(ia64::Bundle b, const Deps* d = nullptr) {
    flush_window();
    finalize_open_bundle();
    _ob_open = false;
    if (d != nullptr && UseStopElision &&
        _last_sect == code_section() && _last_off == raw_offset() - BytesPerBundle &&
        !conflicts(*d)) {
      // Join the open group: clear the stop at the end of the previous bundle.
      address prev = code_section()->start() + _last_off;
      assert((*prev & 1) == 1, "previous bundle must still end in a stop");
      *prev &= ~1;
    } else {
      clear_pending();
    }
    if (d != nullptr) {
      add_pending(*d);
    }
    emit_raw_bundle(b);
    if (d != nullptr && !d->is_branch && !d->keep_stop) {
      _last_sect = code_section();
      _last_off  = raw_offset() - BytesPerBundle;
    } else {
      _last_sect = nullptr;               // its stop stays: the next instruction
      _last_off  = -1;                    // starts a new group
      clear_pending();
    }
  }

  // ---- bundle packing (BUNDLING.md, Stage 2) -------------------------------
  //
  // Inside a PackScope (UseBundlePacking), M, I, A and F instructions with a
  // Deps are packed up to three to a bundle. The open bundle's address is
  // fixed when it opens -- its 16 bytes are emitted at once and rewritten as
  // instructions join -- so nothing already handed out ever moves. Slots keep
  // program order; a RAW/WAW dependency on the open group needs a stop, which
  // goes inside the bundle (MI_I, M_MI) or ends it.
  //
  // Correct by construction rather than by audit: every way of observing the
  // code position -- pc(), offset(), bind(), relocate(), data emission,
  // switching sections -- closes the open bundle first (the hiding overloads
  // below), so whatever the caller records names the start of a fresh bundle,
  // and the next instruction opens it. Branches, movl and barriers always
  // close the open bundle and take a bundle of their own, so call sequences,
  // patch sites and every shape a NativeInstruction recognises are unchanged
  // -- except that br_cond to a label may take slot 2 of the open bundle
  // (UseBranchPacking), its patch site then being that bundle.
  // U_L: a movl in the scheduling window (movl_const), packed as an MLX bundle.
  enum Unit : uint8_t { U_M, U_I, U_A, U_F, U_L };

  void emit_m(ia64::Insn i, const Deps& d) { emit_unit(U_M, i, d); }
  void emit_i(ia64::Insn i, const Deps& d) { emit_unit(U_I, i, d); }
  void emit_a(ia64::Insn i, const Deps& d) { emit_unit(U_A, i, d); }
  void emit_f(ia64::Insn i, const Deps& d) { emit_unit(U_F, i, d); }
  void emit_b(ia64::Insn i, const Deps& d) { emit_bundle(ia64::BundleB(i), &d); }
  // Barriers: forms with implicit resources or not yet described.
  void emit_m(ia64::Insn i) { emit_bundle(ia64::BundleM(i)); }
  void emit_i(ia64::Insn i) { emit_bundle(ia64::BundleI(i)); }
  void emit_f(ia64::Insn i) { emit_bundle(ia64::BundleF(i)); }
  void emit_b(ia64::Insn i) { emit_bundle(ia64::BundleB(i)); }

  // Packing on (depth > 0) or off within a scope.
  class PackScope {
    Assembler* _a; int _saved;
   public:
    PackScope(Assembler* a, bool enable = true) : _a(a), _saved(a->_pack_depth) {
      a->close_bundle();
      a->_pack_depth = (enable && UseBundlePacking) ? _saved + 1 : 0;
    }
    ~PackScope() { _a->close_bundle(); _a->_pack_depth = _saved; }
  };
  // A fixed-shape sequence (call cells, patch sites, recognised stubs): no
  // packing inside, whatever the caller allows.
  class NoPackScope : public PackScope {
   public:
    NoPackScope(Assembler* a) : PackScope(a, false) {}
  };

  void close_bundle() { flush_window(); finalize_open_bundle(); _ob_open = false; }

  // Packing for everything this assembler emits outside explicit scopes
  // (C1_MacroAssembler turns it on for compiled code).
  void set_pack_default(bool on) {
    close_bundle();
    _pack_depth = (on && UseBundlePacking) ? 1 : 0;
  }

  // Position observers: close the open bundle first (see above).
  address pc()  { close_bundle(); return AbstractAssembler::pc(); }
  int offset() {
    if (_offset_keeps_bundle) {
      // C2 emission (UseC2BundlePacking): the open bundle's 16 bytes are
      // already in the buffer, so the raw offset is past it and names where
      // the next bundle starts. Anything that must start a bundle at a
      // recorded offset -- labels, branches, relocations, barriers, memory
      // nodes, call shapes -- closes the open bundle and starts exactly
      // there; packable instructions of the next node may still join it.
      //
      // Under UseC2CrossNodeScheduling the scheduling window survives too,
      // so the next node's instructions are scheduled together with this
      // one's, and the offset returned is only a lower bound for where this
      // node's code ends. PhaseOutput::fill_buffer calls
      // CodeBuffer::flush_bundle before the offsets it needs exactly (calls,
      // safepoints, padding), which makes this offset() flush the window;
      // implicit null checks take the faulting access's own position
      // (C2_MacroAssembler::implicit_null_check_offset); anything else that
      // needs an exact position uses exact_offset(), pc(), or closes the
      // bundle first.
      bool flush_requested = code()->pd_take_flush_request();
      if (!UseC2CrossNodeScheduling || flush_requested) {
        flush_window();
      }
      return AbstractAssembler::offset();
    }
    close_bundle();
    return AbstractAssembler::offset();
  }
  // The offset where the next instruction goes, with every instruction
  // emitted so far in the buffer (the open bundle may still take more in
  // C2 emission, see offset()).
  int exact_offset() { flush_window(); return offset(); }
  // Whether instructions of one C2 node may be emitted while a later node is
  // (UseC2CrossNodeScheduling), so per-node emitted sizes mean nothing.
  bool defers_instructions() const { return _offset_keeps_bundle && UseC2CrossNodeScheduling; }
  // See offset(). Set for the whole of a C2 compilation's emission.
  void set_offset_keeps_bundle(bool on) { close_bundle(); _offset_keeps_bundle = on; }
  // A label also ends the instruction group, keeping the stop before it.
  // Continuing the group across it would be legal (a taken branch to it
  // starts a new group anyway), but then the label's first instructions
  // carry the fall-through path's dependencies, and at a loop head that
  // puts a stop inside every iteration instead of one before the loop.
  void bind(Label& L) { close_bundle(); end_group(); AbstractAssembler::bind(L); }
  void end_group() { _last_sect = nullptr; _last_off = -1; clear_pending(); }
  void relocate(RelocationHolder const& rspec, int format = 0) {
    close_bundle(); AbstractAssembler::relocate(rspec, format);
  }
  void relocate(relocInfo::relocType rtype, int format = 0) {
    close_bundle(); AbstractAssembler::relocate(rtype, format);
  }
  void emit_int8(int8_t x)   { close_bundle(); AbstractAssembler::emit_int8(x); }
  void emit_int16(int16_t x) { close_bundle(); AbstractAssembler::emit_int16(x); }
  void emit_int32(int32_t x) { close_bundle(); AbstractAssembler::emit_int32(x); }
  void emit_int64(int64_t x) { close_bundle(); AbstractAssembler::emit_int64(x); }
  address start_a_stub(int required_space) { close_bundle(); return AbstractAssembler::start_a_stub(required_space); }
  void end_a_stub() { close_bundle(); AbstractAssembler::end_a_stub(); }

 private:
  int raw_offset() const { return AbstractAssembler::offset(); }

  int          _pack_depth = 0;
  bool         _offset_keeps_bundle = false;
  bool         _ob_open    = false;      // the last bundle can take more
  CodeSection* _ob_sect    = nullptr;
  int          _ob_off     = -1;
  int          _ob_n       = 0;
  ia64::Insn   _ob_insn[3];
  Unit         _ob_unit[3];
  bool         _ob_stop[3];              // a stop precedes instruction k
  ia64::Template _ob_br_tmpl = ia64::tMIB; // see pack_branch_slot
  int          _ob_br_first = 0;

  static bool fits(Unit u, char slot) {
    switch (u) {
      case U_M: return slot == 'M';
      case U_I: return slot == 'I';
      case U_A: return slot == 'M' || slot == 'I';
      case U_F: return slot == 'F';
      case U_L: return false;
    }
    return false;
  }

  // Find a template and increasing slots for n instructions (program order),
  // honouring required stops with the template's internal stop. While the
  // bundle is open, prefers the placement that leaves the most room, then
  // the fewest stops; |final| (the bundle is closing) drops the room
  // criterion. Under UseDispersalTemplates ties then go to the template with
  // the fewest I slots, then F slots: Itanium 2 disperses every syllable,
  // nops included, to a port of its slot's type, and has four M ports but
  // only two I and two F ports, so two MII bundles can never issue in the
  // same cycle while two MMI or MMF bundles can.
  static bool place(int n, const Unit* u, const bool* stop, ia64::Template* tmpl, int* pos,
                    bool final = false) {
    struct T { ia64::Template t; const char* slots; int gb; int icost; int fcost; };
    static const T ts[] = {
      { ia64::tMII,  "MII", 0, 2, 0 }, { ia64::tMMI, "MMI", 0, 1, 0 }, { ia64::tMFI, "MFI", 0, 1, 1 },
      { ia64::tMMF,  "MMF", 0, 0, 1 }, { ia64::tMI_I, "MII", 2, 2, 0 }, { ia64::tM_MI, "MMI", 1, 1, 0 }
    };
    // Increasing slot tuples, for n = 1, 2, 3.
    static const int tuples1[3][3] = { {0}, {1}, {2} };
    static const int tuples2[3][3] = { {0, 1}, {0, 2}, {1, 2} };
    static const int tuples3[1][3] = { {0, 1, 2} };
    const int (*tuples)[3] = (n == 1) ? tuples1 : (n == 2) ? tuples2 : tuples3;
    const int ntuples = (n == 3) ? 1 : 3;
    const bool disp = UseDispersalTemplates;
    // Lexicographic score: room (unless final), stops, I slots, F slots.
    int best[4] = { 99, 99, 99, 99 };
    bool found = false;
    for (const T& t : ts) {
      for (int i = 0; i < ntuples; i++) {
        const int* p = tuples[i];
        bool ok = true;
        for (int k = 0; k < n && ok; k++) ok = fits(u[k], t.slots[p[k]]);
        for (int k = 1; k < n && ok; k++) {
          if (stop[k]) ok = (t.gb != 0 && p[k - 1] < t.gb && t.gb <= p[k]);
        }
        if (!ok) continue;
        int score[4] = { final ? 0 : p[n - 1], (t.gb != 0) ? 1 : 0,
                         disp ? t.icost : 0, disp ? t.fcost : 0 };
        bool better = false;
        for (int k = 0; k < 4; k++) {
          if (score[k] != best[k]) { better = score[k] < best[k]; break; }
        }
        if (better) {
          for (int k = 0; k < 4; k++) best[k] = score[k];
          found = true;
          *tmpl = t.t;
          for (int k = 0; k < n; k++) pos[k] = p[k];
        }
      }
    }
    return found;
  }

  // The open bundle is about to close: re-pick its template for its final
  // contents, which may no longer need the room kept for later instructions.
  // Must run while the bundle still ends in its own stop.
  void finalize_open_bundle() {
    if (!_ob_open || !UseDispersalTemplates || _ob_sect == nullptr) return;
    int pos[3]; ia64::Template tmpl;
    if (place(_ob_n, _ob_unit, _ob_stop, &tmpl, pos, true)) {
      write_open_bundle(tmpl, pos);
    }
  }

  // Put an IP-relative branch into slot 2 of the open bundle, as MIB, MMB or
  // MFB, if its instructions fit slots 0-1 in order with no stop between
  // them and the branch needs no stop before it (a compare's predicate is
  // visible to a branch in its own group). The bundle then closes, keeping
  // its stop. Returns the bundle's address, or nullptr if it did not fit.
  address pack_branch_slot(const Deps& d) {
    if (!UseBranchPacking || _pack_depth == 0 || !_ob_open || _ob_n > 2 ||
        _ob_sect != code_section() || _ob_off != raw_offset() - BytesPerBundle ||
        conflicts(d)) {
      return nullptr;
    }
    if (_ob_n == 2 && _ob_stop[1]) return nullptr;
    struct T { ia64::Template t; const char* slots; int icost; int fcost; };
    static const T ts[] = {
      { ia64::tMMB, "MMB", 0, 0 }, { ia64::tMIB, "MIB", 1, 0 }, { ia64::tMFB, "MFB", 0, 1 }
    };
    // Two instructions take slots 0 and 1; one takes slot 0 if it can (an
    // M slot), else slot 1.
    for (int first = 0; first + _ob_n <= 2; first++) {
      for (const T& t : ts) {
        bool ok = true;
        for (int k = 0; k < _ob_n && ok; k++) ok = fits(_ob_unit[k], t.slots[first + k]);
        if (ok) {
          _ob_br_tmpl = t.t;
          _ob_br_first = first;
          return _ob_sect->start() + _ob_off;
        }
      }
    }
    return nullptr;
  }
  // Second half of pack_branch_slot, once the branch is encoded.
  void write_branch_slot(ia64::Insn br) {
    ia64::Insn slot[3] = { ia64::NopM(), ia64::NopI(), br };
    for (int k = 0; k < _ob_n; k++) slot[_ob_br_first + k] = _ob_insn[k];
    ia64::Bundle b = ia64::MakeBundle((ia64::Template)(_ob_br_tmpl | 1), slot[0], slot[1], slot[2]);
    address p = _ob_sect->start() + _ob_off;
    *(uint64_t*)p = b.lo;
    *(uint64_t*)(p + 8) = b.hi;
    _ob_open = false;
    end_group();                        // the branch keeps its stop
  }

  // Encode the open bundle in place; it always ends in a stop until a later
  // instruction continues its group into the next bundle.
  void write_open_bundle(ia64::Template tmpl, const int* pos) {
    ia64::Insn slot[3] = { ia64::NopM(), ia64::NopI(), ia64::NopI() };
    for (int k = 0; k < _ob_n; k++) slot[pos[k]] = _ob_insn[k];
    ia64::Bundle b = ia64::MakeBundle((ia64::Template)(tmpl | 1), slot[0], slot[1], slot[2]);
    address p = _ob_sect->start() + _ob_off;
    *(uint64_t*)p = b.lo;
    *(uint64_t*)(p + 8) = b.hi;
  }

  // ---- list scheduling (BUNDLING.md, Stage 3) -------------------------------
  //
  // Under UseBundleScheduling, packable instructions are held in a window
  // and reordered before packing, so independent work fills the groups a
  // dependency chain leaves half empty. The window is flushed -- scheduled
  // and handed to the packer -- by everything that ends packing's open
  // bundle (every position observer, branches, barriers, scope changes), so
  // nothing recorded ever points into it. The order is constrained by:
  //  - RAW and WAW: the consumer goes after the producer, in a later group;
  //  - WAR: the writer goes after the reader (the same group is fine);
  //  - "ordered" instructions keep their relative order: memory and system
  //    instructions (Deps describe registers, not memory or ARs), and
  //    writes of sp or fp, which a fault handler reads (stack bang);
  //  - the window's first instruction stays first: a position recorded
  //    just before it (an implicit null check's pc) names it, and nothing
  //    after it in program order may execute before it faults. Not in C2
  //    emission under UseC2CrossNodeScheduling, where no recorded position
  //    names a window instruction: faulting accesses are barrier bundles
  //    (C2_MacroAssembler::access) and offset() is a lower bound.
  //
  // Under UseC2CrossNodeScheduling the window spans C2 nodes (offset()) and
  // holds up to 48 instructions; otherwise 24. C2SchedStressSeed picks among
  // the ready instructions at random instead of by height, to shake out
  // missing dependencies (a wrong result instead of a slow one).
  static const int kSchedWindow = 48;
  static const int kSchedWindowNode = 24;
  // U_L: insn holds r1 (bits 6:0) and qp (12:7), imm the 64-bit constant.
  struct WinInsn { ia64::Insn insn; uint64_t imm; Unit u; Deps d; bool ordered; };
  WinInsn _win[kSchedWindow];
  int     _win_n = 0;
  uint64_t _sched_rand = 0;

  int win_limit() const { return defers_instructions() ? kSchedWindow : kSchedWindowNode; }
  // A stress choice in [0, n), or -1 when not stressing.
  int sched_stress_pick(int n) {
    if (C2SchedStressSeed == 0 || !defers_instructions()) return -1;
    if (_sched_rand == 0) _sched_rand = (uint64_t)C2SchedStressSeed * 0x9e3779b97f4a7c15ULL | 1;
    _sched_rand ^= _sched_rand << 13; _sched_rand ^= _sched_rand >> 7; _sched_rand ^= _sched_rand << 17;
    return (int)(_sched_rand % (uint64_t)n);
  }

  static bool is_ordered(Unit u, ia64::Insn insn, const Deps& d) {
    int op = (int)((insn >> 37) & 0xf);
    switch (u) {
      case U_M: if (op < 8) return true; break;           // ld/st/fetchadd/xchg/getf/setf/system
      case U_I: if (op == 0) {                            // I misc: only sxt/zxt/czx are plain ALU
                  int x3 = (int)((insn >> 33) & 0x7), x6 = (int)((insn >> 27) & 0x3f);
                  if (!(x3 == 0 && x6 >= 0x10 && x6 <= 0x1f)) return true;
                }
                break;
      case U_F: if (op <= 1) return true; break;          // F misc (fsetc, fclrf, frcpa, ...)
      case U_A: case U_L: break;
    }
    for (int i = 0; i < d.nwr; i++) {
      if (d.wr_cls[i] == Deps::GR && (d.wr_num[i] == 12 || d.wr_num[i] == 4)) return true;  // sp, fp
    }
    return false;
  }

  static bool reads(const Deps& d, int c, int n) {
    for (int i = 0; i < d.nrd; i++) if (d.rd_cls[i] == c && d.rd_num[i] == n) return true;
    return false;
  }
  static bool writes(const Deps& d, int c, int n) {
    for (int i = 0; i < d.nwr; i++) if (d.wr_cls[i] == c && d.wr_num[i] == n) return true;
    return false;
  }
  // a precedes b in program order: must it stay before? strict = RAW/WAW.
  static bool depends(const WinInsn& a, const WinInsn& b, bool* strict) {
    *strict = false;
    for (int i = 0; i < b.d.nrd; i++) if (writes(a.d, b.d.rd_cls[i], b.d.rd_num[i])) { *strict = true; return true; }
    for (int i = 0; i < b.d.nwr; i++) if (writes(a.d, b.d.wr_cls[i], b.d.wr_num[i])) { *strict = true; return true; }
    for (int i = 0; i < b.d.nwr; i++) if (reads(a.d, b.d.wr_cls[i], b.d.wr_num[i])) return true;
    return a.ordered && b.ordered;
  }

  // ---- the machine model (UseC2LatencyScheduling) ---------------------------
  //
  // With UseC2CrossNodeScheduling, C2 code's window is scheduled for the
  // Itanium 2 pipeline instead of by chain height, after Firefox's IA-64
  // scheduler (src/firefox-jit-ia64-opt, Scheduler-ia64.h) and the model in
  // tools/sched-model.py (C2-DESIGN.md section 11):
  //  - edges carry latencies: integer load 1 (2 into another access's
  //    address), FP load 6, setf 6, getf 5, F unit 4 (2 to a predicate),
  //    anything else 1; WAW 1; WAR and memory order 0 (same group, in order);
  //  - memory: loads and stores keep their program order. Under
  //    UseC2SchedMemoryReordering loads move freely among themselves, and a
  //    store stays in order with an access only if the two addresses are not
  //    told apart (same root register value in the window, disjoint offset
  //    ranges): that lost 6% on J6Bench2 sbuild (allocation-heavy) on
  //    rx2800, so it is off. "System" instructions (semaphores, M/I misc,
  //    F status ops) and sp/fp writes stay in order with all memory
  //    operations either way. getf/setf are register transfers, and F misc
  //    ALU ops (frcpa, fmerge, fcvt) are not system instructions;
  //  - a cycle-driven ready list: a cycle takes the highest ready
  //    instruction whose latencies have elapsed and that fits the cycle's
  //    ports (2 loads; 2 stores/setf, getf on M2 only; 4 M; 2 I; 2 F;
  //    A-types on a spare I or M port; 6 syllables), and when nothing fits
  //    the clock moves to the next cycle in which something is ready.
  // The packer then lays the order out as before; its stops follow
  // register conflicts, which the latencies keep in separate cycles.
  enum SchedKind : uint8_t { SK_A, SK_I, SK_M, SK_LD, SK_LDF, SK_ST, SK_GETF, SK_SETF, SK_F, SK_SYS, SK_L };
  struct SchedOp {
    SchedKind k;
    bool mem;                 // a load or store (memory ordering)
    bool ordered;             // a system instruction, or writes sp/fp
    bool addr_known;          // plain access, unpredicated: base, size valid
    uint8_t base;             // address register
    uint8_t size;             // bytes accessed
    int16_t inc;              // post-increment (0: none or by register)
    bool reg_update;          // base updated by a register (M2/M7)
    uint16_t root; int64_t off;  // the address, symbolically (sched_addrs)
  };

  bool use_machine_model() const { return defers_instructions() && UseC2LatencyScheduling; }

  static SchedOp sched_classify(Unit u, ia64::Insn insn, const Deps& d) {
    SchedOp s = {};
    const int op = (int)((insn >> 37) & 0xf);
    switch (u) {
      case U_A: s.k = SK_A; break;
      case U_L: s.k = SK_L; break;
      case U_F:
        s.k = SK_F;
        // F misc with xb = 0, x6 < 0x10: break.f, nop.f, fsetc, fclrf, fchkf.
        if (op <= 1 && ((insn >> 33) & 1) == 0 && ((insn >> 27) & 0x3f) < 0x10) s.k = SK_SYS;
        break;
      case U_I:
        s.k = SK_I;
        if (op == 0) {                  // I misc: only sxt/zxt/czx are plain ALU
          int x3 = (int)((insn >> 33) & 0x7), x6 = (int)((insn >> 27) & 0x3f);
          if (!(x3 == 0 && x6 >= 0x10 && x6 <= 0x1f)) s.k = SK_SYS;
        }
        break;
      case U_M: {
        const int m = (int)((insn >> 36) & 1), x = (int)((insn >> 27) & 1), x6 = (int)((insn >> 30) & 0x3f);
        s.k = SK_SYS;
        if (op == 4 && x == 0) {        // integer ld/st (M1, M2, M4)
          if (x6 <= 0x03) { s.k = SK_LD; s.size = (uint8_t)(1 << (x6 & 3)); s.reg_update = (m == 1); }
          else if (x6 >= 0x30 && x6 <= 0x33 && m == 0) { s.k = SK_ST; s.size = (uint8_t)(1 << (x6 & 3)); }
        } else if (op == 4 && m == 0 && x == 1 && x6 >= 0x1c && x6 <= 0x1f) {
          s.k = SK_GETF;
        } else if (op == 5) {           // integer ld/st, imm9 post-increment (M3, M5)
          int load = x6 <= 0x03, store = x6 >= 0x30 && x6 <= 0x33;
          if (load || store) {
            s.k = load ? SK_LD : SK_ST;
            s.size = (uint8_t)(1 << (x6 & 3));
            uint64_t lo = load ? (insn >> 13) & 0x7f : (insn >> 6) & 0x7f;
            uint64_t v = lo | (((insn >> 27) & 1) << 7) | (((insn >> 36) & 1) << 8);
            s.inc = (int16_t)((int64_t)(v << 55) >> 55);
          }
        } else if (op == 6 && x == 0) { // FP ld/st, lfetch (M6, M7, M9, M13)
          if (x6 == 0x02 || x6 == 0x03) { s.k = SK_LDF; s.size = (x6 & 1) ? 8 : 4; s.reg_update = (m == 1); }
          else if (x6 == 0x01) { s.k = SK_LDF; s.size = 8; s.reg_update = (m == 1); }
          else if ((x6 == 0x32 || x6 == 0x33) && m == 0) { s.k = SK_ST; s.size = (x6 & 1) ? 8 : 4; }
          else if (x6 >= 0x2c && x6 <= 0x2f) { s.k = SK_LD; }          // lfetch: no result
        } else if (op == 6 && m == 0 && x == 1 && x6 >= 0x1c && x6 <= 0x1f) {
          s.k = SK_SETF;
        } else if (op == 7) {           // FP ld/st, imm9 post-increment (M8, M10)
          int load = (x6 == 0x02 || x6 == 0x03), store = (x6 == 0x32 || x6 == 0x33);
          if (load || store) {
            s.k = load ? SK_LDF : SK_ST;
            s.size = (x6 & 1) ? 8 : 4;
            uint64_t lo = load ? (insn >> 13) & 0x7f : (insn >> 6) & 0x7f;
            uint64_t v = lo | (((insn >> 27) & 1) << 7) | (((insn >> 36) & 1) << 8);
            s.inc = (int16_t)((int64_t)(v << 55) >> 55);
          }
        }
        break;
      }
    }
    s.mem = (s.k == SK_LD || s.k == SK_LDF || s.k == SK_ST);
    s.ordered = (s.k == SK_SYS);
    for (int i = 0; i < d.nwr; i++) {
      if (d.wr_cls[i] == Deps::GR && (d.wr_num[i] == 12 || d.wr_num[i] == 4)) s.ordered = true;  // sp, fp
    }
    s.base = (uint8_t)((insn >> 20) & 0x7f);
    s.addr_known = s.mem && s.size != 0 && (insn & 0x3f) == 0;
    return s;
  }

  // Firefox's computeAddrs(): each GR's value as (root, offset), where a root
  // is a register's value at the window's start or any value defined in it
  // other than by adds (A4) or a post-increment.
  void sched_addrs(int n, SchedOp* s) {
    struct Sym { uint16_t root; int64_t off; };
    Sym sym[128];
    for (int r = 0; r < 128; r++) sym[r] = Sym{ (uint16_t)(r + 1), 0 };
    uint16_t next = 129;
    for (int j = 0; j < n; j++) {
      const ia64::Insn insn = _win[j].insn;
      const Deps& d = _win[j].d;
      s[j].root = 0;
      if (s[j].addr_known) { s[j].root = sym[s[j].base].root; s[j].off = sym[s[j].base].off; }
      // adds r1 = imm14, r3 (unpredicated)
      bool adds = _win[j].u == U_A && ((insn >> 37) & 0xf) == 8 && ((insn >> 34) & 3) == 2 &&
                  ((insn >> 33) & 1) == 0 && (insn & 0x3f) == 0;
      int r1 = (int)((insn >> 6) & 0x7f), r3 = (int)((insn >> 20) & 0x7f);
      int64_t imm = 0;
      if (adds) {
        uint64_t v = ((insn >> 13) & 0x7f) | (((insn >> 27) & 0x3f) << 7) | (((insn >> 36) & 1) << 13);
        imm = (int64_t)(v << 50) >> 50;
      }
      const Sym from = adds ? sym[r3] : Sym{ 0, 0 };
      for (int i = 0; i < d.nwr; i++) {
        if (d.wr_cls[i] != Deps::GR) continue;
        int r = d.wr_num[i];
        if (adds && r == r1) {
          sym[r] = Sym{ from.root, from.off + imm };
        } else if (s[j].mem && r == s[j].base && s[j].inc != 0 && (insn & 0x3f) == 0) {
          sym[r].off += s[j].inc;
        } else {
          sym[r] = Sym{ next++, 0 };
        }
      }
    }
  }

  static bool sched_disjoint(const SchedOp& a, const SchedOp& b) {
    return a.root != 0 && a.root == b.root &&
           (a.off + a.size <= b.off || b.off + b.size <= a.off);
  }

  // Cycles from a's issue until b, which reads register (c, r) a writes.
  static int sched_latency(const SchedOp& a, const SchedOp& b, ia64::Insn b_insn, int c, int r) {
    if (c == Deps::BR) return 1;                       // incl. the pseudo-resources
    if (c == Deps::PR) return a.k == SK_F ? 2 : 1;
    if (c == Deps::GR) {
      if ((a.k == SK_LD || a.k == SK_LDF) && a.mem) {
        if (r == a.base && (a.inc != 0 || a.reg_update)) return 1;   // the base update
        if (b.mem && (int)((b_insn >> 20) & 0x7f) == r) return 2;    // into an address
        return 1;
      }
      if (a.k == SK_GETF) return 5;
      return 1;
    }
    // FR
    if (a.k == SK_LDF) return 6;
    if (a.k == SK_SETF) return 6;
    if (a.k == SK_F) return 4;
    return 1;
  }
  static int sched_result_latency(const SchedOp& a) {
    switch (a.k) {
      case SK_LDF: case SK_SETF: return 6;
      case SK_GETF: return 5;
      case SK_F: return 4;
      default: return 1;
    }
  }

  // Port use of one cycle (tools/sched-model.py fits()).
  struct Ports { uint8_t ld, st, getf, mgen, i, a, f, n; };
  static bool sched_fits(const Ports& p, Unit u, SchedKind k) {
    Ports q = p;
    switch (k) {
      case SK_LD: case SK_LDF: q.ld++; break;
      case SK_ST: case SK_SETF: q.st++; break;
      case SK_GETF: q.getf++; break;
      case SK_L: q.i++; q.n++; break;            // L+X: an I port, two syllables
      default:
        switch (u) {
          case U_M: q.mgen++; break;
          case U_I: q.i++; break;
          case U_A: q.a++; break;
          case U_F: q.f++; break;
        }
    }
    q.n++;
    if (q.ld > 2 || q.getf > 1 || q.st + q.getf > 2) return false;
    if (q.i > 2 || q.f > 2) return false;
    int m = q.ld + q.st + q.getf + q.mgen;
    if (m > 4) return false;
    int a_over = q.a - (2 - q.i);
    if (a_over > 0 && a_over > 4 - m) return false;
    return q.n <= 6;
  }
  static void sched_take(Ports& p, Unit u, SchedKind k) {
    switch (k) {
      case SK_LD: case SK_LDF: p.ld++; break;
      case SK_ST: case SK_SETF: p.st++; break;
      case SK_GETF: p.getf++; break;
      case SK_L: p.i++; p.n++; break;
      default:
        switch (u) {
          case U_M: p.mgen++; break;
          case U_I: p.i++; break;
          case U_A: p.a++; break;
          case U_F: p.f++; break;
        }
    }
    p.n++;
  }

  // ---- renaming dead values into r40-r55 (UseC2RenamePool) ------------------
  //
  // Firefox's renameDead() (src/firefox-jit-ia64-opt, SchedRename-ia64.h). A GR
  // written by an unpredicated instruction D and written again by an
  // unpredicated K later in the window, with every read of D's value in
  // between (K's own reads included) in a tagged register field (Deps::w6,
  // r13, r20), gets a pool register instead: D writes it, those reads read it,
  // K still writes the original. The window is straight-line code (branches,
  // calls, labels and barriers flush it), so after K, and at the window's
  // end, every register holds what it would have, and the pool register is
  // dead before the window ends: nothing outside the window -- debug info,
  // oop maps, the allocator -- can see the difference. Refused when a read
  // of the value has no field tag (an implicit read, addl's 2-bit r3, or a
  // post-increment base, which one field reads and writes), or K is
  // predicated. Renamed: the MacroAssembler temporaries t0-t4 (r2, r3,
  // r9-r11) and C2's allocatable r6-r8, r14-r31; never gp, fp, Rthread, sp,
  // tp or the outs. A window touching r32+ is left alone (it sets up a C
  // call). r40-r55 exist because call_stub's alloc has 24 outputs under this
  // flag (stubGenerator_ia64.cpp): outputs, so clobbered by any call, which a
  // window never spans. Pool registers go round in order of their last read.
  static const int kPoolBase = 40, kPoolSize = 16;
  // A windowed movl (U_L) keeps r1 at bits 6:0 of WinInsn::insn; its Deps
  // tag is this marker (0 means untagged).
  static const int kMovlR1Pos = 1;
  static int gr_field(const WinInsn& w, int pos) { return w.u == U_L ? 0 : pos; }
  // Whether the tagged field really holds GR r: a wrong tag would rename the
  // wrong operand, so it is checked rather than trusted (and refused).
  static bool gr_field_holds(const WinInsn& w, int pos, int r) {
    bool ok = (int)((w.insn >> gr_field(w, pos)) & 0x7f) == r;
    assert(ok, "Deps GR field tag does not match the encoding");
    return ok;
  }
  static bool renameable_gr(int r) {
    return r == 2 || r == 3 || (r >= 6 && r <= 11) || (r >= 14 && r <= 31);
  }
  static ia64::Insn set_gr_field(ia64::Insn b, int pos, int r) {
    return (b & ~((ia64::Insn)0x7f << pos)) | ((ia64::Insn)r << pos);
  }
  static int win_qp(const WinInsn& w) {
    return (int)((w.u == U_L ? (w.insn >> 7) : w.insn) & 0x3f);
  }

  int rename_window(int n) {
    for (int j = 0; j < n; j++) {
      const Deps& d = _win[j].d;
      for (int x = 0; x < d.nrd; x++) if (d.rd_cls[x] == Deps::GR && d.rd_num[x] >= 32) return 0;
      for (int x = 0; x < d.nwr; x++) if (d.wr_cls[x] == Deps::GR && d.wr_num[x] >= 32) return 0;
    }
    int busy[kPoolSize];                // the last window position reading each pool register
    for (int q = 0; q < kPoolSize; q++) busy[q] = -1;
    int renamed = 0;
    for (int i = 0; i + 1 < n; i++) {
      WinInsn& a = _win[i];
      if (win_qp(a) != 0) continue;
      for (int dw = 0; dw < a.d.nwr; dw++) {
        if (a.d.wr_cls[dw] != Deps::GR || a.d.wr_pos[dw] == 0) continue;
        const int r = a.d.wr_num[dw];
        if (!renameable_gr(r)) continue;
        bool ok = gr_field_holds(a, a.d.wr_pos[dw], r);
        for (int x = 0; x < a.d.nrd; x++) {
          if (a.d.rd_cls[x] == Deps::GR && a.d.rd_num[x] == r && a.d.rd_pos[x] == a.d.wr_pos[dw]) ok = false;
        }
        int kill = -1, last = i;
        for (int k = i + 1; k < n && ok && kill < 0; k++) {
          const WinInsn& b = _win[k];
          bool writes = false;
          for (int e = 0; e < b.d.nwr; e++) {
            if (b.d.wr_cls[e] == Deps::GR && b.d.wr_num[e] == r) writes = true;
          }
          for (int x = 0; x < b.d.nrd; x++) {
            if (b.d.rd_cls[x] == Deps::GR && b.d.rd_num[x] == r) {
              if (b.d.rd_pos[x] == 0 || !gr_field_holds(b, b.d.rd_pos[x], r)) ok = false;
              last = k;
            }
          }
          if (writes) {
            if (win_qp(b) != 0) ok = false;
            kill = k;
          }
        }
        if (!ok || kill < 0) continue;
        int p = -1;                     // the pool register free longest
        for (int q = 0; q < kPoolSize; q++) {
          if (busy[q] <= i && (p < 0 || busy[q] < busy[p])) p = q;
        }
        if (p < 0) continue;
        const int pr = kPoolBase + p;
        a.insn = set_gr_field(a.insn, gr_field(a, a.d.wr_pos[dw]), pr);
        a.d.wr_num[dw] = (uint8_t)pr;
        for (int k = i + 1; k <= kill; k++) {
          WinInsn& b = _win[k];
          for (int x = 0; x < b.d.nrd; x++) {
            if (b.d.rd_cls[x] == Deps::GR && b.d.rd_num[x] == r) {
              b.insn = set_gr_field(b.insn, b.d.rd_pos[x], pr);
              b.d.rd_num[x] = (uint8_t)pr;
            }
          }
        }
        busy[p] = last;
        renamed++;
      }
    }
    return renamed;
  }

  void flush_window_machine(int n) {
    if (UseC2RenamePool) rename_window(n);
    SchedOp s[kSchedWindow];
    for (int j = 0; j < n; j++) s[j] = sched_classify(_win[j].u, _win[j].insn, _win[j].d);
    if (UseC2SchedMemoryReordering) sched_addrs(n, s);
    // Edges i -> j (i < j) with latencies; successor lists.
    uint8_t nsucc[kSchedWindow], succ[kSchedWindow][kSchedWindow], slat[kSchedWindow][kSchedWindow];
    uint8_t npred[kSchedWindow];
    for (int j = 0; j < n; j++) { nsucc[j] = 0; npred[j] = 0; }
    for (int j = 1; j < n; j++) {
      const Deps& b = _win[j].d;
      for (int i = 0; i < j; i++) {
        const Deps& a = _win[i].d;
        int lat = -1;
        for (int x = 0; x < b.nrd; x++) {               // RAW
          if (writes(a, b.rd_cls[x], b.rd_num[x])) {
            int l = sched_latency(s[i], s[j], _win[j].insn, b.rd_cls[x], b.rd_num[x]);
            if (l > lat) lat = l;
          }
        }
        for (int x = 0; x < b.nwr && lat < 1; x++) {    // WAW
          if (writes(a, b.wr_cls[x], b.wr_num[x])) lat = 1;
        }
        for (int x = 0; x < b.nwr && lat < 0; x++) {    // WAR
          if (reads(a, b.wr_cls[x], b.wr_num[x])) lat = 0;
        }
        if (lat < 0) {
          const SchedOp& p = s[i]; const SchedOp& q = s[j];
          if ((p.ordered && (q.ordered || q.mem)) || (q.ordered && p.mem) ||
              (p.mem && q.mem && (!UseC2SchedMemoryReordering ||
                                  ((p.k == SK_ST || q.k == SK_ST) && !sched_disjoint(p, q))))) {
            lat = 0;
          }
        }
        if (lat >= 0) {
          succ[i][nsucc[i]] = (uint8_t)j; slat[i][nsucc[i]] = (uint8_t)lat; nsucc[i]++;
          npred[j]++;
        }
      }
    }
    // Priority: the longest latency-weighted path to the window's end.
    int prio[kSchedWindow];
    for (int i = n - 1; i >= 0; i--) {
      prio[i] = sched_result_latency(s[i]);
      for (int k = 0; k < nsucc[i]; k++) {
        int p = slat[i][k] + prio[succ[i][k]];
        if (p > prio[i]) prio[i] = p;
      }
    }
    int earliest[kSchedWindow];
    uint8_t ready[kSchedWindow];
    int nready = 0;
    for (int j = 0; j < n; j++) {
      earliest[j] = 0;
      if (npred[j] == 0) ready[nready++] = (uint8_t)j;
    }
    int order[kSchedWindow];
    int placed = 0;
    const bool stress = C2SchedStressSeed != 0;
    for (int cycle = 0; placed < n;) {
      Ports ports = {};
      for (;;) {
        int best = -1, best_at = -1;
        if (stress) {
          if (nready > 0) { best_at = sched_stress_pick(nready); best = ready[best_at]; }
        } else {
          for (int r = 0; r < nready; r++) {
            int j = ready[r];
            if (earliest[j] > cycle || !sched_fits(ports, _win[j].u, s[j].k)) continue;
            if (best < 0 || prio[j] > prio[best] || (prio[j] == prio[best] && j < best)) {
              best = j; best_at = r;
            }
          }
        }
        if (best < 0) break;
        order[placed++] = best;
        sched_take(ports, _win[best].u, s[best].k);
        ready[best_at] = ready[--nready];
        for (int k = 0; k < nsucc[best]; k++) {
          int t = succ[best][k];
          int at = cycle + slat[best][k];
          if (at > earliest[t]) earliest[t] = at;
          if (--npred[t] == 0) ready[nready++] = (uint8_t)t;
        }
        if (stress) break;
      }
      int next = cycle + 1;
      if (!stress && nready > 0) {
        int first = earliest[ready[0]];
        for (int r = 1; r < nready; r++) if (earliest[ready[r]] < first) first = earliest[ready[r]];
        if (first > next) next = first;
      }
      cycle = next;
    }
    for (int k = 0; k < n; k++) {
      const WinInsn& w = _win[order[k]];
      pack_win(w);
    }
  }

  void flush_window() {
    if (_win_n == 0) return;
    const int n = _win_n;
    _win_n = 0;                         // pack_unit below must not re-enter
    if (use_machine_model()) {
      flush_window_machine(n);
      return;
    }
    const bool pin_first = !defers_instructions();
    uint64_t pred[kSchedWindow], strict_succ[kSchedWindow];
    for (int j = 0; j < n; j++) { pred[j] = 0; strict_succ[j] = 0; }
    for (int j = 1; j < n; j++) {
      if (pin_first) pred[j] |= 1;      // the first instruction stays first
      for (int i = 0; i < j; i++) {
        bool strict;
        if (depends(_win[i], _win[j], &strict)) {
          pred[j] |= (uint64_t)1 << i;
          if (strict) strict_succ[i] |= (uint64_t)1 << j;
        }
      }
    }
    int height[kSchedWindow];           // longest RAW/WAW chain to the end
    for (int i = n - 1; i >= 0; i--) {
      height[i] = 1;
      for (int j = i + 1; j < n; j++) {
        if (((strict_succ[i] >> j) & 1) && height[j] + 1 > height[i]) height[i] = height[j] + 1;
      }
    }
    uint64_t done = 0;
    uint64_t gw[4][2] = {};             // registers written in the group being formed
    int order[kSchedWindow];
    for (int k = 0; k < n; k++) {
      int best = -1;
      for (int pass = 0; pass < 2 && best < 0; pass++) {
        if (pass == 1) memset(gw, 0, sizeof(gw));    // nothing fits: start a new group
        int ready[kSchedWindow], nready = 0;
        for (int j = 0; j < n; j++) {
          if (((done >> j) & 1) || (pred[j] & ~done) != 0) continue;
          const Deps& d = _win[j].d;
          bool clash = false;
          for (int i = 0; i < d.nrd && !clash; i++) clash = (gw[d.rd_cls[i]][d.rd_num[i] >> 6] >> (d.rd_num[i] & 63)) & 1;
          for (int i = 0; i < d.nwr && !clash; i++) clash = (gw[d.wr_cls[i]][d.wr_num[i] >> 6] >> (d.wr_num[i] & 63)) & 1;
          if (clash) continue;
          ready[nready++] = j;
          if (best < 0 || height[j] > height[best]) best = j;
        }
        if (nready > 1) {
          int pick = sched_stress_pick(nready);
          if (pick >= 0) best = ready[pick];
        }
      }
      assert(best >= 0, "a ready instruction always fits an empty group");
      order[k] = best;
      done |= (uint64_t)1 << best;
      const Deps& d = _win[best].d;
      for (int i = 0; i < d.nwr; i++) gw[d.wr_cls[i]][d.wr_num[i] >> 6] |= (uint64_t)1 << (d.wr_num[i] & 63);
    }
    for (int k = 0; k < n; k++) {
      const WinInsn& w = _win[order[k]];
      pack_win(w);
    }
  }

  // A store (M opcode 4-7, x clear, x6 0x30-0x3b: st1-8[.rel], st8.spill,
  // stf*). With UseBundleScheduling each also writes a pseudo-resource (BR
  // class, number 64; there are only 8 branch registers) so no two stores
  // share an instruction group: the scheduler would otherwise pair them,
  // which made allocation-heavy code ~15% slower on rx2800.
  static bool is_store(Unit u, ia64::Insn insn) {
    if (u != U_M) return false;
    int op = (int)((insn >> 37) & 0xf), x = (int)((insn >> 27) & 1), x6 = (int)((insn >> 30) & 0x3f);
    return op >= 4 && op <= 7 && x == 0 && x6 >= 0x30 && x6 <= 0x3b;
  }
  static const int kStoreSlot = 64;

  // getf.{s,d,exp,sig} (M opcode 4, x set, x6 0x1c-0x1f). It issues on M2
  // only, and a store after it in the same instruction group holds the next
  // group until the getf completes, ~5 cycles, where a stop costs one
  // (FINDINGS "Firefox's Montecito probes on Tukwila", getf_st; itanium2-sw
  // 3.3.2). Under UseGetfStoreRule a getf writes a pseudo-resource (BR class,
  // number 66) that every store reads, so a store never follows a getf in
  // its group; a store ahead of it is unaffected (that split costs a stop).
  static bool is_getf(Unit u, ia64::Insn insn) {
    if (u != U_M) return false;
    int op = (int)((insn >> 37) & 0xf), m = (int)((insn >> 36) & 1), x = (int)((insn >> 27) & 1),
        x6 = (int)((insn >> 30) & 0x3f);
    return op == 4 && m == 0 && x == 1 && x6 >= 0x1c && x6 <= 0x1f;
  }
  static const int kGetfPort = 66;

  void emit_unit(Unit u, ia64::Insn insn, const Deps& d0) {
    Deps d = d0;
    if (_pack_depth > 0 && UseBundleScheduling && is_store(u, insn) && d.nwr < 3) {
      d.wr(Deps::BR, kStoreSlot);
    }
    if (_pack_depth > 0 && UseGetfStoreRule) {
      if (is_getf(u, insn) && d.nwr < 3) {
        d.wr(Deps::BR, kGetfPort);
      } else if (is_store(u, insn) && d.nrd < 6) {
        d.rd(Deps::BR, kGetfPort);
      }
    }
    if (_pack_depth > 0 && UseBundleScheduling) {
      if (d.is_branch || d.keep_stop) {
        flush_window();
        pack_unit(u, insn, d);
        return;
      }
      if (_win_n >= win_limit()) flush_window();
      WinInsn& w = _win[_win_n++];
      w.insn = insn; w.u = u; w.d = d; w.ordered = is_ordered(u, insn, d);
      return;
    }
    pack_unit(u, insn, d);
  }

  void pack_unit(Unit u, ia64::Insn insn, const Deps& d) {
    if (_pack_depth == 0) {
      ia64::Bundle b = (u == U_I) ? ia64::BundleI(insn) :
                       (u == U_F) ? ia64::BundleF(insn) : ia64::BundleM(insn);
      emit_bundle(b, &d);
      return;
    }
    bool conflict = conflicts(d);
    if (_ob_open && _ob_n < 3 && _ob_sect == code_section() &&
        _ob_off == raw_offset() - BytesPerBundle) {
      Unit us[3]; bool st[3]; int pos[3]; ia64::Template tmpl;
      for (int k = 0; k < _ob_n; k++) { us[k] = _ob_unit[k]; st[k] = _ob_stop[k]; }
      us[_ob_n] = u; st[_ob_n] = conflict;
      if (place(_ob_n + 1, us, st, &tmpl, pos)) {
        _ob_insn[_ob_n] = insn; _ob_unit[_ob_n] = u; _ob_stop[_ob_n] = conflict;
        _ob_n++;
        write_open_bundle(tmpl, pos);
        if (conflict) clear_pending();
        add_pending(d);
        return;
      }
    }
    // A new bundle. The group continues into it, clearing the previous
    // bundle's end stop, only if that bundle allows it and nothing conflicts.
    finalize_open_bundle();
    if (!conflict && UseStopElision &&
        _last_sect == code_section() && _last_off == raw_offset() - BytesPerBundle) {
      address prev = code_section()->start() + _last_off;
      assert((*prev & 1) == 1, "previous bundle must still end in a stop");
      *prev &= ~1;
    } else {
      clear_pending();
    }
    add_pending(d);
    _ob_open = false;
    emit_raw_bundle(ia64::BundleNop());
    _ob_open = true;
    _ob_sect = code_section();
    _ob_off  = raw_offset() - BytesPerBundle;
    _ob_n = 1;
    _ob_insn[0] = insn; _ob_unit[0] = u; _ob_stop[0] = false;
    int pos[1]; ia64::Template tmpl;
    bool ok = place(1, _ob_unit, _ob_stop, &tmpl, pos);
    assert(ok, "a single instruction always fits");
    write_open_bundle(tmpl, pos);
    _last_sect = _ob_sect;
    _last_off  = _ob_off;
  }

  void pack_win(const WinInsn& w) {
    if (w.u == U_L) {
      pack_movl(w);
    } else {
      pack_unit(w.u, w.insn, w.d);
    }
  }

  // A movl from the window: into the open bundle as MLX if that holds just
  // one M or A instruction, independent of it; otherwise an MLX bundle of
  // its own. Either way its group may continue into the next bundle (stop
  // elision): nothing recognises or patches these.
  // MovlBundleWith() asserts r1 < 40 (the static registers and the outs);
  // a movl renamed into the pool (rename_window) is encoded with r0 and its
  // X-slot r1 field (bundle bits 99:93) patched.
  static ia64::Bundle movl_bundle(ia64::Insn s0, uint32_t r1, uint64_t imm, uint32_t qp) {
    ia64::Bundle b = ia64::MovlBundleWith(s0, r1 < 40 ? r1 : 0, imm, qp, ia64::tMLX_);
    if (r1 >= 40) b.hi = (b.hi & ~((uint64_t)0x7f << 29)) | ((uint64_t)r1 << 29);
    return b;
  }

  void pack_movl(const WinInsn& w) {
    const uint32_t r1 = (uint32_t)(w.insn & 0x7f), qp = (uint32_t)((w.insn >> 7) & 0x3f);
    const Deps& d = w.d;
    const bool conflict = conflicts(d);
    if (_pack_depth > 0 && !conflict && _ob_open && _ob_n == 1 &&
        (_ob_unit[0] == U_M || _ob_unit[0] == U_A) &&
        _ob_sect == code_section() && _ob_off == raw_offset() - BytesPerBundle) {
      ia64::Bundle b = movl_bundle(_ob_insn[0], r1, w.imm, qp);
      address p = _ob_sect->start() + _ob_off;
      *(uint64_t*)p = b.lo;
      *(uint64_t*)(p + 8) = b.hi;
      _ob_open = false;
      add_pending(d);
      return;                           // _last_off already names this bundle
    }
    finalize_open_bundle();
    _ob_open = false;
    if (!conflict && UseStopElision &&
        _last_sect == code_section() && _last_off == raw_offset() - BytesPerBundle) {
      address prev = code_section()->start() + _last_off;
      assert((*prev & 1) == 1, "previous bundle must still end in a stop");
      *prev &= ~1;
    } else {
      clear_pending();
    }
    add_pending(d);
    emit_raw_bundle(movl_bundle(ia64::NopM(), r1, w.imm, qp));
    _last_sect = code_section();
    _last_off  = raw_offset() - BytesPerBundle;
  }

 public:

  void nop() { emit_bundle(ia64::BundleNop()); }

  // Every form below takes an optional trailing qualifying predicate. The
  // instruction executes only if it is true; the default, p0, is hardwired
  // true. Predication is how IA-64 expresses short conditional code without
  // a branch, and the MacroAssembler leans on it heavily.
#define QP PredicateRegister qp = pTrue
#define Q  qp.encoding()

  // ---- loads and stores (M unit) -----------------------------------------
  //
  // No displacement form exists; the address must already be in a register.
  // IA-64 loads zero-extend: a signed narrow load needs an sxt afterwards.

  void ld1(Register r1, Register r3, QP)  { emit_m(ia64::Ld1(r1->encoding(), r3->encoding(), Q), D().w6(r1).r20(r3).r(qp)); }
  void ld2(Register r1, Register r3, QP)  { emit_m(ia64::Ld2(r1->encoding(), r3->encoding(), Q), D().w6(r1).r20(r3).r(qp)); }
  void ld4(Register r1, Register r3, QP)  { emit_m(ia64::Ld4(r1->encoding(), r3->encoding(), Q), D().w6(r1).r20(r3).r(qp)); }
  void ld8(Register r1, Register r3, QP)  { emit_m(ia64::Ld8(r1->encoding(), r3->encoding(), Q), D().w6(r1).r20(r3).r(qp)); }

  void st1(Register r3, Register r2, QP)  { emit_m(ia64::St1(r3->encoding(), r2->encoding(), Q), D().r20(r3).r13(r2).r(qp)); }
  void st2(Register r3, Register r2, QP)  { emit_m(ia64::St2(r3->encoding(), r2->encoding(), Q), D().r20(r3).r13(r2).r(qp)); }
  void st4(Register r3, Register r2, QP)  { emit_m(ia64::St4(r3->encoding(), r2->encoding(), Q), D().r20(r3).r13(r2).r(qp)); }
  void st8(Register r3, Register r2, QP)  { emit_m(ia64::St8(r3->encoding(), r2->encoding(), Q), D().r20(r3).r13(r2).r(qp)); }

  // Post-increment: access [r3], then r3 += imm9 (signed 9 bits). The one
  // addressing mode beyond a bare register; it is how the expression stack is
  // pushed and popped without a separate adds.
  void ld1_inc(Register r1, Register r3, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::Ld1Inc(r1->encoding(), r3->encoding(), imm9, Q), D().w6(r1).w(r3).r(r3).r(qp)); }
  void ld2_inc(Register r1, Register r3, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::Ld2Inc(r1->encoding(), r3->encoding(), imm9, Q), D().w6(r1).w(r3).r(r3).r(qp)); }
  void ld4_inc(Register r1, Register r3, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::Ld4Inc(r1->encoding(), r3->encoding(), imm9, Q), D().w6(r1).w(r3).r(r3).r(qp)); }
  void ld8_inc(Register r1, Register r3, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::Ld8Inc(r1->encoding(), r3->encoding(), imm9, Q), D().w6(r1).w(r3).r(r3).r(qp)); }
  void st1_inc(Register r3, Register r2, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::St1Inc(r3->encoding(), r2->encoding(), imm9, Q), D().w(r3).r(r3).r13(r2).r(qp)); }
  void st2_inc(Register r3, Register r2, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::St2Inc(r3->encoding(), r2->encoding(), imm9, Q), D().w(r3).r(r3).r13(r2).r(qp)); }
  void st4_inc(Register r3, Register r2, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::St4Inc(r3->encoding(), r2->encoding(), imm9, Q), D().w(r3).r(r3).r13(r2).r(qp)); }
  void st8_inc(Register r3, Register r2, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::St8Inc(r3->encoding(), r2->encoding(), imm9, Q), D().w(r3).r(r3).r13(r2).r(qp)); }

  void ld4_acq(Register r1, Register r3, QP) { emit_m(ia64::Ld4Acq(r1->encoding(), r3->encoding(), Q)); }
  void ld8_acq(Register r1, Register r3, QP) { emit_m(ia64::Ld8Acq(r1->encoding(), r3->encoding(), Q)); }
  void st4_rel(Register r3, Register r2, QP) { emit_m(ia64::St4Rel(r3->encoding(), r2->encoding(), Q)); }
  void st8_rel(Register r3, Register r2, QP) { emit_m(ia64::St8Rel(r3->encoding(), r2->encoding(), Q)); }

  // NaT-preserving forms. call_stub must use these for the C caller's r4-r7:
  // a plain ld8/st8 round-trip drops the NaT bit, and st8.spill deposits it
  // into ar.unat, which must be saved and restored around the block.
  void ld8_fill(Register r1, Register r3, QP)  { emit_m(ia64::Ld8Fill(r1->encoding(), r3->encoding(), Q)); }
  void st8_spill(Register r3, Register r2, QP) { emit_m(ia64::St8Spill(r3->encoding(), r2->encoding(), Q)); }

  // Line prefetch, never faults; |excl| when the line will be written.
  void lfetch(Register r3, bool excl = false, uint32_t hint = 0, QP) { emit_m(ia64::Lfetch(r3->encoding(), excl, hint, Q), D().r20(r3).r(qp)); }
  void ldfs(FloatRegister f1, Register r3, QP) { emit_m(ia64::Ldfs(f1->encoding(), r3->encoding(), Q), D().w(f1).r20(r3).r(qp)); }
  void ldfd(FloatRegister f1, Register r3, QP) { emit_m(ia64::Ldfd(f1->encoding(), r3->encoding(), Q), D().w(f1).r20(r3).r(qp)); }
  void stfs(Register r3, FloatRegister f2, QP) { emit_m(ia64::Stfs(r3->encoding(), f2->encoding(), Q), D().r20(r3).r(f2).r(qp)); }
  void stfd(Register r3, FloatRegister f2, QP) { emit_m(ia64::Stfd(r3->encoding(), f2->encoding(), Q), D().r20(r3).r(f2).r(qp)); }

  // Full 82-bit FP save/restore, 16 bytes and 16-byte aligned. Required for
  // f2-f5 and f16-f31 in call_stub: a double round-trip would silently
  // truncate a C caller's long double.
  void ldf_fill(FloatRegister f1, Register r3, QP)  { emit_m(ia64::LdfFill(f1->encoding(), r3->encoding(), Q)); }
  void stf_spill(Register r3, FloatRegister f2, QP) { emit_m(ia64::StfSpill(r3->encoding(), f2->encoding(), Q)); }

  // ---- ALU (A unit: an M or an I slot) -------------------------------------

  void add(Register r1, Register r2, Register r3, QP)   { emit_a(ia64::Add(r1->encoding(), r2->encoding(), r3->encoding(), Q), D().w6(r1).r13(r2).r20(r3).r(qp)); }
  void sub(Register r1, Register r2, Register r3, QP)   { emit_a(ia64::Sub(r1->encoding(), r2->encoding(), r3->encoding(), Q), D().w6(r1).r13(r2).r20(r3).r(qp)); }
  void and_(Register r1, Register r2, Register r3, QP)  { emit_a(ia64::And(r1->encoding(), r2->encoding(), r3->encoding(), Q), D().w6(r1).r13(r2).r20(r3).r(qp)); }
  void andcm(Register r1, Register r2, Register r3, QP) { emit_a(ia64::Andcm(r1->encoding(), r2->encoding(), r3->encoding(), Q), D().w6(r1).r13(r2).r20(r3).r(qp)); }
  void or_(Register r1, Register r2, Register r3, QP)   { emit_a(ia64::Or(r1->encoding(), r2->encoding(), r3->encoding(), Q), D().w6(r1).r13(r2).r20(r3).r(qp)); }
  void xor_(Register r1, Register r2, Register r3, QP)  { emit_a(ia64::Xor(r1->encoding(), r2->encoding(), r3->encoding(), Q), D().w6(r1).r13(r2).r20(r3).r(qp)); }

  // The 8-bit immediate forms. Note the immediate is the FIRST source:
  // sub_imm(r1, imm8, r3) computes imm8 - r3 (so negation is sub_imm(r1, 0, r3)).
  void and_imm(Register r1, int64_t imm8, Register r3, QP) { assert(ia64::is_simm8(imm8), "imm8"); emit_a(ia64::AndImm(r1->encoding(), imm8, r3->encoding(), Q), D().w6(r1).r20(r3).r(qp)); }
  void or_imm(Register r1, int64_t imm8, Register r3, QP)  { assert(ia64::is_simm8(imm8), "imm8"); emit_a(ia64::OrImm(r1->encoding(), imm8, r3->encoding(), Q), D().w6(r1).r20(r3).r(qp)); }
  void xor_imm(Register r1, int64_t imm8, Register r3, QP) { assert(ia64::is_simm8(imm8), "imm8"); emit_a(ia64::XorImm(r1->encoding(), imm8, r3->encoding(), Q), D().w6(r1).r20(r3).r(qp)); }
  void sub_imm(Register r1, int64_t imm8, Register r3, QP) { assert(ia64::is_simm8(imm8), "imm8"); emit_a(ia64::SubImm(r1->encoding(), imm8, r3->encoding(), Q), D().w6(r1).r20(r3).r(qp)); }

  // r1 = (r2 << count) + r3, count 1..4: base + index * scale in one step.
  void shladd(Register r1, Register r2, int count, Register r3, QP) {
    emit_a(ia64::Shladd(r1->encoding(), r2->encoding(), count, r3->encoding(), Q), D().w6(r1).r13(r2).r20(r3).r(qp));
  }

  // adds is also the register move (imm == 0) and the only way to add a small
  // constant; anything wider than 14 bits signed needs movl + add.
  void adds(Register r1, int64_t imm14, Register r3, QP) {
    assert(ia64::is_simm14(imm14), "immediate too wide for adds -- use movl + add");
    emit_a(ia64::Adds(r1->encoding(), imm14, r3->encoding(), Q), D().w6(r1).r20(r3).r(qp));
  }
  void mov(Register r1, Register r3, QP) { emit_a(ia64::MovReg(r1->encoding(), r3->encoding(), Q), D().w6(r1).r20(r3).r(qp)); }
  // addl: a 22-bit signed constant plus r0-r3 (with r0, a one-slot constant load).
  void addl(Register r1, int64_t imm22, Register r3, QP) {
    emit_a(ia64::Addl(r1->encoding(), imm22, r3->encoding(), Q), D().w6(r1).r(r3).r(qp));
  }

  // ---- compare (A unit) --------------------------------------------------
  //
  // Each writes p1 = relation and p2 = !relation. The 64-bit forms compare
  // whole registers; the cmp4 forms only the low 32 bits, which is what Java
  // int comparisons want. The immediate forms take the 8-bit immediate as the
  // FIRST operand: cmp_lt_imm(p1, p2, 5, r) tests 5 < r.

  void cmp_eq(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_a(ia64::CmpEq(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r13(r2).r20(r3).r(qp)); }
  void cmp_ne(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_a(ia64::CmpNe(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r13(r2).r20(r3).r(qp)); }
  void cmp_lt(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_a(ia64::CmpLt(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r13(r2).r20(r3).r(qp)); }
  void cmp_ltu(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP) { emit_a(ia64::CmpLtu(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r13(r2).r20(r3).r(qp)); }

  void cmp4_eq(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_a(ia64::Cmp4Eq(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r13(r2).r20(r3).r(qp)); }
  void cmp4_ne(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_a(ia64::Cmp4Ne(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r13(r2).r20(r3).r(qp)); }
  void cmp4_lt(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_a(ia64::Cmp4Lt(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r13(r2).r20(r3).r(qp)); }
  void cmp4_ltu(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP) { emit_a(ia64::Cmp4Ltu(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r13(r2).r20(r3).r(qp)); }

#define CMP_IMM(name, Enc) \
  void name(PredicateRegister p1, PredicateRegister p2, int64_t imm8, Register r3, QP) { \
    assert(ia64::is_simm8(imm8), "imm8"); \
    emit_a(ia64::Enc(p1.encoding(), p2.encoding(), imm8, r3->encoding(), Q), D().w(p1).w(p2).r20(r3).r(qp)); \
  }
  CMP_IMM(cmp_eq_imm,   CmpEqImm)
  CMP_IMM(cmp_ne_imm,   CmpNeImm)
  CMP_IMM(cmp_lt_imm,   CmpLtImm)
  CMP_IMM(cmp_ltu_imm,  CmpLtuImm)
  CMP_IMM(cmp4_eq_imm,  Cmp4EqImm)
  CMP_IMM(cmp4_ne_imm,  Cmp4NeImm)
  CMP_IMM(cmp4_lt_imm,  Cmp4LtImm)
  CMP_IMM(cmp4_ltu_imm, Cmp4LtuImm)
#undef CMP_IMM

  // p1 = (bit pos of r3 is clear) / (is set); p2 the complement.
  void tbit_z(PredicateRegister p1, PredicateRegister p2, Register r3, int pos, QP)  { emit_i(ia64::TbitZ(p1.encoding(), p2.encoding(), r3->encoding(), pos, Q), D().w(p1).w(p2).r20(r3).r(qp)); }
  void tbit_nz(PredicateRegister p1, PredicateRegister p2, Register r3, int pos, QP) { emit_i(ia64::TbitNz(p1.encoding(), p2.encoding(), r3->encoding(), pos, Q), D().w(p1).w(p2).r20(r3).r(qp)); }

  // ---- shifts, extends (I unit) ------------------------------------------

  void shl(Register r1, Register value, Register count, QP)  { emit_i(ia64::Shl(r1->encoding(), value->encoding(), count->encoding(), Q), D().w6(r1).r13(value).r20(count).r(qp)); }
  void shr(Register r1, Register value, Register count, QP)  { emit_i(ia64::Shr(r1->encoding(), value->encoding(), count->encoding(), Q), D().w6(r1).r20(value).r13(count).r(qp)); }
  void shru(Register r1, Register value, Register count, QP) { emit_i(ia64::ShrU(r1->encoding(), value->encoding(), count->encoding(), Q), D().w6(r1).r20(value).r13(count).r(qp)); }

  void shl_imm(Register r1, Register r2, uint32_t count, QP)  { emit_i(ia64::ShlImm(r1->encoding(), r2->encoding(), count, Q), D().w6(r1).r13(r2).r(qp)); }
  void shr_imm(Register r1, Register r3, uint32_t count, QP)  { emit_i(ia64::ShrImm(r1->encoding(), r3->encoding(), count, Q), D().w6(r1).r20(r3).r(qp)); }
  void shru_imm(Register r1, Register r3, uint32_t count, QP) { emit_i(ia64::ShrUImm(r1->encoding(), r3->encoding(), count, Q), D().w6(r1).r20(r3).r(qp)); }

  void extr_u(Register r1, Register r3, uint32_t pos, uint32_t len, QP) { emit_i(ia64::ExtrU(r1->encoding(), r3->encoding(), pos, len, Q), D().w6(r1).r20(r3).r(qp)); }
  void extr(Register r1, Register r3, uint32_t pos, uint32_t len, QP)   { emit_i(ia64::Extr(r1->encoding(), r3->encoding(), pos, len, Q), D().w6(r1).r20(r3).r(qp)); }
  void dep_z(Register r1, Register r2, uint32_t pos, uint32_t len, QP)  { emit_i(ia64::DepZ(r1->encoding(), r2->encoding(), pos, len, Q), D().w6(r1).r13(r2).r(qp)); }

  void sxt1(Register r1, Register r3, QP) { emit_i(ia64::Sxt1(r1->encoding(), r3->encoding(), Q), D().w6(r1).r20(r3).r(qp)); }
  void sxt2(Register r1, Register r3, QP) { emit_i(ia64::Sxt2(r1->encoding(), r3->encoding(), Q), D().w6(r1).r20(r3).r(qp)); }
  void sxt4(Register r1, Register r3, QP) { emit_i(ia64::Sxt4(r1->encoding(), r3->encoding(), Q), D().w6(r1).r20(r3).r(qp)); }
  void zxt1(Register r1, Register r3, QP) { emit_i(ia64::Zxt1(r1->encoding(), r3->encoding(), Q), D().w6(r1).r20(r3).r(qp)); }
  void zxt2(Register r1, Register r3, QP) { emit_i(ia64::Zxt2(r1->encoding(), r3->encoding(), Q), D().w6(r1).r20(r3).r(qp)); }
  void zxt4(Register r1, Register r3, QP) { emit_i(ia64::Zxt4(r1->encoding(), r3->encoding(), Q), D().w6(r1).r20(r3).r(qp)); }

  // shrp r1 = r2, r3, count: the 128-bit r2:r3 shifted right by count, low
  // 64 bits (a rotate when r2 == r3).
  void shrp(Register r1, Register r2, Register r3, uint32_t count, QP) { emit_i(ia64::Shrp(r1->encoding(), r2->encoding(), r3->encoding(), count, Q), D().w6(r1).r13(r2).r20(r3).r(qp)); }
  void popcnt(Register r1, Register r3, QP) { emit_i(ia64::Popcnt(r1->encoding(), r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }

  // r1 = address of the bundle holding this instruction.
  void mov_from_ip(Register r1, QP) { emit_i(ia64::MovFromIp(r1->encoding(), Q)); }
  // r1 = all 64 predicates (bit i = PR i); pr = r2 under mask17 (-1: all).
  // Barriers for stop elision: they read/write every predicate at once.
  void mov_from_pr(Register r1, QP)            { emit_i(ia64::MovFromPr(r1->encoding(), Q)); }
  void mov_to_pr(Register r2, int32_t mask17, QP) { emit_i(ia64::MovToPr(r2->encoding(), mask17, Q)); }

  // ---- atomics (M unit) --------------------------------------------------
  //
  // cmpxchg takes its comparand from ar.ccv, whose exact bit pattern is
  // compared -- a narrow cmpxchg must be given a comparand zero-extended to
  // the access width, or it can never succeed.

  void mov_to_ar_ccv(Register r2, QP) { emit_m(ia64::MovToArCcv(r2->encoding(), Q), D().w_ccv().r(r2).r(qp)); }

#define IA64_CMPXCHG_DEPS D().w(r1).r(r3).r(r2).r_ccv().r(qp)
  void cmpxchg4_acq(Register r1, Register r3, Register r2, QP) { emit_m(ia64::Cmpxchg4Acq(r1->encoding(), r3->encoding(), r2->encoding(), Q), IA64_CMPXCHG_DEPS); }
  void cmpxchg8_acq(Register r1, Register r3, Register r2, QP) { emit_m(ia64::Cmpxchg8Acq(r1->encoding(), r3->encoding(), r2->encoding(), Q), IA64_CMPXCHG_DEPS); }
  void cmpxchg4_rel(Register r1, Register r3, Register r2, QP) { emit_m(ia64::Cmpxchg4Rel(r1->encoding(), r3->encoding(), r2->encoding(), Q), IA64_CMPXCHG_DEPS); }
  void cmpxchg8_rel(Register r1, Register r3, Register r2, QP) { emit_m(ia64::Cmpxchg8Rel(r1->encoding(), r3->encoding(), r2->encoding(), Q), IA64_CMPXCHG_DEPS); }
#undef IA64_CMPXCHG_DEPS
  void xchg4(Register r1, Register r3, Register r2, QP)        { emit_m(ia64::Xchg4(r1->encoding(), r3->encoding(), r2->encoding(), Q), D().w(r1).r(r3).r(r2).r(qp)); }
  void xchg8(Register r1, Register r3, Register r2, QP)        { emit_m(ia64::Xchg8(r1->encoding(), r3->encoding(), r2->encoding(), Q), D().w(r1).r(r3).r(r2).r(qp)); }

  void mf() { emit_m(ia64::Mf()); }

  // ---- application and branch registers ----------------------------------

  void mov_from_ar(Register r1, uint32_t ar, QP) {
    // ar.pfs and ar.lc are I-unit registers; ar.ccv/unat/fpsr are M-unit.
    if (ia64::ar_is_m_unit(ar)) emit_m(ia64::MovFromAr(r1->encoding(), ar, Q));
    else                        emit_i(ia64::MovFromAr(r1->encoding(), ar, Q));
  }
  void mov_to_ar(uint32_t ar, Register r2, QP) {
    if (ia64::ar_is_m_unit(ar)) emit_m(ia64::MovToAr(ar, r2->encoding(), Q));
    else                        emit_i(ia64::MovToAr(ar, r2->encoding(), Q));
  }

  void mov_from_pfs(Register r1) { mov_from_ar(r1, ia64::kArPfs); }
  void mov_to_pfs(Register r2)   { mov_to_ar(ia64::kArPfs, r2); }

  void mov_to_br(BranchRegister b1, Register r2, QP)   {
    if (b1 == b7) _branch_reg_epoch++;
    emit_i(ia64::MovToBr(b1.encoding(), r2->encoding(), Q), D().w(b1).r(r2).r(qp).keep());
  }
  void mov_from_br(Register r1, BranchRegister b2, QP) { emit_i(ia64::MovFromBr(r1->encoding(), b2.encoding(), Q), D().w(r1).r(b2).r(qp)); }

  // The one and only alloc, in StubRoutines::call_stub(). See FRAME-DESIGN.md
  // section 1: a second one anywhere invalidates the CFM invariant that makes
  // br.ret safe in generated code. Debug builds assert it is emitted once.
  void alloc(Register r1, uint32_t ins, uint32_t locals, uint32_t outs, uint32_t rot = 0);

  // ---- branches (B unit) --------------------------------------------------

  void br_cond(BranchRegister b2, QP)                    { emit_b(ia64::BrCond(b2.encoding(), Q), D().r(b2).r(qp).branch()); }
  void br_ret(BranchRegister b2 = breturn, QP)           { emit_b(ia64::BrRet(b2.encoding(), Q)); }
  void br_call(BranchRegister b1, BranchRegister b2, QP) { _branch_reg_epoch++; emit_b(ia64::BrCall(b1.encoding(), b2.encoding(), Q)); }

  // IP-relative, to a label: +/-16 MiB, measured in bundles from this one.
  // Enough for any branch within one blob; anything that may be farther (a
  // call into another blob, a stub) goes through a branch register instead.
  // target() records an unbound label's patch site at AbstractAssembler's
  // own position, which does not flush the scheduling window: flush first.
  // The hint, unless given: .sptk for an unconditional branch; for a
  // conditional one, the dynamic predictor with the classic static guess --
  // .dptk backward (a loop), .dpnt forward (an exit, check or slow path).
  void br_cond(Label& L, QP) {
    br_cond(L, qp, !(qp == pTrue) ? (L.is_bound() ? ia64::kDptk : ia64::kDpnt) : ia64::kSptk);
  }
  void br_cond(Label& L, PredicateRegister qp, ia64::BranchHint hint) {
    Deps d = D().r(qp).branch();
    flush_window();
    address bundle = pack_branch_slot(d);
    if (bundle != nullptr) {
      // The patch site is the open bundle, displacement measured from it.
      address dest = code_section()->target(L, bundle);
      write_branch_slot(ia64::BrCondRel(bundle_disp(dest, bundle), Q, hint));
      return;
    }
    close_bundle();
    address dest = target(L);
    emit_b(ia64::BrCondRel(bundle_disp(dest), Q, hint), d);
  }
  // { cmp.ltu p7,p6 = a, b ; (p6) br.ret b0 ; br.cond L }, one MBB bundle:
  // return unless a < b (unsigned), else branch to L. One instruction group
  // (a branch may read a predicate from its own group), where a compare,
  // its branch and a separate br.ret take two. The return poll
  // (MachEpilogNode). L's patch site is this bundle's slot 2.
  void ret_unless_ltu(Register a, Register b, Label& L) {
    close_bundle();
    address bundle = AbstractAssembler::pc();
    address dest = code_section()->target(L, bundle);
    emit_bundle(ia64::MakeBundle(ia64::tMBB_,
                                 ia64::CmpLtu(ptmp1.encoding(), ptmp0.encoding(), a->encoding(), b->encoding()),
                                 ia64::BrRet(breturn.encoding(), ptmp0.encoding()),
                                 ia64::BrCondRel(bundle_disp(dest, bundle), 0, ia64::kSptk)),
                &D().w(ptmp0).w(ptmp1).r(a).r(b).r(breturn).branch());
  }
  void br_call(BranchRegister b1, Label& L, QP) {
    _branch_reg_epoch++;
    close_bundle();
    address dest = target(L);
    emit_b(ia64::BrCallRel(b1.encoding(), bundle_disp(dest), Q));
  }
  // br.call b0 = <bundle_disp> alone in its MIB bundle: NativeCall's shape
  // (MacroAssembler::trampoline_call).
  void br_call_rel(int32_t bundle_disp) {
    _branch_reg_epoch++;
    close_bundle();
    emit_b(ia64::BrCallRel(breturn.encoding(), bundle_disp));
  }
  // An unconditional jump is just a branch on p0.
  void br(Label& L) { br_cond(L); }

  void brk(uint32_t imm21, QP) { emit_b(ia64::BreakB(imm21, Q)); }

  // ---- 64-bit immediates (MLX) -------------------------------------------
  //
  // The port has no gp-relative addressing, and IP-relative branches reach
  // only +/-16 MiB, so every absolute address is materialised here.
  void movl(Register r1, uint64_t imm, QP) { Deps d = D().w(r1).r(qp).keep(); emit_bundle(ia64::MovlBundle(r1->encoding(), imm, Q), &d); }
  void movl(Register r1, address a, QP)    { movl(r1, (uint64_t)(uintptr_t)a, qp); }
  // A constant nothing relocates, recognises or patches: in C2 emission
  // under UseC2CrossNodeScheduling it is scheduled with the instructions
  // around it (pack_movl) instead of ending the window.
  void movl_const(Register r1, uint64_t imm, QP) {
    if (_pack_depth > 0 && UseBundleScheduling && defers_instructions() &&
        code_section()->locs_point() != code_section()->end()) {
      if (_win_n >= win_limit()) flush_window();
      WinInsn& w = _win[_win_n++];
      w.insn = (ia64::Insn)r1->encoding() | ((ia64::Insn)Q << 7);
      w.imm = imm; w.u = U_L; w.d = D().r(qp); w.d.wr(Deps::GR, r1->encoding(), kMovlR1Pos); w.ordered = false;
      return;
    }
    movl(r1, imm, qp);
  }

  // Load the address of a label, position-independently:
  //     mov  r1 = ip              // the address of this bundle, B
  //     movl tmp = L - B          // patched when L is bound
  //     add  r1 = r1, tmp
  // No relocation is needed, so the code may be copied out of its CodeBuffer
  // freely. Three bundles; used for return addresses (set_last_Java_frame)
  // and exception-handler addresses, never on a hot path.
  void la(Register r1, Label& L, Register tmp);

  // ---- floating point (F unit) -------------------------------------------

  void fma_d(FloatRegister f1, FloatRegister f3, FloatRegister f4, FloatRegister f2, QP) {
    emit_f(ia64::FmaD(f1->encoding(), f3->encoding(), f4->encoding(), f2->encoding(), ia64::sf0, Q), D().w(f1).r(f3).r(f4).r(f2).r(qp));
  }
  void fadd_d(FloatRegister f1, FloatRegister f3, FloatRegister f2, QP) { emit_f(ia64::FaddD(f1->encoding(), f3->encoding(), f2->encoding(), ia64::sf0, Q), D().w(f1).r(f3).r(f2).r(qp)); }
  void fsub_d(FloatRegister f1, FloatRegister f3, FloatRegister f2, QP) { emit_f(ia64::FsubD(f1->encoding(), f3->encoding(), f2->encoding(), ia64::sf0, Q), D().w(f1).r(f3).r(f2).r(qp)); }
  void fmpy_d(FloatRegister f1, FloatRegister f3, FloatRegister f4, QP) { emit_f(ia64::FmpyD(f1->encoding(), f3->encoding(), f4->encoding(), ia64::sf0, Q), D().w(f1).r(f3).r(f4).r(qp)); }
  // f1 = f3, an exact copy of the 82-bit register (fmerge.s f1 = f3, f3; GNU
  // as's "mov f1 = f3"). Not fnorm: normalising to double format would turn a
  // float denormal's register image into a normal value with an exponent below
  // the single range, which stfs then packs wrongly (exponent off by 2^128).
  void fmov(FloatRegister f1, FloatRegister f3, QP)                     { emit_f(ia64::FmergeS(f1->encoding(), f3->encoding(), f3->encoding(), Q), D().w(f1).r(f3).r(qp)); }

  // Single precision rounds to IEEE single (the .s completer): Java float
  // arithmetic needs it on every operation, since registers are 82 bits.
  void fadd_s(FloatRegister f1, FloatRegister f3, FloatRegister f2, QP) { emit_f(ia64::FaddS(f1->encoding(), f3->encoding(), f2->encoding(), ia64::sf0, Q), D().w(f1).r(f3).r(f2).r(qp)); }
  void fsub_s(FloatRegister f1, FloatRegister f3, FloatRegister f2, QP) { emit_f(ia64::FsubS(f1->encoding(), f3->encoding(), f2->encoding(), ia64::sf0, Q), D().w(f1).r(f3).r(f2).r(qp)); }
  void fmpy_s(FloatRegister f1, FloatRegister f3, FloatRegister f4, QP) { emit_f(ia64::FmpyS(f1->encoding(), f3->encoding(), f4->encoding(), ia64::sf0, Q), D().w(f1).r(f3).r(f4).r(qp)); }
  void fnorm_s(FloatRegister f1, FloatRegister f3, QP)                  { emit_f(ia64::FnormS(f1->encoding(), f3->encoding(), ia64::sf0, Q), D().w(f1).r(f3).r(qp)); }
  void fnorm_d(FloatRegister f1, FloatRegister f3, QP)                  { emit_f(ia64::FnormD(f1->encoding(), f3->encoding(), ia64::sf0, Q), D().w(f1).r(f3).r(qp)); }
  // Explicit-status-field forms, for multi-step sequences (the inline divide)
  // whose intermediate steps run on sf1. Linux starts every process with sf1
  // set to widest-range exponent, extended precision, round-to-nearest and
  // traps disabled (measured: tools/gate/fpsrprobe.c), which is what the
  // published IA-64 division algorithms assume. fma / fnma round to the
  // status field's precision (extended); fma_s / fma_d to IEEE single/double.
  void fma(FloatRegister f1, FloatRegister f3, FloatRegister f4, FloatRegister f2, ia64::FpSf sf, QP) {
    emit_f(ia64::Fma(f1->encoding(), f3->encoding(), f4->encoding(), f2->encoding(), sf, Q), D().w(f1).r(f3).r(f4).r(f2).r(qp));
  }
  void fnma(FloatRegister f1, FloatRegister f3, FloatRegister f4, FloatRegister f2, ia64::FpSf sf, QP) {
    emit_f(ia64::Fnma(f1->encoding(), f3->encoding(), f4->encoding(), f2->encoding(), sf, Q), D().w(f1).r(f3).r(f4).r(f2).r(qp));
  }
  void fma_s(FloatRegister f1, FloatRegister f3, FloatRegister f4, FloatRegister f2, ia64::FpSf sf, QP) {
    emit_f(ia64::FmaS(f1->encoding(), f3->encoding(), f4->encoding(), f2->encoding(), sf, Q), D().w(f1).r(f3).r(f4).r(f2).r(qp));
  }
  void fma_d(FloatRegister f1, FloatRegister f3, FloatRegister f4, FloatRegister f2, ia64::FpSf sf, QP) {
    emit_f(ia64::FmaD(f1->encoding(), f3->encoding(), f4->encoding(), f2->encoding(), sf, Q), D().w(f1).r(f3).r(f4).r(f2).r(qp));
  }
  // f1 ~= f2 / f3 (a reciprocal approximation of f3, scaled), p2 = whether the
  // software Newton-Raphson sequence must refine it. p2 is cleared when f1 is
  // already the IEEE result (zeros, infinities, NaNs, and the cases the
  // kernel's floating-point software assist completes), so every refinement
  // step is predicated on p2.
  void frcpa(FloatRegister f1, PredicateRegister p2, FloatRegister f2, FloatRegister f3, QP) {
    emit_f(ia64::Frcpa(f1->encoding(), p2.encoding(), f2->encoding(), f3->encoding(), ia64::sf0, Q), D().w(f1).w(p2).r(f2).r(f3).r(qp).fp_predicate());
  }
  // f1 ~= 1 / sqrt(f3), p2 = whether software refinement is needed; when p2
  // is cleared f1 already holds the IEEE square root (zeros, infinities,
  // NaNs, negative operands). vol. 3 frsqrta.
  void frsqrta(FloatRegister f1, PredicateRegister p2, FloatRegister f3, QP) {
    emit_f(ia64::Frsqrta(f1->encoding(), p2.encoding(), f3->encoding(), ia64::sf0, Q), D().w(f1).w(p2).r(f3).r(qp).fp_predicate());
  }
  // f1 = f3 with its sign inverted: exact, and NaN-preserving.
  void fneg(FloatRegister f1, FloatRegister f3, QP)                     { emit_f(ia64::FmergeNs(f1->encoding(), f3->encoding(), f3->encoding(), Q), D().w(f1).r(f3).r(qp)); }
  // f1 = the 64-bit signed integer in f2's significand, as a floating value
  // (exact in register format; round with fnorm_s / fnorm_d after).
  void fcvt_xf(FloatRegister f1, FloatRegister f2, QP)                  { emit_f(ia64::FcvtXf(f1->encoding(), f2->encoding(), Q), D().w(f1).r(f2).r(qp)); }
  // f1's significand = f2 converted to a signed 64-bit integer, rounding
  // toward zero. NaN and out-of-range values give the integer indefinite
  // 0x8000000000000000 (Invalid Operation is disabled; vol. 3 fcvt.fx).
  void fcvt_fx_trunc(FloatRegister f1, FloatRegister f2, ia64::FpSf sf, QP) { emit_f(ia64::FcvtFxTrunc(f1->encoding(), f2->encoding(), sf, Q), D().w(f1).r(f2).r(qp)); }
  void fcvt_fxu_trunc(FloatRegister f1, FloatRegister f2, ia64::FpSf sf, QP) { emit_f(ia64::FcvtFxuTrunc(f1->encoding(), f2->encoding(), sf, Q), D().w(f1).r(f2).r(qp)); }
  // p1 = relation, p2 = !relation; an unordered operand makes eq/lt/le false.
  void fcmp_eq(PredicateRegister p1, PredicateRegister p2, FloatRegister f2, FloatRegister f3, QP)    { emit_f(ia64::FcmpEq(p1.encoding(), p2.encoding(), f2->encoding(), f3->encoding(), ia64::sf0, Q), D().w(p1).w(p2).r(f2).r(f3).r(qp).fp_predicate()); }
  void fcmp_lt(PredicateRegister p1, PredicateRegister p2, FloatRegister f2, FloatRegister f3, QP)    { emit_f(ia64::FcmpLt(p1.encoding(), p2.encoding(), f2->encoding(), f3->encoding(), ia64::sf0, Q), D().w(p1).w(p2).r(f2).r(f3).r(qp).fp_predicate()); }
  void fcmp_le(PredicateRegister p1, PredicateRegister p2, FloatRegister f2, FloatRegister f3, QP)    { emit_f(ia64::FcmpLe(p1.encoding(), p2.encoding(), f2->encoding(), f3->encoding(), ia64::sf0, Q), D().w(p1).w(p2).r(f2).r(f3).r(qp).fp_predicate()); }
  void fcmp_unord(PredicateRegister p1, PredicateRegister p2, FloatRegister f2, FloatRegister f3, QP) { emit_f(ia64::FcmpUnord(p1.encoding(), p2.encoding(), f2->encoding(), f3->encoding(), ia64::sf0, Q), D().w(p1).w(p2).r(f2).r(f3).r(qp).fp_predicate()); }

  // r1 = r2 with its eight bytes reversed.
  void unpack1_l(Register r1, Register r2, Register r3, QP) { emit_i(ia64::Unpack1L(r1->encoding(), r2->encoding(), r3->encoding(), Q), D().w(r1).r(r2).r(r3).r(qp)); }
  void pack2_uss(Register r1, Register r2, Register r3, QP) { emit_i(ia64::Pack2Uss(r1->encoding(), r2->encoding(), r3->encoding(), Q), D().w(r1).r(r2).r(r3).r(qp)); }
  void mux1_brcst(Register r1, Register r2, QP) { emit_i(ia64::Mux1(r1->encoding(), r2->encoding(), ia64::kMux1Brcst, Q), D().w(r1).r(r2).r(qp)); }
  void mux2(Register r1, Register r2, uint32_t mht8, QP) { emit_i(ia64::Mux2(r1->encoding(), r2->encoding(), mht8, Q), D().w(r1).r(r2).r(qp)); }
  // Index of the lowest zero byte (0-7, else 8) / halfword (0-3, else 4).
  void czx1_r(Register r1, Register r3, QP) { emit_i(ia64::Czx1R(r1->encoding(), r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }
  void czx2_r(Register r1, Register r3, QP) { emit_i(ia64::Czx2R(r1->encoding(), r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }
  void mux1_rev(Register r1, Register r2, QP) { emit_i(ia64::Mux1(r1->encoding(), r2->encoding(), ia64::kMux1Rev, Q), D().w(r1).r(r2).r(qp)); }

  void getf_d(Register r1, FloatRegister f2, QP)   { emit_m(ia64::GetfD(r1->encoding(), f2->encoding(), Q), D().w6(r1).r(f2).r(qp)); }
  void setf_d(FloatRegister f1, Register r2, QP)   { emit_m(ia64::SetfD(f1->encoding(), r2->encoding(), Q), D().w(f1).r13(r2).r(qp)); }
  void getf_s(Register r1, FloatRegister f2, QP)   { emit_m(ia64::GetfS(r1->encoding(), f2->encoding(), Q), D().w6(r1).r(f2).r(qp)); }
  void setf_s(FloatRegister f1, Register r2, QP)   { emit_m(ia64::SetfS(f1->encoding(), r2->encoding(), Q), D().w(f1).r13(r2).r(qp)); }
  void getf_exp(Register r1, FloatRegister f2, QP) { emit_m(ia64::GetfExp(r1->encoding(), f2->encoding(), Q), D().w6(r1).r(f2).r(qp)); }
  void getf_sig(Register r1, FloatRegister f2, QP) { emit_m(ia64::GetfSig(r1->encoding(), f2->encoding(), Q), D().w6(r1).r(f2).r(qp)); }
  void setf_sig(FloatRegister f1, Register r2, QP) { emit_m(ia64::SetfSig(f1->encoding(), r2->encoding(), Q), D().w(f1).r13(r2).r(qp)); }
  // setf.exp: r2{16:0} becomes the exponent, r2{17} the sign, significand 1.0.
  void setf_exp(FloatRegister f1, Register r2, QP) { emit_m(ia64::SetfExp(f1->encoding(), r2->encoding(), Q), D().w(f1).r13(r2).r(qp)); }

  // Integer multiply goes through the FP significand path: the integer units
  // have no multiplier.
  void xma_l(FloatRegister f1, FloatRegister f3, FloatRegister f4, FloatRegister f2, QP) {
    emit_f(ia64::XmaL(f1->encoding(), f3->encoding(), f4->encoding(), f2->encoding(), Q), D().w(f1).r(f3).r(f4).r(f2).r(qp));
  }
  // The high 64 bits of the 128-bit product, signed (xma.h) and unsigned (xma.hu).
  void xma_h(FloatRegister f1, FloatRegister f3, FloatRegister f4, FloatRegister f2, QP) {
    emit_f(ia64::XmaH(f1->encoding(), f3->encoding(), f4->encoding(), f2->encoding(), Q), D().w(f1).r(f3).r(f4).r(f2).r(qp));
  }
  void xma_hu(FloatRegister f1, FloatRegister f3, FloatRegister f4, FloatRegister f2, QP) {
    emit_f(ia64::XmaHu(f1->encoding(), f3->encoding(), f4->encoding(), f2->encoding(), Q), D().w(f1).r(f3).r(f4).r(f2).r(qp));
  }
  // fnorm in register precision (no .s/.d) on status field sf: normalises a
  // setf.sig integer without rounding it (count leading zeros).
  void fnorm_reg(FloatRegister f1, FloatRegister f3, ia64::FpSf sf, QP) {
    emit_f(ia64::Fnorm(f1->encoding(), f3->encoding(), sf, Q), D().w(f1).r(f3).r(qp));
  }

#undef QP
#undef Q

 protected:
  // Displacement from the bundle being emitted to |dest|, in bundles.
  int32_t bundle_disp(address dest) { return bundle_disp(dest, pc()); }
  int32_t bundle_disp(address dest, address from) {
    intptr_t d = dest - from;
    assert(is_aligned(d, BytesPerBundle), "branch target must be bundle-aligned");
    d /= BytesPerBundle;
    guarantee(ia64::BranchDispInRange((int32_t)d), "branch out of +/-16 MiB range");
    return (int32_t)d;
  }
};

#endif // CPU_IA64_ASSEMBLER_IA64_HPP
