//===- BasicBlockSectionUtils.h - Utilities for basic block sections     --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_BASICBLOCKSECTIONUTILS_H
#define LLVM_CODEGEN_BASICBLOCKSECTIONUTILS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/CommandLine.h"

namespace llvm {

/// Command-line option for the text section prefix of cold basic block clusters.
extern LLVM_ABI cl::opt<std::string> BBSectionsColdTextPrefix;

class MachineFunction;
class MachineBasicBlock;

/// Comparator used to order machine basic blocks for section layout.
///
/// \param LHS Left-hand machine basic block in the comparison.
/// \param RHS Right-hand machine basic block in the comparison.
using MachineBasicBlockComparator =
    function_ref<bool(const MachineBasicBlock &LHS,
                      const MachineBasicBlock &RHS)>;

/// Sort the machine basic blocks of \p MF and update branches after the reorder.
///
/// \param MF Function whose basic blocks are sorted.
/// \param MBBCmp Comparator that defines the desired block order.
LLVM_ABI void
sortBasicBlocksAndUpdateBranches(MachineFunction &MF,
                                 MachineBasicBlockComparator MBBCmp);

/// Insert a NOP before a section-leading EH pad so its LSDA offset is nonzero.
///
/// \param MF Function whose exception landing pads are adjusted.
LLVM_ABI void avoidZeroOffsetLandingPad(MachineFunction &MF);

/// Return whether the function has an instrumented-profile hash mismatch.
///
/// This checks if the source of this function has drifted since this binary was
/// profiled previously. For now, we are piggy backing on what PGO does to
/// detect this with instrumented profiles.  PGO emits an hash of the IR and
/// checks if the hash has changed.  Advanced basic block layout is usually done
/// on top of PGO optimized binaries and hence this check works well in
/// practice.
///
/// \param MF Function whose profile hash metadata is inspected.
/// \return True if the function has an instrumented-profile hash mismatch.
LLVM_ABI bool hasInstrProfHashMismatch(MachineFunction &MF);

} // end namespace llvm

#endif // LLVM_CODEGEN_BASICBLOCKSECTIONUTILS_H
