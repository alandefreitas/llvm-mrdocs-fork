//===-- llvm/MC/MCFixup.h - Instruction Relocation and Patching -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCFIXUP_H
#define LLVM_MC_MCFIXUP_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SMLoc.h"
#include <cassert>

namespace llvm {
class MCExpr;

/// Extensible enumeration to represent the type of a fixup.
using MCFixupKind = uint16_t;
/// Standard fixup kinds shared by all targets, plus bounds for literal and
/// target-specific ranges.
enum {
  // [0, FirstLiteralRelocationKind) encodes raw relocation types.

  /// First kind for literal relocation types from \c .reloc directives.
  ///
  /// Kind \c FirstLiteralRelocationKind+t encodes relocation type \c t. Values
  /// in \c [FirstLiteralRelocationKind, FK_NONE) are literal relocation kinds.
  FirstLiteralRelocationKind = 2000,

  // Other kinds indicate the fixup may resolve to a constant, allowing the
  // assembler to update the instruction or data directly without a relocation.
  FK_NONE = 4000, ///< A no-op fixup.
  FK_Data_1,      ///< A one-byte fixup.
  FK_Data_2,      ///< A two-byte fixup.
  FK_Data_4,      ///< A four-byte fixup.
  FK_Data_8,      ///< A eight-byte fixup.
  FK_Data_leb128, ///< A leb128 fixup.
  FK_SecRel_1,    ///< A one-byte section relative fixup.
  FK_SecRel_2,    ///< A two-byte section relative fixup.
  FK_SecRel_4,    ///< A four-byte section relative fixup.
  FK_SecRel_8,    ///< A eight-byte section relative fixup.

  /// First kind available for target-specific fixups.
  FirstTargetFixupKind,
};

/// Encode information on a single operation to perform on a byte
/// sequence (e.g., an encoded instruction) which requires assemble- or run-
/// time patching.
///
/// Fixups are used any time the target instruction encoder needs to represent
/// some value in an instruction which is not yet concrete. The encoder will
/// encode the instruction assuming the value is 0, and emit a fixup which
/// communicates to the assembler backend how it should rewrite the encoded
/// value.
///
/// During the process of relaxation, the assembler will apply fixups as
/// symbolic values become concrete. When relaxation is complete, any remaining
/// fixups become relocations in the object file (or errors, if the fixup cannot
/// be encoded on the target).
class MCFixup {
  /// The value to put into the fixup location. The exact interpretation of the
  /// expression is target dependent, usually it will be one of the operands to
  /// an instruction or an assembler directive.
  const MCExpr *Value = nullptr;

  /// The byte index of start of the relocation inside the MCFragment.
  uint32_t Offset = 0;

  /// The target dependent kind of fixup item this is. The kind is used to
  /// determine how the operand value should be encoded into the instruction.
  MCFixupKind Kind = FK_NONE;

  /// True if this is a PC-relative fixup. The relocatable expression is
  /// typically resolved When SymB is nullptr and SymA is a local symbol defined
  /// within the current section.
  bool PCRel = false;

  /// Used by RISC-V style linker relaxation. Whether the fixup is
  /// linker-relaxable.
  bool LinkerRelaxable = false;

  /// Consider bit fields if we need more flags.

public:
  /// Create a fixup at \p Offset for \p Value with the given \p Kind.
  ///
  /// \param Offset - Byte index of the fixup within the fragment.
  /// \param Value - Expression whose value should be written at the fixup.
  /// \param Kind - Target-dependent kind describing how to encode the value.
  /// \param PCRel - True if this is a PC-relative fixup.
  /// \return The newly created fixup.
  static MCFixup create(uint32_t Offset, const MCExpr *Value, MCFixupKind Kind,
                        bool PCRel = false) {
    MCFixup FI;
    FI.Value = Value;
    FI.Offset = Offset;
    FI.Kind = Kind;
    FI.PCRel = PCRel;
    return FI;
  }

  /// Return the target-dependent kind of this fixup.
  ///
  /// \return The target-dependent fixup kind.
  MCFixupKind getKind() const { return Kind; }

  /// Return the byte offset of this fixup within its fragment.
  ///
  /// \return The byte offset within the fragment.
  uint32_t getOffset() const { return Offset; }
  /// Set the byte offset of this fixup within its fragment.
  ///
  /// \param Value - New byte offset of the fixup.
  void setOffset(uint32_t Value) { Offset = Value; }

  /// Return the expression whose value should be written at the fixup.
  ///
  /// \return The expression to write at the fixup location.
  const MCExpr *getValue() const { return Value; }

  /// Return true if this is a PC-relative fixup.
  ///
  /// \return True if this is a PC-relative fixup.
  bool isPCRel() const { return PCRel; }
  /// Mark this fixup as PC-relative.
  void setPCRel() { PCRel = true; }
  /// Return true if this fixup is linker-relaxable.
  ///
  /// \return True if this fixup is linker-relaxable.
  bool isLinkerRelaxable() const { return LinkerRelaxable; }
  /// Mark this fixup as linker-relaxable.
  void setLinkerRelaxable() { LinkerRelaxable = true; }

  /// Return the generic fixup kind for a value with the given size. It
  /// is an error to pass an unsupported size.
  ///
  /// \param Size - Size in bytes of the data to fix up (1, 2, 4, or 8).
  /// \return The generic fixup kind for \p Size bytes.
  static MCFixupKind getDataKindForSize(unsigned Size) {
    switch (Size) {
    default: llvm_unreachable("Invalid generic fixup size!");
    case 1:
      return FK_Data_1;
    case 2:
      return FK_Data_2;
    case 4:
      return FK_Data_4;
    case 8:
      return FK_Data_8;
    }
  }

  /// Return the source location associated with this fixup.
  ///
  /// \return The source location of this fixup.
  LLVM_ABI SMLoc getLoc() const;
};

/// Fixup classification helpers used during assembly and relocation.
namespace mc {
/// Return true if \p FixupKind encodes a relocation rather than a constant.
///
/// \param FixupKind - Fixup kind to classify.
/// \return False if the fixup can be resolved without a relocation.
inline bool isRelocation(MCFixupKind FixupKind) { return FixupKind < FK_NONE; }

/// Return true if \p FixupKind is a literal relocation from a \c .reloc
/// directive.
///
/// In ELF, this skips STT_SECTION adjustment and STT_TLS symbol type setting
/// for TLS relocations.
///
/// \param FixupKind - Fixup kind to classify.
/// \return True if \p FixupKind is a literal relocation kind.
inline bool isRelocRelocation(MCFixupKind FixupKind) {
  return FirstLiteralRelocationKind <= FixupKind && FixupKind < FK_NONE;
}
} // namespace mc

} // End llvm namespace

#endif
