//===-- RuntimeLibcallInfo.h - Runtime library information ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_RUNTIMELIBCALLINFO_H
#define LLVM_ANALYSIS_RUNTIMELIBCALLINFO_H

#include "llvm/IR/RuntimeLibcalls.h"
#include "llvm/Pass.h"
#include <optional>
#include <string>

namespace llvm {

/// Analysis pass providing the \c RTLIB::RuntimeLibcallsInfo for a module.
class LLVM_ABI RuntimeLibraryAnalysis
    : public AnalysisInfoMixin<RuntimeLibraryAnalysis> {
public:
  /// The analysis result type; runtime libcall availability for a module.
  using Result = RTLIB::RuntimeLibcallsInfo;

  /// Default-construct the analysis with no extra TargetOptions overrides.
  RuntimeLibraryAnalysis() = default;
  /// Construct the analysis with TargetOptions values not yet in the IR.
  /// @param ExceptionModel Exception-handling model for the target.
  /// @param EABIVersion EABI version when applicable.
  /// @param ABIName Optional ABI name string for the target.
  /// @param VecLib Vector math library whose calls should be available.
  RuntimeLibraryAnalysis(ExceptionHandling ExceptionModel,
                         EABI EABIVersion = EABI::Default,
                         StringRef ABIName = "",
                         VectorLibrary VecLib = VectorLibrary::NoLibrary)
      : ExceptionModel(ExceptionModel), EABIVersion(EABIVersion),
        ABIName(ABIName.str()), VecLib(VecLib) {}

  /// Compute runtime libcall info for module \p M.
  /// @param M Module providing the target triple and float-abi flag.
  /// @param MAM Module analysis manager (unused; no dependencies).
  /// @return Runtime libcall availability derived from \p M and stored options.
  RTLIB::RuntimeLibcallsInfo run(const Module &M, ModuleAnalysisManager &MAM);

private:
  friend AnalysisInfoMixin<RuntimeLibraryAnalysis>;
  static AnalysisKey Key;

  // FIXME: These are TargetOptions values that are not yet represented in the
  // IR, copied here so run() can forward them to the RuntimeLibcallsInfo Module
  // constructor. Delete each one as they are migrated to module flags.
  ExceptionHandling ExceptionModel = ExceptionHandling::None;
  EABI EABIVersion = EABI::Default;
  std::string ABIName;
  VectorLibrary VecLib = VectorLibrary::NoLibrary;
};

/// Legacy immutable pass wrapping \c RuntimeLibraryAnalysis for the old PM.
class LLVM_ABI RuntimeLibraryInfoWrapper : public ImmutablePass {
  RuntimeLibraryAnalysis RTLA;
  std::optional<RTLIB::RuntimeLibcallsInfo> RTLCI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct the wrapper with default TargetOptions overrides.
  RuntimeLibraryInfoWrapper();
  /// Construct the wrapper with TargetOptions values not yet in the IR.
  /// @param ExceptionModel Exception-handling model for the target.
  /// @param EABIVersion EABI version when applicable.
  /// @param ABIName Optional ABI name string for the target.
  /// @param VecLib Vector math library whose calls should be available.
  RuntimeLibraryInfoWrapper(ExceptionHandling ExceptionModel,
                            EABI EABIVersion = EABI::Default,
                            StringRef ABIName = "",
                            VectorLibrary VecLib = VectorLibrary::NoLibrary);

  /// Return runtime libcall info for \p M, computing it on first use.
  /// @param M Module whose runtime libcalls are requested.
  /// @return Cached \c RTLIB::RuntimeLibcallsInfo for \p M.
  const RTLIB::RuntimeLibcallsInfo &getRTLCI(const Module &M) {
    if (!RTLCI) {
      ModuleAnalysisManager DummyMAM;
      RTLCI = RTLA.run(M, DummyMAM);
    }

    return *RTLCI;
  }

  /// Declare that this pass preserves all analyses.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// Create a legacy pass that provides \c RTLIB::RuntimeLibcallsInfo.
/// @return A ModulePass wrapping RuntimeLibraryAnalysis for the old PM.
LLVM_ABI ModulePass *createRuntimeLibraryInfoWrapperPass();

} // namespace llvm

#endif
