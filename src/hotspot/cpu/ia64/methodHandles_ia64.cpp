/*
 * Copyright (c) 1997, 2025, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, Red Hat Inc. All rights reserved.
 * Copyright (c) 2020, 2023, Huawei Technologies Co., Ltd. All rights reserved.
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
#include "compiler/disassembler.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "memory/allocation.inline.hpp"
#include "prims/jvmtiExport.hpp"
#include "prims/methodHandles.hpp"
#include "runtime/flags/flagSetting.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/stubRoutines.hpp"

#include "runtime/sharedRuntime.hpp"

// Derived from cpu/riscv/methodHandles_riscv.cpp. Register mapping: riscv
// xmethod / x19_sender_sp -> Rmethod / Rsender_sp, the argument locator x13
// -> r23, mh x11 -> r21, recv x12 -> r22. The three temporaries are r29-r31:
// neither Java (r20-r27) nor C (out0-out7) argument registers, so the same
// dispatch code stays valid for a compiled-code entry later.

#define __ Disassembler::hook<MacroAssembler>(__FILE__, __LINE__, _masm)->

#ifdef PRODUCT
#define BLOCK_COMMENT(str) /* nothing */
#else
#define BLOCK_COMMENT(str) __ block_comment(str)
#endif

#define BIND(label) bind(label); BLOCK_COMMENT(#label ":")

void MethodHandles::load_klass_from_Class(MacroAssembler* _masm, Register klass_reg) {
  if (VerifyMethodHandles) {
    verify_klass(_masm, klass_reg, VM_CLASS_ID(java_lang_Class),
                 "MH argument is a Class");
  }
  __ ld8(klass_reg, Address(klass_reg, java_lang_Class::klass_offset()));
}

#ifdef ASSERT
static int check_nonzero(const char* xname, int x) {
  assert(x != 0, "%s should be nonzero", xname);
  return x;
}
#define NONZERO(x) check_nonzero(#x, x)
#else //ASSERT
#define NONZERO(x) (x)
#endif //PRODUCT

#ifdef ASSERT
// Uses t2 and t3 only, so it can be dropped into any sequence here.
void MethodHandles::verify_klass(MacroAssembler* _masm,
                                 Register obj, vmClassID klass_id,
                                 const char* error_message) {
  InstanceKlass** klass_addr = vmClasses::klass_addr_at(klass_id);
  Klass* klass = vmClasses::klass_at(klass_id);
  Register temp1 = t2;
  Register temp2 = t3;
  assert_different_registers(obj, temp1, temp2);
  Label L_ok, L_bad;
  BLOCK_COMMENT("verify_klass {");
  __ verify_oop(obj);
  __ beqz(obj, L_bad);
  __ load_klass(temp1, obj);
  __ movl(temp2, (address)klass_addr);
  __ ld8(temp2, temp2);
  __ beq(temp1, temp2, L_ok);
  intptr_t super_check_offset = klass->super_check_offset();
  __ ld8(temp1, Address(temp1, super_check_offset));
  __ beq(temp1, temp2, L_ok);
  __ bind(L_bad);
  __ stop(error_message);
  __ BIND(L_ok);
  BLOCK_COMMENT("} verify_klass");
}

void MethodHandles::verify_ref_kind(MacroAssembler* _masm, int ref_kind, Register member_reg, Register temp) {}

#endif //ASSERT

void MethodHandles::jump_from_method_handle(MacroAssembler* _masm, Register method, Register temp,
                                            bool for_compiler_entry) {
  assert(method == Rmethod, "interpreter calling convention");
  Label L_no_such_method;
  __ beqz(Rmethod, L_no_such_method);

  if (!for_compiler_entry && JvmtiExport::can_post_interpreter_events()) {
    Label run_compiled_code;
    // JVMTI events, such as single-stepping, are implemented partly by avoiding running
    // compiled code in threads for which the event is enabled.  Check here for
    // interp_only_mode if these events CAN be enabled.

    __ ld4(temp, Address(Rthread, JavaThread::interp_only_mode_offset()));
    __ beqz(temp, run_compiled_code);
    __ ld8(temp, Address(method, Method::interpreter_entry_offset()));
    __ jr(temp);
    __ BIND(run_compiled_code);
  }

  const ByteSize entry_offset = for_compiler_entry ? Method::from_compiled_offset() :
                                                     Method::from_interpreted_offset();
  __ ld8(temp, Address(method, entry_offset));
  __ jr(temp);
  __ bind(L_no_such_method);
  __ far_jump(SharedRuntime::throw_AbstractMethodError_entry());
}

void MethodHandles::jump_to_lambda_form(MacroAssembler* _masm,
                                        Register recv, Register method_temp,
                                        Register temp2,
                                        bool for_compiler_entry) {
  BLOCK_COMMENT("jump_to_lambda_form {");
  // This is the initial entry point of a lazy method handle.
  // After type checking, it picks up the invoker from the LambdaForm.
  assert_different_registers(recv, method_temp, temp2);
  assert(recv != noreg, "required register");
  assert(method_temp == Rmethod, "required register for loading method");

  // Load the invoker, as MH -> MH.form -> LF.vmentry
  __ verify_oop(recv);
  __ load_heap_oop(method_temp, Address(recv, NONZERO(java_lang_invoke_MethodHandle::form_offset())), temp2, noreg);
  __ verify_oop(method_temp);
  __ load_heap_oop(method_temp, Address(method_temp, NONZERO(java_lang_invoke_LambdaForm::vmentry_offset())), temp2, noreg);
  __ verify_oop(method_temp);
  __ load_heap_oop(method_temp, Address(method_temp, NONZERO(java_lang_invoke_MemberName::method_offset())), temp2, noreg);
  __ verify_oop(method_temp);
  __ access_load_at(T_ADDRESS, IN_HEAP, method_temp, Address(method_temp, NONZERO(java_lang_invoke_ResolvedMethodName::vmtarget_offset())), noreg, noreg);

  if (VerifyMethodHandles && !for_compiler_entry) {
    // make sure recv is already on stack
    __ ld8(temp2, Address(method_temp, Method::const_offset()));
    __ ld2(temp2, Address(temp2, ConstMethod::size_of_parameters_offset()));
    Label L;
    // the first argument: Resp + (size_of_parameters - 1) words
    __ shladd(temp2, temp2, LogBytesPerWord, Resp);
    __ ld8(temp2, Address(temp2, -wordSize));
    __ beq(recv, temp2, L);
    __ stop("IA-64: method handle receiver is not on the stack");
    __ BIND(L);
  }

  jump_from_method_handle(_masm, method_temp, temp2, for_compiler_entry);
  BLOCK_COMMENT("} jump_to_lambda_form");
}

// Code generation
address MethodHandles::generate_method_handle_interpreter_entry(MacroAssembler* _masm,
                                                                vmIntrinsics::ID iid) {
  const bool not_for_compiler_entry = false;  // this is the interpreter entry
  assert(is_signature_polymorphic(iid), "expected invoke iid");
  if (iid == vmIntrinsics::_invokeGeneric ||
      iid == vmIntrinsics::_compiledLambdaForm) {
    // Perhaps surprisingly, the symbolic references visible to Java are not directly used.
    // They are linked to Java-generated adapters via MethodHandleNatives.linkMethod.
    // They all allow an appendix argument.
    __ brk(0);           // empty stubs make SG sick
    return nullptr;
  }

  // No need in interpreter entry for linkToNative for now.
  // Interpreter calls compiled entry through i2c.
  if (iid == vmIntrinsics::_linkToNative) {
    __ brk(0);
    return nullptr;
  }

  // Rsender_sp: sender SP (must preserve; see prepare_to_jump_from_interpreted)
  // Rmethod: Method*
  // argp: argument locator (parameter slot count, added to Resp)
  // mh: used as temp to hold mh or receiver
  Register argp   = r23;   // argument list ptr, live on error paths
  Register mh     = r21;   // MH receiver; dies quickly and is recycled

  // here's where control starts out:
  __ align(CodeEntryAlignment);
  address entry_point = __ pc();

  if (VerifyMethodHandles) {
    assert(Method::intrinsic_id_size_in_bytes() == 2, "assuming Method::_intrinsic_id is u2");

    Label L;
    BLOCK_COMMENT("verify_intrinsic_id {");
    __ ld2(t2, Address(Rmethod, Method::intrinsic_id_offset()));
    __ mov_immediate(t3, (int) iid);
    __ beq(t2, t3, L);
    __ stop("IA-64: bad Method*::intrinsic_id");
    __ bind(L);
    BLOCK_COMMENT("} verify_intrinsic_id");
  }

  // First task:  Find out how big the argument list is.
  int ref_kind = signature_polymorphic_intrinsic_ref_kind(iid);
  assert(ref_kind != 0 || iid == vmIntrinsics::_invokeBasic, "must be _invokeBasic or a linkTo intrinsic");
  bool have_first_arg = (ref_kind == 0 || MethodHandles::ref_kind_has_receiver(ref_kind));
  if (have_first_arg) {
    // argp = address of the first argument: Resp + (size_of_parameters - 1) words
    __ ld8(argp, Address(Rmethod, Method::const_offset()));
    __ ld2(argp, Address(argp, ConstMethod::size_of_parameters_offset()));
    __ shladd(argp, argp, LogBytesPerWord, Resp);
    __ adds(argp, -wordSize, argp);
  }
  const Address first_arg_addr(argp, 0);

  if (!is_signature_polymorphic_static(iid)) {
    assert(have_first_arg, "must be");
    __ ld8(mh, first_arg_addr);
  }

  // first_arg_addr is live!

  trace_method_handle_interpreter_entry(_masm, iid);
  if (iid == vmIntrinsics::_invokeBasic) {
    generate_method_handle_dispatch(_masm, iid, mh, noreg, not_for_compiler_entry);
  } else {
    // Adjust argument list by popping the trailing MemberName argument.
    Register recv = noreg;
    if (MethodHandles::ref_kind_has_receiver(ref_kind)) {
      // Load the receiver (not the MH; the actual MemberName's receiver) up from the interpreter stack.
      recv = r22;
      __ ld8(recv, first_arg_addr);
    }
    Register xmember = Rmethod;  // MemberName ptr; incoming method ptr is dead now
    __ ld8_inc(xmember, Resp, wordSize);   // extract last argument
    generate_method_handle_dispatch(_masm, iid, recv, xmember, not_for_compiler_entry);
  }

  return entry_point;
}

void MethodHandles::jump_to_native_invoker(MacroAssembler* _masm, Register nep_reg, Register temp_target) {
  // FFM is unsupported on IA-64 (foreignGlobals_ia64.cpp): no downcall stubs.
  Unimplemented();
}

void MethodHandles::generate_method_handle_dispatch(MacroAssembler* _masm,
                                                    vmIntrinsics::ID iid,
                                                    Register receiver_reg,
                                                    Register member_reg,
                                                    bool for_compiler_entry) {
  assert(is_signature_polymorphic(iid), "expected invoke iid");
  // temps used in this code are not used in *either* compiled or interpreted calling sequences
  Register temp1 = r29;
  Register temp2 = r30;
  Register temp3 = r31;
  if (for_compiler_entry) {
    assert(receiver_reg == (iid == vmIntrinsics::_linkToStatic || iid == vmIntrinsics::_linkToNative ? noreg : j_rarg0), "only valid assignment");
    assert_different_registers(temp1, j_rarg0, j_rarg1, j_rarg2, j_rarg3, j_rarg4, j_rarg5, j_rarg6, j_rarg7);
    assert_different_registers(temp2, j_rarg0, j_rarg1, j_rarg2, j_rarg3, j_rarg4, j_rarg5, j_rarg6, j_rarg7);
    assert_different_registers(temp3, j_rarg0, j_rarg1, j_rarg2, j_rarg3, j_rarg4, j_rarg5, j_rarg6, j_rarg7);
  }

  assert_different_registers(temp1, temp2, temp3, receiver_reg);
  assert_different_registers(temp1, temp2, temp3, member_reg);

  if (iid == vmIntrinsics::_invokeBasic) {
    // indirect through MH.form.vmentry.vmtarget
    jump_to_lambda_form(_masm, receiver_reg, Rmethod, temp1, for_compiler_entry);
  } else if (iid == vmIntrinsics::_linkToNative) {
    assert(for_compiler_entry, "only compiler entry is supported");
    jump_to_native_invoker(_masm, member_reg, temp1);
  } else {
    // The method is a member invoker used by direct method handles.
    if (VerifyMethodHandles) {
      // make sure the trailing argument really is a MemberName (caller responsibility)
      verify_klass(_masm, member_reg, VM_CLASS_ID(java_lang_invoke_MemberName),
                   "MemberName required for invokeVirtual etc.");
    }

    Address member_clazz(    member_reg, NONZERO(java_lang_invoke_MemberName::clazz_offset()));
    Address member_vmindex(  member_reg, NONZERO(java_lang_invoke_MemberName::vmindex_offset()));
    Address member_vmtarget( member_reg, NONZERO(java_lang_invoke_MemberName::method_offset()));
    Address vmtarget_method( Rmethod, NONZERO(java_lang_invoke_ResolvedMethodName::vmtarget_offset()));

    Register temp1_recv_klass = temp1;
    if (iid != vmIntrinsics::_linkToStatic) {
      __ verify_oop(receiver_reg);
      if (iid == vmIntrinsics::_linkToSpecial) {
        // Don't actually load the klass; just null-check the receiver.
        __ null_check(receiver_reg);
      } else {
        // load receiver klass itself
        __ load_klass(temp1_recv_klass, receiver_reg);
      }
      BLOCK_COMMENT("check_receiver {");
      // The receiver for the MemberName must be in receiver_reg.
      // Check the receiver against the MemberName.clazz
      if (VerifyMethodHandles && iid == vmIntrinsics::_linkToSpecial) {
        // Did not load it above...
        __ load_klass(temp1_recv_klass, receiver_reg);
      }
      if (VerifyMethodHandles && iid != vmIntrinsics::_linkToInterface) {
        Label L_ok;
        Register temp2_defc = temp2;
        __ load_heap_oop(temp2_defc, member_clazz, temp3, noreg);
        load_klass_from_Class(_masm, temp2_defc);
        __ check_klass_subtype(temp1_recv_klass, temp2_defc, temp3, t2, L_ok);
        // If we get here, the type check failed!
        __ stop("IA-64: method handle receiver type check failed");
        __ bind(L_ok);
      }
      BLOCK_COMMENT("} check_receiver");
    }

    // Live registers at this point:
    //  member_reg - MemberName that was the trailing argument
    //  temp1_recv_klass - klass of stacked receiver, if needed
    //  Rsender_sp - interpreter linkage (if interpreted)

    Label L_incompatible_class_change_error;
    switch (iid) {
      case vmIntrinsics::_linkToSpecial:
        if (VerifyMethodHandles) {
          verify_ref_kind(_masm, JVM_REF_invokeSpecial, member_reg, temp3);
        }
        __ load_heap_oop(Rmethod, member_vmtarget, temp3, noreg);
        __ access_load_at(T_ADDRESS, IN_HEAP, Rmethod, vmtarget_method, noreg, noreg);
        break;

      case vmIntrinsics::_linkToStatic:
        if (VerifyMethodHandles) {
          verify_ref_kind(_masm, JVM_REF_invokeStatic, member_reg, temp3);
        }
        __ load_heap_oop(Rmethod, member_vmtarget, temp3, noreg);
        __ access_load_at(T_ADDRESS, IN_HEAP, Rmethod, vmtarget_method, noreg, noreg);
        break;

      case vmIntrinsics::_linkToVirtual:
      {
        // same as TemplateTable::invokevirtual,
        // minus the CP setup and profiling:

        if (VerifyMethodHandles) {
          verify_ref_kind(_masm, JVM_REF_invokeVirtual, member_reg, temp3);
        }

        // pick out the vtable index from the MemberName, and then we can discard it:
        Register temp2_index = temp2;
        __ access_load_at(T_ADDRESS, IN_HEAP, temp2_index, member_vmindex, noreg, noreg);

        if (VerifyMethodHandles) {
          Label L_index_ok;
          __ cmp_lt(ptmp0, ptmp1, temp2_index, zr);
          __ br_cond(L_index_ok, ptmp1);
          __ stop("IA-64: negative vtable index in MemberName");
          __ BIND(L_index_ok);
        }

        // Note:  The verifier invariants allow us to ignore MemberName.clazz and vmtarget
        // at this point.  And VerifyMethodHandles has already checked clazz, if needed.

        // get target Method* & entry point
        __ lookup_virtual_method(temp1_recv_klass, temp2_index, Rmethod);
        break;
      }

      case vmIntrinsics::_linkToInterface:
      {
        // same as TemplateTable::invokeinterface
        // (minus the CP setup and profiling, with different argument motion)
        if (VerifyMethodHandles) {
          verify_ref_kind(_masm, JVM_REF_invokeInterface, member_reg, temp3);
        }

        Register temp3_intf = temp3;
        __ load_heap_oop(temp3_intf, member_clazz, temp2, noreg);
        load_klass_from_Class(_masm, temp3_intf);

        Register rindex = Rmethod;
        __ access_load_at(T_ADDRESS, IN_HEAP, rindex, member_vmindex, noreg, noreg);
        if (VerifyMethodHandles) {
          Label L;
          __ cmp_lt(ptmp0, ptmp1, rindex, zr);
          __ br_cond(L, ptmp1);
          __ stop("IA-64: negative itable index in MemberName");
          __ bind(L);
        }

        // given intf, index, and recv klass, dispatch to the implementation method
        __ lookup_interface_method(temp1_recv_klass, temp3_intf,
                                   // note: next two args must be the same:
                                   rindex, Rmethod,
                                   temp2,
                                   L_incompatible_class_change_error);
        break;
      }

      default:
        fatal("unexpected intrinsic %d: %s", vmIntrinsics::as_int(iid), vmIntrinsics::name_at(iid));
        break;
    }

    // live at this point:  Rmethod, Rsender_sp (if interpreted)

    // After figuring out which concrete method to call, jump into it.
    // Note that this works in the interpreter with no data motion.
    jump_from_method_handle(_masm, Rmethod, temp1, for_compiler_entry);
    if (iid == vmIntrinsics::_linkToInterface) {
      __ bind(L_incompatible_class_change_error);
      __ far_jump(SharedRuntime::throw_IncompatibleClassChangeError_entry());
    }
  }
}

#ifndef PRODUCT
void MethodHandles::trace_method_handle(MacroAssembler* _masm, const char* adaptername) {  }
#endif //PRODUCT
