//===-- SymbolDumpDelegate.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SYMBOLDUMPDELEGATE_H
#define LLVM_DEBUGINFO_CODEVIEW_SYMBOLDUMPDELEGATE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/SymbolVisitorDelegate.h"
#include <cstdint>

namespace llvm {
namespace codeview {

/// Delegate for object-file-specific hooks used when dumping CodeView symbols.
class SymbolDumpDelegate : public SymbolVisitorDelegate {
public:
  /// Destroy a symbol dump delegate.
  ~SymbolDumpDelegate() override = default;

  /// Print a relocated field with the given label and optional resolved symbol.
  ///
  /// \param Label Field name printed before the relocated value.
  /// \param RelocOffset Offset within the section where the relocation applies.
  /// \param Offset Unrelocated field value to print alongside the relocation.
  /// \param RelocSym Optional out-parameter set to the resolved symbol name.
  virtual void printRelocatedField(StringRef Label, uint32_t RelocOffset,
                                   uint32_t Offset,
                                   StringRef *RelocSym = nullptr) = 0;
  /// Print a binary block and any relocations that apply within it.
  ///
  /// \param Label Label printed before the binary block.
  /// \param Block Bytes of the block to dump with relocation annotations.
  virtual void printBinaryBlockWithRelocs(StringRef Label,
                                          ArrayRef<uint8_t> Block) = 0;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_SYMBOLDUMPDELEGATE_H
