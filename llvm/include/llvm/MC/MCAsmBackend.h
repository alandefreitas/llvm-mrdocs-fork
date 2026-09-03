//===- llvm/MC/MCAsmBackend.h - MC Asm Backend ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMBACKEND_H
#define LLVM_MC_MCASMBACKEND_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include <cstdint>

namespace llvm {

class MCFragment;
class MCSymbol;
class MCAssembler;
class MCContext;
struct MCDwarfFrameInfo;
class MCInst;
class MCObjectStreamer;
class MCObjectTargetWriter;
class MCObjectWriter;
class MCOperand;
class MCSubtargetInfo;
class MCValue;
class raw_pwrite_stream;
class StringRef;
class raw_ostream;

/// Target independent information on a fixup kind.
struct MCFixupKindInfo {
  /// A target specific name for the fixup kind. The names will be unique for
  /// distinct kinds on any given target.
  const char *Name;

  /// The bit offset to write the relocation into.
  uint8_t TargetOffset;

  /// The number of bits written by this fixup. The bits are assumed to be
  /// contiguous.
  uint8_t TargetSize;

  /// Flags describing additional information on this fixup kind.
  unsigned Flags;
};

/// Generic interface to target specific assembler backends.
class LLVM_ABI MCAsmBackend {
protected: // Can only create subclasses.
  /// Construct an assembler backend with the given endianness.
  ///
  /// \param Endian - Byte order used when writing fixups and data.
  MCAsmBackend(llvm::endianness Endian) : Endian(Endian) {}

  /// Assembler currently associated with this backend, if any.
  MCAssembler *Asm = nullptr;

  /// True if the target may automatically pad instructions.
  bool AllowAutoPadding = false;
  /// True if the target allows enhanced relaxation of unrelaxable instructions.
  bool AllowEnhancedRelaxation = false;
  /// True if the target implements instruction bundling (`.bundle_align_mode`).
  bool AllowBundling = false;

public:
  /// Deleted copy constructor.
  ///
  /// \param Other - Unused source object.
  MCAsmBackend(const MCAsmBackend &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other - Unused source object.
  MCAsmBackend &operator=(const MCAsmBackend &Other) = delete;
  /// Destroy the assembler backend.
  virtual ~MCAsmBackend();

  /// Endianness used when writing fixups and encoded data.
  const llvm::endianness Endian;

  /// Associate this backend with assembler \p A.
  ///
  /// \param A - Assembler that owns this backend during emission.
  void setAssembler(MCAssembler *A) { Asm = A; }

  /// Return the MCContext for the associated assembler.
  ///
  /// \return The MCContext for the associated assembler.
  MCContext &getContext() const;

  /// Return true if this target might automatically pad instructions and thus
  /// need to emit padding enable/disable directives around sensative code.
  ///
  /// \return True if auto-padding may be used.
  bool allowAutoPadding() const { return AllowAutoPadding; }
  /// Return true if this target allows enhanced instruction relaxation.
  ///
  /// Return true if this target allows an unrelaxable instruction to be
  /// emitted into RelaxableFragment and then we can increase its size in a
  /// tricky way for optimization.
  ///
  /// \return True if enhanced relaxation is allowed.
  bool allowEnhancedRelaxation() const { return AllowEnhancedRelaxation; }
  /// Return true if this target implements `.bundle_align_mode`. Other targets
  /// reject the directive instead of emitting unbundled code.
  ///
  /// \return True if instruction bundling is supported.
  bool allowBundling() const { return AllowBundling; }

  /// lifetime management
  virtual void reset() {}

  /// Create a new MCObjectWriter instance for use by the assembler backend to
  /// emit the final object file.
  ///
  /// \param OS - Output stream that receives the object file.
  /// \return A new object writer for \p OS.
  std::unique_ptr<MCObjectWriter>
  createObjectWriter(raw_pwrite_stream &OS) const;

  /// Create an object writer that emits both a linked `.o` and a `.dwo` file.
  ///
  /// Create an MCObjectWriter that writes two object files: a .o file which is
  /// linked into the final program and a .dwo file which is used by debuggers.
  /// This function is only supported with ELF targets.
  ///
  /// \param OS - Output stream for the primary object file.
  /// \param DwoOS - Output stream for the split-DWARF `.dwo` file.
  /// \return A new object writer that emits both object files.
  std::unique_ptr<MCObjectWriter>
  createDwoObjectWriter(raw_pwrite_stream &OS, raw_pwrite_stream &DwoOS) const;

  /// Create the target-specific object writer used by \c createObjectWriter.
  ///
  /// \return A new target-specific object writer.
  virtual std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const = 0;

  /// \name Target Fixup Interfaces
  /// @{

  /// Map a relocation name used in .reloc to a fixup kind.
  ///
  /// \param Name - Relocation name from a `.reloc` directive.
  /// \return The corresponding fixup kind, or std::nullopt if unknown.
  virtual std::optional<MCFixupKind> getFixupKind(StringRef Name) const;

  /// Get information on a fixup kind.
  ///
  /// \param Kind - Fixup kind to describe.
  /// \return Target-independent information about \p Kind.
  virtual MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const;

  /// Evaluate a fixup, optionally overriding default resolution handling.
  ///
  /// Returning std::nullopt uses default handling for `Value` and
  /// `IsResolved`. Otherwise, returns `IsResolved` with the expectation that
  /// the hook updates `Value`.
  ///
  /// \param F - Fragment containing the fixup.
  /// \param Fixup - Fixup to evaluate.
  /// \param Target - [out] Relocatable expression the fixup evaluates to.
  /// \param Value - [out] Evaluated fixup value to update when overriding.
  /// \return std::nullopt for default handling, otherwise the resolved flag.
  virtual std::optional<bool> evaluateFixup(const MCFragment &F, MCFixup &Fixup,
                                            MCValue &Target, uint64_t &Value) {
    return {};
  }

  /// Record a relocation for the fixup when it is not fully resolved.
  ///
  /// \param F - Fragment containing the fixup.
  /// \param Fixup - Fixup that may require a relocation.
  /// \param Target - Relocatable expression evaluated for the fixup.
  /// \param Value - Evaluated fixup value, updated when recording a relocation.
  /// \param IsResolved - True if the fixup value is fully resolved.
  void maybeAddReloc(const MCFragment &F, const MCFixup &Fixup,
                     const MCValue &Target, uint64_t &Value, bool IsResolved);

  /// Apply a fixup to encoded data, recording a relocation when required.
  ///
  /// Determine if a relocation is required. In addition, apply `Value` to the
  /// `Data` fragment at the specified fixup offset if applicable. `Data` points
  /// to the first byte of the fixup offset, which may be at the content's end
  /// if the fixup is zero-sized.
  ///
  /// \param F - Fragment containing the fixup.
  /// \param Fixup - Fixup to apply.
  /// \param Target - Relocatable expression evaluated for the fixup.
  /// \param Data - First byte of encoded content at the fixup offset.
  /// \param Value - Evaluated fixup value to apply when resolved.
  /// \param IsResolved - True if the fixup value is fully resolved.
  virtual void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                          const MCValue &Target, uint8_t *Data, uint64_t Value,
                          bool IsResolved) = 0;

  /// @}

  /// \name Target Relaxation Interfaces
  /// @{

  /// Check whether the given instruction (encoded as Opcode+Operands) may need
  /// relaxation.
  ///
  /// \param Opcode - Opcode of the instruction to inspect.
  /// \param Operands - Operands of the instruction to inspect.
  /// \param STI - Subtarget information for the instruction.
  /// \return True if the instruction may need relaxation.
  virtual bool mayNeedRelaxation(unsigned Opcode, ArrayRef<MCOperand> Operands,
                                 const MCSubtargetInfo &STI) const {
    return false;
  }

  /// Target specific predicate for whether a given fixup requires the
  /// associated instruction to be relaxed.
  ///
  /// \param F - Fragment containing the fixup.
  /// \param Fixup - Fixup being considered for relaxation.
  /// \param Target - Relocatable expression evaluated for the fixup.
  /// \param Value - Current evaluated value of the fixup.
  /// \param Resolved - True if the fixup value is fully resolved.
  /// \return True if the instruction must be relaxed.
  virtual bool fixupNeedsRelaxationAdvanced(const MCFragment &F,
                                            const MCFixup &Fixup,
                                            const MCValue &Target,
                                            uint64_t Value,
                                            bool Resolved) const;

  /// Relax the instruction in the given fragment to the next wider instruction.
  ///
  /// \param [out] Inst The instruction to relax, which is also the relaxed
  /// instruction.
  /// \param STI the subtarget information for the associated instruction.
  virtual void relaxInstruction(MCInst &Inst,
                                const MCSubtargetInfo &STI) const {
    llvm_unreachable("Needed if fixupNeedsRelaxationAdvanced may return true");
  }

  // Defined by linker relaxation targets.

  /// Relax an alignment fragment, optionally computing custom padding size.
  ///
  /// Return false to use default handling. Otherwise, set `Size` to the number
  /// of padding bytes.
  ///
  /// \param F - Alignment fragment to relax.
  /// \param Size - [out] Padding size in bytes when custom handling is used.
  /// \return True if custom padding handling was used.
  virtual bool relaxAlign(MCFragment &F, unsigned &Size) { return false; }
  /// Relax a DWARF line-address delta fragment for linker relaxation.
  ///
  /// \param F - DWARF line-address fragment to relax.
  /// \return True if the fragment was handled by the backend.
  virtual bool relaxDwarfLineAddr(MCFragment &F) const { return false; }
  /// Relax a DWARF CFA advance fragment for linker relaxation.
  ///
  /// \param F - DWARF CFA advance fragment to relax.
  /// \return True if the fragment was handled by the backend.
  virtual bool relaxDwarfCFA(MCFragment &F) const { return false; }
  /// Relax an SFrame CFA advance fragment for linker relaxation.
  ///
  /// \param F - SFrame CFA advance fragment to relax.
  /// \return True if the fragment was handled by the backend.
  virtual bool relaxSFrameCFA(MCFragment &F) const { return false; }

  /// Relax a LEB128 fragment, optionally emitting a relocation.
  ///
  /// Defined by linker relaxation targets to possibly emit LEB128 relocations
  /// and set Value at the relocated location.
  ///
  /// \param F - LEB128 fragment to relax.
  /// \param Value - [out] Value written at the relocated LEB128 location.
  /// \return `{Relaxed, UseZeroPad}`: whether relaxation succeeded and whether
  /// zero-padding should be used.
  virtual std::pair<bool, bool> relaxLEB128(MCFragment &F,
                                            int64_t &Value) const {
    return std::make_pair(false, false);
  }

  /// @}

  /// Return the minimum nop size in bytes for this target.
  ///
  /// Returns the minimum size of a nop in bytes on this target. The assembler
  /// will use this to emit excess padding in situations where the padding
  /// required for simple alignment would be less than the minimum nop size.
  ///
  /// \return The minimum nop size in bytes.
  virtual unsigned getMinimumNopSize() const { return 1; }

  /// Returns the maximum size of a nop in bytes on this target.
  ///
  /// \param STI - Subtarget used to select the largest encodable nop.
  /// \return The maximum nop size in bytes, or 0 if unlimited/unused.
  virtual unsigned getMaximumNopSize(const MCSubtargetInfo &STI) const {
    return 0;
  }

  /// Write an (optimal) nop sequence of Count bytes to the given output. If the
  /// target cannot generate such a sequence, it should return an error.
  ///
  /// \param OS - Output stream that receives the nop bytes.
  /// \param Count - Number of nop bytes to emit.
  /// \param STI - Subtarget used to select nop encodings, or null.
  /// \return - True on success.
  virtual bool writeNopData(raw_ostream &OS, uint64_t Count,
                            const MCSubtargetInfo *STI) const = 0;

  /// Return true if fragment offsets changed and another layout pass is needed.
  ///
  /// Return true if fragment offsets have been adjusted and an extra layout
  /// iteration is needed.
  ///
  /// \return True if another layout iteration is needed.
  virtual bool finishLayout() const { return false; }

  /// Generate the compact unwind encoding for the CFI instructions.
  ///
  /// \param FI - DWARF frame info describing the function's CFI.
  /// \param Ctxt - Assembler context used for compact-unwind decisions.
  /// \return The compact unwind encoding, or 0 if unavailable.
  virtual uint64_t generateCompactUnwindEncoding(const MCDwarfFrameInfo *FI,
                                                 const MCContext *Ctxt) const {
    return 0;
  }

  /// Return true if \p Sym is a canonical Darwin personality function.
  ///
  /// \param Sym - Personality symbol to test, or null for no personality.
  /// \return True if \p Sym is a canonical Darwin personality, or null.
  bool isDarwinCanonicalPersonality(const MCSymbol *Sym) const;

  /// Return the subtarget info for an instruction-bearing fragment.
  ///
  /// Return STI for fragments with hasInstructions() == true.
  ///
  /// \param F - Fragment whose subtarget information is requested.
  /// \return The fragment's subtarget info, or null if none.
  static const MCSubtargetInfo *getSubtargetInfo(const MCFragment &F);
};

} // end namespace llvm

#endif // LLVM_MC_MCASMBACKEND_H
