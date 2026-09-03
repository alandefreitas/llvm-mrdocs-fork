//===- InputFile.h -------------------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_INPUTFILE_H
#define LLVM_DEBUGINFO_PDB_NATIVE_INPUTFILE_H

#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/iterator.h"
#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/StringsAndChecksums.h"
#include "llvm/DebugInfo/PDB/Native/LinePrinter.h"
#include "llvm/DebugInfo/PDB/Native/ModuleDebugStream.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
class LazyRandomTypeCollection;
}
namespace object {
class COFFObjectFile;
} // namespace object

namespace pdb {
class InputFile;
class LinePrinter;
class PDBFile;
class NativeSession;
class SymbolGroupIterator;
class SymbolGroup;

/// Unified view of a PDB, COFF object, or unrecognized binary for dumping.
///
/// Tools that accept either a PDB or an object file with CodeView debug info
/// use this wrapper so callers can query types, IDs, and symbol groups without
/// branching on the underlying file kind at every call site.
class InputFile {
  InputFile();

  std::unique_ptr<NativeSession> PdbSession;
  object::OwningBinary<object::Binary> CoffObject;
  std::unique_ptr<MemoryBuffer> UnknownFile;
  PointerUnion<PDBFile *, object::COFFObjectFile *, MemoryBuffer *> PdbOrObj;

  using TypeCollectionPtr = std::unique_ptr<codeview::LazyRandomTypeCollection>;

  TypeCollectionPtr Types;
  TypeCollectionPtr Ids;

  enum TypeCollectionKind { kTypes, kIds };
  codeview::LazyRandomTypeCollection &
  getOrCreateTypeCollection(TypeCollectionKind Kind);

public:
  /// Construct an input file wrapping an already-opened PDB.
  ///
  /// \param Pdb Non-owning pointer to the PDB file to wrap.
  LLVM_ABI InputFile(PDBFile *Pdb);
  /// Construct an input file wrapping an already-opened COFF object.
  ///
  /// \param Obj Non-owning pointer to the COFF object file to wrap.
  LLVM_ABI InputFile(object::COFFObjectFile *Obj);
  /// Construct an input file wrapping an unrecognized memory buffer.
  ///
  /// \param Buffer Non-owning pointer to the raw file buffer to wrap.
  LLVM_ABI InputFile(MemoryBuffer *Buffer);
  /// Destroy the input file and release owned session or buffer state.
  LLVM_ABI ~InputFile();
  /// Move-construct an input file, transferring ownership of session or buffer
  /// state.
  ///
  /// \param Other The input file to move from.
  InputFile(InputFile &&Other) = default;

  /// Open \p Path as a PDB, COFF object, or optionally an unknown file.
  ///
  /// \param Path Filesystem path of the file to open.
  /// \param AllowUnknownFile If true, accept unrecognized file types as a raw
  ///     memory buffer; otherwise return an error for unsupported types.
  ///
  /// \returns An InputFile on success, or an Error if the path cannot be
  ///     opened or identified.
  LLVM_ABI static Expected<InputFile> open(StringRef Path,
                                           bool AllowUnknownFile = false);

  /// Return the underlying PDB file.
  ///
  /// \returns A mutable reference to the wrapped PDBFile.
  ///
  /// Preconditions: \c isPdb() is true.
  LLVM_ABI PDBFile &pdb();
  /// Return the underlying PDB file.
  ///
  /// \returns A const reference to the wrapped PDBFile.
  ///
  /// Preconditions: \c isPdb() is true.
  LLVM_ABI const PDBFile &pdb() const;
  /// Return the underlying COFF object file.
  ///
  /// \returns A mutable reference to the wrapped COFFObjectFile.
  ///
  /// Preconditions: \c isObj() is true.
  LLVM_ABI object::COFFObjectFile &obj();
  /// Return the underlying COFF object file.
  ///
  /// \returns A const reference to the wrapped COFFObjectFile.
  ///
  /// Preconditions: \c isObj() is true.
  LLVM_ABI const object::COFFObjectFile &obj() const;
  /// Return the underlying unrecognized file buffer.
  ///
  /// \returns A mutable reference to the wrapped MemoryBuffer.
  ///
  /// Preconditions: \c isUnknown() is true.
  LLVM_ABI MemoryBuffer &unknown();
  /// Return the underlying unrecognized file buffer.
  ///
  /// \returns A const reference to the wrapped MemoryBuffer.
  ///
  /// Preconditions: \c isUnknown() is true.
  LLVM_ABI const MemoryBuffer &unknown() const;

  /// Return the path or buffer identifier of the wrapped file.
  ///
  /// \returns The PDB path, object file name, or memory-buffer identifier.
  LLVM_ABI StringRef getFilePath() const;

  /// Return whether this file contains a CodeView type stream.
  ///
  /// \returns True if a TPI stream (PDB) or \c .debug$T / \c .debug$P section
  ///     (object) is present.
  LLVM_ABI bool hasTypes() const;
  /// Return whether this file contains a separate CodeView ID stream.
  ///
  /// \returns True only for PDBs that have an IPI stream; always false for
  ///     object files.
  LLVM_ABI bool hasIds() const;

  /// Return the lazily constructed type collection for this file.
  ///
  /// \returns A mutable reference to the TPI (or object \c .debug$T) type
  ///     collection, creating it on first use.
  LLVM_ABI codeview::LazyRandomTypeCollection &types();
  /// Return the lazily constructed ID collection for this file.
  ///
  /// For object files and PDBs without an IPI stream, IDs live in the type
  /// stream, so this returns the same collection as \c types().
  ///
  /// \returns A mutable reference to the IPI (or shared type) collection.
  LLVM_ABI codeview::LazyRandomTypeCollection &ids();

  /// Return an iterator range over all symbol groups in this file.
  ///
  /// \returns A range from \c symbol_groups_begin() to \c symbol_groups_end().
  LLVM_ABI iterator_range<SymbolGroupIterator> symbol_groups();
  /// Return an iterator to the first symbol group.
  ///
  /// \returns A SymbolGroupIterator positioned at the first module or
  ///     \c .debug$S section.
  LLVM_ABI SymbolGroupIterator symbol_groups_begin();
  /// Return an end iterator for symbol groups.
  ///
  /// \returns A default-constructed SymbolGroupIterator past the last group.
  LLVM_ABI SymbolGroupIterator symbol_groups_end();

  /// Return whether this input wraps a PDB file.
  ///
  /// \returns True if the underlying storage is a PDBFile.
  LLVM_ABI bool isPdb() const;
  /// Return whether this input wraps a COFF object file.
  ///
  /// \returns True if the underlying storage is a COFFObjectFile.
  LLVM_ABI bool isObj() const;
  /// Return whether this input wraps an unrecognized file buffer.
  ///
  /// \returns True if the underlying storage is a MemoryBuffer.
  LLVM_ABI bool isUnknown() const;
};

/// One module (PDB) or \c .debug$S section (object) with its debug subsections.
///
/// Provides string-table and checksum lookups and formatting helpers used when
/// dumping module-scoped CodeView debug information.
class SymbolGroup {
  friend class SymbolGroupIterator;

public:
  /// Construct a symbol group for module or section index \p GroupIndex.
  ///
  /// For a PDB, loads that module's debug stream and checksums. For an object
  /// file, selects the corresponding \c .debug$S subsection array.
  ///
  /// \param File The input file owning this group; may be null.
  /// \param GroupIndex Zero-based module index (PDB) or \c .debug$S section
  ///     ordinal (object). Defaults to 0.
  LLVM_ABI explicit SymbolGroup(InputFile *File, uint32_t GroupIndex = 0);

  /// Look up a string in this group's string table at \p Offset.
  ///
  /// \param Offset Byte offset into the PDB or subsection string table.
  ///
  /// \returns The string at that offset, or an Error on failure.
  LLVM_ABI Expected<StringRef> getNameFromStringTable(uint32_t Offset) const;
  /// Resolve a file name from a checksums-subsection offset.
  ///
  /// \param Offset Offset of a file-checksum entry within the checksums
  ///     subsection.
  ///
  /// \returns The file name from the string table, or an empty StringRef if
  ///     checksums are missing or the offset is invalid.
  LLVM_ABI Expected<StringRef> getNameFromChecksums(uint32_t Offset) const;

  /// Print \p File with its checksum (if known) via \p Printer.
  ///
  /// \param Printer Line printer that receives the formatted output.
  /// \param File Source file name to look up in the checksum map.
  /// \param Append If true, write on the current line; otherwise start a new
  ///     line. Defaults to false.
  LLVM_ABI void formatFromFileName(LinePrinter &Printer, StringRef File,
                                   bool Append = false) const;

  /// Print the file name and checksum for checksums entry \p Offset.
  ///
  /// \param Printer Line printer that receives the formatted output.
  /// \param Offset Offset of a file-checksum entry within the checksums
  ///     subsection.
  /// \param Append If true, write on the current line; otherwise start a new
  ///     line. Defaults to false.
  LLVM_ABI void formatFromChecksumsOffset(LinePrinter &Printer, uint32_t Offset,
                                          bool Append = false) const;

  /// Return the display name of this symbol group.
  ///
  /// \returns The PDB module name, or \c ".debug$S" for object files.
  LLVM_ABI StringRef name() const;

  /// Return the CodeView debug subsections for this group.
  ///
  /// \returns The debug subsection array loaded for this module or section.
  codeview::DebugSubsectionArray getDebugSubsections() const {
    return Subsections;
  }
  /// Return the PDB module debug stream for this group.
  ///
  /// \returns A const reference to the loaded ModuleDebugStreamRef.
  ///
  /// Preconditions: The input is a PDB and \c hasDebugStream() is true.
  LLVM_ABI const ModuleDebugStreamRef &getPdbModuleStream() const;

  /// Return the input file that owns this symbol group.
  ///
  /// \returns A const reference to the parent InputFile.
  const InputFile &getFile() const { return *File; }
  /// Return the input file that owns this symbol group.
  ///
  /// \returns A mutable reference to the parent InputFile.
  InputFile &getFile() { return *File; }

  /// Return whether a PDB module debug stream has been loaded.
  ///
  /// \returns True if \c DebugStream is non-null.
  bool hasDebugStream() const { return DebugStream != nullptr; }

private:
  void initializeForPdb(uint32_t Modi);
  void updatePdbModi(uint32_t Modi);
  void updateDebugS(const codeview::DebugSubsectionArray &SS);

  void rebuildChecksumMap();
  InputFile *File = nullptr;
  StringRef Name;
  codeview::DebugSubsectionArray Subsections;
  std::shared_ptr<ModuleDebugStreamRef> DebugStream;
  codeview::StringsAndChecksumsRef SC;
  StringMap<codeview::FileChecksumEntry> ChecksumsByFile;
};

/// Forward iterator over SymbolGroup entries in an InputFile.
class SymbolGroupIterator
    : public iterator_facade_base<SymbolGroupIterator,
                                  std::forward_iterator_tag, SymbolGroup> {
public:
  /// Construct an end iterator with no associated input file.
  LLVM_ABI SymbolGroupIterator();
  /// Construct an iterator at the first symbol group of \p File.
  ///
  /// \param File The input file whose modules or \c .debug$S sections are
  ///     iterated.
  LLVM_ABI explicit SymbolGroupIterator(InputFile &File);
  /// Copy-construct a symbol group iterator.
  ///
  /// \param Other The iterator to copy.
  SymbolGroupIterator(const SymbolGroupIterator &Other) = default;
  /// Copy-assign a symbol group iterator.
  ///
  /// \param R The iterator to assign from.
  ///
  /// \returns A reference to this iterator.
  SymbolGroupIterator &operator=(const SymbolGroupIterator &R) = default;

  /// Return the current symbol group.
  ///
  /// \returns A const reference to the current SymbolGroup.
  ///
  /// Preconditions: The iterator is not at end.
  LLVM_ABI const SymbolGroup &operator*() const;
  /// Return the current symbol group.
  ///
  /// \returns A mutable reference to the current SymbolGroup.
  ///
  /// Preconditions: The iterator is not at end.
  LLVM_ABI SymbolGroup &operator*();

  /// Compare two symbol group iterators for equality.
  ///
  /// End iterators compare equal to each other. Otherwise equality requires
  /// the same input file and group index.
  ///
  /// \param R The iterator to compare against.
  ///
  /// \returns True if both iterators are at the same position.
  LLVM_ABI bool operator==(const SymbolGroupIterator &R) const;
  /// Advance to the next symbol group.
  ///
  /// \returns A reference to this iterator after advancing.
  ///
  /// Preconditions: The iterator is not at end.
  LLVM_ABI SymbolGroupIterator &operator++();

private:
  void scanToNextDebugS();
  bool isEnd() const;

  uint32_t Index = 0;
  std::optional<object::section_iterator> SectionIter;
  SymbolGroup Value;
};

/// Load the module debug stream for DBI module \p Index and set \p ModuleName.
///
/// \param File The PDB containing the DBI and module streams.
/// \param ModuleName [out] Set to the module's name on success.
/// \param Index Zero-based DBI module index.
///
/// \returns The reloaded ModuleDebugStreamRef, or an Error if the index is
///     out of range or the module stream is missing or corrupt.
LLVM_ABI Expected<ModuleDebugStreamRef>
getModuleDebugStream(PDBFile &File, StringRef &ModuleName, uint32_t Index);
/// Load the module debug stream for DBI module \p Index.
///
/// \param File The PDB containing the DBI and module streams.
/// \param Index Zero-based DBI module index.
///
/// \returns The reloaded ModuleDebugStreamRef, or an Error if the module
///     stream is missing or corrupt.
LLVM_ABI Expected<ModuleDebugStreamRef> getModuleDebugStream(PDBFile &File,
                                                             uint32_t Index);

/// Return whether symbol group \p Group at index \p Idx should be dumped.
///
/// Honors \c JustMyCode and optional \c DumpModi filters from \p Filters.
///
/// \param Idx Zero-based index of the symbol group among all groups.
/// \param Group The symbol group being considered.
/// \param Filters Dump filters from the line printer / command line.
///
/// \returns True if the group passes the active filters.
LLVM_ABI bool shouldDumpSymbolGroup(uint32_t Idx, const SymbolGroup &Group,
                                    const FilterOptions &Filters);

// TODO: Change these callbacks to be function_refs (de-templatify them).
/// Print a module header for \p SG and invoke \p Callback for that module.
///
/// \param File The input file being dumped (unused by the default body beyond
///     the callback).
/// \param HeaderScope Print scope used for the module header and indentation.
/// \param SG The symbol group (module or \c .debug$S section) to visit.
/// \param Modi Zero-based module or group index printed in the header.
/// \param Callback Invoked as \c Callback(Modi, SG); its Error is returned.
///
/// \returns The Error returned by \p Callback.
template <typename CallbackT>
Error iterateOneModule(InputFile &File, const PrintScope &HeaderScope,
                       const SymbolGroup &SG, uint32_t Modi,
                       CallbackT Callback) {
  HeaderScope.P.formatLine(
      "Mod {0:4} | `{1}`: ",
      fmt_align(Modi, AlignStyle::Right, HeaderScope.LabelWidth), SG.name());

  AutoIndent Indent(HeaderScope);
  return Callback(Modi, SG);
}

/// Visit each filtered symbol group in \p Input, invoking \p Callback.
///
/// If \c DumpModi is set in the printer filters, only that module is visited.
/// Otherwise each group that passes \c shouldDumpSymbolGroup is visited.
///
/// \param Input The input file whose symbol groups are iterated.
/// \param HeaderScope Print scope used for module headers and indentation.
/// \param Callback Invoked as \c Callback(Modi, SG) for each selected group.
///
/// \returns The first Error from a callback, or success if all visits succeed.
template <typename CallbackT>
Error iterateSymbolGroups(InputFile &Input, const PrintScope &HeaderScope,
                          CallbackT Callback) {
  AutoIndent Indent(HeaderScope);

  FilterOptions Filters = HeaderScope.P.getFilters();
  if (Filters.DumpModi) {
    uint32_t Modi = *Filters.DumpModi;
    SymbolGroup SG(&Input, Modi);
    return iterateOneModule(Input,
                            withLabelWidth(HeaderScope, NumDigitsBase10(Modi)),
                            SG, Modi, Callback);
  }

  uint32_t I = 0;

  for (const auto &SG : Input.symbol_groups()) {
    if (shouldDumpSymbolGroup(I, SG, Filters))
      if (auto Err = iterateOneModule(
              Input, withLabelWidth(HeaderScope, NumDigitsBase10(I)), SG, I,
              Callback))
        return Err;

    ++I;
  }
  return Error::success();
}

/// Visit each debug subsection of type \p SubsectionT across filtered modules.
///
/// For every symbol group selected by \c iterateSymbolGroups, initializes each
/// matching subsection and invokes \p Callback.
///
/// \param File The input file whose modules and subsections are iterated.
/// \param HeaderScope Print scope forwarded to \c iterateSymbolGroups.
/// \param Callback Invoked as \c Callback(Modi, SG, Subsection) for each
///     matching subsection.
///
/// \returns The first Error from a callback, or success if all visits succeed.
template <typename SubsectionT>
Error iterateModuleSubsections(
    InputFile &File, const PrintScope &HeaderScope,
    llvm::function_ref<Error(uint32_t, const SymbolGroup &, SubsectionT &)>
        Callback) {

  return iterateSymbolGroups(
      File, HeaderScope, [&](uint32_t Modi, const SymbolGroup &SG) -> Error {
        for (const auto &SS : SG.getDebugSubsections()) {
          SubsectionT Subsection;

          if (SS.kind() != Subsection.kind())
            continue;

          BinaryStreamReader Reader(SS.getRecordData());
          if (auto Err = Subsection.initialize(Reader))
            continue;
          if (auto Err = Callback(Modi, SG, Subsection))
            return Err;
        }
        return Error::success();
      });
}

} // namespace pdb
} // namespace llvm

#endif
