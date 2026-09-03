//===- FaultMaps.h - The "FaultMaps" section --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_FAULTMAPS_H
#define LLVM_CODEGEN_FAULTMAPS_H

#include "llvm/MC/MCSymbol.h"
#include <map>
#include <vector>

namespace llvm {

class AsmPrinter;
class MCExpr;

/// Collects potentially-faulting operations and serializes the __llvm_faultmaps
/// section.
class FaultMaps {
public:
  /// Kinds of potentially-faulting operations recorded in a fault map.
  enum FaultKind {
    FaultingLoad = 1, ///< A load that may fault.
    FaultingLoadStore, ///< A load-store that may fault.
    FaultingStore, ///< A store that may fault.
    FaultKindMax ///< Sentinel one past the last valid fault kind.
  };

  /// Construct a fault maps collector bound to AsmPrinter \p AP.
  ///
  /// \param AP AsmPrinter used to emit the fault map section.
  LLVM_ABI explicit FaultMaps(AsmPrinter &AP);

  /// Return a printable name for fault kind \p FT.
  ///
  /// \param FT Fault kind to convert to a string.
  /// \return Null-terminated string naming \p FT.
  LLVM_ABI static const char *faultTypeToString(FaultKind FT);

  /// Record a faulting operation of kind \p FaultTy at \p FaultingLabel with
  /// handler \p HandlerLabel.
  ///
  /// \param FaultTy Kind of potentially-faulting operation.
  /// \param FaultingLabel Label at the faulting instruction.
  /// \param HandlerLabel Label of the fault handler.
  LLVM_ABI void recordFaultingOp(FaultKind FaultTy,
                                 const MCSymbol *FaultingLabel,
                                 const MCSymbol *HandlerLabel);
  /// Emit collected fault map data into the __llvm_faultmaps section.
  LLVM_ABI void serializeToFaultMapSection();
  /// Clear all recorded function fault infos.
  void reset() {
    FunctionInfos.clear();
  }

private:
  static const char *WFMP;

  struct FaultInfo {
    FaultKind Kind = FaultKindMax;
    const MCExpr *FaultingOffsetExpr = nullptr;
    const MCExpr *HandlerOffsetExpr = nullptr;

    FaultInfo() = default;

    explicit FaultInfo(FaultMaps::FaultKind Kind, const MCExpr *FaultingOffset,
                       const MCExpr *HandlerOffset)
        : Kind(Kind), FaultingOffsetExpr(FaultingOffset),
          HandlerOffsetExpr(HandlerOffset) {}
  };

  using FunctionFaultInfos = std::vector<FaultInfo>;

  // We'd like to keep a stable iteration order for FunctionInfos to help
  // FileCheck based testing.
  struct MCSymbolComparator {
    bool operator()(const MCSymbol *LHS, const MCSymbol *RHS) const {
      return LHS->getName() < RHS->getName();
    }
  };

  std::map<const MCSymbol *, FunctionFaultInfos, MCSymbolComparator>
      FunctionInfos;
  AsmPrinter &AP;

  void emitFunctionInfo(const MCSymbol *FnLabel, const FunctionFaultInfos &FFI);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_FAULTMAPS_H
