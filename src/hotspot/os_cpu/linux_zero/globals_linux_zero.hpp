/*
 * Copyright (c) 2000, 2024, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2007, 2008, 2010 Red Hat, Inc.
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

#ifndef OS_CPU_LINUX_ZERO_GLOBALS_LINUX_ZERO_HPP
#define OS_CPU_LINUX_ZERO_GLOBALS_LINUX_ZERO_HPP

//
// Set the default values for platform dependent flags used by the
// runtime system.  See globals.hpp for details of what they do.
//

#ifdef IA64
// IA-64 splits a thread's allocation between the memory stack and the RSE
// register backing store (see os::current_stack_base_and_size), so only half of
// what is requested is usable as memory stack. Ask for twice the usual figure
// so the effective size matches the other 64-bit Linux ports, which use 1024
// (x86), 2040 (aarch64) and 2048 (ppc, riscv).
define_pd_global(intx,  ThreadStackSize,         4096);  // 2048 usable
define_pd_global(intx,  VMThreadStackSize,       2048);  // 1024 usable
#else
define_pd_global(intx,  ThreadStackSize,         1536);
#ifdef _LP64
define_pd_global(intx,  VMThreadStackSize,       1024);
#else
define_pd_global(intx,  VMThreadStackSize,       512);
#endif // _LP64
#endif // IA64
define_pd_global(intx,  CompilerThreadStackSize, 0);
define_pd_global(size_t, JVMInvokeMethodSlack,   8192);

// Used on 64 bit platforms for UseCompressedOops base address
define_pd_global(size_t, HeapBaseMinAddress,     2*G);

#endif // OS_CPU_LINUX_ZERO_GLOBALS_LINUX_ZERO_HPP
