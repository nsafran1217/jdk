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
  // Anything not described by a Deps -- implicit resources (ar.ccv, ar.unat,
  // ar.pfs/CFM, alloc, calls, mf, break), raw bundles, movl -- is a barrier:
  // its stop stays and the group restarts after it. Bundles that code
  // recognisers or patchers inspect by exact encoding (movl, mov to a branch
  // register, branches) always keep their own stop; only plain M/I/F bundles
  // ever lose one. Checked by tools/depcheck.py (as -xexplicit).
  struct Deps {
    enum Cls : uint8_t { GR = 0, FR = 1, PR = 2, BR = 3 };
    uint8_t  nrd = 0, nwr = 0;
    uint8_t  rd_cls[6], rd_num[6];
    uint8_t  wr_cls[3], wr_num[3];
    bool     is_branch = false;   // see the special case above
    bool     fp_pred   = false;   // writes predicates as an FP instruction
    bool     keep_stop = false;   // this bundle keeps its own stop
    Deps& rd(Cls c, int n) { assert(nrd < 6, "deps"); rd_cls[nrd] = c; rd_num[nrd++] = (uint8_t)n; return *this; }
    Deps& wr(Cls c, int n) { assert(nwr < 3, "deps"); wr_cls[nwr] = c; wr_num[nwr++] = (uint8_t)n; return *this; }
    Deps& r(Register x)          { return x->encoding() == 0 ? *this : rd(GR, x->encoding()); }
    Deps& w(Register x)          { return wr(GR, x->encoding()); }
    Deps& r(FloatRegister x)     { return x->encoding() <= 1 ? *this : rd(FR, x->encoding()); }
    Deps& w(FloatRegister x)     { return wr(FR, x->encoding()); }
    Deps& r(PredicateRegister x) { return x.encoding() == 0 ? *this : rd(PR, x.encoding()); }
    Deps& w(PredicateRegister x) { return x.encoding() == 0 ? *this : wr(PR, x.encoding()); }
    Deps& r(BranchRegister x)    { return rd(BR, x.encoding()); }
    Deps& w(BranchRegister x)    { return wr(BR, x.encoding()); }
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
  enum Unit : uint8_t { U_M, U_I, U_A, U_F };

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
      flush_window();
      return AbstractAssembler::offset();
    }
    close_bundle();
    return AbstractAssembler::offset();
  }
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
  //    after it in program order may execute before it faults.
  static const int kSchedWindow = 24;
  struct WinInsn { ia64::Insn insn; Unit u; Deps d; bool ordered; };
  WinInsn _win[kSchedWindow];
  int     _win_n = 0;

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
      case U_A: break;
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

  void flush_window() {
    if (_win_n == 0) return;
    const int n = _win_n;
    _win_n = 0;                         // pack_unit below must not re-enter
    uint32_t pred[kSchedWindow], strict_succ[kSchedWindow];
    for (int j = 0; j < n; j++) { pred[j] = 0; strict_succ[j] = 0; }
    for (int j = 1; j < n; j++) {
      pred[j] |= 1;                     // the first instruction stays first
      for (int i = 0; i < j; i++) {
        bool strict;
        if (depends(_win[i], _win[j], &strict)) {
          pred[j] |= 1u << i;
          if (strict) strict_succ[i] |= 1u << j;
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
    uint32_t done = 0;
    uint64_t gw[4][2] = {};             // registers written in the group being formed
    int order[kSchedWindow];
    for (int k = 0; k < n; k++) {
      int best = -1;
      for (int pass = 0; pass < 2 && best < 0; pass++) {
        if (pass == 1) memset(gw, 0, sizeof(gw));    // nothing fits: start a new group
        for (int j = 0; j < n; j++) {
          if (((done >> j) & 1) || (pred[j] & ~done) != 0) continue;
          const Deps& d = _win[j].d;
          bool clash = false;
          for (int i = 0; i < d.nrd && !clash; i++) clash = (gw[d.rd_cls[i]][d.rd_num[i] >> 6] >> (d.rd_num[i] & 63)) & 1;
          for (int i = 0; i < d.nwr && !clash; i++) clash = (gw[d.wr_cls[i]][d.wr_num[i] >> 6] >> (d.wr_num[i] & 63)) & 1;
          if (clash) continue;
          if (best < 0 || height[j] > height[best]) best = j;
        }
      }
      assert(best >= 0, "a ready instruction always fits an empty group");
      order[k] = best;
      done |= 1u << best;
      const Deps& d = _win[best].d;
      for (int i = 0; i < d.nwr; i++) gw[d.wr_cls[i]][d.wr_num[i] >> 6] |= (uint64_t)1 << (d.wr_num[i] & 63);
    }
    for (int k = 0; k < n; k++) {
      const WinInsn& w = _win[order[k]];
      pack_unit(w.u, w.insn, w.d);
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

  void emit_unit(Unit u, ia64::Insn insn, const Deps& d0) {
    Deps d = d0;
    if (_pack_depth > 0 && UseBundleScheduling && is_store(u, insn) && d.nwr < 3) {
      d.wr(Deps::BR, kStoreSlot);
    }
    if (_pack_depth > 0 && UseBundleScheduling) {
      if (d.is_branch || d.keep_stop) {
        flush_window();
        pack_unit(u, insn, d);
        return;
      }
      if (_win_n == kSchedWindow) flush_window();
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

  void ld1(Register r1, Register r3, QP)  { emit_m(ia64::Ld1(r1->encoding(), r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }
  void ld2(Register r1, Register r3, QP)  { emit_m(ia64::Ld2(r1->encoding(), r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }
  void ld4(Register r1, Register r3, QP)  { emit_m(ia64::Ld4(r1->encoding(), r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }
  void ld8(Register r1, Register r3, QP)  { emit_m(ia64::Ld8(r1->encoding(), r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }

  void st1(Register r3, Register r2, QP)  { emit_m(ia64::St1(r3->encoding(), r2->encoding(), Q), D().r(r3).r(r2).r(qp)); }
  void st2(Register r3, Register r2, QP)  { emit_m(ia64::St2(r3->encoding(), r2->encoding(), Q), D().r(r3).r(r2).r(qp)); }
  void st4(Register r3, Register r2, QP)  { emit_m(ia64::St4(r3->encoding(), r2->encoding(), Q), D().r(r3).r(r2).r(qp)); }
  void st8(Register r3, Register r2, QP)  { emit_m(ia64::St8(r3->encoding(), r2->encoding(), Q), D().r(r3).r(r2).r(qp)); }

  // Post-increment: access [r3], then r3 += imm9 (signed 9 bits). The one
  // addressing mode beyond a bare register; it is how the expression stack is
  // pushed and popped without a separate adds.
  void ld1_inc(Register r1, Register r3, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::Ld1Inc(r1->encoding(), r3->encoding(), imm9, Q), D().w(r1).w(r3).r(r3).r(qp)); }
  void ld2_inc(Register r1, Register r3, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::Ld2Inc(r1->encoding(), r3->encoding(), imm9, Q), D().w(r1).w(r3).r(r3).r(qp)); }
  void ld4_inc(Register r1, Register r3, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::Ld4Inc(r1->encoding(), r3->encoding(), imm9, Q), D().w(r1).w(r3).r(r3).r(qp)); }
  void ld8_inc(Register r1, Register r3, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::Ld8Inc(r1->encoding(), r3->encoding(), imm9, Q), D().w(r1).w(r3).r(r3).r(qp)); }
  void st1_inc(Register r3, Register r2, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::St1Inc(r3->encoding(), r2->encoding(), imm9, Q), D().w(r3).r(r3).r(r2).r(qp)); }
  void st2_inc(Register r3, Register r2, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::St2Inc(r3->encoding(), r2->encoding(), imm9, Q), D().w(r3).r(r3).r(r2).r(qp)); }
  void st4_inc(Register r3, Register r2, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::St4Inc(r3->encoding(), r2->encoding(), imm9, Q), D().w(r3).r(r3).r(r2).r(qp)); }
  void st8_inc(Register r3, Register r2, int imm9, QP) { assert(ia64::is_simm9(imm9), "imm9"); emit_m(ia64::St8Inc(r3->encoding(), r2->encoding(), imm9, Q), D().w(r3).r(r3).r(r2).r(qp)); }

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
  void lfetch(Register r3, bool excl = false, uint32_t hint = 0, QP) { emit_m(ia64::Lfetch(r3->encoding(), excl, hint, Q), D().r(r3).r(qp)); }
  void ldfs(FloatRegister f1, Register r3, QP) { emit_m(ia64::Ldfs(f1->encoding(), r3->encoding(), Q), D().w(f1).r(r3).r(qp)); }
  void ldfd(FloatRegister f1, Register r3, QP) { emit_m(ia64::Ldfd(f1->encoding(), r3->encoding(), Q), D().w(f1).r(r3).r(qp)); }
  void stfs(Register r3, FloatRegister f2, QP) { emit_m(ia64::Stfs(r3->encoding(), f2->encoding(), Q), D().r(r3).r(f2).r(qp)); }
  void stfd(Register r3, FloatRegister f2, QP) { emit_m(ia64::Stfd(r3->encoding(), f2->encoding(), Q), D().r(r3).r(f2).r(qp)); }

  // Full 82-bit FP save/restore, 16 bytes and 16-byte aligned. Required for
  // f2-f5 and f16-f31 in call_stub: a double round-trip would silently
  // truncate a C caller's long double.
  void ldf_fill(FloatRegister f1, Register r3, QP)  { emit_m(ia64::LdfFill(f1->encoding(), r3->encoding(), Q)); }
  void stf_spill(Register r3, FloatRegister f2, QP) { emit_m(ia64::StfSpill(r3->encoding(), f2->encoding(), Q)); }

  // ---- ALU (A unit: an M or an I slot) -------------------------------------

  void add(Register r1, Register r2, Register r3, QP)   { emit_a(ia64::Add(r1->encoding(), r2->encoding(), r3->encoding(), Q), D().w(r1).r(r2).r(r3).r(qp)); }
  void sub(Register r1, Register r2, Register r3, QP)   { emit_a(ia64::Sub(r1->encoding(), r2->encoding(), r3->encoding(), Q), D().w(r1).r(r2).r(r3).r(qp)); }
  void and_(Register r1, Register r2, Register r3, QP)  { emit_a(ia64::And(r1->encoding(), r2->encoding(), r3->encoding(), Q), D().w(r1).r(r2).r(r3).r(qp)); }
  void andcm(Register r1, Register r2, Register r3, QP) { emit_a(ia64::Andcm(r1->encoding(), r2->encoding(), r3->encoding(), Q), D().w(r1).r(r2).r(r3).r(qp)); }
  void or_(Register r1, Register r2, Register r3, QP)   { emit_a(ia64::Or(r1->encoding(), r2->encoding(), r3->encoding(), Q), D().w(r1).r(r2).r(r3).r(qp)); }
  void xor_(Register r1, Register r2, Register r3, QP)  { emit_a(ia64::Xor(r1->encoding(), r2->encoding(), r3->encoding(), Q), D().w(r1).r(r2).r(r3).r(qp)); }

  // The 8-bit immediate forms. Note the immediate is the FIRST source:
  // sub_imm(r1, imm8, r3) computes imm8 - r3 (so negation is sub_imm(r1, 0, r3)).
  void and_imm(Register r1, int64_t imm8, Register r3, QP) { assert(ia64::is_simm8(imm8), "imm8"); emit_a(ia64::AndImm(r1->encoding(), imm8, r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }
  void or_imm(Register r1, int64_t imm8, Register r3, QP)  { assert(ia64::is_simm8(imm8), "imm8"); emit_a(ia64::OrImm(r1->encoding(), imm8, r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }
  void xor_imm(Register r1, int64_t imm8, Register r3, QP) { assert(ia64::is_simm8(imm8), "imm8"); emit_a(ia64::XorImm(r1->encoding(), imm8, r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }
  void sub_imm(Register r1, int64_t imm8, Register r3, QP) { assert(ia64::is_simm8(imm8), "imm8"); emit_a(ia64::SubImm(r1->encoding(), imm8, r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }

  // r1 = (r2 << count) + r3, count 1..4: base + index * scale in one step.
  void shladd(Register r1, Register r2, int count, Register r3, QP) {
    emit_a(ia64::Shladd(r1->encoding(), r2->encoding(), count, r3->encoding(), Q), D().w(r1).r(r2).r(r3).r(qp));
  }

  // adds is also the register move (imm == 0) and the only way to add a small
  // constant; anything wider than 14 bits signed needs movl + add.
  void adds(Register r1, int64_t imm14, Register r3, QP) {
    assert(ia64::is_simm14(imm14), "immediate too wide for adds -- use movl + add");
    emit_a(ia64::Adds(r1->encoding(), imm14, r3->encoding(), Q), D().w(r1).r(r3).r(qp));
  }
  void mov(Register r1, Register r3, QP) { emit_a(ia64::MovReg(r1->encoding(), r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }

  // ---- compare (A unit) --------------------------------------------------
  //
  // Each writes p1 = relation and p2 = !relation. The 64-bit forms compare
  // whole registers; the cmp4 forms only the low 32 bits, which is what Java
  // int comparisons want. The immediate forms take the 8-bit immediate as the
  // FIRST operand: cmp_lt_imm(p1, p2, 5, r) tests 5 < r.

  void cmp_eq(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_a(ia64::CmpEq(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r(r2).r(r3).r(qp)); }
  void cmp_ne(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_a(ia64::CmpNe(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r(r2).r(r3).r(qp)); }
  void cmp_lt(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_a(ia64::CmpLt(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r(r2).r(r3).r(qp)); }
  void cmp_ltu(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP) { emit_a(ia64::CmpLtu(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r(r2).r(r3).r(qp)); }

  void cmp4_eq(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_a(ia64::Cmp4Eq(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r(r2).r(r3).r(qp)); }
  void cmp4_ne(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_a(ia64::Cmp4Ne(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r(r2).r(r3).r(qp)); }
  void cmp4_lt(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP)  { emit_a(ia64::Cmp4Lt(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r(r2).r(r3).r(qp)); }
  void cmp4_ltu(PredicateRegister p1, PredicateRegister p2, Register r2, Register r3, QP) { emit_a(ia64::Cmp4Ltu(p1.encoding(), p2.encoding(), r2->encoding(), r3->encoding(), Q), D().w(p1).w(p2).r(r2).r(r3).r(qp)); }

#define CMP_IMM(name, Enc) \
  void name(PredicateRegister p1, PredicateRegister p2, int64_t imm8, Register r3, QP) { \
    assert(ia64::is_simm8(imm8), "imm8"); \
    emit_a(ia64::Enc(p1.encoding(), p2.encoding(), imm8, r3->encoding(), Q), D().w(p1).w(p2).r(r3).r(qp)); \
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
  void tbit_z(PredicateRegister p1, PredicateRegister p2, Register r3, int pos, QP)  { emit_i(ia64::TbitZ(p1.encoding(), p2.encoding(), r3->encoding(), pos, Q), D().w(p1).w(p2).r(r3).r(qp)); }
  void tbit_nz(PredicateRegister p1, PredicateRegister p2, Register r3, int pos, QP) { emit_i(ia64::TbitNz(p1.encoding(), p2.encoding(), r3->encoding(), pos, Q), D().w(p1).w(p2).r(r3).r(qp)); }

  // ---- shifts, extends (I unit) ------------------------------------------

  void shl(Register r1, Register value, Register count, QP)  { emit_i(ia64::Shl(r1->encoding(), value->encoding(), count->encoding(), Q), D().w(r1).r(value).r(count).r(qp)); }
  void shr(Register r1, Register value, Register count, QP)  { emit_i(ia64::Shr(r1->encoding(), value->encoding(), count->encoding(), Q), D().w(r1).r(value).r(count).r(qp)); }
  void shru(Register r1, Register value, Register count, QP) { emit_i(ia64::ShrU(r1->encoding(), value->encoding(), count->encoding(), Q), D().w(r1).r(value).r(count).r(qp)); }

  void shl_imm(Register r1, Register r2, uint32_t count, QP)  { emit_i(ia64::ShlImm(r1->encoding(), r2->encoding(), count, Q), D().w(r1).r(r2).r(qp)); }
  void shr_imm(Register r1, Register r3, uint32_t count, QP)  { emit_i(ia64::ShrImm(r1->encoding(), r3->encoding(), count, Q), D().w(r1).r(r3).r(qp)); }
  void shru_imm(Register r1, Register r3, uint32_t count, QP) { emit_i(ia64::ShrUImm(r1->encoding(), r3->encoding(), count, Q), D().w(r1).r(r3).r(qp)); }

  void extr_u(Register r1, Register r3, uint32_t pos, uint32_t len, QP) { emit_i(ia64::ExtrU(r1->encoding(), r3->encoding(), pos, len, Q), D().w(r1).r(r3).r(qp)); }
  void extr(Register r1, Register r3, uint32_t pos, uint32_t len, QP)   { emit_i(ia64::Extr(r1->encoding(), r3->encoding(), pos, len, Q), D().w(r1).r(r3).r(qp)); }
  void dep_z(Register r1, Register r2, uint32_t pos, uint32_t len, QP)  { emit_i(ia64::DepZ(r1->encoding(), r2->encoding(), pos, len, Q), D().w(r1).r(r2).r(qp)); }

  void sxt1(Register r1, Register r3, QP) { emit_i(ia64::Sxt1(r1->encoding(), r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }
  void sxt2(Register r1, Register r3, QP) { emit_i(ia64::Sxt2(r1->encoding(), r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }
  void sxt4(Register r1, Register r3, QP) { emit_i(ia64::Sxt4(r1->encoding(), r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }
  void zxt1(Register r1, Register r3, QP) { emit_i(ia64::Zxt1(r1->encoding(), r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }
  void zxt2(Register r1, Register r3, QP) { emit_i(ia64::Zxt2(r1->encoding(), r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }
  void zxt4(Register r1, Register r3, QP) { emit_i(ia64::Zxt4(r1->encoding(), r3->encoding(), Q), D().w(r1).r(r3).r(qp)); }

  // shrp r1 = r2, r3, count: the 128-bit r2:r3 shifted right by count, low
  // 64 bits (a rotate when r2 == r3).
  void shrp(Register r1, Register r2, Register r3, uint32_t count, QP) { emit_i(ia64::Shrp(r1->encoding(), r2->encoding(), r3->encoding(), count, Q), D().w(r1).r(r2).r(r3).r(qp)); }
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

  void mov_to_ar_ccv(Register r2, QP) { emit_m(ia64::MovToArCcv(r2->encoding(), Q)); }

  void cmpxchg4_acq(Register r1, Register r3, Register r2, QP) { emit_m(ia64::Cmpxchg4Acq(r1->encoding(), r3->encoding(), r2->encoding(), Q)); }
  void cmpxchg8_acq(Register r1, Register r3, Register r2, QP) { emit_m(ia64::Cmpxchg8Acq(r1->encoding(), r3->encoding(), r2->encoding(), Q)); }
  void cmpxchg4_rel(Register r1, Register r3, Register r2, QP) { emit_m(ia64::Cmpxchg4Rel(r1->encoding(), r3->encoding(), r2->encoding(), Q)); }
  void cmpxchg8_rel(Register r1, Register r3, Register r2, QP) { emit_m(ia64::Cmpxchg8Rel(r1->encoding(), r3->encoding(), r2->encoding(), Q)); }
  void xchg4(Register r1, Register r3, Register r2, QP)        { emit_m(ia64::Xchg4(r1->encoding(), r3->encoding(), r2->encoding(), Q)); }
  void xchg8(Register r1, Register r3, Register r2, QP)        { emit_m(ia64::Xchg8(r1->encoding(), r3->encoding(), r2->encoding(), Q)); }

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
  void br_call(BranchRegister b1, Label& L, QP) {
    _branch_reg_epoch++;
    close_bundle();
    address dest = target(L);
    emit_b(ia64::BrCallRel(b1.encoding(), bundle_disp(dest), Q));
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

  void getf_d(Register r1, FloatRegister f2, QP)   { emit_m(ia64::GetfD(r1->encoding(), f2->encoding(), Q), D().w(r1).r(f2).r(qp)); }
  void setf_d(FloatRegister f1, Register r2, QP)   { emit_m(ia64::SetfD(f1->encoding(), r2->encoding(), Q), D().w(f1).r(r2).r(qp)); }
  void getf_s(Register r1, FloatRegister f2, QP)   { emit_m(ia64::GetfS(r1->encoding(), f2->encoding(), Q), D().w(r1).r(f2).r(qp)); }
  void setf_s(FloatRegister f1, Register r2, QP)   { emit_m(ia64::SetfS(f1->encoding(), r2->encoding(), Q), D().w(f1).r(r2).r(qp)); }
  void getf_exp(Register r1, FloatRegister f2, QP) { emit_m(ia64::GetfExp(r1->encoding(), f2->encoding(), Q), D().w(r1).r(f2).r(qp)); }
  void getf_sig(Register r1, FloatRegister f2, QP) { emit_m(ia64::GetfSig(r1->encoding(), f2->encoding(), Q), D().w(r1).r(f2).r(qp)); }
  void setf_sig(FloatRegister f1, Register r2, QP) { emit_m(ia64::SetfSig(f1->encoding(), r2->encoding(), Q), D().w(f1).r(r2).r(qp)); }

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
