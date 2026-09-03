//===- RecordName.h ------------------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_RECORDNAME_H
#define LLVM_DEBUGINFO_CODEVIEW_RECORDNAME_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/Support/Compiler.h"
#include <string>

namespace llvm {
namespace codeview {
class TypeCollection;
class TypeIndex;
/// Compute a human-readable name for the type at \p Index in \p Types.
/// \param Types Type collection used to look up and resolve type records.
/// \param Index Type index of the record whose name should be computed.
/// \returns The type name, or `"<unknown UDT>"` if visitation fails.
LLVM_ABI std::string computeTypeName(TypeCollection &Types, TypeIndex Index);
/// Extract the name string embedded in a CodeView symbol record.
/// \param Sym Symbol record whose name field should be read.
/// \returns The symbol name, or an empty string if the kind has no name.
LLVM_ABI StringRef getSymbolName(CVSymbol Sym);
} // namespace codeview
} // namespace llvm

#endif
