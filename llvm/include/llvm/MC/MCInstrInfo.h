//===-- llvm/MC/MCInstrInfo.h - Target Instruction Info ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the target machine instruction set.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCINSTRINFO_H
#define LLVM_MC_MCINSTRINFO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/Compiler.h"
#include <cassert>

namespace llvm {

class MCSubtargetInfo;

//---------------------------------------------------------------------------
/// Interface to description of machine instruction set.
class MCInstrInfo {
public:
  /// Function pointer type for complex instruction deprecation checks.
  using ComplexDeprecationPredicate = bool (*)(MCInst &,
                                               const MCSubtargetInfo &,
                                               std::string &);

private:
  const MCInstrDesc *LastDesc;      // Raw array to allow static init'n
  const unsigned *InstrNameIndices; // Array for name indices in InstrNameData
  const char *InstrNameData;        // Instruction name string pool
  // Subtarget feature that an instruction is deprecated on, if any
  // -1 implies this is not deprecated by any single feature. It may still be
  // deprecated due to a "complex" reason, below.
  const uint8_t *DeprecatedFeatures;
  // A complex method to determine if a certain instruction is deprecated or
  // not, and return the reason for deprecation.
  ComplexDeprecationPredicate ComplexDeprecationInfo;
  unsigned NumOpcodes;              // Number of entries in the desc array

protected:
  /// Flattened [NumHwModes][NumRegClassByHwModes] register-class lookup tables.
  const int16_t *RegClassByHwModeTables;
  /// Number of register-class entries per hardware mode in the lookup tables.
  int16_t NumRegClassByHwModes;

public:
  /// Initialize MCInstrInfo, called by TableGen auto-generated routines.
  /// *DO NOT USE*.
  ///
  /// \param D - Array of instruction descriptors.
  /// \param NI - Array of indices into the instruction name string pool.
  /// \param ND - Instruction name string pool data.
  /// \param DF - Per-opcode deprecated feature IDs, or -1 if none.
  /// \param CDI - Optional complex deprecation predicate.
  /// \param NO - Number of opcodes / descriptors.
  /// \param RCHWTables - Optional HwMode register-class lookup tables.
  /// \param NumRegClassByHwMode - Register-class entries per HwMode.
  void InitMCInstrInfo(const MCInstrDesc *D, const unsigned *NI, const char *ND,
                       const uint8_t *DF, ComplexDeprecationPredicate CDI,
                       unsigned NO, const int16_t *RCHWTables = nullptr,
                       int16_t NumRegClassByHwMode = 0) {
    LastDesc = D + NO - 1;
    InstrNameIndices = NI;
    InstrNameData = ND;
    DeprecatedFeatures = DF;
    ComplexDeprecationInfo = CDI;
    NumOpcodes = NO;
    RegClassByHwModeTables = RCHWTables;
    NumRegClassByHwModes = NumRegClassByHwMode;
  }

  /// Return the number of instruction opcodes described by this info.
  ///
  /// \return Number of instruction opcodes in this info.
  unsigned getNumOpcodes() const { return NumOpcodes; }

  /// Return the register-class lookup row for the given hardware mode.
  ///
  /// \param ModeId - Hardware mode whose register-class table to return.
  /// \return Pointer to the register-class lookup row for \p ModeId.
  const int16_t *getRegClassByHwModeTable(unsigned ModeId) const {
    assert(RegClassByHwModeTables && NumRegClassByHwModes != 0 &&
           "MCInstrInfo not properly initialized");
    return &RegClassByHwModeTables[ModeId * NumRegClassByHwModes];
  }

  /// Return the register class ID for \p OpInfo under \p HwModeId.
  ///
  /// In general TargetInstrInfo's version which is already specialized to the
  /// subtarget should be used.
  ///
  /// \param OpInfo - Operand info whose register class should be resolved.
  /// \param HwModeId - Active hardware mode used for HwMode-dependent classes.
  /// \return Register class ID for the operand under the given hardware mode.
  int16_t getOpRegClassID(const MCOperandInfo &OpInfo,
                          unsigned HwModeId) const {
    int16_t RegClass = OpInfo.RegClass;
    if (OpInfo.isLookupRegClassByHwMode())
      RegClass = getRegClassByHwModeTable(HwModeId)[RegClass];
    return RegClass;
  }

  /// Return the machine instruction descriptor that corresponds to the
  /// specified instruction opcode.
  ///
  /// \param Opcode - Instruction opcode whose descriptor to return.
  /// \return Descriptor for the given instruction opcode.
  const MCInstrDesc &get(unsigned Opcode) const {
    assert(Opcode < NumOpcodes && "Invalid opcode!");
    // The table is indexed backwards from the last entry.
    return *(LastDesc - Opcode);
  }

  /// Returns the name for the instructions with the given opcode.
  ///
  /// \param Opcode - Instruction opcode whose name to return.
  /// \return Name of the instruction with the given opcode.
  StringRef getName(unsigned Opcode) const {
    assert(Opcode < NumOpcodes && "Invalid opcode!");
    return StringRef(&InstrNameData[InstrNameIndices[Opcode]]);
  }

  /// Returns true if a certain instruction is deprecated and if so
  /// returns the reason in \p Info.
  ///
  /// \param MI - Instruction to check for deprecation.
  /// \param STI - Subtarget info used to evaluate deprecation.
  /// \param Info - Filled with the deprecation reason when returning true.
  /// \return True if the instruction is deprecated.
  LLVM_ABI bool getDeprecatedInfo(MCInst &MI, const MCSubtargetInfo &STI,
                                  std::string &Info) const;
};

} // End llvm namespace

#endif
