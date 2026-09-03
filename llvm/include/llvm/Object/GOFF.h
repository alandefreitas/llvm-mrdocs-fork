//===- GOFF.h - GOFF object file implementation -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the GOFFObjectFile class.
// Record classes and derivatives are also declared and implemented.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_GOFF_H
#define LLVM_OBJECT_GOFF_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/GOFF.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace object {

/// \brief Represents a GOFF physical record.
///
/// Specifies protected member functions to manipulate the record. These should
/// be called from deriving classes to change values as that record specifies.
class Record {
public:
  /// \brief Collect continuous data from a record and its continuations.
  ///
  /// \param Record Pointer to the initial physical record.
  /// \param DataLength Number of data bytes to collect.
  /// \param DataIndex Byte index of the data field within the record.
  /// \param CompleteData Buffer that receives the concatenated data.
  /// \return Error::success() on success, or an error if the data is truncated.
  LLVM_ABI static Error getContinuousData(const uint8_t *Record,
                                          uint16_t DataLength, int DataIndex,
                                          SmallString<256> &CompleteData);

  /// \brief Return true if this record is continued by a following record.
  ///
  /// \param Record Pointer to the physical record.
  /// \return True if the continued flag is set.
  static bool isContinued(const uint8_t *Record) {
    uint8_t IsContinued;
    getBits(Record, 1, 7, 1, IsContinued);
    return IsContinued;
  }

  /// \brief Return true if this record is a continuation of a prior record.
  ///
  /// \param Record Pointer to the physical record.
  /// \return True if the continuation flag is set.
  static bool isContinuation(const uint8_t *Record) {
    uint8_t IsContinuation;
    getBits(Record, 1, 6, 1, IsContinuation);
    return IsContinuation;
  }

protected:
  /// \brief Get bit field of specified byte.
  ///
  /// Used to pack bit fields into one byte. Fields are packed left to right.
  /// Bit index zero is the most significant bit of the byte.
  ///
  /// \param Bytes Pointer to the physical record bytes.
  /// \param ByteIndex index of byte the field is in.
  /// \param BitIndex index of first bit of field.
  /// \param Length length of bit field.
  /// \param Value value of bit field.
  static void getBits(const uint8_t *Bytes, uint8_t ByteIndex, uint8_t BitIndex,
                      uint8_t Length, uint8_t &Value) {
    assert(ByteIndex < GOFF::RecordLength && "Byte index out of bounds!");
    assert(BitIndex < 8 && "Bit index out of bounds!");
    assert(Length + BitIndex <= 8 && "Bit length too long!");

    get<uint8_t>(Bytes, ByteIndex, Value);
    Value = (Value >> (8 - BitIndex - Length)) & ((1 << Length) - 1);
  }

  /// \brief Read a big-endian value of type \p T from the record.
  ///
  /// \tparam T Value type to read.
  /// \param Bytes Pointer to the physical record bytes.
  /// \param ByteIndex Index of the first byte of the value.
  /// \param Value Receives the decoded value.
  template <class T>
  static void get(const uint8_t *Bytes, uint8_t ByteIndex, T &Value) {
    assert(ByteIndex + sizeof(T) <= GOFF::RecordLength &&
           "Byte index out of bounds!");
    Value = support::endian::read<T, llvm::endianness::big>(&Bytes[ByteIndex]);
  }
};

/// \brief Represents a GOFF text (TXT) physical record.
class TXTRecord : public Record {
public:
  /// \brief Maximum length of data; any more must go in continuation.
  static const uint8_t TXTMaxDataLength = 56;

  /// \brief Get the text data from a TXT record and its continuations.
  ///
  /// \param Record Pointer to the initial TXT physical record.
  /// \param CompleteData Buffer that receives the concatenated text data.
  /// \return Error::success() on success, or an error if the data is truncated.
  LLVM_ABI static Error getData(const uint8_t *Record,
                                SmallString<256> &CompleteData);

  /// \brief Get the element ESD identifier associated with this text.
  ///
  /// \param Record Pointer to the TXT physical record.
  /// \param EsdId Receives the element ESD identifier.
  static void getElementEsdId(const uint8_t *Record, uint32_t &EsdId) {
    get<uint32_t>(Record, 4, EsdId);
  }

  /// \brief Get the offset of the text within the element.
  ///
  /// \param Record Pointer to the TXT physical record.
  /// \param Offset Receives the text offset.
  static void getOffset(const uint8_t *Record, uint32_t &Offset) {
    get<uint32_t>(Record, 12, Offset);
  }

  /// \brief Get the length of the text data in this record.
  ///
  /// \param Record Pointer to the TXT physical record.
  /// \param Length Receives the text data length.
  static void getDataLength(const uint8_t *Record, uint16_t &Length) {
    get<uint16_t>(Record, 22, Length);
  }
};

/// \brief Represents a GOFF header (HDR) physical record.
class HDRRecord : public Record {
public:
  /// \brief Get the property module data from an HDR record and continuations.
  ///
  /// \param Record Pointer to the initial HDR physical record.
  /// \param CompleteData Buffer that receives the concatenated property data.
  /// \return Error::success() on success, or an error if the data is truncated.
  LLVM_ABI static Error getData(const uint8_t *Record,
                                SmallString<256> &CompleteData);

  /// \brief Get the length of the property module data.
  ///
  /// \param Record Pointer to the HDR physical record.
  /// \return Length of the property module data in bytes.
  static uint16_t getPropertyModuleLength(const uint8_t *Record) {
    uint16_t Length;
    get<uint16_t>(Record, 52, Length);
    return Length;
  }
};

/// \brief Represents a GOFF external symbol definition (ESD) physical record.
class ESDRecord : public Record {
public:
  /// \brief Number of bytes for name; any more must go in continuation.
  /// This is the number of bytes that can fit into the data field of an ESD
  /// record.
  static const uint8_t ESDMaxUncontinuedNameLength = 8;

  /// \brief Maximum name length for ESD records and continuations.
  ///
  /// This is the number of bytes that can fit into the data field of an ESD
  /// record AND following continuations. This is limited fundamentally by the
  /// 16 bit SIGNED length field.
  static const uint16_t MaxNameLength = 32 * 1024;

public:
  /// \brief Get the symbol name data from an ESD record and its continuations.
  ///
  /// \param Record Pointer to the initial ESD physical record.
  /// \param CompleteData Buffer that receives the concatenated name data.
  /// \return Error::success() on success, or an error if the data is truncated.
  LLVM_ABI static Error getData(const uint8_t *Record,
                                SmallString<256> &CompleteData);

  // ESD Get routines.
  /// \brief Get the ESD symbol type.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param SymbolType Receives the symbol type.
  static void getSymbolType(const uint8_t *Record,
                            GOFF::ESDSymbolType &SymbolType) {
    uint8_t Value;
    get<uint8_t>(Record, 3, Value);
    SymbolType = (GOFF::ESDSymbolType)Value;
  }

  /// \brief Get the ESD identifier for this symbol.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param EsdId Receives the ESD identifier.
  static void getEsdId(const uint8_t *Record, uint32_t &EsdId) {
    get<uint32_t>(Record, 4, EsdId);
  }

  /// \brief Get the parent ESD identifier for this symbol.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param EsdId Receives the parent ESD identifier.
  static void getParentEsdId(const uint8_t *Record, uint32_t &EsdId) {
    get<uint32_t>(Record, 8, EsdId);
  }

  /// \brief Get the offset of the symbol within its parent.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Offset Receives the symbol offset.
  static void getOffset(const uint8_t *Record, uint32_t &Offset) {
    get<uint32_t>(Record, 16, Offset);
  }

  /// \brief Get the length of the symbol.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Length Receives the symbol length.
  static void getLength(const uint8_t *Record, uint32_t &Length) {
    get<uint32_t>(Record, 24, Length);
  }

  /// \brief Get the name-space identifier of the symbol.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Id Receives the name-space identifier.
  static void getNameSpaceId(const uint8_t *Record, GOFF::ESDNameSpaceId &Id) {
    uint8_t Value;
    get<uint8_t>(Record, 40, Value);
    Id = (GOFF::ESDNameSpaceId)Value;
  }

  /// \brief Get whether a fill byte is present for the symbol.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Present Receives true if a fill byte is present.
  static void getFillBytePresent(const uint8_t *Record, bool &Present) {
    uint8_t Value;
    getBits(Record, 41, 0, 1, Value);
    Present = (bool)Value;
  }

  /// \brief Get whether the symbol name is mangled.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Mangled Receives true if the name is mangled.
  static void getNameMangled(const uint8_t *Record, bool &Mangled) {
    uint8_t Value;
    getBits(Record, 41, 1, 1, Value);
    Mangled = (bool)Value;
  }

  /// \brief Get whether the symbol is renamable.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Renamable Receives true if the symbol may be renamed.
  static void getRenamable(const uint8_t *Record, bool &Renamable) {
    uint8_t Value;
    getBits(Record, 41, 2, 1, Value);
    Renamable = (bool)Value;
  }

  /// \brief Get whether the symbol is a removable class.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Removable Receives true if the class is removable.
  static void getRemovable(const uint8_t *Record, bool &Removable) {
    uint8_t Value;
    getBits(Record, 41, 3, 1, Value);
    Removable = (bool)Value;
  }

  /// \brief Get the fill byte value for the symbol.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Fill Receives the fill byte value.
  static void getFillByteValue(const uint8_t *Record, uint8_t &Fill) {
    get<uint8_t>(Record, 42, Fill);
  }

  /// \brief Get the associated data area (ADA) ESD identifier.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param EsdId Receives the ADA ESD identifier.
  static void getAdaEsdId(const uint8_t *Record, uint32_t &EsdId) {
    get<uint32_t>(Record, 44, EsdId);
  }

  /// \brief Get the sort priority of the symbol.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Priority Receives the sort priority.
  static void getSortPriority(const uint8_t *Record, uint32_t &Priority) {
    get<uint32_t>(Record, 48, Priority);
  }

  /// \brief Get the addressing mode (Amode) of the symbol.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Amode Receives the addressing mode.
  static void getAmode(const uint8_t *Record, GOFF::ESDAmode &Amode) {
    uint8_t Value;
    get<uint8_t>(Record, 60, Value);
    Amode = (GOFF::ESDAmode)Value;
  }

  /// \brief Get the residence mode (Rmode) of the symbol.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Rmode Receives the residence mode.
  static void getRmode(const uint8_t *Record, GOFF::ESDRmode &Rmode) {
    uint8_t Value;
    get<uint8_t>(Record, 61, Value);
    Rmode = (GOFF::ESDRmode)Value;
  }

  /// \brief Get the text style of the element.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Style Receives the text style.
  static void getTextStyle(const uint8_t *Record, GOFF::ESDTextStyle &Style) {
    uint8_t Value;
    getBits(Record, 62, 0, 4, Value);
    Style = (GOFF::ESDTextStyle)Value;
  }

  /// \brief Get the binding algorithm for the element.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Algorithm Receives the binding algorithm.
  static void getBindingAlgorithm(const uint8_t *Record,
                                  GOFF::ESDBindingAlgorithm &Algorithm) {
    uint8_t Value;
    getBits(Record, 62, 4, 4, Value);
    Algorithm = (GOFF::ESDBindingAlgorithm)Value;
  }

  /// \brief Get the tasking / reusability behavior of the symbol.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param TaskingBehavior Receives the tasking behavior.
  static void getTaskingBehavior(const uint8_t *Record,
                                 GOFF::ESDTaskingBehavior &TaskingBehavior) {
    uint8_t Value;
    getBits(Record, 63, 0, 3, Value);
    TaskingBehavior = (GOFF::ESDTaskingBehavior)Value;
  }

  /// \brief Get whether the symbol is read-only.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param ReadOnly Receives true if the symbol is read-only.
  static void getReadOnly(const uint8_t *Record, bool &ReadOnly) {
    uint8_t Value;
    getBits(Record, 63, 4, 1, Value);
    ReadOnly = (bool)Value;
  }

  /// \brief Get whether the symbol is executable code or data.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Executable Receives the executable classification.
  static void getExecutable(const uint8_t *Record,
                            GOFF::ESDExecutable &Executable) {
    uint8_t Value;
    getBits(Record, 63, 5, 3, Value);
    Executable = (GOFF::ESDExecutable)Value;
  }

  /// \brief Get the severity for duplicate symbols.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param DSS Receives the duplicate-symbol severity.
  static void getDuplicateSeverity(const uint8_t *Record,
                                   GOFF::ESDDuplicateSymbolSeverity &DSS) {
    uint8_t Value;
    getBits(Record, 64, 2, 2, Value);
    DSS = (GOFF::ESDDuplicateSymbolSeverity)Value;
  }

  /// \brief Get the binding strength of the symbol.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Strength Receives the binding strength.
  static void getBindingStrength(const uint8_t *Record,
                                 GOFF::ESDBindingStrength &Strength) {
    uint8_t Value;
    getBits(Record, 64, 4, 4, Value);
    Strength = (GOFF::ESDBindingStrength)Value;
  }

  /// \brief Get the loading behavior of the symbol.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Behavior Receives the loading behavior.
  static void getLoadingBehavior(const uint8_t *Record,
                                 GOFF::ESDLoadingBehavior &Behavior) {
    uint8_t Value;
    getBits(Record, 65, 0, 2, Value);
    Behavior = (GOFF::ESDLoadingBehavior)Value;
  }

  /// \brief Get whether the symbol is an indirect reference.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Indirect Receives true if the symbol is an indirect reference.
  static void getIndirectReference(const uint8_t *Record, bool &Indirect) {
    uint8_t Value;
    getBits(Record, 65, 3, 1, Value);
    Indirect = (bool)Value;
  }

  /// \brief Get the binding scope of the symbol.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Scope Receives the binding scope.
  static void getBindingScope(const uint8_t *Record,
                              GOFF::ESDBindingScope &Scope) {
    uint8_t Value;
    getBits(Record, 65, 4, 4, Value);
    Scope = (GOFF::ESDBindingScope)Value;
  }

  /// \brief Get the linkage type of the symbol.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Type Receives the linkage type.
  static void getLinkageType(const uint8_t *Record,
                             GOFF::ESDLinkageType &Type) {
    uint8_t Value;
    getBits(Record, 66, 2, 1, Value);
    Type = (GOFF::ESDLinkageType)Value;
  }

  /// \brief Get the alignment requirement of the symbol.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \param Alignment Receives the alignment.
  static void getAlignment(const uint8_t *Record,
                           GOFF::ESDAlignment &Alignment) {
    uint8_t Value;
    getBits(Record, 66, 3, 5, Value);
    Alignment = (GOFF::ESDAlignment)Value;
  }

  /// \brief Get the length of the symbol name.
  ///
  /// \param Record Pointer to the ESD physical record.
  /// \return Length of the symbol name in bytes.
  static uint16_t getNameLength(const uint8_t *Record) {
    uint16_t Length;
    get<uint16_t>(Record, 70, Length);
    return Length;
  }
};

/// \brief Represents a GOFF end-of-module (END) physical record.
class ENDRecord : public Record {
public:
  /// \brief Get the entry-point name data from an END record and continuations.
  ///
  /// \param Record Pointer to the initial END physical record.
  /// \param CompleteData Buffer that receives the concatenated name data.
  /// \return Error::success() on success, or an error if the data is truncated.
  LLVM_ABI static Error getData(const uint8_t *Record,
                                SmallString<256> &CompleteData);

  /// \brief Get the length of the entry-point name.
  ///
  /// \param Record Pointer to the END physical record.
  /// \return Length of the entry-point name in bytes.
  static uint16_t getNameLength(const uint8_t *Record) {
    uint16_t Length;
    get<uint16_t>(Record, 24, Length);
    return Length;
  }
};

} // end namespace object
} // end namespace llvm

#endif
