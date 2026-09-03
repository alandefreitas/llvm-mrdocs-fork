//===-- llvm/Target/TargetLoweringObjectFile.h - Object Info ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements classes used to handle lowerings specific to common
// object file formats.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TARGETLOWERINGOBJECTFILE_H
#define LLVM_TARGET_TARGETLOWERINGOBJECTFILE_H

#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {

struct Align;
struct MachineJumpTableEntry;
class Constant;
class DataLayout;
class Function;
class GlobalObject;
class GlobalValue;
class MachineBasicBlock;
class MachineModuleInfo;
class Mangler;
class MCContext;
class MCExpr;
class MCSection;
class MCSymbol;
class MCSymbolRefExpr;
class MCStreamer;
class MCValue;
class Module;
class SectionKind;
class StringRef;
class TargetMachine;
class DSOLocalEquivalent;

/// Target-specific lowering of globals and constants into object-file sections.
class LLVM_ABI TargetLoweringObjectFile : public MCObjectFileInfo {
  /// Name-mangler for global names.
  Mangler *Mang = nullptr;

protected:
  /// True if the target can replace a PC-relative data access via GOT.
  bool SupportIndirectSymViaGOTPCRel = false;
  /// True if GOT PC-relative relocations may encode an added offset.
  bool SupportGOTPCRelWithOffset = true;
  /// True if the target supports TLS offset relocation in debug sections.
  bool SupportDebugThreadLocalLocation = true;
  /// Specifier used for PC-relative PLT references when supported.
  uint32_t PLTPCRelativeSpecifier = 0;

  /// DWARF encoding used for the personality routine reference.
  unsigned PersonalityEncoding = 0;
  /// DWARF encoding used for the language-specific data area (LSDA).
  unsigned LSDAEncoding = 0;
  /// DWARF encoding used for type references in exception handling.
  unsigned TTypeEncoding = 0;
  /// DWARF encoding used for call-site entries in exception handling.
  unsigned CallSiteEncoding = 0;

  /// This section contains the static constructor pointer list.
  MCSection *StaticCtorSection = nullptr;

  /// This section contains the static destructor pointer list.
  MCSection *StaticDtorSection = nullptr;

  /// Target machine associated with this object-file lowering instance.
  const TargetMachine *TM = nullptr;

public:
  /// Construct an uninitialized target object-file lowering instance.
  TargetLoweringObjectFile() = default;
  /// Deleted copy constructor; instances are not copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  TargetLoweringObjectFile(const TargetLoweringObjectFile &Other) = delete;
  /// Deleted copy assignment; instances are not copyable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  TargetLoweringObjectFile &
  operator=(const TargetLoweringObjectFile &Other) = delete;
  /// Destroy this object-file lowering instance and release owned state.
  ~TargetLoweringObjectFile() override;

  /// Return the mangler used for global names.
  ///
  /// \return The mangler used for global names.
  Mangler &getMangler() const { return *Mang; }

  /// Initialize lowering state for the given MC context and target machine.
  ///
  /// This method must be called before any actual lowering is done. This
  /// specifies the current context for codegen, and gives the lowering
  /// implementations a chance to set up their default sections.
  /// \param ctx MC context used for section and symbol creation.
  /// \param TM Target machine whose object-file conventions apply.
  virtual void Initialize(MCContext &ctx, const TargetMachine &TM);

  /// Emit the personality-routine value into the object file if required.
  ///
  /// \param Streamer Assembler streamer receiving the emission.
  /// \param TM Data layout used when emitting the value.
  /// \param Sym Personality symbol being emitted.
  /// \param MMI Machine module info providing EH context, if any.
  virtual void emitPersonalityValue(MCStreamer &Streamer, const DataLayout &TM,
                                    const MCSymbol *Sym,
                                    const MachineModuleInfo *MMI) const;

  /// Emit the module-level metadata that the platform cares about.
  ///
  /// \param Streamer Assembler streamer receiving the metadata.
  /// \param M Module whose metadata is emitted.
  virtual void emitModuleMetadata(MCStreamer &Streamer, Module &M) const {}

  /// Emit Call Graph Profile metadata.
  ///
  /// \param Streamer Assembler streamer receiving the metadata.
  /// \param M Module whose CG profile metadata is emitted.
  void emitCGProfileMetadata(MCStreamer &Streamer, Module &M) const;

  /// Emit pseudo_probe_desc metadata.
  ///
  /// \param Streamer Assembler streamer receiving the metadata.
  /// \param M Module whose pseudo-probe descriptor metadata is emitted.
  /// \param COMDATSymEmitter Optional callback that emits COMDAT symbols.
  void emitPseudoProbeDescMetadata(MCStreamer &Streamer, Module &M,
                                   std::function<void(MCStreamer &Streamer)>
                                       COMDATSymEmitter = nullptr) const;

  /// Process linker options metadata and emit platform-specific bits.
  ///
  /// \param Streamer Assembler streamer receiving linker directives.
  /// \param M Module whose linker-option metadata is processed.
  virtual void emitLinkerDirectives(MCStreamer &Streamer, Module &M) const {}

  /// Get the module-level metadata that the platform cares about.
  ///
  /// \param M Module whose platform-relevant metadata is collected.
  virtual void getModuleMetadata(Module &M) {}

  /// Given a constant with the SectionKind, return a section that it should be
  /// placed in.
  ///
  /// \param DL Data layout used to size and align the constant.
  /// \param Kind Section kind classifying the constant.
  /// \param C Constant being placed, or nullptr when not applicable.
  /// \param Alignment In/out alignment required for the constant.
  /// \param F Function owning the constant pool entry, if any.
  /// \return The section that should hold the constant.
  virtual MCSection *getSectionForConstant(const DataLayout &DL,
                                           SectionKind Kind, const Constant *C,
                                           Align &Alignment,
                                           const Function *F) const;

  /// Similar to the function above, but append \p SectionSuffix to the section
  /// name.
  ///
  /// \param DL Data layout used to size and align the constant.
  /// \param Kind Section kind classifying the constant.
  /// \param C Constant being placed, or nullptr when not applicable.
  /// \param Alignment In/out alignment required for the constant.
  /// \param F Function owning the constant pool entry, if any.
  /// \param SectionSuffix Suffix appended to the chosen section name.
  /// \return The section that should hold the constant, with \p SectionSuffix
  /// appended to its name.
  virtual MCSection *getSectionForConstant(const DataLayout &DL,
                                           SectionKind Kind, const Constant *C,
                                           Align &Alignment, const Function *F,
                                           StringRef SectionSuffix) const;

  /// Return the section used to emit the given machine basic block.
  ///
  /// \param F Function containing \p MBB.
  /// \param MBB Machine basic block whose section is requested.
  /// \param TM Target machine whose sectioning conventions apply.
  /// \return The section used to emit \p MBB.
  virtual MCSection *
  getSectionForMachineBasicBlock(const Function &F,
                                 const MachineBasicBlock &MBB,
                                 const TargetMachine &TM) const;

  /// Return a unique section for the given function when function sections are
  /// enabled.
  ///
  /// \param F Function for which a unique section is requested.
  /// \param TM Target machine whose sectioning conventions apply.
  /// \return A unique section for \p F.
  virtual MCSection *
  getUniqueSectionForFunction(const Function &F,
                              const TargetMachine &TM) const;

  /// Classify the specified global variable into a set of target independent
  /// categories embodied in SectionKind.
  ///
  /// \param GO Global object being classified.
  /// \param TM Target machine whose classification rules apply.
  /// \return The section kind classifying \p GO.
  static SectionKind getKindForGlobal(const GlobalObject *GO,
                                      const TargetMachine &TM);

  /// Return the section name specified by '#pragma clang section' or the
  /// section attribute.
  ///
  /// \param GO Global object whose custom section name is queried.
  /// \param TM Target machine whose naming conventions apply.
  /// \return The custom section name for \p GO.
  static StringRef getCustomSectionName(const GlobalObject *GO,
                                        const TargetMachine &TM);

  /// Compute the section used to emit a defined global object or function.
  ///
  /// This should not be passed external (or available externally) globals.
  /// \param GO Global object or function definition being placed.
  /// \param Kind Precomputed section kind for \p GO.
  /// \param TM Target machine whose sectioning conventions apply.
  /// \return The section used to emit \p GO.
  MCSection *SectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                              const TargetMachine &TM) const;

  /// Compute the section used to emit a defined global object or function.
  ///
  /// This should not be passed external (or available externally) globals.
  /// \param GO Global object or function definition being placed.
  /// \param TM Target machine whose sectioning conventions apply.
  /// \return The section used to emit \p GO.
  MCSection *SectionForGlobal(const GlobalObject *GO,
                              const TargetMachine &TM) const;

  /// Append the mangled name of \p GV, including any required prefix, to
  /// \p OutName.
  ///
  /// \param OutName Buffer that receives the mangled name with prefix.
  /// \param GV Global value whose name is mangled.
  /// \param TM Target machine whose mangling conventions apply.
  virtual void getNameWithPrefix(SmallVectorImpl<char> &OutName,
                                 const GlobalValue *GV,
                                 const TargetMachine &TM) const;

  /// Return the section used to emit the jump table for \p F.
  ///
  /// \param F Function whose jump table section is requested.
  /// \param TM Target machine whose sectioning conventions apply.
  /// \return The section used to emit the jump table for \p F.
  virtual MCSection *getSectionForJumpTable(const Function &F,
                                            const TargetMachine &TM) const;
  /// Return the section used to emit the given jump-table entry for \p F.
  ///
  /// \param F Function owning the jump table.
  /// \param TM Target machine whose sectioning conventions apply.
  /// \param JTE Jump-table entry being placed, if entry-specific.
  /// \return The section used to emit the jump-table entry for \p F.
  virtual MCSection *
  getSectionForJumpTable(const Function &F, const TargetMachine &TM,
                         const MachineJumpTableEntry *JTE) const;

  /// Return the section used to emit the LSDA for the given function.
  ///
  /// \param F Function whose LSDA section is requested.
  /// \param FnSym Symbol associated with \p F.
  /// \param TM Target machine whose sectioning conventions apply.
  /// \return The section used to emit the LSDA for \p F.
  virtual MCSection *getSectionForLSDA(const Function &F, const MCSymbol &FnSym,
                                       const TargetMachine &TM) const {
    return LSDASection;
  }

  /// Return true if the jump table for \p F should live in the function section.
  ///
  /// \param UsesLabelDifference True when entries use label differences.
  /// \param F Function whose jump table placement is decided.
  /// \return True if the jump table should live in the function section.
  virtual bool shouldPutJumpTableInFunctionSection(bool UsesLabelDifference,
                                                   const Function &F) const;

  /// Assign a section to a global that already has an explicit section.
  ///
  /// Targets should implement this method to assign a section to globals with
  /// an explicit section specified. The implementation of this method can
  /// assume that GO->hasSection() is true.
  /// \param GO Global object with an explicit section attribute.
  /// \param Kind Section kind classifying \p GO.
  /// \param TM Target machine whose sectioning conventions apply.
  /// \return The section assigned to \p GO.
  virtual MCSection *
  getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                           const TargetMachine &TM) const = 0;

  /// Return an MCExpr to use for a reference to the specified global variable
  /// from exception handling information.
  ///
  /// \param GV Global value referenced from EH information.
  /// \param Encoding DWARF encoding applied to the reference.
  /// \param TM Target machine whose relocation conventions apply.
  /// \param MMI Machine module info providing EH context.
  /// \param Streamer Assembler streamer used to create the expression.
  /// \return An MCExpr referencing \p GV from exception handling information.
  virtual const MCExpr *getTTypeGlobalReference(const GlobalValue *GV,
                                                unsigned Encoding,
                                                const TargetMachine &TM,
                                                MachineModuleInfo *MMI,
                                                MCStreamer &Streamer) const;

  /// Return the MCSymbol for a private symbol with global value name as its
  /// base, with the specified suffix.
  ///
  /// \param GV Global value providing the base name.
  /// \param Suffix Suffix appended to the private symbol name.
  /// \param TM Target machine whose symbol naming conventions apply.
  /// \return The private symbol with the given base name and suffix.
  MCSymbol *getSymbolWithGlobalValueBase(const GlobalValue *GV,
                                         StringRef Suffix,
                                         const TargetMachine &TM) const;

  /// Return the symbol passed to `.cfi_personality` for \p GV.
  ///
  /// \param GV Personality routine global value.
  /// \param TM Target machine whose symbol conventions apply.
  /// \param MMI Machine module info providing EH context.
  /// \return The symbol passed to \c .cfi_personality for \p GV.
  virtual MCSymbol *getCFIPersonalitySymbol(const GlobalValue *GV,
                                            const TargetMachine &TM,
                                            MachineModuleInfo *MMI) const;

  /// Return the DWARF encoding used for personality routine references.
  ///
  /// \return The DWARF encoding used for personality routine references.
  unsigned getPersonalityEncoding() const { return PersonalityEncoding; }
  /// Return the DWARF encoding used for LSDA references.
  ///
  /// \return The DWARF encoding used for LSDA references.
  unsigned getLSDAEncoding() const { return LSDAEncoding; }
  /// Return the DWARF encoding used for type references.
  ///
  /// \return The DWARF encoding used for type references.
  unsigned getTTypeEncoding() const { return TTypeEncoding; }
  /// Return the DWARF encoding used for call-site entries.
  ///
  /// \return The DWARF encoding used for call-site entries.
  unsigned getCallSiteEncoding() const;

  /// Build an MCExpr that references \p Sym with the given type encoding.
  ///
  /// \param Sym Symbol being referenced from type information.
  /// \param Encoding DWARF encoding applied to the reference.
  /// \param Streamer Assembler streamer used to create the expression.
  /// \return An MCExpr referencing \p Sym with the given encoding.
  const MCExpr *getTTypeReference(const MCSymbolRefExpr *Sym, unsigned Encoding,
                                  MCStreamer &Streamer) const;

  /// Return the section used for static constructors at \p Priority.
  ///
  /// \param Priority Initialization priority for the constructor list.
  /// \param KeySym Optional key symbol associated with the list entry.
  /// \return The section used for static constructors at \p Priority.
  virtual MCSection *getStaticCtorSection(unsigned Priority,
                                          const MCSymbol *KeySym) const {
    return StaticCtorSection;
  }

  /// Return the section used for static destructors at \p Priority.
  ///
  /// \param Priority Termination priority for the destructor list.
  /// \param KeySym Optional key symbol associated with the list entry.
  /// \return The section used for static destructors at \p Priority.
  virtual MCSection *getStaticDtorSection(unsigned Priority,
                                          const MCSymbol *KeySym) const {
    return StaticDtorSection;
  }

  /// Create a symbol reference to describe the given TLS variable when
  /// emitting the address in debug info.
  ///
  /// \param Sym TLS variable symbol being described in debug info.
  /// \return An MCExpr describing the TLS variable in debug info.
  virtual const MCExpr *getDebugThreadLocalSymbol(const MCSymbol *Sym) const;

  /// Lower a relative reference between two globals to a target MCExpr.
  ///
  /// \param LHS Left-hand global value of the relative reference.
  /// \param RHS Right-hand global value of the relative reference.
  /// \param Addend Constant addend applied to the reference.
  /// \param PCRelativeOffset Optional PC-relative offset, if applicable.
  /// \param TM Target machine whose relocation conventions apply.
  /// \return The lowered MCExpr, or nullptr if unsupported.
  virtual const MCExpr *lowerRelativeReference(
      const GlobalValue *LHS, const GlobalValue *RHS, int64_t Addend,
      std::optional<int64_t> PCRelativeOffset, const TargetMachine &TM) const {
    return nullptr;
  }

  /// Target supports a PC-relative relocation that references the PLT of a
  /// function.
  ///
  /// \return True if the target supports PC-relative PLT references.
  bool hasPLTPCRelative() const { return PLTPCRelativeSpecifier; }

  /// Lower a dso_local equivalent reference to a target MCExpr.
  ///
  /// \param LHS Left-hand symbol of the dso_local equivalent.
  /// \param RHS Right-hand symbol of the dso_local equivalent.
  /// \param Addend Constant addend applied to the reference.
  /// \param PCRelativeOffset Optional PC-relative offset, if applicable.
  /// \param TM Target machine whose relocation conventions apply.
  /// \return The lowered MCExpr, or nullptr if unsupported.
  virtual const MCExpr *lowerDSOLocalEquivalent(
      const MCSymbol *LHS, const MCSymbol *RHS, int64_t Addend,
      std::optional<int64_t> PCRelativeOffset, const TargetMachine &TM) const {
    return nullptr;
  }

  /// Target supports replacing a data "PC"-relative access to a symbol
  /// through another symbol, by accessing the later via a GOT entry instead?
  ///
  /// \return True if the target can replace a PC-relative data access via GOT.
  bool supportIndirectSymViaGOTPCRel() const {
    return SupportIndirectSymViaGOTPCRel;
  }

  /// Target GOT "PC"-relative relocation supports encoding an additional
  /// binary expression with an offset?
  ///
  /// \return True if GOT PC-relative relocations may encode an added offset.
  bool supportGOTPCRelWithOffset() const {
    return SupportGOTPCRelWithOffset;
  }

  /// Target supports TLS offset relocation in debug section?
  ///
  /// \return True if the target supports TLS offset relocation in debug
  /// sections.
  bool supportDebugThreadLocalLocation() const {
    return SupportDebugThreadLocalLocation;
  }

  /// Returns the register used as static base in RWPI variants.
  ///
  /// \return The register used as static base, or NoRegister if none.
  virtual MCRegister getStaticBase() const { return MCRegister::NoRegister; }

  /// Get the target specific RWPI relocation.
  ///
  /// \param Sym Symbol accessed through the RWPI relocation.
  /// \return The RWPI relocation expression, or nullptr if unsupported.
  virtual const MCExpr *getIndirectSymViaRWPI(const MCSymbol *Sym) const {
    return nullptr;
  }

  /// Get the target specific PC relative GOT entry relocation
  ///
  /// \param GV Global value whose GOT entry is referenced, if any.
  /// \param Sym Symbol associated with the GOT entry access.
  /// \param MV Machine-code value describing the relocation operands.
  /// \param Offset Additional offset applied to the GOT entry access.
  /// \param MMI Machine module info providing relocation context.
  /// \param Streamer Assembler streamer used to create the expression.
  /// \return The PC-relative GOT entry relocation, or nullptr if unsupported.
  virtual const MCExpr *getIndirectSymViaGOTPCRel(const GlobalValue *GV,
                                                  const MCSymbol *Sym,
                                                  const MCValue &MV,
                                                  int64_t Offset,
                                                  MachineModuleInfo *MMI,
                                                  MCStreamer &Streamer) const {
    return nullptr;
  }

  /// If supported, return the section to use for the llvm.commandline
  /// metadata. Otherwise, return nullptr.
  ///
  /// \return The section for llvm.commandline metadata, or nullptr if
  /// unsupported.
  virtual MCSection *getSectionForCommandLines() const {
    return nullptr;
  }

  /// On targets that use separate function descriptor symbols, return a section
  /// for the descriptor given its symbol. Use only with defined functions.
  ///
  /// \param F Defined function whose descriptor section is requested.
  /// \param TM Target machine whose sectioning conventions apply.
  /// \return The section for the function descriptor, or nullptr if
  /// unsupported.
  virtual MCSection *
  getSectionForFunctionDescriptor(const GlobalObject *F,
                                  const TargetMachine &TM) const {
    return nullptr;
  }

  /// On targets that support TOC entries, return a section for the entry given
  /// the symbol it refers to.
  /// TODO: Implement this interface for existing ELF targets.
  ///
  /// \param S Symbol referred to by the TOC entry.
  /// \param TM Target machine whose sectioning conventions apply.
  /// \return The section for the TOC entry, or nullptr if unsupported.
  virtual MCSection *getSectionForTOCEntry(const MCSymbol *S,
                                           const TargetMachine &TM) const {
    return nullptr;
  }

  /// On targets that associate external references with a section, return such
  /// a section for the given external global.
  ///
  /// \param GO External global object being referenced.
  /// \param TM Target machine whose sectioning conventions apply.
  /// \return The section associated with the external reference, or nullptr if
  /// unsupported.
  virtual MCSection *
  getSectionForExternalReference(const GlobalObject *GO,
                                 const TargetMachine &TM) const {
    return nullptr;
  }

  /// Targets that have a special convention for their symbols could use
  /// this hook to return a specialized symbol.
  ///
  /// \param GV Global value for which a specialized symbol is requested.
  /// \param TM Target machine whose symbol conventions apply.
  /// \return A specialized symbol for \p GV, or nullptr for the default.
  virtual MCSymbol *getTargetSymbol(const GlobalValue *GV,
                                    const TargetMachine &TM) const {
    return nullptr;
  }

  /// If supported, return the function entry point symbol.
  /// Otherwise, returns nullptr.
  /// Func must be a function or an alias which has a function as base object.
  ///
  /// \param Func Function or function alias whose entry point is requested.
  /// \param TM Target machine whose symbol conventions apply.
  /// \return The function entry point symbol, or nullptr if unsupported.
  virtual MCSymbol *getFunctionEntryPointSymbol(const GlobalValue *Func,
                                                const TargetMachine &TM) const {
    return nullptr;
  }

protected:
  /// Select the section used to emit \p GO for the given section kind.
  ///
  /// \param GO Defined global object being placed.
  /// \param Kind Section kind classifying \p GO.
  /// \param TM Target machine whose sectioning conventions apply.
  /// \return The section used to emit \p GO.
  virtual MCSection *SelectSectionForGlobal(const GlobalObject *GO,
                                            SectionKind Kind,
                                            const TargetMachine &TM) const = 0;
};

} // end namespace llvm

#endif // LLVM_TARGET_TARGETLOWERINGOBJECTFILE_H
