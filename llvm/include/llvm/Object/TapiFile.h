//===- TapiFile.h - Text-based Dynamic Library Stub -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the TapiFile interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_TAPIFILE_H
#define LLVM_OBJECT_TAPIFILE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/InterfaceFile.h"

namespace llvm {

class raw_ostream;

namespace object {

/// SymbolicFile for a text-based dynamic library stub (TAPI/TBD) for one architecture.
class LLVM_ABI TapiFile : public SymbolicFile {
public:
  /// Construct a TapiFile for \p Arch from \p Interface backed by \p Source.
  ///
  /// \param Source Memory buffer containing the TAPI/TBD text.
  /// \param Interface Parsed Mach-O interface describing the library.
  /// \param Arch Architecture slice this SymbolicFile represents.
  TapiFile(MemoryBufferRef Source, const MachO::InterfaceFile &Interface,
           MachO::Architecture Arch);
  /// Destroy this TapiFile.
  ~TapiFile() override;

  /// Advances \p DRI to the next symbol.
  ///
  /// \param DRI Symbol data reference to advance.
  void moveSymbolNext(DataRefImpl &DRI) const override;

  /// Print the name of symbol \p DRI to \p OS.
  ///
  /// \param OS Stream that receives the symbol name.
  /// \param DRI Symbol data reference.
  /// \return Success, or an error if the symbol name cannot be printed.
  Error printSymbolName(raw_ostream &OS, DataRefImpl DRI) const override;

  /// Flags for symbol \p DRI (bitwise OR of BasicSymbolRef::Flags).
  ///
  /// \param DRI Symbol data reference.
  /// \return The symbol flags, or an error on failure.
  Expected<uint32_t> getSymbolFlags(DataRefImpl DRI) const override;

  /// Iterator to the first symbol in this file.
  ///
  /// \return Iterator pointing at the first symbol.
  basic_symbol_iterator symbol_begin() const override;

  /// Past-the-end iterator for symbols in this file.
  ///
  /// \return Iterator one past the last symbol.
  basic_symbol_iterator symbol_end() const override;

  /// Symbol type for symbol \p DRI.
  ///
  /// \param DRI Symbol data reference.
  /// \return The symbol type, or an error on failure.
  Expected<SymbolRef::Type> getSymbolType(DataRefImpl DRI) const;

  /// True if this TBD version includes segment information (TBD v5 or later).
  ///
  /// \return True if segment information is present.
  bool hasSegmentInfo() { return FileKind >= MachO::FileType::TBD_V5; }

  /// True if \p v is a TapiFile.
  ///
  /// \param v Binary to test.
  /// \return True if \p v is a TapiFile.
  static bool classof(const Binary *v) { return v->isTapiFile(); }

  /// True if this file uses a 64-bit address size.
  ///
  /// \return True if the architecture uses 64-bit addresses.
  bool is64Bit() const override { return MachO::is64Bit(Arch); }

private:
  struct Symbol {
    StringRef Prefix;
    StringRef Name;
    uint32_t Flags;
    SymbolRef::Type Type;

    constexpr Symbol(StringRef Prefix, StringRef Name, uint32_t Flags,
                     SymbolRef::Type Type)
        : Prefix(Prefix), Name(Name), Flags(Flags), Type(Type) {}
  };

  std::vector<Symbol> Symbols;
  MachO::Architecture Arch;
  MachO::FileType FileKind;
};

} // end namespace object.
} // end namespace llvm.

#endif // LLVM_OBJECT_TAPIFILE_H
