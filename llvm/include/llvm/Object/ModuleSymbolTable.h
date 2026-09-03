//===- ModuleSymbolTable.h - symbol table for in-memory IR ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class represents a symbol table built from in-memory IR. It provides
// access to GlobalValues and should only be used if such access is required
// (e.g. in the LTO implementation).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_MODULESYMBOLTABLE_H
#define LLVM_OBJECT_MODULESYMBOLTABLE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class GlobalValue;
class Module;

/// Symbol table built from in-memory IR modules.
///
/// Provides access to GlobalValues and should only be used if such access is
/// required (e.g. in the LTO implementation).
class ModuleSymbolTable {
public:
  /// Assembly symbol name paired with its BasicSymbolRef flags.
  using AsmSymbol = std::pair<std::string, uint32_t>;
  /// Either an IR GlobalValue or an assembly AsmSymbol entry.
  using Symbol = PointerUnion<GlobalValue *, AsmSymbol *>;

private:
  Module *FirstMod = nullptr;

  SpecificBumpPtrAllocator<AsmSymbol> AsmSymbols;
  std::vector<Symbol> SymTab;
  Mangler Mang;

public:
  /// Returns the symbols recorded in this table.
  ///
  /// \return An ArrayRef of the Symbol entries in this table.
  ArrayRef<Symbol> symbols() const { return SymTab; }
  /// Record symbols from module \p M into this table.
  ///
  /// \param M Module whose globals and inline assembly symbols are added.
  LLVM_ABI void addModule(Module *M);

  /// Print the name of symbol \p S to \p OS.
  ///
  /// \param OS Stream that receives the symbol name.
  /// \param S Symbol whose name is printed.
  LLVM_ABI void printSymbolName(raw_ostream &OS, Symbol S) const;
  /// Flags for symbol \p S (bitwise OR of BasicSymbolRef::Flags).
  ///
  /// \param S Symbol whose flags are queried.
  /// \return The symbol's flags as a bitwise OR of BasicSymbolRef::Flags values.
  LLVM_ABI uint32_t getSymbolFlags(Symbol S) const;

  /// Parse inline ASM and collect the symbols that are defined or referenced in
  /// the current module.
  ///
  /// For each found symbol, call \p AsmSymbol with the name of the symbol found
  /// and the associated flags.
  ///
  /// \param M Module whose inline assembly is scanned for symbols.
  /// \param AsmSymbol Callback invoked for each symbol name and its flags.
  LLVM_ABI static void CollectAsmSymbols(
      const Module &M,
      function_ref<void(StringRef, object::BasicSymbolRef::Flags)> AsmSymbol);

  /// Parse inline ASM and collect the symvers directives that are defined in
  /// the current module.
  ///
  /// For each found symbol, call \p AsmSymver with the name of the symbol and
  /// its alias.
  ///
  /// \param M Module whose inline assembly is scanned for symvers directives.
  /// \param AsmSymver Callback invoked for each symbol name and its alias.
  LLVM_ABI static void
  CollectAsmSymvers(const Module &M,
                    function_ref<void(StringRef, StringRef)> AsmSymver);
};

} // end namespace llvm

#endif // LLVM_OBJECT_MODULESYMBOLTABLE_H
