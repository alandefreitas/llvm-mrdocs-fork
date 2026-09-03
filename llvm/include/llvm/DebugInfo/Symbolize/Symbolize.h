//===- Symbolize.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Header for LLVM symbolization library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZE_H
#define LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/simple_ilist.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/BuildID.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
namespace object {
class ELFObjectFileBase;
class MachOObjectFile;
class ObjectFile;
struct SectionedAddress;
} // namespace object

namespace symbolize {

class SymbolizableModule;

using namespace object;

using FunctionNameKind = DILineInfoSpecifier::FunctionNameKind;
/// Alias for how much path information to include in file/line results.
using FileLineInfoKind = DILineInfoSpecifier::FileLineInfoKind;

class CachedBinary;

/// Facade that loads modules and resolves addresses to source locations.
class LLVMSymbolizer {
public:
  /// Configuration controlling how modules are loaded and results are formed.
  struct Options {
    /// How function names should appear in symbolization results.
    FunctionNameKind PrintFunctions = FunctionNameKind::LinkageName;
    /// How much path information to include for file/line lookups.
    FileLineInfoKind PathStyle = FileLineInfoKind::AbsoluteFilePath;
    /// Prefer approximate line info when the exact line is zero or missing.
    bool SkipLineZero = false;
    /// Fall back to the symbol table when debug info is unavailable.
    bool UseSymbolTable = true;
    /// Demangle C++ (and Microsoft) symbol names in results.
    bool Demangle = true;
    /// Treat module offsets as relative to the module preferred base.
    bool RelativeAddresses = false;
    /// Strip hardware memory-tag bits from addresses before lookup.
    bool UntagAddresses = false;
    /// Prefer the DIA PDB reader over the native PDB reader when available.
    bool UseDIA = false;
    /// Do not look up or use GSYM files for symbolization.
    bool DisableGsym = false;
    /// Default architecture name for multi-arch inputs such as Mach-O universals.
    std::string DefaultArch;
    /// Extra directories to search for Darwin .dSYM bundles.
    std::vector<std::string> DsymHints;
    /// Fallback directory used when resolving GNU debuglink paths.
    std::string FallbackDebugPath;
    /// Explicit path to a DWP file to use instead of automatic discovery.
    std::string DWPName;
    /// Explicit path to a PDB file to use instead of automatic discovery.
    std::string PDBName;
    /// Directories searched for separate debug files and Build ID lookups.
    std::vector<std::string> DebugFileDirectory;
    /// Directories searched for GSYM files corresponding to binaries.
    std::vector<std::string> GsymFileDirectory;
    /// Maximum total size of cached binaries before pruneCache evicts entries.
    size_t MaxCacheSize =
        sizeof(size_t) == 4
            ? 512 * 1024 * 1024 /* 512 MiB */
            : static_cast<size_t>(4ULL * 1024 * 1024 * 1024) /* 4 GiB */;
  };

  /// Construct a symbolizer with default options.
  LLVM_ABI LLVMSymbolizer();
  /// Construct a symbolizer with the given options.
  /// @param Opts Configuration for module loading and result formatting.
  LLVM_ABI LLVMSymbolizer(const Options &Opts);

  /// Destroy the symbolizer and release cached modules and binaries.
  LLVM_ABI ~LLVMSymbolizer();

  /// Symbolize a code address in an already-opened object file.
  ///
  /// Object-file overloads do not currently support COFF.
  /// @param Obj Object file that contains the code address.
  /// @param ModuleOffset Sectioned address within \p Obj to resolve.
  /// @return Line info for the address, or an error on failure.
  LLVM_ABI Expected<DILineInfo>
  symbolizeCode(const ObjectFile &Obj, object::SectionedAddress ModuleOffset);
  /// Symbolize a code address identified by module name.
  /// @param ModuleName Path or name of the module to load.
  /// @param ModuleOffset Sectioned address within the module to resolve.
  /// @return Line info for the address, or an error on failure.
  LLVM_ABI Expected<DILineInfo>
  symbolizeCode(StringRef ModuleName, object::SectionedAddress ModuleOffset);
  /// Symbolize a code address identified by Build ID.
  /// @param BuildID Build ID used to locate the corresponding binary.
  /// @param ModuleOffset Sectioned address within the module to resolve.
  /// @return Line info for the address, or an error on failure.
  LLVM_ABI Expected<DILineInfo>
  symbolizeCode(ArrayRef<uint8_t> BuildID,
                object::SectionedAddress ModuleOffset);
  /// Symbolize a code address including inlined callers in an object file.
  ///
  /// Object-file overloads do not currently support COFF.
  /// @param Obj Object file that contains the code address.
  /// @param ModuleOffset Sectioned address within \p Obj to resolve.
  /// @return Inlining info for the address, or an error on failure.
  LLVM_ABI Expected<DIInliningInfo>
  symbolizeInlinedCode(const ObjectFile &Obj,
                       object::SectionedAddress ModuleOffset);
  /// Symbolize a code address including inlined callers by module name.
  /// @param ModuleName Path or name of the module to load.
  /// @param ModuleOffset Sectioned address within the module to resolve.
  /// @return Inlining info for the address, or an error on failure.
  LLVM_ABI Expected<DIInliningInfo>
  symbolizeInlinedCode(StringRef ModuleName,
                       object::SectionedAddress ModuleOffset);
  /// Symbolize a code address including inlined callers by Build ID.
  /// @param BuildID Build ID used to locate the corresponding binary.
  /// @param ModuleOffset Sectioned address within the module to resolve.
  /// @return Inlining info for the address, or an error on failure.
  LLVM_ABI Expected<DIInliningInfo>
  symbolizeInlinedCode(ArrayRef<uint8_t> BuildID,
                       object::SectionedAddress ModuleOffset);

  /// Symbolize a data address in an already-opened object file.
  ///
  /// Object-file overloads do not currently support COFF.
  /// @param Obj Object file that contains the data address.
  /// @param ModuleOffset Sectioned address within \p Obj to resolve.
  /// @return Global data info for the address, or an error on failure.
  LLVM_ABI Expected<DIGlobal>
  symbolizeData(const ObjectFile &Obj, object::SectionedAddress ModuleOffset);
  /// Symbolize a data address identified by module name.
  /// @param ModuleName Path or name of the module to load.
  /// @param ModuleOffset Sectioned address within the module to resolve.
  /// @return Global data info for the address, or an error on failure.
  LLVM_ABI Expected<DIGlobal>
  symbolizeData(StringRef ModuleName, object::SectionedAddress ModuleOffset);
  /// Symbolize a data address identified by Build ID.
  /// @param BuildID Build ID used to locate the corresponding binary.
  /// @param ModuleOffset Sectioned address within the module to resolve.
  /// @return Global data info for the address, or an error on failure.
  LLVM_ABI Expected<DIGlobal>
  symbolizeData(ArrayRef<uint8_t> BuildID,
                object::SectionedAddress ModuleOffset);
  /// Symbolize local variables for a frame address in an object file.
  ///
  /// Object-file overloads do not currently support COFF.
  /// @param Obj Object file that contains the frame address.
  /// @param ModuleOffset Sectioned address within \p Obj to resolve.
  /// @return Local variables for the frame, or an error on failure.
  LLVM_ABI Expected<std::vector<DILocal>>
  symbolizeFrame(const ObjectFile &Obj, object::SectionedAddress ModuleOffset);
  /// Symbolize local variables for a frame address by module name.
  /// @param ModuleName Path or name of the module to load.
  /// @param ModuleOffset Sectioned address within the module to resolve.
  /// @return Local variables for the frame, or an error on failure.
  LLVM_ABI Expected<std::vector<DILocal>>
  symbolizeFrame(StringRef ModuleName, object::SectionedAddress ModuleOffset);
  /// Symbolize local variables for a frame address by Build ID.
  /// @param BuildID Build ID used to locate the corresponding binary.
  /// @param ModuleOffset Sectioned address within the module to resolve.
  /// @return Local variables for the frame, or an error on failure.
  LLVM_ABI Expected<std::vector<DILocal>>
  symbolizeFrame(ArrayRef<uint8_t> BuildID,
                 object::SectionedAddress ModuleOffset);

  /// Find source locations for a named symbol in an object file.
  ///
  /// Object-file overloads do not currently support COFF.
  /// @param Obj Object file that may contain \p Symbol.
  /// @param Symbol Symbol name to look up.
  /// @param Offset Byte offset added to each matching symbol address.
  /// @return Line info for each match, or an error on failure.
  LLVM_ABI Expected<std::vector<DILineInfo>>
  findSymbol(const ObjectFile &Obj, StringRef Symbol, uint64_t Offset);
  /// Find source locations for a named symbol by module name.
  /// @param ModuleName Path or name of the module to load.
  /// @param Symbol Symbol name to look up.
  /// @param Offset Byte offset added to each matching symbol address.
  /// @return Line info for each match, or an error on failure.
  LLVM_ABI Expected<std::vector<DILineInfo>>
  findSymbol(StringRef ModuleName, StringRef Symbol, uint64_t Offset);
  /// Find source locations for a named symbol by Build ID.
  /// @param BuildID Build ID used to locate the corresponding binary.
  /// @param Symbol Symbol name to look up.
  /// @param Offset Byte offset added to each matching symbol address.
  /// @return Line info for each match, or an error on failure.
  LLVM_ABI Expected<std::vector<DILineInfo>>
  findSymbol(ArrayRef<uint8_t> BuildID, StringRef Symbol, uint64_t Offset);

  /// Clear cached modules, binaries, and related lookup state.
  LLVM_ABI void flush();

  /// Evict cached binaries until the cache is under Options::MaxCacheSize.
  ///
  /// Calling this invalidates references in the DI... objects returned by the
  /// methods above.
  LLVM_ABI void pruneCache();

  /// Demangle \p Name using module context when available.
  /// @param Name Mangled or undecorated symbol name to demangle.
  /// @param DbiModuleDescriptor Module that may supply demangling context,
  ///        or nullptr.
  /// @return Demangled name, or \p Name unchanged when demangling fails.
  LLVM_ABI static std::string
  DemangleName(StringRef Name, const SymbolizableModule *DbiModuleDescriptor);

  /// Install the Build ID fetcher used to locate binaries by Build ID.
  /// @param Fetcher Build ID fetcher to take ownership of, or an empty
  ///        unique_ptr to clear it.
  void setBuildIDFetcher(std::unique_ptr<BuildIDFetcher> Fetcher) {
    BIDFetcher = std::move(Fetcher);
  }

  /// Get or create the SymbolizableModule for a named module.
  ///
  /// Returns a SymbolizableModule or an error if loading debug info failed.
  /// Only one attempt is made to load a module, and errors during loading are
  /// only reported once. Subsequent calls to get module info for a module that
  /// failed to load will return nullptr.
  /// @param ModuleName Path or name of the module to load.
  /// @return Symbolizable module, nullptr after a prior load failure, or an
  ///         error on the first failed load.
  LLVM_ABI Expected<SymbolizableModule *>
  getOrCreateModuleInfo(StringRef ModuleName);

private:
  // Bundles together object file with code/data and object file with
  // corresponding debug info. These objects can be the same.
  using ObjectPair = std::pair<const ObjectFile *, const ObjectFile *>;

  template <typename T>
  Expected<DILineInfo>
  symbolizeCodeCommon(const T &ModuleSpecifier,
                      object::SectionedAddress ModuleOffset);
  template <typename T>
  Expected<DIInliningInfo>
  symbolizeInlinedCodeCommon(const T &ModuleSpecifier,
                             object::SectionedAddress ModuleOffset);
  template <typename T>
  Expected<DIGlobal> symbolizeDataCommon(const T &ModuleSpecifier,
                                         object::SectionedAddress ModuleOffset);
  template <typename T>
  Expected<std::vector<DILocal>>
  symbolizeFrameCommon(const T &ModuleSpecifier,
                       object::SectionedAddress ModuleOffset);
  template <typename T>
  Expected<std::vector<DILineInfo>>
  findSymbolCommon(const T &ModuleSpecifier, StringRef Symbol, uint64_t Offset);

  Expected<SymbolizableModule *> getOrCreateModuleInfo(const ObjectFile &Obj);

  /// Returns a SymbolizableModule or an error if loading debug info failed.
  /// Unlike the above, errors are reported each time, since they are more
  /// likely to be transient.
  Expected<SymbolizableModule *>
  getOrCreateModuleInfo(ArrayRef<uint8_t> BuildID);

  Expected<SymbolizableModule *>
  createModuleInfo(const ObjectFile *Obj, std::unique_ptr<DIContext> Context,
                   StringRef ModuleName);

  ObjectFile *lookUpDsymFile(const std::string &Path,
                             const MachOObjectFile *ExeObj,
                             const std::string &ArchName);
  ObjectFile *lookUpDebuglinkObject(const std::string &Path,
                                    const ObjectFile *Obj,
                                    const std::string &ArchName);
  ObjectFile *lookUpBuildIDObject(const std::string &Path,
                                  const ELFObjectFileBase *Obj,
                                  const std::string &ArchName);
  std::string lookUpGsymFile(const std::string &Path);

  bool findDebugBinary(const std::string &OrigPath,
                       const std::string &DebuglinkName, uint32_t CRCHash,
                       std::string &Result);

  bool getOrFindDebugBinary(const ArrayRef<uint8_t> BuildID,
                            std::string &Result);

  /// Returns pair of pointers to object and debug object.
  Expected<ObjectPair> getOrCreateObjectPair(const std::string &Path,
                                             const std::string &ArchName);

  /// Return a pointer to the object file with the specified name, for a
  /// specified architecture (e.g. if path refers to a Mach-O universal
  /// binary, only one object file from it will be returned).
  Expected<ObjectFile *> getOrCreateObject(const std::string &InputPath,
                                           const std::string &DefaultArchName);

  /// Return a pointer to the object file with the specified name, for a
  /// specified architecture that is present inside an archive file.
  Expected<ObjectFile *> getOrCreateObjectFromArchive(StringRef ArchivePath,
                                                      StringRef MemberName,
                                                      StringRef ArchName,
                                                      StringRef FullPath);

  /// Update the LRU cache order when a binary is accessed.
  void recordAccess(CachedBinary &Bin);

  std::map<std::string, std::unique_ptr<SymbolizableModule>, std::less<>>
      Modules;
  StringMap<std::string> BuildIDPaths;

  /// Contains cached results of getOrCreateObjectPair().
  std::map<std::pair<std::string, std::string>, ObjectPair>
      ObjectPairForPathArch;

  /// Contains parsed binary for each path, or parsing error.
  std::map<std::string, CachedBinary, std::less<>> BinaryForPath;

  /// Store the archive path for the object file.
  DenseMap<const object::ObjectFile *, std::string> ObjectToArchivePath;

  /// A list of cached binaries in LRU order.
  simple_ilist<CachedBinary> LRUBinaries;
  /// Sum of the sizes of the cached binaries.
  size_t CacheSize = 0;

  struct ContainerCacheKey {
    std::string Path;
    std::string MemberName;
    std::string ArchName;

    // Required for map comparison.
    bool operator<(const ContainerCacheKey &Other) const {
      return std::tie(Path, MemberName, ArchName) <
             std::tie(Other.Path, Other.MemberName, Other.ArchName);
    }
  };

  /// Parsed object file for each path/member/architecture triple.
  /// Used to cache objects extracted from containers (e.g., Mach-O
  /// universal binaries, archives).
  std::map<ContainerCacheKey, std::unique_ptr<ObjectFile>> ObjectFileCache;

  Expected<object::Binary *>
  loadOrGetBinary(const std::string &ArchivePathKey,
                  std::optional<StringRef> FullPathKey = std::nullopt);

  Expected<ObjectFile *> findOrCacheObject(
      const ContainerCacheKey &Key,
      llvm::function_ref<Expected<std::unique_ptr<ObjectFile>>()> Loader,
      const std::string &PathForBinaryCache);

  Options Opts;

  std::unique_ptr<BuildIDFetcher> BIDFetcher;
};

/// Binary held in an intrusive LRU cache, or an empty error placeholder.
///
/// If the binary is empty, then the entry marks that an error occurred, and it
/// is not part of the LRU list.
class CachedBinary : public ilist_node<CachedBinary> {
public:
  /// Construct an empty cache entry that marks a prior load error.
  CachedBinary() = default;
  /// Construct a cache entry that owns \p Bin.
  /// @param Bin Owned binary stored in this cache entry.
  CachedBinary(OwningBinary<Binary> Bin) : Bin(std::move(Bin)) {}

  /// Access the owned binary wrapper.
  /// @return Reference to the owned binary wrapper.
  OwningBinary<Binary> &operator*() { return Bin; }
  /// Access the owned binary wrapper through a pointer.
  /// @return Pointer to the owned binary wrapper.
  OwningBinary<Binary> *operator->() { return &Bin; }

  /// Register an eviction callback to run before existing ones.
  /// @param Evictor Action invoked when this entry is evicted.
  LLVM_ABI void pushEvictor(std::function<void()> Evictor);

  /// Run registered eviction callbacks in reverse registration order.
  void evict() {
    if (Evictor)
      Evictor();
  }

  /// Return the size in bytes of the cached binary's data.
  /// @return Size in bytes of the cached binary's data.
  size_t size() { return Bin.getBinary()->getData().size(); }

private:
  OwningBinary<Binary> Bin;
  std::function<void()> Evictor;
};

} // end namespace symbolize
} // end namespace llvm

#endif // LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZE_H
