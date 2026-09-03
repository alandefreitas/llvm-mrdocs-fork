//==- llvm/CodeGen/TargetLoweringObjectFileImpl.h - Object Info --*- C++ -*-==//
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

#ifndef LLVM_CODEGEN_TARGETLOWERINGOBJECTFILEIMPL_H
#define LLVM_CODEGEN_TARGETLOWERINGOBJECTFILEIMPL_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/Target/TargetLoweringObjectFile.h"

namespace llvm {

class GlobalValue;
class MachineModuleInfo;
class MachineFunction;
class MCContext;
class MCExpr;
class MCSection;
class MCSymbol;
class Module;
class TargetMachine;

/// Object-file lowering for ELF targets.
class LLVM_ABI TargetLoweringObjectFileELF : public TargetLoweringObjectFile {
  bool UseInitArray = false;
  mutable unsigned NextUniqueID = 1;  // ID 0 is reserved for execute-only sections
  SmallPtrSet<GlobalObject *, 2> Used;

protected:
  /// Specifier used for PLT-relative symbol references.
  uint16_t PLTRelativeSpecifier = 0;

  /// Return true if \p C is a large constant for the given section kind.
  ///
  /// \param DL Data layout of the module.
  /// \param Kind Section kind classifying the constant.
  /// \param C Constant to inspect; may be null.
  /// \return True if \p C is a large constant for \p Kind.
  bool isLargeConstant(const DataLayout &DL, SectionKind Kind,
                       const Constant *C) const;

  /// Select an ELF section for a constant, optionally appending a suffix.
  ///
  /// \param DL Data layout of the module.
  /// \param Kind Section kind classifying the constant.
  /// \param C Constant to place; may be null.
  /// \param SectionSuffix Optional suffix appended to the section name.
  /// \return The ELF section selected for the constant.
  MCSection *getSectionForConstantImpl(const DataLayout &DL, SectionKind Kind,
                                       const Constant *C,
                                       StringRef SectionSuffix) const;

public:
  /// Destroy this ELF object-file lowering instance.
  ~TargetLoweringObjectFileELF() override = default;

  /// Initialize ELF default sections for the given codegen context.
  ///
  /// This method must be called before any actual lowering is done. This
  /// specifies the current context for codegen, and gives the lowering
  /// implementations a chance to set up their default sections.
  ///
  /// \param Ctx MC context used to create sections and symbols.
  /// \param TM Target machine providing ABI and section options.
  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

  /// Collect module-level metadata that affects ELF section selection.
  ///
  /// \param M Module whose metadata is inspected.
  void getModuleMetadata(Module &M) override;

  /// Emit Obj-C garbage collection and linker options.
  ///
  /// \param Streamer Assembler streamer that receives the metadata.
  /// \param M Module whose flags and options are emitted.
  void emitModuleMetadata(MCStreamer &Streamer, Module &M) const override;

  /// Emit the personality value used by DWARF EH for \p Sym.
  ///
  /// \param Streamer Assembler streamer that receives the emission.
  /// \param DL Data layout of the module.
  /// \param Sym Personality function symbol.
  /// \param MMI Machine module info providing EH context; may be null.
  void emitPersonalityValue(MCStreamer &Streamer, const DataLayout &DL,
                            const MCSymbol *Sym,
                            const MachineModuleInfo *MMI) const override;

  /// Target-specific emission of a personality value for \p Sym.
  ///
  /// \param Streamer Assembler streamer that receives the emission.
  /// \param DL Data layout of the module.
  /// \param Sym Personality function symbol.
  /// \param MMI Machine module info providing EH context; may be null.
  virtual void emitPersonalityValueImpl(MCStreamer &Streamer,
                                        const DataLayout &DL,
                                        const MCSymbol *Sym,
                                        const MachineModuleInfo *MMI) const;

  /// Emit ELF linker directives derived from module metadata.
  ///
  /// \param Streamer Assembler streamer that receives the directives.
  /// \param M Module whose linker options are emitted.
  void emitLinkerDirectives(MCStreamer &Streamer, Module &M) const override;

  /// Given a constant with the SectionKind, return a section that it should be
  /// placed in.
  ///
  /// \param DL Data layout of the module.
  /// \param Kind Section kind classifying the constant.
  /// \param C Constant to place; may be null.
  /// \param Alignment Alignment required for the constant; may be increased.
  /// \param F Function associated with the constant, if any; may be null.
  /// \return The section that should hold the constant.
  MCSection *getSectionForConstant(const DataLayout &DL, SectionKind Kind,
                                   const Constant *C, Align &Alignment,
                                   const Function *F) const override;

  /// Similar to the function above, but append \p SectionSuffix to the section
  /// name.
  ///
  /// \param DL Data layout of the module.
  /// \param Kind Section kind classifying the constant.
  /// \param C Constant to place; may be null.
  /// \param Alignment Alignment required for the constant; may be increased.
  /// \param F Function associated with the constant, if any; may be null.
  /// \param SectionSuffix Suffix appended to the section name.
  /// \return The section for the constant, with \p SectionSuffix appended to its name.
  MCSection *getSectionForConstant(const DataLayout &DL, SectionKind Kind,
                                   const Constant *C, Align &Alignment,
                                   const Function *F,
                                   StringRef SectionSuffix) const override;

  /// Return the ELF section named by an explicit section attribute on \p GO.
  ///
  /// \param GO Global with an explicit section name.
  /// \param Kind Section kind classifying the global.
  /// \param TM Target machine providing ABI and section options.
  /// \return The ELF section named by the explicit section attribute on \p GO.
  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

  /// Select the default ELF section for \p GO based on its section kind.
  ///
  /// \param GO Global to place.
  /// \param Kind Section kind classifying the global.
  /// \param TM Target machine providing ABI and section options.
  /// \return The default ELF section for \p GO.
  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

  /// Return the ELF section used for jump tables of \p F.
  ///
  /// \param F Function whose jump table is being placed.
  /// \param TM Target machine providing ABI and section options.
  /// \return The ELF section used for jump tables of \p F.
  MCSection *getSectionForJumpTable(const Function &F,
                                    const TargetMachine &TM) const override;

  /// Return the ELF section used for a specific jump-table entry of \p F.
  ///
  /// \param F Function whose jump table is being placed.
  /// \param TM Target machine providing ABI and section options.
  /// \param JTE Jump-table entry being placed; may be null.
  /// \return The ELF section used for the jump-table entry of \p F.
  MCSection *
  getSectionForJumpTable(const Function &F, const TargetMachine &TM,
                         const MachineJumpTableEntry *JTE) const override;

  /// Return the ELF LSDA section for exception handling of \p F.
  ///
  /// \param F Function whose LSDA is being placed.
  /// \param FnSym Symbol for the function.
  /// \param TM Target machine providing ABI and section options.
  /// \return The ELF LSDA section for exception handling of \p F.
  MCSection *getSectionForLSDA(const Function &F, const MCSymbol &FnSym,
                               const TargetMachine &TM) const override;

  /// Return the ELF section for machine basic block \p MBB of \p F.
  ///
  /// \param F Function containing the basic block.
  /// \param MBB Machine basic block to place.
  /// \param TM Target machine providing ABI and section options.
  /// \return The ELF section for machine basic block \p MBB of \p F.
  MCSection *
  getSectionForMachineBasicBlock(const Function &F,
                                 const MachineBasicBlock &MBB,
                                 const TargetMachine &TM) const override;

  /// Return a unique ELF section reserved for function \p F.
  ///
  /// \param F Function that needs a unique section.
  /// \param TM Target machine providing ABI and section options.
  /// \return A unique ELF section reserved for \p F.
  MCSection *
  getUniqueSectionForFunction(const Function &F,
                              const TargetMachine &TM) const override;

  /// Return true if jump tables for \p F should live in its text section.
  ///
  /// \param UsesLabelDifference Whether entries use label differences.
  /// \param F Function whose jump tables are being placed.
  /// \return True if jump tables for \p F should live in its text section.
  bool shouldPutJumpTableInFunctionSection(bool UsesLabelDifference,
                                           const Function &F) const override;

  /// Return an MCExpr to use for a reference to the specified type info global
  /// variable from exception handling information.
  ///
  /// \param GV Type-info global being referenced.
  /// \param Encoding DWARF pointer-encoding for the reference.
  /// \param TM Target machine providing ABI and section options.
  /// \param MMI Machine module info providing EH context.
  /// \param Streamer Assembler streamer used to form the reference.
  /// \return An MCExpr referencing \p GV from exception handling information.
  const MCExpr *getTTypeGlobalReference(const GlobalValue *GV,
                                        unsigned Encoding,
                                        const TargetMachine &TM,
                                        MachineModuleInfo *MMI,
                                        MCStreamer &Streamer) const override;

  /// Return the symbol passed to \c .cfi_personality for \p GV.
  ///
  /// \param GV Personality function global.
  /// \param TM Target machine providing ABI and section options.
  /// \param MMI Machine module info providing EH context.
  /// \return The symbol passed to \c .cfi_personality for \p GV.
  MCSymbol *getCFIPersonalitySymbol(const GlobalValue *GV,
                                    const TargetMachine &TM,
                                    MachineModuleInfo *MMI) const override;

  /// Configure whether ELF uses \c .init_array / \c .fini_array sections.
  ///
  /// \param UseInitArray_ True to use init/fini arrays instead of ctors/dtors.
  void InitializeELF(bool UseInitArray_);

  /// Return the ELF section for static constructors at \p Priority.
  ///
  /// \param Priority Constructor priority; lower runs earlier.
  /// \param KeySym COMDAT key symbol, if any; may be null.
  /// \return The ELF section for static constructors at \p Priority.
  MCSection *getStaticCtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;

  /// Return the ELF section for static destructors at \p Priority.
  ///
  /// \param Priority Destructor priority; lower runs earlier.
  /// \param KeySym COMDAT key symbol, if any; may be null.
  /// \return The ELF section for static destructors at \p Priority.
  MCSection *getStaticDtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;

  /// Lower \p LHS - \p RHS + \p Addend into an ELF relocation expression.
  ///
  /// \param LHS Left-hand symbol of the difference.
  /// \param RHS Right-hand symbol of the difference.
  /// \param Addend Constant addend applied to the difference.
  /// \param PCRelativeOffset Optional PC-relative offset; may be empty.
  /// \return An MCExpr for \p LHS - \p RHS + \p Addend.
  const MCExpr *
  lowerSymbolDifference(const MCSymbol *LHS, const MCSymbol *RHS,
                        int64_t Addend,
                        std::optional<int64_t> PCRelativeOffset) const;

  /// Lower a dso_local equivalent reference for ELF.
  ///
  /// \param LHS Left-hand symbol of the reference.
  /// \param RHS Right-hand symbol of the reference.
  /// \param Addend Constant addend applied to the reference.
  /// \param PCRelativeOffset Optional PC-relative offset; may be empty.
  /// \param TM Target machine providing ABI and section options.
  /// \return An MCExpr for the dso_local equivalent reference.
  const MCExpr *lowerDSOLocalEquivalent(const MCSymbol *LHS,
                                        const MCSymbol *RHS, int64_t Addend,
                                        std::optional<int64_t> PCRelativeOffset,
                                        const TargetMachine &TM) const override;

  /// Return the ELF section used for \c llvm.commandline metadata.
  ///
  /// \return The ELF section used for \c llvm.commandline metadata.
  MCSection *getSectionForCommandLines() const override;
};

/// Object-file lowering for Mach-O targets.
class LLVM_ABI TargetLoweringObjectFileMachO : public TargetLoweringObjectFile {
public:
  /// Construct a Mach-O object-file lowering instance.
  TargetLoweringObjectFileMachO();

  /// Destroy this Mach-O object-file lowering instance.
  ~TargetLoweringObjectFileMachO() override = default;

  /// Initialize Mach-O default sections for the given codegen context.
  ///
  /// This method must be called before any actual lowering is done. This
  /// specifies the current context for codegen, and gives the lowering
  /// implementations a chance to set up their default sections.
  ///
  /// \param Ctx MC context used to create sections and symbols.
  /// \param TM Target machine providing ABI and section options.
  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

  /// Return the Mach-O section for static destructors at \p Priority.
  ///
  /// \param Priority Destructor priority; lower runs earlier.
  /// \param KeySym COMDAT key symbol, if any; may be null.
  /// \return The Mach-O section for static destructors at \p Priority.
  MCSection *getStaticDtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;

  /// Emit the module flags that specify the garbage collection information.
  ///
  /// \param Streamer Assembler streamer that receives the metadata.
  /// \param M Module whose flags are emitted.
  void emitModuleMetadata(MCStreamer &Streamer, Module &M) const override;

  /// Emit Mach-O linker directives derived from module metadata.
  ///
  /// \param Streamer Assembler streamer that receives the directives.
  /// \param M Module whose linker options are emitted.
  void emitLinkerDirectives(MCStreamer &Streamer, Module &M) const override;

  /// Select the default Mach-O section for \p GO based on its section kind.
  ///
  /// \param GO Global to place.
  /// \param Kind Section kind classifying the global.
  /// \param TM Target machine providing ABI and section options.
  /// \return The default Mach-O section for \p GO.
  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

  /// Return the Mach-O section named by an explicit section attribute on \p GO.
  ///
  /// \param GO Global with an explicit section name.
  /// \param Kind Section kind classifying the global.
  /// \param TM Target machine providing ABI and section options.
  /// \return The Mach-O section named by the explicit section attribute on \p GO.
  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

  /// Return the Mach-O section that should hold constant \p C.
  ///
  /// \param DL Data layout of the module.
  /// \param Kind Section kind classifying the constant.
  /// \param C Constant to place; may be null.
  /// \param Alignment Alignment required for the constant; may be increased.
  /// \param F Function associated with the constant, if any; may be null.
  /// \return The Mach-O section that should hold constant \p C.
  MCSection *getSectionForConstant(const DataLayout &DL, SectionKind Kind,
                                   const Constant *C, Align &Alignment,
                                   const Function *F) const override;

  /// The mach-o version of this method defaults to returning a stub reference.
  ///
  /// \param GV Type-info global being referenced.
  /// \param Encoding DWARF pointer-encoding for the reference.
  /// \param TM Target machine providing ABI and section options.
  /// \param MMI Machine module info providing EH context.
  /// \param Streamer Assembler streamer used to form the reference.
  /// \return An MCExpr referencing \p GV, defaulting to a stub reference.
  const MCExpr *getTTypeGlobalReference(const GlobalValue *GV,
                                        unsigned Encoding,
                                        const TargetMachine &TM,
                                        MachineModuleInfo *MMI,
                                        MCStreamer &Streamer) const override;

  /// Return the symbol passed to \c .cfi_personality for \p GV.
  ///
  /// \param GV Personality function global.
  /// \param TM Target machine providing ABI and section options.
  /// \param MMI Machine module info providing EH context.
  /// \return The symbol passed to \c .cfi_personality for \p GV.
  MCSymbol *getCFIPersonalitySymbol(const GlobalValue *GV,
                                    const TargetMachine &TM,
                                    MachineModuleInfo *MMI) const override;

  /// Get MachO PC relative GOT entry relocation.
  ///
  /// \param GV Global value associated with the reference, if any; may be null.
  /// \param Sym Symbol whose GOT entry is referenced.
  /// \param MV Base MCValue of the relocation.
  /// \param Offset Additional constant offset applied to the reference.
  /// \param MMI Machine module info providing relocation context.
  /// \param Streamer Assembler streamer used to form the reference.
  /// \return An MCExpr for the Mach-O PC-relative GOT entry relocation.
  const MCExpr *getIndirectSymViaGOTPCRel(const GlobalValue *GV,
                                          const MCSymbol *Sym,
                                          const MCValue &MV, int64_t Offset,
                                          MachineModuleInfo *MMI,
                                          MCStreamer &Streamer) const override;

  /// Append the Mach-O mangled name of \p GV to \p OutName.
  ///
  /// \param OutName Buffer that receives the mangled name.
  /// \param GV Global whose name is mangled.
  /// \param TM Target machine providing mangling options.
  void getNameWithPrefix(SmallVectorImpl<char> &OutName, const GlobalValue *GV,
                         const TargetMachine &TM) const override;

  /// Return the Mach-O section used for \c llvm.commandline metadata.
  ///
  /// \return The Mach-O section used for \c llvm.commandline metadata.
  MCSection *getSectionForCommandLines() const override;
};

/// Object-file lowering for COFF targets.
class LLVM_ABI TargetLoweringObjectFileCOFF : public TargetLoweringObjectFile {
  mutable unsigned NextUniqueID = 0;
  const TargetMachine *TM = nullptr;

public:
  /// Destroy this COFF object-file lowering instance.
  ~TargetLoweringObjectFileCOFF() override = default;

  /// Initialize COFF default sections for the given codegen context.
  ///
  /// This method must be called before any actual lowering is done. This
  /// specifies the current context for codegen, and gives the lowering
  /// implementations a chance to set up their default sections.
  ///
  /// \param Ctx MC context used to create sections and symbols.
  /// \param TM Target machine providing ABI and section options.
  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

  /// Return the COFF section named by an explicit section attribute on \p GO.
  ///
  /// \param GO Global with an explicit section name.
  /// \param Kind Section kind classifying the global.
  /// \param TM Target machine providing ABI and section options.
  /// \return The COFF section named by the explicit section attribute on \p GO.
  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

  /// Select the default COFF section for \p GO based on its section kind.
  ///
  /// \param GO Global to place.
  /// \param Kind Section kind classifying the global.
  /// \param TM Target machine providing ABI and section options.
  /// \return The default COFF section for \p GO.
  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

  /// Append the COFF mangled name of \p GV to \p OutName.
  ///
  /// \param OutName Buffer that receives the mangled name.
  /// \param GV Global whose name is mangled.
  /// \param TM Target machine providing mangling options.
  void getNameWithPrefix(SmallVectorImpl<char> &OutName, const GlobalValue *GV,
                         const TargetMachine &TM) const override;

  /// Return the COFF section used for jump tables of \p F.
  ///
  /// \param F Function whose jump table is being placed.
  /// \param TM Target machine providing ABI and section options.
  /// \return The COFF section used for jump tables of \p F.
  MCSection *getSectionForJumpTable(const Function &F,
                                    const TargetMachine &TM) const override;

  /// Return true if jump tables for \p F should live in its text section.
  ///
  /// \param UsesLabelDifference Whether entries use label differences.
  /// \param F Function whose jump tables are being placed.
  /// \return True if jump tables for \p F should live in its text section.
  bool shouldPutJumpTableInFunctionSection(bool UsesLabelDifference,
                                           const Function &F) const override;

  /// Emit Obj-C garbage collection and linker options.
  ///
  /// \param Streamer Assembler streamer that receives the metadata.
  /// \param M Module whose flags and options are emitted.
  void emitModuleMetadata(MCStreamer &Streamer, Module &M) const override;

  /// Emit COFF linker directives derived from module metadata.
  ///
  /// \param Streamer Assembler streamer that receives the directives.
  /// \param M Module whose linker options are emitted.
  void emitLinkerDirectives(MCStreamer &Streamer, Module &M) const override;

  /// Return the COFF section for static constructors at \p Priority.
  ///
  /// \param Priority Constructor priority; lower runs earlier.
  /// \param KeySym COMDAT key symbol, if any; may be null.
  /// \return The COFF section for static constructors at \p Priority.
  MCSection *getStaticCtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;

  /// Return the COFF section for static destructors at \p Priority.
  ///
  /// \param Priority Destructor priority; lower runs earlier.
  /// \param KeySym COMDAT key symbol, if any; may be null.
  /// \return The COFF section for static destructors at \p Priority.
  MCSection *getStaticDtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;

  /// Lower a relative reference between \p LHS and \p RHS for COFF.
  ///
  /// \param LHS Left-hand global of the relative reference.
  /// \param RHS Right-hand global of the relative reference.
  /// \param Addend Constant addend applied to the reference.
  /// \param PCRelativeOffset Optional PC-relative offset; may be empty.
  /// \param TM Target machine providing ABI and section options.
  /// \return An MCExpr for the relative reference, or nullptr if unsupported.
  const MCExpr *lowerRelativeReference(const GlobalValue *LHS,
                                       const GlobalValue *RHS, int64_t Addend,
                                       std::optional<int64_t> PCRelativeOffset,
                                       const TargetMachine &TM) const override;

  /// Given a mergeable constant with the specified size and relocation
  /// information, return a section that it should be placed in.
  ///
  /// \param DL Data layout of the module.
  /// \param Kind Section kind classifying the constant.
  /// \param C Constant to place; may be null.
  /// \param Alignment Alignment required for the constant; may be increased.
  /// \param F Function associated with the constant, if any; may be null.
  /// \return The section that should hold the mergeable constant.
  MCSection *getSectionForConstant(const DataLayout &DL, SectionKind Kind,
                                   const Constant *C, Align &Alignment,
                                   const Function *F) const override;
};

/// Object-file lowering for WebAssembly targets.
class LLVM_ABI TargetLoweringObjectFileWasm : public TargetLoweringObjectFile {
  mutable unsigned NextUniqueID = 0;
  SmallPtrSet<GlobalObject *, 2> Used;

public:
  /// Construct a WebAssembly object-file lowering instance.
  TargetLoweringObjectFileWasm() = default;

  /// Destroy this WebAssembly object-file lowering instance.
  ~TargetLoweringObjectFileWasm() override = default;

  /// Collect module-level metadata that affects Wasm section selection.
  ///
  /// \param M Module whose metadata is inspected.
  void getModuleMetadata(Module &M) override;

  /// Return the Wasm section named by an explicit section attribute on \p GO.
  ///
  /// \param GO Global with an explicit section name.
  /// \param Kind Section kind classifying the global.
  /// \param TM Target machine providing ABI and section options.
  /// \return The Wasm section named by the explicit section attribute on \p GO.
  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

  /// Select the default Wasm section for \p GO based on its section kind.
  ///
  /// \param GO Global to place.
  /// \param Kind Section kind classifying the global.
  /// \param TM Target machine providing ABI and section options.
  /// \return The default Wasm section for \p GO.
  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

  /// Return true if jump tables for \p F should live in its text section.
  ///
  /// \param UsesLabelDifference Whether entries use label differences.
  /// \param F Function whose jump tables are being placed.
  /// \return True if jump tables for \p F should live in its text section.
  bool shouldPutJumpTableInFunctionSection(bool UsesLabelDifference,
                                           const Function &F) const override;

  /// Initialize Wasm-specific object-file lowering defaults.
  void InitializeWasm();

  /// Return the Wasm section for static constructors at \p Priority.
  ///
  /// \param Priority Constructor priority; lower runs earlier.
  /// \param KeySym COMDAT key symbol, if any; may be null.
  /// \return The Wasm section for static constructors at \p Priority.
  MCSection *getStaticCtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;

  /// Return the Wasm section for static destructors at \p Priority.
  ///
  /// \param Priority Destructor priority; lower runs earlier.
  /// \param KeySym COMDAT key symbol, if any; may be null.
  /// \return The Wasm section for static destructors at \p Priority.
  MCSection *getStaticDtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;
};

/// Object-file lowering for XCOFF targets.
class LLVM_ABI TargetLoweringObjectFileXCOFF : public TargetLoweringObjectFile {
public:
  /// Construct an XCOFF object-file lowering instance.
  TargetLoweringObjectFileXCOFF() = default;

  /// Destroy this XCOFF object-file lowering instance.
  ~TargetLoweringObjectFileXCOFF() override = default;

  /// Return true if \p MF needs an EH info block.
  ///
  /// \param MF Machine function to inspect.
  /// \return True if \p MF needs an EH info block.
  static bool ShouldEmitEHBlock(const MachineFunction *MF);

  /// Return true if the SSP canary bit should be set in the traceback table.
  ///
  /// \param MF Machine function to inspect.
  /// \return True if the SSP canary bit should be set in the traceback table.
  static bool ShouldSetSSPCanaryBitInTB(const MachineFunction *MF);

  /// Return the EH info table symbol for \p MF.
  ///
  /// \param MF Machine function whose EH info table is requested.
  /// \return The EH info table symbol for \p MF.
  static MCSymbol *getEHInfoTableSymbol(const MachineFunction *MF);

  /// Initialize XCOFF default sections for the given codegen context.
  ///
  /// This method must be called before any actual lowering is done. This
  /// specifies the current context for codegen, and gives the lowering
  /// implementations a chance to set up their default sections.
  ///
  /// \param Ctx MC context used to create sections and symbols.
  /// \param TM Target machine providing ABI and section options.
  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

  /// Return true if jump tables for \p F should live in its text section.
  ///
  /// \param UsesLabelDifference Whether entries use label differences.
  /// \param F Function whose jump tables are being placed.
  /// \return True if jump tables for \p F should live in its text section.
  bool shouldPutJumpTableInFunctionSection(bool UsesLabelDifference,
                                           const Function &F) const override;

  /// Return the XCOFF section named by an explicit section attribute on \p GO.
  ///
  /// \param GO Global with an explicit section name.
  /// \param Kind Section kind classifying the global.
  /// \param TM Target machine providing ABI and section options.
  /// \return The XCOFF section named by the explicit section attribute on \p GO.
  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

  /// Return the XCOFF section for static constructors at \p Priority.
  ///
  /// \param Priority Constructor priority; lower runs earlier.
  /// \param KeySym COMDAT key symbol, if any; may be null.
  /// \return The XCOFF section for static constructors at \p Priority.
  MCSection *getStaticCtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;

  /// Return the XCOFF section for static destructors at \p Priority.
  ///
  /// \param Priority Destructor priority; lower runs earlier.
  /// \param KeySym COMDAT key symbol, if any; may be null.
  /// \return The XCOFF section for static destructors at \p Priority.
  MCSection *getStaticDtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;

  /// Select the default XCOFF section for \p GO based on its section kind.
  ///
  /// \param GO Global to place.
  /// \param Kind Section kind classifying the global.
  /// \param TM Target machine providing ABI and section options.
  /// \return The default XCOFF section for \p GO.
  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

  /// Return the XCOFF section used for jump tables of \p F.
  ///
  /// \param F Function whose jump table is being placed.
  /// \param TM Target machine providing ABI and section options.
  /// \return The XCOFF section used for jump tables of \p F.
  MCSection *getSectionForJumpTable(const Function &F,
                                    const TargetMachine &TM) const override;

  /// Given a constant with the SectionKind, return a section that it should be
  /// placed in.
  ///
  /// \param DL Data layout of the module.
  /// \param Kind Section kind classifying the constant.
  /// \param C Constant to place; may be null.
  /// \param Alignment Alignment required for the constant; may be increased.
  /// \param F Function associated with the constant, if any; may be null.
  /// \return The section that should hold the constant.
  MCSection *getSectionForConstant(const DataLayout &DL, SectionKind Kind,
                                   const Constant *C, Align &Alignment,
                                   const Function *F) const override;

  /// Return the XCOFF storage class for global \p GV.
  ///
  /// \param GV Global whose storage class is requested.
  /// \return The XCOFF storage class for \p GV.
  static XCOFF::StorageClass getStorageClassForGlobal(const GlobalValue *GV);

  /// Return the XCOFF csect that holds the function descriptor for \p F.
  ///
  /// \param F Defined function whose descriptor section is requested.
  /// \param TM Target machine providing ABI and section options.
  /// \return The XCOFF csect that holds the function descriptor for \p F.
  MCSection *
  getSectionForFunctionDescriptor(const GlobalObject *F,
                                  const TargetMachine &TM) const override;

  /// Return the XCOFF TOC entry section for symbol \p Sym.
  ///
  /// \param Sym Symbol referenced by the TOC entry.
  /// \param TM Target machine providing ABI and section options.
  /// \return The XCOFF TOC entry section for \p Sym.
  MCSection *getSectionForTOCEntry(const MCSymbol *Sym,
                                   const TargetMachine &TM) const override;

  /// For external functions, this will always return a function descriptor
  /// csect.
  ///
  /// \param GO External global whose reference section is requested.
  /// \param TM Target machine providing ABI and section options.
  /// \return The function descriptor csect for external \p GO.
  MCSection *
  getSectionForExternalReference(const GlobalObject *GO,
                                 const TargetMachine &TM) const override;

  /// For functions, this will always return a function descriptor symbol.
  ///
  /// \param GV Global whose target-specific symbol is requested.
  /// \param TM Target machine providing ABI and section options.
  /// \return The function descriptor symbol for \p GV.
  MCSymbol *getTargetSymbol(const GlobalValue *GV,
                            const TargetMachine &TM) const override;

  /// Return the XCOFF function entry-point symbol for \p Func.
  ///
  /// \param Func Function or function alias whose entry point is requested.
  /// \param TM Target machine providing ABI and section options.
  /// \return The XCOFF function entry-point symbol for \p Func.
  MCSymbol *getFunctionEntryPointSymbol(const GlobalValue *Func,
                                        const TargetMachine &TM) const override;

  /// Return the XCOFF LSDA section for exception handling of \p F.
  ///
  /// For functions, this will return the LSDA section. If option
  /// -ffunction-sections is on, this will return a unique csect with the
  /// function name appended to .gcc_except_table as a suffix of the LSDA
  /// section name.
  ///
  /// \param F Function whose LSDA is being placed.
  /// \param FnSym Symbol for the function.
  /// \param TM Target machine providing ABI and section options.
  /// \return The XCOFF LSDA section for \p F.
  MCSection *getSectionForLSDA(const Function &F, const MCSymbol &FnSym,
                               const TargetMachine &TM) const override;
};

/// Object-file lowering for GOFF targets.
class LLVM_ABI TargetLoweringObjectFileGOFF : public TargetLoweringObjectFile {
  std::string DefaultRootSDName;
  std::string DefaultADAPRName;

public:
  /// Construct a GOFF object-file lowering instance.
  TargetLoweringObjectFileGOFF();

  /// Destroy this GOFF object-file lowering instance.
  ~TargetLoweringObjectFileGOFF() override = default;

  /// Collect module-level metadata that affects GOFF section selection.
  ///
  /// \param M Module whose metadata is inspected.
  void getModuleMetadata(Module &M) override;

  /// Return true if jump tables for \p F should live in its text section.
  ///
  /// \param UsesLabelDifference Whether entries use label differences.
  /// \param F Function whose jump tables are being placed.
  /// \return True if jump tables for \p F should live in its text section.
  bool shouldPutJumpTableInFunctionSection(bool UsesLabelDifference,
                                           const Function &F) const override;

  /// Select the default GOFF section for \p GO based on its section kind.
  ///
  /// \param GO Global to place.
  /// \param Kind Section kind classifying the global.
  /// \param TM Target machine providing ABI and section options.
  /// \return The default GOFF section for \p GO.
  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

  /// Return the GOFF section named by an explicit section attribute on \p GO.
  ///
  /// \param GO Global with an explicit section name.
  /// \param Kind Section kind classifying the global.
  /// \param TM Target machine providing ABI and section options.
  /// \return The GOFF section named by the explicit section attribute on \p GO.
  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

  /// Return the GOFF LSDA section for exception handling of \p F.
  ///
  /// \param F Function whose LSDA is being placed.
  /// \param FnSym Symbol for the function.
  /// \param TM Target machine providing ABI and section options.
  /// \return The GOFF LSDA section for exception handling of \p F.
  MCSection *getSectionForLSDA(const Function &F, const MCSymbol &FnSym,
                               const TargetMachine &TM) const override;

  /// Return the GOFF section for static constructors or destructors.
  ///
  /// \param Priority Constructor or destructor priority; lower runs earlier.
  /// \return The GOFF section for static constructors or destructors at \p Priority.
  MCSection *getStaticXtorSection(unsigned Priority) const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_TARGETLOWERINGOBJECTFILEIMPL_H
