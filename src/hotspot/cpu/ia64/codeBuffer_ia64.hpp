/*
 * Copyright (c) 2020, 2025, Oracle and/or its affiliates. All rights reserved.
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

#ifndef CPU_IA64_CODEBUFFER_IA64_HPP
#define CPU_IA64_CODEBUFFER_IA64_HPP

private:
  void pd_initialize() {}

  // Shared stubs (one trampoline reused by several call sites) are an
  // optimisation, not a requirement; ppc, s390, arm and zero all decline them.
  // Declining is also the honest choice while there is no MacroAssembler to
  // emit them with -- emit_shared_stubs_to_interp<MacroAssembler> is what the
  // supporting ports call here.
  bool pd_finalize_stubs() {
    Unimplemented();
    return true;
  }

public:
  // Careful with the name: this has nothing to do with IA-64 instruction
  // bundles. It is the x86 scheduling-group hook, and every other port makes it
  // a no-op. Bundle formation for this port lives in the assembler, which emits
  // one instruction per 16-byte bundle with the stop bit always set.
  void flush_bundle(bool start_new_bundle) {}

  static constexpr bool supports_shared_stubs() { return false; }

#endif // CPU_IA64_CODEBUFFER_IA64_HPP
