//===-LTO.h - LLVM Link Time Optimizer ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares functions and classes used to support LTO. It is intended
// to be used both by LTO classes as well as by clients (gold-plugin) that
// don't utilize the LTO code generator interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_LTO_H
#define LLVM_LTO_LTO_H

#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/IR/RuntimeLibcalls.h"
#include "llvm/Support/Compiler.h"
#include <memory>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/LTO/Config.h"
#include "llvm/Object/IRSymtab.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/thread.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/IPO/FunctionImport.h"

namespace llvm {

class Error;
class IRMover;
class LLVMContext;
class MemoryBufferRef;
class Module;
class raw_pwrite_stream;
class ToolOutputFile;

/// Resolve linkage for prevailing symbols in the index.
///
/// Linkage changes recorded in the index and the ThinLTO backends must apply
/// the changes to the module via thinLTOFinalizeInModule.
///
/// This is done for correctness (if value exported, ensure we always
/// emit a copy), and compile-time optimization (allow drop of duplicates).
/// @param C LTO configuration for the link.
/// @param Index Combined module summary index to update.
/// @param isPrevailing Callback that returns true if a GUID's summary is
/// prevailing.
/// @param recordNewLinkage Callback invoked when a symbol's linkage changes.
/// @param GUIDPreservedSymbols GUIDs that must keep external linkage.
LLVM_ABI void thinLTOResolvePrevailingInIndex(
    const lto::Config &C, ModuleSummaryIndex &Index,
    function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
        isPrevailing,
    function_ref<void(StringRef, GlobalValue::GUID, GlobalValue::LinkageTypes)>
        recordNewLinkage,
    const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols);

/// Mark exported values external and non-exported values internal in the index.
///
/// The ThinLTO backends must apply the changes to the Module via
/// thinLTOInternalizeModule.
/// @param Index Combined module summary index to update.
/// @param isExported Callback that returns true if a value is exported.
/// @param isPrevailing Callback that returns true if a GUID's summary is
/// prevailing.
/// @param ExternallyVisibleSymbolNamesPtr Optional set of symbol names that
/// must remain externally visible.
LLVM_ABI void thinLTOInternalizeAndPromoteInIndex(
    ModuleSummaryIndex &Index,
    function_ref<bool(StringRef, ValueInfo)> isExported,
    function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
        isPrevailing,
    DenseSet<StringRef> *ExternallyVisibleSymbolNamesPtr = nullptr);

/// Compute a unique LTO cache key for a module.
///
/// Considers the current list of export/import and other global analysis
/// results.
/// @param Conf LTO configuration contributing to the key.
/// @param Index Combined module summary index.
/// @param ModuleID Identifier of the module being keyed.
/// @param ImportList Functions to import into the module.
/// @param ExportList Functions exported from the module.
/// @param ResolvedODR Resolved ODR linkage types by GUID.
/// @param DefinedGlobals Summaries of globals defined in the module.
/// @param CfiFunctionDefs CFI function definition GUIDs.
/// @param CfiFunctionDecls CFI function declaration GUIDs.
/// @return Unique LTO cache key for the module.
LLVM_ABI std::string computeLTOCacheKey(
    const lto::Config &Conf, const ModuleSummaryIndex &Index,
    StringRef ModuleID, const FunctionImporter::ImportMapTy &ImportList,
    const FunctionImporter::ExportSetTy &ExportList,
    const std::map<GlobalValue::GUID, GlobalValue::LinkageTypes> &ResolvedODR,
    const GVSummaryMapTy &DefinedGlobals,
    const DenseSet<GlobalValue::GUID> &CfiFunctionDefs = {},
    const DenseSet<GlobalValue::GUID> &CfiFunctionDecls = {});

/// Recompute an LTO cache key with an extra identifier.
/// @param Key Existing LTO cache key.
/// @param ExtraID Additional identifier mixed into the key.
/// @return Recomputed LTO cache key incorporating \p ExtraID.
LLVM_ABI std::string recomputeLTOCacheKey(const std::string &Key,
                                          StringRef ExtraID);

namespace lto {

/// Return the default ThinLTO CPU name for the given triple.
/// @param TheTriple Target triple used to select the default CPU.
/// @return Default ThinLTO CPU name for \p TheTriple.
LLVM_ABI StringLiteral getThinLTODefaultCPU(const Triple &TheTriple);

/// Rewrite an output path by replacing a matching prefix.
///
/// Given the original \p Path to an output file, replace any path prefix
/// matching \p OldPrefix with \p NewPrefix. Also, create the resulting
/// directory if it does not yet exist.
/// @param Path Original output file path.
/// @param OldPrefix Path prefix to replace when present.
/// @param NewPrefix Replacement path prefix.
/// @return Rewritten output path with any matching prefix replaced.
LLVM_ABI std::string getThinLTOOutputFile(StringRef Path, StringRef OldPrefix,
                                          StringRef NewPrefix);

/// Setup optimization remarks.
/// @param Context LLVM context that owns the remark streamer.
/// @param RemarksFilename Output path for optimization remarks.
/// @param RemarksPasses Passes for which remarks should be emitted.
/// @param RemarksFormat Serialization format for remarks.
/// @param RemarksWithHotness Whether to include hotness information.
/// @param RemarksHotnessThreshold Minimum hotness for a remark, if set.
/// @param Count Optional counter used when generating unique filenames.
/// @return Remark file handle owned by \p Context, or an error.
LLVM_ABI Expected<LLVMRemarkFileHandle> setupLLVMOptimizationRemarks(
    LLVMContext &Context, StringRef RemarksFilename, StringRef RemarksPasses,
    StringRef RemarksFormat, bool RemarksWithHotness,
    std::optional<uint64_t> RemarksHotnessThreshold = 0, int Count = -1);

/// Setups the output file for saving statistics.
/// @param StatsFilename Path of the statistics output file.
/// @return Tool output file for statistics, or an error.
LLVM_ABI Expected<std::unique_ptr<ToolOutputFile>>
setupStatsFile(StringRef StatsFilename);

/// Produces a container ordering for optimal multi-threaded processing.
///
/// Returns ordered indices to elements in the input array.
/// @param R Bitcode modules to order for processing.
/// @return Ordered indices into the input bitcode module array.
LLVM_ABI std::vector<int> generateModulesOrdering(ArrayRef<BitcodeModule *> R);

class LTO;
struct SymbolResolution;

/// An input file. This is a symbol table wrapper that only exposes the
/// information that an LTO client should need in order to do symbol resolution.
class InputFile {
public:
  struct Symbol;

private:
  // FIXME: Remove LTO class friendship once we have bitcode symbol tables.
  friend LTO;
  InputFile() = default;

  std::vector<BitcodeModule> Mods;
  SmallVector<char, 0> Strtab;
  std::vector<Symbol> Symbols;

  // [begin, end) for each module
  std::vector<std::pair<size_t, size_t>> ModuleSymIndices;

  StringRef TargetTriple, SourceFileName, COFFLinkerOpts;
  std::vector<StringRef> DependentLibraries;
  std::vector<std::pair<StringRef, Comdat::SelectionKind>> ComdatTable;

  MemoryBufferRef MbRef;
  bool IsFatLTOObject = false;
  // For distributed compilation, each input must exist as an individual
  // bitcode file on disk identified by its ModuleID. For archive members and
  // FatLTO objects, the input bitcode is a sub-section of a larger file. In
  // these cases we flag that the bitcode must be written to a temporary
  // standalone file. Effectively, extracted from its container.
  bool ExtractForDistribution = false;
  bool IsThinLTO = false;
  StringRef ArchivePath;
  StringRef MemberName;

public:
  /// Destroy this input file.
  LLVM_ABI ~InputFile();

  /// Create an InputFile.
  /// @param Object Memory buffer containing the input object or bitcode.
  /// @return Newly created InputFile, or an error.
  LLVM_ABI static Expected<std::unique_ptr<InputFile>>
  create(MemoryBufferRef Object);

  /// The purpose of this struct is to only expose the symbol information that
  /// an LTO client should need in order to do symbol resolution.
  struct Symbol : irsymtab::Symbol {
    friend LTO;

  public:
    /// Construct a symbol view from an IR symbol table entry.
    /// @param S Underlying IR symbol table symbol.
    Symbol(const irsymtab::Symbol &S) : irsymtab::Symbol(S) {}

    /// Returns true if the symbol is undefined.
    using irsymtab::Symbol::isUndefined;
    /// Returns true if the symbol is a common definition.
    using irsymtab::Symbol::isCommon;
    /// Returns true if the symbol is weak.
    using irsymtab::Symbol::isWeak;
    /// Returns true if the symbol is an indirect reference.
    using irsymtab::Symbol::isIndirect;
    /// Returns the mangled symbol name.
    using irsymtab::Symbol::getName;
    /// Returns the unmangled IR name, or empty if not an IR symbol.
    using irsymtab::Symbol::getIRName;
    /// Returns the symbol's visibility.
    using irsymtab::Symbol::getVisibility;
    /// Returns true if the symbol may be omitted from the symbol table.
    using irsymtab::Symbol::canBeOmittedFromSymbolTable;
    /// Returns true if the symbol is thread-local.
    using irsymtab::Symbol::isTLS;
    /// Returns the comdat table index, or -1 if not a comdat member.
    using irsymtab::Symbol::getComdatIndex;
    /// Returns the size of a common symbol.
    using irsymtab::Symbol::getCommonSize;
    /// Returns the alignment of a common symbol.
    using irsymtab::Symbol::getCommonAlignment;
    /// Returns the COFF weak-external fallback symbol name.
    using irsymtab::Symbol::getCOFFWeakExternalFallback;
    /// Returns the section name associated with the symbol.
    using irsymtab::Symbol::getSectionName;
    /// Returns true if the symbol is executable.
    using irsymtab::Symbol::isExecutable;
    /// Returns true if the symbol is marked used.
    using irsymtab::Symbol::isUsed;

    /// Return whether this symbol is a library call LTO may reference.
    ///
    /// Such symbols must be considered external, as removing them or modifying
    /// their interfaces would invalidate the code generator's knowledge about
    /// them.
    /// @param TLI Target library info used to classify library calls.
    /// @param Libcalls Runtime libcall info used to classify library calls.
    /// @return True if this symbol is a library call LTO may reference.
    LLVM_ABI bool isLibcall(const TargetLibraryInfo &TLI,
                            const RTLIB::RuntimeLibcallsInfo &Libcalls) const;
  };

  /// A range over the symbols in this InputFile.
  /// @return Range of symbols in this InputFile.
  ArrayRef<Symbol> symbols() const { return Symbols; }

  /// Returns linker options specified in the input file.
  /// @return COFF linker options specified in the input file.
  StringRef getCOFFLinkerOpts() const { return COFFLinkerOpts; }

  /// Returns dependent library specifiers from the input file.
  /// @return Dependent library specifiers from the input file.
  ArrayRef<StringRef> getDependentLibraries() const { return DependentLibraries; }

  /// Returns the path to the InputFile.
  /// @return Path to the InputFile.
  LLVM_ABI StringRef getName() const;

  /// Returns the input file's target triple.
  /// @return Target triple of the input file.
  StringRef getTargetTriple() const { return TargetTriple; }

  /// Returns the source file path specified at compile time.
  /// @return Source file path specified at compile time.
  StringRef getSourceFileName() const { return SourceFileName; }

  /// Returns a table with all the comdats used by this file.
  /// @return Table of comdat names and selection kinds used by this file.
  ArrayRef<std::pair<StringRef, Comdat::SelectionKind>> getComdatTable() const {
    return ComdatTable;
  }

  /// Returns the only BitcodeModule from InputFile.
  /// @return The single BitcodeModule contained in this input file.
  LLVM_ABI BitcodeModule &getSingleBitcodeModule();
  /// Returns the primary BitcodeModule from InputFile.
  /// @return The primary BitcodeModule contained in this input file.
  LLVM_ABI BitcodeModule &getPrimaryBitcodeModule();
  /// Returns the memory buffer reference for this input file.
  /// @return Memory buffer reference for this input file.
  MemoryBufferRef getFileBuffer() const { return MbRef; }
  /// Returns true if this input should be extracted to disk for distribution.
  ///
  /// See the comment on ExtractForDistribution for details.
  /// @return True if the input must be extracted for distribution.
  bool getExtractForDistribution() const { return ExtractForDistribution; }
  /// Mark whether this input should be extracted to disk for distribution.
  ///
  /// See the comment on ExtractForDistribution for details.
  /// @param EFD True if the input must be extracted for distribution.
  void setExtractForDistribution(bool EFD) { ExtractForDistribution = EFD; }
  /// Returns true if this bitcode came from a FatLTO object.
  /// @return True if the bitcode originated in a FatLTO object.
  bool isFatLTOObject() const { return IsFatLTOObject; }
  /// Mark this bitcode as coming from a FatLTO object.
  /// @param FO True if the bitcode originated in a FatLTO object.
  void fatLTOObject(bool FO) { IsFatLTOObject = FO; }

  /// Returns true if bitcode is ThinLTO.
  /// @return True if the bitcode uses ThinLTO.
  bool isThinLTO() const { return IsThinLTO; }

  /// Store an archive path and a member name.
  /// @param Path Path to the containing archive.
  /// @param Name Archive member name within \p Path.
  void setArchivePathAndName(StringRef Path, StringRef Name) {
    ArchivePath = Path;
    MemberName = Name;
  }
  /// Returns the path to the containing archive, if any.
  /// @return Path to the containing archive, or empty if none.
  StringRef getArchivePath() const { return ArchivePath; }
  /// Returns the archive member name, if any.
  /// @return Archive member name, or empty if none.
  StringRef getMemberName() const { return MemberName; }

private:
  ArrayRef<Symbol> module_symbols(unsigned I) const {
    const auto &Indices = ModuleSymIndices[I];
    return {Symbols.data() + Indices.first, Symbols.data() + Indices.second};
  }
};

/// Callback invoked when a ThinLTO index file is written.
using IndexWriteCallback = std::function<void(const std::string &)>;

/// Container of imported file paths for a ThinLTO module.
using ImportsFilesContainer = llvm::SmallVector<std::string>;

/// This class defines the interface to the ThinLTO backend.
class ThinBackendProc {
protected:
  /// LTO configuration for backend jobs.
  const Config &Conf;
  /// Combined module summary index for the thin link.
  ModuleSummaryIndex &CombinedIndex;
  /// Map from module identifier to defined global-value summaries.
  const DenseMap<StringRef, GVSummaryMapTy> &ModuleToDefinedGVSummaries;
  /// Callback invoked when an index file is written.
  IndexWriteCallback OnWrite;
  /// Whether import lists should be written to disk.
  bool ShouldEmitImportsFiles;
  /// Thread pool used to run backend jobs.
  DefaultThreadPool BackendThreadPool;
  /// First error observed while running backend jobs, if any.
  std::optional<Error> Err;
  /// Mutex guarding updates to \p Err.
  std::mutex ErrMu;

public:
  /// Construct a ThinLTO backend process.
  /// @param Conf LTO configuration for backend jobs.
  /// @param CombinedIndex Combined module summary index.
  /// @param ModuleToDefinedGVSummaries Map of defined summaries per module.
  /// @param OnWrite Callback invoked when an index file is written.
  /// @param ShouldEmitImportsFiles Whether to write import lists to disk.
  /// @param ThinLTOParallelism Thread-pool strategy for backend jobs.
  ThinBackendProc(
      const Config &Conf, ModuleSummaryIndex &CombinedIndex,
      const DenseMap<StringRef, GVSummaryMapTy> &ModuleToDefinedGVSummaries,
      lto::IndexWriteCallback OnWrite, bool ShouldEmitImportsFiles,
      ThreadPoolStrategy ThinLTOParallelism)
      : Conf(Conf), CombinedIndex(CombinedIndex),
        ModuleToDefinedGVSummaries(ModuleToDefinedGVSummaries),
        OnWrite(OnWrite), ShouldEmitImportsFiles(ShouldEmitImportsFiles),
        BackendThreadPool(ThinLTOParallelism) {}

  /// Destroy this ThinLTO backend process.
  virtual ~ThinBackendProc() = default;
  /// Prepare the backend for a batch of ThinLTO tasks.
  /// @param ThinLTONumTasks Number of ThinLTO tasks to run.
  /// @param ThinLTOTaskOffset Starting task identifier offset.
  /// @param Triple Target triple for the ThinLTO jobs.
  virtual void setup(unsigned ThinLTONumTasks, unsigned ThinLTOTaskOffset,
                     Triple Triple) {}
  /// Start processing one ThinLTO backend task.
  /// @param Task Task identifier for this backend job.
  /// @param BM Bitcode module to compile.
  /// @param ImportList Functions to import into the module.
  /// @param ExportList Functions exported from the module.
  /// @param ResolvedODR Resolved ODR linkage types by GUID.
  /// @param ModuleMap Map of module identifiers to bitcode modules.
  /// @return Success, or an error describing why the task could not be started.
  virtual Error start(
      unsigned Task, BitcodeModule BM,
      const FunctionImporter::ImportMapTy &ImportList,
      const FunctionImporter::ExportSetTy &ExportList,
      const std::map<GlobalValue::GUID, GlobalValue::LinkageTypes> &ResolvedODR,
      MapVector<StringRef, BitcodeModule> &ModuleMap) = 0;
  /// Wait for outstanding backend jobs to finish.
  /// @return Success, or the first error observed while running backend jobs.
  virtual Error wait() {
    BackendThreadPool.wait();
    if (Err)
      return std::move(*Err);
    return Error::success();
  }
  /// Return the maximum number of concurrent backend threads.
  /// @return Maximum number of concurrent backend threads.
  unsigned getThreadCount() { return BackendThreadPool.getMaxConcurrency(); }
  /// Return true if backend results depend on input processing order.
  /// @return True if backend results depend on input processing order.
  virtual bool isSensitiveToInputOrder() { return false; }

  /// Write sharded indices and optionally imports to disk.
  /// @param ImportList Functions imported by the module.
  /// @param Task Task identifier associated with the module.
  /// @param ModulePath Path of the input module.
  /// @param NewModulePath Output path used for derived artifacts.
  /// @return Success, or an error describing why the files could not be written.
  LLVM_ABI Error emitFiles(const FunctionImporter::ImportMapTy &ImportList,
                           unsigned Task, StringRef ModulePath,
                           const std::string &NewModulePath) const;

  /// Write sharded indices, optionally imports, and optionally record imports.
  /// @param ImportList Functions imported by the module.
  /// @param Task Task identifier associated with the module.
  /// @param ModulePath Path of the input module.
  /// @param NewModulePath Output path used for derived artifacts.
  /// @param SummaryPath Path where the sharded summary index is written.
  /// @return Success, or an error describing why the files could not be written.
  LLVM_ABI Error emitFiles(const FunctionImporter::ImportMapTy &ImportList,
                           unsigned Task, StringRef ModulePath,
                           const std::string &NewModulePath,
                           StringRef SummaryPath) const;
};

/// Callable defining ThinLTO backend behavior after the thin-link phase.
///
/// It accepts a configuration \p C, a combined module summary index
/// \p CombinedIndex, a map of module identifiers to global variable summaries
/// \p ModuleToDefinedGVSummaries, a function to add output streams \p
/// AddStream, and a file cache \p Cache. It returns a unique pointer to a
/// ThinBackendProc, which can be used to launch backends in parallel.
using ThinBackendFunction = std::function<std::unique_ptr<ThinBackendProc>(
    const Config &C, ModuleSummaryIndex &CombinedIndex,
    const DenseMap<StringRef, GVSummaryMapTy> &ModuleToDefinedGVSummaries,
    AddStreamFn AddStream, FileCache Cache,
    ArrayRef<StringRef> BitcodeLibFuncs)>;

/// ThinLTO backend behavior and thread-pool strategy after the thin-link.
///
/// It encapsulates a backend function and a strategy for thread pool
/// parallelism. Clients should use one of the provided create*ThinBackend()
/// functions to instantiate a ThinBackend. Parallelism defines the thread pool
/// strategy to be used for processing.
struct ThinBackend {
  /// Construct a ThinBackend from a backend function and parallelism strategy.
  /// @param Func Backend function invoked after the thin-link phase.
  /// @param Parallelism Thread-pool strategy for backend jobs.
  ThinBackend(ThinBackendFunction Func, ThreadPoolStrategy Parallelism)
      : Func(std::move(Func)), Parallelism(std::move(Parallelism)) {}
  /// Construct an empty, invalid ThinBackend.
  ThinBackend() = default;

  /// Invoke the configured ThinLTO backend function.
  /// @param Conf LTO configuration for backend jobs.
  /// @param CombinedIndex Combined module summary index.
  /// @param ModuleToDefinedGVSummaries Map of defined summaries per module.
  /// @param AddStream Callback used to create output streams.
  /// @param Cache Optional native-object file cache.
  /// @param BitcodeLibFuncs Bitcode-implemented library function names.
  /// @return Backend process used to launch ThinLTO jobs in parallel.
  std::unique_ptr<ThinBackendProc> operator()(
      const Config &Conf, ModuleSummaryIndex &CombinedIndex,
      const DenseMap<StringRef, GVSummaryMapTy> &ModuleToDefinedGVSummaries,
      AddStreamFn AddStream, FileCache Cache,
      ArrayRef<StringRef> BitcodeLibFuncs) {
    assert(isValid() && "Invalid backend function");
    return Func(Conf, CombinedIndex, ModuleToDefinedGVSummaries,
                std::move(AddStream), std::move(Cache), BitcodeLibFuncs);
  }
  /// Return the configured thread-pool parallelism strategy.
  /// @return Configured thread-pool parallelism strategy.
  ThreadPoolStrategy getParallelism() const { return Parallelism; }
  /// Return true if a backend function has been configured.
  /// @return True if a backend function has been configured.
  bool isValid() const { return static_cast<bool>(Func); }

private:
  ThinBackendFunction Func = nullptr;
  ThreadPoolStrategy Parallelism;
};

/// Create a ThinBackend that runs individual jobs in-process.
///
/// The default parallelism value means to use one job per hardware core (not
/// hyper-thread). OnWrite is callback which receives module identifier and
/// notifies LTO user that index file for the module (and optionally imports
/// file) was created. ShouldEmitIndexFiles being true will write sharded
/// ThinLTO index files to the same path as the input module, with suffix
/// ".thinlto.bc". ShouldEmitImportsFiles is true it also writes a list of
/// imported files to a similar path with ".imports" appended instead.
/// @param Parallelism Thread-pool strategy for in-process backend jobs.
/// @param OnWrite Optional callback when an index file is written.
/// @param ShouldEmitIndexFiles Whether to write sharded ThinLTO index files.
/// @param ShouldEmitImportsFiles Whether to write import lists to disk.
/// @return ThinBackend that runs individual jobs in-process.
LLVM_ABI ThinBackend createInProcessThinBackend(
    ThreadPoolStrategy Parallelism, IndexWriteCallback OnWrite = nullptr,
    bool ShouldEmitIndexFiles = false, bool ShouldEmitImportsFiles = false);

/// Create a ThinBackend that writes module indexes for distributed builds.
///
/// This backend writes individual module indexes to files, instead of running
/// the individual backend jobs. This backend is for distributed builds where
/// separate processes will invoke the real backends.
///
/// To find the path to write the index to, the backend checks if the path has a
/// prefix of OldPrefix; if so, it replaces that prefix with NewPrefix. It then
/// appends ".thinlto.bc" and writes the index to that path. If
/// ShouldEmitImportsFiles is true it also writes a list of imported files to a
/// similar path with ".imports" appended instead.
/// LinkedObjectsFile is an output stream to write the list of object files for
/// the final ThinLTO linking. Can be nullptr.  If LinkedObjectsFile is not
/// nullptr and NativeObjectPrefix is not empty then it replaces the prefix of
/// the objects with NativeObjectPrefix instead of NewPrefix. OnWrite is
/// callback which receives module identifier and notifies LTO user that index
/// file for the module (and optionally imports file) was created.
/// @param Parallelism Thread-pool strategy for index-writing jobs.
/// @param OldPrefix Path prefix to replace in module paths.
/// @param NewPrefix Replacement path prefix for index output paths.
/// @param NativeObjectPrefix Optional prefix used when listing native objects.
/// @param ShouldEmitImportsFiles Whether to write import lists to disk.
/// @param LinkedObjectsFile Optional stream listing linked object paths.
/// @param OnWrite Callback invoked when an index file is written.
/// @return ThinBackend that writes sharded indexes for distributed builds.
LLVM_ABI ThinBackend createWriteIndexesThinBackend(
    ThreadPoolStrategy Parallelism, std::string OldPrefix,
    std::string NewPrefix, std::string NativeObjectPrefix,
    bool ShouldEmitImportsFiles, raw_fd_ostream *LinkedObjectsFile,
    IndexWriteCallback OnWrite);

/// Resolution-based interface to LLVM's LTO functionality.
///
/// It supports regular LTO, parallel LTO code generation and ThinLTO. You can
/// use it from a linker in the following way:
/// - Set hooks and code generation options (see lto::Config struct defined in
///   Config.h), and use the lto::Config object to create an lto::LTO object.
/// - Create lto::InputFile objects using lto::InputFile::create(), then use
///   the symbols() function to enumerate its symbols and compute a resolution
///   for each symbol (see SymbolResolution below).
/// - After the linker has visited each input file (and each regular object
///   file) and computed a resolution for each symbol, take each lto::InputFile
///   and pass it and an array of symbol resolutions to the add() function.
/// - Call the getMaxTasks() function to get an upper bound on the number of
///   native object files that LTO may add to the link.
/// - Call the run() function. This function will use the supplied AddStream
///   and Cache functions to add up to getMaxTasks() native object files to
///   the link.
class LLVM_ABI LTO {
  friend InputFile;

public:
  /// Unified LTO modes
  enum LTOKind {
    /// Any LTO mode without Unified LTO. The default mode.
    LTOK_Default,

    /// Regular LTO, with Unified LTO enabled.
    LTOK_UnifiedRegular,

    /// ThinLTO, with Unified LTO enabled.
    LTOK_UnifiedThin,
  };

  /// Create an LTO object with the given configuration.
  ///
  /// A default constructed LTO object has a reasonable production
  /// configuration, but you can customize it by passing arguments to this
  /// constructor.
  /// FIXME: We do currently require the DiagHandler field to be set in Conf.
  /// Until that is fixed, a Config argument is required.
  /// @param Conf LTO configuration options.
  /// @param Backend ThinLTO backend to use after the thin-link phase.
  /// @param ParallelCodeGenParallelismLevel Parallelism for regular LTO codegen.
  /// @param LTOMode Unified LTO mode for this link.
  LTO(Config Conf, ThinBackend Backend = {},
      unsigned ParallelCodeGenParallelismLevel = 1,
      LTOKind LTOMode = LTOK_Default);
  /// Destroy this LTO object.
  virtual ~LTO();

  /// Add an input file to the LTO link.
  ///
  /// The symbol resolutions must appear in the enumeration order given by
  /// InputFile::symbols().
  /// @param Obj Input file to add to the link.
  /// @param Res Symbol resolutions matching \p Obj's symbols.
  /// @return Success, or an error describing why the input could not be added.
  Error add(std::unique_ptr<InputFile> Obj, ArrayRef<SymbolResolution> Res);

  /// Set bitcode-implemented library functions not extracted from an archive.
  ///
  /// Such functions may not be referenced, as they have lost their opportunity
  /// to be defined.
  /// @param BitcodeLibFuncs Names of unavailable bitcode library functions.
  void setBitcodeLibFuncs(ArrayRef<StringRef> BitcodeLibFuncs);

  /// Return an upper bound on the number of client tasks.
  ///
  /// This may only be called after all IR object files have been added. For a
  /// full description of tasks see LTOBackend.h.
  /// @return Upper bound on the number of client tasks LTO may produce.
  unsigned getMaxTasks() const;

  /// Runs the LTO pipeline. This function calls the supplied AddStream
  /// function to add native object files to the link.
  ///
  /// The Cache parameter is optional. If supplied, it will be used to cache
  /// native object files and add them to the link.
  ///
  /// The client will receive at most one callback (via either AddStream or
  /// Cache) for each task identifier.
  /// @param AddStream Callback used to create output streams for tasks.
  /// @param Cache Optional native-object file cache.
  /// @return Success, or an error describing why the LTO pipeline failed.
  virtual Error run(AddStreamFn AddStream, FileCache Cache = {});

  /// Wait for cleanup work started by run() to finish.
  ///
  /// A client may delay this call to overlap asynchronous cleanup with later
  /// linking work, but must call it before finalizing time trace data because
  /// cleanup may emit time trace events. Most LTO implementations have no
  /// asynchronous cleanup.
  virtual void waitForCleanup() {}

  /// Return runtime libcall symbols LTO may generate.
  ///
  /// These symbols might not be visible from the bitcode symbol table.
  /// @param TT Target triple used to select runtime libcalls.
  /// @return Runtime libcall symbol names LTO may generate.
  static SmallVector<const char *> getRuntimeLibcallSymbols(const Triple &TT);

  /// Return library function symbols LTO may generate.
  ///
  /// These symbols might not be visible from the bitcode symbol table. Unlike
  /// the runtime libcalls, the linker can report to the code generator which of
  /// these are actually available in the link, and the code generator can then
  /// only reference that set of symbols.
  /// @param TT Target triple used to select library functions.
  /// @param Saver String saver that owns returned symbol names.
  /// @return Library function symbol names LTO may generate.
  static SmallVector<StringRef> getLibFuncSymbols(const Triple &TT,
                                                  llvm::StringSaver &Saver);

protected:
  /// Perform cleanup before returning from run().
  virtual void cleanup();

  /// LTO configuration for this link.
  Config Conf;

  /// State for regular (full) LTO modules in the link.
  struct RegularLTOState {
    /// Construct regular LTO state for the given configuration.
    /// @param ParallelCodeGenParallelismLevel Parallelism for codegen partitions.
    /// @param Conf LTO configuration used to create the combined module context.
    LLVM_ABI RegularLTOState(unsigned ParallelCodeGenParallelismLevel,
                             const Config &Conf);
    /// Resolved size and alignment for a common symbol.
    struct CommonResolution {
      /// Size of the common symbol in bytes.
      uint64_t Size = 0;
      /// Required alignment of the common symbol.
      Align Alignment;
      /// Record if at least one instance of the common was marked as prevailing
      bool Prevailing = false;
    };
    /// Common-symbol resolutions keyed by mangled name.
    std::map<std::string, CommonResolution> Commons;

    /// Parallelism level for regular LTO code generation.
    unsigned ParallelCodeGenParallelismLevel;
    /// LLVM context that owns the combined regular LTO module.
    LTOLLVMContext Ctx;
    /// Combined IR module for regular LTO.
    std::unique_ptr<Module> CombinedModule;
    /// IR mover used to merge modules into \p CombinedModule.
    std::unique_ptr<IRMover> Mover;

    /// Information about a regular LTO module added to the link.
    ///
    /// It will either be linked immediately (for modules without summaries) or
    /// after summary-based dead stripping (for modules with summaries).
    struct AddedModule {
      /// Parsed IR module for this regular LTO input.
      std::unique_ptr<Module> M;
      /// Global values that must be kept alive when linking.
      std::vector<GlobalValue *> Keep;
    };
    /// Regular LTO modules that carry summaries and are linked later.
    std::vector<AddedModule> ModsWithSummaries;
    /// True if no IR has been linked into CombinedModule yet.
    bool EmptyCombinedModule = true;
  } RegularLTO; ///< Regular LTO state for this link.

  /// Map from module identifier to ThinLTO bitcode module.
  using ModuleMapType = MapVector<StringRef, BitcodeModule>;

  /// State for ThinLTO modules in the link.
  struct ThinLTOState {
    /// Construct ThinLTO state with the given backend.
    /// @param Backend ThinLTO backend invoked after the thin-link.
    LLVM_ABI ThinLTOState(ThinBackend Backend);

    /// ThinLTO backend used after the thin-link phase.
    ThinBackend Backend;
    /// Combined ThinLTO module summary index.
    ModuleSummaryIndex CombinedIndex;
    /// The full set of bitcode modules in input order.
    ModuleMapType ModuleMap;
    /// The bitcode modules to compile, if specified by the LTO Config.
    std::optional<ModuleMapType> ModulesToCompile;

    /// Record the prevailing module for \p GUID.
    /// @param GUID Global value GUID whose prevailing module is set.
    /// @param Module Identifier of the module that prevails for \p GUID.
    void setPrevailingModuleForGUID(GlobalValue::GUID GUID, StringRef Module) {
      PrevailingModuleForGUID[GUID] = Module;
    }
    /// Return true if \p Module is prevailing for \p GUID.
    /// @param GUID Global value GUID to query.
    /// @param Module Module identifier to compare against the prevailing one.
    /// @return True if \p Module is the prevailing module for \p GUID.
    bool isPrevailingModuleForGUID(GlobalValue::GUID GUID,
                                   StringRef Module) const {
      auto It = PrevailingModuleForGUID.find(GUID);
      return It != PrevailingModuleForGUID.end() && It->second == Module;
    }

  private:
    // Make this private so all accesses must go through above accessor methods
    // to avoid inadvertently creating new entries on lookups.
    DenseMap<GlobalValue::GUID, StringRef> PrevailingModuleForGUID;
  } ThinLTO; ///< ThinLTO state for this link.

private:
  // The global resolution for a particular (mangled) symbol name. This is in
  // particular necessary to track whether each symbol can be internalized.
  // Because any input file may introduce a new cross-partition reference, we
  // cannot make any final internalization decisions until all input files have
  // been added and the client has called run(). During run() we apply
  // internalization decisions either directly to the module (for regular LTO)
  // or to the combined index (for ThinLTO).
  // FIXME: Make this GlobalResolution a class, it has been becoming more than
  // just a data bag.
  struct GlobalResolution {
    /// The unmangled name of the global.
    std::string IRName;

    /// Keep track if the symbol is visible outside of a module with a summary
    /// (i.e. in either a regular object or a regular LTO module without a
    /// summary).
    bool VisibleOutsideSummary = false;

    /// The symbol was exported dynamically, and therefore could be referenced
    /// by a shared library not visible to the linker.
    bool ExportDynamic = false;

    bool UnnamedAddr = true;

    /// True if module contains the prevailing definition.
    bool Prevailing = false;

    /// Returns true if module contains the prevailing definition and symbol is
    /// an IR symbol. For example when module-level inline asm block is used,
    /// symbol can be prevailing in module but have no IR name.
    bool isPrevailingIRSymbol() const { return Prevailing && !IRName.empty(); }

    /// This field keeps track of the partition number of this global. The
    /// regular LTO object is partition 0, while each ThinLTO object has its own
    /// partition number from 1 onwards.
    ///
    /// Any global that is defined or used by more than one partition, or that
    /// is referenced externally, may not be internalized.
    ///
    /// Partitions generally have a one-to-one correspondence with tasks, except
    /// that we use partition 0 for all parallel LTO code generation partitions.
    /// Any partitioning of the combined LTO object is done internally by the
    /// LTO backend.
    unsigned Partition = Unknown;

  private:
    GlobalValue::GUID GUID = 0;

  public:
    void setGUID(GlobalValue::GUID G) {
      assert(G);
      assert(!GUID || GUID == G);
      GUID = G;
    }

    GlobalValue::GUID getGUID() const {
      return GUID ? GUID
                  : GlobalValue::getGUIDAssumingExternalLinkage(
                        GlobalValue::getGlobalIdentifier(
                            IRName, GlobalValue::LinkageTypes::ExternalLinkage,
                            ""));
    }

    /// Special partition numbers.
    enum : unsigned {
      /// A partition number has not yet been assigned to this global.
      Unknown = -1u,

      /// This global is either used by more than one partition or has an
      /// external reference, and therefore cannot be internalized.
      External = -2u,

      /// The RegularLTO partition
      RegularLTO = 0,
    };
  };

  // GlobalResolutionSymbolSaver allocator.
  std::unique_ptr<llvm::BumpPtrAllocator> Alloc;

  // Symbol saver for global resolution map.
  std::unique_ptr<llvm::StringSaver> GlobalResolutionSymbolSaver;

  // Global mapping from mangled symbol names to resolutions.
  // Make this an unique_ptr to guard against accessing after it has been reset
  // (to reduce memory after we're done with it).
  std::unique_ptr<llvm::DenseMap<StringRef, GlobalResolution>>
      GlobalResolutions;

  void releaseGlobalResolutionsMemory();

  void addModuleToGlobalRes(ArrayRef<InputFile::Symbol> Syms,
                            ArrayRef<SymbolResolution> Res, unsigned Partition,
                            bool InSummary, const Triple &TT);

  // These functions take a range of symbol resolutions and consume the
  // resolutions used by a single input module. Functions return ranges refering
  // to the resolutions for the remaining modules in the InputFile.
  Expected<ArrayRef<SymbolResolution>>
  addModule(InputFile &Input, ArrayRef<SymbolResolution> InputRes,
            unsigned ModI, ArrayRef<SymbolResolution> Res);

  Expected<std::pair<RegularLTOState::AddedModule, ArrayRef<SymbolResolution>>>
  addRegularLTO(InputFile &Input, ArrayRef<SymbolResolution> InputRes,
                BitcodeModule BM, ArrayRef<InputFile::Symbol> Syms,
                ArrayRef<SymbolResolution> Res);
  Error linkRegularLTO(RegularLTOState::AddedModule Mod,
                       bool LivenessFromIndex);

  Expected<ArrayRef<SymbolResolution>>
  addThinLTO(BitcodeModule BM, ArrayRef<InputFile::Symbol> Syms,
             ArrayRef<SymbolResolution> Res);

  Error runRegularLTO(AddStreamFn AddStream);
  Error runThinLTO(AddStreamFn AddStream, FileCache Cache,
                   const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols);

  Error checkPartiallySplit();

  mutable bool CalledGetMaxTasks = false;

protected:
  /// Unified LTO mode for this link.
  LTOKind LTOMode;

private:
  // Use Optional to distinguish false from not yet initialized.
  std::optional<bool> EnableSplitLTOUnit;

  // Identify symbols exported dynamically, and that therefore could be
  // referenced by a shared library not visible to the linker.
  DenseSet<GlobalValue::GUID> DynamicExportSymbols;

  // Diagnostic optimization remarks file
  LLVMRemarkFileHandle DiagnosticOutputFile;

  // A dummy module to host the dummy function.
  std::unique_ptr<Module> DummyModule;

  // A dummy function created in a private module to provide a context for
  // LTO-link optimization remarks. This is needed for ThinLTO where we
  // may not have any IR functions available, because the optimization remark
  // handling requires a function.
  Function *LinkerRemarkFunction = nullptr;

  // Setup optimization remarks according to the provided configuration.
  Error setupOptimizationRemarks();

  // LibFuncs that were implemented in bitcode but were not extracted
  // from their libraries. Such functions cannot safely be called, since
  // they have lost their opportunity to be defined.
  SmallVector<StringRef> BitcodeLibFuncs;

public:
  /// Helper to emit an optimization remark during the LTO link when outside of
  /// the standard optimization pass pipeline.
  /// @param Remark Optimization remark to emit.
  void emitRemark(OptimizationRemark &Remark);

  /// Take ownership of an input file and return a shared handle to it.
  /// @param InputPtr Input file to adopt into the LTO link.
  /// @return Shared handle to the adopted input file, or an error.
  virtual Expected<std::shared_ptr<lto::InputFile>>
  addInput(std::unique_ptr<lto::InputFile> InputPtr) {
    return std::shared_ptr<lto::InputFile>(InputPtr.release());
  }
};

/// The resolution for a symbol. The linker must provide a SymbolResolution for
/// each global symbol based on its internal resolution of that symbol.
struct SymbolResolution {
  /// Construct a SymbolResolution with all flags cleared.
  SymbolResolution()
      : Prevailing(0), FinalDefinitionInLinkageUnit(0), VisibleToRegularObj(0),
        ExportDynamic(0), LinkerRedefined(0) {}

  /// The linker has chosen this definition of the symbol.
  unsigned Prevailing : 1;

  /// The definition of this symbol is unpreemptable at runtime and is known to
  /// be in this linkage unit.
  unsigned FinalDefinitionInLinkageUnit : 1;

  /// The definition of this symbol is visible outside of the LTO unit.
  unsigned VisibleToRegularObj : 1;

  /// The symbol was exported dynamically, and therefore could be referenced
  /// by a shared library not visible to the linker.
  unsigned ExportDynamic : 1;

  /// Linker redefined version of the symbol which appeared in -wrap or -defsym
  /// linker option.
  unsigned LinkerRedefined : 1;
};

} // namespace lto
} // namespace llvm

#endif
