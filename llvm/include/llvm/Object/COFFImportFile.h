//===- COFFImportFile.h - COFF short import file implementation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// COFF short import file is a special kind of file which contains
// only symbol names for DLL-exported symbols. This class implements
// exporting of Symbols to create libraries and a SymbolicFile
// interface for the file type.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_COFFIMPORTFILE_H
#define LLVM_OBJECT_COFFIMPORTFILE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace object {

/// Prefix for per-DLL import descriptor symbols (e.g. __IMPORT_DESCRIPTOR_foo).
constexpr std::string_view ImportDescriptorPrefix = "__IMPORT_DESCRIPTOR_";
/// Name of the null import descriptor sentinel symbol.
constexpr std::string_view NullImportDescriptorSymbolName =
    "__NULL_IMPORT_DESCRIPTOR";
/// Prefix byte used when forming null-thunk data symbol names.
constexpr std::string_view NullThunkDataPrefix = "\x7f";
/// Suffix for null-thunk data symbols (e.g. \x7ffoo_NULL_THUNK_DATA).
constexpr std::string_view NullThunkDataSuffix = "_NULL_THUNK_DATA";

/// SymbolicFile for a COFF short import library member.
class LLVM_ABI COFFImportFile : public SymbolicFile {
private:
  enum SymbolIndex { ImpSymbol, ThunkSymbol, ECAuxSymbol, ECThunkSymbol };

public:
  /// Construct a COFFImportFile backed by \p Source.
  ///
  /// \param Source Memory buffer containing a COFF short import file.
  COFFImportFile(MemoryBufferRef Source)
      : SymbolicFile(ID_COFFImportFile, Source) {}

  /// True if \p V is a COFFImportFile.
  ///
  /// \param V Binary to test.
  /// \return True if \p V is a COFFImportFile.
  static bool classof(Binary const *V) { return V->isCOFFImportFile(); }

  /// Advances \p Symb to the next symbol.
  ///
  /// \param Symb Symbol data reference to advance.
  void moveSymbolNext(DataRefImpl &Symb) const override { ++Symb.p; }

  /// Print the name of symbol \p Symb to \p OS.
  ///
  /// \param OS Stream that receives the symbol name.
  /// \param Symb Symbol data reference.
  /// \return Success, or an error if the symbol name cannot be printed.
  Error printSymbolName(raw_ostream &OS, DataRefImpl Symb) const override;

  /// Flags for symbol \p Symb (bitwise OR of BasicSymbolRef::Flags).
  ///
  /// \param Symb Symbol data reference.
  /// \return Symbol flags for \p Symb, or an error on failure.
  Expected<uint32_t> getSymbolFlags(DataRefImpl Symb) const override {
    return SymbolRef::SF_Global;
  }

  /// Iterator to the first symbol in this file.
  ///
  /// \return Iterator to the first symbol.
  basic_symbol_iterator symbol_begin() const override {
    return BasicSymbolRef(DataRefImpl(), this);
  }

  /// Past-the-end iterator for symbols in this file.
  ///
  /// \return Iterator one past the last symbol.
  basic_symbol_iterator symbol_end() const override {
    DataRefImpl Symb;
    if (isData())
      Symb.p = ImpSymbol + 1;
    else if (COFF::isArm64EC(getMachine()))
      Symb.p = ECThunkSymbol + 1;
    else
      Symb.p = ThunkSymbol + 1;
    return BasicSymbolRef(Symb, this);
  }

  /// True if this file uses a 64-bit address size.
  ///
  /// \return True if this file uses a 64-bit address size.
  bool is64Bit() const override { return false; }

  /// Returns the COFF short import header at the start of the buffer.
  ///
  /// \return Pointer to the COFF short import header.
  const coff_import_header *getCOFFImportHeader() const {
    return reinterpret_cast<const object::coff_import_header *>(
        Data.getBufferStart());
  }

  /// Machine type from the COFF import header.
  ///
  /// \return Machine type value from the import header.
  uint16_t getMachine() const { return getCOFFImportHeader()->Machine; }

  /// Human-readable name of this COFF import file format.
  ///
  /// \return Human-readable name of this file format.
  StringRef getFileFormatName() const;
  /// Exported symbol name described by this import file.
  ///
  /// \return Exported symbol name described by this import file.
  StringRef getExportName() const;

private:
  bool isData() const {
    return getCOFFImportHeader()->getType() == COFF::IMPORT_DATA;
  }
};

/// Compact description of a single COFF DLL export used when writing import
/// libraries.
struct COFFShortExport {
  /// The export name as given in the .def file or on the command line.
  ///
  /// The name of the export as specified in the .def file or on the command
  /// line, i.e. "foo" in "/EXPORT:foo", and "bar" in "/EXPORT:foo=bar". This
  /// may lack mangling, such as underscore prefixing and stdcall suffixing.
  std::string Name;

  /// The external, exported name. Only non-empty when export renaming is in
  /// effect, i.e. "foo" in "/EXPORT:foo=bar".
  std::string ExtName;

  /// The real, mangled symbol name from the object file. Given
  /// "/export:foo=bar", this could be "_bar@8" if bar is stdcall.
  std::string SymbolName;

  /// DLL export name referenced when linking against this import library entry.
  ///
  /// Creates an import library entry that imports from a DLL export with a
  /// different name. This is the name of the DLL export that should be
  /// referenced when linking against this import library entry. In a .def
  /// file, this is "baz" in "EXPORTS\nfoo = bar == baz".
  std::string ImportName;

  /// Specifies EXPORTAS name. In a .def file, this is "bar" in
  /// "EXPORTS\nfoo EXPORTAS bar".
  std::string ExportAs;

  /// Optional export ordinal; zero if none was specified.
  uint16_t Ordinal = 0;
  /// True if the export is by ordinal only (NONAME).
  bool Noname = false;
  /// True if this is a data export rather than code.
  bool Data = false;
  /// True if the export is private and omitted from the import library.
  bool Private = false;
  /// True if the export is marked CONSTANT in the .def file.
  bool Constant = false;

  /// True if both exports describe the same public export attributes.
  ///
  /// \param L Left-hand export.
  /// \param R Right-hand export.
  /// \return True if both exports describe the same public export attributes.
  friend bool operator==(const COFFShortExport &L, const COFFShortExport &R) {
    return L.Name == R.Name && L.ExtName == R.ExtName &&
            L.Ordinal == R.Ordinal && L.Noname == R.Noname &&
            L.Data == R.Data && L.Private == R.Private;
  }

  /// True if the exports differ in their compared public attributes.
  ///
  /// \param L Left-hand export.
  /// \param R Right-hand export.
  /// \return True if the exports differ in their compared public attributes.
  friend bool operator!=(const COFFShortExport &L, const COFFShortExport &R) {
    return !(L == R);
  }
};

/// Writes a COFF import library containing entries described by the Exports
/// array.
///
/// For hybrid targets such as ARM64EC, additional native entry points can be
/// exposed using the NativeExports parameter. When NativeExports is used, the
/// output import library will expose these native ARM64 imports alongside the
/// entries described in the Exports array. Such a library can be used for
/// linking both ARM64EC and pure ARM64 objects, and the linker will pick only
/// the exports relevant to the target platform. For non-hybrid targets,
/// the NativeExports parameter should not be used.
///
/// \param ImportName Name of the DLL being imported from.
/// \param Path Output path for the import library file.
/// \param Exports Export entries to include for the primary target.
/// \param Machine Target machine type for the import library.
/// \param MinGW If true, emit MinGW-style import library members.
/// \param NativeExports Optional native (non-EC) exports for hybrid targets.
/// \return Success, or an error if the import library could not be written.
LLVM_ABI Error writeImportLibrary(StringRef ImportName, StringRef Path,
                                  ArrayRef<COFFShortExport> Exports,
                                  COFF::MachineTypes Machine, bool MinGW,
                                  ArrayRef<COFFShortExport> NativeExports = {});

} // namespace object
} // namespace llvm

#endif
