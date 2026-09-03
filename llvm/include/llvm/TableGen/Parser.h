//===- llvm/TableGen/Parser.h - tblgen parser entry point -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares an entry point into the tablegen parser for use by tools.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_PARSER_H
#define LLVM_TABLEGEN_PARSER_H

#include "llvm/Support/Compiler.h"

namespace llvm {
class RecordKeeper;
class SourceMgr;

/// Parse the TableGen file from the main buffer of a SourceMgr.
///
/// On success, populates the provided RecordKeeper with the parsed records and
/// returns false. On failure, returns true.
///
/// NOTE: TableGen currently relies on global state within a given parser
///       invocation, so this function is not thread-safe.
///
/// \param InputSrcMgr Source manager whose main buffer holds the TableGen file.
/// \param Records Record keeper populated with the parsed records on success.
/// \return false on success, true on failure.
LLVM_ABI bool TableGenParseFile(SourceMgr &InputSrcMgr, RecordKeeper &Records);

} // end namespace llvm

#endif // LLVM_TABLEGEN_PARSER_H
