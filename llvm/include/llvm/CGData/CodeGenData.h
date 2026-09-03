//===- CodeGenData.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for codegen data that has stable summary which
// can be used to optimize the code in the subsequent codegen.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CGDATA_CODEGENDATA_H
#define LLVM_CGDATA_CODEGENDATA_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StableHashing.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/CGData/OutlinedHashTree.h"
#include "llvm/CGData/OutlinedHashTreeRecord.h"
#include "llvm/CGData/StableFunctionMapRecord.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Triple.h"
#include <mutex>

namespace llvm {

/// Object-file section kinds that store codegen data.
///
/// Enumerators are defined in CodeGenData.inc: \c CG_outline for the outlined
/// hash tree section and \c CG_merge for the stable function merging map
/// section.
enum CGDataSectKind {
#define CG_DATA_SECT_ENTRY(Kind, SectNameCommon, SectNameCoff, Prefix) Kind,
#include "llvm/CGData/CodeGenData.inc"
};

/// Return the object-file section name for codegen data kind \p CGSK.
///
/// \param CGSK Section kind to name.
/// \param OF Object file format (ELF, Mach-O, COFF, ...).
/// \param AddSegmentInfo When true, include Mach-O segment prefix information.
/// \return The section name string for \p CGSK in format \p OF.
LLVM_ABI std::string getCodeGenDataSectionName(CGDataSectKind CGSK,
                                               Triple::ObjectFormatType OF,
                                               bool AddSegmentInfo = true);

/// Bitmask identifying which kinds of codegen data are present.
enum class CGDataKind {
  Unknown = 0x0, ///< No recognized codegen data kind.
  /// Function outlining information (outlined hash tree).
  FunctionOutlinedHashTree = 0x1,
  /// Function merging information (stable function map).
  StableFunctionMergingMap = 0x2,
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/StableFunctionMergingMap)
};

/// Return the \c std::error_category for codegen data errors.
///
/// \return The error category used for codegen data \c std::error_code values.
LLVM_ABI const std::error_category &cgdata_category();

/// Error codes for codegen data reading and validation.
enum class cgdata_error {
  success = 0,         ///< No error.
  eof,                 ///< Unexpected end of file while reading.
  bad_magic,           ///< Magic signature did not match.
  bad_header,          ///< Header was invalid.
  empty_cgdata,        ///< Codegen data was empty.
  malformed,           ///< Data was malformed.
  unsupported_version, ///< Format version is not supported.
};

/// Convert \p E into a \c std::error_code in the cgdata category.
///
/// \param E Codegen-data error enumerator to convert.
/// \return A \c std::error_code corresponding to \p E.
inline std::error_code make_error_code(cgdata_error E) {
  return std::error_code(static_cast<int>(E), cgdata_category());
}

/// Typed error carrying a \c cgdata_error code and optional message.
class LLVM_ABI CGDataError : public ErrorInfo<CGDataError> {
public:
  /// Construct a CGDataError from \p Err and optional message \p ErrStr.
  ///
  /// \param Err Error code; must not be \c cgdata_error::success.
  /// \param ErrStr Optional detail message.
  CGDataError(cgdata_error Err, const Twine &ErrStr = Twine())
      : Err(Err), Msg(ErrStr.str()) {
    assert(Err != cgdata_error::success && "Not an error");
  }

  /// Return the error message as a string.
  ///
  /// \return The formatted error message string.
  std::string message() const override;

  /// Write the error message to \p OS.
  ///
  /// \param OS Stream to receive the error message.
  void log(raw_ostream &OS) const override { OS << message(); }

  /// Convert this error to a \c std::error_code.
  ///
  /// \return A \c std::error_code in the cgdata category for this error.
  std::error_code convertToErrorCode() const override {
    return make_error_code(Err);
  }

  /// Return the \c cgdata_error enumerator for this error.
  ///
  /// \return The \c cgdata_error code stored in this error.
  cgdata_error get() const { return Err; }
  /// Return the optional detail message for this error.
  ///
  /// \return The optional detail message string.
  const std::string &getMessage() const { return Msg; }

  /// Consume a CGDataError from \p E and return its code and message.
  ///
  /// The Error must either be a success value, or contain a single
  /// CGDataError.
  ///
  /// \param E Error to consume; must be success or a single CGDataError.
  /// \return A pair of the \c cgdata_error code and optional detail message.
  static std::pair<cgdata_error, std::string> take(Error E) {
    auto Err = cgdata_error::success;
    std::string Msg;
    handleAllErrors(std::move(E), [&Err, &Msg](const CGDataError &IPE) {
      assert(Err == cgdata_error::success && "Multiple errors encountered");
      Err = IPE.get();
      Msg = IPE.getMessage();
    });
    return {Err, Msg};
  }

  /// RTTI identifier used by ErrorInfo::classID.
  static char ID;

private:
  cgdata_error Err;
  std::string Msg;
};

/// Whether codegen data is unused, being read, or being written.
enum CGDataMode {
  None,  ///< No codegen data mode is active.
  Read,  ///< Codegen data is being read or consumed.
  Write, ///< Codegen data is being generated or written.
};

/// Process-wide singleton holding shared codegen data used across modules.
class CodeGenData {
  /// Global outlined hash tree that has oulined hash sequences across modules.
  std::unique_ptr<OutlinedHashTree> PublishedHashTree;
  /// Global stable function map that has stable function info across modules.
  std::unique_ptr<StableFunctionMap> PublishedStableFunctionMap;

  /// This flag is set when -fcodegen-data-generate is passed.
  /// Or, it can be mutated with -fcodegen-data-thinlto-two-rounds.
  bool EmitCGData;

  /// This is a singleton instance which is thread-safe. Unlike profile data
  /// which is largely function-based, codegen data describes the whole module.
  /// Therefore, this can be initialized once, and can be used across modules
  /// instead of constructing the same one for each codegen backend.
  static std::unique_ptr<CodeGenData> Instance;
  static std::once_flag OnceFlag;

  CodeGenData() = default;

public:
  /// Destroy the codegen data instance.
  ~CodeGenData() = default;

  /// Return the process-wide CodeGenData singleton.
  ///
  /// \return Reference to the process-wide CodeGenData singleton.
  LLVM_ABI static CodeGenData &getInstance();

  /// Returns true if we have a valid outlined hash tree.
  ///
  /// \return True if a non-empty outlined hash tree has been published.
  bool hasOutlinedHashTree() {
    return PublishedHashTree && !PublishedHashTree->empty();
  }
  /// Return true if a non-empty stable function map has been published.
  ///
  /// \return True if a non-empty stable function map has been published.
  bool hasStableFunctionMap() {
    return PublishedStableFunctionMap && !PublishedStableFunctionMap->empty();
  }

  /// Returns the outlined hash tree. This can be globally used in a read-only
  /// manner.
  ///
  /// \return Pointer to the published outlined hash tree, or null if none.
  const OutlinedHashTree *getOutlinedHashTree() {
    return PublishedHashTree.get();
  }
  /// Return the published stable function map for read-only use.
  ///
  /// \return Pointer to the published stable function map, or null if none.
  const StableFunctionMap *getStableFunctionMap() {
    return PublishedStableFunctionMap.get();
  }

  /// Returns true if we should write codegen data.
  ///
  /// \return True if codegen data should be emitted.
  bool emitCGData() { return EmitCGData; }

  /// Publish the (globally) merged or read outlined hash tree.
  ///
  /// \param HashTree Outlined hash tree to publish; takes ownership.
  void publishOutlinedHashTree(std::unique_ptr<OutlinedHashTree> HashTree) {
    PublishedHashTree = std::move(HashTree);
    // Ensure we disable emitCGData as we do not want to read and write both.
    EmitCGData = false;
  }
  /// Publish the (globally) merged or read stable function map.
  ///
  /// \param FunctionMap Stable function map to publish; takes ownership.
  void
  publishStableFunctionMap(std::unique_ptr<StableFunctionMap> FunctionMap) {
    PublishedStableFunctionMap = std::move(FunctionMap);
    // Ensure we disable emitCGData as we do not want to read and write both.
    EmitCGData = false;
  }
};

/// Convenience wrappers around the CodeGenData singleton.
namespace cgdata {

/// Return true if a valid outlined hash tree has been published.
///
/// \return True if a non-empty outlined hash tree is available.
inline bool hasOutlinedHashTree() {
  return CodeGenData::getInstance().hasOutlinedHashTree();
}

/// Return true if a non-empty stable function map has been published.
///
/// \return True if a non-empty stable function map is available.
inline bool hasStableFunctionMap() {
  return CodeGenData::getInstance().hasStableFunctionMap();
}

/// Return the published outlined hash tree for read-only use.
///
/// \return Pointer to the published outlined hash tree, or null if none.
inline const OutlinedHashTree *getOutlinedHashTree() {
  return CodeGenData::getInstance().getOutlinedHashTree();
}

/// Return the published stable function map for read-only use.
///
/// \return Pointer to the published stable function map, or null if none.
inline const StableFunctionMap *getStableFunctionMap() {
  return CodeGenData::getInstance().getStableFunctionMap();
}

/// Return true if codegen data should be written.
///
/// \return True if codegen data should be emitted.
inline bool emitCGData() { return CodeGenData::getInstance().emitCGData(); }

/// Publish the (globally) merged or read outlined hash tree.
///
/// \param HashTree Outlined hash tree to publish; takes ownership.
inline void
publishOutlinedHashTree(std::unique_ptr<OutlinedHashTree> HashTree) {
  CodeGenData::getInstance().publishOutlinedHashTree(std::move(HashTree));
}

/// Publish the (globally) merged or read stable function map.
///
/// \param FunctionMap Stable function map to publish; takes ownership.
inline void
publishStableFunctionMap(std::unique_ptr<StableFunctionMap> FunctionMap) {
  CodeGenData::getInstance().publishStableFunctionMap(std::move(FunctionMap));
}

/// Per-task output streams and optional file cache for two-round codegen.
struct StreamCacheData {
  /// Backing buffer for serialized data stream.
  SmallVector<SmallString<0>> Outputs;
  /// Callback function to add serialized data to the stream.
  AddStreamFn AddStream;
  /// Backing buffer for cached data.
  SmallVector<std::unique_ptr<MemoryBuffer>> Files;
  /// Cache mechanism for storing data.
  FileCache Cache;

  /// Construct stream and optional file-cache buffers for \p Size partitions.
  ///
  /// \param Size Number of parallel tasks/partitions to allocate for.
  /// \param OrigCache Original file cache to derive a local codegen-data cache
  /// from.
  /// \param CachePrefix Prefix used when creating the local cache.
  StreamCacheData(unsigned Size, const FileCache &OrigCache,
                  const Twine &CachePrefix)
      : Outputs(Size), Files(Size) {
    AddStream = [&](size_t Task, const Twine &ModuleName) {
      return std::make_unique<CachedFileStream>(
          std::make_unique<raw_svector_ostream>(Outputs[Task]));
    };

    if (OrigCache.isValid()) {
      auto CGCacheOrErr =
          localCache("ThinLTO", CachePrefix, OrigCache.getCacheDirectoryPath(),
                     [&](size_t Task, const Twine &ModuleName,
                         std::unique_ptr<MemoryBuffer> MB) {
                       Files[Task] = std::move(MB);
                     });
      if (Error Err = CGCacheOrErr.takeError())
        report_fatal_error(std::move(Err));
      Cache = std::move(*CGCacheOrErr);
    }
  }
  /// Deleted; StreamCacheData requires a size and optional file cache.
  StreamCacheData() = delete;

  /// Retrieve results from either the cache or the stream.
  ///
  /// \return A vector of string refs for each partition, preferring cached
  /// file buffers when present and falling back to stream outputs otherwise.
  std::unique_ptr<SmallVector<StringRef>> getResult() {
    unsigned NumOutputs = Outputs.size();
    auto Result = std::make_unique<SmallVector<StringRef>>(NumOutputs);
    for (unsigned I = 0; I < NumOutputs; ++I)
      if (Files[I])
        (*Result)[I] = Files[I]->getBuffer();
      else
        (*Result)[I] = Outputs[I];
    return Result;
  }
};

/// Save the module before the first codegen round.
///
/// \param TheModule Module to serialize before the first round.
/// \param Task Partition number in the parallel code generation process.
/// \param AddStream Callback used to add the serialized module to the stream.
LLVM_ABI void saveModuleForTwoRounds(const Module &TheModule, unsigned Task,
                                     AddStreamFn AddStream);

/// Load the optimized bitcode module for the second codegen round.
///
/// \param OrigModule Original bitcode module.
/// \param Task Partition number in the parallel code generation process.
/// \param Context Environment settings for module operations.
/// \param IRFiles Optimized bitcode module files needed for loading.
/// \return A unique_ptr to the loaded Module, or nullptr if loading fails.
LLVM_ABI std::unique_ptr<Module>
loadModuleForTwoRounds(BitcodeModule &OrigModule, unsigned Task,
                       LLVMContext &Context, ArrayRef<StringRef> IRFiles);

/// Merge the codegen data from the scratch objects from the first codegen
/// round.
///
/// \param ObjectFiles Scratch object-file buffers produced by the first round.
/// \return The combined hash of the merged codegen data.
LLVM_ABI Expected<stable_hash>
mergeCodeGenData(ArrayRef<StringRef> ObjectFiles);

/// Report a codegen-data warning derived from error \p E.
///
/// \param E Error to format into a warning.
/// \param Whence Optional context describing where the warning originated.
LLVM_ABI void warn(Error E, StringRef Whence = "");
/// Report a codegen-data warning with message \p Message.
///
/// \param Message Warning text to report.
/// \param Whence Optional context describing where the warning originated.
/// \param Hint Optional hint appended to the warning.
LLVM_ABI void warn(Twine Message, StringRef Whence = "", StringRef Hint = "");

} // end namespace cgdata

/// Binary indexed codegen data format constants and header layout.
namespace IndexedCGData {

/// Magic signature for validation ("\xffcgdata\x81" in little-endian order).
const uint64_t Magic = 0x81617461646763ff;

/// Version numbers for the indexed codegen data format.
enum CGDataVersion {
  /// First version; supports the outlined hash tree.
  Version1 = 1,
  /// Adds support for the stable function merging map.
  Version2 = 2,
  /// Adds the total size of Names in the stable function map so they can be
  /// skipped in non-assertion builds.
  Version3 = 3,
  /// Adjusts the stable function merging map layout for lazy loading.
  Version4 = 4,
  /// Latest supported indexed codegen data format version.
  CurrentVersion = CG_DATA_INDEX_VERSION
};
/// Current indexed codegen data format version.
const uint64_t Version = CGDataVersion::CurrentVersion;

/// Header for the indexed (binary) codegen data format.
struct Header {
  /// Magic signature identifying the cgdata format.
  uint64_t Magic;
  /// Format version number.
  uint32_t Version;
  /// Bitmask of \c CGDataKind values present in the file.
  uint32_t DataKind;
  /// File offset of the outlined hash tree payload.
  uint64_t OutlinedHashTreeOffset;
  /// File offset of the stable function map payload.
  uint64_t StableFunctionMapOffset;

  // New fields should only be added at the end to ensure that the size
  // computation is correct. The methods below need to be updated to ensure that
  // the new field is read correctly.

  /// Read a header struct from the buffer starting at \p Curr.
  ///
  /// \param Curr Pointer to the start of the header in a binary buffer.
  /// \return The parsed header, or an error if the buffer is invalid.
  LLVM_ABI static Expected<Header> readFromBuffer(const unsigned char *Curr);
};

} // end namespace IndexedCGData

} // end namespace llvm

#endif // LLVM_CODEGEN_PREPARE_H
