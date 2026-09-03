//===------ AbsoluteSymbols.h - Absolute symbols utilities ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// absoluteSymbols function and related utilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_ABSOLUTESYMBOLS_H
#define LLVM_EXECUTIONENGINE_ORC_ABSOLUTESYMBOLS_H

#include "llvm/ExecutionEngine/Orc/MaterializationUnit.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
/// On-Request Compilation (ORC) JIT APIs.
///
/// ProxySpec types and factories that implement ORC Proxies via SPS.
namespace orc {

/// A MaterializationUnit implementation for pre-existing absolute symbols.
///
/// All symbols will be resolved and marked ready as soon as the unit is
/// materialized.
class LLVM_ABI AbsoluteSymbolsMaterializationUnit : public MaterializationUnit {
public:
  /// Construct a materialization unit for the given absolute symbols.
  /// @param Symbols Map of symbol names to absolute definitions.
  AbsoluteSymbolsMaterializationUnit(SymbolMap Symbols);

  /// Return the name of this materialization unit.
  /// @return The name of this materialization unit.
  StringRef getName() const override;

private:
  void materialize(std::unique_ptr<MaterializationResponsibility> R) override;
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;
  static MaterializationUnit::Interface extractFlags(const SymbolMap &Symbols);

  SymbolMap Symbols;
};

/// Create an AbsoluteSymbolsMaterializationUnit with the given symbols.
/// Useful for inserting absolute symbols into a JITDylib. E.g.:
/// \code{.cpp}
///   JITDylib &JD = ...;
///   SymbolStringPtr Foo = ...;
///   ExecutorSymbolDef FooSym = ...;
///   if (auto Err = JD.define(absoluteSymbols({
///         { Foo, FooSym },
///         { Bar, BarSym }
///       })))
///     return Err;
/// \endcode
/// @param Symbols Map of symbol names to absolute definitions.
/// @return An AbsoluteSymbolsMaterializationUnit for the given symbols.
inline std::unique_ptr<AbsoluteSymbolsMaterializationUnit>
absoluteSymbols(SymbolMap Symbols) {
  return std::make_unique<AbsoluteSymbolsMaterializationUnit>(
      std::move(Symbols));
}

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_ABSOLUTESYMBOLS_H
