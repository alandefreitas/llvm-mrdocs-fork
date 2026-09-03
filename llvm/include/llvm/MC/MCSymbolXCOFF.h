//===- MCSymbolXCOFF.h -  ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_MC_MCSYMBOLXCOFF_H
#define LLVM_MC_MCSYMBOLXCOFF_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolTableEntry.h"

namespace llvm {

class MCSectionXCOFF;

/// An MCSymbol for XCOFF, with storage class, csect, and rename state.
class MCSymbolXCOFF : public MCSymbol {

  enum XCOFFSymbolFlags : uint16_t { SF_EHInfo = 0x0001 };

public:
  /// Construct an XCOFF MC symbol with the given name entry.
  /// @param Name Symbol table entry that owns the symbol name.
  /// @param isTemporary Whether this is an assembler-temporary symbol.
  MCSymbolXCOFF(const MCSymbolTableEntry *Name, bool isTemporary)
      : MCSymbol(Name, isTemporary) {}

  /// Per-symbol code model used for XCOFF addressability.
  enum CodeModel : uint8_t {
    CM_Small, ///< Small code model for this symbol.
    CM_Large, ///< Large code model for this symbol.
  };

  /// Return \p Name without a trailing XCOFF storage-mapping-class qualifier.
  /// @param Name Possibly qualified symbol name such as `foo[RW]`.
  /// @return \p Name without a trailing XCOFF storage-mapping-class qualifier.
  static StringRef getUnqualifiedName(StringRef Name) {
    if (Name.back() == ']') {
      StringRef Lhs, Rhs;
      std::tie(Lhs, Rhs) = Name.rsplit('[');
      assert(!Rhs.empty() && "Invalid SMC format in XCOFF symbol.");
      return Lhs;
    }
    return Name;
  }

  /// Return true if this symbol is visible outside this translation unit.
  /// @return True if this symbol is visible outside this translation unit.
  bool isExternal() const { return IsExternal; }
  /// Set whether this symbol is visible outside this translation unit.
  /// @param Value True if the symbol should be treated as external.
  void setExternal(bool Value) const { IsExternal = Value; }
  /// Set the XCOFF storage class for this symbol.
  /// @param SC Storage class to record on the symbol.
  void setStorageClass(XCOFF::StorageClass SC) {
    StorageClass = SC;
  };

  /// Return the XCOFF storage class for this symbol.
  /// @return The XCOFF storage class for this symbol.
  XCOFF::StorageClass getStorageClass() const {
    assert(StorageClass && "StorageClass not set on XCOFF MCSymbol.");
    return *StorageClass;
  }

  /// Return this symbol's name without a storage-mapping-class qualifier.
  /// @return This symbol's name without a storage-mapping-class qualifier.
  StringRef getUnqualifiedName() const { return getUnqualifiedName(getName()); }

  /// Return the csect this symbol represents, if any.
  /// @return The csect this symbol represents, or null if none.
  LLVM_ABI MCSectionXCOFF *getRepresentedCsect() const;

  /// Set the csect this symbol represents.
  /// @param C Csect section associated with this symbol.
  LLVM_ABI void setRepresentedCsect(MCSectionXCOFF *C);

  /// Set the XCOFF visibility type for this symbol.
  /// @param SVT Visibility type to record on the symbol.
  void setVisibilityType(XCOFF::VisibilityType SVT) { VisibilityType = SVT; };

  /// Return the XCOFF visibility type for this symbol.
  /// @return The XCOFF visibility type for this symbol.
  XCOFF::VisibilityType getVisibilityType() const { return VisibilityType; }

  /// Return true if a renamed symbol-table name has been set.
  /// @return True if a renamed symbol-table name has been set.
  bool hasRename() const { return HasRename; }

  /// Set the name emitted for this symbol in the XCOFF symbol table.
  /// @param STN Symbol-table name to use instead of the unqualified name.
  void setSymbolTableName(StringRef STN) {
    SymbolTableName = STN;
    HasRename = true;
  }

  /// Return the name emitted for this symbol in the XCOFF symbol table.
  /// @return The name emitted for this symbol in the XCOFF symbol table.
  StringRef getSymbolTableName() const {
    if (hasRename())
      return SymbolTableName;
    return getUnqualifiedName();
  }

  /// Return true if this symbol is an exception-handling info symbol.
  /// @return True if this symbol is an exception-handling info symbol.
  bool isEHInfo() const { return getFlags() & SF_EHInfo; }

  /// Mark this symbol as an exception-handling info symbol.
  void setEHInfo() const { modifyFlags(SF_EHInfo, SF_EHInfo); }

  /// Return true if a per-symbol code model has been set.
  /// @return True if a per-symbol code model has been set.
  bool hasPerSymbolCodeModel() const { return PerSymbolCodeModel.has_value(); }

  /// Return the per-symbol code model for this symbol.
  /// @return The per-symbol code model for this symbol.
  CodeModel getPerSymbolCodeModel() const {
    assert(hasPerSymbolCodeModel() &&
           "Requested code model for symbol without one");
    return *PerSymbolCodeModel;
  }

  /// Set the per-symbol code model for this symbol.
  /// @param Model Code model to associate with this symbol.
  void setPerSymbolCodeModel(MCSymbolXCOFF::CodeModel Model) {
    PerSymbolCodeModel = Model;
  }

private:
  std::optional<XCOFF::StorageClass> StorageClass;
  std::optional<CodeModel> PerSymbolCodeModel;

  MCSectionXCOFF *RepresentedCsect = nullptr;
  XCOFF::VisibilityType VisibilityType = XCOFF::SYM_V_UNSPECIFIED;
  StringRef SymbolTableName;
  bool HasRename = false;
};

} // end namespace llvm

#endif // LLVM_MC_MCSYMBOLXCOFF_H
