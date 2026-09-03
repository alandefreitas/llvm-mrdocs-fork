//===- MCGOFFObjectWriter.h - GOFF Object Writer ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCGOFFOBJECTWRITER_H
#define LLVM_MC_MCGOFFOBJECTWRITER_H

#include "llvm/BinaryFormat/GOFF.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include <memory>
#include <vector>

namespace llvm {
class MCObjectWriter;
class MCSectionGOFF;
class MCSymbolGOFF;
class raw_pwrite_stream;

/// Target-specific GOFF object-file writer hooks.
class MCGOFFObjectTargetWriter : public MCObjectTargetWriter {
protected:
  /// Construct a GOFF target object writer.
  MCGOFFObjectTargetWriter() = default;

public:
  /// GOFF relocation directory (RLD) relocation types.
  enum RLDRelocationType {
    /// General address.
    Reloc_Type_ACon = 0x1,
    /// Relative-immediate address.
    Reloc_Type_RICon = 0x2,
    /// Offset of symbol in class.
    Reloc_Type_QCon = 0x3,
    /// Address of external symbol.
    Reloc_Type_VCon = 0x4,
    /// PSECT of symbol.
    Reloc_Type_RCon = 0x5,
  };

  /// Destroy the GOFF target object writer.
  ~MCGOFFObjectTargetWriter() override = default;

  /// Return the GOFF relocation type for \p Fixup applied to \p Target.
  ///
  /// \param Target - Relocatable expression evaluated for the fixup.
  /// \param Fixup - Fixup being recorded as a relocation.
  /// \returns The GOFF RLD relocation type for the fixup.
  virtual unsigned getRelocType(const MCValue &Target,
                                const MCFixup &Fixup) const = 0;

  /// Return the object file format handled by this writer.
  ///
  /// \returns The GOFF object format type.
  Triple::ObjectFormatType getFormat() const override { return Triple::GOFF; }

  /// Return true if \p W is a GOFF target object writer.
  ///
  /// \param W - Object target writer to test.
  /// \returns True if \p W is a GOFF target object writer.
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::GOFF;
  }
};

/// A single GOFF relocation to be emitted.
///
/// For the naming, see
/// https://www.ibm.com/docs/en/zos/3.1.0?topic=record-relocation-directory-data-item.
struct GOFFRelocationEntry {
  /// The R pointer (referent symbol).
  const MCSymbolGOFF *Rptr;
  /// The P pointer (target section).
  const MCSectionGOFF *Pptr;
  /// The R pointer ESD identifier.
  uint32_t REsdId = 0;
  /// The P pointer ESD identifier.
  uint32_t PEsdId = 0;
  /// The offset within the element described by the P pointer.
  uint64_t POffset;
  /// The byte length of the target field.
  uint32_t TargetLength;

  /// How the relocation references its target.
  GOFF::RLDReferenceType ReferenceType : 4;
  /// How the referent of the relocation is interpreted.
  GOFF::RLDReferentType ReferentType : 2;
  /// Relocation action to apply.
  GOFF::RLDAction Action : 1;
  /// Whether the relocation is a fetch or store reference.
  GOFF::RLDFetchStore FetchStore : 1;

  /// Construct a GOFF relocation entry.
  ///
  /// \param Pptr - P pointer (target section).
  /// \param Rptr - R pointer (referent symbol).
  /// \param ReferenceType - How the relocation references its target.
  /// \param ReferentType - How the referent is interpreted.
  /// \param Action - Relocation action to apply.
  /// \param FetchStore - Whether the reference is a fetch or store.
  /// \param POffset - Offset within the element described by \p Pptr.
  /// \param TargetLength - Byte length of the target field.
  GOFFRelocationEntry(const MCSectionGOFF *Pptr, const MCSymbolGOFF *Rptr,
                      GOFF::RLDReferenceType ReferenceType,
                      GOFF::RLDReferentType ReferentType,
                      GOFF::RLDAction Action, GOFF::RLDFetchStore FetchStore,
                      uint64_t POffset, uint32_t TargetLength)
      : Rptr(Rptr), Pptr(Pptr), POffset(POffset), TargetLength(TargetLength),
        ReferenceType(ReferenceType), ReferentType(ReferentType),
        Action(Action), FetchStore(FetchStore) {}
};

/// GOFF object writer that emits GOFF object files from an MCAssembler.
class LLVM_ABI GOFFObjectWriter : public MCObjectWriter {
  // The target specific GOFF writer instance.
  std::unique_ptr<MCGOFFObjectTargetWriter> TargetObjectWriter;

  // The stream used to write the GOFF records.
  raw_pwrite_stream &OS;

  // The stream used to write the split DWARF file.
  raw_pwrite_stream *DwoOS = nullptr;

  // The RootSD section.
  MCSectionGOFF *RootSD = nullptr;

  // Saved relocation data.
  std::vector<GOFFRelocationEntry> Relocations;

public:
  /// Construct a GOFF object writer for a single output stream.
  ///
  /// \param MOTW - Target-specific GOFF writer.
  /// \param OS - Stream to write the object to.
  GOFFObjectWriter(std::unique_ptr<MCGOFFObjectTargetWriter> MOTW,
                   raw_pwrite_stream &OS);
  /// Construct a GOFF object writer with a split-DWARF output stream.
  ///
  /// \param MOTW - Target-specific GOFF writer.
  /// \param OS - Stream to write the non-DWO object to.
  /// \param DwoOS - Stream to write the DWO object to.
  GOFFObjectWriter(std::unique_ptr<MCGOFFObjectTargetWriter> MOTW,
                   raw_pwrite_stream &OS, raw_pwrite_stream &DwoOS);
  /// Destroy the GOFF object writer.
  ~GOFFObjectWriter() override;

  /// Reset writer state for reuse.
  void reset() override;

  /// Set the RootSD section used as the GOFF root structured definition.
  ///
  /// \param RootSD - Root structured-definition section, or null.
  void setRootSD(MCSectionGOFF *RootSD) { this->RootSD = RootSD; }

  /// Record a GOFF relocation for fixup \p Fixup in fragment \p F.
  ///
  /// \param F - Fragment containing the fixup.
  /// \param Fixup - Fixup to record as a relocation.
  /// \param Target - Relocatable expression for the fixup.
  /// \param FixedValue - [out] Value to encode into the fragment.
  void recordRelocation(const MCFragment &F, const MCFixup &Fixup,
                        MCValue Target, uint64_t &FixedValue) override;

  /// Write the GOFF object file and return the number of bytes written.
  ///
  /// \returns The number of bytes written.
  uint64_t writeObject() override;
};

/// \brief Construct a new GOFF writer instance.
///
/// \param MOTW - The target-specific GOFF writer subclass.
/// \param OS - The stream to write to.
/// \returns The constructed object writer.
LLVM_ABI std::unique_ptr<MCObjectWriter>
createGOFFObjectWriter(std::unique_ptr<MCGOFFObjectTargetWriter> MOTW,
                       raw_pwrite_stream &OS);

/// Construct a new GOFF writer instance with a split-DWARF stream.
///
/// \param MOTW - The target-specific GOFF writer subclass.
/// \param OS - The stream to write the non-DWO object to.
/// \param DwoOS - The stream to write the DWO object to.
/// \returns The constructed object writer.
std::unique_ptr<MCObjectWriter>
createGOFFObjectWriter(std::unique_ptr<MCGOFFObjectTargetWriter> MOTW,
                       raw_pwrite_stream &OS, raw_pwrite_stream &DwoOS);
} // namespace llvm

#endif
