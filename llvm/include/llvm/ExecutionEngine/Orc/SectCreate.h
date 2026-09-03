//===---- SectCreate.h -- Emulates ld64's -sectcreate option ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Emulates ld64's -sectcreate option.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SECTCREATE_H
#define LLVM_EXECUTIONENGINE_ORC_SECTCREATE_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/Support/Compiler.h"

#include <utility>

namespace llvm::orc {

/// A MaterializationUnit that emulates ld64's -sectcreate option.
///
/// Creates a named section with the given content and optionally defines
/// extra symbols at specified offsets within that section.
class LLVM_ABI SectCreateMaterializationUnit : public MaterializationUnit {
public:
  /// Describes an extra symbol to define within the created section.
  struct ExtraSymbolInfo {
    /// JIT symbol flags for the extra symbol.
    JITSymbolFlags Flags;
    /// Byte offset of the symbol within the section content.
    size_t Offset = 0;
  };

  /// Map from symbol names to extra symbol info within the section.
  using ExtraSymbolsMap = DenseMap<SymbolStringPtr, ExtraSymbolInfo>;

  /// Construct a materialization unit that creates a section with the given
  /// content.
  /// @param ObjLinkingLayer Object linking layer used to emit the section.
  /// @param SectName Name of the section to create.
  /// @param MP Memory protection flags for the section.
  /// @param Alignment Alignment requirement for the section content.
  /// @param Data Buffer holding the raw section content.
  /// @param ExtraSymbols Optional map of extra symbols to define in the
  ///        section.
  SectCreateMaterializationUnit(
      ObjectLinkingLayer &ObjLinkingLayer, std::string SectName, MemProt MP,
      uint64_t Alignment, std::unique_ptr<MemoryBuffer> Data,
      ExtraSymbolsMap ExtraSymbols = ExtraSymbolsMap())
      : MaterializationUnit(getInterface(ExtraSymbols)),
        ObjLinkingLayer(ObjLinkingLayer), SectName(std::move(SectName)), MP(MP),
        Alignment(Alignment), Data(std::move(Data)),
        ExtraSymbols(std::move(ExtraSymbols)) {}

  /// Return the name of this materialization unit.
  /// @return Name of this materialization unit.
  StringRef getName() const override { return "SectCreate"; }

  /// Materialize the section and any extra symbols into the given
  /// responsibility.
  /// @param R Materialization responsibility for the symbols being emitted.
  void materialize(std::unique_ptr<MaterializationResponsibility> R) override;

private:
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;

  static Interface getInterface(const ExtraSymbolsMap &ExtraSymbols);

  ObjectLinkingLayer &ObjLinkingLayer;
  std::string SectName;
  MemProt MP;
  uint64_t Alignment;
  std::unique_ptr<MemoryBuffer> Data;
  ExtraSymbolsMap ExtraSymbols;
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_SECTCREATE_H
