//===- llvm/CodeGen/AsmPrinter.h - AsmPrinter Framework ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a class to be used as the base class for target specific
// asm writers.  This class primarily handles common functionality used by
// all asm writers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ASMPRINTER_H
#define LLVM_CODEGEN_ASMPRINTER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/StaticDataProfileInfo.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/DwarfStringPoolEntry.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {

/// Maps address-taken basic blocks to the MCSymbols that label them.
class AddrLabelMap;
class AsmPrinterHandler;
class BasicBlock;
class BlockAddress;
class Constant;
class ConstantArray;
class ConstantPtrAuth;
class DataLayout;
class DebugHandlerBase;
class DIE;
class DIEAbbrev;
class DwarfDebug;
/// Emits exception-handling unwind information for an AsmPrinter.
class EHStreamer;
/// Emits GC metadata as assembly code for an AsmPrinter.
class GCMetadataPrinter;
class GCStrategy;
class GlobalAlias;
class GlobalObject;
class GlobalValue;
class GlobalVariable;
class MachineBasicBlock;
/// Abstract base for target-specific constant pool values.
class MachineConstantPoolValue;
class MachineDominatorTree;
class MachineFunction;
class MachineInstr;
class MachineJumpTableInfo;
class MachineLoopInfo;
class MachineModuleInfo;
class MachineOptimizationRemarkEmitter;
class MCAsmInfo;
class MCCFIInstruction;
class MCContext;
class MCExpr;
class MCInst;
class MCSection;
class MCStreamer;
class MCSubtargetInfo;
class MCSymbol;
class MCTargetOptions;
class MDNode;
class Module;
/// Emits pseudo-probe records with embedded inline context for an AsmPrinter.
class PseudoProbeHandler;
class raw_ostream;
class StringRef;
class TargetLoweringObjectFile;
class TargetMachine;
class Twine;

namespace remarks {
class RemarkStreamer;
}

/// This class is intended to be used as a driving class for all asm writers.
class LLVM_ABI AsmPrinter : public MachineFunctionPass {
public:
  /// Target machine description.
  TargetMachine &TM;

  /// Target Asm Printer information.
  const MCAsmInfo &MAI;

  /// This is the context for the output file that we are streaming. This owns
  /// all of the global MC-related objects for the generated translation unit.
  MCContext &OutContext;

  /// MCStreamer for the assembly or object file being generated.
  ///
  /// Holds transient state for the current translation unit (such as the
  /// current section).
  std::unique_ptr<MCStreamer> OutStreamer;

  /// The current machine function.
  MachineFunction *MF = nullptr;

  /// This is a pointer to the current MachineModuleInfo.
  MachineModuleInfo *MMI = nullptr;

  /// This is a pointer to the current MachineDominatorTree.
  MachineDominatorTree *MDT = nullptr;

  /// This is a pointer to the current MachineLoopInfo.
  MachineLoopInfo *MLI = nullptr;

  /// Optimization remark emitter.
  MachineOptimizationRemarkEmitter *ORE = nullptr;

  /// The symbol for the entry in __patchable_function_entires.
  MCSymbol *CurrentPatchableFunctionEntrySym = nullptr;

  /// The symbol for the current function. This is recalculated at the beginning
  /// of each call to runOnMachineFunction().
  MCSymbol *CurrentFnSym = nullptr;

  /// The symbol for the current function descriptor on AIX. This is created
  /// at the beginning of each call to SetupMachineFunction().
  MCSymbol *CurrentFnDescSym = nullptr;

  /// Start symbol used when calculating the current function's size.
  ///
  /// Used for directives such as \c .size. By default this equals
  /// \c CurrentFnSym.
  MCSymbol *CurrentFnSymForSize = nullptr;

  /// End-of-callsite symbols for the current function, keyed by basic block.
  ///
  /// Within each block, callsite symbols are stored in the order they appear.
  DenseMap<const MachineBasicBlock *, SmallVector<MCSymbol *, 1>>
      CurrentFnCallsiteEndSymbols;

  /// Provides the profile information for constants.
  const StaticDataProfileInfo *SDPI = nullptr;

  /// The profile summary information.
  const ProfileSummaryInfo *PSI = nullptr;

  /// Map a basic block section ID to the begin and end symbols of that section
  ///  which determine the section's range.
  struct MBBSectionRange {
    /// Label at the start of the basic-block section.
    MCSymbol *BeginLabel;
    /// Label at the end of the basic-block section.
    MCSymbol *EndLabel;
  };

  /// Begin and end symbols for each basic-block section ID.
  MapVector<MBBSectionID, MBBSectionRange> MBBSectionRanges;

  /// Map global GOT equivalent MCSymbols to GlobalVariables and keep track of
  /// its number of uses by other globals.
  using GOTEquivUsePair = std::pair<const GlobalVariable *, unsigned>;
  /// Maps GOT-equivalent MCSymbols to the GlobalVariable and how often other
  /// globals reference that equivalent.
  MapVector<const MCSymbol *, GOTEquivUsePair> GlobalGOTEquivs;

  /// Which CFI section is required for a function or module.
  enum class CFISection : unsigned {
    None = 0, ///< Do not emit either .eh_frame or .debug_frame
    EH = 1,   ///< Emit .eh_frame
    Debug = 2 ///< Emit .debug_frame
  };

  // Callbacks to get analyses to allow portability between the new and
  // legacy pass managers.
  // TODO(boomanaiden154): Remove these and use a more native solution once
  // we drop support for the legacy PM.
  /// Callback that returns the current MachineModuleInfo.
  std::function<MachineModuleInfo *()> GetMMI;
  /// Callback that returns the optimization remark emitter for a function.
  std::function<MachineOptimizationRemarkEmitter *(MachineFunction &)> GetORE;
  /// Callback to obtain the MachineDominatorTree for a MachineFunction.
  std::function<MachineDominatorTree *(MachineFunction &)> GetMDT;
  /// Callback that returns MachineLoopInfo for a MachineFunction.
  std::function<MachineLoopInfo *(MachineFunction &)> GetMLI;
  /// Callback invoked before emitting GC assembly for a module.
  std::function<void(Module &)> BeginGCAssembly;
  /// Callback invoked after emitting GC assembly for a module.
  std::function<void(Module &)> FinishGCAssembly;
  /// Callback invoked after function emission to write stack map records.
  ///
  /// Delegates to GC metadata printers when available; otherwise serializes
  /// collected stack maps with the default format. Emitted before debug info so
  /// Mach-O and similar targets keep data sections ahead of debug sections.
  std::function<void(Module &)> EmitStackMaps;
  /// Callback that asserts debug and EH handlers have been finalized.
  std::function<void()> AssertDebugEHFinalized;

private:
  MCSymbol *CurrentFnEnd = nullptr;

  /// Map a basic block section ID to the exception symbol associated with that
  /// section. Map entries are assigned and looked up via
  /// AsmPrinter::getMBBExceptionSym.
  DenseMap<MBBSectionID, MCSymbol *> MBBSectionExceptionSyms;

  // The symbol used to represent the start of the current BB section of the
  // function. This is used to calculate the size of the BB section.
  MCSymbol *CurrentSectionBeginSym = nullptr;

  /// This map keeps track of which symbol is being used for the specified basic
  /// block's address of label.
  std::unique_ptr<AddrLabelMap> AddrLabelSymbols;

  /// The garbage collection metadata printer table.
  DenseMap<GCStrategy *, std::unique_ptr<GCMetadataPrinter>> GCMetadataPrinters;

  /// Emit comments in assembly output if this is true.
  bool VerboseAsm;

  /// Store symbols and type identifiers used to create callgraph section
  /// entries related to a function.
  struct FunctionCallGraphInfo {
    /// Numeric type identifier used in callgraph section for indirect calls
    /// and targets.
    using CGTypeId = uint64_t;

    /// Unique target type IDs.
    SmallSetVector<CGTypeId, 4> IndirectCalleeTypeIDs;
    /// Unique direct callees.
    SmallSetVector<MCSymbol *, 4> DirectCallees;
  };

  enum CallGraphSectionFormatVersion : uint8_t {
    V_0 = 0,
  };

  /// Output stream for the stack usage file (i.e., .su file).
  std::unique_ptr<raw_fd_ostream> StackUsageStream;

  /// List of symbols to be inserted into PC sections.
  DenseMap<const MDNode *, SmallVector<const MCSymbol *>> PCSectionsSymbols;

  static char ID;

protected:
  /// Symbol marking the beginning of the current function.
  MCSymbol *CurrentFnBegin = nullptr;

  /// For dso_local functions, the current $local alias for the function.
  MCSymbol *CurrentFnBeginLocal = nullptr;

  /// A handle to the EH info emitter (if present).
  SmallVector<std::unique_ptr<EHStreamer>, 1> EHHandlers;

  /// Debug-info and related AsmPrinter handlers owned by this printer.
  ///
  /// Protected so targets can add their own. This vector maintains ownership
  /// of the emitters.
  SmallVector<std::unique_ptr<AsmPrinterHandler>, 2> Handlers;
  /// Number of user-registered handlers at the front of \c Handlers.
  size_t NumUserHandlers = 0;

  /// Stack map records collected while emitting the current module.
  StackMaps SM;

private:
  /// If generated on the fly this own the instance.
  std::unique_ptr<MachineDominatorTree> OwnedMDT;

  /// If generated on the fly this own the instance.
  std::unique_ptr<MachineLoopInfo> OwnedMLI;

  /// If the target supports dwarf debug info, this pointer is non-null.
  DwarfDebug *DD = nullptr;

  /// A handler that supports pseudo probe emission with embedded inline
  /// context.
  std::unique_ptr<PseudoProbeHandler> PP;

  /// CFISection type the module needs i.e. either .eh_frame or .debug_frame.
  CFISection ModuleCFISection = CFISection::None;

  /// True if the module contains split-stack functions. This is used to
  /// emit .note.GNU-split-stack section as required by the linker for
  /// special handling split-stack function calling no-split-stack function.
  bool HasSplitStack = false;

  /// True if the module contains no-split-stack functions. This is used to emit
  /// .note.GNU-no-split-stack section when it also contains functions without a
  /// split stack prologue.
  bool HasNoSplitStack = false;

  /// True if debugging information is available in this module.
  bool DbgInfoAvailable = false;

protected:
  /// Construct an AsmPrinter for \p TM that writes through \p Streamer.
  ///
  /// \param TM Target machine describing the output architecture.
  /// \param Streamer MCStreamer that receives emitted assembly or object code.
  /// \param ID Pass identifier used by the MachineFunctionPass infrastructure.
  AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer,
             char &ID = AsmPrinter::ID);

  /// Create the DwarfDebug handler. Targets can override this to provide
  /// custom debug information handling.
  ///
  /// \return Newly created DwarfDebug handler for this printer.
  virtual DwarfDebug *createDwarfDebug();

public:
  /// Virtual destructor.
  ~AsmPrinter() override;

  /// Return the DwarfDebug handler, or null if DWARF debug info is disabled.
  ///
  /// \return DwarfDebug handler, or null if DWARF debug info is disabled.
  DwarfDebug *getDwarfDebug() { return DD; }
  /// Return the DwarfDebug handler, or null if DWARF debug info is disabled.
  ///
  /// \return DwarfDebug handler, or null if DWARF debug info is disabled.
  DwarfDebug *getDwarfDebug() const { return DD; }

  /// Return the DWARF version used for debug info emission.
  ///
  /// \return DWARF version number in use.
  uint16_t getDwarfVersion() const;
  /// Set the DWARF version used for debug info emission.
  ///
  /// \param Version DWARF version number to emit.
  void setDwarfVersion(uint16_t Version);

  /// Return true if DWARF64 format is in use.
  ///
  /// \return True if DWARF64 format is in use.
  bool isDwarf64() const;

  /// Returns 4 for DWARF32 and 8 for DWARF64.
  ///
  /// \return Offset field size in bytes for the current DWARF format.
  unsigned int getDwarfOffsetByteSize() const;

  /// Returns 4 for DWARF32 and 12 for DWARF64.
  ///
  /// \return Unit-length field size in bytes for the current DWARF format.
  unsigned int getUnitLengthFieldByteSize() const;

  /// Returns information about the byte size of DW_FORM values.
  ///
  /// \return Form parameters describing DW_FORM value sizes.
  dwarf::FormParams getDwarfFormParams() const;

  /// Return true if the output is being generated for a position-independent
  /// image.
  ///
  /// \return True if generating position-independent output.
  bool isPositionIndependent() const;

  /// Return true if assembly output should contain comments.
  ///
  /// \return True if verbose assembly comments are enabled.
  bool isVerbose() const { return VerboseAsm; }

  /// Return a unique ID for the current function.
  ///
  /// \return Unique identifier for the current function.
  unsigned getFunctionNumber() const;

  /// Return symbol for the function pseudo stack if the stack frame is not a
  /// register based.
  ///
  /// \return Frame symbol for a non-register stack frame, or null.
  virtual const MCSymbol *getFunctionFrameSymbol() const { return nullptr; }

  /// Return the symbol marking the beginning of the current function.
  ///
  /// \return Symbol at the start of the current function.
  MCSymbol *getFunctionBegin() const { return CurrentFnBegin; }
  /// Return the symbol marking the end of the current function.
  ///
  /// \return Symbol at the end of the current function.
  MCSymbol *getFunctionEnd() const { return CurrentFnEnd; }

  /// Return the exception symbol for the MBB section containing \p MBB.
  ///
  /// \param MBB Basic block whose containing section's exception symbol is
  /// requested.
  /// \return Exception symbol for the section containing \p MBB.
  MCSymbol *getMBBExceptionSym(const MachineBasicBlock &MBB);

  /// Return the address-taken symbol for basic block \p BB.
  ///
  /// This cannot be the normal LBB label because the block may be accessed
  /// outside its containing function.
  ///
  /// \param BB Basic block whose address-taken symbol is requested.
  /// \return Address-taken symbol for \p BB.
  MCSymbol *getAddrLabelSymbol(const BasicBlock *BB) {
    return getAddrLabelSymbolToEmit(BB).front();
  }

  /// Return all address-taken symbols that must be emitted for \p BB.
  ///
  /// If other blocks were RAUW'd to this one, their symbols may need to be
  /// emitted as well.
  ///
  /// \param BB Basic block whose address-taken symbols are requested.
  /// \return Address-taken symbols that must be emitted for \p BB.
  ArrayRef<MCSymbol *> getAddrLabelSymbolToEmit(const BasicBlock *BB);

  /// Create a symbol marking the end of a callsite in \p MBB.
  ///
  /// \param MBB Basic block that contains the callsite.
  /// \return Newly created callsite-end symbol.
  MCSymbol *createCallsiteEndSymbol(const MachineBasicBlock &MBB);

  /// Collect symbols for deleted address-taken blocks in \p F.
  ///
  /// If references to address-taken blocks were generated but those blocks
  /// were deleted, return the symbols so they can still be emitted. This
  /// prevents emitting a reference to a symbol that has no definition.
  ///
  /// \param F Function whose deleted address-taken block symbols are requested.
  /// \param Result Output vector that receives the symbols to emit.
  void takeDeletedSymbolsForFunction(const Function *F,
                                     std::vector<MCSymbol *> &Result);

  /// Return information about object file lowering.
  ///
  /// \return Object-file lowering helpers for the current target.
  const TargetLoweringObjectFile &getObjFileLowering() const;

  /// Return information about data layout.
  ///
  /// \return Data layout for the current module.
  const DataLayout &getDataLayout() const;

  /// Return the pointer size from the TargetMachine
  ///
  /// \return Pointer size in bytes.
  unsigned getPointerSize() const;

  /// Return information about subtarget.
  ///
  /// \return Subtarget info for the current machine function.
  const MCSubtargetInfo &getSubtargetInfo() const;

  /// Emit machine instruction \p Inst to streamer \p S.
  ///
  /// \param S Streamer that receives the lowered instruction.
  /// \param Inst Machine instruction to emit.
  void EmitToStreamer(MCStreamer &S, const MCInst &Inst);

  /// Return the current section we are emitting to.
  ///
  /// \return Current output section, or null if none.
  const MCSection *getCurrentSection() const;

  /// Fill \p Name with the mangled name of \p GV, including any target prefix.
  ///
  /// \param Name Output buffer that receives the mangled name.
  /// \param GV Global value whose mangled name is requested.
  void getNameWithPrefix(SmallVectorImpl<char> &Name,
                         const GlobalValue *GV) const;

  /// Return the MCSymbol for global value \p GV.
  ///
  /// \param GV Global value whose symbol is requested.
  /// \return Symbol for \p GV.
  MCSymbol *getSymbol(const GlobalValue *GV) const;

  /// Return a symbol preferred for references to \p GV.
  ///
  /// Similar to \c getSymbol(), but preferred for references. On ELF, this
  /// uses a local symbol if a reference to \p GV is guaranteed to resolve to
  /// the definition in the same module.
  ///
  /// \param GV Global value being referenced.
  /// \return Preferred symbol for references to \p GV.
  MCSymbol *getSymbolPreferLocal(const GlobalValue &GV) const;

  /// Return true if DWARF emission uses cross-section relocations.
  ///
  /// \return True if DWARF uses relocations across sections.
  bool doesDwarfUseRelocationsAcrossSections() const {
    return DwarfUsesRelocationsAcrossSections;
  }

  /// Set whether DWARF emission uses cross-section relocations.
  ///
  /// \param Enable True to use relocations across sections.
  void setDwarfUsesRelocationsAcrossSections(bool Enable) {
    DwarfUsesRelocationsAcrossSections = Enable;
  }

  /// Return a section suffix (hot or unlikely) for \p C when profiles exist.
  ///
  /// Returns an empty string when profile data is unavailable.
  ///
  /// \param C Constant whose section suffix is requested.
  /// \return Section suffix for \p C, or an empty string if none.
  StringRef getConstantSectionSuffix(const Constant *C) const;

  /// Record call-graph section info for call instruction \p MI.
  ///
  /// If \p MI is an indirect call, add expected type IDs to the indirect type
  /// ID list. If \p MI is a direct call, add the callee symbol to the direct
  /// callsites list of \p FuncCGInfo.
  ///
  /// \param FuncCGInfo Call-graph info for the current function to update.
  /// \param CallSitesInfoMap Map of additional call-site info for the function.
  /// \param MI Call instruction being processed.
  void handleCallsiteForCallgraph(
      FunctionCallGraphInfo &FuncCGInfo,
      const MachineFunction::CallSiteInfoMap &CallSitesInfoMap,
      const MachineInstr &MI);

  //===------------------------------------------------------------------===//
  // XRay instrumentation implementation.
  //===------------------------------------------------------------------===//
public:
  /// Kind of XRay instrumentation sled recorded in the XRay table.
  enum class SledKind : uint8_t {
    FUNCTION_ENTER = 0, ///< Function-entry sled.
    FUNCTION_EXIT = 1,  ///< Function-exit sled.
    TAIL_CALL = 2,      ///< Tail-call sled.
    LOG_ARGS_ENTER = 3, ///< Function-entry sled that also logs arguments.
    CUSTOM_EVENT = 4,   ///< Custom event sled.
    TYPED_EVENT = 5,    ///< Typed event sled.
  };

  /// Describes one XRay sled and the function that contains it.
  ///
  /// Table entries point to the sled, the function containing the sled, the
  /// sled kind, whether the sled should always be instrumented, and a version
  /// identifier the runtime can use to interpret the sled.
  struct XRayFunctionEntry {
    /// MCSymbol for the XRay sled instrumentation point in the object file.
    const MCSymbol *Sled;
    /// Symbol for the function that contains this sled.
    const MCSymbol *Function;
    /// Kind of XRay sled this entry describes.
    SledKind Kind;
    /// True if the XRay runtime should always instrument this sled.
    bool AlwaysInstrument;
    /// IR function associated with this sled entry.
    const class Function *Fn;
    /// Version identifier for the sled format.
    uint8_t Version;

    /// Emit this XRay sled entry to streamer \p Out.
    ///
    /// \param Bytes Number of bytes occupied by the sled sequence.
    /// \param Out Streamer that receives the sled entry.
    LLVM_ABI void emit(int Bytes, MCStreamer *Out) const;
  };

  /// XRay sleds collected for the current module.
  SmallVector<XRayFunctionEntry, 4> Sleds;

  /// Record XRay sled \p Sled associated with instruction \p MI.
  ///
  /// \param Sled Symbol marking the sled instrumentation point.
  /// \param MI Machine instruction that produced the sled.
  /// \param Kind Kind of XRay sled being recorded.
  /// \param Version Version identifier for the sled format.
  void recordSled(MCSymbol *Sled, const MachineInstr &MI, SledKind Kind,
                  uint8_t Version = 0);

  /// Emit a table with all XRay instrumentation points.
  void emitXRayTable();

  /// Emit entries for functions marked as patchable.
  void emitPatchableFunctionEntries();

  //===------------------------------------------------------------------===//
  // MachineFunctionPass Implementation.
  //===------------------------------------------------------------------===//

  /// Record analysis usage.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Set up the AsmPrinter when working on a new module.
  ///
  /// If your pass overrides this, it must explicitly call this implementation.
  ///
  /// \param M Module being initialized.
  /// \return True if the module was modified.
  bool doInitialization(Module &M) override;

  /// Shut down the AsmPrinter after finishing a module.
  ///
  /// If you override this in your pass, you must call it explicitly.
  ///
  /// \param M Module being finalized.
  /// \return True if the module was modified.
  bool doFinalization(Module &M) override;

  /// Emit function \p MF to the OutStreamer.
  ///
  /// \param MF Machine function to emit.
  /// \return True if the function was modified; always false here.
  bool runOnMachineFunction(MachineFunction &MF) override {
    SetupMachineFunction(MF);
    emitFunctionBody();
    return false;
  }

  //===------------------------------------------------------------------===//
  // Coarse grained IR lowering routines.
  //===------------------------------------------------------------------===//

  /// Prepare the AsmPrinter for MachineFunction \p MF.
  ///
  /// Called when a new MachineFunction is processed from
  /// \c runOnMachineFunction.
  ///
  /// \param MF Machine function about to be emitted.
  virtual void SetupMachineFunction(MachineFunction &MF);

  /// This method emits the body and trailer for a function.
  void emitFunctionBody();

  /// Emit the CFI instruction described by \p MI.
  ///
  /// \param MI Machine instruction carrying CFI operands.
  void emitCFIInstruction(const MachineInstr &MI);

  /// Emit a frame allocation label for instruction \p MI.
  ///
  /// \param MI FRAME_ALLOC machine instruction to emit.
  void emitFrameAlloc(const MachineInstr &MI);

  /// Emit the stack-size section entry for function \p MF.
  ///
  /// \param MF Machine function whose stack size is recorded.
  void emitStackSizeSection(const MachineFunction &MF);

  /// Emit stack usage information for function \p MF.
  ///
  /// \param MF Machine function whose stack usage is recorded.
  void emitStackUsage(const MachineFunction &MF);

  /// Emit the basic-block address map section for function \p MF.
  ///
  /// \param MF Machine function whose BB address map is emitted.
  void emitBBAddrMapSection(const MachineFunction &MF);

  /// Emit a KCFI trap entry for \p Symbol in function \p MF.
  ///
  /// \param MF Machine function that owns the trap entry.
  /// \param Symbol Symbol associated with the KCFI trap.
  void emitKCFITrapEntry(const MachineFunction &MF, const MCSymbol *Symbol);
  /// Emit the KCFI type identifier for function \p MF.
  ///
  /// \param MF Machine function whose KCFI type id is emitted.
  virtual void emitKCFITypeId(const MachineFunction &MF);

  /// Emit the call-graph section for function \p MF.
  ///
  /// \param MF Machine function whose call-graph info is emitted.
  /// \param FuncCGInfo Call-graph info collected for \p MF.
  void emitCallGraphSection(const MachineFunction &MF,
                            FunctionCallGraphInfo &FuncCGInfo);

  /// Emit a prefetch-target symbol for \p BBID and \p CallsiteIndex.
  ///
  /// The symbol is emitted as a label and its linkage is set from the
  /// function's linkage.
  ///
  /// \param BBID Basic-block identifier for the prefetch target.
  /// \param CallsiteIndex Callsite index associated with the prefetch target.
  void emitPrefetchTargetSymbol(const UniqueBBID &BBID, unsigned CallsiteIndex);

  /// Emit prefetch targets that were not mapped to any basic block. These
  /// targets are emitted at the beginning of the function body.
  void emitDanglingPrefetchTargets();

  /// Emit the pseudo-probe described by instruction \p MI.
  ///
  /// \param MI Pseudo-probe machine instruction to emit.
  void emitPseudoProbe(const MachineInstr &MI);

  /// Emit the remarks section using remark streamer \p RS.
  ///
  /// \param RS Remark streamer that supplies remarks to serialize.
  void emitRemarksSection(remarks::RemarkStreamer &RS);

  /// Emit a label used as a PC-sections reference.
  ///
  /// \param MF Machine function that owns the label.
  /// \param MD Metadata node describing the PC section.
  void emitPCSectionsLabel(const MachineFunction &MF, const MDNode &MD);

  /// Emit the PC sections collected from instructions in \p MF.
  ///
  /// \param MF Machine function whose PC sections are emitted.
  void emitPCSections(const MachineFunction &MF);

  /// Get the CFISection type for function \p F.
  ///
  /// \param F IR function whose CFI section type is requested.
  /// \return CFI section type for \p F.
  CFISection getFunctionCFISectionType(const Function &F) const;

  /// Get the CFISection type for machine function \p MF.
  ///
  /// \param MF Machine function whose CFI section type is requested.
  /// \return CFI section type for \p MF.
  CFISection getFunctionCFISectionType(const MachineFunction &MF) const;

  /// Get the CFISection type for the module.
  ///
  /// \return Module-wide CFI section type.
  CFISection getModuleCFISectionType() const { return ModuleCFISection; }

  /// Returns true if valid debug info is present.
  ///
  /// \return True if valid debug info is present.
  bool hasDebugInfo() const { return DbgInfoAvailable; }

  /// Return true if Windows SEH move information must be emitted for the
  /// current function.
  ///
  /// \return True if SEH move information must be emitted.
  bool needsSEHMoves();

  /// Return true if CFI is used even when exception handling is disabled.
  ///
  /// Emitting CFI unwind information is entangled with exception support.
  /// This returns true for platforms that still use CFI for other purposes
  /// (debugging, sanitizers, ...) when
  /// \c MCAsmInfo::ExceptionsType == ExceptionHandling::None.
  ///
  /// \return True if CFI is used without exception handling.
  bool usesCFIWithoutEH() const;

  /// Emit assembly for constants in the current function's constant pool.
  ///
  /// Used to print constants that the code generator has spilled to memory.
  virtual void emitConstantPool();

  /// Print assembly representations of the jump tables used by the current
  /// function to the current output stream.
  virtual void emitJumpTableInfo();

  /// Emit global variable \p GV to the assembly output.
  ///
  /// \param GV Global variable to emit.
  virtual void emitGlobalVariable(const GlobalVariable *GV);

  /// Emit global variable \p GV with an explicit alignment granule.
  ///
  /// The granule is applied to both the address and the allocation size.
  ///
  /// \param GV Global variable to emit.
  /// \param AlignmentGranule Optional minimum alignment applied to address and
  /// size.
  virtual void emitGlobalVariable(const GlobalVariable *GV,
                                  MaybeAlign AlignmentGranule);

  /// Emit \p GV if it is a special LLVM global, otherwise do nothing.
  ///
  /// \param GV Global variable to inspect.
  /// \return True if \p GV was recognized and emitted.
  bool emitSpecialLLVMGlobal(const GlobalVariable *GV);

  /// One entry in an \c llvm.global_ctors or \c llvm.global_dtors array.
  struct Structor {
    /// Initialization or termination priority.
    int Priority = 0;
    /// Global initialization or clean-up function.
    Constant *Func = nullptr;
    /// Associated comdat key, if any.
    GlobalValue *ComdatKey = nullptr;

    /// Construct a default-initialized structor entry.
    Structor() = default;
  };

  /// Gather structors from \p List and sort them by priority.
  ///
  /// \param DL Data layout used while interpreting the list.
  /// \param List Initializer of \c llvm.global_ctors or \c llvm.global_dtors.
  /// \param[out] Structors Sorted Structor entries by Priority.
  void preprocessXXStructorList(const DataLayout &DL, const Constant *List,
                                SmallVector<Structor, 8> &Structors);

  /// Emit an \c llvm.global_ctors or \c llvm.global_dtors list.
  ///
  /// \param DL Data layout used while emitting the list.
  /// \param List Initializer of the ctor or dtor array.
  /// \param IsCtor True when emitting constructors; false for destructors.
  virtual void emitXXStructorList(const DataLayout &DL, const Constant *List,
                                  bool IsCtor);

  /// Emit an alignment directive to a power-of-two boundary.
  ///
  /// If \p GV is specified and has an explicit alignment, that alignment
  /// overrides \p Alignment when required for correctness. Returns the
  /// effective alignment that was emitted (which may exceed \p Alignment when
  /// \p GV has a stricter explicit alignment).
  ///
  /// \param Alignment Requested alignment boundary.
  /// \param GV Optional global whose explicit alignment may override the
  /// request.
  /// \param MaxBytesToEmit Maximum padding bytes the directive may emit.
  /// \return Effective alignment that was emitted.
  Align emitAlignment(Align Alignment, const GlobalObject *GV = nullptr,
                      unsigned MaxBytesToEmit = 0) const;

  /// Lower LLVM constant \p CV to an MCExpr.
  ///
  /// When \p BaseCV is present, lowers the element at \p BaseCV plus \p Offset.
  ///
  /// \param CV Constant to lower.
  /// \param BaseCV Optional base constant when lowering a sub-element.
  /// \param Offset Byte offset from \p BaseCV when lowering a sub-element.
  /// \return MCExpr representing the lowered constant.
  virtual const MCExpr *lowerConstant(const Constant *CV,
                                      const Constant *BaseCV = nullptr,
                                      uint64_t Offset = 0);

  /// Map from offset within a global to aliases that refer to that offset.
  using AliasMapTy = DenseMap<uint64_t, SmallVector<const GlobalAlias *, 1>>;
  /// Print LLVM constant \p CV to the assembly output.
  ///
  /// On AIX, when an alias refers to a sub-element of a global variable, the
  /// label of that alias needs to be emitted before the corresponding element.
  ///
  /// \param DL Data layout used while emitting the constant.
  /// \param CV Constant to emit.
  /// \param AliasList Optional map of aliases that must be emitted at offsets.
  void emitGlobalConstant(const DataLayout &DL, const Constant *CV,
                          AliasMapTy *AliasList = nullptr);

  /// Find GOT-equivalent globals in module \p M and record them.
  ///
  /// Unnamed constant globals that solely contain a pointer to another global
  /// act like a proxy, or GOT equivalent: they only hold the address of the
  /// final global. Accesses to these proxies can be replaced with the GOT
  /// entry for that final global. This selects candidates among module
  /// globals, avoids emitting them unnecessarily, and later replaces
  /// references with PC-relative GOT accesses.
  ///
  /// \param M Module whose globals are scanned for GOT equivalents.
  void computeGlobalGOTEquivs(Module &M);

  /// Emit GOT-equivalent proxies that could not be optimized away.
  ///
  /// Constant expressions using GOT-equivalent globals may not be eligible for
  /// PC-relative GOT entry conversion; in those cases emit the proxies
  /// previously omitted in \c emitGlobalVariable.
  void emitGlobalGOTEquivs();

  //===------------------------------------------------------------------===//
  // Overridable Hooks
  //===------------------------------------------------------------------===//

  /// Register an additional AsmPrinter handler.
  ///
  /// \param Handler Handler to take ownership of and invoke during emission.
  void addAsmPrinterHandler(std::unique_ptr<AsmPrinterHandler> Handler);

  // Targets can, or in the case of EmitInstruction, must implement these to
  // customize output.

  /// Emit target-specific content at the start of the assembly file.
  ///
  /// \param M Module being emitted.
  virtual void emitStartOfAsmFile(Module &M) {}

  /// Emit target-specific content at the end of the assembly file.
  ///
  /// \param M Module being emitted.
  virtual void emitEndOfAsmFile(Module &M) {}

  /// Targets can override this to emit stuff before the first basic block in
  /// the function.
  virtual void emitFunctionBodyStart() {}

  /// Targets can override this to emit stuff after the last basic block in the
  /// function.
  virtual void emitFunctionBodyEnd() {}

  /// Emit content at the start of basic block \p MBB.
  ///
  /// By default this prints the label for \p MBB, an alignment if present, and
  /// a comment describing it when appropriate.
  ///
  /// \param MBB Basic block whose prologue is being emitted.
  virtual void emitBasicBlockStart(const MachineBasicBlock &MBB);

  /// Emit content at the end of basic block \p MBB.
  ///
  /// \param MBB Basic block whose epilogue is being emitted.
  virtual void emitBasicBlockEnd(const MachineBasicBlock &MBB);

  /// Emit machine instruction \p MI.
  ///
  /// Targets must override this.
  ///
  /// \param MI Instruction to emit.
  virtual void emitInstruction(const MachineInstr *MI) {
    llvm_unreachable("EmitInstruction not implemented");
  }

  /// Return the symbol for constant-pool entry \p CPID.
  ///
  /// \param CPID Constant-pool entry index.
  /// \return Symbol for the constant-pool entry.
  virtual MCSymbol *GetCPISymbol(unsigned CPID) const;

  /// Emit the entry label for the current function.
  virtual void emitFunctionEntryLabel();

  /// Emit the function descriptor for the current function.
  virtual void emitFunctionDescriptor() {
    llvm_unreachable("Function descriptor is target-specific.");
  }

  /// Emit a target-specific machine constant pool value.
  ///
  /// \param MCPV Constant-pool value to emit.
  virtual void emitMachineConstantPoolValue(MachineConstantPoolValue *MCPV);

  /// Emit a global constant that participates in a ctor/dtor list.
  ///
  /// \param DL Data layout used while emitting the constant.
  /// \param CV Constant to emit.
  virtual void emitXXStructor(const DataLayout &DL, const Constant *CV) {
    emitGlobalConstant(DL, CV);
  }

  /// Lower pointer-authentication constant \p CPA to an MCExpr.
  ///
  /// \param CPA Pointer-authentication constant to lower.
  /// \return MCExpr representing the lowered pointer-authentication constant.
  virtual const MCExpr *lowerConstantPtrAuth(const ConstantPtrAuth &CPA) {
    reportFatalUsageError("ptrauth constant lowering not implemented");
  }

  /// Lower block-address constant \p BA to an MCExpr.
  ///
  /// \param BA Block address to lower.
  /// \return MCExpr representing the lowered block address.
  virtual const MCExpr *lowerBlockAddressConstant(const BlockAddress &BA);

  /// Return true if \p MBB is reachable only by fall-through from one pred.
  ///
  /// \param MBB Basic block to test.
  /// \return True if \p MBB is reachable only by fall-through from one
  /// predecessor.
  virtual bool
  isBlockOnlyReachableByFallthrough(const MachineBasicBlock *MBB) const;

  /// Emit an IMPLICIT_DEF instruction in verbose assembly mode.
  ///
  /// \param MI IMPLICIT_DEF instruction to describe.
  virtual void emitImplicitDef(const MachineInstr *MI) const;

  /// Return subtarget info for Mach-O IFunc lowering.
  ///
  /// \c getSubtargetInfo() cannot be used here because there is no
  /// MachineFunction when lowering a GlobalIFunc, and \c getSubtargetInfo
  /// requires one. Override in targets that support Mach-O IFunc lowering.
  ///
  /// \return Subtarget info for IFunc lowering, or null if unsupported.
  virtual const MCSubtargetInfo *getIFuncMCSubtargetInfo() const {
    return nullptr;
  }

  /// Emit the Mach-O IFunc stub body for \p GI.
  ///
  /// \param M Module being emitted.
  /// \param GI GlobalIFunc being lowered.
  /// \param LazyPointer Symbol for the lazy pointer used by the stub.
  virtual void emitMachOIFuncStubBody(Module &M, const GlobalIFunc &GI,
                                      MCSymbol *LazyPointer) {
    llvm_unreachable(
        "Mach-O IFunc lowering is not yet supported on this target");
  }

  /// Emit the Mach-O IFunc stub helper body for \p GI.
  ///
  /// \param M Module being emitted.
  /// \param GI GlobalIFunc being lowered.
  /// \param LazyPointer Symbol for the lazy pointer used by the helper.
  virtual void emitMachOIFuncStubHelperBody(Module &M, const GlobalIFunc &GI,
                                            MCSymbol *LazyPointer) {
    llvm_unreachable(
        "Mach-O IFunc lowering is not yet supported on this target");
  }

  /// Emit \p N NOP instructions.
  ///
  /// \param N Number of NOP instructions to emit.
  void emitNops(unsigned N);

  //===------------------------------------------------------------------===//
  // Symbol Lowering Routines.
  //===------------------------------------------------------------------===//

  /// Create a temporary MCSymbol with name \p Name.
  ///
  /// \param Name Base name for the temporary symbol.
  /// \return Newly created temporary symbol.
  MCSymbol *createTempSymbol(const Twine &Name) const;

  /// Return a private MCSymbol based on \p GV with suffix \p Suffix.
  ///
  /// \param GV Global value providing the base name.
  /// \param Suffix Suffix appended to the mangled base name.
  /// \return Private symbol derived from \p GV with \p Suffix.
  MCSymbol *getSymbolWithGlobalValueBase(const GlobalValue *GV,
                                         StringRef Suffix) const;

  /// Return the MCSymbol for external symbol name \p Sym.
  ///
  /// \param Sym External symbol name.
  /// \return Symbol for the external name \p Sym.
  MCSymbol *GetExternalSymbolSymbol(const Twine &Sym) const;

  /// Return the symbol for jump-table entry \p JTID.
  ///
  /// \param JTID Jump-table identifier.
  /// \param isLinkerPrivate True to request a linker-private symbol.
  /// \return Symbol for the jump-table entry.
  MCSymbol *GetJTISymbol(unsigned JTID, bool isLinkerPrivate = false) const;

  /// Return the symbol for the specified jump-table \c .set directive.
  ///
  /// FIXME: privatize to AsmPrinter.
  ///
  /// \param UID Unique identifier for the jump-table set.
  /// \param MBBID Basic-block identifier associated with the set entry.
  /// \return Symbol for the jump-table \c .set directive.
  MCSymbol *GetJTSetSymbol(unsigned UID, unsigned MBBID) const;

  /// Return the MCSymbol used for BlockAddress references to \p BA.
  ///
  /// \param BA Block address whose symbol is requested.
  /// \return Symbol used for BlockAddress references to \p BA.
  MCSymbol *GetBlockAddressSymbol(const BlockAddress *BA) const;
  /// Return the MCSymbol used for BlockAddress references to \p BB.
  ///
  /// \param BB Basic block whose address symbol is requested.
  /// \return Symbol used for BlockAddress references to \p BB.
  MCSymbol *GetBlockAddressSymbol(const BasicBlock *BB) const;

  //===------------------------------------------------------------------===//
  // Emission Helper Routines.
  //===------------------------------------------------------------------===//

  /// Print \p Offset to stream \p OS.
  ///
  /// \param Offset Byte offset to print.
  /// \param OS Output stream.
  void printOffset(int64_t Offset, raw_ostream &OS) const;

  /// Emit a byte directive with value \p Value.
  ///
  /// \param Value 8-bit integer to emit.
  void emitInt8(int Value) const;

  /// Emit a short directive with value \p Value.
  ///
  /// \param Value 16-bit integer to emit.
  void emitInt16(int Value) const;

  /// Emit a long directive with value \p Value.
  ///
  /// \param Value 32-bit integer to emit.
  void emitInt32(int Value) const;

  /// Emit a long-long directive with value \p Value.
  ///
  /// \param Value 64-bit integer to emit.
  void emitInt64(uint64_t Value) const;

  /// Emit signed LEB128 value \p Value.
  ///
  /// \param Value Signed value to encode.
  /// \param Desc Optional verbose-assembly comment describing the value.
  void emitSLEB128(int64_t Value, const char *Desc = nullptr) const;

  /// Emit unsigned LEB128 value \p Value.
  ///
  /// \param Value Unsigned value to encode.
  /// \param Desc Optional verbose-assembly comment describing the value.
  /// \param PadTo Optional minimum encoded width in bytes.
  void emitULEB128(uint64_t Value, const char *Desc = nullptr,
                   unsigned PadTo = 0) const;

  /// Emit a sized difference between labels \p Hi and \p Lo.
  ///
  /// Emits something like \c .long Hi-Lo. Uses \c .set when available.
  ///
  /// \param Hi Minuend label.
  /// \param Lo Subtrahend label.
  /// \param Size Size in bytes of the emitted directive.
  void emitLabelDifference(const MCSymbol *Hi, const MCSymbol *Lo,
                           unsigned Size) const;

  /// Emit a ULEB128 difference between labels \p Hi and \p Lo.
  ///
  /// Emits something like \c .uleb128 Hi-Lo.
  ///
  /// \param Hi Minuend label.
  /// \param Lo Subtrahend label.
  void emitLabelDifferenceAsULEB128(const MCSymbol *Hi,
                                    const MCSymbol *Lo) const;

  /// Emit a sized reference to \p Label plus \p Offset.
  ///
  /// Emits something like \c .long Label+Offset. Uses \c .set when available.
  ///
  /// \param Label Base label.
  /// \param Offset Byte offset added to \p Label.
  /// \param Size Size in bytes of the emitted directive.
  /// \param IsSectionRelative True to emit a section-relative reference.
  void emitLabelPlusOffset(const MCSymbol *Label, uint64_t Offset,
                           unsigned Size, bool IsSectionRelative = false) const;

  /// Emit a sized reference to \p Label.
  ///
  /// Emits something like \c .long Label.
  ///
  /// \param Label Label to reference.
  /// \param Size Size in bytes of the emitted directive.
  /// \param IsSectionRelative True to emit a section-relative reference.
  void emitLabelReference(const MCSymbol *Label, unsigned Size,
                          bool IsSectionRelative = false) const {
    emitLabelPlusOffset(Label, 0, Size, IsSectionRelative);
  }

  //===------------------------------------------------------------------===//
  // Dwarf Emission Helper Routines
  //===------------------------------------------------------------------===//

  /// Emit a \c .byte directive for encoding value \p Val.
  ///
  /// When verbose assembly is enabled, comments describe the encoding.
  ///
  /// \param Val Encoding byte to emit.
  /// \param Desc Optional description of what the encoding specifies (e.g.
  /// "LSDA").
  void emitEncodingByte(unsigned Val, const char *Desc = nullptr) const;

  /// Return the size in bytes of DWARF encoding \p Encoding.
  ///
  /// \param Encoding DWARF pointer/offset encoding to measure.
  /// \return Size in bytes of a value with the given encoding.
  unsigned GetSizeOfEncodedValue(unsigned Encoding) const;

  /// Emit a ttype reference to global \p GV with encoding \p Encoding.
  ///
  /// \param GV Global value referenced by the type info entry.
  /// \param Encoding DWARF encoding used for the reference.
  virtual void emitTTypeReference(const GlobalValue *GV, unsigned Encoding);

  /// Emit a DWARF reference to symbol \p Label.
  ///
  /// Different object formats represent this differently: some use a
  /// relocation; others encode the label offset within its section.
  ///
  /// \param Label Symbol being referenced.
  /// \param ForceOffset True to force an offset encoding instead of a
  /// relocation.
  void emitDwarfSymbolReference(const MCSymbol *Label,
                                bool ForceOffset = false) const;

  /// Emit the section-relative offset of string-pool entry \p S.
  ///
  /// When possible, emit a DwarfStringPool section offset without relocations
  /// and without using the symbol. Otherwise, defers to
  /// \a emitDwarfSymbolReference(). The emitted width depends on the DWARF
  /// format.
  ///
  /// \param S String-pool entry whose offset is emitted.
  void emitDwarfStringOffset(DwarfStringPoolEntry S) const;

  /// Emit the section-relative offset of string-pool entry \p S.
  ///
  /// \param S String-pool entry reference whose offset is emitted.
  void emitDwarfStringOffset(DwarfStringPoolEntryRef S) const {
    emitDwarfStringOffset(S.getEntry());
  }

  /// Emit DWARF offset \p Label + \p Offset.
  ///
  /// Emits \c .long or \c .quad depending on the DWARF format.
  ///
  /// \param Label Base label.
  /// \param Offset Byte offset added to \p Label.
  void emitDwarfOffset(const MCSymbol *Label, uint64_t Offset) const;

  /// Emit a 32- or 64-bit DWARF length/offset value.
  ///
  /// \param Value Length or offset to emit.
  void emitDwarfLengthOrOffset(uint64_t Value) const;

  /// Emit a DWARF unit-length field with absolute length \p Length.
  ///
  /// Chooses DWARF32 or DWARF64 according to the current settings.
  ///
  /// \param Length Unit length to emit.
  /// \param Comment Assembly comment associated with the field.
  void emitDwarfUnitLength(uint64_t Length, const Twine &Comment) const;

  /// Emit a DWARF unit-length field and return its end symbol.
  ///
  /// Chooses DWARF32 or DWARF64 according to the current settings. The caller
  /// must emit the returned end symbol.
  ///
  /// \param Prefix Prefix used when creating the end symbol.
  /// \param Comment Assembly comment associated with the field.
  /// \return End symbol that the caller must emit after the unit contents.
  MCSymbol *emitDwarfUnitLength(const Twine &Prefix,
                                const Twine &Comment) const;

  /// Emit a call-site offset between \p Hi and \p Lo with \p Encoding.
  ///
  /// \param Hi High label of the call-site range.
  /// \param Lo Low label of the call-site range.
  /// \param Encoding DWARF encoding used for the offset.
  void emitCallSiteOffset(const MCSymbol *Hi, const MCSymbol *Lo,
                          unsigned Encoding) const;
  /// Emit call-site integer \p Value with encoding \p Encoding.
  ///
  /// \param Value Integer payload for the call site.
  /// \param Encoding DWARF encoding used for the value.
  void emitCallSiteValue(uint64_t Value, unsigned Encoding) const;

  /// Get the value for DW_AT_APPLE_isa. Zero if no isa encoding specified.
  ///
  /// \return DW_AT_APPLE_isa encoding value, or zero if none is specified.
  virtual unsigned getISAEncoding() { return 0; }

  /// Emit a debug value expression of \p Size bytes.
  ///
  /// \param Value Expression value to emit.
  /// \param Size Size of the integer in bytes.
  virtual void emitDebugValue(const MCExpr *Value, unsigned Size) const;

  //===------------------------------------------------------------------===//
  // Dwarf Lowering Routines
  //===------------------------------------------------------------------===//

  /// Emit frame instruction \p Inst describing the frame layout.
  ///
  /// \param Inst CFI instruction to emit.
  void emitCFIInstruction(const MCCFIInstruction &Inst) const;

  /// Emit the DWARF abbreviation table in \p Abbrevs.
  ///
  /// \param Abbrevs Sequence of abbreviation entries to emit.
  template <typename T> void emitDwarfAbbrevs(const T &Abbrevs) const {
    // For each abbreviation.
    for (const auto &Abbrev : Abbrevs)
      emitDwarfAbbrev(*Abbrev);

    // Mark end of abbreviations.
    emitULEB128(0, "EOM(3)");
  }

  /// Emit one DWARF abbreviation entry to the output stream.
  ///
  /// \param Abbrev Abbreviation entry to emit.
  void emitDwarfAbbrev(const DIEAbbrev &Abbrev) const;

  /// Recursively emit the DWARF DIE tree rooted at \p Die.
  ///
  /// \param Die Root DIE to emit.
  void emitDwarfDIE(const DIE &Die) const;

  //===------------------------------------------------------------------===//
  // CodeView Helper Routines
  //===------------------------------------------------------------------===//

  /// Get CodeView jump-table debug info for jump table \p JTI.
  ///
  /// Return value is <Base Address, Base Offset, Branch Address, Entry Size>.
  ///
  /// \param JTI Jump-table index.
  /// \param BranchInstr Branch instruction that uses the jump table.
  /// \param BranchLabel Label at the branch site.
  /// \return Tuple of base address, base offset, branch address, and entry
  /// size.
  virtual std::tuple<const MCSymbol *, uint64_t, const MCSymbol *,
                     codeview::JumpTableEntrySize>
  getCodeViewJumpTableInfo(int JTI, const MachineInstr *BranchInstr,
                           const MCSymbol *BranchLabel) const;

  //===------------------------------------------------------------------===//
  // COFF Helper Routines
  //===------------------------------------------------------------------===//

  /// Emit COFF symbols/data for loader-replaceable functions in \p M.
  ///
  /// \param M Module whose replaceable functions are emitted.
  void emitCOFFReplaceableFunctionData(Module &M);

  /// Emit the COFF \c @feat.00 symbol for features enabled in \p M.
  ///
  /// \param M Module whose feature symbol is emitted.
  void emitCOFFFeatureSymbol(Module &M);

  //===------------------------------------------------------------------===//
  // Inline Asm Support
  //===------------------------------------------------------------------===//

  // These are hooks that targets can override to implement inline asm
  // support.  These should probably be moved out of AsmPrinter someday.

  /// Print special inline-asm code \p Code for instruction \p MI.
  ///
  /// Useful for portably encoding the comment character or other
  /// target-specific knowledge into asm strings via syntax such as
  /// \c ${:comment}. Targets can override this to support their own codes.
  ///
  /// \param MI Instruction that owns the inline asm string.
  /// \param OS Output stream.
  /// \param Code Special code name following the \c ${:..} syntax.
  virtual void PrintSpecial(const MachineInstr *MI, raw_ostream &OS,
                            StringRef Code) const;

  /// Print machine operand \p MO as a symbol to \p OS.
  ///
  /// Targets with complex symbol-reference handling should override the base
  /// implementation.
  ///
  /// \param MO Operand to print as a symbol.
  /// \param OS Output stream.
  virtual void PrintSymbolOperand(const MachineOperand &MO, raw_ostream &OS);

  /// Print INLINEASM operand \p OpNo of \p MI to \p OS.
  ///
  /// Targets should override this to format operands appropriately.
  ///
  /// \param MI INLINEASM instruction being printed.
  /// \param OpNo Operand index within \p MI.
  /// \param ExtraCode Optional modifier characters from the asm string.
  /// \param OS Output stream.
  /// \return True if the operand is erroneous.
  virtual bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                               const char *ExtraCode, raw_ostream &OS);

  /// Print INLINEASM memory operand \p OpNo of \p MI to \p OS.
  ///
  /// Targets should override this to format addresses appropriately.
  ///
  /// \param MI INLINEASM instruction being printed.
  /// \param OpNo Operand index within \p MI.
  /// \param ExtraCode Optional modifier characters from the asm string.
  /// \param OS Output stream.
  /// \return True if the operand is erroneous.
  virtual bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                                     const char *ExtraCode, raw_ostream &OS);

  /// Let the target prepare state before emitting inline assembly.
  virtual void emitInlineAsmStart() const;

  /// Let the target restore state after emitting inline assembly.
  ///
  /// Useful when inline asm contains directives that switch modes.
  ///
  /// \param StartInfo Original subtarget info before inline asm.
  /// \param EndInfo Final subtarget info after parsing inline asm, or null if
  /// unknown.
  /// \param MI INLINEASM instruction that was emitted.
  virtual void emitInlineAsmEnd(const MCSubtargetInfo &StartInfo,
                                const MCSubtargetInfo *EndInfo,
                                const MachineInstr *MI);

  /// Push directives enabling instructions allowed by \p STI.
  ///
  /// Emits directives for features enabled by \p STI that are not enabled by
  /// the global subtarget (\c TM.getSubTargetInfo()).
  ///
  /// \param STI Subtarget whose features should be temporarily enabled.
  /// \return True if any directives were emitted.
  virtual bool emitTargetFeaturePush(const MCSubtargetInfo &STI) {
    return false;
  }

  /// Pop target-feature directives previously pushed for \p STI.
  ///
  /// \param STI Subtarget previously passed to \c emitTargetFeaturePush.
  /// \param DidPush Result of the matching \c emitTargetFeaturePush call.
  virtual void emitTargetFeaturePop(const MCSubtargetInfo &STI, bool DidPush) {}

  /// Emit visibility information for symbol \p Sym when supported.
  ///
  /// \param Sym Symbol whose visibility is emitted.
  /// \param Visibility Visibility kind to apply.
  /// \param IsDefinition True if \p Sym is a definition rather than a
  /// declaration.
  void emitVisibility(MCSymbol *Sym, unsigned Visibility,
                      bool IsDefinition = true) const;

  /// Emit linkage information for \p GVSym based on \p GV when supported.
  ///
  /// \param GV Global value that determines the linkage.
  /// \param GVSym Symbol that receives the linkage directive.
  virtual void emitLinkage(const GlobalValue *GV, MCSymbol *GVSym) const;

  /// Return the alignment to use for global object \p GV.
  ///
  /// \param GV Global object whose alignment is requested.
  /// \param DL Data layout consulted for the ABI alignment.
  /// \param InAlign Minimum alignment to respect.
  /// \return Effective alignment for \p GV, at least \p InAlign.
  static Align getGVAlignment(const GlobalObject *GV, const DataLayout &DL,
                              Align InAlign = Align(1));

private:
  /// Private state for PrintSpecial()
  // Assign a unique ID to this machine instruction.
  mutable const MachineInstr *LastMI = nullptr;
  mutable unsigned LastFn = 0;
  mutable unsigned Counter = ~0U;

  bool DwarfUsesRelocationsAcrossSections = false;

  /// This method emits the header for the current function.
  virtual void emitFunctionHeader();

  /// This method emits a comment next to header for the current function.
  virtual void emitFunctionHeaderComment();

  /// This method emits prefix-like data before the current function.
  void emitFunctionPrefix(ArrayRef<const Constant *> Prefix);

  /// Emit a blob of inline asm to the output streamer.
  virtual void
  emitInlineAsm(StringRef Str, const MCSubtargetInfo &STI,
                const MCTargetOptions &MCOptions,
                const MDNode *LocMDNode = nullptr,
                InlineAsm::AsmDialect AsmDialect = InlineAsm::AD_ATT,
                const MachineInstr *MI = nullptr);

  /// This method formats and emits the specified machine instruction that is an
  /// inline asm.
  void emitInlineAsm(const MachineInstr *MI);

  /// Add inline assembly info to the diagnostics machinery, so we can
  /// emit file and position info. Returns SrcMgr memory buffer position.
  unsigned addInlineAsmDiagBuffer(StringRef AsmStr,
                                  const MDNode *LocMDNode) const;

  //===------------------------------------------------------------------===//
  // Internal Implementation Details
  //===------------------------------------------------------------------===//

  virtual void emitJumpTableImpl(const MachineJumpTableInfo &MJTI,
                                 ArrayRef<unsigned> JumpTableIndices);

  void emitJumpTableSizesSection(const MachineJumpTableInfo &MJTI,
                                 const Function &F) const;

  void emitLLVMUsedList(const ConstantArray *InitList);
  /// Emit llvm.ident metadata in an '.ident' directive.
  void emitModuleIdents(Module &M);
  /// Emit bytes for llvm.commandline metadata.
  virtual void emitModuleCommandLines(Module &M);

  GCMetadataPrinter *getOrCreateGCPrinter(GCStrategy &S);
  virtual void emitGlobalIFunc(Module &M, const GlobalIFunc &GI);

  /// This method decides whether the specified basic block requires a label.
  bool shouldEmitLabelForBasicBlock(const MachineBasicBlock &MBB) const;

protected:
  /// Emit one jump-table entry for destination \p MBB.
  ///
  /// \param MJTI Jump-table info for the current function.
  /// \param MBB Destination basic block for this entry.
  /// \param uid Jump-table identifier for the entry being emitted.
  virtual void emitJumpTableEntry(const MachineJumpTableInfo &MJTI,
                                  const MachineBasicBlock *MBB,
                                  unsigned uid) const;
  /// Emit global alias \p GA from module \p M.
  ///
  /// \param M Module that contains the alias.
  /// \param GA Alias to emit.
  virtual void emitGlobalAlias(const Module &M, const GlobalAlias &GA);
  /// Return true if weak Swift async extended frame-pointer flags are needed.
  ///
  /// \return True if weak Swift async extended frame-pointer flags are needed.
  virtual bool shouldEmitWeakSwiftAsyncExtendedFramePointerFlags() const {
    return false;
  }

  /// Return an optional minimum alignment granule for global \p GV.
  ///
  /// Applies to both the address and the allocation size. Used on systems such
  /// as CHERI and MTE that require globals to be padded to a minimum
  /// alignment.
  ///
  /// \param GV Global variable whose alignment granule is requested.
  /// \return Minimum alignment granule for \p GV, or \c std::nullopt if none.
  virtual MaybeAlign
  getRequiredGlobalAlignmentGranule(const GlobalVariable &GV) {
    return std::nullopt;
  };
};

/// Configure \p AsmPrinter for module-level emission of \p M.
///
/// \param M Module being prepared for assembly emission.
/// \param MAM Module analysis manager supplying analyses.
/// \param AsmPrinter AsmPrinter instance to configure.
LLVM_ABI void setupModuleAsmPrinter(Module &M, ModuleAnalysisManager &MAM,
                                    AsmPrinter &AsmPrinter);

/// Configure \p AsmPrinter for machine-function emission of \p MF.
///
/// \param MFAM Machine-function analysis manager supplying analyses.
/// \param MF Machine function about to be emitted.
/// \param AsmPrinter AsmPrinter instance to configure.
LLVM_ABI void
setupMachineFunctionAsmPrinter(MachineFunctionAnalysisManager &MFAM,
                               MachineFunction &MF, AsmPrinter &AsmPrinter);

} // end namespace llvm

#endif // LLVM_CODEGEN_ASMPRINTER_H
