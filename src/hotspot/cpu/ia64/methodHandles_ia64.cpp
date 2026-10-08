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
#include "classfile/javaClasses.inline.hpp"
#include "classfile/vmClasses.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "memory/allocation.inline.hpp"
#include "prims/jvmtiExport.hpp"
#include "prims/methodHandles.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/stubRoutines.hpp"

#define __ _masm->

// Method-handle intrinsics (invokeBasic, linkToStatic, ...). The interpreter
// entries are generated at startup for every intrinsic kind; until they are
// written (JIT-SCOPE.md phase 5) each one traps when a method handle is
// actually invoked, naming the intrinsic.

address MethodHandles::generate_method_handle_interpreter_entry(MacroAssembler* _masm,
                                                                vmIntrinsics::ID iid) {
  address entry_point = __ pc();
  const char* buf;
  {
    ResourceMark rm;
    stringStream ss;
    ss.print("IA-64: method handle intrinsic %s", vmIntrinsics::name_at(iid));
    buf = __ code_string(ss.as_string());
  }
  __ stop(buf);
  return entry_point;
}

void MethodHandles::trace_method_handle(MacroAssembler* _masm, const char* adaptername) {
}
