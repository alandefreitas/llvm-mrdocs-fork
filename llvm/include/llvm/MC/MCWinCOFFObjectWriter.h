//===- llvm/MC/MCWinCOFFObjectWriter.h - Win COFF Object Writer -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCWINCOFFOBJECTWRITER_H
#define LLVM_MC_MCWINCOFFOBJECTWRITER_H

#include "llvm/MC/MCObjectWriter.h"
#include <memory>

namespace llvm {

class MCAsmBackend;
class MCContext;
class MCFixup;
class MCValue;
class raw_pwrite_stream;

/// Target-specific Win COFF object-file writer hooks.
class MCWinCOFFObjectTargetWriter : public MCObjectTargetWriter {
  virtual void anchor();

  const unsigned Machine;

protected:
  /// Construct a Win COFF target object writer.
  ///
  /// \param Machine_ - COFF machine type for the object header.
  MCWinCOFFObjectTargetWriter(unsigned Machine_);

public:
  /// Destroy the Win COFF target object writer.
  ~MCWinCOFFObjectTargetWriter() override = default;

  /// Return the object file format handled by this writer.
  ///
  /// \return The object file format, always \c Triple::COFF.
  Triple::ObjectFormatType getFormat() const override { return Triple::COFF; }
  /// Return true if \p W is a Win COFF target object writer.
  ///
  /// \param W - Object target writer to test.
  /// \return True if \p W is a Win COFF target object writer.
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::COFF;
  }

  /// Return the COFF machine type for this writer.
  ///
  /// \return The COFF machine type for this writer.
  unsigned getMachine() const { return Machine; }
  /// Return the COFF relocation type for \p Fixup applied to \p Target.
  ///
  /// \param Ctx - Assembler context used for diagnostics.
  /// \param Target - Relocatable expression evaluated for the fixup.
  /// \param Fixup - Fixup being recorded as a relocation.
  /// \param IsCrossSection - True if the relocation crosses sections.
  /// \param MAB - Asm backend providing target-specific fixup info.
  /// \return The COFF relocation type for the fixup.
  virtual unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                                const MCFixup &Fixup, bool IsCrossSection,
                                const MCAsmBackend &MAB) const = 0;
  /// Return true if \p Fixup should be recorded as a COFF relocation.
  ///
  /// \param Fixup - Fixup being considered for relocation.
  /// \return True if \p Fixup should be recorded as a COFF relocation.
  virtual bool recordRelocation(const MCFixup &Fixup) const { return true; }
};

/// Helper that emits one Win COFF object stream (main or DWO).
class WinCOFFWriter;

/// Win COFF object writer that emits COFF object files from an MCAssembler.
class WinCOFFObjectWriter final : public MCObjectWriter {
  friend class WinCOFFWriter;

  std::unique_ptr<MCWinCOFFObjectTargetWriter> TargetObjectWriter;
  std::unique_ptr<WinCOFFWriter> ObjWriter, DwoWriter;
  bool IncrementalLinkerCompatible = false;

public:
  /// Construct a Win COFF object writer for a single output stream.
  ///
  /// \param MOTW - Target-specific Win COFF writer.
  /// \param OS - Stream to write the object to.
  WinCOFFObjectWriter(std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW,
                      raw_pwrite_stream &OS);
  /// Construct a Win COFF object writer with a split-DWARF output stream.
  ///
  /// \param MOTW - Target-specific Win COFF writer.
  /// \param OS - Stream to write the non-DWO object to.
  /// \param DwoOS - Stream to write the DWO object to.
  WinCOFFObjectWriter(std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW,
                      raw_pwrite_stream &OS, raw_pwrite_stream &DwoOS);

  // MCObjectWriter interface implementation.
  /// Reset writer state for reuse.
  void reset() override;
  /// Set the assembler used by this writer and its helpers.
  ///
  /// \param Asm - Assembler instance to associate.
  void setAssembler(MCAssembler *Asm) override;
  /// Set whether the object should be incremental-linker compatible.
  ///
  /// When enabled, a non-zero timestamp is written so tools that rely on
  /// unique object identity (such as incremental linking) can distinguish
  /// successive builds.
  ///
  /// \param Value - True to enable incremental-linker compatible output.
  void setIncrementalLinkerCompatible(bool Value) {
    IncrementalLinkerCompatible = Value;
  }
  /// Perform late binding of symbols after layout.
  void executePostLayoutBinding() override;
  /// Check whether the difference between \p SymA and \p FB is fully resolved.
  ///
  /// \param SymA - First symbol in the difference.
  /// \param FB - Fragment defining the second symbol.
  /// \param InSet - True if the difference appears in a set expression.
  /// \param IsPCRel - True if the reference is PC-relative.
  /// \return True if the symbol difference is fully resolved.
  bool isSymbolRefDifferenceFullyResolvedImpl(const MCSymbol &SymA,
                                              const MCFragment &FB, bool InSet,
                                              bool IsPCRel) const override;
  /// Record a Win COFF relocation for fixup \p Fixup in fragment \p F.
  ///
  /// \param F - Fragment containing the fixup.
  /// \param Fixup - Fixup to record as a relocation.
  /// \param Target - Relocatable expression for the fixup.
  /// \param FixedValue - [out] Value to encode into the fragment.
  void recordRelocation(const MCFragment &F, const MCFixup &Fixup,
                        MCValue Target, uint64_t &FixedValue) override;
  /// Write the Win COFF object file and return the number of bytes written.
  ///
  /// \return The number of bytes written to the output stream(s).
  uint64_t writeObject() override;
  /// Return the one-based COFF section number for \p Section.
  ///
  /// \param Section - Section whose COFF section number is requested.
  /// \return The one-based COFF section number for \p Section.
  int getSectionNumber(const MCSection &Section) const;
};

/// Construct a new Win COFF writer instance.
///
/// \param MOTW - The target specific WinCOFF writer subclass.
/// \param OS - The stream to write to.
/// \returns The constructed object writer.
LLVM_ABI std::unique_ptr<MCObjectWriter>
createWinCOFFObjectWriter(std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW,
                          raw_pwrite_stream &OS);

/// Construct a new Win COFF writer instance with a split-DWARF stream.
///
/// \param MOTW - The target specific WinCOFF writer subclass.
/// \param OS - The stream to write the non-DWO object to.
/// \param DwoOS - The stream to write the DWO object to.
/// \returns The constructed object writer.
LLVM_ABI std::unique_ptr<MCObjectWriter>
createWinCOFFDwoObjectWriter(std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW,
                             raw_pwrite_stream &OS, raw_pwrite_stream &DwoOS);
} // end namespace llvm

#endif // LLVM_MC_MCWINCOFFOBJECTWRITER_H
