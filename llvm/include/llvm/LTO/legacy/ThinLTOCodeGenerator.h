//===-ThinLTOCodeGenerator.h - LLVM Link Time Optimizer -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the ThinLTOCodeGenerator class, similar to the
// LTOCodeGenerator but for the ThinLTO scheme. It provides an interface for
// linker plugin.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_LEGACY_THINLTOCODEGENERATOR_H
#define LLVM_LTO_LEGACY_THINLTOCODEGENERATOR_H

#include "llvm-c/lto.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"

#include <string>

namespace llvm {
class StringRef;
class TargetMachine;

/// ThinLTOCodeGeneratorImpl - Namespace used for ThinLTOCodeGenerator
/// implementation details. It should be considered private to the
/// implementation.
namespace ThinLTOCodeGeneratorImpl {
struct TargetMachineBuilder;
}

/// Helper to gather options relevant to the target machine creation
struct ThinLTOCodeGeneratorImpl::TargetMachineBuilder {
  /// Target triple for the machine to build.
  Triple TheTriple;
  /// CPU name used to initialize the TargetMachine.
  std::string MCpu;
  /// Subtarget feature attributes.
  std::string MAttr;
  /// Target options passed to the TargetMachine.
  TargetOptions Options;
  /// Optional relocation model; nullopt selects the target default.
  std::optional<Reloc::Model> RelocModel;
  /// Code generation optimization level.
  CodeGenOptLevel CGOptLevel = CodeGenOptLevel::Aggressive;

  /// Create a TargetMachine from the configured options.
  /// \return Owned TargetMachine built from the configured options.
  LLVM_ABI std::unique_ptr<TargetMachine> create() const;
};

/// Interface similar to LTOCodeGenerator, adapted for ThinLTO processing.
///
/// The ThinLTOCodeGenerator is not intended to be reused for multiple
/// compilation: the model is that the client adds modules to the generator and
/// ask to perform the ThinLTO optimizations / codegen, and finally destroys the
/// codegenerator.
class ThinLTOCodeGenerator {
public:
  /// Add given module to the code generator.
  /// \param Identifier Module identifier used for ThinLTO bookkeeping.
  /// \param Data Bitcode buffer contents for the module.
  LLVM_ABI void addModule(StringRef Identifier, StringRef Data);

  /// Adds a global symbol that must exist in the final generated code.
  ///
  /// If a symbol is not listed there, it will be optimized away if it is
  /// inlined into every usage.
  /// \param Name Symbol name that must be preserved.
  LLVM_ABI void preserveSymbol(StringRef Name);

  /// Adds a global symbol that is cross-referenced between ThinLTO files.
  ///
  /// If the ThinLTO CodeGenerator can ensure that every references from a
  /// ThinLTO module to this symbol is optimized away, then the symbol can be
  /// discarded.
  /// \param Name Symbol name that is cross-referenced.
  LLVM_ABI void crossReferenceSymbol(StringRef Name);

  /**
   * Process all the modules that were added to the code generator in parallel.
   *
   * Client can access the resulting object files using getProducedBinaries(),
   * unless setGeneratedObjectsDirectory() has been called, in which case
   * results are available through getProducedBinaryFiles().
   */
  LLVM_ABI void run();

  /// Return the in-memory binaries produced by the code generator.
  ///
  /// This is filled after run() unless setGeneratedObjectsDirectory() has been
  /// called, in which case results are available through
  /// getProducedBinaryFiles().
  /// \return Reference to the vector of in-memory object-file buffers.
  std::vector<std::unique_ptr<MemoryBuffer>> &getProducedBinaries() {
    return ProducedBinaries;
  }

  /// Return the on-disk binaries produced by the code generator.
  ///
  /// This is filled after run() when setGeneratedObjectsDirectory() has been
  /// called.
  /// \return Reference to the vector of generated object-file paths.
  std::vector<std::string> &getProducedBinaryFiles() {
    return ProducedBinaryFiles;
  }

  /**
   * \defgroup Options setters
   * @{
   */

  /**
   * \defgroup Cache controlling options
   *
   * These entry points control the ThinLTO cache. The cache is intended to
   * support incremental build, and thus needs to be persistent accross build.
   * The client enabled the cache by supplying a path to an existing directory.
   * The code generator will use this to store objects files that may be reused
   * during a subsequent build.
   * To avoid filling the disk space, a few knobs are provided:
   *  - The pruning interval limit the frequency at which the garbage collector
   *    will try to scan the cache directory to prune it from expired entries.
   *    Setting to -1 disable the pruning (default). Setting to 0 will force
   *    pruning to occur.
   *  - The pruning expiration time indicates to the garbage collector how old
   *    an entry needs to be to be removed.
   *  - Finally, the garbage collector can be instructed to prune the cache till
   *    the occupied space goes below a threshold.
   * @{
   */

  /// Options controlling ThinLTO object-file caching.
  struct CachingOptions {
    /// Path to the cache directory; empty disables caching.
    std::string Path;
    /// Policy used when pruning the cache.
    CachePruningPolicy Policy;
  };

  /// Provide a path to a directory where to store the cached files for
  /// incremental build.
  /// \param Path Directory used to store cached object files.
  void setCacheDir(std::string Path) { CacheOptions.Path = std::move(Path); }

  /// Cache policy: interval (seconds) between two prunes of the cache. Set to a
  /// negative value to disable pruning. A value of 0 will force pruning to
  /// occur.
  /// \param Interval Seconds between cache prunes; negative disables pruning.
  void setCachePruningInterval(int Interval) {
    if(Interval < 0)
      CacheOptions.Policy.Interval.reset();
    else
      CacheOptions.Policy.Interval = std::chrono::seconds(Interval);
  }

  /// Cache policy: expiration (in seconds) for an entry.
  /// A value of 0 will be ignored.
  /// \param Expiration Entry lifetime in seconds; 0 is ignored.
  void setCacheEntryExpiration(unsigned Expiration) {
    if (Expiration)
      CacheOptions.Policy.Expiration = std::chrono::seconds(Expiration);
  }

  /// Set the maximum cache size as a percentage of available disk space.
  ///
  /// Set to 100 to indicate no limit, 50 to indicate that the cache size will
  /// not be left over half the available space. A value over 100 will be
  /// reduced to 100, and a value of 0 will be ignored.
  ///
  /// The formula looks like:
  ///  AvailableSpace = FreeSpace + ExistingCacheSize
  ///  NewCacheSize = AvailableSpace * P/100
  /// \param Percentage Maximum cache size as a percentage of available space.
  void setMaxCacheSizeRelativeToAvailableSpace(unsigned Percentage) {
    if (Percentage)
      CacheOptions.Policy.MaxSizePercentageOfAvailableSpace = Percentage;
  }

  /// Set the maximum size for the cache directory in bytes.
  ///
  /// A value over the amount of available space on the disk will be reduced to
  /// the amount of available space. A value of 0 will be ignored.
  /// \param MaxSizeBytes Maximum cache size in bytes; 0 is ignored.
  void setCacheMaxSizeBytes(uint64_t MaxSizeBytes) {
    if (MaxSizeBytes)
      CacheOptions.Policy.MaxSizeBytes = MaxSizeBytes;
  }

  /// Cache policy: the maximum number of files in the cache directory. A value
  /// of 0 will be ignored.
  /// \param MaxSizeFiles Maximum number of files in the cache; 0 is ignored.
  void setCacheMaxSizeFiles(unsigned MaxSizeFiles) {
    if (MaxSizeFiles)
      CacheOptions.Policy.MaxSizeFiles = MaxSizeFiles;
  }

  /**@}*/

  /// Set the path to a directory where to save temporaries at various stages of
  /// the processing.
  /// \param Path Directory used to store temporary bitcode files.
  void setSaveTempsDir(std::string Path) { SaveTempsDir = std::move(Path); }

  /// Set the directory where generated object files are saved.
  ///
  /// This path can be used by a linker to request on-disk files instead of
  /// in-memory buffers. When set, results are available through
  /// getProducedBinaryFiles() instead of getProducedBinaries().
  /// \param Path Directory used to store generated object files.
  void setGeneratedObjectsDirectory(std::string Path) {
    SavedObjectsDirectoryPath = std::move(Path);
  }

  /// CPU to use to initialize the TargetMachine
  /// \param Cpu Target CPU name.
  void setCpu(std::string Cpu) { TMBuilder.MCpu = std::move(Cpu); }

  /// Subtarget attributes
  /// \param MAttr Subtarget feature attribute string.
  void setAttr(std::string MAttr) { TMBuilder.MAttr = std::move(MAttr); }

  /// TargetMachine options
  /// \param Options Target options to apply.
  void setTargetOptions(TargetOptions Options) {
    TMBuilder.Options = std::move(Options);
  }

  /// Enable the Freestanding mode: indicate that the optimizer should not
  /// assume builtins are present on the target.
  /// \param Enabled Whether freestanding mode is enabled.
  void setFreestanding(bool Enabled) { Freestanding = Enabled; }

  /// CodeModel
  /// \param Model Relocation model, or nullopt for the target default.
  void setCodePICModel(std::optional<Reloc::Model> Model) {
    TMBuilder.RelocModel = Model;
  }

  /// CodeGen optimization level
  /// \param CGOptLevel Code generation optimization level.
  void setCodeGenOptLevel(CodeGenOptLevel CGOptLevel) {
    TMBuilder.CGOptLevel = CGOptLevel;
  }

  /// IR optimization level: from 0 to 3.
  /// \param NewOptLevel IR optimization level (clamped to 0-3).
  void setOptLevel(unsigned NewOptLevel) {
    OptLevel = (NewOptLevel > 3) ? 3 : NewOptLevel;
  }

  /// Enable or disable debug output for the new pass manager.
  /// \param Enabled Whether pass-manager debugging is enabled.
  void setDebugPassManager(unsigned Enabled) { DebugPassManager = Enabled; }

  /// Set the -mllvm arguments to include in the cache key.
  /// \param Args -mllvm argument strings to include in the cache key.
  void setMllvmArgs(ArrayRef<std::string> Args) {
    MllvmArgs.assign(Args.begin(), Args.end());
  }

  /// Disable CodeGen, only run the stages till codegen and stop. The output
  /// will be bitcode.
  /// \param Disable Whether code generation should be disabled.
  void disableCodeGen(bool Disable) { DisableCodeGen = Disable; }

  /// Perform CodeGen only: disable all other stages.
  /// \param CGOnly Whether only code generation should run.
  void setCodeGenOnly(bool CGOnly) { CodeGenOnly = CGOnly; }

  /**@}*/

  /**
   * \defgroup Set of APIs to run individual stages in isolation.
   * @{
   */

  /**
   * Produce the combined summary index from all the bitcode files:
   * "thin-link".
   * \return Owned combined module summary index.
   */
  LLVM_ABI std::unique_ptr<ModuleSummaryIndex> linkCombinedIndex();

  /// Promote and rename exported internal functions, and resolve weak symbols.
  ///
  /// Index is updated to reflect linkage changes from weak resolution.
  /// \param Module Module to promote.
  /// \param Index Combined module summary index, updated for linkage changes.
  /// \param File Input file corresponding to \p Module.
  LLVM_ABI void promote(Module &Module, ModuleSummaryIndex &Index,
                        const lto::InputFile &File);

  /// Compute and emit the imported files for module at \p ModulePath.
  /// \param Module Module whose import list is emitted.
  /// \param OutputName Path of the imports file to write.
  /// \param Index Combined module summary index.
  /// \param File Input file corresponding to \p Module.
  LLVM_ABI void emitImports(Module &Module, StringRef OutputName,
                            ModuleSummaryIndex &Index,
                            const lto::InputFile &File);

  /// Perform cross-module importing for the module identified by
  /// ModuleIdentifier.
  /// \param Module Module that receives imported functions.
  /// \param Index Combined module summary index.
  /// \param File Input file corresponding to \p Module.
  LLVM_ABI void crossModuleImport(Module &Module, ModuleSummaryIndex &Index,
                                  const lto::InputFile &File);

  /// Compute the list of summaries and the subset of declaration summaries
  /// needed for importing into module.
  /// \param Module Module that will import from others.
  /// \param Index Combined module summary index.
  /// \param ModuleToSummariesForIndex Map filled with summaries needed for
  ///        import.
  /// \param DecSummaries Set filled with declaration summaries needed for
  ///        import.
  /// \param File Input file corresponding to \p Module.
  LLVM_ABI void gatherImportedSummariesForModule(
      Module &Module, ModuleSummaryIndex &Index,
      ModuleToSummariesForIndexTy &ModuleToSummariesForIndex,
      GVSummaryPtrSet &DecSummaries, const lto::InputFile &File);

  /// Perform internalization. Index is updated to reflect linkage changes.
  /// \param Module Module to internalize.
  /// \param Index Combined module summary index, updated for linkage changes.
  /// \param File Input file corresponding to \p Module.
  LLVM_ABI void internalize(Module &Module, ModuleSummaryIndex &Index,
                            const lto::InputFile &File);

  /// Perform post-importing ThinLTO optimizations.
  /// \param Module Module to optimize.
  LLVM_ABI void optimize(Module &Module);

  /// Write a generated object file and optionally link it into the cache.
  ///
  /// Writes a temporary object file to SavedObjectDirectoryPath and a symlink
  /// to the Cache directory if needed. Returns the path to the generated file
  /// in SavedObjectsDirectoryPath.
  /// \param count Index used when naming the generated object file.
  /// \param CacheEntryPath Path of the corresponding cache entry, if any.
  /// \param OutputBuffer Object-file contents to write.
  /// \return Path to the generated object file in SavedObjectsDirectoryPath.
  LLVM_ABI std::string writeGeneratedObject(int count, StringRef CacheEntryPath,
                                            const MemoryBuffer &OutputBuffer);
  /**@}*/

private:
  /// Helper factory to build a TargetMachine
  ThinLTOCodeGeneratorImpl::TargetMachineBuilder TMBuilder;

  /// Vector holding the in-memory buffer containing the produced binaries, when
  /// SavedObjectsDirectoryPath isn't set.
  std::vector<std::unique_ptr<MemoryBuffer>> ProducedBinaries;

  /// Path to generated files in the supplied SavedObjectsDirectoryPath if any.
  std::vector<std::string> ProducedBinaryFiles;

  /// Vector holding the input buffers containing the bitcode modules to
  /// process.
  std::vector<std::unique_ptr<lto::InputFile>> Modules;

  /// Set of symbols that need to be preserved outside of the set of bitcode
  /// files.
  StringSet<> PreservedSymbols;

  /// Set of symbols that are cross-referenced between bitcode files.
  StringSet<> CrossReferencedSymbols;

  /// Control the caching behavior.
  CachingOptions CacheOptions;

  /// Path to a directory to save the temporary bitcode files.
  std::string SaveTempsDir;

  /// Path to a directory to save the generated object files.
  std::string SavedObjectsDirectoryPath;

  /// Flag to enable/disable CodeGen. When set to true, the process stops after
  /// optimizations and a bitcode is produced.
  bool DisableCodeGen = false;

  /// Flag to indicate that only the CodeGen will be performed, no cross-module
  /// importing or optimization.
  bool CodeGenOnly = false;

  /// Flag to indicate that the optimizer should not assume builtins are present
  /// on the target.
  bool Freestanding = false;

  /// IR Optimization Level [0-3].
  unsigned OptLevel = 3;

  /// Flag to indicate whether debug output should be enabled for the new pass
  /// manager.
  bool DebugPassManager = false;

  /// -mllvm arguments included in the cache key.
  std::vector<std::string> MllvmArgs;
};
}
#endif
