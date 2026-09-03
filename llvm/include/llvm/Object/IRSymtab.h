//===- IRSymtab.h - data definitions for IR symbol tables -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains data definitions and a reader and builder for a symbol
// table for LLVM IR. Its purpose is to allow linkers and other consumers of
// bitcode files to efficiently read the symbol table for symbol resolution
// purposes without needing to construct a module in memory.
//
// As with most object files the symbol table has two parts: the symbol table
// itself and a string table which is referenced by the symbol table.
//
// A symbol table corresponds to a single bitcode file, which may consist of
// multiple modules, so symbol tables may likewise contain symbols for multiple
// modules.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_IRSYMTAB_H
#define LLVM_OBJECT_IRSYMTAB_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cassert>
#include <cstdint>
#include <vector>

namespace llvm {

struct BitcodeFileContents;
class StringTableBuilder;

/// Symbol table format, reader, and builder for LLVM IR bitcode.
namespace irsymtab {

/// Low-level serialization format for the IR symbol table.
///
/// Clients that just want to read a symbol table should use the
/// irsymtab::Reader class.
namespace storage {

/// 32-bit little-endian word used throughout the serialized format.
using Word = support::ulittle32_t;

/// A reference to a string in the string table.
struct Str {
  Word Offset; ///< Byte offset of the string in the string table.
  Word Size;   ///< Length of the string in bytes.

  /// Resolve this reference against \p Strtab.
  ///
  /// \param Strtab String table that backs this reference.
  /// @return The referenced substring within \p Strtab.
  StringRef get(StringRef Strtab) const {
    return {Strtab.data() + Offset, Size};
  }
};

/// A reference to a range of objects in the symbol table.
template <typename T> struct Range {
  Word Offset; ///< Byte offset of the first element in the symbol table.
  Word Size;   ///< Number of elements in the range.

  /// Resolve this range against \p Symtab.
  ///
  /// \param Symtab Symbol table blob that backs this range.
  /// @return The referenced slice of \p T elements within \p Symtab.
  ArrayRef<T> get(StringRef Symtab) const {
    return {reinterpret_cast<const T *>(Symtab.data() + Offset), Size};
  }
};

/// Describes the range of a particular module's symbols within the symbol
/// table.
struct Module {
  Word Begin; ///< Index of the first symbol belonging to this module.
  Word End;   ///< One-past-the-last symbol index for this module.

  /// The index of the first Uncommon for this Module.
  Word UncBegin;
};

/// This is equivalent to an IR comdat.
struct Comdat {
  /// The comdat name.
  Str Name;

  /// Selection kind; see llvm::Comdat::SelectionKind.
  Word SelectionKind;
};

/// Contains the information needed by linkers for symbol resolution, as well as
/// by the LTO implementation itself.
struct Symbol {
  /// The mangled symbol name.
  Str Name;

  /// The unmangled symbol name, or the empty string if this is not an IR
  /// symbol.
  Str IRName;

  /// The index into Header::Comdats, or -1 if not a comdat member.
  Word ComdatIndex;

  /// Bitfield of FlagBits describing this symbol.
  Word Flags;

  /// Bit positions within Flags.
  enum FlagBits {
    FB_visibility, ///< Visibility; occupies two bits.
    FB_has_uncommon = FB_visibility + 2, ///< Symbol has an Uncommon record.
    FB_undefined,       ///< Symbol is undefined.
    FB_weak,            ///< Symbol is weak.
    FB_common,          ///< Symbol is a common symbol.
    FB_indirect,        ///< Symbol is an alias/indirect reference.
    FB_used,            ///< Symbol is marked used.
    FB_tls,             ///< Symbol is thread-local.
    FB_may_omit,        ///< Symbol may be omitted from the symbol table.
    FB_global,          ///< Symbol is a global (not local).
    FB_format_specific, ///< Format-specific symbol (e.g. section).
    FB_unnamed_addr,    ///< Symbol has unnamed_addr.
    FB_executable,      ///< Symbol points to executable content.
  };
};

/// This data structure contains rarely used symbol fields and is optionally
/// referenced by a Symbol.
struct Uncommon {
  /// Size in bytes of a common symbol.
  support::ulittle64_t CommonSize;
  /// Alignment of a common symbol.
  Word CommonAlign;

  /// COFF-specific: the name of the symbol that a weak external resolves to
  /// if not defined.
  Str COFFWeakExternFallbackName;

  /// Specified section name, if any.
  Str SectionName;
};


/// Header of a serialized IR symbol table.
struct Header {
  /// Version number of the symtab format.
  ///
  /// This number should be incremented when the format changes, but it does
  /// not need to be incremented if a change to LLVM would cause it to create a
  /// different symbol table.
  Word Version;

  /// Named constants for Header::Version.
  enum {
    kCurrentVersion = 4 ///< Current IR symbol table format version.
  };

  /// Version string of the LLVM that produced this symbol table.
  ///
  /// The producer's version string (LLVM_VERSION_STRING " " LLVM_REVISION).
  /// Consumers should rebuild the symbol table from IR if the producer's
  /// version does not match the consumer's version due to potential differences
  /// in symbol table format, symbol enumeration order and so on.
  Str Producer;

  /// Per-module symbol ranges within the table.
  Range<Module> Modules;
  /// Comdat table for the file.
  Range<Comdat> Comdats;
  /// Symbol records for the file.
  Range<Symbol> Symbols;
  /// Optional uncommon fields referenced by Symbols.
  Range<Uncommon> Uncommons;

  /// Target triple for the bitcode file.
  Str TargetTriple;
  /// Source file name recorded at compile time.
  Str SourceFileName;

  /// COFF-specific: linker directives.
  Str COFFLinkerOpts;

  /// Dependent Library Specifiers
  Range<Str> DependentLibraries;
};

} // end namespace storage

/// Fills in Symtab and StrtabBuilder with a valid symbol and string table for
/// Mods.
///
/// \param Mods Modules to encode in the symbol table.
/// \param Symtab Destination buffer for the serialized symbol table.
/// \param StrtabBuilder Builder that accumulates strings referenced by Symtab.
/// \param Alloc Allocator for temporary storage used while building.
/// @return Success, or an error if the symbol table could not be built.
LLVM_ABI Error build(ArrayRef<Module *> Mods, SmallVector<char, 0> &Symtab,
                     StringTableBuilder &StrtabBuilder,
                     BumpPtrAllocator &Alloc);

/// This represents a symbol that has been read from a storage::Symbol and
/// possibly a storage::Uncommon.
struct Symbol {
  // Copied from storage::Symbol.
  /// Mangled symbol name.
  mutable StringRef Name;
  /// Unmangled IR name, or empty if this is not an IR symbol.
  StringRef IRName;
  /// Index into the comdat table, or -1 if not a comdat member.
  int ComdatIndex;
  /// Bitfield of storage::Symbol::FlagBits for this symbol.
  uint32_t Flags;

  // Copied from storage::Uncommon.
  /// Size in bytes if this is a common symbol.
  uint64_t CommonSize;
  /// Alignment if this is a common symbol.
  uint32_t CommonAlign;
  /// COFF weak-external fallback symbol name, if any.
  StringRef COFFWeakExternFallbackName;
  /// Section name for this symbol, if any.
  StringRef SectionName;

  /// Returns the mangled symbol name.
  ///
  /// @return The mangled symbol name.
  StringRef getName() const { return Name; }

  /// Returns the unmangled symbol name, or the empty string if this is not an
  /// IR symbol.
  ///
  /// @return The unmangled IR name, or empty if this is not an IR symbol.
  StringRef getIRName() const { return IRName; }

  /// Returns the index into the comdat table (see Reader::getComdatTable()), or
  /// -1 if not a comdat member.
  ///
  /// @return The comdat table index, or -1 if not a comdat member.
  int getComdatIndex() const { return ComdatIndex; }

  /// Alias for storage::Symbol used when decoding Flags.
  using S = storage::Symbol;

  /// Returns the symbol's visibility.
  ///
  /// @return The symbol's visibility kind.
  GlobalValue::VisibilityTypes getVisibility() const {
    return GlobalValue::VisibilityTypes((Flags >> S::FB_visibility) & 3);
  }

  /// True if the symbol is undefined.
  ///
  /// @return True if the symbol is undefined.
  bool isUndefined() const { return (Flags >> S::FB_undefined) & 1; }
  /// True if the symbol is weak.
  ///
  /// @return True if the symbol is weak.
  bool isWeak() const { return (Flags >> S::FB_weak) & 1; }
  /// True if the symbol is a common symbol.
  ///
  /// @return True if the symbol is a common symbol.
  bool isCommon() const { return (Flags >> S::FB_common) & 1; }
  /// True if the symbol is an indirect reference/alias.
  ///
  /// @return True if the symbol is an indirect reference or alias.
  bool isIndirect() const { return (Flags >> S::FB_indirect) & 1; }
  /// True if the symbol is marked used.
  ///
  /// @return True if the symbol is marked used.
  bool isUsed() const { return (Flags >> S::FB_used) & 1; }
  /// True if the symbol is thread-local.
  ///
  /// @return True if the symbol is thread-local.
  bool isTLS() const { return (Flags >> S::FB_tls) & 1; }

  /// True if the symbol may be omitted from the linker symbol table.
  ///
  /// @return True if the symbol may be omitted from the linker symbol table.
  bool canBeOmittedFromSymbolTable() const {
    return (Flags >> S::FB_may_omit) & 1;
  }

  /// True if the symbol is global (not local).
  ///
  /// @return True if the symbol is global (not local).
  bool isGlobal() const { return (Flags >> S::FB_global) & 1; }
  /// True if the symbol is format-specific (e.g. a section symbol).
  ///
  /// @return True if the symbol is format-specific.
  bool isFormatSpecific() const { return (Flags >> S::FB_format_specific) & 1; }
  /// True if the symbol has unnamed_addr.
  ///
  /// @return True if the symbol has unnamed_addr.
  bool isUnnamedAddr() const { return (Flags >> S::FB_unnamed_addr) & 1; }
  /// True if the symbol refers to executable content.
  ///
  /// @return True if the symbol refers to executable content.
  bool isExecutable() const { return (Flags >> S::FB_executable) & 1; }

  /// Returns the size of this common symbol.
  ///
  /// @return Size in bytes of this common symbol.
  uint64_t getCommonSize() const {
    assert(isCommon());
    return CommonSize;
  }

  /// Returns the alignment of this common symbol.
  ///
  /// @return Alignment of this common symbol.
  uint32_t getCommonAlignment() const {
    assert(isCommon());
    return CommonAlign;
  }

  /// COFF-specific: for weak externals, returns the name of the symbol that is
  /// used as a fallback if the weak external remains undefined.
  ///
  /// @return The fallback symbol name for this weak external.
  StringRef getCOFFWeakExternalFallback() const {
    assert(isWeak() && isIndirect());
    return COFFWeakExternFallbackName;
  }

  /// Returns the section name for this symbol, if any.
  ///
  /// @return The section name, or empty if none is specified.
  StringRef getSectionName() const { return SectionName; }
};

/// This class can be used to read a Symtab and Strtab produced by
/// irsymtab::build.
class Reader {
  StringRef Symtab, Strtab;

  ArrayRef<storage::Module> Modules;
  ArrayRef<storage::Comdat> Comdats;
  ArrayRef<storage::Symbol> Symbols;
  ArrayRef<storage::Uncommon> Uncommons;
  ArrayRef<storage::Str> DependentLibraries;

  StringRef str(storage::Str S) const { return S.get(Strtab); }

  template <typename T> ArrayRef<T> range(storage::Range<T> R) const {
    return R.get(Symtab);
  }

  const storage::Header &header() const {
    return *reinterpret_cast<const storage::Header *>(Symtab.data());
  }

public:
  class SymbolRef;

  /// Constructs an empty reader.
  Reader() = default;
  /// Constructs a reader over the given symbol and string tables.
  ///
  /// \param Symtab Serialized symbol table bytes.
  /// \param Strtab String table referenced by Symtab.
  Reader(StringRef Symtab, StringRef Strtab) : Symtab(Symtab), Strtab(Strtab) {
    Modules = range(header().Modules);
    Comdats = range(header().Comdats);
    Symbols = range(header().Symbols);
    Uncommons = range(header().Uncommons);
    DependentLibraries = range(header().DependentLibraries);
  }

  /// Range of SymbolRef iterators over symbols in this file.
  using symbol_range = iterator_range<object::content_iterator<SymbolRef>>;

  /// Returns the symbol table for the entire bitcode file.
  ///
  /// The symbols enumerated by this method are ephemeral, but they can be
  /// copied into an irsymtab::Symbol object.
  ///
  /// @return A range of SymbolRef iterators over all symbols in the file.
  symbol_range symbols() const;

  /// Returns the number of modules described by this symbol table.
  ///
  /// @return The number of modules in the symbol table.
  size_t getNumModules() const { return Modules.size(); }

  /// Returns a slice of the symbol table for the I'th module in the file.
  ///
  /// The symbols enumerated by this method are ephemeral, but they can be
  /// copied into an irsymtab::Symbol object.
  ///
  /// \param I Zero-based module index.
  /// @return A range of SymbolRef iterators over the I'th module's symbols.
  symbol_range module_symbols(unsigned I) const;

  /// Returns the target triple recorded in the symbol table header.
  ///
  /// @return The target triple string.
  StringRef getTargetTriple() const { return str(header().TargetTriple); }

  /// Returns the source file path specified at compile time.
  ///
  /// @return The source file name string.
  StringRef getSourceFileName() const { return str(header().SourceFileName); }

  /// Returns a table with all the comdats used by this file.
  ///
  /// @return Pairs of comdat name and selection kind for each comdat.
  std::vector<std::pair<StringRef, llvm::Comdat::SelectionKind>>
  getComdatTable() const {
    std::vector<std::pair<StringRef, llvm::Comdat::SelectionKind>> ComdatTable;
    ComdatTable.reserve(Comdats.size());
    for (auto C : Comdats)
      ComdatTable.push_back({str(C.Name), llvm::Comdat::SelectionKind(
                                              uint32_t(C.SelectionKind))});
    return ComdatTable;
  }

  /// COFF-specific: returns linker options specified in the input file.
  ///
  /// @return The COFF linker options string.
  StringRef getCOFFLinkerOpts() const { return str(header().COFFLinkerOpts); }

  /// Returns dependent library specifiers
  ///
  /// @return The dependent library specifier strings.
  std::vector<StringRef> getDependentLibraries() const {
    std::vector<StringRef> Specifiers;
    Specifiers.reserve(DependentLibraries.size());
    for (auto S : DependentLibraries) {
      Specifiers.push_back(str(S));
    }
    return Specifiers;
  }
};

/// Ephemeral symbols produced by Reader::symbols() and
/// Reader::module_symbols().
class Reader::SymbolRef : public Symbol {
  const storage::Symbol *SymI, *SymE;
  const storage::Uncommon *UncI;
  const Reader *R;

  void read() {
    if (SymI == SymE)
      return;

    Name = R->str(SymI->Name);
    IRName = R->str(SymI->IRName);
    ComdatIndex = SymI->ComdatIndex;
    Flags = SymI->Flags;

    if (Flags & (1 << storage::Symbol::FB_has_uncommon)) {
      CommonSize = UncI->CommonSize;
      CommonAlign = UncI->CommonAlign;
      COFFWeakExternFallbackName = R->str(UncI->COFFWeakExternFallbackName);
      SectionName = R->str(UncI->SectionName);
    } else
      // Reset this field so it can be queried unconditionally for all symbols.
      SectionName = "";
  }

public:
  /// Constructs a SymbolRef spanning [\p SymI, \p SymE) with uncommon data.
  ///
  /// \param SymI First symbol in the range.
  /// \param SymE One-past-the-last symbol in the range.
  /// \param UncI Uncommon record corresponding to \p SymI, if any.
  /// \param R Reader that owns the backing symbol and string tables.
  SymbolRef(const storage::Symbol *SymI, const storage::Symbol *SymE,
            const storage::Uncommon *UncI, const Reader *R)
      : SymI(SymI), SymE(SymE), UncI(UncI), R(R) {
    read();
  }

  /// Advances this reference to the next symbol in the range.
  void moveNext() {
    ++SymI;
    if (Flags & (1 << storage::Symbol::FB_has_uncommon))
      ++UncI;
    read();
  }

  /// True if this and \p Other refer to the same storage::Symbol.
  ///
  /// \param Other Symbol reference to compare against.
  /// @return True if both references point to the same storage::Symbol.
  bool operator==(const SymbolRef &Other) const { return SymI == Other.SymI; }
};

inline Reader::symbol_range Reader::symbols() const {
  return {SymbolRef(Symbols.begin(), Symbols.end(), Uncommons.begin(), this),
          SymbolRef(Symbols.end(), Symbols.end(), nullptr, this)};
}

inline Reader::symbol_range Reader::module_symbols(unsigned I) const {
  const storage::Module &M = Modules[I];
  const storage::Symbol *MBegin = Symbols.begin() + M.Begin,
                        *MEnd = Symbols.begin() + M.End;
  return {SymbolRef(MBegin, MEnd, Uncommons.begin() + M.UncBegin, this),
          SymbolRef(MEnd, MEnd, nullptr, this)};
}

/// The contents of the irsymtab in a bitcode file. Any underlying data for the
/// irsymtab are owned by Symtab and Strtab.
struct FileContents {
  /// Serialized symbol table bytes.
  SmallVector<char, 0> Symtab;
  /// String table bytes referenced by Symtab.
  SmallVector<char, 0> Strtab;
  /// Reader over Symtab and Strtab.
  Reader TheReader;
};

/// Reads the contents of a bitcode file, creating its irsymtab if necessary.
///
/// \param BFC Parsed bitcode file contents to read the symbol table from.
/// @return The file's irsymtab contents, or an error on failure.
LLVM_ABI Expected<FileContents> readBitcode(const BitcodeFileContents &BFC);

} // end namespace irsymtab
} // end namespace llvm

#endif // LLVM_OBJECT_IRSYMTAB_H
