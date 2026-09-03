//===--- DWARFExpression.h - DWARF Expression handling ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_LOWLEVEL_DWARFEXPRESSION_H
#define LLVM_DEBUGINFO_DWARF_LOWLEVEL_DWARFEXPRESSION_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataExtractor.h"

namespace llvm {
class DWARFUnit;
struct DIDumpOptions;
class MCRegisterInfo;
class raw_ostream;

/// A DWARF expression consisting of a sequence of operations.
///
/// Each operation is a DWARF opcode with zero or more operands. The
/// expression bytes come from a DataExtractor; address size and optional
/// DWARF format are used when decoding address and section-offset operands.
class DWARFExpression {
public:
  class iterator;

  /// This class represents an Operation in the Expression.
  ///
  /// An Operation can be in Error state (check with isError()). This means
  /// that it couldn't be decoded successfully. Some fields stay valid so that
  /// a caller can report or copy the bytes it could not decode: getCode(),
  /// getDescription(), getEndOffset(), which is the offset the operation
  /// started at, and in some cases getSubCode(). The remaining operand values
  /// are undefined.
  class Operation {
  public:
    /// Size and signedness of expression operations' operands.
    enum Encoding : uint8_t {
      Size1 = 0, ///< Operand is encoded as a 1-byte value.
      Size2 = 1, ///< Operand is encoded as a 2-byte value.
      Size4 = 2, ///< Operand is encoded as a 4-byte value.
      Size8 = 3, ///< Operand is encoded as an 8-byte value.
      SizeLEB = 4, ///< Operand is encoded as a LEB128 value.
      SizeAddr = 5, ///< Operand size equals the target address size.
      SizeRefAddr = 6, ///< Operand size equals the DWARF offset size.
      SizeBlock = 7, ///< Preceding operand contains block size
      BaseTypeRef = 8, ///< Operand is a ULEB128 offset to a DW_TAG_base_type.
      /// The operand is a ULEB128 encoded SubOpcode. This is only valid
      /// for the first operand of an operation.
      SizeSubOpLEB = 9,
      WasmLocationArg = 30,
      /// The operand is a ULEB128 encoded selector naming an NVIDIA specific
      /// operation. The number and type of any operands that follow are
      /// implied by the selector, so an unrecognized one cannot be skipped
      /// and stops decoding.
      NvidiaMuxArg = 31,
      SignBit = 0x80,
      SignedSize1 = SignBit | Size1,
      SignedSize2 = SignBit | Size2,
      SignedSize4 = SignBit | Size4,
      SignedSize8 = SignBit | Size8,
      SignedSizeLEB = SignBit | SizeLEB,
    };

    /// DWARF version in which an operation was introduced.
    enum DwarfVersion : uint8_t {
      DwarfNA, ///< Serves as a marker for unused entries
      Dwarf2 = 2, ///< DWARF version 2.
      Dwarf3,     ///< DWARF version 3.
      Dwarf4,     ///< DWARF version 4.
      Dwarf5      ///< DWARF version 5.
    };

    /// Description of the encoding of one expression Op.
    struct Description {
      DwarfVersion Version;     ///< Dwarf version where the Op was introduced.
      SmallVector<Encoding> Op; ///< Encoding for Op operands.

      /// Construct a description from a DWARF version and operand encodings.
      ///
      /// \param Version DWARF version that introduced the operation.
      /// \param Op Encoding of each operand, in order.
      template <typename... Ts>
      Description(DwarfVersion Version, Ts... Op)
          : Version(Version), Op{Op...} {}
      /// Construct an unused description with version DwarfNA.
      Description() : Description(DwarfNA) {}
      /// Destroy this description.
      ~Description() = default;
    };

  private:
    friend class DWARFExpression::iterator;
    friend class DWARFVerifier;

    uint8_t Opcode; ///< The Op Opcode, DW_OP_<something>.
    Description Desc;
    bool Error = false;
    uint64_t EndOffset;
    SmallVector<uint64_t> Operands;
    SmallVector<uint64_t> OperandEndOffsets;

  public:
    /// Return the encoding description of this operation.
    ///
    /// \return The Description of this operation's encoding.
    const Description &getDescription() const { return Desc; }
    /// Return the DWARF opcode of this operation.
    ///
    /// \return The DWARF opcode.
    uint8_t getCode() const { return Opcode; }
    /// Return the sub-opcode when the first operand is a SizeSubOpLEB.
    ///
    /// \return The sub-opcode, or std::nullopt if not applicable.
    LLVM_ABI std::optional<unsigned> getSubCode() const;
    /// Return the number of decoded operands.
    ///
    /// \return The number of operands.
    uint64_t getNumOperands() const { return Operands.size(); }
    /// Return the raw decoded operand values.
    ///
    /// \return An ArrayRef of the raw operand values.
    ArrayRef<uint64_t> getRawOperands() const { return Operands; };
    /// Return the raw decoded value of operand \p Idx.
    ///
    /// \param Idx Zero-based operand index.
    /// \return The raw value of operand Idx.
    uint64_t getRawOperand(unsigned Idx) const { return Operands[Idx]; }
    /// End offsets (within the expression) of each decoded operand.
    ///
    /// \return An ArrayRef of end offsets for each operand.
    ArrayRef<uint64_t> getOperandEndOffsets() const {
      return OperandEndOffsets;
    }
    /// Return the end offset of operand \p Idx within the expression.
    ///
    /// \param Idx Zero-based operand index.
    /// \return The byte offset just past operand Idx.
    uint64_t getOperandEndOffset(unsigned Idx) const {
      return OperandEndOffsets[Idx];
    }
    /// Return the end offset of this operation within the expression.
    ///
    /// \return The byte offset just past this operation.
    uint64_t getEndOffset() const { return EndOffset; }
    /// Return true if this operation could not be decoded successfully.
    ///
    /// \return True if decoding failed.
    bool isError() const { return Error; }

  private:
    LLVM_ABI bool extract(DataExtractor Data, uint8_t AddressSize,
                          uint64_t Offset,
                          std::optional<dwarf::DwarfFormat> Format);
  };

  /// An iterator to go through the expression operations.
  class iterator
      : public iterator_facade_base<iterator, std::forward_iterator_tag,
                                    const Operation> {
    friend class DWARFExpression;
    const DWARFExpression *Expr;
    uint64_t Offset;
    Operation Op;
    iterator(const DWARFExpression *Expr, uint64_t Offset)
        : Expr(Expr), Offset(Offset) {
      Op.Error =
          Offset >= Expr->Data.getData().size() ||
          !Op.extract(Expr->Data, Expr->AddressSize, Offset, Expr->Format);
    }

  public:
    /// Get the byte offset of the current operation within the expression.
    ///
    /// \return The byte offset of the current operation.
    uint64_t getOffset() const { return Offset; }

    /// Advance this iterator to the next operation.
    ///
    /// \return A reference to this iterator.
    iterator &operator++() {
      Offset = Op.isError() ? Expr->Data.getData().size() : Op.EndOffset;
      Op.Error =
          Offset >= Expr->Data.getData().size() ||
          !Op.extract(Expr->Data, Expr->AddressSize, Offset, Expr->Format);
      return *this;
    }

    /// Return the current operation.
    ///
    /// \return A reference to the current Operation.
    const Operation &operator*() const { return Op; }

    /// Return an iterator \p Add bytes past the end of the current operation.
    ///
    /// \param Add Extra bytes to skip after the current operation.
    /// \return An iterator positioned Add bytes after the current operation's end.
    iterator skipBytes(uint64_t Add) const {
      return iterator(Expr, Op.EndOffset + Add);
    }

    // Comparison operators are provided out of line.
    friend bool operator==(const iterator &, const iterator &);
  };

  /// Construct a DWARF expression from extracted bytes.
  ///
  /// \param Data Extractor covering the expression bytes.
  /// \param AddressSize Target address size in bytes; must be 2, 4, or 8.
  /// \param Format Optional DWARF format used to decode SizeRefAddr operands.
  DWARFExpression(DataExtractor Data, uint8_t AddressSize,
                  std::optional<dwarf::DwarfFormat> Format = std::nullopt)
      : Data(Data), AddressSize(AddressSize), Format(Format) {
    assert(AddressSize == 8 || AddressSize == 4 || AddressSize == 2);
  }

  /// Return an iterator to the first operation.
  ///
  /// \return A begin iterator for this expression.
  iterator begin() const { return iterator(this, 0); }
  /// Return an iterator past the last operation.
  ///
  /// \return An end iterator for this expression.
  iterator end() const { return iterator(this, Data.getData().size()); }

  /// Compare address size, format, and raw expression bytes for equality.
  ///
  /// \param RHS Expression to compare against.
  /// \return True if both expressions have the same address size, format, and bytes.
  LLVM_ABI bool operator==(const DWARFExpression &RHS) const;

  /// Return the raw expression bytes.
  ///
  /// \return The expression bytes as a StringRef.
  StringRef getData() const { return Data.getData(); }

  friend class DWARFVerifier;

private:
  DataExtractor Data;
  uint8_t AddressSize;
  std::optional<dwarf::DwarfFormat> Format;
};

/// Return true if \p LHS and \p RHS are the same position in one expression.
///
/// \param LHS Left-hand iterator.
/// \param RHS Right-hand iterator.
/// \return True if both iterators refer to the same expression and offset.
inline bool operator==(const DWARFExpression::iterator &LHS,
                       const DWARFExpression::iterator &RHS) {
  return LHS.Expr == RHS.Expr && LHS.Offset == RHS.Offset;
}

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_LOWLEVEL_DWARFEXPRESSION_H
