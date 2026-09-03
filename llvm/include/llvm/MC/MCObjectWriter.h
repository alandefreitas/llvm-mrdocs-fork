//===- llvm/MC/MCObjectWriter.h - Object File Writer Interface --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCOBJECTWRITER_H
#define LLVM_MC_MCOBJECTWRITER_H

#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>

namespace llvm {

class MCAssembler;
class MCFixup;
class MCFragment;
class MCSymbol;
class MCSymbolRefExpr;
class MCValue;

/// Defines the object file and target independent interfaces used by the
/// assembler backend to write native file format object files.
///
/// The object writer contains a few callbacks used by the assembler to allow
/// the object writer to modify the assembler data structures at appropriate
/// points. Once assembly is complete, the object writer is given the
/// MCAssembler instance, which contains all the symbol and section data which
/// should be emitted as part of writeObject().
class LLVM_ABI MCObjectWriter {
protected:
  /// Assembler that owns this object writer, or null if unset.
  MCAssembler *Asm = nullptr;
  /// List of declared file names
  SmallVector<std::pair<std::string, size_t>, 0> FileNames;
  /// Optional compiler version string (XCOFF-specific).
  std::string CompilerVersion;
  /// Symbols to record in the address-significance table.
  std::vector<const MCSymbol *> AddrsigSyms;
  /// True if an address-significance section should be emitted.
  bool EmitAddrsigSection = false;
  /// True if Mach-O .subsections_via_symbols is enabled.
  bool SubsectionsViaSymbols = false;

  /// One call-graph profile edge with a from/to pair and edge weight.
  struct CGProfileEntry {
    /// Caller (from) symbol reference for this profile edge.
    const MCSymbolRefExpr *From;
    /// Callee (to) symbol reference for this profile edge.
    const MCSymbolRefExpr *To;
    /// Edge weight / call count for this profile entry.
    uint64_t Count;
  };
  /// Accumulated call-graph profile edges to emit.
  SmallVector<CGProfileEntry, 0> CGProfile;

  /// Construct an object writer with default state.
  MCObjectWriter() = default;

public:
  /// Deleted copy constructor.
  ///
  /// \param Other The object writer that would be copied.
  MCObjectWriter(const MCObjectWriter &Other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param Other The object writer that would be assigned from.
  MCObjectWriter &operator=(const MCObjectWriter &Other) = delete;
  /// Destroy the object writer.
  virtual ~MCObjectWriter();

  /// Set the assembler that owns this object writer.
  /// \param A Assembler to associate, or null to clear.
  virtual void setAssembler(MCAssembler *A) { Asm = A; }

  /// Get the MC context associated with the assembler.
  ///
  /// \return Reference to the assembler's MC context.
  MCContext &getContext() const;

  /// lifetime management
  virtual void reset();

  /// \name High-Level API
  /// @{

  /// Perform any late binding of symbols (for example, to assign symbol
  /// indices for use when generating relocations).
  ///
  /// This routine is called by the assembler after layout and relaxation is
  /// complete.
  virtual void executePostLayoutBinding() {}

  /// Record a relocation entry.
  ///
  /// This routine is called by the assembler after layout and relaxation, and
  /// post layout binding. The implementation is responsible for storing
  /// information about the relocation so that it can be emitted during
  /// writeObject().
  /// \param F Fragment that contains the fixup.
  /// \param Fixup Fixup that produced this relocation.
  /// \param Target Relocatable expression the fixup evaluates to.
  /// \param FixedValue [out] Absolute portion of the fixup value after
  /// recording the relocation.
  virtual void recordRelocation(const MCFragment &F, const MCFixup &Fixup,
                                MCValue Target, uint64_t &FixedValue);

  /// Check whether the difference (A - B) between two symbol references is
  /// fully resolved.
  ///
  /// Clients are not required to answer precisely and may conservatively return
  /// false, even when a difference is fully resolved.
  /// \param A First (minuend) symbol in the difference.
  /// \param B Second (subtrahend) symbol in the difference.
  /// \param InSet True when evaluating a difference inside a set expression.
  /// \return True if the symbol difference is known to be fully resolved.
  bool isSymbolRefDifferenceFullyResolved(const MCSymbol &A, const MCSymbol &B,
                                          bool InSet) const;

  /// Implementation hook for isSymbolRefDifferenceFullyResolved.
  ///
  /// \param SymA First symbol in the difference.
  /// \param FB Fragment of the second symbol in the difference.
  /// \param InSet True when evaluating a difference inside a set expression.
  /// \param IsPCRel True when the difference is used in a PC-relative fixup.
  /// \return True if the symbol difference is known to be fully resolved.
  virtual bool isSymbolRefDifferenceFullyResolvedImpl(const MCSymbol &SymA,
                                                      const MCFragment &FB,
                                                      bool InSet,
                                                      bool IsPCRel) const;

  /// Return the list of declared input file names.
  ///
  /// \return Mutable array reference to the declared file-name list.
  MutableArrayRef<std::pair<std::string, size_t>> getFileNames() {
    return FileNames;
  }
  /// Append a declared input file name.
  /// \param FileName Path or name of the input file to record.
  void addFileName(StringRef FileName);
  /// Set the optional compiler version string (XCOFF).
  /// \param CompilerVers Compiler version text to store.
  void setCompilerVersion(StringRef CompilerVers) {
    CompilerVersion = CompilerVers;
  }

  /// Request emission of an address-significance table.
  ///
  /// Tell the object writer to emit an address-significance table during
  /// writeObject(). If this function is not called, all symbols are treated as
  /// address-significant.
  void emitAddrsigSection() { EmitAddrsigSection = true; }

  /// Return whether an address-significance section will be emitted.
  ///
  /// \return True if an address-significance section will be emitted.
  bool getEmitAddrsigSection() { return EmitAddrsigSection; }

  /// Record the given symbol in the address-significance table to be written
  /// diring writeObject().
  /// \param Sym Symbol to mark as address-significant.
  void addAddrsigSymbol(const MCSymbol *Sym) { AddrsigSyms.push_back(Sym); }

  /// Return the symbols recorded for the address-significance table.
  ///
  /// \return Mutable reference to the address-significance symbol list.
  std::vector<const MCSymbol *> &getAddrsigSyms() { return AddrsigSyms; }
  /// Return the call-graph profile entries to emit.
  ///
  /// \return Mutable reference to the call-graph profile entry list.
  SmallVector<CGProfileEntry, 0> &getCGProfile() { return CGProfile; }

  /// Return whether Mach-O .subsections_via_symbols is enabled.
  ///
  /// \return True if .subsections_via_symbols is enabled.
  bool getSubsectionsViaSymbols() const { return SubsectionsViaSymbols; }
  /// Enable or disable Mach-O .subsections_via_symbols.
  /// \param Value True to enable subsections via symbols.
  void setSubsectionsViaSymbols(bool Value) { SubsectionsViaSymbols = Value; }

  /// Write the object file and returns the number of bytes written.
  ///
  /// This routine is called by the assembler after layout and relaxation is
  /// complete, fixups have been evaluated and applied, and relocations
  /// generated.
  /// \return Number of bytes written to the object file.
  virtual uint64_t writeObject() = 0;

  /// @}
};

/// Base class for classes that define behaviour that is specific to both the
/// target and the object format.
class MCObjectTargetWriter {
public:
  /// Destroy the target object writer.
  virtual ~MCObjectTargetWriter() = default;
  /// Set the assembler associated with this target writer.
  /// \param A Assembler to associate, or null to clear.
  void setAssembler(MCAssembler *A) { Asm = A; }
  /// Return the object file format this writer targets.
  ///
  /// \return The Triple object-format kind this writer targets.
  virtual Triple::ObjectFormatType getFormat() const = 0;

protected:
  /// Get the MC context associated with the assembler.
  ///
  /// \return Reference to the assembler's MC context.
  LLVM_ABI MCContext &getContext() const;
  /// Report an error at \p L with message \p Msg via the assembler context.
  /// \param L Source location associated with the error.
  /// \param Msg Error message to report.
  LLVM_ABI void reportError(SMLoc L, const Twine &Msg) const;

  /// Assembler that owns this target writer, or null if unset.
  MCAssembler *Asm = nullptr;
};

} // end namespace llvm

#endif // LLVM_MC_MCOBJECTWRITER_H
