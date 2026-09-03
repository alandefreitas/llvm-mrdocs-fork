//===- LLToken.h - Token Codes for LLVM Assembly Files ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the enums for the .ll lexer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ASMPARSER_LLTOKEN_H
#define LLVM_ASMPARSER_LLTOKEN_H

namespace llvm {
/// Token codes for the LLVM IR assembly (.ll) lexer.
namespace lltok {
/// Lexical token kinds recognized in LLVM IR assembly.
enum Kind {
  // Markers
  Eof, ///< End of file.
  Error, ///< Lexical error.

  // Tokens with no info.
  dotdotdot, ///< Ellipsis "...".
  equal, ///< Equals sign "=".
  comma, ///< Comma ",".
  star, ///< Asterisk "*".
  lsquare, ///< Left square bracket "[".
  rsquare, ///< Right square bracket "]".
  lbrace, ///< Left brace "{".
  rbrace, ///< Right brace "}".
  less, ///< Less-than "<".
  greater, ///< Greater-than ">".
  lparen, ///< Left parenthesis "(".
  rparen, ///< Right parenthesis ")".
  exclaim, ///< Exclamation mark "!".
  bar, ///< Vertical bar "|".
  colon, ///< Colon ":".
  hash, ///< Hash "#".

  kw_vscale, ///< Keyword "vscale".
  kw_x, ///< Keyword "x".
  kw_true, ///< Keyword "true".
  kw_false, ///< Keyword "false".
  kw_declare, ///< Keyword "declare".
  kw_define, ///< Keyword "define".
  kw_global, ///< Keyword "global".
  kw_constant, ///< Keyword "constant".

  kw_dso_local, ///< Keyword "dso_local".
  kw_dso_preemptable, ///< Keyword "dso_preemptable".

  kw_private, ///< Keyword "private".
  kw_internal, ///< Keyword "internal".
  kw_linkonce, ///< Keyword "linkonce".
  kw_linkonce_odr, ///< Keyword "linkonce_odr".
  kw_weak, ///< Keyword "weak" (linkage, or cmpxchg modifier).
  kw_weak_odr, ///< Keyword "weak_odr".
  kw_appending, ///< Keyword "appending".
  kw_dllimport, ///< Keyword "dllimport".
  kw_dllexport, ///< Keyword "dllexport".
  kw_common, ///< Keyword "common".
  kw_available_externally, ///< Keyword "available_externally".
  kw_default, ///< Keyword "default".
  kw_hidden, ///< Keyword "hidden".
  kw_protected, ///< Keyword "protected".
  kw_unnamed_addr, ///< Keyword "unnamed_addr".
  kw_local_unnamed_addr, ///< Keyword "local_unnamed_addr".
  kw_externally_initialized, ///< Keyword "externally_initialized".
  kw_extern_weak, ///< Keyword "extern_weak".
  kw_external, ///< Keyword "external".
  kw_thread_local, ///< Keyword "thread_local".
  kw_localdynamic, ///< Keyword "localdynamic".
  kw_initialexec, ///< Keyword "initialexec".
  kw_localexec, ///< Keyword "localexec".
  kw_zeroinitializer, ///< Keyword "zeroinitializer".
  kw_undef, ///< Keyword "undef".
  kw_poison, ///< Keyword "poison".
  kw_null, ///< Keyword "null".
  kw_none, ///< Keyword "none".
  kw_to, ///< Keyword "to".
  kw_caller, ///< Keyword "caller".
  kw_within, ///< Keyword "within".
  kw_from, ///< Keyword "from".
  kw_tail, ///< Keyword "tail".
  kw_musttail, ///< Keyword "musttail".
  kw_notail, ///< Keyword "notail".
  kw_target, ///< Keyword "target".
  kw_triple, ///< Keyword "triple".
  kw_source_filename, ///< Keyword "source_filename".
  kw_unwind, ///< Keyword "unwind".
  kw_datalayout, ///< Keyword "datalayout".
  kw_volatile, ///< Keyword "volatile".
  kw_elementwise, ///< Keyword "elementwise".
  kw_atomic, ///< Keyword "atomic".
  kw_unordered, ///< Keyword "unordered".
  kw_monotonic, ///< Keyword "monotonic".
  kw_acquire, ///< Keyword "acquire".
  kw_release, ///< Keyword "release".
  kw_acq_rel, ///< Keyword "acq_rel".
  kw_seq_cst, ///< Keyword "seq_cst".
  kw_syncscope, ///< Keyword "syncscope".
  kw_nnan, ///< Keyword "nnan".
  kw_ninf, ///< Keyword "ninf".
  kw_nsz, ///< Keyword "nsz".
  kw_arcp, ///< Keyword "arcp".
  kw_contract, ///< Keyword "contract".
  kw_reassoc, ///< Keyword "reassoc".
  kw_afn, ///< Keyword "afn".
  kw_fast, ///< Keyword "fast".
  kw_nuw, ///< Keyword "nuw".
  kw_nsw, ///< Keyword "nsw".
  kw_nusw, ///< Keyword "nusw".
  kw_exact, ///< Keyword "exact".
  kw_disjoint, ///< Keyword "disjoint".
  kw_inbounds, ///< Keyword "inbounds".
  kw_nneg, ///< Keyword "nneg".
  kw_samesign, ///< Keyword "samesign".
  kw_inrange, ///< Keyword "inrange".
  kw_addrspace, ///< Keyword "addrspace".
  kw_section, ///< Keyword "section".
  kw_partition, ///< Keyword "partition".
  kw_code_model, ///< Keyword "code_model".
  kw_alias, ///< Keyword "alias".
  kw_ifunc, ///< Keyword "ifunc".
  kw_module, ///< Keyword "module".
  kw_asm, ///< Keyword "asm".
  kw_sideeffect, ///< Keyword "sideeffect".
  kw_inteldialect, ///< Keyword "inteldialect".
  kw_gc, ///< Keyword "gc".
  kw_prefix, ///< Keyword "prefix".
  kw_prologue, ///< Keyword "prologue".
  kw_c, ///< Keyword "c".
  kw_prefalign, ///< Keyword "prefalign".

  kw_cc, ///< Keyword "cc".
  kw_ccc, ///< Keyword "ccc".
  kw_fastcc, ///< Keyword "fastcc".
  kw_coldcc, ///< Keyword "coldcc".
  kw_intel_ocl_bicc, ///< Keyword "intel_ocl_bicc".
  kw_cfguard_checkcc, ///< Keyword "cfguard_checkcc".
  kw_x86_stdcallcc, ///< Keyword "x86_stdcallcc".
  kw_x86_fastcallcc, ///< Keyword "x86_fastcallcc".
  kw_x86_thiscallcc, ///< Keyword "x86_thiscallcc".
  kw_x86_vectorcallcc, ///< Keyword "x86_vectorcallcc".
  kw_x86_regcallcc, ///< Keyword "x86_regcallcc".
  kw_arm_apcscc, ///< Keyword "arm_apcscc".
  kw_arm_aapcscc, ///< Keyword "arm_aapcscc".
  kw_arm_aapcs_vfpcc, ///< Keyword "arm_aapcs_vfpcc".
  kw_aarch64_vector_pcs, ///< Keyword "aarch64_vector_pcs".
  kw_aarch64_sve_vector_pcs, ///< Keyword "aarch64_sve_vector_pcs".
  kw_aarch64_sme_preservemost_from_x0, ///< Keyword "aarch64_sme_preservemost_from_x0".
  kw_aarch64_sme_preservemost_from_x1, ///< Keyword "aarch64_sme_preservemost_from_x1".
  kw_aarch64_sme_preservemost_from_x2, ///< Keyword "aarch64_sme_preservemost_from_x2".
  kw_msp430_intrcc, ///< Keyword "msp430_intrcc".
  kw_avr_intrcc, ///< Keyword "avr_intrcc".
  kw_avr_signalcc, ///< Keyword "avr_signalcc".
  kw_ptx_kernel, ///< Keyword "ptx_kernel".
  kw_ptx_device, ///< Keyword "ptx_device".
  kw_spir_kernel, ///< Keyword "spir_kernel".
  kw_spir_func, ///< Keyword "spir_func".
  kw_x86_64_sysvcc, ///< Keyword "x86_64_sysvcc".
  kw_win64cc, ///< Keyword "win64cc".
  kw_anyregcc, ///< Keyword "anyregcc".
  kw_swiftcc, ///< Keyword "swiftcc".
  kw_swifttailcc, ///< Keyword "swifttailcc".
  kw_preserve_mostcc, ///< Keyword "preserve_mostcc".
  kw_preserve_allcc, ///< Keyword "preserve_allcc".
  kw_preserve_nonecc, ///< Keyword "preserve_nonecc".
  kw_ghccc, ///< Keyword "ghccc".
  kw_x86_intrcc, ///< Keyword "x86_intrcc".
  kw_hhvmcc, ///< Keyword "hhvmcc".
  kw_hhvm_ccc, ///< Keyword "hhvm_ccc".
  kw_cxx_fast_tlscc, ///< Keyword "cxx_fast_tlscc".
  kw_amdgpu_vs, ///< Keyword "amdgpu_vs".
  kw_amdgpu_ls, ///< Keyword "amdgpu_ls".
  kw_amdgpu_hs, ///< Keyword "amdgpu_hs".
  kw_amdgpu_es, ///< Keyword "amdgpu_es".
  kw_amdgpu_gs, ///< Keyword "amdgpu_gs".
  kw_amdgpu_ps, ///< Keyword "amdgpu_ps".
  kw_amdgpu_cs, ///< Keyword "amdgpu_cs".
  kw_amdgpu_cs_chain, ///< Keyword "amdgpu_cs_chain".
  kw_amdgpu_cs_chain_preserve, ///< Keyword "amdgpu_cs_chain_preserve".
  kw_amdgpu_kernel, ///< Keyword "amdgpu_kernel".
  kw_amdgpu_gfx, ///< Keyword "amdgpu_gfx".
  kw_amdgpu_gfx_whole_wave, ///< Keyword "amdgpu_gfx_whole_wave".
  kw_tailcc, ///< Keyword "tailcc".
  kw_m68k_rtdcc, ///< Keyword "m68k_rtdcc".
  kw_graalcc, ///< Keyword "graalcc".
  kw_riscv_vector_cc, ///< Keyword "riscv_vector_cc".
  kw_riscv_vls_cc, ///< Keyword "riscv_vls_cc".
  kw_cheriot_compartmentcallcc, ///< Keyword "cheriot_compartmentcallcc".
  kw_cheriot_compartmentcalleecc, ///< Keyword "cheriot_compartmentcalleecc".
  kw_cheriot_librarycallcc, ///< Keyword "cheriot_librarycallcc".

  // Attributes:
  kw_attributes, ///< Keyword "attributes".
  kw_sync, ///< Keyword "sync".
  kw_async, ///< Keyword "async".
#define GET_ATTR_NAMES
#define ATTRIBUTE_ENUM(ENUM_NAME, DISPLAY_NAME) \
  kw_##DISPLAY_NAME,
#include "llvm/IR/Attributes.inc"

  // Memory attribute:
  kw_read, ///< Keyword "read".
  kw_write, ///< Keyword "write".
  kw_readwrite, ///< Keyword "readwrite".
  kw_argmem, ///< Keyword "argmem".
  kw_inaccessiblemem, ///< Keyword "inaccessiblemem".
  kw_target_mem, ///< Keyword "target_mem".
  kw_target_mem0, ///< Keyword "target_mem0".
  kw_target_mem1, ///< Keyword "target_mem1".
  kw_errnomem, ///< Keyword "errnomem".

  // Legacy attributes:
  kw_argmemonly, ///< Keyword "argmemonly".
  kw_inaccessiblememonly, ///< Keyword "inaccessiblememonly".
  kw_inaccessiblemem_or_argmemonly, ///< Keyword "inaccessiblemem_or_argmemonly".
  kw_nocapture, ///< Keyword "nocapture".

  // Captures attribute:
  kw_address, ///< Keyword "address".
  kw_address_is_null, ///< Keyword "address_is_null".
  kw_provenance, ///< Keyword "provenance".
  kw_read_provenance, ///< Keyword "read_provenance".

  // denormal_fpenv attribute:
  kw_ieee, ///< Keyword "ieee".
  kw_preservesign, ///< Keyword "preservesign".
  kw_positivezero, ///< Keyword "positivezero".
  kw_dynamic, ///< Keyword "dynamic".

  // nofpclass attribute:
  kw_all, ///< Keyword "all".
  kw_nan, ///< Keyword "nan".
  kw_snan, ///< Keyword "snan".
  kw_qnan, ///< Keyword "qnan".
  kw_inf, ///< Keyword "inf".
  // kw_ninf, - already an fmf
  kw_pinf, ///< Keyword "pinf".
  kw_norm, ///< Keyword "norm".
  kw_nnorm, ///< Keyword "nnorm".
  kw_pnorm, ///< Keyword "pnorm".
  // kw_sub,  - already an instruction
  kw_nsub, ///< Keyword "nsub".
  kw_psub, ///< Keyword "psub".
  kw_zero, ///< Keyword "zero".
  kw_nzero, ///< Keyword "nzero".
  kw_pzero, ///< Keyword "pzero".

  kw_type, ///< Keyword "type".
  kw_opaque, ///< Keyword "opaque".

  kw_comdat, ///< Keyword "comdat".

  // Comdat types
  kw_any, ///< Keyword "any".
  kw_exactmatch, ///< Keyword "exactmatch".
  kw_largest, ///< Keyword "largest".
  kw_nodeduplicate, ///< Keyword "nodeduplicate".
  kw_samesize, ///< Keyword "samesize".

  kw_eq, ///< Keyword "eq".
  kw_ne, ///< Keyword "ne".
  kw_slt, ///< Keyword "slt".
  kw_sgt, ///< Keyword "sgt".
  kw_sle, ///< Keyword "sle".
  kw_sge, ///< Keyword "sge".
  kw_ult, ///< Keyword "ult".
  kw_ugt, ///< Keyword "ugt".
  kw_ule, ///< Keyword "ule".
  kw_uge, ///< Keyword "uge".
  kw_oeq, ///< Keyword "oeq".
  kw_one, ///< Keyword "one".
  kw_olt, ///< Keyword "olt".
  kw_ogt, ///< Keyword "ogt".
  kw_ole, ///< Keyword "ole".
  kw_oge, ///< Keyword "oge".
  kw_ord, ///< Keyword "ord".
  kw_uno, ///< Keyword "uno".
  kw_ueq, ///< Keyword "ueq".
  kw_une, ///< Keyword "une".

  // atomicrmw operations that aren't also instruction keywords.
  kw_xchg, ///< Keyword "xchg".
  kw_nand, ///< Keyword "nand".
  kw_max, ///< Keyword "max".
  kw_min, ///< Keyword "min".
  kw_umax, ///< Keyword "umax".
  kw_umin, ///< Keyword "umin".
  kw_fmax, ///< Keyword "fmax".
  kw_fmin, ///< Keyword "fmin".
  kw_fmaximum, ///< Keyword "fmaximum".
  kw_fminimum, ///< Keyword "fminimum".
  kw_fmaximumnum, ///< Keyword "fmaximumnum".
  kw_fminimumnum, ///< Keyword "fminimumnum".
  kw_uinc_wrap, ///< Keyword "uinc_wrap".
  kw_udec_wrap, ///< Keyword "udec_wrap".
  kw_usub_cond, ///< Keyword "usub_cond".
  kw_usub_sat, ///< Keyword "usub_sat".

  // Instruction Opcodes (Opcode in UIntVal).
  kw_fneg, ///< Keyword "fneg".
  kw_add, ///< Keyword "add".
  kw_fadd, ///< Keyword "fadd".
  kw_sub, ///< Keyword "sub".
  kw_fsub, ///< Keyword "fsub".
  kw_mul, ///< Keyword "mul".
  kw_fmul, ///< Keyword "fmul".
  kw_udiv, ///< Keyword "udiv".
  kw_sdiv, ///< Keyword "sdiv".
  kw_fdiv, ///< Keyword "fdiv".
  kw_urem, ///< Keyword "urem".
  kw_srem, ///< Keyword "srem".
  kw_frem, ///< Keyword "frem".
  kw_shl, ///< Keyword "shl".
  kw_lshr, ///< Keyword "lshr".
  kw_ashr, ///< Keyword "ashr".
  kw_and, ///< Keyword "and".
  kw_or, ///< Keyword "or".
  kw_xor, ///< Keyword "xor".
  kw_icmp, ///< Keyword "icmp".
  kw_fcmp, ///< Keyword "fcmp".

  kw_phi, ///< Keyword "phi".
  kw_call, ///< Keyword "call".
  kw_trunc, ///< Keyword "trunc".
  kw_zext, ///< Keyword "zext".
  kw_sext, ///< Keyword "sext".
  kw_fptrunc, ///< Keyword "fptrunc".
  kw_fpext, ///< Keyword "fpext".
  kw_uitofp, ///< Keyword "uitofp".
  kw_sitofp, ///< Keyword "sitofp".
  kw_fptoui, ///< Keyword "fptoui".
  kw_fptosi, ///< Keyword "fptosi".
  kw_inttoptr, ///< Keyword "inttoptr".
  kw_ptrtoaddr, ///< Keyword "ptrtoaddr".
  kw_ptrtoint, ///< Keyword "ptrtoint".
  kw_bitcast, ///< Keyword "bitcast".
  kw_addrspacecast, ///< Keyword "addrspacecast".
  kw_select, ///< Keyword "select".
  kw_va_arg, ///< Keyword "va_arg".

  kw_landingpad, ///< Keyword "landingpad".
  kw_personality, ///< Keyword "personality".
  kw_cleanup, ///< Keyword "cleanup".
  kw_catch, ///< Keyword "catch".
  kw_filter, ///< Keyword "filter".

  kw_ret, ///< Keyword "ret".
  kw_br, ///< Keyword "br".
  kw_switch, ///< Keyword "switch".
  kw_indirectbr, ///< Keyword "indirectbr".
  kw_invoke, ///< Keyword "invoke".
  kw_resume, ///< Keyword "resume".
  kw_unreachable, ///< Keyword "unreachable".
  kw_cleanupret, ///< Keyword "cleanupret".
  kw_catchswitch, ///< Keyword "catchswitch".
  kw_catchret, ///< Keyword "catchret".
  kw_catchpad, ///< Keyword "catchpad".
  kw_cleanuppad, ///< Keyword "cleanuppad".
  kw_callbr, ///< Keyword "callbr".

  kw_alloca, ///< Keyword "alloca".
  kw_load, ///< Keyword "load".
  kw_store, ///< Keyword "store".
  kw_fence, ///< Keyword "fence".
  kw_cmpxchg, ///< Keyword "cmpxchg".
  kw_atomicrmw, ///< Keyword "atomicrmw".
  kw_getelementptr, ///< Keyword "getelementptr".

  kw_extractelement, ///< Keyword "extractelement".
  kw_insertelement, ///< Keyword "insertelement".
  kw_shufflevector, ///< Keyword "shufflevector".
  kw_splat, ///< Keyword "splat".
  kw_extractvalue, ///< Keyword "extractvalue".
  kw_insertvalue, ///< Keyword "insertvalue".
  kw_blockaddress, ///< Keyword "blockaddress".
  kw_dso_local_equivalent, ///< Keyword "dso_local_equivalent".
  kw_no_cfi, ///< Keyword "no_cfi".
  kw_ptrauth, ///< Keyword "ptrauth".

  kw_freeze, ///< Keyword "freeze".

  // Metadata types.
  kw_distinct, ///< Keyword "distinct".

  // Use-list order directives.
  kw_uselistorder, ///< Keyword "uselistorder".

  // Summary index keywords
  kw_path, ///< Keyword "path".
  kw_hash, ///< Keyword "hash".
  kw_gv, ///< Keyword "gv".
  kw_guid, ///< Keyword "guid".
  kw_name, ///< Keyword "name".
  kw_summaries, ///< Keyword "summaries".
  kw_flags, ///< Keyword "flags".
  kw_blockcount, ///< Keyword "blockcount".
  kw_linkage, ///< Keyword "linkage".
  kw_visibility, ///< Keyword "visibility".
  kw_notEligibleToImport, ///< Keyword "notEligibleToImport".
  kw_live, ///< Keyword "live".
  kw_dsoLocal, ///< Keyword "dsoLocal".
  kw_canAutoHide, ///< Keyword "canAutoHide".
  kw_importType, ///< Keyword "importType".
  kw_definition, ///< Keyword "definition".
  kw_declaration, ///< Keyword "declaration".
  kw_noRenameOnPromotion, ///< Keyword "noRenameOnPromotion".
  kw_function, ///< Keyword "function".
  kw_insts, ///< Keyword "insts".
  kw_funcFlags, ///< Keyword "funcFlags".
  kw_readNone, ///< Keyword "readNone".
  kw_readOnly, ///< Keyword "readOnly".
  kw_noRecurse, ///< Keyword "noRecurse".
  kw_returnDoesNotAlias, ///< Keyword "returnDoesNotAlias".
  kw_noInline, ///< Keyword "noInline".
  kw_alwaysInline, ///< Keyword "alwaysInline".
  kw_noUnwind, ///< Keyword "noUnwind".
  kw_mayThrow, ///< Keyword "mayThrow".
  kw_hasUnknownCall, ///< Keyword "hasUnknownCall".
  kw_mustBeUnreachable, ///< Keyword "mustBeUnreachable".
  kw_calls, ///< Keyword "calls".
  kw_callee, ///< Keyword "callee".
  kw_params, ///< Keyword "params".
  kw_param, ///< Keyword "param".
  kw_hotness, ///< Keyword "hotness".
  kw_unknown, ///< Keyword "unknown".
  kw_critical, ///< Keyword "critical".
  kw_relbf, ///< Keyword "relbf".
  kw_variable, ///< Keyword "variable".
  kw_vTableFuncs, ///< Keyword "vTableFuncs".
  kw_virtFunc, ///< Keyword "virtFunc".
  kw_aliasee, ///< Keyword "aliasee".
  kw_refs, ///< Keyword "refs".
  kw_typeIdInfo, ///< Keyword "typeIdInfo".
  kw_typeTests, ///< Keyword "typeTests".
  kw_typeTestAssumeVCalls, ///< Keyword "typeTestAssumeVCalls".
  kw_typeCheckedLoadVCalls, ///< Keyword "typeCheckedLoadVCalls".
  kw_typeTestAssumeConstVCalls, ///< Keyword "typeTestAssumeConstVCalls".
  kw_typeCheckedLoadConstVCalls, ///< Keyword "typeCheckedLoadConstVCalls".
  kw_vFuncId, ///< Keyword "vFuncId".
  kw_offset, ///< Keyword "offset".
  kw_args, ///< Keyword "args".
  kw_typeid, ///< Keyword "typeid".
  kw_typeidCompatibleVTable, ///< Keyword "typeidCompatibleVTable".
  kw_summary, ///< Keyword "summary".
  kw_typeTestRes, ///< Keyword "typeTestRes".
  kw_kind, ///< Keyword "kind".
  kw_unsat, ///< Keyword "unsat".
  kw_byteArray, ///< Keyword "byteArray".
  kw_inline, ///< Keyword "inline".
  kw_single, ///< Keyword "single".
  kw_allOnes, ///< Keyword "allOnes".
  kw_sizeM1BitWidth, ///< Keyword "sizeM1BitWidth".
  kw_alignLog2, ///< Keyword "alignLog2".
  kw_sizeM1, ///< Keyword "sizeM1".
  kw_bitMask, ///< Keyword "bitMask".
  kw_inlineBits, ///< Keyword "inlineBits".
  kw_vcall_visibility, ///< Keyword "vcall_visibility".
  kw_wpdResolutions, ///< Keyword "wpdResolutions".
  kw_wpdRes, ///< Keyword "wpdRes".
  kw_indir, ///< Keyword "indir".
  kw_singleImpl, ///< Keyword "singleImpl".
  kw_branchFunnel, ///< Keyword "branchFunnel".
  kw_singleImplName, ///< Keyword "singleImplName".
  kw_resByArg, ///< Keyword "resByArg".
  kw_byArg, ///< Keyword "byArg".
  kw_uniformRetVal, ///< Keyword "uniformRetVal".
  kw_uniqueRetVal, ///< Keyword "uniqueRetVal".
  kw_virtualConstProp, ///< Keyword "virtualConstProp".
  kw_info, ///< Keyword "info".
  kw_byte, ///< Keyword "byte".
  kw_bit, ///< Keyword "bit".
  kw_varFlags, ///< Keyword "varFlags".
  // The following are used by MemProf summary info.
  kw_callsites, ///< Keyword "callsites".
  kw_clones, ///< Keyword "clones".
  kw_stackIds, ///< Keyword "stackIds".
  kw_allocs, ///< Keyword "allocs".
  kw_versions, ///< Keyword "versions".
  kw_memProf, ///< Keyword "memProf".
  kw_notcold, ///< Keyword "notcold".

  /// Keyword for address-sanitizer ignore / no_sanitize("address").
  ///
  /// Marks GVs with __attribute__((no_sanitize("address"))), or things in
  /// -fsanitize-ignorelist when built with ASan.
  kw_no_sanitize_address,
  /// Keyword for HWASan ignore / no_sanitize("hwaddress").
  ///
  /// Marks GVs with __attribute__((no_sanitize("hwaddress"))), or things in
  /// -fsanitize-ignorelist when built with HWASan.
  kw_no_sanitize_hwaddress,
  /// Keyword noting ASan dynamic initialization for ODR detection.
  ///
  /// Marks GVs where the clang++ frontend (when ASan is used) notes that this
  /// is dynamically initialized, and thus needs ODR detection.
  kw_sanitize_address_dyninit,

  // Unsigned Valued tokens (UIntVal).
  LabelID, ///< Numeric label identifier (e.g. "42:").
  GlobalID, ///< Numeric global identifier (e.g. "@42").
  LocalVarID, ///< Numeric local identifier (e.g. "%42").
  AttrGrpID, ///< Numeric attribute-group identifier (e.g. "#42").
  SummaryID, ///< Numeric summary identifier (e.g. "^42").

  // String valued tokens (StrVal).
  LabelStr, ///< Named label (e.g. "foo:").
  GlobalVar, ///< Named global (e.g. "@foo" or @"foo").
  ComdatVar, ///< Named comdat (e.g. "$foo").
  LocalVar, ///< Named local (e.g. "%foo" or %"foo").
  MetadataVar, ///< Named metadata (e.g. "!foo").
  StringConstant, ///< String constant (e.g. "foo").
  DwarfTag, ///< DWARF tag name (e.g. DW_TAG_foo).
  DwarfAttEncoding, ///< DWARF attribute encoding (e.g. DW_ATE_foo).
  DwarfVirtuality, ///< DWARF virtuality (e.g. DW_VIRTUALITY_foo).
  DwarfLang, ///< DWARF language (e.g. DW_LANG_foo).
  DwarfSourceLangName, ///< DWARF source language name (e.g. DW_LNAME_foo).
  DwarfLangDialect, ///< DWARF language dialect (e.g. DW_LLVM_LANG_DIALECT_foo).
  DwarfCC, ///< DWARF calling convention (e.g. DW_CC_foo).
  EmissionKind, ///< Debug emission kind (e.g. lineTablesOnly).
  NameTableKind, ///< Debug name-table kind (e.g. GNU).
  FixedPointKind, ///< Fixed-point kind token.
  DwarfOp, ///< DWARF operation (e.g. DW_OP_foo).
  DIFlag, ///< Debug info flag (e.g. DIFlagFoo).
  DISPFlag, ///< Debug info SP flag (e.g. DISPFlagFoo).
  DwarfMacinfo, ///< DWARF macinfo (e.g. DW_MACINFO_foo).
  ChecksumKind, ///< Checksum kind (e.g. CSK_foo).
  DbgRecordType, ///< Debug record type (e.g. dbg_foo).
  DwarfEnumKind, ///< DWARF enum kind (e.g. DW_APPLE_ENUM_KIND_foo).
  FloatLiteral, ///< Unparsed float literal.

  // Type valued tokens (TyVal).
  Type, ///< Type-valued token (TyVal).

  FloatHexLiteral, ///< Hex float literal (f0x...), stored as APSInt.
  APFloat, ///< Parsed APFloat value.
  APSInt ///< Parsed APSInt value.
};
} // end namespace lltok
} // end namespace llvm

#endif
