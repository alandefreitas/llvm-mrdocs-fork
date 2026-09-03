//===- PDBSymbolCompilandDetails.h - PDB compiland details ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLCOMPILANDDETAILS_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLCOMPILANDDETAILS_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

namespace pdb {

/// PDB symbol for compiler and build details of a Compiland.
///
/// Exposes frontend/backend version, language, platform, source file, and
/// compilation flags for a Compiland in the PDB lexical hierarchy.
/// https://msdn.microsoft.com/en-us/library/sycdt6ys.aspx
class LLVM_ABI PDBSymbolCompilandDetails : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Sym tag value identifying CompilandDetails symbols.
  static const PDB_SymType Tag = PDB_SymType::CompilandDetails;

  /// Return true if \p S is a CompilandDetails symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a CompilandDetails symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this CompilandDetails symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Fill \p Version with the frontend compiler version for this Compiland.
  ///
  /// \param Version Receives the frontend version components.
  void getFrontEndVersion(VersionInfo &Version) const {
    RawSymbol->getFrontEndVersion(Version);
  }

  /// Fill \p Version with the backend compiler version for this Compiland.
  ///
  /// \param Version Receives the backend version components.
  void getBackEndVersion(VersionInfo &Version) const {
    RawSymbol->getBackEndVersion(Version);
  }

  /// Return the name of the compiler that produced this Compiland.
  ///
  /// \returns The compiler name as a string.
  FORWARD_SYMBOL_METHOD(getCompilerName)
  /// Return true if Edit and Continue is enabled for this Compiland.
  ///
  /// \returns True if Edit and Continue is enabled.
  FORWARD_SYMBOL_METHOD(isEditAndContinueEnabled)
  /// Return true if this Compiland has debug information.
  ///
  /// \returns True if this Compiland has debug information.
  FORWARD_SYMBOL_METHOD(hasDebugInfo)
  /// Return true if this Compiland contains managed code.
  ///
  /// \returns True if this Compiland contains managed code.
  FORWARD_SYMBOL_METHOD(hasManagedCode)
  /// Return true if this Compiland was built with security checks.
  ///
  /// \returns True if this Compiland was built with security checks.
  FORWARD_SYMBOL_METHOD(hasSecurityChecks)
  /// Return true if this Compiland was converted from CIL.
  ///
  /// \returns True if this Compiland was converted from CIL.
  FORWARD_SYMBOL_METHOD(isCVTCIL)
  /// Return true if this Compiland's data is aligned.
  ///
  /// \returns True if this Compiland's data is aligned.
  FORWARD_SYMBOL_METHOD(isDataAligned)
  /// Return true if this Compiland's code is hotpatchable.
  ///
  /// \returns True if this Compiland's code is hotpatchable.
  FORWARD_SYMBOL_METHOD(isHotpatchable)
  /// Return true if this Compiland was compiled with link-time code generation.
  ///
  /// \returns True if this Compiland was compiled with LTCG.
  FORWARD_SYMBOL_METHOD(isLTCG)
  /// Return true if this Compiland is an MSIL netmodule.
  ///
  /// \returns True if this Compiland is an MSIL netmodule.
  FORWARD_SYMBOL_METHOD(isMSILNetmodule)
  /// Return the source language of this Compiland.
  ///
  /// \returns The source language of this Compiland.
  FORWARD_SYMBOL_METHOD(getLanguage)
  /// Return the symbol id of this CompilandDetails' lexical parent.
  ///
  /// \returns The symbol id of the lexical parent.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this CompilandDetails' lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the target CPU platform for this Compiland.
  ///
  /// \returns The target CPU platform for this Compiland.
  FORWARD_SYMBOL_METHOD(getPlatform)
  /// Return the source file name associated with this Compiland.
  ///
  /// \returns The source file name associated with this Compiland.
  FORWARD_SYMBOL_METHOD(getSourceFileName)
};

} // namespace llvm
}

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLCOMPILANDDETAILS_H
