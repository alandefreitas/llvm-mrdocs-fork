//=- DXILMetadataAnalysis.h - Representation of Module metadata --*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DXILMETADATA_H
#define LLVM_ANALYSIS_DXILMETADATA_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/PassManager.h"
#include "llvm/MC/DXContainerInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {

class Function;
/// DXIL module metadata types shared by analysis passes.
namespace dxil {

/// Properties attached to a DXIL shader entry function.
struct EntryProperties {
  /// Entry function these properties describe.
  const Function *Entry{nullptr};
  /// Specific target shader stage for the entry, if specified.
  Triple::EnvironmentType ShaderStage{Triple::UnknownEnvironment};
  /// X component of the numthreads attribute.
  unsigned NumThreadsX{0};
  /// Y component of the numthreads attribute.
  unsigned NumThreadsY{0};
  /// Z component of the numthreads attribute.
  unsigned NumThreadsZ{0};
  /// Minimum wave size for the entry.
  unsigned WaveSizeMin{0};
  /// Maximum wave size for the entry.
  unsigned WaveSizeMax{0};
  /// Preferred wave size for the entry.
  unsigned WaveSizePref{0};

  /// Construct entry properties for \p Fn.
  /// \param Fn Entry function, or null when unset.
  EntryProperties(const Function *Fn = nullptr) : Entry(Fn) {};
};

/// Collected DXIL module-level metadata for a module.
struct ModuleMetadataInfo {
  /// DXIL language version from module metadata.
  VersionTuple DXILVersion{};
  /// Shader model version from module metadata.
  VersionTuple ShaderModelVersion{};
  /// Shader profile (stage) from module metadata.
  Triple::EnvironmentType ShaderProfile{Triple::UnknownEnvironment};
  /// DXIL validator version from module metadata.
  VersionTuple ValidatorVersion{};
  /// Per-entry properties discovered in the module.
  SmallVector<EntryProperties> EntryPropertyVec{};
  /// Optional source-info builder from module metadata.
  std::optional<mcdxbc::SourceInfoBuilder> SourceInfo;
  /// Print the module metadata to \p OS.
  /// \param OS Output stream.
  LLVM_ABI void print(raw_ostream &OS) const;
};

} // namespace dxil

/// Analysis that gathers DXIL module metadata for the new pass manager.
class DXILMetadataAnalysis : public AnalysisInfoMixin<DXILMetadataAnalysis> {
  friend AnalysisInfoMixin<DXILMetadataAnalysis>;

  static AnalysisKey Key;

public:
  /// Result type of this analysis.
  using Result = dxil::ModuleMetadataInfo;
  /// Gather module metadata info for the module \c M.
  /// \param M Module to analyze.
  /// \param AM Module analysis manager (unused).
  /// \return Collected DXIL module metadata for \p M.
  LLVM_ABI dxil::ModuleMetadataInfo run(Module &M, ModuleAnalysisManager &AM);
};

/// Printer pass for the \c DXILMetadataAnalysis results.
class DXILMetadataAnalysisPrinterPass
    : public RequiredPassInfoMixin<DXILMetadataAnalysisPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// \param OS Stream that receives the printed metadata.
  explicit DXILMetadataAnalysisPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print the DXIL module metadata for module \p M.
  /// \param M Module whose metadata is printed.
  /// \param AM Module analysis manager providing DXILMetadataAnalysis.
  /// \return All analyses preserved.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Legacy module pass wrapping \c dxil::ModuleMetadataInfo.
class LLVM_ABI DXILMetadataAnalysisWrapperPass : public ModulePass {
  std::unique_ptr<dxil::ModuleMetadataInfo> MetadataInfo;

public:
  /// Pass identification, replacement for typeid.
  static char ID; // Class identification, replacement for typeinfo

  /// Construct the DXIL metadata analysis wrapper pass.
  DXILMetadataAnalysisWrapperPass();
  /// Destroy the DXIL metadata analysis wrapper pass.
  ~DXILMetadataAnalysisWrapperPass() override;

  /// Return the computed module metadata.
  /// \return Const reference to the collected DXIL module metadata.
  const dxil::ModuleMetadataInfo &getModuleMetadata() const {
    return *MetadataInfo;
  }
  /// Return the mutable computed module metadata.
  /// \return Mutable reference to the collected DXIL module metadata.
  dxil::ModuleMetadataInfo &getModuleMetadata() { return *MetadataInfo; }

  /// Declare required and preserved analyses for this pass.
  /// \param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Gather DXIL module metadata for module \p M.
  /// \param M Module to analyze.
  /// \return False; this analysis does not modify the module.
  bool runOnModule(Module &M) override;
  /// Release the computed module metadata.
  void releaseMemory() override;

  /// Print the module metadata for module \p M.
  /// \param OS Stream to print to.
  /// \param M Optional module (unused).
  void print(raw_ostream &OS, const Module *M) const override;
  /// Dump the module metadata to stderr.
  void dump() const;
};

} // namespace llvm

#endif // LLVM_ANALYSIS_DXILMETADATA_H
