//===- DWARFLinkerBase.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFLINKER_DWARFLINKERBASE_H
#define LLVM_DWARFLINKER_DWARFLINKERBASE_H
#include "AddressesMap.h"
#include "DWARFFile.h"
#include "llvm/ADT/AddressRanges.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugRangeList.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/LowLevel/DWARFExpression.h"
#include "llvm/Support/Compiler.h"
#include <map>
namespace llvm {
class DWARFUnit;
class ThreadPoolInterface;

namespace dwarf_linker {

/// List of tracked debug tables.
enum class DebugSectionKind : uint8_t {
  DebugInfo = 0,      ///< The \c .debug_info section.
  DebugLine,          ///< The \c .debug_line section.
  DebugFrame,         ///< The \c .debug_frame section.
  DebugRange,         ///< The \c .debug_ranges section.
  DebugRngLists,      ///< The \c .debug_rnglists section.
  DebugLoc,           ///< The \c .debug_loc section.
  DebugLocLists,      ///< The \c .debug_loclists section.
  DebugARanges,       ///< The \c .debug_aranges section.
  DebugAbbrev,        ///< The \c .debug_abbrev section.
  DebugMacinfo,       ///< The \c .debug_macinfo section.
  DebugMacro,         ///< The \c .debug_macro section.
  DebugAddr,          ///< The \c .debug_addr section.
  DebugStr,           ///< The \c .debug_str section.
  DebugLineStr,       ///< The \c .debug_line_str section.
  DebugStrOffsets,    ///< The \c .debug_str_offsets section.
  DebugPubNames,      ///< The \c .debug_pubnames section.
  DebugPubTypes,      ///< The \c .debug_pubtypes section.
  DebugNames,         ///< The \c .debug_names section.
  AppleNames,         ///< The \c .apple_names section.
  AppleNamespaces,    ///< The \c .apple_namespaces section.
  AppleObjC,          ///< The \c .apple_objc section.
  AppleTypes,         ///< The \c .apple_types section.
  NumberOfEnumEntries ///< Number of section kinds; must remain last.
};

static constexpr size_t SectionKindsNum =
    static_cast<size_t>(DebugSectionKind::NumberOfEnumEntries);

static constexpr StringLiteral SectionNames[SectionKindsNum] = {
    "debug_info",     "debug_line",     "debug_frame",       "debug_ranges",
    "debug_rnglists", "debug_loc",      "debug_loclists",    "debug_aranges",
    "debug_abbrev",   "debug_macinfo",  "debug_macro",       "debug_addr",
    "debug_str",      "debug_line_str", "debug_str_offsets", "debug_pubnames",
    "debug_pubtypes", "debug_names",    "apple_names",       "apple_namespac",
    "apple_objc",     "apple_types"};

/// Return the name of the section.
static constexpr const StringLiteral &
getSectionName(DebugSectionKind SectionKind) {
  return SectionNames[static_cast<uint8_t>(SectionKind)];
}

/// Recognise the table name and match it with the DebugSectionKind.
///
/// \param Name Debug table or section name to parse.
/// \return The matching DebugSectionKind, or \c std::nullopt if unrecognized.
LLVM_ABI std::optional<DebugSectionKind> parseDebugTableName(StringRef Name);

/// The base interface for DWARFLinker implementations.
class DWARFLinkerBase {
public:
  /// Destroy the DWARF linker.
  virtual ~DWARFLinkerBase() = default;
  /// Callback type for warning and error messages.
  using MessageHandlerTy = std::function<void(
      const Twine &Warning, StringRef Context, const DWARFDie *DIE)>;
  /// Callback that loads a DWARF object file by container and path.
  using ObjFileLoaderTy = std::function<ErrorOr<DWARFFile &>(
      StringRef ContainerName, StringRef Path)>;
  /// Callback invoked when input DWARF verification produces output.
  using InputVerificationHandlerTy =
      std::function<void(const DWARFFile &File, llvm::StringRef Output)>;
  /// Map from object-file path prefixes to remapped prefixes.
  using ObjectPrefixMapTy = std::map<std::string, std::string>;
  /// Callback invoked for each compile unit.
  using CompileUnitHandlerTy = function_ref<void(const DWARFUnit &Unit)>;
  /// Map from module names to Swift interface file paths.
  using SwiftInterfacesMapTy = std::map<std::string, std::string>;
  /// Type of output file.
  enum class OutputFileType : uint8_t {
    Object,   ///< Emit an object file.
    Assembly, ///< Emit an assembly file.
  };
  /// The kind of accelerator tables to be emitted.
  enum class AccelTableKind : uint8_t {
    Apple,     ///< .apple_names, .apple_namespaces, .apple_types, .apple_objc.
    Pub,       ///< .debug_pubnames, .debug_pubtypes
    DebugNames ///< .debug_names.
  };
  /// Add an object file to be linked.
  ///
  /// Pre-load compile unit die. Call \p OnCUDieLoaded for each compile unit
  /// die. If \p File has reference to a Clang module and
  /// UpdateIndexTablesOnly == false then the module is be pre-loaded by
  /// \p Loader.
  ///
  /// \pre a call to setNoODR(true) and/or setUpdateIndexTablesOnly(bool Update)
  ///      must be made when required.
  /// \param File Object file whose DWARF should be linked.
  /// \param Loader Optional loader used to resolve Clang module references.
  /// \param OnCUDieLoaded Callback invoked for each pre-loaded compile unit
  ///        DIE.
  virtual void addObjectFile(
      DWARFFile &File, ObjFileLoaderTy Loader = nullptr,
      CompileUnitHandlerTy OnCUDieLoaded = [](const DWARFUnit &) {}) = 0;
  /// Link the debug info for all object files added through calls to
  /// addObjectFile.
  ///
  /// \return Success, or an Error describing why linking failed.
  virtual Error link() = 0;
  /// A number of methods setting various linking options:

  /// Enable logging to standard output.
  ///
  /// \param Verbose If true, enable verbose logging to standard output.
  virtual void setVerbosity(bool Verbose) = 0;
  /// Print statistics to standard output.
  ///
  /// \param Statistics If true, print linking statistics to standard output.
  virtual void setStatistics(bool Statistics) = 0;
  /// Verify the input DWARF.
  ///
  /// \param Verify If true, verify the input DWARF before linking.
  virtual void setVerifyInputDWARF(bool Verify) = 0;
  /// Do not unique types according to ODR.
  ///
  /// \param NoODR If true, skip ODR-based type uniquing.
  virtual void setNoODR(bool NoODR) = 0;
  /// Update index tables only (do not modify rest of DWARF).
  ///
  /// \param Update If true, update index tables only.
  virtual void setUpdateIndexTablesOnly(bool Update) = 0;
  /// Set whether to keep the enclosing function for a static variable.
  ///
  /// \param KeepFunctionForStatic If true, keep the enclosing function for a
  ///        static variable.
  virtual void setKeepFunctionForStatic(bool KeepFunctionForStatic) = 0;
  /// Use specified number of threads for parallel files linking.
  ///
  /// \param NumThreads Number of threads to use for parallel linking.
  virtual void setNumThreads(unsigned NumThreads) = 0;
  /// Add kind of accelerator tables to be generated.
  ///
  /// \param Kind Accelerator table kind to generate.
  virtual void addAccelTableKind(AccelTableKind Kind) = 0;
  /// Set prepend path for clang modules.
  ///
  /// \param Ppath Path prepended when resolving Clang module references.
  virtual void setPrependPath(StringRef Ppath) = 0;
  /// Set estimated objects files amount, for preliminary data allocation.
  ///
  /// \param ObjFilesNum Estimated number of input object files.
  virtual void setEstimatedObjfilesAmount(unsigned ObjFilesNum) = 0;
  /// Set verification handler used to report verification errors.
  ///
  /// \param Handler Callback used to report input verification errors.
  virtual void
  setInputVerificationHandler(InputVerificationHandlerTy Handler) = 0;
  /// Set map for Swift interfaces.
  ///
  /// \param Map Map from module names to Swift interface paths.
  virtual void setSwiftInterfacesMap(SwiftInterfacesMapTy *Map) = 0;
  /// Set prefix map for objects.
  ///
  /// \param Map Map from object-file path prefixes to remapped prefixes.
  virtual void setObjectPrefixMap(ObjectPrefixMapTy *Map) = 0;
  /// Set target DWARF version.
  ///
  /// \param TargetDWARFVersion DWARF version to emit in the output.
  /// \return Success, or an Error if \p TargetDWARFVersion is unsupported.
  virtual Error setTargetDWARFVersion(uint16_t TargetDWARFVersion) = 0;
  /// Set the thread pool used to link the object files.
  ///
  /// \param Pool Thread pool used to link object files in parallel.
  virtual void setThreadPool(ThreadPoolInterface *Pool) = 0;
};
} // end namespace dwarf_linker
} // end namespace llvm
#endif // LLVM_DWARFLINKER_DWARFLINKERBASE_H
