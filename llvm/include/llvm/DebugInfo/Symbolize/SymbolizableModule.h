//===- SymbolizableModule.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SymbolizableModule interface.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZABLEMODULE_H
#define LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZABLEMODULE_H

#include "llvm/DebugInfo/DIContext.h"
#include <cstdint>

namespace llvm {
namespace symbolize {

/// Alias for the preferred function-name resolution kind.
using FunctionNameKind = DILineInfoSpecifier::FunctionNameKind;

/// Abstract interface for looking up debug info in a loaded module.
class SymbolizableModule {
public:
  /// Destroy the symbolizable module.
  virtual ~SymbolizableModule() = default;

  /// Symbolize a code address within this module.
  /// @param ModuleOffset Address within the module to symbolize.
  /// @param LineInfoSpecifier Which line and function-name fields to fill.
  /// @param UseSymbolTable Prefer symbol-table names when debug info is missing.
  /// @return Line and function information for the code address.
  virtual DILineInfo symbolizeCode(object::SectionedAddress ModuleOffset,
                                   DILineInfoSpecifier LineInfoSpecifier,
                                   bool UseSymbolTable) const = 0;
  /// Symbolize a code address, expanding inlined call frames.
  /// @param ModuleOffset Address within the module to symbolize.
  /// @param LineInfoSpecifier Which line and function-name fields to fill.
  /// @param UseSymbolTable Prefer symbol-table names when debug info is missing.
  /// @return Inlining stack of line info for the code address.
  virtual DIInliningInfo
  symbolizeInlinedCode(object::SectionedAddress ModuleOffset,
                       DILineInfoSpecifier LineInfoSpecifier,
                       bool UseSymbolTable) const = 0;
  /// Symbolize a data address to a global variable or data symbol.
  /// @param ModuleOffset Address within the module to symbolize.
  /// @return Global or data-symbol information for the address.
  virtual DIGlobal
  symbolizeData(object::SectionedAddress ModuleOffset) const = 0;
  /// Symbolize local variables for a frame at the given address.
  /// @param ModuleOffset Address within the module identifying the frame.
  /// @return Local variables visible in the frame at that address.
  virtual std::vector<DILocal>
  symbolizeFrame(object::SectionedAddress ModuleOffset) const = 0;

  /// Find addresses of a named symbol, optionally offset from its start.
  /// @param Symbol Symbol name to look up.
  /// @param Offset Byte offset added to each matching symbol address.
  /// @return Sectioned addresses of matching symbols plus Offset.
  virtual std::vector<object::SectionedAddress>
  findSymbol(StringRef Symbol, uint64_t Offset) const = 0;

  /// Return true if this is a 32-bit x86 PE COFF module.
  /// @return True if this module is a 32-bit x86 PE COFF image.
  virtual bool isWin32Module() const = 0;

  /// Return the preferred base of the module.
  ///
  /// This is where the loader would place it in memory assuming there were no
  /// conflicts.
  /// @return Preferred load base address of the module.
  virtual uint64_t getModulePreferredBase() const = 0;
};

} // end namespace symbolize
} // end namespace llvm

#endif  // LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZABLEMODULE_H
