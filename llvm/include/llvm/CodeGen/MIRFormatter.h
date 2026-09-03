//===-- llvm/CodeGen/MIRFormatter.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MIRFormatter class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MIRFORMATTER_H
#define LLVM_CODEGEN_MIRFORMATTER_H

#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <optional>

namespace llvm {

class MachineFunction;
class MachineInstr;
class ModuleSlotTracker;
struct PerFunctionMIParsingState;
class Twine;
class Value;

/// MIRFormater - Interface to format MIR operand based on target
class MIRFormatter {
public:
  /// Callback type used to report MIR parsing errors.
  ///
  /// \param Loc Iterator pointing at the location of the error in the source.
  /// \param Msg Human-readable error message.
  /// \returns true if the error is fatal and parsing should stop.
  typedef function_ref<bool(StringRef::iterator Loc, const Twine &)>
      ErrorCallbackType;

  /// Construct a default MIR formatter.
  MIRFormatter() = default;
  /// Destroy a MIR formatter.
  virtual ~MIRFormatter() = default;

  /// Print a machine operand immediate with a target-specific mnemonic.
  ///
  /// Implement target specific printing for machine operand immediate value, so
  /// that we can have more meaningful mnemonic than a 64-bit integer. Passing
  /// std::nullopt to OpIdx means the index is unknown.
  ///
  /// \param OS Output stream that receives the printed immediate.
  /// \param MI Machine instruction that owns the immediate operand.
  /// \param OpIdx Optional index of the operand within \p MI; std::nullopt when
  ///              the index is unknown.
  /// \param Imm Immediate value to print.
  virtual void printImm(raw_ostream &OS, const MachineInstr &MI,
                        std::optional<unsigned> OpIdx, int64_t Imm) const {
    OS << Imm;
  }

  /// Implement target specific parsing of immediate mnemonics. The mnemonic is
  /// dot separated strings.
  ///
  /// \param OpCode Opcode of the instruction that owns the immediate.
  /// \param OpIdx Index of the immediate operand within the instruction.
  /// \param Src Immediate mnemonic text to parse.
  /// \param Imm Filled with the parsed immediate value on success.
  /// \param ErrorCallback Invoked to report parse errors.
  /// \returns true on success, false if parsing fails.
  virtual bool parseImmMnemonic(const unsigned OpCode, const unsigned OpIdx,
                                StringRef Src, int64_t &Imm,
                                ErrorCallbackType ErrorCallback) const {
    llvm_unreachable("target did not implement parsing MIR immediate mnemonic");
  }

  /// Implement target specific printing of target custom pseudo source value.
  /// Default implementation is not necessarily the correct MIR serialization
  /// format.
  ///
  /// \param OS Output stream that receives the printed pseudo source value.
  /// \param MST Module slot tracker used when printing IR references.
  /// \param PSV Custom pseudo source value to print.
  virtual void
  printCustomPseudoSourceValue(raw_ostream &OS, ModuleSlotTracker &MST,
                               const PseudoSourceValue &PSV) const {
    PSV.printCustom(OS);
  }

  /// Implement target specific parsing of target custom pseudo source value.
  ///
  /// \param Src Text of the custom pseudo source value to parse.
  /// \param MF Machine function that owns the resulting pseudo source value.
  /// \param PFS Per-function MIR parsing state used during resolution.
  /// \param PSV Filled with the parsed custom pseudo source value on success.
  /// \param ErrorCallback Invoked to report parse errors.
  /// \returns true on success, false if parsing fails.
  virtual bool parseCustomPseudoSourceValue(
      StringRef Src, MachineFunction &MF, PerFunctionMIParsingState &PFS,
      const PseudoSourceValue *&PSV, ErrorCallbackType ErrorCallback) const {
    llvm_unreachable(
        "target did not implement parsing MIR custom pseudo source value");
  }

  /// Print an IR value in MIR serialization format.
  ///
  /// Helper for target-specific printers, e.g. when printing an IR value in a
  /// custom pseudo source value.
  ///
  /// \param OS Output stream that receives the printed IR value.
  /// \param V IR value to print.
  /// \param MST Module slot tracker used to print named and numbered values.
  LLVM_ABI static void printIRValue(raw_ostream &OS, const Value &V,
                                    ModuleSlotTracker &MST);

  /// Parse an IR value from MIR serialization format.
  ///
  /// Helper for target-specific parsers, e.g. when parsing an IR value for a
  /// custom pseudo source value.
  ///
  /// \param Src Text of the IR value to parse.
  /// \param MF Machine function that owns the value context.
  /// \param PFS Per-function MIR parsing state used during resolution.
  /// \param V Filled with the parsed IR value on success.
  /// \param ErrorCallback Invoked to report parse errors.
  /// \returns true on success, false if parsing fails.
  LLVM_ABI static bool parseIRValue(StringRef Src, MachineFunction &MF,
                                    PerFunctionMIParsingState &PFS,
                                    const Value *&V,
                                    ErrorCallbackType ErrorCallback);
};

} // end namespace llvm

#endif
