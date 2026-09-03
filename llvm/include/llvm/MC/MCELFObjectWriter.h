//===- llvm/MC/MCELFObjectWriter.h - ELF Object Writer ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCELFOBJECTWRITER_H
#define LLVM_MC_MCELFOBJECTWRITER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace llvm {

class MCAssembler;
class MCContext;
class MCFixup;
class MCSymbol;
class MCSymbolELF;
class MCTargetOptions;
class MCValue;

/// A single ELF relocation to be emitted for a section.
struct ELFRelocationEntry {
  /// Offset within the section where the relocation applies.
  uint64_t Offset;
  /// The symbol to relocate with, or null for an absolute relocation.
  const MCSymbolELF *Symbol;
  /// The ELF relocation type.
  unsigned Type;
  /// The addend to use with the relocation.
  uint64_t Addend;

  /// Construct a relocation entry.
  ///
  /// \param Offset - Offset within the section where the relocation applies.
  /// \param Symbol - Symbol to relocate with, or null.
  /// \param Type - ELF relocation type.
  /// \param Addend - Addend to store with the relocation.
  ELFRelocationEntry(uint64_t Offset, const MCSymbolELF *Symbol, unsigned Type,
                     uint64_t Addend)
      : Offset(Offset), Symbol(Symbol), Type(Type), Addend(Addend) {}

  /// Print a human-readable description of this relocation to \p Out.
  ///
  /// \param Out - Stream to print to.
  void print(raw_ostream &Out) const {
    Out << "Off=" << Offset << ", Sym=" << Symbol << ", Type=" << Type
        << ", Addend=" << Addend;
  }

  /// Print a human-readable description of this relocation to stderr.
  LLVM_DUMP_METHOD void dump() const { print(errs()); }
};

/// Target-specific ELF object-file writer hooks.
class LLVM_ABI MCELFObjectTargetWriter : public MCObjectTargetWriter {
  const uint8_t OSABI;
  const uint8_t ABIVersion;
  const uint16_t EMachine;
  const unsigned HasRelocationAddend : 1;
  const unsigned Is64Bit : 1;

protected:
  /// Construct an ELF target object writer.
  ///
  /// \param Is64Bit_ - True if writing a 64-bit ELF object.
  /// \param OSABI_ - ELF OS/ABI identifier for \c e_ident[EI_OSABI].
  /// \param EMachine_ - ELF machine type for \c e_machine.
  /// \param HasRelocationAddend_ - True if the target uses RELA relocations.
  /// \param ABIVersion_ - ELF ABI version for \c e_ident[EI_ABIVERSION].
  MCELFObjectTargetWriter(bool Is64Bit_, uint8_t OSABI_, uint16_t EMachine_,
                          bool HasRelocationAddend_, uint8_t ABIVersion_ = 0);

public:
  /// Destroy the ELF target object writer.
  ~MCELFObjectTargetWriter() override = default;

  /// Return the object file format handled by this writer.
  ///
  /// \return The object file format, always \c Triple::ELF.
  Triple::ObjectFormatType getFormat() const override { return Triple::ELF; }
  /// Return true if \p W is an ELF target object writer.
  ///
  /// \param W - Object target writer to test.
  /// \return True if \p W is an ELF target object writer.
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::ELF;
  }

  /// Map a triple OS type to an ELF OSABI value.
  ///
  /// \param OSType - Operating system from the target triple.
  /// \return The ELF OSABI value corresponding to \p OSType.
  static uint8_t getOSABI(Triple::OSType OSType) {
    switch (OSType) {
      case Triple::HermitCore:
        return ELF::ELFOSABI_STANDALONE;
      case Triple::PS4:
      case Triple::FreeBSD:
        return ELF::ELFOSABI_FREEBSD;
      case Triple::Solaris:
        return ELF::ELFOSABI_SOLARIS;
      case Triple::OpenBSD:
        return ELF::ELFOSABI_OPENBSD;
      default:
        return ELF::ELFOSABI_NONE;
    }
  }

  /// Return the ELF relocation type for \p Fixup applied to \p Target.
  ///
  /// \param Fixup - Fixup being recorded as a relocation.
  /// \param Target - Relocatable expression evaluated for the fixup.
  /// \param IsPCRel - True if the relocation is PC-relative.
  /// \return The ELF relocation type for the fixup.
  virtual unsigned getRelocType(const MCFixup &Fixup, const MCValue &Target,
                                bool IsPCRel) const = 0;

  /// Return true if relocation \p Type must use a symbol rather than a
  /// section.
  ///
  /// \param Target - Relocatable expression associated with the relocation.
  /// \param Type - ELF relocation type.
  /// \return True if the relocation must use a symbol rather than a section.
  virtual bool needsRelocateWithSymbol(const MCValue &Target,
                                       unsigned Type) const {
    return false;
  }

  /// Sort \p Relocs into the order required for emission.
  ///
  /// \param Relocs - Relocations to reorder in place.
  virtual void sortRelocs(std::vector<ELFRelocationEntry> &Relocs);

  /// \name Accessors
  /// @{
  /// Return the ELF OS/ABI identifier for this writer.
  ///
  /// \return The ELF OS/ABI identifier for \c e_ident[EI_OSABI].
  uint8_t getOSABI() const { return OSABI; }
  /// Return the ELF ABI version for this writer.
  ///
  /// \return The ELF ABI version for \c e_ident[EI_ABIVERSION].
  uint8_t getABIVersion() const { return ABIVersion; }
  /// Return the ELF machine type for this writer.
  ///
  /// \return The ELF machine type for \c e_machine.
  uint16_t getEMachine() const { return EMachine; }
  /// Return true if this target emits RELA (explicit addend) relocations.
  ///
  /// \return True if this target emits RELA relocations.
  bool hasRelocationAddend() const { return HasRelocationAddend; }
  /// Return true if this writer targets 64-bit ELF.
  ///
  /// \return True if this writer targets 64-bit ELF.
  bool is64Bit() const { return Is64Bit; }
  /// @}

  // Instead of changing everyone's API we pack the N64 Type fields
  // into the existing 32 bit data unsigned.
  /// Bit shift for the primary N64 relocation type field.
#define R_TYPE_SHIFT 0
#define R_TYPE_MASK 0xffffff00
#define R_TYPE2_SHIFT 8
#define R_TYPE2_MASK 0xffff00ff
#define R_TYPE3_SHIFT 16
#define R_TYPE3_MASK 0xff00ffff
#define R_SSYM_SHIFT 24
#define R_SSYM_MASK 0x00ffffff

  // N64 relocation type accessors
  /// Extract the primary N64 relocation type from packed \p Type.
  ///
  /// \param Type - Packed N64 relocation type word.
  /// \return The primary N64 relocation type field.
  uint8_t getRType(uint32_t Type) const {
    return (unsigned)((Type >> R_TYPE_SHIFT) & 0xff);
  }
  /// Extract the second N64 relocation type from packed \p Type.
  ///
  /// \param Type - Packed N64 relocation type word.
  /// \return The second N64 relocation type field.
  uint8_t getRType2(uint32_t Type) const {
    return (unsigned)((Type >> R_TYPE2_SHIFT) & 0xff);
  }
  /// Extract the third N64 relocation type from packed \p Type.
  ///
  /// \param Type - Packed N64 relocation type word.
  /// \return The third N64 relocation type field.
  uint8_t getRType3(uint32_t Type) const {
    return (unsigned)((Type >> R_TYPE3_SHIFT) & 0xff);
  }
  /// Extract the N64 special-symbol field from packed \p Type.
  ///
  /// \param Type - Packed N64 relocation type word.
  /// \return The N64 special-symbol field.
  uint8_t getRSsym(uint32_t Type) const {
    return (unsigned)((Type >> R_SSYM_SHIFT) & 0xff);
  }

  // N64 relocation type setting
  /// Pack three N64 relocation types into a single word.
  ///
  /// \param Value1 - Primary relocation type.
  /// \param Value2 - Second relocation type.
  /// \param Value3 - Third relocation type.
  /// \return The packed N64 relocation type word.
  static unsigned setRTypes(unsigned Value1, unsigned Value2, unsigned Value3) {
    return ((Value1 & 0xff) << R_TYPE_SHIFT) |
           ((Value2 & 0xff) << R_TYPE2_SHIFT) |
           ((Value3 & 0xff) << R_TYPE3_SHIFT);
  }
  /// Set the N64 special-symbol field in packed relocation \p Type.
  ///
  /// \param Value - Special-symbol field to store.
  /// \param Type - Existing packed relocation type word.
  /// \return The packed relocation type word with the special-symbol field set.
  unsigned setRSsym(unsigned Value, unsigned Type) const {
    return (Type & R_SSYM_MASK) | ((Value & 0xff) << R_SSYM_SHIFT);
  }
};

/// ELF object writer that emits ELF object files from an MCAssembler.
class LLVM_ABI ELFObjectWriter final : public MCObjectWriter {
  unsigned ELFHeaderEFlags = 0;

public:
  /// Target-specific ELF writer providing relocation and ABI details.
  std::unique_ptr<MCELFObjectTargetWriter> TargetObjectWriter;
  /// Primary output stream for the ELF object.
  raw_pwrite_stream &OS;
  /// Optional output stream for split-DWARF (.dwo) contents.
  raw_pwrite_stream *DwoOS = nullptr;

  /// Relocations to emit, keyed by the section they apply to.
  DenseMap<const MCSectionELF *, std::vector<ELFRelocationEntry>> Relocations;
  /// Map from original symbols to their versioned aliases.
  DenseMap<const MCSymbolELF *, const MCSymbolELF *> Renames;
  /// Symbols that are `.weakref` aliases.
  SmallVector<const MCSymbolELF *, 0> Weakrefs;
  /// True if the object is written little-endian.
  bool IsLittleEndian = false;
  /// True if GNU ABI features have been observed.
  bool SeenGnuAbi = false;
  /// Optional override for \c e_ident[EI_ABIVERSION].
  std::optional<uint8_t> OverrideABIVersion;

  /// A pending `.symver` directive to apply during post-layout binding.
  struct Symver {
    /// Source location of the `.symver` directive.
    SMLoc Loc;
    /// Symbol being versioned.
    const MCSymbol *Sym;
    /// Versioned alias name from the directive.
    StringRef Name;
    /// True if `.symver *, *@@@*` or `.symver *, *, remove` keeps the original
    /// symbol.
    bool KeepOriginalSym;
  };
  /// Pending `.symver` directives collected during assembly.
  SmallVector<Symver, 0> Symvers;

  /// Construct an ELF object writer for a single output stream.
  ///
  /// \param MOTW - Target-specific ELF writer.
  /// \param OS - Stream to write the object to.
  /// \param IsLittleEndian - True to emit little-endian ELF.
  ELFObjectWriter(std::unique_ptr<MCELFObjectTargetWriter> MOTW,
                  raw_pwrite_stream &OS, bool IsLittleEndian);
  /// Construct an ELF object writer with a split-DWARF output stream.
  ///
  /// \param MOTW - Target-specific ELF writer.
  /// \param OS - Stream to write the non-DWO object to.
  /// \param DwoOS - Stream to write the DWO object to.
  /// \param IsLittleEndian - True to emit little-endian ELF.
  ELFObjectWriter(std::unique_ptr<MCELFObjectTargetWriter> MOTW,
                  raw_pwrite_stream &OS, raw_pwrite_stream &DwoOS,
                  bool IsLittleEndian);

  /// Reset writer state for reuse.
  void reset() override;
  /// Set the assembler used by this writer and its target writer.
  ///
  /// \param Asm - Assembler instance to associate.
  void setAssembler(MCAssembler *Asm) override;
  /// Apply symbol versioning and weakref binding after layout.
  void executePostLayoutBinding() override;
  /// Record an ELF relocation for fixup \p Fixup in fragment \p F.
  ///
  /// \param F - Fragment containing the fixup.
  /// \param Fixup - Fixup to record as a relocation.
  /// \param Target - Relocatable expression for the fixup.
  /// \param FixedValue - [out] Value to encode into the fragment.
  void recordRelocation(const MCFragment &F, const MCFixup &Fixup,
                        MCValue Target, uint64_t &FixedValue) override;
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
  /// Write the ELF object file and return the number of bytes written.
  ///
  /// \return The number of bytes written to the output stream(s).
  uint64_t writeObject() override;

  /// Return true if the target uses RELA (explicit addend) relocations.
  ///
  /// \return True if the target uses RELA relocations.
  bool hasRelocationAddend() const;
  /// Return true if section \p Sec should use RELA-style relocations.
  ///
  /// \param TO - Target options that may force CREL output.
  /// \param Sec - Section whose relocation style is queried.
  /// \return True if \p Sec should use RELA-style relocations.
  bool usesRela(const MCTargetOptions &TO, const MCSectionELF &Sec) const;

  /// Return true if relocation of \p Val should use a section symbol.
  ///
  /// Preferring a section symbol allows omitting some local symbols from the
  /// symbol table when that encodes the same information.
  ///
  /// \param Val - Relocatable expression being relocated.
  /// \param Sym - Symbol currently selected for the relocation.
  /// \param C - Constant addend associated with the relocation.
  /// \param Type - ELF relocation type.
  /// \return True if the relocation should use a section symbol.
  bool useSectionSymbol(const MCValue &Val, const MCSymbolELF *Sym, uint64_t C,
                        unsigned Type) const;

  /// Validate that a relocation from \p From to \p To is allowed.
  ///
  /// \param Loc - Source location for diagnostics.
  /// \param From - Section containing the relocation.
  /// \param To - Section referred to by the relocation, or null.
  /// \return True if the relocation is allowed; false if an error was reported.
  bool checkRelocation(SMLoc Loc, const MCSectionELF *From,
                       const MCSectionELF *To);

  /// Return the ELF header \c e_flags value to emit.
  ///
  /// \return The ELF header \c e_flags value to emit.
  unsigned getELFHeaderEFlags() const { return ELFHeaderEFlags; }
  /// Set the ELF header \c e_flags value to emit.
  ///
  /// \param Flags - New \c e_flags value.
  void setELFHeaderEFlags(unsigned Flags) { ELFHeaderEFlags = Flags; }

  /// Record that GNU ABI usage has been observed.
  ///
  /// Examples include \c SHF_GNU_RETAIN and \c STB_GNU_UNIQUE.
  void markGnuAbi() { SeenGnuAbi = true; }
  /// Return true if GNU ABI usage has been observed.
  ///
  /// \return True if GNU ABI usage has been observed.
  bool seenGnuAbi() const { return SeenGnuAbi; }

  /// Override the default \c e_ident[EI_ABIVERSION] in the ELF header.
  ///
  /// \param V - ABI version to write into the ELF identification bytes.
  void setOverrideABIVersion(uint8_t V) { OverrideABIVersion = V; }
};
} // end namespace llvm

#endif // LLVM_MC_MCELFOBJECTWRITER_H
