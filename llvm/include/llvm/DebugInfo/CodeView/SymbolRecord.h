//===- SymbolRecord.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SYMBOLRECORD_H
#define LLVM_DEBUGINFO_CODEVIEW_SYMBOLRECORD_H

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/RecordSerialization.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include <cstdint>
#include <vector>

namespace llvm {
namespace codeview {

/// Base class for decoded CodeView symbol records.
class SymbolRecord {
protected:
  /// Construct a symbol record of the given kind.
  ///
  /// \param Kind Symbol record kind for this instance.
  explicit SymbolRecord(SymbolRecordKind Kind) : Kind(Kind) {}

public:
  /// Return the kind of this symbol record.
  ///
  /// \returns The kind of this symbol record.
  SymbolRecordKind getKind() const { return Kind; }

  /// Kind of this CodeView symbol record.
  SymbolRecordKind Kind;
};

/// Procedure symbol for global or local functions.
///
/// Corresponds to S_GPROC32, S_LPROC32, S_GPROC32_ID, S_LPROC32_ID,
/// S_LPROC32_DPC, or S_LPROC32_DPC_ID.
class ProcSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 32;

public:
  /// Construct a procedure symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit ProcSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a procedure symbol at the given stream offset.
  ///
  /// \param Kind Symbol record kind.
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  ProcSym(SymbolRecordKind Kind, uint32_t RecordOffset)
      : SymbolRecord(Kind), RecordOffset(RecordOffset) {}

  /// Return the byte offset of the relocatable code address field.
  ///
  /// \returns Byte offset of the relocatable code address within the stream.
  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  /// Offset of the parent scope end record within the symbol stream.
  uint32_t Parent = 0;
  /// Offset of this procedure's end record within the symbol stream.
  uint32_t End = 0;
  /// Offset of the next procedure symbol, if any.
  uint32_t Next = 0;
  /// Size of the procedure's code in bytes.
  uint32_t CodeSize = 0;
  /// Offset from the procedure start to the first byte of prologue end.
  uint32_t DbgStart = 0;
  /// Offset from the procedure start to the first byte of epilogue start.
  uint32_t DbgEnd = 0;
  /// Type index of the procedure's function type.
  TypeIndex FunctionType;
  /// Offset of the procedure within \c Segment.
  uint32_t CodeOffset = 0;
  /// Segment containing the procedure code.
  uint16_t Segment = 0;
  /// Procedure attribute flags.
  ProcSymFlags Flags = ProcSymFlags::None;
  /// Name of the procedure.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// 32-bit thunk symbol (S_THUNK32).
class Thunk32Sym : public SymbolRecord {
public:
  /// Construct a thunk symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit Thunk32Sym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a thunk symbol at the given stream offset.
  ///
  /// \param Kind Symbol record kind.
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  Thunk32Sym(SymbolRecordKind Kind, uint32_t RecordOffset)
      : SymbolRecord(Kind), RecordOffset(RecordOffset) {}

  /// Offset of the parent scope end record within the symbol stream.
  uint32_t Parent = 0;
  /// Offset of this thunk's end record within the symbol stream.
  uint32_t End = 0;
  /// Offset of the next thunk symbol, if any.
  uint32_t Next = 0;
  /// Offset of the thunk within \c Segment.
  uint32_t Offset = 0;
  /// Segment containing the thunk.
  uint16_t Segment = 0;
  /// Length of the thunk in bytes.
  uint16_t Length = 0;
  /// Ordinal describing the thunk kind.
  ThunkOrdinal Thunk = ThunkOrdinal::Standard;
  /// Name of the thunk.
  StringRef Name;
  /// Optional variant-specific payload bytes.
  ArrayRef<uint8_t> VariantData;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Trampoline symbol describing a thunk that redirects to a target (S_TRAMPOLINE).
class TrampolineSym : public SymbolRecord {
public:
  /// Construct a trampoline symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit TrampolineSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a trampoline symbol at the given stream offset.
  ///
  /// \param Kind Symbol record kind.
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  TrampolineSym(SymbolRecordKind Kind, uint32_t RecordOffset)
      : SymbolRecord(Kind), RecordOffset(RecordOffset) {}

  /// Kind of trampoline.
  TrampolineType Type;
  /// Size of the trampoline in bytes.
  uint16_t Size = 0;
  /// Offset of the thunk within \c ThunkSection.
  uint32_t ThunkOffset = 0;
  /// Offset of the target within \c TargetSection.
  uint32_t TargetOffset = 0;
  /// Section containing the thunk.
  uint16_t ThunkSection = 0;
  /// Section containing the trampoline target.
  uint16_t TargetSection = 0;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// COFF section contribution symbol (S_SECTION).
class SectionSym : public SymbolRecord {
public:
  /// Construct a section symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit SectionSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a section symbol at the given stream offset.
  ///
  /// \param Kind Symbol record kind.
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  SectionSym(SymbolRecordKind Kind, uint32_t RecordOffset)
      : SymbolRecord(Kind), RecordOffset(RecordOffset) {}

  /// One-based COFF section number.
  uint16_t SectionNumber = 0;
  /// Section alignment as a power of two.
  uint8_t Alignment = 0;
  /// Relative virtual address of the section.
  uint32_t Rva = 0;
  /// Length of the section in bytes.
  uint32_t Length = 0;
  /// COFF section characteristics flags.
  uint32_t Characteristics = 0;
  /// Name of the section.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// COFF group symbol (S_COFFGROUP).
class CoffGroupSym : public SymbolRecord {
public:
  /// Construct a COFF group symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit CoffGroupSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a COFF group symbol at the given stream offset.
  ///
  /// \param Kind Symbol record kind.
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  CoffGroupSym(SymbolRecordKind Kind, uint32_t RecordOffset)
      : SymbolRecord(Kind), RecordOffset(RecordOffset) {}

  /// Size of the group in bytes.
  uint32_t Size = 0;
  /// COFF section characteristics for the group.
  uint32_t Characteristics = 0;
  /// Offset of the group within \c Segment.
  uint32_t Offset = 0;
  /// Segment containing the group.
  uint16_t Segment = 0;
  /// Name of the COFF group.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Scope end symbol that closes a prior scope-opening record (S_END).
class ScopeEndSym : public SymbolRecord {
public:
  /// Construct a scope end symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit ScopeEndSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a scope end symbol at the given stream offset.
  ///
  /// \param Kind Symbol record kind.
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  ScopeEndSym(SymbolRecordKind Kind, uint32_t RecordOffset)
      : SymbolRecord(Kind), RecordOffset(RecordOffset) {}

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Jump table symbol describing a switch dispatch table (S_ARMSWITCHTABLE).
class JumpTableSym : public SymbolRecord {
public:
  /// Construct a jump table symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit JumpTableSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a jump table symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  JumpTableSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::JumpTableSym),
        RecordOffset(RecordOffset) {}

  /// Offset of the jump table base within \c BaseSegment.
  uint32_t BaseOffset = 0;
  /// Segment containing the jump table base.
  uint16_t BaseSegment = 0;

  /// Size of each jump table entry.
  JumpTableEntrySize SwitchType;
  /// Offset of the branch instruction within \c BranchSegment.
  uint32_t BranchOffset = 0;
  /// Offset of the table within \c TableSegment.
  uint32_t TableOffset = 0;
  /// Segment containing the branch instruction.
  uint16_t BranchSegment = 0;
  /// Segment containing the jump table.
  uint16_t TableSegment = 0;

  /// Number of entries in the jump table.
  uint32_t EntriesCount = 0;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Caller or callee list symbol (S_CALLERS / S_CALLEES / S_INLINEES).
class CallerSym : public SymbolRecord {
public:
  /// Construct a caller symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit CallerSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a caller symbol at the given stream offset.
  ///
  /// \param Kind Symbol record kind.
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  CallerSym(SymbolRecordKind Kind, uint32_t RecordOffset)
      : SymbolRecord(Kind), RecordOffset(RecordOffset) {}

  /// Type indices of the referenced functions.
  std::vector<TypeIndex> Indices;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Hot-patchable function symbol (S_HOTPATCHFUNC).
class HotPatchFuncSym : public SymbolRecord {
public:
  /// Construct a hot-patch function symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit HotPatchFuncSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a hot-patch function symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  HotPatchFuncSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::HotPatchFuncSym),
        RecordOffset(RecordOffset) {}

  /// Type index of the hot-patched function.
  ///
  /// This is an ItemID in the IPI stream, which points to an LF_FUNC_ID or
  /// LF_MFUNC_ID record.
  TypeIndex Function;
  /// Name of the hot-patched function.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// One decoded binary annotation opcode and its operands.
struct DecodedAnnotation {
  /// Human-readable name of the opcode.
  StringRef Name;
  /// Raw bytes that encoded this annotation.
  ArrayRef<uint8_t> Bytes;
  /// Decoded binary annotation opcode.
  BinaryAnnotationsOpCode OpCode = BinaryAnnotationsOpCode::Invalid;
  /// First unsigned operand, when applicable.
  uint32_t U1 = 0;
  /// Second unsigned operand, when applicable.
  uint32_t U2 = 0;
  /// First signed operand, when applicable.
  int32_t S1 = 0;
};

/// Forward iterator over compressed inline-site binary annotations.
struct BinaryAnnotationIterator
    : public iterator_facade_base<BinaryAnnotationIterator,
                                  std::forward_iterator_tag,
                                  DecodedAnnotation> {
  /// Construct an end iterator.
  BinaryAnnotationIterator() = default;
  /// Construct an iterator over the annotation bytes in \p Annotations.
  ///
  /// \param Annotations Compressed binary annotation payload.
  BinaryAnnotationIterator(ArrayRef<uint8_t> Annotations) : Data(Annotations) {}
  /// Construct a copy of \p Other.
  ///
  /// \param Other Iterator to copy.
  BinaryAnnotationIterator(const BinaryAnnotationIterator &Other)
      : Data(Other.Data) {}

  /// Return true if this iterator and \p Other refer to the same remaining bytes.
  ///
  /// \param Other Iterator to compare against.
  /// \returns True if both iterators have the same remaining annotation bytes.
  bool operator==(BinaryAnnotationIterator Other) const {
    return Data == Other.Data;
  }

  /// Assign this iterator from \p Other.
  ///
  /// \param Other Iterator to assign from.
  /// \returns A reference to this iterator after assignment.
  BinaryAnnotationIterator &operator=(const BinaryAnnotationIterator Other) {
    Data = Other.Data;
    return *this;
  }

  /// Advance to the next annotation, or become an end iterator on failure.
  ///
  /// \returns A reference to this iterator after advancing.
  BinaryAnnotationIterator &operator++() {
    if (!ParseCurrentAnnotation()) {
      *this = BinaryAnnotationIterator();
      return *this;
    }
    Data = Next;
    Next = ArrayRef<uint8_t>();
    Current.reset();
    return *this;
  }

  /// Return the current decoded annotation, parsing it if needed.
  ///
  /// \returns The current decoded annotation.
  const DecodedAnnotation &operator*() {
    ParseCurrentAnnotation();
    return *Current;
  }

private:
  static uint32_t GetCompressedAnnotation(ArrayRef<uint8_t> &Annotations) {
    if (Annotations.empty())
      return -1;

    uint8_t FirstByte = Annotations.consume_front();

    if ((FirstByte & 0x80) == 0x00)
      return FirstByte;

    if (Annotations.empty())
      return -1;

    uint8_t SecondByte = Annotations.consume_front();

    if ((FirstByte & 0xC0) == 0x80)
      return ((FirstByte & 0x3F) << 8) | SecondByte;

    if (Annotations.empty())
      return -1;

    uint8_t ThirdByte = Annotations.consume_front();

    if (Annotations.empty())
      return -1;

    uint8_t FourthByte = Annotations.consume_front();

    if ((FirstByte & 0xE0) == 0xC0)
      return ((FirstByte & 0x1F) << 24) | (SecondByte << 16) |
             (ThirdByte << 8) | FourthByte;

    return -1;
  }

  static int32_t DecodeSignedOperand(uint32_t Operand) {
    if (Operand & 1)
      return -(Operand >> 1);
    return Operand >> 1;
  }

  static int32_t DecodeSignedOperand(ArrayRef<uint8_t> &Annotations) {
    return DecodeSignedOperand(GetCompressedAnnotation(Annotations));
  }

  bool ParseCurrentAnnotation() {
    if (Current)
      return true;

    Next = Data;
    uint32_t Op = GetCompressedAnnotation(Next);
    DecodedAnnotation Result;
    Result.OpCode = static_cast<BinaryAnnotationsOpCode>(Op);
    switch (Result.OpCode) {
    case BinaryAnnotationsOpCode::Invalid:
      Result.Name = "Invalid";
      Next = ArrayRef<uint8_t>();
      break;
    case BinaryAnnotationsOpCode::CodeOffset:
      Result.Name = "CodeOffset";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeCodeOffsetBase:
      Result.Name = "ChangeCodeOffsetBase";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeCodeOffset:
      Result.Name = "ChangeCodeOffset";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeCodeLength:
      Result.Name = "ChangeCodeLength";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeFile:
      Result.Name = "ChangeFile";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeLineEndDelta:
      Result.Name = "ChangeLineEndDelta";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeRangeKind:
      Result.Name = "ChangeRangeKind";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeColumnStart:
      Result.Name = "ChangeColumnStart";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeColumnEnd:
      Result.Name = "ChangeColumnEnd";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeLineOffset:
      Result.Name = "ChangeLineOffset";
      Result.S1 = DecodeSignedOperand(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeColumnEndDelta:
      Result.Name = "ChangeColumnEndDelta";
      Result.S1 = DecodeSignedOperand(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeCodeOffsetAndLineOffset: {
      Result.Name = "ChangeCodeOffsetAndLineOffset";
      uint32_t Annotation = GetCompressedAnnotation(Next);
      Result.S1 = DecodeSignedOperand(Annotation >> 4);
      Result.U1 = Annotation & 0xf;
      break;
    }
    case BinaryAnnotationsOpCode::ChangeCodeLengthAndCodeOffset: {
      Result.Name = "ChangeCodeLengthAndCodeOffset";
      Result.U1 = GetCompressedAnnotation(Next);
      Result.U2 = GetCompressedAnnotation(Next);
      break;
    }
    }
    Result.Bytes = Data.take_front(Data.size() - Next.size());
    Current = Result;
    return true;
  }

  std::optional<DecodedAnnotation> Current;
  ArrayRef<uint8_t> Data;
  ArrayRef<uint8_t> Next;
};

/// Inline site symbol describing an inlined call (S_INLINESITE).
class InlineSiteSym : public SymbolRecord {
public:
  /// Construct an inline site symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit InlineSiteSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct an inline site symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit InlineSiteSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::InlineSiteSym),
        RecordOffset(RecordOffset) {}

  /// Return an iterator range over the compressed binary annotations.
  ///
  /// \returns An iterator range covering the annotation payload.
  iterator_range<BinaryAnnotationIterator> annotations() const {
    return make_range(BinaryAnnotationIterator(AnnotationData),
                      BinaryAnnotationIterator());
  }

  /// Offset of the parent scope end record within the symbol stream.
  uint32_t Parent = 0;
  /// Offset of this inline site's end record within the symbol stream.
  uint32_t End = 0;
  /// Type index of the inlined function.
  TypeIndex Inlinee;
  /// Compressed binary annotation payload for this inline site.
  std::vector<uint8_t> AnnotationData;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Fixed on-disk header for an S_PUB32 public symbol.
struct PublicSym32Header {
  /// Public symbol flags.
  ulittle32_t Flags;
  /// Offset of the symbol within \c Segment.
  ulittle32_t Offset;
  /// Segment containing the public symbol.
  ulittle16_t Segment;
  // char Name[];
};

/// 32-bit public symbol (S_PUB32).
class PublicSym32 : public SymbolRecord {
public:
  /// Construct an empty public symbol.
  PublicSym32() : SymbolRecord(SymbolRecordKind::PublicSym32) {}
  /// Construct a public symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit PublicSym32(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a public symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit PublicSym32(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::PublicSym32),
        RecordOffset(RecordOffset) {}

  /// Public symbol flags.
  PublicSymFlags Flags = PublicSymFlags::None;
  /// Offset of the symbol within \c Segment.
  uint32_t Offset = 0;
  /// Segment containing the public symbol.
  uint16_t Segment = 0;
  /// Name of the public symbol.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Register variable symbol (S_REGISTER).
class RegisterSym : public SymbolRecord {
public:
  /// Construct a register symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit RegisterSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a register symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit RegisterSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::RegisterSym),
        RecordOffset(RecordOffset) {}

  /// Type index of the register variable.
  TypeIndex Index;
  /// Register that holds the variable.
  RegisterId Register = RegisterId::NONE;
  /// Name of the register variable.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Procedure reference symbol (S_PROCREF or S_LPROCREF).
class ProcRefSym : public SymbolRecord {
public:
  /// Construct a procedure reference symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit ProcRefSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a procedure reference symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit ProcRefSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::ProcRefSym), RecordOffset(RecordOffset) {
  }

  /// Checksum of the referenced procedure name.
  uint32_t SumName = 0;
  /// Offset of the referenced procedure symbol within its module.
  uint32_t SymOffset = 0;
  /// One-based module index containing the referenced procedure.
  uint16_t Module = 0;
  /// Name of the referenced procedure.
  StringRef Name;

  /// Return the zero-based module index of the referenced procedure.
  ///
  /// \returns Zero-based module index of the referenced procedure.
  uint16_t modi() const { return Module - 1; }
  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Local variable symbol (S_LOCAL).
class LocalSym : public SymbolRecord {
public:
  /// Construct a local symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit LocalSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a local symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit LocalSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::LocalSym), RecordOffset(RecordOffset) {}

  /// Type index of the local variable.
  TypeIndex Type;
  /// Local variable attribute flags.
  LocalSymFlags Flags = LocalSymFlags::None;
  /// Name of the local variable.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Contiguous address range where a local variable is live.
struct LocalVariableAddrRange {
  /// Starting offset within \c ISectStart.
  uint32_t OffsetStart = 0;
  /// Section index of the range start.
  uint16_t ISectStart = 0;
  /// Length of the live range in bytes.
  uint16_t Range = 0;
};

/// Gap inside a local variable address range where the variable is not live.
struct LocalVariableAddrGap {
  /// Offset from the range start where the gap begins.
  uint16_t GapStartOffset = 0;
  /// Length of the gap in bytes.
  uint16_t Range = 0;
};

/// Maximum length of a single def-range address range.
enum : uint16_t {
  MaxDefRange = 0xf000 ///< Maximum byte length of a single def-range.
};

/// Generic def-range symbol locating a local via a DIA program (S_DEFRANGE).
class DefRangeSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 8;

public:
  /// Construct a def-range symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit DefRangeSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a def-range symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit DefRangeSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DefRangeSym),
        RecordOffset(RecordOffset) {}

  /// Return the byte offset of the relocatable range start field.
  ///
  /// \returns Byte offset of the relocatable range start within the stream.
  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  /// DIA program used to evaluate the variable location.
  uint32_t Program = 0;
  /// Address range where the variable is live.
  LocalVariableAddrRange Range;
  /// Gaps within \c Range where the variable is not live.
  std::vector<LocalVariableAddrGap> Gaps;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Def-range for a subfield of a local, located via a DIA program (S_DEFRANGE_SUBFIELD).
class DefRangeSubfieldSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 12;

public:
  /// Construct a subfield def-range symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit DefRangeSubfieldSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a subfield def-range symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit DefRangeSubfieldSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DefRangeSubfieldSym),
        RecordOffset(RecordOffset) {}

  /// Return the byte offset of the relocatable range start field.
  ///
  /// \returns Byte offset of the relocatable range start within the stream.
  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  /// DIA program used to evaluate the parent variable location.
  uint32_t Program = 0;
  /// Byte offset of the subfield within the parent variable.
  uint16_t OffsetInParent = 0;
  /// Address range where the subfield is live.
  LocalVariableAddrRange Range;
  /// Gaps within \c Range where the subfield is not live.
  std::vector<LocalVariableAddrGap> Gaps;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Fixed header for an S_DEFRANGE_REGISTER symbol.
struct DefRangeRegisterHeader {
  /// Register that holds the variable.
  ulittle16_t Register;
  /// Non-zero when the variable may have no name.
  ulittle16_t MayHaveNoName;
};

/// Def-range for a local that lives in a register (S_DEFRANGE_REGISTER).
class DefRangeRegisterSym : public SymbolRecord {
public:
  /// Construct a register def-range symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit DefRangeRegisterSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a register def-range symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit DefRangeRegisterSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DefRangeRegisterSym),
        RecordOffset(RecordOffset) {}

  /// Return the byte offset of the relocatable range start field.
  ///
  /// \returns Byte offset of the relocatable range start within the stream.
  uint32_t getRelocationOffset() const { return RecordOffset + sizeof(DefRangeRegisterHeader); }

  /// Fixed register location header.
  DefRangeRegisterHeader Hdr;
  /// Address range where the variable is live.
  LocalVariableAddrRange Range;
  /// Gaps within \c Range where the variable is not live.
  std::vector<LocalVariableAddrGap> Gaps;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Fixed header for an S_DEFRANGE_SUBFIELD_REGISTER symbol.
struct DefRangeSubfieldRegisterHeader {
  /// Register that holds the parent variable.
  ulittle16_t Register;
  /// Non-zero when the variable may have no name.
  ulittle16_t MayHaveNoName;
  /// Byte offset of the subfield within the parent variable.
  ulittle32_t OffsetInParent;
};

/// Def-range for a register-held subfield (S_DEFRANGE_SUBFIELD_REGISTER).
class DefRangeSubfieldRegisterSym : public SymbolRecord {
public:
  /// Construct a subfield-register def-range symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit DefRangeSubfieldRegisterSym(SymbolRecordKind Kind)
      : SymbolRecord(Kind) {}
  /// Construct a subfield-register def-range at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit DefRangeSubfieldRegisterSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DefRangeSubfieldRegisterSym),
        RecordOffset(RecordOffset) {}

  /// Return the byte offset of the relocatable range start field.
  ///
  /// \returns Byte offset of the relocatable range start within the stream.
  uint32_t getRelocationOffset() const { return RecordOffset + sizeof(DefRangeSubfieldRegisterHeader); }

  /// Fixed subfield-register location header.
  DefRangeSubfieldRegisterHeader Hdr;
  /// Address range where the subfield is live.
  LocalVariableAddrRange Range;
  /// Gaps within \c Range where the subfield is not live.
  std::vector<LocalVariableAddrGap> Gaps;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Fixed header for an S_DEFRANGE_FRAMEPOINTER_REL symbol.
struct DefRangeFramePointerRelHeader {
  /// Offset from the frame pointer to the variable.
  little32_t Offset;
};

/// Def-range for a frame-pointer-relative local (S_DEFRANGE_FRAMEPOINTER_REL).
class DefRangeFramePointerRelSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 8;

public:
  /// Construct a frame-pointer-relative def-range of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit DefRangeFramePointerRelSym(SymbolRecordKind Kind)
      : SymbolRecord(Kind) {}
  /// Construct a frame-pointer-relative def-range at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit DefRangeFramePointerRelSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DefRangeFramePointerRelSym),
        RecordOffset(RecordOffset) {}

  /// Return the byte offset of the relocatable range start field.
  ///
  /// \returns Byte offset of the relocatable range start within the stream.
  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  /// Fixed frame-pointer-relative location header.
  DefRangeFramePointerRelHeader Hdr;
  /// Address range where the variable is live.
  LocalVariableAddrRange Range;
  /// Gaps within \c Range where the variable is not live.
  std::vector<LocalVariableAddrGap> Gaps;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Fixed header for an S_DEFRANGE_REGISTER_REL symbol.
struct DefRangeRegisterRelHeader {
  /// Base register for the relative location.
  ulittle16_t Register;
  /// Packed flags: subfield bit and offset-in-parent.
  ulittle16_t Flags;
  /// Offset from the base register to the variable or parent.
  little32_t BasePointerOffset;
};

/// Def-range for a register-relative local (S_DEFRANGE_REGISTER_REL).
class DefRangeRegisterRelSym : public SymbolRecord {
public:
  /// Construct a register-relative def-range of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit DefRangeRegisterRelSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a register-relative def-range at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit DefRangeRegisterRelSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DefRangeRegisterRelSym),
        RecordOffset(RecordOffset) {}

  // The flags implement this notional bitfield:
  //   uint16_t IsSubfield : 1;
  //   uint16_t Padding : 3;
  //   uint16_t OffsetInParent : 12;
  /// Flag bits packed into \c DefRangeRegisterRelHeader::Flags.
  enum : uint16_t {
    IsSubfieldFlag = 1,      ///< Set when the range covers a spilled UDT member.
    OffsetInParentShift = 4, ///< Bit shift for the offset-in-parent field.
  };

  /// Return true if this range describes a spilled UDT member.
  ///
  /// \returns True if the IsSubfield flag is set.
  bool hasSpilledUDTMember() const { return Hdr.Flags & IsSubfieldFlag; }
  /// Return the byte offset of the member within its parent UDT.
  ///
  /// \returns Byte offset of the member within its parent UDT.
  uint16_t offsetInParent() const { return Hdr.Flags >> OffsetInParentShift; }

  /// Return the byte offset of the relocatable range start field.
  ///
  /// \returns Byte offset of the relocatable range start within the stream.
  uint32_t getRelocationOffset() const { return RecordOffset + sizeof(DefRangeRegisterRelHeader); }

  /// Fixed register-relative location header.
  DefRangeRegisterRelHeader Hdr;
  /// Address range where the variable is live.
  LocalVariableAddrRange Range;
  /// Gaps within \c Range where the variable is not live.
  std::vector<LocalVariableAddrGap> Gaps;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Full-scope frame-pointer-relative def-range (S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE).
class DefRangeFramePointerRelFullScopeSym : public SymbolRecord {
public:
  /// Construct a full-scope frame-pointer-relative def-range of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit DefRangeFramePointerRelFullScopeSym(SymbolRecordKind Kind)
      : SymbolRecord(Kind) {}
  /// Construct a full-scope frame-pointer-relative def-range at \p RecordOffset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit DefRangeFramePointerRelFullScopeSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DefRangeFramePointerRelFullScopeSym),
        RecordOffset(RecordOffset) {}

  /// Offset from the frame pointer to the variable.
  int32_t Offset = 0;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Fixed header for an S_DEFRANGE_REGISTER_REL_INDIR symbol.
struct DefRangeRegisterRelIndirHeader {
  /// Base register for the indirect location.
  ulittle16_t Register;
  /// Packed flags: subfield bit and offset-in-parent.
  ulittle16_t Flags;
  /// Offset added to \c Register before dereferencing.
  little32_t BasePointerOffset;
  /// Offset to add after dereferencing `Register + BasePointerOffset`.
  little32_t OffsetInUdt;
};

/// S_DEFRANGE_REGISTER_REL_INDIR
///
/// The local is located at `*(Register + BasePointerOffset) + OffsetInUDT`.
class DefRangeRegisterRelIndirSym : public SymbolRecord {
public:
  /// Construct an indirect register-relative def-range of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit DefRangeRegisterRelIndirSym(SymbolRecordKind Kind)
      : SymbolRecord(Kind) {}
  /// Construct an indirect register-relative def-range at \p RecordOffset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit DefRangeRegisterRelIndirSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DefRangeRegisterRelIndirSym),
        RecordOffset(RecordOffset) {}

  // These flags are the same as in DefRangeRegisterRelSym.
  // The flags implement this notional bitfield:
  //   uint16_t IsSubfield : 1;
  //   uint16_t Padding : 3;
  //   uint16_t OffsetInParent : 12;
  /// Flag bits packed into \c DefRangeRegisterRelIndirHeader::Flags.
  enum : uint16_t {
    IsSubfieldFlag = 1,      ///< Set when the range covers a spilled UDT member.
    OffsetInParentShift = 4, ///< Bit shift for the offset-in-parent field.
  };

  /// Return true if this range describes a spilled UDT member.
  ///
  /// \returns True if the IsSubfield flag is set.
  bool hasSpilledUDTMember() const { return Hdr.Flags & IsSubfieldFlag; }
  /// Return the byte offset of the member within its parent UDT.
  ///
  /// \returns Byte offset of the member within its parent UDT.
  uint16_t offsetInParent() const { return Hdr.Flags >> OffsetInParentShift; }

  /// Return the byte offset of the relocatable range start field.
  ///
  /// \returns Byte offset of the relocatable range start within the stream.
  uint32_t getRelocationOffset() const {
    return RecordOffset + sizeof(DefRangeRegisterRelIndirHeader);
  }

  /// Fixed indirect register-relative location header.
  DefRangeRegisterRelIndirHeader Hdr;
  /// Address range where the variable is live.
  LocalVariableAddrRange Range;
  /// Gaps within \c Range where the variable is not live.
  std::vector<LocalVariableAddrGap> Gaps;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Lexical block symbol (S_BLOCK32).
class BlockSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 16;

public:
  /// Construct a block symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit BlockSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a block symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit BlockSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::BlockSym), RecordOffset(RecordOffset) {}

  /// Return the byte offset of the relocatable code address field.
  ///
  /// \returns Byte offset of the relocatable code address within the stream.
  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  /// Offset of the parent scope end record within the symbol stream.
  uint32_t Parent = 0;
  /// Offset of this block's end record within the symbol stream.
  uint32_t End = 0;
  /// Size of the block's code in bytes.
  uint32_t CodeSize = 0;
  /// Offset of the block within \c Segment.
  uint32_t CodeOffset = 0;
  /// Segment containing the block.
  uint16_t Segment = 0;
  /// Optional name of the block.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Label symbol (S_LABEL32).
class LabelSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 4;

public:
  /// Construct a label symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit LabelSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a label symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit LabelSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::LabelSym), RecordOffset(RecordOffset) {}

  /// Return the byte offset of the relocatable code address field.
  ///
  /// \returns Byte offset of the relocatable code address within the stream.
  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  /// Offset of the label within \c Segment.
  uint32_t CodeOffset = 0;
  /// Segment containing the label.
  uint16_t Segment = 0;
  /// Label attribute flags.
  ProcSymFlags Flags = ProcSymFlags::None;
  /// Name of the label.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Object file name symbol (S_OBJNAME).
class ObjNameSym : public SymbolRecord {
public:
  /// Construct an empty object name symbol.
  explicit ObjNameSym() : SymbolRecord(SymbolRecordKind::ObjNameSym) {}
  /// Construct an object name symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit ObjNameSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct an object name symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit ObjNameSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::ObjNameSym), RecordOffset(RecordOffset) {
  }

  /// Signature identifying the object file.
  uint32_t Signature = 0;
  /// Path or name of the object file.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Environment block of key/value string pairs (S_ENVBLOCK).
class EnvBlockSym : public SymbolRecord {
public:
  /// Construct an environment block symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit EnvBlockSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct an environment block symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit EnvBlockSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::EnvBlockSym),
        RecordOffset(RecordOffset) {}

  /// Alternating environment key and value strings.
  std::vector<StringRef> Fields;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Exported symbol (S_EXPORT).
class ExportSym : public SymbolRecord {
public:
  /// Construct an export symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit ExportSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct an export symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit ExportSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::ExportSym), RecordOffset(RecordOffset) {}

  /// Export ordinal.
  uint16_t Ordinal = 0;
  /// Export attribute flags.
  ExportFlags Flags = ExportFlags::None;
  /// Name of the exported symbol.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// File-static variable symbol (S_FILESTATIC).
class FileStaticSym : public SymbolRecord {
public:
  /// Construct a file-static symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit FileStaticSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a file-static symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit FileStaticSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::FileStaticSym),
        RecordOffset(RecordOffset) {}

  /// Type index of the file-static variable.
  TypeIndex Index;
  /// Offset of the module filename in the string table.
  uint32_t ModFilenameOffset = 0;
  /// Local variable attribute flags.
  LocalSymFlags Flags = LocalSymFlags::None;
  /// Name of the file-static variable.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Compile flags symbol version 2 (S_COMPILE2).
class Compile2Sym : public SymbolRecord {
public:
  /// Construct a compile-2 symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit Compile2Sym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a compile-2 symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit Compile2Sym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::Compile2Sym),
        RecordOffset(RecordOffset) {}

  /// Compile flags, including the language in the low byte.
  CompileSym2Flags Flags = CompileSym2Flags::None;
  /// Target CPU type.
  CPUType Machine;
  /// Frontend major version number.
  uint16_t VersionFrontendMajor = 0;
  /// Frontend minor version number.
  uint16_t VersionFrontendMinor = 0;
  /// Frontend build number.
  uint16_t VersionFrontendBuild = 0;
  /// Backend major version number.
  uint16_t VersionBackendMajor = 0;
  /// Backend minor version number.
  uint16_t VersionBackendMinor = 0;
  /// Backend build number.
  uint16_t VersionBackendBuild = 0;
  /// Compiler version string.
  StringRef Version;
  /// Extra null-terminated version strings.
  std::vector<StringRef> ExtraStrings;

  /// Return the source language encoded in the low byte of \c Flags.
  ///
  /// \returns Source language code from the low byte of \c Flags.
  uint8_t getLanguage() const { return static_cast<uint32_t>(Flags) & 0xFF; }
  /// Return the compile flags with the language byte cleared.
  ///
  /// \returns Compile flags with the language byte cleared.
  uint32_t getFlags() const { return static_cast<uint32_t>(Flags) & ~0xFF; }

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Compile flags symbol version 3 (S_COMPILE3).
class Compile3Sym : public SymbolRecord {
public:
  /// Construct an empty compile-3 symbol.
  Compile3Sym() : SymbolRecord(SymbolRecordKind::Compile3Sym) {}
  /// Construct a compile-3 symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit Compile3Sym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a compile-3 symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit Compile3Sym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::Compile3Sym),
        RecordOffset(RecordOffset) {}

  /// Compile flags, including the language in the low byte.
  CompileSym3Flags Flags = CompileSym3Flags::None;
  /// Target CPU type.
  CPUType Machine;
  /// Frontend major version number.
  uint16_t VersionFrontendMajor = 0;
  /// Frontend minor version number.
  uint16_t VersionFrontendMinor = 0;
  /// Frontend build number.
  uint16_t VersionFrontendBuild = 0;
  /// Frontend QFE (hotfix) number.
  uint16_t VersionFrontendQFE = 0;
  /// Backend major version number.
  uint16_t VersionBackendMajor = 0;
  /// Backend minor version number.
  uint16_t VersionBackendMinor = 0;
  /// Backend build number.
  uint16_t VersionBackendBuild = 0;
  /// Backend QFE (hotfix) number.
  uint16_t VersionBackendQFE = 0;
  /// Compiler version string.
  StringRef Version;

  /// Set the source language encoded in the low byte of \c Flags.
  ///
  /// \param Lang Source language to store.
  void setLanguage(SourceLanguage Lang) {
    Flags = CompileSym3Flags((uint32_t(Flags) & 0xFFFFFF00) | uint32_t(Lang));
  }

  /// Return the source language encoded in the low byte of \c Flags.
  ///
  /// \returns Source language encoded in the low byte of \c Flags.
  SourceLanguage getLanguage() const {
    return static_cast<SourceLanguage>(static_cast<uint32_t>(Flags) & 0xFF);
  }
  /// Return the compile flags with the language byte cleared.
  ///
  /// \returns Compile flags with the language byte cleared.
  CompileSym3Flags getFlags() const {
    return static_cast<CompileSym3Flags>(static_cast<uint32_t>(Flags) & ~0xFF);
  }

  /// Return true if PGO or LTCG optimizations were enabled.
  ///
  /// \returns True if PGO or LTCG optimization flags are set.
  bool hasOptimizations() const {
    return CompileSym3Flags::None !=
           (getFlags() & (CompileSym3Flags::PGO | CompileSym3Flags::LTCG));
  }

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Frame procedure details symbol (S_FRAMEPROC).
class FrameProcSym : public SymbolRecord {
public:
  /// Construct a frame procedure symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit FrameProcSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a frame procedure symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit FrameProcSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::FrameProcSym),
        RecordOffset(RecordOffset) {}

  /// Total size of the frame in bytes.
  uint32_t TotalFrameBytes = 0;
  /// Number of padding bytes in the frame.
  uint32_t PaddingFrameBytes = 0;
  /// Offset within the frame where padding begins.
  uint32_t OffsetToPadding = 0;
  /// Number of bytes occupied by callee-saved registers.
  uint32_t BytesOfCalleeSavedRegisters = 0;
  /// Offset of the exception handler within its section.
  uint32_t OffsetOfExceptionHandler = 0;
  /// Section containing the exception handler.
  uint16_t SectionIdOfExceptionHandler = 0;
  /// Frame procedure option flags.
  FrameProcedureOptions Flags = FrameProcedureOptions::None;

  /// Extract the register this frame uses to refer to local variables.
  ///
  /// \param CPU Target CPU used to decode the encoded frame pointer register.
  /// \returns Register used to refer to local variables.
  RegisterId getLocalFramePtrReg(CPUType CPU) const {
    return decodeFramePtrReg(
        EncodedFramePtrReg((uint32_t(Flags) >> 14U) & 0x3U), CPU);
  }

  /// Extract the register this frame uses to refer to parameters.
  ///
  /// \param CPU Target CPU used to decode the encoded frame pointer register.
  /// \returns Register used to refer to parameters.
  RegisterId getParamFramePtrReg(CPUType CPU) const {
    return decodeFramePtrReg(
        EncodedFramePtrReg((uint32_t(Flags) >> 16U) & 0x3U), CPU);
  }

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;

private:
};

/// Indirect call site type information (S_CALLSITEINFO).
class CallSiteInfoSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 4;

public:
  /// Construct a call site info symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit CallSiteInfoSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a call site info symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit CallSiteInfoSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::CallSiteInfoSym) {}

  /// Return the byte offset of the relocatable code address field.
  ///
  /// \returns Byte offset of the relocatable code address within the stream.
  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  /// Offset of the call site within \c Segment.
  uint32_t CodeOffset = 0;
  /// Segment containing the call site.
  uint16_t Segment = 0;
  /// Type index of the called function type.
  TypeIndex Type;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Heap allocation site symbol (S_HEAPALLOCSITE).
class HeapAllocationSiteSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 4;

public:
  /// Construct a heap allocation site symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit HeapAllocationSiteSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a heap allocation site symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit HeapAllocationSiteSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::HeapAllocationSiteSym),
        RecordOffset(RecordOffset) {}

  /// Return the byte offset of the relocatable code address field.
  ///
  /// \returns Byte offset of the relocatable code address within the stream.
  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  /// Offset of the allocation call within \c Segment.
  uint32_t CodeOffset = 0;
  /// Segment containing the allocation call.
  uint16_t Segment = 0;
  /// Size of the call instruction in bytes.
  uint16_t CallInstructionSize = 0;
  /// Type index describing the allocated type.
  TypeIndex Type;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Security cookie on the stack frame (S_FRAMECOOKIE).
class FrameCookieSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 4;

public:
  /// Construct a frame cookie symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit FrameCookieSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a frame cookie symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit FrameCookieSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::FrameCookieSym) {}

  /// Return the byte offset of the relocatable code address field.
  ///
  /// \returns Byte offset of the relocatable code address within the stream.
  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  /// Offset of the cookie within the frame.
  uint32_t CodeOffset = 0;
  /// Register used as the frame base for the cookie.
  uint16_t Register = 0;
  /// Kind of frame cookie.
  FrameCookieKind CookieKind;
  /// Additional cookie flags.
  uint8_t Flags = 0;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// User-defined type symbol (S_UDT or S_COBOLUDT).
class UDTSym : public SymbolRecord {
public:
  /// Construct a UDT symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit UDTSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a UDT symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit UDTSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::UDTSym) {}

  /// Type index of the user-defined type.
  TypeIndex Type;
  /// Name of the user-defined type.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Build information symbol (S_BUILDINFO).
class BuildInfoSym : public SymbolRecord {
public:
  /// Construct a build info symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit BuildInfoSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a build info symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit BuildInfoSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::BuildInfoSym),
        RecordOffset(RecordOffset) {}

  /// Type index of the LF_BUILDINFO record.
  TypeIndex BuildId;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// BP-relative local variable symbol (S_BPREL32).
class BPRelativeSym : public SymbolRecord {
public:
  /// Construct a BP-relative symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit BPRelativeSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a BP-relative symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit BPRelativeSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::BPRelativeSym),
        RecordOffset(RecordOffset) {}

  /// Offset from BP to the variable.
  int32_t Offset = 0;
  /// Type index of the variable.
  TypeIndex Type;
  /// Name of the variable.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Register-relative local variable symbol (S_REGREL32).
class RegRelativeSym : public SymbolRecord {
public:
  /// Construct a register-relative symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit RegRelativeSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a register-relative symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit RegRelativeSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::RegRelativeSym),
        RecordOffset(RecordOffset) {}

  /// Offset from \c Register to the variable.
  uint32_t Offset = 0;
  /// Type index of the variable.
  TypeIndex Type;
  /// Base register for the relative location.
  RegisterId Register = RegisterId::NONE;
  /// Name of the variable.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// S_REGREL32_INDIR
///
/// \p Name is located at `*($Register + Offset) + OffsetInUDT` with type
/// \p Type.
class RegRelativeIndirSym : public SymbolRecord {
public:
  /// Construct an indirect register-relative symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit RegRelativeIndirSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct an indirect register-relative symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit RegRelativeIndirSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::RegRelativeIndirSym),
        RecordOffset(RecordOffset) {}

  /// Offset added to \c Register before dereferencing.
  uint32_t Offset = 0;
  /// Type index of the variable.
  TypeIndex Type;
  /// Offset added after dereferencing to reach the field within the UDT.
  uint32_t OffsetInUdt = 0;
  /// Base register for the indirect location.
  RegisterId Register = RegisterId::NONE;
  /// Name of the variable.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Constant value symbol (S_CONSTANT or S_MANCONSTANT).
class ConstantSym : public SymbolRecord {
public:
  /// Construct a constant symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit ConstantSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a constant symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit ConstantSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::ConstantSym),
        RecordOffset(RecordOffset) {}

  /// Type index of the constant.
  TypeIndex Type;
  /// Numeric value of the constant.
  APSInt Value;
  /// Name of the constant.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Data symbol for global or local data (S_LDATA32, S_GDATA32, and managed variants).
class DataSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 8;

public:
  /// Construct a data symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit DataSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a data symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit DataSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DataSym), RecordOffset(RecordOffset) {}

  /// Return the byte offset of the relocatable data address field.
  ///
  /// \returns Byte offset of the relocatable data address within the stream.
  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  /// Type index of the data.
  TypeIndex Type;
  /// Offset of the data within \c Segment.
  uint32_t DataOffset = 0;
  /// Segment containing the data.
  uint16_t Segment = 0;
  /// Name of the data symbol.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Thread-local data symbol (S_LTHREAD32 or S_GTHREAD32).
class ThreadLocalDataSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 8;

public:
  /// Construct a thread-local data symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit ThreadLocalDataSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a thread-local data symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit ThreadLocalDataSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::ThreadLocalDataSym),
        RecordOffset(RecordOffset) {}

  /// Return the byte offset of the relocatable data address field.
  ///
  /// \returns Byte offset of the relocatable data address within the stream.
  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  /// Type index of the thread-local data.
  TypeIndex Type;
  /// Offset of the data within \c Segment.
  uint32_t DataOffset = 0;
  /// Segment containing the thread-local data.
  uint16_t Segment = 0;
  /// Name of the thread-local data symbol.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Using-namespace symbol (S_UNAMESPACE).
class UsingNamespaceSym : public SymbolRecord {
public:
  /// Construct a using-namespace symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit UsingNamespaceSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct a using-namespace symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit UsingNamespaceSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::UsingNamespaceSym),
        RecordOffset(RecordOffset) {}

  /// Namespace brought into scope.
  StringRef Name;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Code annotation symbol (S_ANNOTATION).
class AnnotationSym : public SymbolRecord {
public:
  /// Construct an annotation symbol of the given kind.
  ///
  /// \param Kind Symbol record kind.
  explicit AnnotationSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  /// Construct an annotation symbol at the given stream offset.
  ///
  /// \param RecordOffset Byte offset of this record in the symbol stream.
  explicit AnnotationSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::AnnotationSym),
        RecordOffset(RecordOffset) {}

  /// Offset of the annotated code within \c Segment.
  uint32_t CodeOffset = 0;
  /// Segment containing the annotated code.
  uint16_t Segment = 0;
  /// Annotation strings associated with the code location.
  std::vector<StringRef> Strings;

  /// Byte offset of this record in the symbol stream.
  uint32_t RecordOffset = 0;
};

/// Read one CodeView symbol record from \p Stream at \p Offset.
///
/// \param Stream Binary stream containing CodeView symbol records.
/// \param Offset Byte offset of the record prefix within \p Stream.
///
/// \returns The parsed symbol record, or an error if the record is truncated
/// or corrupt.
LLVM_ABI Expected<CVSymbol> readSymbolFromStream(BinaryStreamRef Stream,
                                                 uint32_t Offset);

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_SYMBOLRECORD_H
