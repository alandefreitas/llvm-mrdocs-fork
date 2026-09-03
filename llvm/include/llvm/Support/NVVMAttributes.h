//===--- NVVMAttributes.h - NVVM IR attribute names -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Canonical string constants for NVVM function and parameter attributes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_NVVMATTRIBUTES_H
#define LLVM_SUPPORT_NVVMATTRIBUTES_H

#include "llvm/ADT/StringRef.h"

namespace llvm {
/// Canonical string constants for NVVM function and parameter attributes.
namespace NVVMAttr {

/// Function attribute name for the maximum number of threads per CTA.
constexpr StringLiteral MaxNTID("nvvm.maxntid");
/// Function attribute name for the exact number of threads per CTA.
constexpr StringLiteral ReqNTID("nvvm.reqntid");
/// Function attribute name for the number of CTAs in a cluster.
constexpr StringLiteral ClusterDim("nvvm.cluster_dim");
/// Function attribute name for the maximum number of blocks per cluster.
constexpr StringLiteral MaxClusterRank("nvvm.maxclusterrank");
/// Function attribute name for the minimum number of CTAs per SM.
constexpr StringLiteral MinCTASm("nvvm.minctasm");
/// Function attribute name for the maximum number of registers per kernel.
constexpr StringLiteral MaxNReg("nvvm.maxnreg");
/// Function attribute name indicating launch config counts clusters, not blocks.
constexpr StringLiteral BlocksAreClusters("nvvm.blocksareclusters");
/// Parameter attribute name for grid-constant \c byval kernel arguments.
constexpr StringLiteral GridConstant("nvvm.grid_constant");

} // namespace NVVMAttr
} // namespace llvm

#endif // LLVM_SUPPORT_NVVMATTRIBUTES_H
