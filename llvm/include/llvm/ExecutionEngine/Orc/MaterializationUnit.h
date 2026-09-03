//===---- MaterializationUnit.h -- Materialization Black Box ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MaterializationUnit class and related types and operations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_MATERIALIZATIONUNIT_H
#define LLVM_EXECUTIONENGINE_ORC_MATERIALIZATIONUNIT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/CoreContainers.h"
#include "llvm/ExecutionEngine/Orc/SymbolStringPool.h"
#include "llvm/Support/Compiler.h"

namespace llvm::orc {

class MaterializationResponsibility;

/// A set of symbol definitions that can be materialized as a group or
/// individually discarded.
///
/// A MaterializationUnit represents a set of symbol definitions that can
/// be materialized as a group, or individually discarded (when
/// overriding definitions are encountered).
///
/// MaterializationUnits are used when providing lazy definitions of symbols to
/// JITDylibs. The JITDylib will call materialize when the address of a symbol
/// is requested via the lookup method. The JITDylib will call discard if a
/// stronger definition is added or already present.
class LLVM_ABI MaterializationUnit {
  friend class ExecutionSession;
  friend class JITDylib;

public:
  /// Identifier for this MaterializationUnit type.
  static char ID;

  /// Describes the symbols provided by a MaterializationUnit.
  struct Interface {
    /// Construct an empty interface.
    Interface() = default;
    /// Construct an interface from symbol flags and an optional init symbol.
    /// @param InitalSymbolFlags Map of symbol names to JIT symbol flags.
    /// @param InitSymbol Optional initializer symbol; must appear in the flags
    /// map if set.
    Interface(SymbolFlagsMap InitalSymbolFlags, SymbolStringPtr InitSymbol)
        : SymbolFlags(std::move(InitalSymbolFlags)),
          InitSymbol(std::move(InitSymbol)) {
      assert((!this->InitSymbol || this->SymbolFlags.count(this->InitSymbol)) &&
             "If set, InitSymbol should appear in InitialSymbolFlags map");
    }

    /// Map of symbol names to JIT symbol flags for this interface.
    SymbolFlagsMap SymbolFlags;
    /// Optional initializer symbol; if set, must appear in SymbolFlags.
    SymbolStringPtr InitSymbol;
  };

  /// Construct a MaterializationUnit with the given interface.
  /// @param I Interface describing the symbols provided by this unit.
  MaterializationUnit(Interface I)
      : SymbolFlags(std::move(I.SymbolFlags)),
        InitSymbol(std::move(I.InitSymbol)) {}
  /// Destroy this MaterializationUnit.
  virtual ~MaterializationUnit() = default;

  /// Return the name of this materialization unit. Useful for debugging
  /// output.
  /// @return Name of this materialization unit.
  virtual StringRef getName() const = 0;

  /// Return the set of symbols that this source provides.
  /// @return Map of symbol names to JIT symbol flags for this unit.
  const SymbolFlagsMap &getSymbols() const { return SymbolFlags; }

  /// Returns the initialization symbol for this MaterializationUnit (if any).
  /// @return Initializer symbol, or null if none is set.
  const SymbolStringPtr &getInitializerSymbol() const { return InitSymbol; }

  /// Implementations of this method should materialize all symbols
  /// in the materialzation unit, except for those that have been
  /// previously discarded.
  /// @param R Responsibility for the symbols being materialized.
  virtual void
  materialize(std::unique_ptr<MaterializationResponsibility> R) = 0;

  /// Called by JITDylibs to notify MaterializationUnits that the given symbol
  /// has been overridden.
  /// @param JD JITDylib in which the symbol was overridden.
  /// @param Name Symbol that has been overridden.
  void doDiscard(const JITDylib &JD, const SymbolStringPtr &Name) {
    SymbolFlags.erase(Name);
    if (InitSymbol == Name) {
      DEBUG_WITH_TYPE("orc", {
        dbgs() << "In " << getName() << ": discarding init symbol \""
               << *Name << "\"\n";
      });
      InitSymbol = nullptr;
    }
    discard(JD, std::move(Name));
  }

protected:
  /// Map of symbols provided by this unit to their flags.
  SymbolFlagsMap SymbolFlags;
  /// Optional initializer symbol for this unit.
  SymbolStringPtr InitSymbol;

private:
  virtual void anchor();

  /// Implementations of this method should discard the given symbol
  ///        from the source (e.g. if the source is an LLVM IR Module and the
  ///        symbol is a function, delete the function body or mark it available
  ///        externally).
  virtual void discard(const JITDylib &JD, const SymbolStringPtr &Name) = 0;
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_MATERIALIZATIONUNIT_H
