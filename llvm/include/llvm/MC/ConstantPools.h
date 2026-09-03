//===- ConstantPools.h - Keep track of assembler-generated ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the ConstantPool and AssemblerConstantPools classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_CONSTANTPOOLS_H
#define LLVM_MC_CONSTANTPOOLS_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/SMLoc.h"
#include <cstdint>

namespace llvm {

class MCContext;
class MCExpr;
class MCSection;
class MCStreamer;
class MCSymbol;
class MCSymbolRefExpr;

/// A single entry in an assembler-generated constant pool.
struct ConstantPoolEntry {
  /// Construct a constant-pool entry for \p L, \p Val, \p Sz, and \p Loc_.
  ///
  /// \param L - Symbol labeling this pool entry.
  /// \param Val - Expression value stored in the pool.
  /// \param Sz - Size in bytes of the stored value.
  /// \param Loc_ - Source location associated with the entry.
  ConstantPoolEntry(MCSymbol *L, const MCExpr *Val, unsigned Sz, SMLoc Loc_)
    : Label(L), Value(Val), Size(Sz), Loc(Loc_) {}

  /// Symbol labeling this constant-pool entry.
  MCSymbol *Label;
  /// Expression value stored in the constant pool.
  const MCExpr *Value;
  /// Size in bytes of the stored value.
  unsigned Size;
  /// Source location associated with this entry.
  SMLoc Loc;
};

/// Tracks assembler-generated constant pools used to implement the ldr-pseudo.
class ConstantPool {
  using EntryVecTy = SmallVector<ConstantPoolEntry, 4>;
  EntryVecTy Entries;

  // Caches of entries that already exist, indexed by their contents
  // and also the size of the constant.
  DenseMap<std::pair<int64_t, unsigned>, const MCSymbolRefExpr *>
      CachedConstantEntries;
  DenseMap<std::pair<const MCSymbol *, unsigned>, const MCSymbolRefExpr *>
      CachedSymbolEntries;

public:
  /// Initialize a new empty constant pool.
  ConstantPool() = default;

  /// Add a new entry to the constant pool in the next slot.
  ///
  /// \param Value - New entry to put in the constant pool.
  /// \param Context - Assembler context used to create symbols and expressions.
  /// \param Size - Size in bytes of the entry.
  /// \param Loc - Source location associated with the entry.
  ///
  /// \returns an MCExpr that references the newly inserted value.
  LLVM_ABI const MCExpr *addEntry(const MCExpr *Value, MCContext &Context,
                                  unsigned Size, SMLoc Loc);

  /// Emit the contents of the constant pool using the provided streamer.
  ///
  /// \param Streamer - Streamer used to emit the pool entries.
  LLVM_ABI void emitEntries(MCStreamer &Streamer);

  /// Return true if the constant pool is empty.
  ///
  /// \returns true if the constant pool has no entries.
  LLVM_ABI bool empty();

  /// Clear the caches of previously added constant-pool entries.
  LLVM_ABI void clearCache();
};

/// Manages per-section constant pools used by the assembler ldr-pseudo.
///
/// The map associates a section to its constant pool. The constant pool is a
/// vector of (label, value) pairs. When the ldr pseudo is parsed we insert a
/// new (label, value) pair into the constant pool for the current section and
/// add an MCSymbolRefExpr to the new label as an opcode to the ldr. After we
/// have parsed all the user input we output the (label, value) pairs in each
/// constant pool at the end of the section.
///
/// We use MapVector for the map type to ensure stable iteration of the
/// sections at the end of the parse. We need to iterate over the sections in a
/// stable order to ensure that we print the constant pools in a deterministic
/// order when printing an assembly file.
class AssemblerConstantPools {
  using ConstantPoolMapTy = MapVector<MCSection *, ConstantPool>;
  ConstantPoolMapTy ConstantPools;

public:
  /// Emit every section's constant pool using \p Streamer.
  ///
  /// \param Streamer - Streamer used to emit the constant pools.
  LLVM_ABI void emitAll(MCStreamer &Streamer);
  /// Emit the constant pool for the streamer's current section.
  ///
  /// \param Streamer - Streamer whose current section's pool is emitted.
  LLVM_ABI void emitForCurrentSection(MCStreamer &Streamer);
  /// Clear the entry cache for the streamer's current section.
  ///
  /// \param Streamer - Streamer whose current section's pool cache is cleared.
  LLVM_ABI void clearCacheForCurrentSection(MCStreamer &Streamer);
  /// Add \p Expr to the constant pool for the streamer's current section.
  ///
  /// \param Streamer - Streamer identifying the current section and context.
  /// \param Expr - Expression value to place in the constant pool.
  /// \param Size - Size in bytes of the entry.
  /// \param Loc - Source location associated with the entry.
  ///
  /// \returns an MCExpr that references the newly inserted value.
  LLVM_ABI const MCExpr *addEntry(MCStreamer &Streamer, const MCExpr *Expr,
                                  unsigned Size, SMLoc Loc);

private:
  ConstantPool *getConstantPool(MCSection *Section);
  ConstantPool &getOrCreateConstantPool(MCSection *Section);
};

} // end namespace llvm

#endif // LLVM_MC_CONSTANTPOOLS_H
