//===- MatrixUtils.h - Utilities to lower matrix intrinsics -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for generating tiled loops for matrix operations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_MATRIXUTILS_H
#define LLVM_TRANSFORMS_UTILS_MATRIXUTILS_H

#include "llvm/ADT/StringRef.h"

namespace llvm {
class DomTreeUpdater;
class BasicBlock;
class ConstantInt;
class Value;
class Loop;
class LoopInfo;
class IRBuilderBase;

/// Helper to create tiled IR loop nests for matrix operations.
///
/// Generates nests of the form:
///   for ColumnLoop.Index = 0..NumColumns
///     for RowLoop.Index = 0..NumRows
///       for KLoop.Index = 0..NumInner
struct TileInfo {
  /// Number of rows of the matrix.
  unsigned NumRows;

  /// Number of columns of the matrix.
  unsigned NumColumns;

  /// Number of columns of the first matrix of a multiply /
  /// number of rows of the second matrix of a multiply.
  unsigned NumInner;

  /// Number of rows/columns in a tile.
  unsigned TileSize = -1;

  /// Properties of a single loop used when generating the tiled loop nest.
  struct MatrixLoop {
    /// The index updated on every iteration.
    Value *Index = nullptr;
    /// Header basic block of the loop.
    BasicBlock *Header = nullptr;
    /// Latch basic block of the loop.
    BasicBlock *Latch = nullptr;
  };

  /// The loop iterating on the rows.
  MatrixLoop RowLoop;
  /// The loop iterating on the columns.
  MatrixLoop ColumnLoop;
  /// The loop iterating on k (inner dimension).
  MatrixLoop KLoop;

  /// Construct tile info for a matrix of the given dimensions.
  ///
  /// \param NumRows Number of rows of the matrix.
  /// \param NumColumns Number of columns of the matrix.
  /// \param NumInner Inner dimension (columns of first / rows of second).
  /// \param TileSize Number of rows/columns in a tile.
  TileInfo(unsigned NumRows, unsigned NumColumns, unsigned NumInner,
           unsigned TileSize)
      : NumRows(NumRows), NumColumns(NumColumns), NumInner(NumInner),
        TileSize(TileSize) {}

  /// Creates an IR loop nests for tiling of the form below. Returns the block
  /// for the inner loop body and sets {Column,Row,Inner}LoopHeader/Latch
  /// fields.
  ///
  /// for ColumnLoop.Index = 0..NumColumns
  ///   for RowLoop.Index = 0..NumRows
  ///     for InnerLoop.Index = 0..NumInner
  ///
  /// \param Start Preheader block from which the column loop is entered.
  /// \param End Exit block of the tiled loop nest.
  /// \param B IR builder used to emit the loop instructions.
  /// \param DTU Dominator tree updater for the inserted blocks.
  /// \param LI Loop info to register the newly created loops.
  /// \return The basic block for the inner loop body.
  LLVM_ABI BasicBlock *CreateTiledLoops(BasicBlock *Start, BasicBlock *End,
                                        IRBuilderBase &B, DomTreeUpdater &DTU,
                                        LoopInfo &LI);

private:
  /// Creates a new loop with header, body and latch blocks that iterates from
  /// [0, Bound). Updates \p Preheader to branch to the new header and uses \p
  /// Exit as exit block.  Adds the new loop blocks to \L and applies dominator
  /// tree updates to \p DTU.
  static BasicBlock *CreateLoop(BasicBlock *Preheader, BasicBlock *Exit,
                                ConstantInt *Bound, ConstantInt *Step,
                                StringRef Name, IRBuilderBase &B,
                                DomTreeUpdater &DTU, Loop *L, LoopInfo &LI);
};
} // namespace llvm

#endif
