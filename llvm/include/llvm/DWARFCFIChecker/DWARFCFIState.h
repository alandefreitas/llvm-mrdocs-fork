//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares DWARFCFIState class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFCFICHECKER_UNWINDINFOSTATE_H
#define LLVM_DWARFCFICHECKER_UNWINDINFOSTATE_H

#include "llvm/DebugInfo/DWARF/LowLevel/DWARFUnwindTable.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/Support/Compiler.h"
#include <optional>

namespace llvm {

/// DWARF register number used by CFI unwinding rules.
using DWARFRegNum = uint32_t;

/// Maintains a CFI unwinding row during Call Frame Information analysis.
///
/// The only way to modify the state is by updating it with a CFI directive.
class DWARFCFIState {
public:
  /// Construct a CFI state that reports issues through \p Context.
  /// \param Context Context used to emit errors and warnings from CFI updates.
  DWARFCFIState(MCContext *Context) : Context(Context), IsInitiated(false) {};

  /// Get the current unwinding row.
  ///
  /// \return The current unwinding row, or nullopt if none has been applied.
  LLVM_ABI std::optional<dwarf::UnwindRow> getCurrentUnwindRow() const;

  /// Apply \p Directive to the current CFI unwinding row.
  ///
  /// If the directive is not supported by the checker or any error happens
  /// while applying the CFI directive, a warning or error is reported to the
  /// user, and the directive is ignored, leaving the state unchanged.
  /// \param Directive CFI instruction to apply to this state.
  LLVM_ABI void update(const MCCFIInstruction &Directive);

private:
  dwarf::CFIProgram convert(MCCFIInstruction Directive);

private:
  dwarf::UnwindRow Row;
  MCContext *Context;
  bool IsInitiated;
};

} // namespace llvm

#endif
