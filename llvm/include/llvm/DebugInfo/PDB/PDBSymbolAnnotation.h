//===- PDBSymbolAnnotation.h - Accessors for querying PDB annotations ---*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLANNOTATION_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLANNOTATION_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

namespace pdb {

/// PDB symbol for an annotation attached to a code address.
///
/// Exposes address and data-kind accessors for a SymTagAnnotation symbol in
/// the PDB lexical hierarchy.
class LLVM_ABI PDBSymbolAnnotation : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// The PDB symbol tag for annotation symbols (`PDB_SymType::Annotation`).
  static const PDB_SymType Tag = PDB_SymType::Annotation;

  /// True if \p S is an annotation PDB symbol.
  ///
  /// \param S Symbol to test.
  ///
  /// \returns True if \p S is an annotation PDB symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this annotation symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the section-relative address offset of this annotation.
  ///
  /// \returns The section-relative address offset of this annotation.
  FORWARD_SYMBOL_METHOD(getAddressOffset)
  /// Return the section index of this annotation's address.
  ///
  /// \returns The section index of this annotation's address.
  FORWARD_SYMBOL_METHOD(getAddressSection)
  /// Return the data kind of this annotation.
  ///
  /// \returns The data kind of this annotation.
  FORWARD_SYMBOL_METHOD(getDataKind)
  /// Return the relative virtual address of this annotation.
  ///
  /// \returns The relative virtual address of this annotation.
  FORWARD_SYMBOL_METHOD(getRelativeVirtualAddress)
  // FORWARD_SYMBOL_METHOD(getValue)
  /// Return the virtual address of this annotation.
  ///
  /// \returns The virtual address of this annotation.
  FORWARD_SYMBOL_METHOD(getVirtualAddress)
};
}
}

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLANNOTATION_H
