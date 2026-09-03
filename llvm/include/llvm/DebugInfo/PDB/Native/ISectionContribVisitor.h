//===- ISectionContribVisitor.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_ISECTIONCONTRIBVISITOR_H
#define LLVM_DEBUGINFO_PDB_NATIVE_ISECTIONCONTRIBVISITOR_H

namespace llvm {
namespace pdb {

struct SectionContrib;
struct SectionContrib2;

/// Interface for visiting section contribution records from a PDB DBI stream.
class ISectionContribVisitor {
public:
  /// Destroy the section contribution visitor.
  virtual ~ISectionContribVisitor() = default;

  /// Visit a version-1 section contribution record.
  ///
  /// \param C The section contribution to visit.
  virtual void visit(const SectionContrib &C) = 0;

  /// Visit a version-2 section contribution record.
  ///
  /// \param C The section contribution to visit.
  virtual void visit(const SectionContrib2 &C) = 0;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_ISECTIONCONTRIBVISITOR_H
