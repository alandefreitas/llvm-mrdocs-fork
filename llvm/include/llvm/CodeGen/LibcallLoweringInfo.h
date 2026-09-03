//===- LibcallLoweringInfo.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Legacy-pass-manager wrapper around the Analysis-layer libcall lowering info,
// which is aware of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIBCALLLOWERINGINFO_H
#define LLVM_CODEGEN_LIBCALLLOWERINGINFO_H

#include "llvm/Analysis/LibcallLoweringInfo.h"
#include "llvm/Pass.h"

namespace llvm {
class RuntimeLibraryInfoWrapper;
class TargetSubtargetInfo;

/// Resolve the LibcallLoweringInfo for \p Subtarget from the module-level \p
/// ModuleInfo, applying the subtarget's libcall overrides.
/// @param ModuleInfo Module-level libcall lowering map to query.
/// @param Subtarget Subtarget whose libcall overrides are applied.
/// @return Libcall lowering info for \p Subtarget derived from \p ModuleInfo.
LLVM_ABI const LibcallLoweringInfo &
getLibcallLowering(const ModuleLibcallLoweringInfo &ModuleInfo,
                   const TargetSubtargetInfo &Subtarget);

/// Legacy-pass-manager wrapper around ModuleLibcallLoweringInfo.
class LLVM_ABI LibcallLoweringInfoWrapper : public ImmutablePass {
  ModuleLibcallLoweringInfo Result;
  RuntimeLibraryInfoWrapper *RuntimeLibcallsWrapper = nullptr;

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct the legacy-pass-manager wrapper around ModuleLibcallLoweringInfo.
  LibcallLoweringInfoWrapper();

  /// Return libcall lowering info for \p Subtarget in module \p M.
  /// @param M Module providing the module-level libcall defaults.
  /// @param Subtarget Subtarget whose libcall overrides are applied.
  /// @return Libcall lowering info for \p Subtarget in module \p M.
  const LibcallLoweringInfo &
  getLibcallLowering(const Module &M, const TargetSubtargetInfo &Subtarget);

  /// Return the module-level libcall lowering info for \p M, initializing it on
  /// first use.
  /// @param M Module whose libcall lowering info is requested.
  /// @return Module-level libcall lowering info for \p M.
  const ModuleLibcallLoweringInfo &getResult(const Module &M);

  /// Cache the RuntimeLibraryInfoWrapper analysis required by this pass.
  void initializePass() override;
  /// Require RuntimeLibraryInfoWrapper and declare that this pass preserves all
  /// analyses.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Clear the cached ModuleLibcallLoweringInfo.
  void releaseMemory() override;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_LIBCALLLOWERINGINFO_H
