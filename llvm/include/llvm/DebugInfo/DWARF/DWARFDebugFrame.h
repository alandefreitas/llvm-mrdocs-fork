//===- DWARFDebugFrame.h - Parsing of .debug_frame --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGFRAME_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGFRAME_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/iterator.h"
#include "llvm/DebugInfo/DWARF/LowLevel/DWARFCFIProgram.h"
#include "llvm/DebugInfo/DWARF/LowLevel/DWARFExpression.h"
#include "llvm/DebugInfo/DWARF/LowLevel/DWARFUnwindTable.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"
#include <memory>
#include <vector>

namespace llvm {

class raw_ostream;
class DWARFDataExtractor;
class MCRegisterInfo;
struct DIDumpOptions;

namespace dwarf {

class CIE;

/// Create an UnwindTable from a Common Information Entry (CIE).
///
/// \param Cie The Common Information Entry to extract the table from. The
/// CFIProgram is retrieved from the \a Cie object and used to create the
/// UnwindTable.
///
/// \returns An error if the DWARF Call Frame Information opcodes have state
/// machine errors, or a valid UnwindTable otherwise.
LLVM_ABI Expected<UnwindTable> createUnwindTable(const CIE *Cie);

class FDE;

/// Create an UnwindTable from a Frame Descriptor Entry (FDE).
///
/// \param Fde The Frame Descriptor Entry to extract the table from. The
/// CFIProgram is retrieved from the \a Fde object and used to create the
/// UnwindTable.
///
/// \returns An error if the DWARF Call Frame Information opcodes have state
/// machine errors, or a valid UnwindTable otherwise.
LLVM_ABI Expected<UnwindTable> createUnwindTable(const FDE *Fde);

/// An entry in either debug_frame or eh_frame. This entry can be a CIE or an
/// FDE.
class FrameEntry {
public:
  /// Distinguishes Common Information Entries from Frame Description Entries.
  enum FrameKind {
    FK_CIE, ///< Common Information Entry.
    FK_FDE  ///< Frame Description Entry.
  };

  /// Construct a frame entry with the given kind, layout, and CFI program.
  ///
  /// \param K Whether this entry is a CIE or an FDE.
  /// \param IsDWARF64 True if this entry uses the DWARF64 format.
  /// \param Offset Byte offset of this entry within its section.
  /// \param Length Entry length as specified in DWARF (excluding the length
  ///        field itself).
  /// \param CodeAlign Code alignment factor for the CFI program.
  /// \param DataAlign Data alignment factor for the CFI program.
  /// \param Arch Target architecture used when interpreting CFI instructions.
  FrameEntry(FrameKind K, bool IsDWARF64, uint64_t Offset, uint64_t Length,
             uint64_t CodeAlign, int64_t DataAlign, Triple::ArchType Arch)
      : Kind(K), IsDWARF64(IsDWARF64), Offset(Offset), Length(Length),
        CFIs(CodeAlign, DataAlign, Arch) {}

  /// Destroy a frame entry.
  virtual ~FrameEntry() = default;

  /// Return whether this entry is a CIE or an FDE.
  ///
  /// \returns FK_CIE or FK_FDE for this entry.
  FrameKind getKind() const { return Kind; }
  /// Return the byte offset of this entry within its section.
  ///
  /// \returns The byte offset of this entry within its section.
  uint64_t getOffset() const { return Offset; }
  /// Return the DWARF-specified length of this entry.
  ///
  /// \returns The DWARF-specified length of this entry.
  uint64_t getLength() const { return Length; }
  /// Return the call frame information program for this entry.
  ///
  /// \returns Const reference to this entry's CFI program.
  const CFIProgram &cfis() const { return CFIs; }
  /// Return the call frame information program for this entry.
  ///
  /// \returns Mutable reference to this entry's CFI program.
  CFIProgram &cfis() { return CFIs; }

  /// Dump the instructions in this CFI fragment.
  ///
  /// \param OS Output stream to write the dump to.
  /// \param DumpOpts Options controlling what and how to dump.
  virtual void dump(raw_ostream &OS, DIDumpOptions DumpOpts) const = 0;

protected:
  /// Whether this entry is a CIE or an FDE.
  const FrameKind Kind;

  /// True if this entry uses the DWARF64 format.
  const bool IsDWARF64;

  /// Offset of this entry in the section.
  const uint64_t Offset;

  /// Entry length as specified in DWARF.
  const uint64_t Length;

  /// Call frame information instructions belonging to this entry.
  CFIProgram CFIs;
};

/// DWARF Common Information Entry (CIE)
class LLVM_ABI CIE : public FrameEntry {
public:
  /// Construct a CIE from its fully parsed DWARF fields.
  ///
  /// CIEs (and FDEs) are simply container classes, so the only sensible way to
  /// create them is by providing the full parsed contents in the constructor.
  ///
  /// \param IsDWARF64 True if this CIE uses the DWARF64 format.
  /// \param Offset Byte offset of this CIE within its section.
  /// \param Length CIE length as specified in DWARF.
  /// \param Version CIE version number.
  /// \param Augmentation Augmentation string from the CIE.
  /// \param AddressSize Size in bytes of an address on the target.
  /// \param SegmentDescriptorSize Size in bytes of a segment descriptor.
  /// \param CodeAlignmentFactor Code alignment factor for CFI instructions.
  /// \param DataAlignmentFactor Data alignment factor for CFI instructions.
  /// \param ReturnAddressRegister Column number of the return address register.
  /// \param AugmentationData Raw augmentation data used for EH frame entries.
  /// \param FDEPointerEncoding Encoding used for FDE pointers in linked FDEs.
  /// \param LSDAPointerEncoding Encoding used for LSDA pointers in linked FDEs.
  /// \param Personality Optional personality routine address.
  /// \param PersonalityEnc Optional encoding of the personality routine
  ///        pointer.
  /// \param Arch Target architecture used when interpreting CFI instructions.
  CIE(bool IsDWARF64, uint64_t Offset, uint64_t Length, uint8_t Version,
      SmallString<8> Augmentation, uint8_t AddressSize,
      uint8_t SegmentDescriptorSize, uint64_t CodeAlignmentFactor,
      int64_t DataAlignmentFactor, uint64_t ReturnAddressRegister,
      SmallString<8> AugmentationData, uint32_t FDEPointerEncoding,
      uint32_t LSDAPointerEncoding, std::optional<uint64_t> Personality,
      std::optional<uint32_t> PersonalityEnc, Triple::ArchType Arch)
      : FrameEntry(FK_CIE, IsDWARF64, Offset, Length, CodeAlignmentFactor,
                   DataAlignmentFactor, Arch),
        Version(Version), Augmentation(std::move(Augmentation)),
        AddressSize(AddressSize), SegmentDescriptorSize(SegmentDescriptorSize),
        CodeAlignmentFactor(CodeAlignmentFactor),
        DataAlignmentFactor(DataAlignmentFactor),
        ReturnAddressRegister(ReturnAddressRegister),
        AugmentationData(std::move(AugmentationData)),
        FDEPointerEncoding(FDEPointerEncoding),
        LSDAPointerEncoding(LSDAPointerEncoding), Personality(Personality),
        PersonalityEnc(PersonalityEnc) {}

  /// Return true if \p FE is a CIE.
  ///
  /// \param FE Frame entry to test.
  /// \returns True if \p FE is a CIE; false otherwise.
  static bool classof(const FrameEntry *FE) { return FE->getKind() == FK_CIE; }

  /// Return the CIE augmentation string.
  ///
  /// \returns The CIE augmentation string.
  StringRef getAugmentationString() const { return Augmentation; }
  /// Return the code alignment factor.
  ///
  /// \returns The code alignment factor.
  uint64_t getCodeAlignmentFactor() const { return CodeAlignmentFactor; }
  /// Return the data alignment factor.
  ///
  /// \returns The data alignment factor.
  int64_t getDataAlignmentFactor() const { return DataAlignmentFactor; }
  /// Return the CIE version number.
  ///
  /// \returns The CIE version number.
  uint8_t getVersion() const { return Version; }
  /// Return the return address register column number.
  ///
  /// \returns The return address register column number.
  uint64_t getReturnAddressRegister() const { return ReturnAddressRegister; }
  /// Return the optional personality routine address.
  ///
  /// \returns The personality routine address, or std::nullopt if absent.
  std::optional<uint64_t> getPersonalityAddress() const { return Personality; }
  /// Return the optional personality routine pointer encoding.
  ///
  /// \returns The personality routine pointer encoding, or std::nullopt if
  /// absent.
  std::optional<uint32_t> getPersonalityEncoding() const {
    return PersonalityEnc;
  }

  /// Return the raw augmentation data for EH frame CIEs.
  ///
  /// \returns The raw augmentation data for EH frame CIEs.
  StringRef getAugmentationData() const { return AugmentationData; }

  /// Return the pointer encoding used for FDE addresses.
  ///
  /// \returns The pointer encoding used for FDE addresses.
  uint32_t getFDEPointerEncoding() const { return FDEPointerEncoding; }

  /// Return the pointer encoding used for LSDA addresses.
  ///
  /// \returns The pointer encoding used for LSDA addresses.
  uint32_t getLSDAPointerEncoding() const { return LSDAPointerEncoding; }

  /// Dump this CIE to \p OS.
  ///
  /// \param OS Output stream to write the dump to.
  /// \param DumpOpts Options controlling what and how to dump.
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts) const override;

private:
  /// The following fields are defined in section 6.4.1 of the DWARF standard v4
  const uint8_t Version;
  const SmallString<8> Augmentation;
  const uint8_t AddressSize;
  const uint8_t SegmentDescriptorSize;
  const uint64_t CodeAlignmentFactor;
  const int64_t DataAlignmentFactor;
  const uint64_t ReturnAddressRegister;

  // The following are used when the CIE represents an EH frame entry.
  const SmallString<8> AugmentationData;
  const uint32_t FDEPointerEncoding;
  const uint32_t LSDAPointerEncoding;
  const std::optional<uint64_t> Personality;
  const std::optional<uint32_t> PersonalityEnc;
};

/// DWARF Frame Description Entry (FDE)
class LLVM_ABI FDE : public FrameEntry {
public:
  /// Construct an FDE from its fully parsed DWARF fields.
  ///
  /// \param IsDWARF64 True if this FDE uses the DWARF64 format.
  /// \param Offset Byte offset of this FDE within its section.
  /// \param Length FDE length as specified in DWARF.
  /// \param CIEPointer Offset or relative pointer to the linked CIE.
  /// \param InitialLocation Starting address of the code range covered by this
  ///        FDE.
  /// \param AddressRange Number of bytes of code covered by this FDE.
  /// \param Cie Linked CIE providing alignment factors and other shared state,
  ///        or null if unavailable.
  /// \param LSDAAddress Optional language-specific data area address.
  /// \param Arch Target architecture used when interpreting CFI instructions.
  FDE(bool IsDWARF64, uint64_t Offset, uint64_t Length, uint64_t CIEPointer,
      uint64_t InitialLocation, uint64_t AddressRange, CIE *Cie,
      std::optional<uint64_t> LSDAAddress, Triple::ArchType Arch)
      : FrameEntry(FK_FDE, IsDWARF64, Offset, Length,
                   Cie ? Cie->getCodeAlignmentFactor() : 0,
                   Cie ? Cie->getDataAlignmentFactor() : 0, Arch),
        CIEPointer(CIEPointer), InitialLocation(InitialLocation),
        AddressRange(AddressRange), LinkedCIE(Cie), LSDAAddress(LSDAAddress) {}

  /// Destroy an FDE.
  ~FDE() override = default;

  /// Return the CIE linked to this FDE, if available.
  ///
  /// \returns The linked CIE, or nullptr if unavailable.
  const CIE *getLinkedCIE() const { return LinkedCIE; }
  /// Return the CIE pointer stored in this FDE.
  ///
  /// \returns The CIE pointer stored in this FDE.
  uint64_t getCIEPointer() const { return CIEPointer; }
  /// Return the initial location of the code range covered by this FDE.
  ///
  /// \returns The initial location of the code range covered by this FDE.
  uint64_t getInitialLocation() const { return InitialLocation; }
  /// Return the length of the code range covered by this FDE.
  ///
  /// \returns The length of the code range covered by this FDE.
  uint64_t getAddressRange() const { return AddressRange; }
  /// Return the optional LSDA address associated with this FDE.
  ///
  /// \returns The LSDA address, or std::nullopt if absent.
  std::optional<uint64_t> getLSDAAddress() const { return LSDAAddress; }

  /// Dump this FDE to \p OS.
  ///
  /// \param OS Output stream to write the dump to.
  /// \param DumpOpts Options controlling what and how to dump.
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts) const override;

  /// Return true if \p FE is an FDE.
  ///
  /// \param FE Frame entry to test.
  /// \returns True if \p FE is an FDE; false otherwise.
  static bool classof(const FrameEntry *FE) { return FE->getKind() == FK_FDE; }

private:
  /// The following fields are defined in section 6.4.1 of the DWARFv3 standard.
  /// Note that CIE pointers in EH FDEs, unlike DWARF FDEs, contain relative
  /// offsets to the linked CIEs. See the following link for more info:
  /// https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html
  const uint64_t CIEPointer;
  const uint64_t InitialLocation;
  const uint64_t AddressRange;
  const CIE *LinkedCIE;
  const std::optional<uint64_t> LSDAAddress;
};

} // end namespace dwarf

/// A parsed .debug_frame or .eh_frame section
class DWARFDebugFrame {
  const Triple::ArchType Arch;
  // True if this is parsing an eh_frame section.
  const bool IsEH;
  // Not zero for sane pointer values coming out of eh_frame
  const uint64_t EHFrameAddress;

  std::vector<std::unique_ptr<dwarf::FrameEntry>> Entries;
  using iterator = pointee_iterator<decltype(Entries)::const_iterator>;

  /// Return the entry at the given offset or nullptr.
  dwarf::FrameEntry *getEntryAtOffset(uint64_t Offset) const;

public:
  /// Construct a parser for a .debug_frame or .eh_frame section.
  ///
  /// If IsEH is true, assume it is a .eh_frame section. Otherwise, it is a
  /// .debug_frame section. EHFrameAddress should be different than zero for
  /// correct parsing of .eh_frame addresses when they use a PC-relative
  /// encoding.
  ///
  /// \param Arch Target architecture used when interpreting CFI instructions.
  /// \param IsEH True to parse the section as .eh_frame; false for
  ///        .debug_frame.
  /// \param EHFrameAddress Load address of the .eh_frame section, used when
  ///        decoding PC-relative pointers; may be zero for .debug_frame.
  LLVM_ABI DWARFDebugFrame(Triple::ArchType Arch, bool IsEH = false,
                           uint64_t EHFrameAddress = 0);
  /// Destroy a parsed frame section.
  LLVM_ABI ~DWARFDebugFrame();

  /// Dump the section data into the given stream.
  ///
  /// \param OS Output stream to write the dump to.
  /// \param DumpOpts Options controlling what and how to dump.
  /// \param Offset If set, dump only the frame entry at this section offset;
  ///        otherwise dump the whole section.
  LLVM_ABI void dump(raw_ostream &OS, DIDumpOptions DumpOpts,
                     std::optional<uint64_t> Offset) const;

  /// Parse the section from raw data.
  ///
  /// \param Data Assumed to contain the whole frame section contents to be
  ///        parsed.
  /// \returns Success, or an error if the section could not be parsed.
  LLVM_ABI Error parse(DWARFDataExtractor Data);

  /// Return whether the section has any entries.
  ///
  /// \returns True if the section has no entries; false otherwise.
  bool empty() const { return Entries.empty(); }

  /// Return an iterator to the first frame entry.
  ///
  /// \returns An iterator to the first frame entry.
  iterator begin() const { return Entries.begin(); }
  /// Return an iterator past the last frame entry.
  ///
  /// \returns An iterator past the last frame entry.
  iterator end() const { return Entries.end(); }
  /// Return a range over all frame entries in this section.
  ///
  /// \returns An iterator range over all frame entries in this section.
  iterator_range<iterator> entries() const { return Entries; }

  /// Return the .eh_frame section load address used for PC-relative decoding.
  ///
  /// \returns The .eh_frame section load address used for PC-relative decoding.
  uint64_t getEHFrameAddress() const { return EHFrameAddress; }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGFRAME_H
