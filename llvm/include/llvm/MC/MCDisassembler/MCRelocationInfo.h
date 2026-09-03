//===- llvm/MC/MCRelocationInfo.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCRelocationInfo class, which provides methods to
// create MCExprs from relocations, either found in an object::ObjectFile
// (object::RelocationRef), or provided through the C API.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCDISASSEMBLER_MCRELOCATIONINFO_H
#define LLVM_MC_MCDISASSEMBLER_MCRELOCATIONINFO_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class MCContext;
class MCExpr;

/// Create MCExprs from relocations found in an object file.
class LLVM_ABI MCRelocationInfo {
protected:
  /// Context used to create symbolic MCExprs from relocations.
  MCContext &Ctx;

public:
  /// Construct relocation info bound to context \p Ctx.
  ///
  /// \param Ctx - Context used to create symbolic MCExprs.
  MCRelocationInfo(MCContext &Ctx);
  /// Deleted copy constructor.
  ///
  /// \param Other - Unused; copy construction is deleted.
  MCRelocationInfo(const MCRelocationInfo &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other - Unused; copy assignment is deleted.
  MCRelocationInfo &operator=(const MCRelocationInfo &Other) = delete;
  /// Destroy the relocation info.
  virtual ~MCRelocationInfo();

  /// Create an MCExpr for the target-specific \p VariantKind.
  /// The VariantKinds are defined in llvm-c/Disassembler.h.
  /// Used by MCExternalSymbolizer.
  /// \param SubExpr - Base expression to wrap with the variant kind.
  /// \param VariantKind - Target-specific variant kind from llvm-c/Disassembler.h.
  /// \returns If possible, an MCExpr corresponding to VariantKind, else 0.
  virtual const MCExpr *createExprForCAPIVariantKind(const MCExpr *SubExpr,
                                                     unsigned VariantKind);
};

} // end namespace llvm

#endif // LLVM_MC_MCDISASSEMBLER_MCRELOCATIONINFO_H
