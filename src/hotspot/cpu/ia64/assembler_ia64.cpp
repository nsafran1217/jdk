/*
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

#include "asm/assembler.hpp"
#include "asm/assembler.inline.hpp"

static uint8_t bundle_template(const ia64::Bundle* b) {
  return (uint8_t)(b->lo & 0x1f);
}

void Assembler::pd_patch_instruction(address branch, address target, const char* file, int line) {
  assert(is_aligned(branch, BytesPerBundle) && is_aligned(target, BytesPerBundle),
         "patch site and target must be bundle-aligned");
  ia64::Bundle* b = (ia64::Bundle*)branch;
  switch (bundle_template(b)) {
    case ia64::tMIB:
    case ia64::tMIB_: {
      // An IP-relative br.cond / br.call in slot 2 (Assembler::br_cond,
      // br_call). The displacement is from the branch's own bundle.
      intptr_t disp = (target - branch) / BytesPerBundle;
      guarantee(ia64::BranchDispInRange((int32_t)disp),
                "branch out of +/-16 MiB range at %s:%d", file, line);
      ia64::PatchBranchDisp(b, (int32_t)disp);
      break;
    }
    case ia64::tMLX:
    case ia64::tMLX_:
      // The movl of an la() sequence. Its base is the `mov r = ip` bundle
      // emitted immediately before it.
      ia64::WriteMovlImm(b, (uint64_t)(target - (branch - BytesPerBundle)));
      break;
    default:
      fatal("IA-64: unexpected bundle template 0x%x at a label patch site (%s:%d)",
            bundle_template(b), file, line);
  }
}

void Assembler::la(Register r1, Label& L, Register tmp) {
  assert_different_registers(r1, tmp);
  address base = pc();
  mov_from_ip(r1);
  address dest = target(L);    // registers this movl as the patch site if L is unbound
  assert(pc() == base + BytesPerBundle, "the movl must directly follow mov r = ip");
  movl(tmp, (uint64_t)(dest - base));
  add(r1, r1, tmp);
}

void Assembler::alloc(Register r1, uint32_t ins, uint32_t locals, uint32_t outs, uint32_t rot) {
#ifdef ASSERT
  // FRAME-DESIGN.md section 1: exactly one alloc, in call_stub. A second one
  // would make every br.ret in generated code a register-stack corruption.
  static int allocs_emitted = 0;
  assert(++allocs_emitted == 1, "IA-64: generated code must execute exactly one alloc (call_stub)");
#endif
  emit_m(ia64::Alloc(r1->encoding(), ins, locals, outs, rot));
}
