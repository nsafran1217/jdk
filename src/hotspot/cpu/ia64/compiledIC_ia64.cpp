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
#include "asm/macroAssembler.hpp"
#include "code/compiledIC.hpp"
#include "code/nmethod.hpp"
#include "runtime/mutexLocker.hpp"

// Compiled-to-compiled and compiled-to-interpreted static calls only exist
// once there is a compiler: none of this is reachable in the core variant.
// It arrives with C1 (milestone 2).

void CompiledDirectCall::set_to_interpreted(const methodHandle& callee, address entry) {
  ShouldNotReachHere();
}

void CompiledDirectCall::set_stub_to_clean(static_stub_Relocation* static_stub) {
  ShouldNotReachHere();
}

#ifndef PRODUCT
void CompiledDirectCall::verify() {
  ShouldNotReachHere();
}
#endif
