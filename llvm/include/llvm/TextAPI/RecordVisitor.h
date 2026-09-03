//===- llvm/TextAPI/RecordSlice.h - TAPI RecordSlice ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// Defines the TAPI Record Visitor.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_RECORDVISITOR_H
#define LLVM_TEXTAPI_RECORDVISITOR_H

#include "llvm/Support/Compiler.h"
#include "llvm/TextAPI/Record.h"
#include "llvm/TextAPI/SymbolSet.h"

namespace llvm {
namespace MachO {

/// Base class for any usage of traversing over collected Records.
class LLVM_ABI RecordVisitor {
public:
  /// Destroy the record visitor.
  virtual ~RecordVisitor();

  /// Visit a non-ObjC global record.
  ///
  /// \param GR The global record being visited.
  virtual void visitGlobal(const GlobalRecord &GR) = 0;
  /// Visit an Objective-C interface (class) record.
  ///
  /// \param ObjCR The Objective-C interface record being visited.
  virtual void visitObjCInterface(const ObjCInterfaceRecord &ObjCR);
  /// Visit an Objective-C category record.
  ///
  /// \param Cat The Objective-C category record being visited.
  virtual void visitObjCCategory(const ObjCCategoryRecord &Cat);
};

/// Specialized RecordVisitor for collecting exported symbols
/// and undefined symbols if RecordSlice being visited represents a
/// flat-namespaced library.
class LLVM_ABI SymbolConverter : public RecordVisitor {
public:
  /// Construct a symbol converter for the given symbol set and target.
  ///
  /// \param Symbols The symbol set to populate with visited symbols.
  /// \param T The target associated with the records being visited.
  /// \param RecordUndefs Whether to record undefined symbols for flat-
  /// namespaced libraries.
  SymbolConverter(SymbolSet *Symbols, const Target &T,
                  const bool RecordUndefs = false)
      : Symbols(Symbols), Targ(T), RecordUndefs(RecordUndefs) {}
  /// Convert a non-ObjC global record into symbols.
  ///
  /// \param GR The global record being visited.
  void visitGlobal(const GlobalRecord &GR) override;
  /// Convert an Objective-C interface record into symbols.
  ///
  /// \param ObjCR The Objective-C interface record being visited.
  void visitObjCInterface(const ObjCInterfaceRecord &ObjCR) override;
  /// Convert an Objective-C category record into symbols.
  ///
  /// \param Cat The Objective-C category record being visited.
  void visitObjCCategory(const ObjCCategoryRecord &Cat) override;

private:
  void addIVars(const ArrayRef<ObjCIVarRecord *>, StringRef ContainerName);
  SymbolSet *Symbols;
  const Target Targ;
  const bool RecordUndefs;
};

} // end namespace MachO.
} // end namespace llvm.

#endif // LLVM_TEXTAPI_RECORDVISITOR_H
