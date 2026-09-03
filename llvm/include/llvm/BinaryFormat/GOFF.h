//===-- llvm/BinaryFormat/GOFF.h - GOFF definitions --------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header contains common, non-processor-specific data structures and
// constants for the GOFF file format.
//
// GOFF specifics can be found in MVS Program Management: Advanced Facilities.
// See
// https://www.ibm.com/docs/en/zos/3.1.0?topic=facilities-generalized-object-file-format-goff
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_GOFF_H
#define LLVM_BINARYFORMAT_GOFF_H

#include "llvm/Support/DataTypes.h"

namespace llvm {

/// \brief Constants, structures, and helpers for the GOFF object-file format.
namespace GOFF {

/// \brief Length of the parts of a physical GOFF record.
constexpr uint8_t RecordLength = 80;
/// \brief Length of the three-byte physical record prefix.
constexpr uint8_t RecordPrefixLength = 3;
/// \brief Length of the data payload within a physical record.
constexpr uint8_t PayloadLength = 77;
/// \brief Length of record content excluding the prefix.
constexpr uint8_t RecordContentLength = RecordLength - RecordPrefixLength;

/// \brief Maximum data length before starting a new card for RLD and TXT data.
///
/// The maximum number of bytes that can be included in an RLD or TXT record and
/// their continuations is a SIGNED 16 bit int despite what the spec says. The
/// number of bytes we allow ourselves to attach to a card is thus arbitrarily
/// limited to 32K-1 bytes.
constexpr uint16_t MaxDataLength = 32 * 1024 - 1;

/// \brief Prefix byte on every record. This indicates GOFF format.
constexpr uint8_t PTVPrefix = 0x03;

/// \brief GOFF physical record type codes.
enum RecordType : uint8_t {
  RT_ESD = 0,  ///< External symbol definition (ESD) record.
  RT_TXT = 1,  ///< Text (TXT) data record.
  RT_RLD = 2,  ///< Relocation directory (RLD) record.
  RT_LEN = 3,  ///< Length (LEN) record.
  RT_END = 4,  ///< End-of-module (END) record.
  RT_HDR = 15, ///< Header (HDR) record.
};

/// \brief External symbol definition (ESD) symbol type codes.
enum ESDSymbolType : uint8_t {
  ESD_ST_SectionDefinition = 0, ///< Section definition symbol.
  ESD_ST_ElementDefinition = 1, ///< Element definition symbol.
  ESD_ST_LabelDefinition = 2,   ///< Label definition symbol.
  ESD_ST_PartReference = 3,     ///< Part reference symbol.
  ESD_ST_ExternalReference = 4, ///< External reference symbol.
};

/// \brief Namespace identifiers for ESD names.
enum ESDNameSpaceId : uint8_t {
  ESD_NS_ProgramManagementBinder = 0, ///< Program management binder namespace.
  ESD_NS_NormalName = 1,              ///< Normal (user) name namespace.
  ESD_NS_PseudoRegister = 2,          ///< Pseudo-register namespace.
  ESD_NS_Parts = 3                    ///< Parts namespace.
};

/// \brief Number of reserved doublewords associated with an ESD symbol.
enum ESDReserveQwords : uint8_t {
  ESD_RQ_0 = 0, ///< No reserved doublewords.
  ESD_RQ_1 = 1, ///< One reserved doubleword.
  ESD_RQ_2 = 2, ///< Two reserved doublewords.
  ESD_RQ_3 = 3  ///< Three reserved doublewords.
};

/// \brief Addressing mode (Amode) values for an ESD symbol.
enum ESDAmode : uint8_t {
  ESD_AMODE_None = 0, ///< No addressing mode specified.
  ESD_AMODE_24 = 1,   ///< 24-bit addressing mode.
  ESD_AMODE_31 = 2,   ///< 31-bit addressing mode.
  ESD_AMODE_ANY = 3,  ///< Any (24- or 31-bit) addressing mode.
  ESD_AMODE_64 = 4,   ///< 64-bit addressing mode.
  ESD_AMODE_MIN = 16, ///< Minimum Amode of the module's contributors.
};

/// \brief Residence mode (Rmode) values for an ESD symbol.
enum ESDRmode : uint8_t {
  ESD_RMODE_None = 0, ///< No residence mode specified.
  ESD_RMODE_24 = 1,   ///< 24-bit residence mode.
  ESD_RMODE_31 = 3,   ///< 31-bit residence mode.
  ESD_RMODE_64 = 4,   ///< 64-bit residence mode.
};

/// \brief Text-style classification for an ESD element.
enum ESDTextStyle : uint8_t {
  ESD_TS_ByteOriented = 0,  ///< Byte-oriented text.
  ESD_TS_Structured = 1,    ///< Structured text.
  ESD_TS_Unstructured = 2,  ///< Unstructured text.
};

/// \brief Binding algorithm used when combining ESD elements.
enum ESDBindingAlgorithm : uint8_t {
  ESD_BA_Concatenate = 0, ///< Concatenate contributing elements.
  ESD_BA_Merge = 1,       ///< Merge contributing elements.
};

/// \brief Tasking / reusability behavior for an ESD section.
enum ESDTaskingBehavior : uint8_t {
  ESD_TA_Unspecified = 0, ///< Tasking behavior not specified.
  ESD_TA_NonReus = 1,     ///< Non-reusable.
  ESD_TA_Reus = 2,        ///< Serially reusable.
  ESD_TA_Rent = 3,        ///< Reentrant.
};

/// \brief Whether an ESD symbol describes code or data.
enum ESDExecutable : uint8_t {
  ESD_EXE_Unspecified = 0, ///< Executable property not specified.
  ESD_EXE_DATA = 1,        ///< Data (non-executable).
  ESD_EXE_CODE = 2,        ///< Executable code.
};

/// \brief Severity applied when a duplicate ESD symbol is encountered.
enum ESDDuplicateSymbolSeverity : uint8_t {
  ESD_DSS_NoWarning = 0, ///< Do not warn on duplicates.
  ESD_DSS_Warning = 1,   ///< Warn on duplicates.
  ESD_DSS_Error = 2,     ///< Treat duplicates as an error.
  ESD_DSS_Reserved = 3,  ///< Reserved severity value.
};

/// \brief Binding strength of an ESD symbol.
enum ESDBindingStrength : uint8_t {
  ESD_BST_Strong = 0, ///< Strong binding.
  ESD_BST_Weak = 1,   ///< Weak binding.
};

/// \brief Loading behavior requested for an ESD symbol.
enum ESDLoadingBehavior : uint8_t {
  ESD_LB_Initial = 0,  ///< Load with the initial module load.
  ESD_LB_Deferred = 1, ///< Defer loading until first reference.
  ESD_LB_NoLoad = 2,   ///< Do not load.
  ESD_LB_Reserved = 3, ///< Reserved loading-behavior value.
};

/// \brief Binding scope of an ESD symbol.
enum ESDBindingScope : uint8_t {
  ESD_BSC_Unspecified = 0,  ///< Binding scope not specified.
  ESD_BSC_Section = 1,      ///< Section scope.
  ESD_BSC_Module = 2,       ///< Module scope.
  ESD_BSC_Library = 3,      ///< Library scope.
  ESD_BSC_ImportExport = 4, ///< Import/export scope.
};

/// \brief Linkage convention associated with an ESD symbol.
enum ESDLinkageType : uint8_t {
  ESD_LT_OS = 0,     ///< Standard OS linkage.
  ESD_LT_XPLink = 1, ///< Extra-performance linkage (XPLINK).
};

/// \brief Alignment requirement encoded in an ESD behavioral attribute.
enum ESDAlignment : uint8_t {
  ESD_ALIGN_Byte = 0,      ///< Byte alignment.
  ESD_ALIGN_Halfword = 1,  ///< Halfword (2-byte) alignment.
  ESD_ALIGN_Fullword = 2,  ///< Fullword (4-byte) alignment.
  ESD_ALIGN_Doubleword = 3, ///< Doubleword (8-byte) alignment.
  ESD_ALIGN_Quadword = 4,  ///< Quadword (16-byte) alignment.
  ESD_ALIGN_32byte = 5,    ///< 32-byte alignment.
  ESD_ALIGN_64byte = 6,    ///< 64-byte alignment.
  ESD_ALIGN_128byte = 7,   ///< 128-byte alignment.
  ESD_ALIGN_256byte = 8,   ///< 256-byte alignment.
  ESD_ALIGN_512byte = 9,   ///< 512-byte alignment.
  ESD_ALIGN_1024byte = 10, ///< 1024-byte alignment.
  ESD_ALIGN_2Kpage = 11,   ///< 2K-page alignment.
  ESD_ALIGN_4Kpage = 12,   ///< 4K-page alignment.
};

/// \brief Relocation target type encoded in an RLD entry.
enum RLDReferenceType : uint8_t {
  RLD_RT_RAddress = 0,            ///< Relocate an address.
  RLD_RT_ROffset = 1,             ///< Relocate an offset.
  RLD_RT_RLength = 2,             ///< Relocate a length.
  RLD_RT_RRelativeImmediate = 6,  ///< Relative immediate relocation.
  RLD_RT_RTypeConstant = 7,       ///< Type-constant relocation.
  RLD_RT_RLongDisplacement = 9,   ///< Long-displacement relocation.
};

/// \brief Kind of referent named by an RLD entry.
enum RLDReferentType : uint8_t {
  RLD_RO_Label = 0,   ///< Referent is a label.
  RLD_RO_Element = 1, ///< Referent is an element.
  RLD_RO_Class = 2,   ///< Referent is a class.
  RLD_RO_Part = 3,    ///< Referent is a part.
};

/// \brief Arithmetic action performed by an RLD relocation.
enum RLDAction : uint8_t {
  RLD_ACT_Add = 0,      ///< Add the relocation value.
  RLD_ACT_Subtract = 1, ///< Subtract the relocation value.
};

/// \brief Whether an RLD entry fetches or stores the relocated value.
enum RLDFetchStore : uint8_t {
  RLD_FS_Fetch = 0, ///< Fetch (load) the relocated value.
  RLD_FS_Store = 1, ///< Store the relocated value.
};

/// \brief How an END record identifies the module entry point.
enum ENDEntryPointRequest : uint8_t {
  END_EPR_None = 0,         ///< No entry point requested.
  END_EPR_EsdidOffset = 1,  ///< Entry point given as ESDID plus offset.
  END_EPR_ExternalName = 2, ///< Entry point given as an external name.
  END_EPR_Reserved = 3,     ///< Reserved entry-point request value.
};

/// \brief Bitfield helper that numbers bits in System/390 order.
///
/// The standard System/390 convention is to name the high-order (leftmost) bit
/// in a byte as bit zero. The Flags type helps to set bits in a byte according
/// to this numeration order.
class Flags {
  uint8_t Val = 0;

  constexpr static uint8_t bits(uint8_t BitIndex, uint8_t Length, uint8_t Value,
                                uint8_t OldValue) {
    uint8_t Pos = 8 - BitIndex - Length;
    uint8_t Mask = ((1 << Length) - 1) << Pos;
    Value = Value << Pos;
    return (OldValue & ~Mask) | Value;
  }

public:
  /// \brief Construct an empty flags byte.
  constexpr Flags() = default;
  /// \brief Construct flags with a single bitfield initialized.
  /// \param BitIndex Index of the high-order bit of the field (0 = leftmost).
  /// \param Length Width of the field in bits.
  /// \param Value Value to store in the field.
  constexpr Flags(uint8_t BitIndex, uint8_t Length, uint8_t Value)
      : Val(bits(BitIndex, Length, Value, 0)) {}

  /// \brief Set a bitfield within the flags byte.
  /// \tparam T Type of \p NewValue; cast to \c uint8_t before packing.
  /// \param BitIndex Index of the high-order bit of the field (0 = leftmost).
  /// \param Length Width of the field in bits.
  /// \param NewValue Value to store in the field.
  template <typename T>
  constexpr void set(uint8_t BitIndex, uint8_t Length, T NewValue) {
    Val = bits(BitIndex, Length, static_cast<uint8_t>(NewValue), Val);
  }

  /// \brief Extract a bitfield from the flags byte.
  /// \tparam T Type used to return the extracted value.
  /// \param BitIndex Index of the high-order bit of the field (0 = leftmost).
  /// \param Length Width of the field in bits.
  /// \return The extracted field value cast to \c T.
  template <typename T>
  constexpr T get(uint8_t BitIndex, uint8_t Length) const {
    return static_cast<T>((Val >> (8 - BitIndex - Length)) &
                          ((1 << Length) - 1));
  }

  /// \brief Convert the flags to their underlying byte value.
  /// \return The packed flags as a byte.
  constexpr operator uint8_t() const { return Val; }
};

/// \brief Flag field of an external symbol definition (ESD) record.
///
/// See
/// https://www.ibm.com/docs/en/zos/3.1.0?topic=formats-external-symbol-definition-record
/// at offset 41 for the definition.
struct SymbolFlags {
  /// \brief Packed symbol-flag bits.
  Flags SymFlags;

  /// \brief Set whether a fill byte is present for the symbol.
  /// \param Val True if a fill byte is present.
  void setFillBytePresence(bool Val) { SymFlags.set<bool>(0, 1, Val); }
  /// \brief Get whether a fill byte is present for the symbol.
  /// \return True if a fill byte is present.
  bool getFillBytePresence() const { return SymFlags.get<bool>(0, 1); }

  /// \brief Set whether the symbol name is mangled.
  /// \param Val True if the name is mangled.
  void setMangled(bool Val) { SymFlags.set<bool>(1, 1, Val); }
  /// \brief Get whether the symbol name is mangled.
  /// \return True if the name is mangled.
  bool getMangled() const { return SymFlags.get<bool>(1, 1); }

  /// \brief Set whether the symbol is renameable.
  /// \param Val True if the symbol may be renamed.
  void setRenameable(bool Val) { SymFlags.set<bool>(2, 1, Val); }
  /// \brief Get whether the symbol is renameable.
  /// \return True if the symbol may be renamed.
  bool getRenameable() const { return SymFlags.get<bool>(2, 1); }

  /// \brief Set whether the symbol is a removable class.
  /// \param Val True if the class is removable.
  void setRemovableClass(bool Val) { SymFlags.set<bool>(3, 1, Val); }
  /// \brief Get whether the symbol is a removable class.
  /// \return True if the class is removable.
  bool getRemovableClass() const { return SymFlags.get<bool>(3, 1); }

  /// \brief Set the number of reserved doublewords for the symbol.
  /// \param Val Reserved-doubleword count.
  void setReservedQwords(ESDReserveQwords Val) {
    SymFlags.set<ESDReserveQwords>(5, 3, Val);
  }
  /// \brief Get the number of reserved doublewords for the symbol.
  /// \return Reserved-doubleword count.
  ESDReserveQwords getReservedQwords() const {
    return SymFlags.get<ESDReserveQwords>(5, 3);
  }

  /// \brief Convert the symbol flags to their underlying byte value.
  /// \return The packed symbol flags as a byte.
  constexpr operator uint8_t() const { return static_cast<uint8_t>(SymFlags); }
};

/// \brief Behavioral attributes of an external symbol definition (ESD) record.
///
/// See
/// https://www.ibm.com/docs/en/zos/3.1.0?topic=record-external-symbol-definition-behavioral-attributes
/// for the definition.
struct BehavioralAttributes {
  /// \brief Packed behavioral-attribute flag bytes.
  Flags Attr[10];

  /// \brief Set the addressing mode (Amode).
  /// \param Val Addressing mode value.
  void setAmode(GOFF::ESDAmode Val) { Attr[0].set<GOFF::ESDAmode>(0, 8, Val); }
  /// \brief Get the addressing mode (Amode).
  /// \return Addressing mode value.
  GOFF::ESDAmode getAmode() const {
    return Attr[0].get<GOFF::ESDAmode>(0, 8);
  }

  /// \brief Set the residence mode (Rmode).
  /// \param Val Residence mode value.
  void setRmode(GOFF::ESDRmode Val) { Attr[1].set<GOFF::ESDRmode>(0, 8, Val); }
  /// \brief Get the residence mode (Rmode).
  /// \return Residence mode value.
  GOFF::ESDRmode getRmode() const {
    return Attr[1].get<GOFF::ESDRmode>(0, 8);
  }

  /// \brief Set the text style of the element.
  /// \param Val Text style value.
  void setTextStyle(GOFF::ESDTextStyle Val) {
    Attr[2].set<GOFF::ESDTextStyle>(0, 4, Val);
  }
  /// \brief Get the text style of the element.
  /// \return Text style value.
  GOFF::ESDTextStyle getTextStyle() const {
    return Attr[2].get<GOFF::ESDTextStyle>(0, 4);
  }

  /// \brief Set the binding algorithm for the element.
  /// \param Val Binding algorithm value.
  void setBindingAlgorithm(GOFF::ESDBindingAlgorithm Val) {
    Attr[2].set<GOFF::ESDBindingAlgorithm>(4, 4, Val);
  }
  /// \brief Get the binding algorithm for the element.
  /// \return Binding algorithm value.
  GOFF::ESDBindingAlgorithm getBindingAlgorithm() const {
    return Attr[2].get<GOFF::ESDBindingAlgorithm>(4, 4);
  }

  /// \brief Set the tasking / reusability behavior.
  /// \param Val Tasking behavior value.
  void setTaskingBehavior(GOFF::ESDTaskingBehavior Val) {
    Attr[3].set<GOFF::ESDTaskingBehavior>(0, 3, Val);
  }
  /// \brief Get the tasking / reusability behavior.
  /// \return Tasking behavior value.
  GOFF::ESDTaskingBehavior getTaskingBehavior() const {
    return Attr[3].get<GOFF::ESDTaskingBehavior>(0, 3);
  }

  /// \brief Set whether the symbol is read-only.
  /// \param Val True if the symbol is read-only.
  void setReadOnly(bool Val) { Attr[3].set<bool>(4, 1, Val); }
  /// \brief Get whether the symbol is read-only.
  /// \return True if the symbol is read-only.
  bool getReadOnly() const { return Attr[3].get<bool>(4, 1); }

  /// \brief Set whether the symbol is executable code or data.
  /// \param Val Executable classification value.
  void setExecutable(GOFF::ESDExecutable Val) {
    Attr[3].set<GOFF::ESDExecutable>(5, 3, Val);
  }
  /// \brief Get whether the symbol is executable code or data.
  /// \return Executable classification value.
  GOFF::ESDExecutable getExecutable() const {
    return Attr[3].get<GOFF::ESDExecutable>(5, 3);
  }

  /// \brief Set the severity for duplicate symbols.
  /// \param Val Duplicate-symbol severity value.
  void setDuplicateSymbolSeverity(GOFF::ESDDuplicateSymbolSeverity Val) {
    Attr[4].set<GOFF::ESDDuplicateSymbolSeverity>(2, 2, Val);
  }
  /// \brief Get the severity for duplicate symbols.
  /// \return Duplicate-symbol severity value.
  GOFF::ESDDuplicateSymbolSeverity getDuplicateSymbolSeverity() const {
    return Attr[4].get<GOFF::ESDDuplicateSymbolSeverity>(2, 2);
  }

  /// \brief Set the binding strength of the symbol.
  /// \param Val Binding strength value.
  void setBindingStrength(GOFF::ESDBindingStrength Val) {
    Attr[4].set<GOFF::ESDBindingStrength>(4, 4, Val);
  }
  /// \brief Get the binding strength of the symbol.
  /// \return Binding strength value.
  GOFF::ESDBindingStrength getBindingStrength() const {
    return Attr[4].get<GOFF::ESDBindingStrength>(4, 4);
  }

  /// \brief Set the loading behavior of the symbol.
  /// \param Val Loading behavior value.
  void setLoadingBehavior(GOFF::ESDLoadingBehavior Val) {
    Attr[5].set<GOFF::ESDLoadingBehavior>(0, 2, Val);
  }
  /// \brief Get the loading behavior of the symbol.
  /// \return Loading behavior value.
  GOFF::ESDLoadingBehavior getLoadingBehavior() const {
    return Attr[5].get<GOFF::ESDLoadingBehavior>(0, 2);
  }

  /// \brief Set whether the symbol is a COMMON area.
  /// \param Val True if the symbol is COMMON.
  void setCOMMON(bool Val) { Attr[5].set<bool>(2, 1, Val); }
  /// \brief Get whether the symbol is a COMMON area.
  /// \return True if the symbol is COMMON.
  bool getCOMMON() const { return Attr[5].get<bool>(2, 1); }

  /// \brief Set whether the symbol is an indirect reference.
  /// \param Val True if the symbol is an indirect reference.
  void setIndirectReference(bool Val) { Attr[5].set<bool>(3, 1, Val); }
  /// \brief Get whether the symbol is an indirect reference.
  /// \return True if the symbol is an indirect reference.
  bool getIndirectReference() const { return Attr[5].get<bool>(3, 1); }

  /// \brief Set the binding scope of the symbol.
  /// \param Val Binding scope value.
  void setBindingScope(GOFF::ESDBindingScope Val) {
    Attr[5].set<GOFF::ESDBindingScope>(4, 4, Val);
  }
  /// \brief Get the binding scope of the symbol.
  /// \return Binding scope value.
  GOFF::ESDBindingScope getBindingScope() const {
    return Attr[5].get<GOFF::ESDBindingScope>(4, 4);
  }

  /// \brief Set the linkage type of the symbol.
  /// \param Val Linkage type value.
  void setLinkageType(GOFF::ESDLinkageType Val) {
    Attr[6].set<GOFF::ESDLinkageType>(2, 1, Val);
  }
  /// \brief Get the linkage type of the symbol.
  /// \return Linkage type value.
  GOFF::ESDLinkageType getLinkageType() const {
    return Attr[6].get<GOFF::ESDLinkageType>(2, 1);
  }

  /// \brief Set the alignment requirement of the symbol.
  /// \param Val Alignment value.
  void setAlignment(GOFF::ESDAlignment Val) {
    Attr[6].set<GOFF::ESDAlignment>(3, 5, Val);
  }
  /// \brief Get the alignment requirement of the symbol.
  /// \return Alignment value.
  GOFF::ESDAlignment getAlignment() const {
    return Attr[6].get<GOFF::ESDAlignment>(3, 5);
  }
};
} // end namespace GOFF

} // end namespace llvm

#endif // LLVM_BINARYFORMAT_GOFF_H
