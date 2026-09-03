//===------ PGOOptions.h -- PGO option tunables ----------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Define option tunables for PGO.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PGOOPTIONS_H
#define LLVM_SUPPORT_PGOOPTIONS_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
/// A struct capturing PGO tunables.
struct PGOOptions {
  /// Profile-guided optimization action to run.
  enum PGOAction {
    NoAction,  ///< Do not perform IR or sample PGO.
    IRInstr,   ///< Instrument IR to generate a profile.
    IRUse,     ///< Use an IR instrumentation profile.
    SampleUse, ///< Use a sample-based profile.
  };
  /// Context-sensitive PGO action to run.
  enum CSPGOAction {
    NoCSAction, ///< Do not perform context-sensitive PGO.
    CSIRInstr,  ///< Instrument IR for a context-sensitive profile.
    CSIRUse,    ///< Use a context-sensitive IR profile.
  };
  /// Attribute applied to cold functions under PGO.
  enum class ColdFuncOpt {
    Default, ///< Leave cold functions unchanged.
    OptSize, ///< Mark cold functions with \c optsize.
    MinSize, ///< Mark cold functions with \c minsize.
    OptNone, ///< Mark cold functions with \c optnone.
  };
  /// Construct PGO tunables for the given profile paths and actions.
  ///
  /// \param ProfileFile Path to the profile file to read or write.
  /// \param CSProfileGenFile Path for context-sensitive profile generation.
  /// \param ProfileRemappingFile Path to the profile remapping file, if any.
  /// \param MemoryProfile Path to the memory profile file, if any.
  /// \param Action Non-CS PGO action to perform.
  /// \param CSAction Context-sensitive PGO action to perform.
  /// \param ColdType Attribute to apply to cold functions.
  /// \param DebugInfoForProfiling Whether to emit debug info for profiling.
  /// \param PseudoProbeForProfiling Whether to emit pseudo probes for
  ///        profiling.
  /// \param AtomicCounterUpdate Whether to update profile counters atomically.
  LLVM_ABI PGOOptions(std::string ProfileFile, std::string CSProfileGenFile,
                      std::string ProfileRemappingFile,
                      std::string MemoryProfile, PGOAction Action = NoAction,
                      CSPGOAction CSAction = NoCSAction,
                      ColdFuncOpt ColdType = ColdFuncOpt::Default,
                      bool DebugInfoForProfiling = false,
                      bool PseudoProbeForProfiling = false,
                      bool AtomicCounterUpdate = false);
  /// Copy-construct PGO options from another instance.
  ///
  /// \param Other The PGO options to copy.
  LLVM_ABI PGOOptions(const PGOOptions &Other);
  /// Destroy these PGO options.
  LLVM_ABI ~PGOOptions();
  /// Copy-assign PGO options from another instance.
  ///
  /// \param Other The PGO options to assign from.
  /// \return A reference to this PGO options instance.
  LLVM_ABI PGOOptions &operator=(const PGOOptions &Other);

  /// Path to the profile file to read or write.
  std::string ProfileFile;
  /// Path used when generating a context-sensitive profile.
  std::string CSProfileGenFile;
  /// Path to the profile remapping file, if any.
  std::string ProfileRemappingFile;
  /// Path to the memory profile file, if any.
  std::string MemoryProfile;
  /// Non-CS PGO action selected for this configuration.
  PGOAction Action;
  /// Context-sensitive PGO action selected for this configuration.
  CSPGOAction CSAction;
  /// Attribute applied to cold functions under this configuration.
  ColdFuncOpt ColdOptType;
  /// Whether to emit special debug info to enable PGO profile generation.
  bool DebugInfoForProfiling;
  /// Whether to emit pseudo probes to enable PGO profile generation.
  bool PseudoProbeForProfiling;
  /// Whether profile counters should be updated atomically.
  bool AtomicCounterUpdate;
};
} // namespace llvm

#endif
