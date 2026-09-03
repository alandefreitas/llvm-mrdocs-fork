//===-- CodeGen/RuntimeLibcallUtil.h - Runtime Library Calls ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines some helper functions for runtime library calls.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_RUNTIMELIBCALLS_H
#define LLVM_CODEGEN_RUNTIMELIBCALLS_H

#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/RuntimeLibcalls.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
/// Helpers that map operations and types to \c RTLIB::Libcall values.
namespace RTLIB {

/// Return the SHL_* value for the given type, or UNKNOWN_LIBCALL if there is
/// none.
///
/// \param VT Value type used to select the SHL_* libcall.
/// \return The SHL_* libcall for \p VT, or UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getSHL(EVT VT);

/// Return the SRL_* value for the given type, or UNKNOWN_LIBCALL if there is
/// none.
///
/// \param VT Value type used to select the SRL_* libcall.
/// \return The SRL_* libcall for \p VT, or UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getSRL(EVT VT);

/// Return the SRA_* value for the given type, or UNKNOWN_LIBCALL if there is
/// none.
///
/// \param VT Value type used to select the SRA_* libcall.
/// \return The SRA_* libcall for \p VT, or UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getSRA(EVT VT);

/// Return the MUL_* value for the given type, or UNKNOWN_LIBCALL if there is
/// none.
///
/// \param VT Value type used to select the MUL_* libcall.
/// \return The MUL_* libcall for \p VT, or UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getMUL(EVT VT);

/// Return the MULO_* value for the given type, or UNKNOWN_LIBCALL if there is
/// none.
///
/// \param VT Value type used to select the MULO_* libcall.
/// \return The MULO_* libcall for \p VT, or UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getMULO(EVT VT);

/// Return the SDIV_* value for the given type, or UNKNOWN_LIBCALL if there is
/// none.
///
/// \param VT Value type used to select the SDIV_* libcall.
/// \return The SDIV_* libcall for \p VT, or UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getSDIV(EVT VT);

/// Return the UDIV_* value for the given type, or UNKNOWN_LIBCALL if there is
/// none.
///
/// \param VT Value type used to select the UDIV_* libcall.
/// \return The UDIV_* libcall for \p VT, or UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getUDIV(EVT VT);

/// Return the SREM_* value for the given type, or UNKNOWN_LIBCALL if there is
/// none.
///
/// \param VT Value type used to select the SREM_* libcall.
/// \return The SREM_* libcall for \p VT, or UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getSREM(EVT VT);

/// Return the UREM_* value for the given type, or UNKNOWN_LIBCALL if there is
/// none.
///
/// \param VT Value type used to select the UREM_* libcall.
/// \return The UREM_* libcall for \p VT, or UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getUREM(EVT VT);

/// Return the CTPOP_* value for the given type, or UNKNOWN_LIBCALL if there
/// is none.
///
/// \param VT Value type used to select the CTPOP_* libcall.
/// \return The CTPOP_* libcall for \p VT, or UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getCTPOP(EVT VT);

/// Return the right libcall for the given floating-point type, or
/// UNKNOWN_LIBCALL if there is none.
///
/// \param VT Floating-point value type used to select among the Call_* args.
/// \param Call_F32 Libcall to return when \p VT is f32.
/// \param Call_F64 Libcall to return when \p VT is f64.
/// \param Call_F80 Libcall to return when \p VT is f80.
/// \param Call_F128 Libcall to return when \p VT is f128.
/// \param Call_PPCF128 Libcall to return when \p VT is ppcf128.
/// \return The selected floating-point libcall for \p VT, or UNKNOWN_LIBCALL
///         if none exists.
LLVM_ABI Libcall getFPLibCall(EVT VT, Libcall Call_F32, Libcall Call_F64,
                              Libcall Call_F80, Libcall Call_F128,
                              Libcall Call_PPCF128);

/// Return the FPEXT_*_* value for the given types, or UNKNOWN_LIBCALL if there
/// is none.
///
/// \param OpVT Source floating-point value type.
/// \param RetVT Destination floating-point value type.
/// \return The FPEXT_*_* libcall for \p OpVT and \p RetVT, or UNKNOWN_LIBCALL
///         if none exists.
LLVM_ABI Libcall getFPEXT(EVT OpVT, EVT RetVT);

/// Return the FPROUND_*_* value for the given types, or UNKNOWN_LIBCALL if
/// there is none.
///
/// \param OpVT Source floating-point value type.
/// \param RetVT Destination floating-point value type.
/// \return The FPROUND_*_* libcall for \p OpVT and \p RetVT, or
///         UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getFPROUND(EVT OpVT, EVT RetVT);

/// Return the FPTOSINT_*_* value for the given types, or UNKNOWN_LIBCALL if
/// there is none.
///
/// \param OpVT Source floating-point value type.
/// \param RetVT Destination signed-integer value type.
/// \return The FPTOSINT_*_* libcall for \p OpVT and \p RetVT, or
///         UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getFPTOSINT(EVT OpVT, EVT RetVT);

/// Return the FPTOUINT_*_* value for the given types, or UNKNOWN_LIBCALL if
/// there is none.
///
/// \param OpVT Source floating-point value type.
/// \param RetVT Destination unsigned-integer value type.
/// \return The FPTOUINT_*_* libcall for \p OpVT and \p RetVT, or
///         UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getFPTOUINT(EVT OpVT, EVT RetVT);

/// Return the SINTTOFP_*_* value for the given types, or UNKNOWN_LIBCALL if
/// there is none.
///
/// \param OpVT Source signed-integer value type.
/// \param RetVT Destination floating-point value type.
/// \return The SINTTOFP_*_* libcall for \p OpVT and \p RetVT, or
///         UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getSINTTOFP(EVT OpVT, EVT RetVT);

/// Return the UINTTOFP_*_* value for the given types, or UNKNOWN_LIBCALL if
/// there is none.
///
/// \param OpVT Source unsigned-integer value type.
/// \param RetVT Destination floating-point value type.
/// \return The UINTTOFP_*_* libcall for \p OpVT and \p RetVT, or
///         UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getUINTTOFP(EVT OpVT, EVT RetVT);

/// Declare floating-point libcall selector overloads for each family.
///
/// The floating-point selectors get<OP>(EVT) return the <OP>_* libcall for the
/// given type, or UNKNOWN_LIBCALL. Generated from the RuntimeLibcallFamily
/// table in RuntimeLibcalls.td.
#define GET_RUNTIME_LIBCALL_FP_SELECTOR_DECLS
#include "llvm/IR/RuntimeLibcalls.inc"

/// Return the SYNC_FETCH_AND_* value for the given opcode and type, or
/// UNKNOWN_LIBCALL if there is none.
///
/// \param Opc Atomic sync ISD opcode to map to a SYNC_* libcall.
/// \param VT Memory value type selecting the SYNC_* width.
/// \return The SYNC_FETCH_AND_* libcall for \p Opc and \p VT, or
///         UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getSYNC(unsigned Opc, MVT VT);

/// Return the outline atomics value for the given atomic ordering, access
/// size and set of libcalls for a given atomic, or UNKNOWN_LIBCALL if there
/// is none.
///
/// \param LC Table of outline-atomic libcalls indexed by size and ordering.
/// \param Order Atomic memory ordering used to select a column of \p LC.
/// \param MemSize Access size in bytes used to select a row of \p LC.
/// \return The outline-atomic libcall for \p Order and \p MemSize, or
///         UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getOutlineAtomicHelper(const Libcall (&LC)[5][4],
                                        AtomicOrdering Order, uint64_t MemSize);

/// Return the outline atomics value for the given opcode, atomic ordering
/// and type, or UNKNOWN_LIBCALL if there is none.
///
/// \param Opc Atomic ISD opcode selecting which outline-atomic family to use.
/// \param Order Atomic memory ordering for the outline-atomic libcall.
/// \param VT Memory value type selecting the outline-atomic width.
/// \return The outline-atomic libcall for \p Opc, \p Order, and \p VT, or
///         UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getOUTLINE_ATOMIC(unsigned Opc, AtomicOrdering Order, MVT VT);

/// Return the MEMCPY_ELEMENT_UNORDERED_ATOMIC_* value for the given element
/// size, or UNKNOWN_LIBCALL if there is none.
///
/// \param ElementSize Element size in bytes for the unordered-atomic memcpy.
/// \return The MEMCPY_ELEMENT_UNORDERED_ATOMIC_* libcall for \p ElementSize,
///         or UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getMEMCPY_ELEMENT_UNORDERED_ATOMIC(uint64_t ElementSize);

/// Return the MEMMOVE_ELEMENT_UNORDERED_ATOMIC_* value for the given element
/// size, or UNKNOWN_LIBCALL if there is none.
///
/// \param ElementSize Element size in bytes for the unordered-atomic memmove.
/// \return The MEMMOVE_ELEMENT_UNORDERED_ATOMIC_* libcall for \p ElementSize,
///         or UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getMEMMOVE_ELEMENT_UNORDERED_ATOMIC(uint64_t ElementSize);

/// Return the MEMSET_ELEMENT_UNORDERED_ATOMIC_* value for the given element
/// size, or UNKNOWN_LIBCALL if there is none.
///
/// \param ElementSize Element size in bytes for the unordered-atomic memset.
/// \return The MEMSET_ELEMENT_UNORDERED_ATOMIC_* libcall for \p ElementSize,
///         or UNKNOWN_LIBCALL if none exists.
LLVM_ABI Libcall getMEMSET_ELEMENT_UNORDERED_ATOMIC(uint64_t ElementSize);

} // namespace RTLIB
} // namespace llvm

#endif
