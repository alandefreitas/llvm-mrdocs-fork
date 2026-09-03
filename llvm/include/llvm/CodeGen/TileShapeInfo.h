//===- llvm/CodeGen/TileShapeInfo.h - ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Shape utility for AMX.
/// AMX hardware requires to config the shape of tile data register before use.
/// The 2D shape includes row and column. In AMX intrinsics interface the shape
/// is passed as 1st and 2nd parameter and they are lowered as the 1st and 2nd
/// machine operand of AMX pseudo instructions. ShapeT class is to facilitate
/// tile config and register allocator. The row and column are machine operand
/// of AMX pseudo instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TILESHAPEINFO_H
#define LLVM_CODEGEN_TILESHAPEINFO_H

#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {

/// 2D tile shape (row and column) for AMX tile configuration.
///
/// Holds the machine operands that define a tile's row and column dimensions,
/// optionally with deduced immediate values used during tile config and
/// register allocation.
class ShapeT {
public:
  /// Construct a shape from row and column machine operands.
  ///
  /// When \p MRI is non-null, immediately deduces immediate shape values from
  /// the defining move-immediate instructions of \p Row and \p Col.
  ///
  /// \param Row Machine operand for the tile row dimension.
  /// \param Col Machine operand for the tile column dimension.
  /// \param MRI Optional register info used to deduce immediate shapes.
  ShapeT(MachineOperand *Row, MachineOperand *Col,
         const MachineRegisterInfo *MRI = nullptr)
      : Row(Row), Col(Col) {
    if (MRI)
      deduceImm(MRI);
  }
  /// Construct an invalid shape with null operands and unset immediates.
  ShapeT()
      : Row(nullptr), Col(nullptr), RowImm(InvalidImmShape),
        ColImm(InvalidImmShape) {}
  /// Return true if this shape equals \p Shape by register or immediate.
  ///
  /// Shapes compare equal when both have non-null row/column operands with
  /// matching registers, or when both have valid deduced immediates that
  /// match. Otherwise returns false.
  ///
  /// \param Shape Shape to compare against.
  /// \return True if the shapes are equal by register or immediate.
  bool operator==(const ShapeT &Shape) const {
    MachineOperand *R = Shape.Row;
    MachineOperand *C = Shape.Col;
    if (!R || !C)
      return false;
    if (!Row || !Col)
      return false;
    if (Row->getReg() == R->getReg() && Col->getReg() == C->getReg())
      return true;
    if ((RowImm != InvalidImmShape) && (ColImm != InvalidImmShape))
      return RowImm == Shape.getRowImm() && ColImm == Shape.getColImm();
    return false;
  }

  /// Return true if this shape is not equal to \p Shape.
  ///
  /// \param Shape Shape to compare against.
  /// \return True if the shapes are not equal.
  bool operator!=(const ShapeT &Shape) const { return !(*this == Shape); }

  /// Return the machine operand for the tile row dimension.
  ///
  /// \return The machine operand for the tile row dimension.
  MachineOperand *getRow() const { return Row; }
  /// Return the machine operand for the tile column dimension.
  ///
  /// \return The machine operand for the tile column dimension.
  MachineOperand *getCol() const { return Col; }

  /// Return the deduced immediate value of the row dimension, or -1 if unset.
  ///
  /// \return The deduced row immediate, or -1 if unset.
  int64_t getRowImm() const { return RowImm; }
  /// Return the deduced immediate value of the column dimension, or -1 if unset.
  ///
  /// \return The deduced column immediate, or -1 if unset.
  int64_t getColImm() const { return ColImm; }

  /// Return true if both row and column machine operands are non-null.
  ///
  /// \return True if both row and column operands are non-null.
  bool isValid() { return (Row != nullptr) && (Col != nullptr); }

  /// Deduce immediate row and column values from defining move-immediates.
  ///
  /// Walks defs of the row and column registers via \p MRI and records the
  /// immediate from any move-immediate instruction. Implicit immediates are
  /// treated as invalid shapes.
  ///
  /// \param MRI Register info used to find defining operands.
  void deduceImm(const MachineRegisterInfo *MRI) {
    // All def must be the same value, otherwise it is invalid MIs.
    // Find the immediate.
    // TODO copy propagation.
    auto GetImm = [&](Register Reg) {
      int64_t Imm = InvalidImmShape;
      for (const MachineOperand &DefMO : MRI->def_operands(Reg)) {
        const auto *MI = DefMO.getParent();
        if (MI->isMoveImmediate()) {
          if (MI->getOperand(1).isImm()) {
            Imm = MI->getOperand(1).getImm();
          } else {
            assert(MI->getOperand(1).isImplicit() &&
                   "Operand 1 is assumed to be implicit.");
            // The implicit immediate can vary (MOV32r0, MOV32r1, MOV32r_1,
            // ...) but in any case, is not a valid shape.
          }
          break;
        }
      }
      return Imm;
    };
    RowImm = GetImm(Row->getReg());
    ColImm = GetImm(Col->getReg());
  }

private:
  static constexpr int64_t InvalidImmShape = -1;
  MachineOperand *Row;
  MachineOperand *Col;
  int64_t RowImm = -1;
  int64_t ColImm = -1;
};

} // namespace llvm

#endif
