//===--------- MemProfUse.h - Memory profiler use pass ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MemProfUsePass class and related utilities.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_MEMPROFUSE_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_MEMPROFUSE_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/IR/PassManager.h"
#include "llvm/ProfileData/DataAccessProf.h"
#include "llvm/ProfileData/MemProf.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class IndexedInstrProfReader;
class Module;
class TargetLibraryInfo;

namespace vfs {
class FileSystem;
} // namespace vfs

/// Public interface to the MemProf profile-use pass that applies memory
/// profiles to the IR.
class MemProfUsePass : public OptionalPassInfoMixin<MemProfUsePass> {
public:
  /// Construct a MemProf profile-use pass.
  /// @param MemoryProfileFile Path to the memory profile file to apply.
  /// @param FS Optional virtual file system used to read the profile file.
  LLVM_ABI explicit MemProfUsePass(
      std::string MemoryProfileFile,
      IntrusiveRefCntPtr<vfs::FileSystem> FS = nullptr);
  /// Apply the memory profile to the module.
  /// @param M Module to annotate with MemProf profile data.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  // Annotate global variables' section prefix based on data access profile,
  // return true if any global variable is annotated and false otherwise.
  bool
  annotateGlobalVariables(Module &M,
                          const memprof::DataAccessProfData *DataAccessProf);
  std::string MemoryProfileFileName;
  IntrusiveRefCntPtr<vfs::FileSystem> FS;
};

namespace memprof {

/// Extract all calls from the IR, keyed by caller GUID.
///
/// Arranges them in a map from caller GUIDs to a list of call sites, each of
/// the form {LineLocation, CalleeGUID}.
/// @param M Module whose call sites are extracted.
/// @param TLI Target library info used to recognize library calls.
/// @param IsPresentInProfile Predicate returning true if a GUID is present in
/// the profile; defaults to accepting every GUID.
/// @return Map from caller GUIDs to their extracted call edges.
LLVM_ABI DenseMap<uint64_t, SmallVector<CallEdgeTy, 0>> extractCallsFromIR(
    Module &M, const TargetLibraryInfo &TLI,
    function_ref<bool(uint64_t)> IsPresentInProfile = [](uint64_t) {
      return true;
    });

/// Map from a profile source location to the matching location in the IR.
using LocToLocMap = DenseMap<LineLocation, LineLocation>;

/// Compute an undrifting map from profile locations to IR locations.
///
/// The result is a map from caller GUIDs to an inner map that maps source
/// locations in the profile to those in the current IR.
/// @param M Module whose IR locations are matched against the profile.
/// @param MemProfReader Reader providing the indexed MemProf profile.
/// @param TLI Target library info used when extracting calls from the IR.
/// @return Map from caller GUIDs to location undrifting maps.
LLVM_ABI DenseMap<uint64_t, LocToLocMap>
computeUndriftMap(Module &M, IndexedInstrProfReader *MemProfReader,
                  const TargetLibraryInfo &TLI);

} // namespace memprof
} // namespace llvm

#endif
