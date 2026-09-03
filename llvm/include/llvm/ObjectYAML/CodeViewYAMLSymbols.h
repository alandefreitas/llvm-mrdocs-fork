//===- CodeViewYAMLSymbols.h - CodeView YAMLIO Symbol implementation ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of CodeView
// Debug Info.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_CODEVIEWYAMLSYMBOLS_H
#define LLVM_OBJECTYAML_CODEVIEWYAMLSYMBOLS_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/YAMLTraits.h"
#include <memory>

namespace llvm {
namespace CodeViewYAML {

namespace detail {

struct SymbolRecordBase;

} // end namespace detail

/// YAML representation of a CodeView symbol record.
struct SymbolRecord {
  /// Type-erased implementation of the concrete symbol kind.
  std::shared_ptr<detail::SymbolRecordBase> Symbol;

  /// Convert this YAML symbol into a CodeView symbol record.
  /// \param Allocator Allocator used for CodeView record storage.
  /// \param Container CodeView container kind (PDB or object file).
  /// \return The serialized CodeView symbol.
  LLVM_ABI codeview::CVSymbol
  toCodeViewSymbol(BumpPtrAllocator &Allocator,
                   codeview::CodeViewContainer Container) const;

  /// Build a YAML symbol from a CodeView symbol record.
  /// \param Symbol CodeView symbol to convert.
  /// \return The YAML symbol, or an error on failure.
  LLVM_ABI static Expected<SymbolRecord>
  fromCodeViewSymbol(codeview::CVSymbol Symbol);
};

} // end namespace CodeViewYAML
} // end namespace llvm

namespace llvm {
namespace yaml {

/// YAMLIO mapping traits for \c CodeViewYAML::SymbolRecord.
template <> struct LLVM_ABI MappingTraits<CodeViewYAML::SymbolRecord> {
  /// Map YAML symbol record fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj YAML symbol record being mapped.
  static void mapping(IO &IO, CodeViewYAML::SymbolRecord &Obj);
};

/// Sequences of YAML symbol records use block formatting.
template <> struct SequenceElementTraits<CodeViewYAML::SymbolRecord> {
  /// Emit sequences of YAML symbol records in block style.
  static const bool flow = false;
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_CODEVIEWYAMLSYMBOLS_H
