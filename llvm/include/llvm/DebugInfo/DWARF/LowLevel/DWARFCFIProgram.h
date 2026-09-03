//===- DWARFCFIProgram.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_LOWLEVEL_DWARFCFIPROGRAM_H
#define LLVM_DEBUGINFO_DWARF_LOWLEVEL_DWARFCFIPROGRAM_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/iterator.h"
#include "llvm/DebugInfo/DWARF/LowLevel/DWARFDataExtractorSimple.h"
#include "llvm/DebugInfo/DWARF/LowLevel/DWARFExpression.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"
#include <vector>

namespace llvm {

namespace dwarf {

/// A sequence of Call Frame Information instructions that map PC to frame state.
///
/// When read in order, these instructions construct a table mapping PC to frame
/// state. This can also be referred to as "CFI rules" in DWARF literature to
/// avoid confusion with computer programs in the broader sense, and in this
/// context each instruction would be a rule to establish the mapping. Refer to
/// pg. 172 in the DWARF5 manual, "6.4.1 Structure of Call Frame Information".
class CFIProgram {
public:
  /// Maximum number of operands a single CFI instruction may have.
  static constexpr size_t MaxOperands = 3;
  /// Operand values for a CFI instruction, up to MaxOperands entries.
  typedef SmallVector<uint64_t, MaxOperands> Operands;

  /// A single DWARF CFI instruction with opcode and optional operands.
  ///
  /// An instruction consists of a DWARF CFI opcode and an optional sequence of
  /// operands. If it refers to an expression, then this expression has its own
  /// sequence of operations and operands handled separately by DWARFExpression.
  struct Instruction {
    /// Construct an instruction with the given DWARF CFI opcode.
    ///
    /// \param Opcode DW_CFA_* opcode for this instruction.
    Instruction(uint8_t Opcode) : Opcode(Opcode) {}

    /// DWARF CFI opcode (DW_CFA_*) for this instruction.
    uint8_t Opcode;
    /// Operand values associated with this instruction.
    Operands Ops;
    /// Associated DWARF expression when this instruction refers to one.
    std::optional<DWARFExpression> Expression;

    /// Return operand \p OperandIdx interpreted as an unsigned value.
    ///
    /// \param CFIP Program providing code/data alignment factors for decoding.
    /// \param OperandIdx Zero-based index of the operand to read.
    /// \returns The unsigned operand value, or an error if the index or type
    ///          is invalid.
    LLVM_ABI Expected<uint64_t> getOperandAsUnsigned(const CFIProgram &CFIP,
                                                     uint32_t OperandIdx) const;

    /// Return operand \p OperandIdx interpreted as a signed value.
    ///
    /// \param CFIP Program providing code/data alignment factors for decoding.
    /// \param OperandIdx Zero-based index of the operand to read.
    /// \returns The signed operand value, or an error if the index or type is
    ///          invalid.
    LLVM_ABI Expected<int64_t> getOperandAsSigned(const CFIProgram &CFIP,
                                                  uint32_t OperandIdx) const;
  };

  /// Contiguous list of CFI instructions in program order.
  using InstrList = std::vector<Instruction>;
  /// Mutable iterator over instructions in this program.
  using iterator = InstrList::iterator;
  /// Const iterator over instructions in this program.
  using const_iterator = InstrList::const_iterator;

  /// Return a mutable iterator to the first instruction.
  ///
  /// \returns Mutable iterator to the first instruction.
  iterator begin() { return Instructions.begin(); }
  /// Return a const iterator to the first instruction.
  ///
  /// \returns Const iterator to the first instruction.
  const_iterator begin() const { return Instructions.begin(); }
  /// Return a mutable iterator past the last instruction.
  ///
  /// \returns Mutable iterator past the last instruction.
  iterator end() { return Instructions.end(); }
  /// Return a const iterator past the last instruction.
  ///
  /// \returns Const iterator past the last instruction.
  const_iterator end() const { return Instructions.end(); }

  /// Return the number of instructions in this program.
  ///
  /// \returns Number of instructions in this program.
  unsigned size() const { return (unsigned)Instructions.size(); }
  /// Return true if this program contains no instructions.
  ///
  /// \returns True if this program contains no instructions.
  bool empty() const { return Instructions.empty(); }
  /// Return the code alignment factor used by this program.
  ///
  /// \returns Code alignment factor used by this program.
  uint64_t codeAlign() const { return CodeAlignmentFactor; }
  /// Return the data alignment factor used by this program.
  ///
  /// \returns Data alignment factor used by this program.
  int64_t dataAlign() const { return DataAlignmentFactor; }
  /// Return the architecture triple associated with this program.
  ///
  /// \returns Architecture triple associated with this program.
  Triple::ArchType triple() const { return Arch; }

  /// Construct a CFI program with the given alignment factors and architecture.
  ///
  /// \param CodeAlignmentFactor Factor applied to factored code offsets.
  /// \param DataAlignmentFactor Factor applied to factored data offsets.
  /// \param Arch Target architecture for architecture-specific opcodes.
  CFIProgram(uint64_t CodeAlignmentFactor, int64_t DataAlignmentFactor,
             Triple::ArchType Arch)
      : CodeAlignmentFactor(CodeAlignmentFactor),
        DataAlignmentFactor(DataAlignmentFactor), Arch(Arch) {}

  /// Parse and store a sequence of CFI instructions from \p Data.
  ///
  /// Starts at \p *Offset and ends at \p EndOffset. \p *Offset is updated to
  /// \p EndOffset upon successful parsing, or indicates the offset where a
  /// problem occurred in case an error is returned.
  ///
  /// \param Data Extractor providing the CFI instruction bytes.
  /// \param Offset On entry, start offset; on exit, end offset or error
  ///        position.
  /// \param EndOffset Exclusive end offset of the instruction sequence.
  /// \returns Success, or an error describing a parse failure.
  template <typename T>
  Error parse(DWARFDataExtractorBase<T> &Data, uint64_t *Offset,
              uint64_t EndOffset) {
    // See DWARF standard v3, section 7.23
    const uint8_t DWARF_CFI_PRIMARY_OPCODE_MASK = 0xc0;
    const uint8_t DWARF_CFI_PRIMARY_OPERAND_MASK = 0x3f;

    DataExtractor::Cursor C(*Offset);
    while (C && C.tell() < EndOffset) {
      uint8_t Opcode = Data.getRelocatedValue(C, 1);
      if (!C)
        break;

      // Some instructions have a primary opcode encoded in the top bits.
      if (uint8_t Primary = Opcode & DWARF_CFI_PRIMARY_OPCODE_MASK) {
        // If it's a primary opcode, the first operand is encoded in the
        // bottom bits of the opcode itself.
        uint64_t Op1 = Opcode & DWARF_CFI_PRIMARY_OPERAND_MASK;
        switch (Primary) {
        case DW_CFA_advance_loc:
        case DW_CFA_restore:
          addInstruction(Primary, Op1);
          break;
        case DW_CFA_offset:
          addInstruction(Primary, Op1, Data.getULEB128(C));
          break;
        default:
          llvm_unreachable("invalid primary CFI opcode");
        }
        continue;
      }

      // Extended opcode - its value is Opcode itself.
      switch (Opcode) {
      default:
        return createStringError(errc::illegal_byte_sequence,
                                 "invalid extended CFI opcode 0x%" PRIx8,
                                 Opcode);
      case DW_CFA_nop:
      case DW_CFA_remember_state:
      case DW_CFA_restore_state:
      case DW_CFA_GNU_window_save:
      case DW_CFA_AARCH64_negate_ra_state_with_pc:
        // No operands
        addInstruction(Opcode);
        break;
      case DW_CFA_AARCH64_set_ra_state: {
        uint64_t RAState = Data.getULEB128(C);
        uint64_t FactoredOffset = static_cast<uint64_t>(Data.getSLEB128(C));
        addInstruction(Opcode, RAState, FactoredOffset);
        break;
      }
      case DW_CFA_set_loc:
        // Operands: Address
        addInstruction(Opcode, Data.getRelocatedAddress(C));
        break;
      case DW_CFA_advance_loc1:
        // Operands: 1-byte delta
        addInstruction(Opcode, Data.getRelocatedValue(C, 1));
        break;
      case DW_CFA_advance_loc2:
        // Operands: 2-byte delta
        addInstruction(Opcode, Data.getRelocatedValue(C, 2));
        break;
      case DW_CFA_advance_loc4:
        // Operands: 4-byte delta
        addInstruction(Opcode, Data.getRelocatedValue(C, 4));
        break;
      case DW_CFA_restore_extended:
      case DW_CFA_undefined:
      case DW_CFA_same_value:
      case DW_CFA_def_cfa_register:
      case DW_CFA_def_cfa_offset:
      case DW_CFA_GNU_args_size:
        // Operands: ULEB128
        addInstruction(Opcode, Data.getULEB128(C));
        break;
      case DW_CFA_def_cfa_offset_sf:
        // Operands: SLEB128
        addInstruction(Opcode, Data.getSLEB128(C));
        break;
      case DW_CFA_LLVM_def_aspace_cfa:
      case DW_CFA_LLVM_def_aspace_cfa_sf: {
        auto RegNum = Data.getULEB128(C);
        auto CfaOffset = Opcode == DW_CFA_LLVM_def_aspace_cfa
                             ? Data.getULEB128(C)
                             : Data.getSLEB128(C);
        auto AddressSpace = Data.getULEB128(C);
        addInstruction(Opcode, RegNum, CfaOffset, AddressSpace);
        break;
      }
      case DW_CFA_offset_extended:
      case DW_CFA_register:
      case DW_CFA_def_cfa:
      case DW_CFA_val_offset: {
        // Operands: ULEB128, ULEB128
        // Note: We can not embed getULEB128 directly into function
        // argument list. getULEB128 changes Offset and order of evaluation
        // for arguments is unspecified.
        uint64_t op1 = Data.getULEB128(C);
        uint64_t op2 = Data.getULEB128(C);
        addInstruction(Opcode, op1, op2);
        break;
      }
      case DW_CFA_offset_extended_sf:
      case DW_CFA_def_cfa_sf:
      case DW_CFA_val_offset_sf: {
        // Operands: ULEB128, SLEB128
        // Note: see comment for the previous case
        uint64_t op1 = Data.getULEB128(C);
        uint64_t op2 = (uint64_t)Data.getSLEB128(C);
        addInstruction(Opcode, op1, op2);
        break;
      }
      case DW_CFA_def_cfa_expression: {
        uint64_t ExprLength = Data.getULEB128(C);
        addInstruction(Opcode, 0);
        StringRef Expression = Data.getBytes(C, ExprLength);

        DataExtractor Extractor(Expression, Data.isLittleEndian());
        // Note. We do not pass the DWARF format to DWARFExpression, because
        // DW_OP_call_ref, the only operation which depends on the format, is
        // prohibited in call frame instructions, see sec. 6.4.2 in DWARFv5.
        Instructions.back().Expression =
            DWARFExpression(Extractor, Data.getAddressSize());
        break;
      }
      case DW_CFA_expression:
      case DW_CFA_val_expression: {
        uint64_t RegNum = Data.getULEB128(C);
        addInstruction(Opcode, RegNum, 0);

        uint64_t BlockLength = Data.getULEB128(C);
        StringRef Expression = Data.getBytes(C, BlockLength);
        DataExtractor Extractor(Expression, Data.isLittleEndian());
        // Note. We do not pass the DWARF format to DWARFExpression, because
        // DW_OP_call_ref, the only operation which depends on the format, is
        // prohibited in call frame instructions, see sec. 6.4.2 in DWARFv5.
        Instructions.back().Expression =
            DWARFExpression(Extractor, Data.getAddressSize());
        break;
      }
      }
    }

    *Offset = C.tell();
    return C.takeError();
  }

  /// Append an existing instruction to this program.
  ///
  /// \param I Instruction to append.
  void addInstruction(const Instruction &I) { Instructions.push_back(I); }

  /// Get a DWARF CFI call frame string for the given DW_CFA opcode.
  ///
  /// \param Opcode DW_CFA_* opcode to name.
  /// \returns String name of the given DW_CFA opcode.
  LLVM_ABI StringRef callFrameString(unsigned Opcode) const;

  /// Types of operands to CFI instructions.
  ///
  /// In DWARF, this type is implicitly tied to a CFI instruction opcode and
  /// thus this type doesn't need to be explicitly written to the file (this is
  /// not a DWARF encoding). The relationship of instrs to operand types can
  /// be obtained from getOperandTypes() and is only used to simplify
  /// instruction printing and error messages.
  enum OperandType {
    OT_Unset, ///< Operand type has not been set.
    OT_None, ///< Instruction takes no operand in this slot.
    OT_Address, ///< Absolute address operand.
    OT_Offset, ///< Unfactored byte offset.
    OT_FactoredCodeOffset, ///< Code offset scaled by the code alignment factor.
    OT_SignedFactCodeOffset, ///< Signed code offset scaled by code alignment.
    OT_SignedFactDataOffset, ///< Signed data offset scaled by data alignment.
    OT_UnsignedFactDataOffset, ///< Unsigned data offset scaled by data alignment.
    OT_Register, ///< DWARF register number.
    OT_AddressSpace, ///< Address space identifier.
    OT_Expression, ///< Embedded DWARF expression.
    OT_RAState, ///< Return-address signing state.
  };

  /// Get the OperandType as a "const char *".
  ///
  /// \param OT Operand type to convert to a string.
  /// \returns C string describing the given operand type.
  LLVM_ABI static const char *operandTypeString(OperandType OT);

  /// Retrieve the array describing the types of operands according to the enum
  /// above. This is indexed by opcode.
  ///
  /// \returns Array of per-opcode operand type descriptors.
  LLVM_ABI static ArrayRef<OperandType[MaxOperands]> getOperandTypes();

  /// Convenience method to add a new instruction with the given opcode.
  ///
  /// \param Opcode DW_CFA_* opcode for the new instruction.
  void addInstruction(uint8_t Opcode) {
    Instructions.push_back(Instruction(Opcode));
  }

  /// Add a new single-operand instruction.
  ///
  /// \param Opcode DW_CFA_* opcode for the new instruction.
  /// \param Operand1 First operand value.
  void addInstruction(uint8_t Opcode, uint64_t Operand1) {
    Instructions.push_back(Instruction(Opcode));
    Instructions.back().Ops.push_back(Operand1);
  }

  /// Add a new instruction that has two operands.
  ///
  /// \param Opcode DW_CFA_* opcode for the new instruction.
  /// \param Operand1 First operand value.
  /// \param Operand2 Second operand value.
  void addInstruction(uint8_t Opcode, uint64_t Operand1, uint64_t Operand2) {
    Instructions.push_back(Instruction(Opcode));
    Instructions.back().Ops.push_back(Operand1);
    Instructions.back().Ops.push_back(Operand2);
  }

  /// Add a new instruction that has three operands.
  ///
  /// \param Opcode DW_CFA_* opcode for the new instruction.
  /// \param Operand1 First operand value.
  /// \param Operand2 Second operand value.
  /// \param Operand3 Third operand value.
  void addInstruction(uint8_t Opcode, uint64_t Operand1, uint64_t Operand2,
                      uint64_t Operand3) {
    Instructions.push_back(Instruction(Opcode));
    Instructions.back().Ops.push_back(Operand1);
    Instructions.back().Ops.push_back(Operand2);
    Instructions.back().Ops.push_back(Operand3);
  }

private:
  std::vector<Instruction> Instructions;
  const uint64_t CodeAlignmentFactor;
  const int64_t DataAlignmentFactor;
  Triple::ArchType Arch;
};

} // end namespace dwarf

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_LOWLEVEL_DWARFCFIPROGRAM_H
