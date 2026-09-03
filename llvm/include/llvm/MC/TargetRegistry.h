//===- MC/TargetRegistry.h - Target Registration ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file exposes the TargetRegistry interface, which tools can use to access
// the appropriate target specific classes (TargetMachine, AsmPrinter, etc.)
// which have been registered.
//
// Target specific class implementations should register themselves using the
// appropriate TargetRegistry interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_TARGETREGISTRY_H
#define LLVM_MC_TARGETREGISTRY_H

#include "llvm-c/DisassemblerTypes.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <string>

namespace llvm {

class AsmPrinter;
class MCAsmBackend;
class MCAsmInfo;
class MCAsmParser;
class MCCodeEmitter;
class MCContext;
class MCDisassembler;
class MCInstPrinter;
class MCInstrAnalysis;
class MCInstrInfo;
class MCLFIRewriter;
class MCObjectWriter;
class MCRegisterInfo;
class MCRelocationInfo;
class MCStreamer;
class MCSubtargetInfo;
class MCSymbolizer;
class MCTargetAsmParser;
class MCTargetOptions;
class MCTargetStreamer;
class raw_ostream;
class TargetMachine;
class TargetOptions;
/// Machine code analyzer (llvm-mca) customization hooks.
namespace mca {
class CustomBehaviour;
class InstrPostProcess;
class InstrumentManager;
struct SourceMgr;
} // namespace mca

/// Create a no-op streamer that discards all output.
///
/// \param Ctx The machine code context.
/// \return A null streamer that discards all output.
LLVM_ABI MCStreamer *createNullStreamer(MCContext &Ctx);
// Takes ownership of \p TAB and \p CE.

/// Create a machine code streamer which will print out assembly for the native
/// target, suitable for compiling with a native assembler.
///
/// \param Ctx The machine code context.
/// \param OS The formatted output stream. Takes ownership.
/// \param InstPrint - If given, the instruction printer to use. If not given
/// the MCInst representation will be printed.  This method takes ownership of
/// InstPrint.
///
/// \param CE - If given, a code emitter to use to show the instruction
/// encoding inline with the assembly. This method takes ownership of \p CE.
///
/// \param TAB - If given, a target asm backend to use to show the fixup
/// information in conjunction with encoding information. This method takes
/// ownership of \p TAB.
/// \return A new assembly MCStreamer.
LLVM_ABI MCStreamer *
createAsmStreamer(MCContext &Ctx, std::unique_ptr<formatted_raw_ostream> OS,
                  std::unique_ptr<MCInstPrinter> InstPrint,
                  std::unique_ptr<MCCodeEmitter> CE,
                  std::unique_ptr<MCAsmBackend> TAB);

/// Create an ELF object streamer.
///
/// \param Ctx The machine code context.
/// \param TAB The target assembler backend. Takes ownership.
/// \param OW The object writer. Takes ownership.
/// \param CE The code emitter. Takes ownership.
/// \return A new ELF object streamer.
LLVM_ABI MCStreamer *createELFStreamer(MCContext &Ctx,
                                       std::unique_ptr<MCAsmBackend> &&TAB,
                                       std::unique_ptr<MCObjectWriter> &&OW,
                                       std::unique_ptr<MCCodeEmitter> &&CE);
/// Create a GOFF object streamer.
///
/// \param Ctx The machine code context.
/// \param TAB The target assembler backend. Takes ownership.
/// \param OW The object writer. Takes ownership.
/// \param CE The code emitter. Takes ownership.
/// \return A new GOFF object streamer.
LLVM_ABI MCStreamer *createGOFFStreamer(MCContext &Ctx,
                                        std::unique_ptr<MCAsmBackend> &&TAB,
                                        std::unique_ptr<MCObjectWriter> &&OW,
                                        std::unique_ptr<MCCodeEmitter> &&CE);
/// Create a Mach-O object streamer.
///
/// \param Ctx The machine code context.
/// \param TAB The target assembler backend. Takes ownership.
/// \param OW The object writer. Takes ownership.
/// \param CE The code emitter. Takes ownership.
/// \param DWARFMustBeAtTheEnd Whether DWARF sections must be emitted last.
/// \param LabelSections Whether to emit labels for each section.
/// \return A new Mach-O object streamer.
LLVM_ABI MCStreamer *createMachOStreamer(MCContext &Ctx,
                                         std::unique_ptr<MCAsmBackend> &&TAB,
                                         std::unique_ptr<MCObjectWriter> &&OW,
                                         std::unique_ptr<MCCodeEmitter> &&CE,
                                         bool DWARFMustBeAtTheEnd,
                                         bool LabelSections = false);
/// Create a WebAssembly object streamer.
///
/// \param Ctx The machine code context.
/// \param TAB The target assembler backend. Takes ownership.
/// \param OW The object writer. Takes ownership.
/// \param CE The code emitter. Takes ownership.
/// \return A new WebAssembly object streamer.
LLVM_ABI MCStreamer *createWasmStreamer(MCContext &Ctx,
                                        std::unique_ptr<MCAsmBackend> &&TAB,
                                        std::unique_ptr<MCObjectWriter> &&OW,
                                        std::unique_ptr<MCCodeEmitter> &&CE);
/// Create a SPIR-V object streamer.
///
/// \param Ctx The machine code context.
/// \param TAB The target assembler backend. Takes ownership.
/// \param OW The object writer. Takes ownership.
/// \param CE The code emitter. Takes ownership.
/// \return A new SPIR-V object streamer.
LLVM_ABI MCStreamer *createSPIRVStreamer(MCContext &Ctx,
                                         std::unique_ptr<MCAsmBackend> &&TAB,
                                         std::unique_ptr<MCObjectWriter> &&OW,
                                         std::unique_ptr<MCCodeEmitter> &&CE);
/// Create a DXContainer object streamer.
///
/// \param Ctx The machine code context.
/// \param TAB The target assembler backend. Takes ownership.
/// \param OW The object writer. Takes ownership.
/// \param CE The code emitter. Takes ownership.
/// \return A new DXContainer object streamer.
LLVM_ABI MCStreamer *
createDXContainerStreamer(MCContext &Ctx, std::unique_ptr<MCAsmBackend> &&TAB,
                          std::unique_ptr<MCObjectWriter> &&OW,
                          std::unique_ptr<MCCodeEmitter> &&CE);

/// Create a default MCRelocationInfo for the given triple.
///
/// \param TT The target triple.
/// \param Ctx The machine code context.
/// \return A new default MCRelocationInfo.
LLVM_ABI MCRelocationInfo *createMCRelocationInfo(const Triple &TT,
                                                  MCContext &Ctx);

/// Create a default MCSymbolizer for the given triple.
///
/// \param TT The target triple.
/// \param GetOpInfo Callback to get symbolic operand information.
/// \param SymbolLookUp Callback to look up a symbol name.
/// \param DisInfo Opaque pointer passed to the callbacks.
/// \param Ctx The machine code context.
/// \param RelInfo Relocation information. Takes ownership.
/// \return A new default MCSymbolizer.
LLVM_ABI MCSymbolizer *
createMCSymbolizer(const Triple &TT, LLVMOpInfoCallback GetOpInfo,
                   LLVMSymbolLookupCallback SymbolLookUp, void *DisInfo,
                   MCContext *Ctx, std::unique_ptr<MCRelocationInfo> &&RelInfo);

/// Create a default CustomBehaviour for llvm-mca.
///
/// \param STI The subtarget information.
/// \param SrcMgr The MCA source manager.
/// \param MCII The instruction info.
/// \return A new default CustomBehaviour.
LLVM_ABI mca::CustomBehaviour *
createCustomBehaviour(const MCSubtargetInfo &STI, const mca::SourceMgr &SrcMgr,
                      const MCInstrInfo &MCII);

/// Create a default InstrPostProcess for llvm-mca.
///
/// \param STI The subtarget information.
/// \param MCII The instruction info.
/// \return A new default InstrPostProcess.
LLVM_ABI mca::InstrPostProcess *
createInstrPostProcess(const MCSubtargetInfo &STI, const MCInstrInfo &MCII);

/// Create a default InstrumentManager for llvm-mca.
///
/// \param STI The subtarget information.
/// \param MCII The instruction info.
/// \return A new default InstrumentManager.
LLVM_ABI mca::InstrumentManager *
createInstrumentManager(const MCSubtargetInfo &STI, const MCInstrInfo &MCII);

/// Target - Wrapper for Target specific information.
///
/// For registration purposes, this is a POD type so that targets can be
/// registered without the use of static constructors.
///
/// Targets should implement a single global instance of this class (which
/// will be zero initialized), and pass that instance to the TargetRegistry as
/// part of their initialization.
class Target {
public:
  friend struct TargetRegistry;

  /// Function type that checks whether an architecture is supported.
  using ArchMatchFnTy = bool (*)(Triple::ArchType Arch);

  /// Constructor function type for this target's MCAsmInfo.
  using MCAsmInfoCtorFnTy = MCAsmInfo *(*)(const MCRegisterInfo &MRI,
                                           const Triple &TT,
                                           const MCTargetOptions &Options);
  /// Constructor function type for this target's MCObjectFileInfo.
  using MCObjectFileInfoCtorFnTy = MCObjectFileInfo *(*)(MCContext &Ctx,
                                                         bool PIC,
                                                         bool LargeCodeModel);
  /// Constructor function type for this target's MCInstrInfo.
  using MCInstrInfoCtorFnTy = MCInstrInfo *(*)();
  /// Constructor function type for this target's MCInstrAnalysis.
  using MCInstrAnalysisCtorFnTy = MCInstrAnalysis *(*)(const MCInstrInfo *Info);
  /// Constructor function type for this target's MCRegisterInfo.
  using MCRegInfoCtorFnTy = MCRegisterInfo *(*)(const Triple &TT);
  /// Constructor function type for this target's MCSubtargetInfo.
  using MCSubtargetInfoCtorFnTy = MCSubtargetInfo *(*)(const Triple &TT,
                                                       StringRef CPU,
                                                       StringRef Features);
  /// Constructor function type for this target's TargetMachine.
  using TargetMachineCtorTy = TargetMachine
      *(*)(const Target &T, const Triple &TT, StringRef CPU, StringRef Features,
           const TargetOptions &Options, std::optional<Reloc::Model> RM,
           std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT);
  // If it weren't for layering issues (this header is in llvm/Support, but
  // depends on MC?) this should take the Streamer by value rather than rvalue
  // reference.
  /// Constructor function type for this target's AsmPrinter.
  using AsmPrinterCtorTy = AsmPrinter *(*)(
      TargetMachine &TM, std::unique_ptr<MCStreamer> &&Streamer);
  /// Constructor function type for this target's MCAsmBackend.
  using MCAsmBackendCtorTy = MCAsmBackend *(*)(const Target &T,
                                               const MCSubtargetInfo &STI,
                                               const MCRegisterInfo &MRI,
                                               const MCTargetOptions &Options);
  /// Constructor function type for this target's MCTargetAsmParser.
  using MCAsmParserCtorTy = MCTargetAsmParser *(*)(const MCSubtargetInfo &STI,
                                                   MCAsmParser &P,
                                                   const MCInstrInfo &MII);
  /// Constructor function type for this target's MCDisassembler.
  using MCDisassemblerCtorTy = MCDisassembler *(*)(const Target &T,
                                                   const MCSubtargetInfo &STI,
                                                   MCContext &Ctx);
  /// Constructor function type for this target's MCInstPrinter.
  using MCInstPrinterCtorTy = MCInstPrinter *(*)(const Triple &T,
                                                 unsigned SyntaxVariant,
                                                 const MCAsmInfo &MAI,
                                                 const MCInstrInfo &MII,
                                                 const MCRegisterInfo &MRI);
  /// Constructor function type for this target's MCCodeEmitter.
  using MCCodeEmitterCtorTy = MCCodeEmitter *(*)(const MCInstrInfo &II,
                                                 MCContext &Ctx);
  /// Constructor function type for this target's ELF MCStreamer.
  using ELFStreamerCtorTy =
      MCStreamer *(*)(const Triple &T, MCContext &Ctx,
                      std::unique_ptr<MCAsmBackend> &&TAB,
                      std::unique_ptr<MCObjectWriter> &&OW,
                      std::unique_ptr<MCCodeEmitter> &&Emitter);
  /// Constructor function type for this target's Mach-O MCStreamer.
  using MachOStreamerCtorTy =
      MCStreamer *(*)(MCContext &Ctx, std::unique_ptr<MCAsmBackend> &&TAB,
                      std::unique_ptr<MCObjectWriter> &&OW,
                      std::unique_ptr<MCCodeEmitter> &&Emitter);
  /// Constructor function type for this target's COFF MCStreamer.
  using COFFStreamerCtorTy =
      MCStreamer *(*)(MCContext &Ctx, std::unique_ptr<MCAsmBackend> &&TAB,
                      std::unique_ptr<MCObjectWriter> &&OW,
                      std::unique_ptr<MCCodeEmitter> &&Emitter);
  /// Constructor function type for this target's XCOFF MCStreamer.
  using XCOFFStreamerCtorTy =
      MCStreamer *(*)(const Triple &T, MCContext &Ctx,
                      std::unique_ptr<MCAsmBackend> &&TAB,
                      std::unique_ptr<MCObjectWriter> &&OW,
                      std::unique_ptr<MCCodeEmitter> &&Emitter);

  /// Constructor function type for this target's null MCTargetStreamer.
  using NullTargetStreamerCtorTy = MCTargetStreamer *(*)(MCStreamer &S);
  /// Constructor function type for this target's assembly MCTargetStreamer.
  using AsmTargetStreamerCtorTy =
      MCTargetStreamer *(*)(MCStreamer &S, formatted_raw_ostream &OS,
                            MCInstPrinter *InstPrint);
  /// Constructor function type for this target's assembly MCStreamer.
  using AsmStreamerCtorTy = MCStreamer
      *(*)(MCContext & Ctx, std::unique_ptr<formatted_raw_ostream> OS,
           std::unique_ptr<MCInstPrinter> IP, std::unique_ptr<MCCodeEmitter> CE,
           std::unique_ptr<MCAsmBackend> TAB);
  /// Constructor function type for this target's object MCTargetStreamer.
  using ObjectTargetStreamerCtorTy =
      MCTargetStreamer *(*)(MCStreamer &S, const MCSubtargetInfo &STI);
  /// Constructor function type for this target's MCRelocationInfo.
  using MCRelocationInfoCtorTy = MCRelocationInfo *(*)(const Triple &TT,
                                                       MCContext &Ctx);
  /// Constructor function type for this target's MCSymbolizer.
  using MCSymbolizerCtorTy =
      MCSymbolizer *(*)(const Triple &TT, LLVMOpInfoCallback GetOpInfo,
                        LLVMSymbolLookupCallback SymbolLookUp, void *DisInfo,
                        MCContext *Ctx,
                        std::unique_ptr<MCRelocationInfo> &&RelInfo);

  /// Constructor function type for this target's MCA CustomBehaviour.
  using CustomBehaviourCtorTy =
      mca::CustomBehaviour *(*)(const MCSubtargetInfo &STI,
                                const mca::SourceMgr &SrcMgr,
                                const MCInstrInfo &MCII);

  /// Constructor function type for this target's MCA InstrPostProcess.
  using InstrPostProcessCtorTy =
      mca::InstrPostProcess *(*)(const MCSubtargetInfo &STI,
                                 const MCInstrInfo &MCII);

  /// Constructor function type for this target's MCA InstrumentManager.
  using InstrumentManagerCtorTy =
      mca::InstrumentManager *(*)(const MCSubtargetInfo &STI,
                                  const MCInstrInfo &MCII);

  /// Constructor function type for this target's MCLFIRewriter.
  using MCLFIRewriterCtorTy =
      MCLFIRewriter *(*)(MCContext & Ctx,
                         std::unique_ptr<MCRegisterInfo> &&RegInfo,
                         std::unique_ptr<MCInstrInfo> &&InstInfo);

private:
  /// Next - The next registered target in the linked list, maintained by the
  /// TargetRegistry.
  Target *Next;

  /// The target function for checking if an architecture is supported.
  ArchMatchFnTy ArchMatchFn;

  /// Name - The target name.
  const char *Name;

  /// ShortDesc - A short description of the target.
  const char *ShortDesc;

  /// BackendName - The name of the backend implementation. This must match the
  /// name of the 'def X : Target ...' in TableGen.
  const char *BackendName;

  /// HasJIT - Whether this target supports the JIT.
  bool HasJIT;

  /// MCAsmInfoCtorFn - Constructor function for this target's MCAsmInfo, if
  /// registered.
  MCAsmInfoCtorFnTy MCAsmInfoCtorFn;

  /// Constructor function for this target's MCObjectFileInfo, if registered.
  MCObjectFileInfoCtorFnTy MCObjectFileInfoCtorFn;

  /// MCInstrInfoCtorFn - Constructor function for this target's MCInstrInfo,
  /// if registered.
  MCInstrInfoCtorFnTy MCInstrInfoCtorFn;

  /// MCInstrAnalysisCtorFn - Constructor function for this target's
  /// MCInstrAnalysis, if registered.
  MCInstrAnalysisCtorFnTy MCInstrAnalysisCtorFn;

  /// MCRegInfoCtorFn - Constructor function for this target's MCRegisterInfo,
  /// if registered.
  MCRegInfoCtorFnTy MCRegInfoCtorFn;

  /// MCSubtargetInfoCtorFn - Constructor function for this target's
  /// MCSubtargetInfo, if registered.
  MCSubtargetInfoCtorFnTy MCSubtargetInfoCtorFn;

  /// TargetMachineCtorFn - Construction function for this target's
  /// TargetMachine, if registered.
  TargetMachineCtorTy TargetMachineCtorFn;

  /// MCAsmBackendCtorFn - Construction function for this target's
  /// MCAsmBackend, if registered.
  MCAsmBackendCtorTy MCAsmBackendCtorFn;

  /// MCAsmParserCtorFn - Construction function for this target's
  /// MCTargetAsmParser, if registered.
  MCAsmParserCtorTy MCAsmParserCtorFn;

  /// AsmPrinterCtorFn - Construction function for this target's AsmPrinter,
  /// if registered.
  AsmPrinterCtorTy AsmPrinterCtorFn;

  /// MCDisassemblerCtorFn - Construction function for this target's
  /// MCDisassembler, if registered.
  MCDisassemblerCtorTy MCDisassemblerCtorFn;

  /// MCInstPrinterCtorFn - Construction function for this target's
  /// MCInstPrinter, if registered.
  MCInstPrinterCtorTy MCInstPrinterCtorFn;

  /// MCCodeEmitterCtorFn - Construction function for this target's
  /// CodeEmitter, if registered.
  MCCodeEmitterCtorTy MCCodeEmitterCtorFn;

  // Construction functions for the various object formats, if registered.
  COFFStreamerCtorTy COFFStreamerCtorFn = nullptr;
  MachOStreamerCtorTy MachOStreamerCtorFn = nullptr;
  ELFStreamerCtorTy ELFStreamerCtorFn = nullptr;
  XCOFFStreamerCtorTy XCOFFStreamerCtorFn = nullptr;

  /// Construction function for this target's null TargetStreamer, if
  /// registered (default = nullptr).
  NullTargetStreamerCtorTy NullTargetStreamerCtorFn = nullptr;

  /// Construction function for this target's asm TargetStreamer, if
  /// registered (default = nullptr).
  AsmTargetStreamerCtorTy AsmTargetStreamerCtorFn = nullptr;

  /// Construction function for this target's AsmStreamer, if
  /// registered (default = nullptr).
  AsmStreamerCtorTy AsmStreamerCtorFn = nullptr;

  /// Construction function for this target's obj TargetStreamer, if
  /// registered (default = nullptr).
  ObjectTargetStreamerCtorTy ObjectTargetStreamerCtorFn = nullptr;

  /// MCRelocationInfoCtorFn - Construction function for this target's
  /// MCRelocationInfo, if registered (default = llvm::createMCRelocationInfo)
  MCRelocationInfoCtorTy MCRelocationInfoCtorFn = nullptr;

  /// MCSymbolizerCtorFn - Construction function for this target's
  /// MCSymbolizer, if registered (default = llvm::createMCSymbolizer)
  MCSymbolizerCtorTy MCSymbolizerCtorFn = nullptr;

  /// CustomBehaviourCtorFn - Construction function for this target's
  /// CustomBehaviour, if registered (default = nullptr).
  CustomBehaviourCtorTy CustomBehaviourCtorFn = nullptr;

  /// InstrPostProcessCtorFn - Construction function for this target's
  /// InstrPostProcess, if registered (default = nullptr).
  InstrPostProcessCtorTy InstrPostProcessCtorFn = nullptr;

  /// InstrumentManagerCtorFn - Construction function for this target's
  /// InstrumentManager, if registered (default = nullptr).
  InstrumentManagerCtorTy InstrumentManagerCtorFn = nullptr;

  // MCLFIRewriterCtorFn - Construction function for this target's
  // MCLFIRewriter, if registered (default = nullptr).
  MCLFIRewriterCtorTy MCLFIRewriterCtorFn = nullptr;

public:
  /// Construct an empty, zero-initialized target entry.
  Target() = default;

  /// @name Target Information
  /// @{

  /// Return the next registered target in the linked list.
  ///
  /// \return The next Target, or nullptr at the end of the list.
  const Target *getNext() const { return Next; }

  /// getName - Get the target name.
  ///
  /// \return The registered name of this target.
  const char *getName() const { return Name; }

  /// getShortDescription - Get a short description of the target.
  ///
  /// \return A short human-readable description of the target.
  const char *getShortDescription() const { return ShortDesc; }

  /// getBackendName - Get the backend name.
  ///
  /// \return The backend name matching TableGen.
  const char *getBackendName() const { return BackendName; }

  /// @}
  /// @name Feature Predicates
  /// @{

  /// hasJIT - Check if this targets supports the just-in-time compilation.
  ///
  /// \return True if this target supports JIT compilation.
  bool hasJIT() const { return HasJIT; }

  /// hasTargetMachine - Check if this target supports code generation.
  ///
  /// \return True if a TargetMachine constructor is registered.
  bool hasTargetMachine() const { return TargetMachineCtorFn != nullptr; }

  /// hasMCAsmBackend - Check if this target supports .o generation.
  ///
  /// \return True if an MCAsmBackend constructor is registered.
  bool hasMCAsmBackend() const { return MCAsmBackendCtorFn != nullptr; }

  /// hasMCAsmParser - Check if this target supports assembly parsing.
  ///
  /// \return True if an assembly parser constructor is registered.
  bool hasMCAsmParser() const { return MCAsmParserCtorFn != nullptr; }

  /// @}
  /// @name Feature Constructors
  /// @{

  /// Create a MCAsmInfo implementation for the specified
  /// target triple.
  ///
  /// \param MRI The register information for the target.
  /// \param TheTriple This argument is used to determine the target machine
  /// feature set; it should always be provided. Generally this should be
  /// either the target triple from the module, or the target triple of the
  /// host if that does not exist.
  /// \param Options Target-specific assembly options.
  /// \return A new MCAsmInfo, or nullptr if none is registered.
  MCAsmInfo *createMCAsmInfo(const MCRegisterInfo &MRI, const Triple &TheTriple,
                             const MCTargetOptions &Options) const {
    if (!MCAsmInfoCtorFn)
      return nullptr;
    return MCAsmInfoCtorFn(MRI, TheTriple, Options);
  }

  /// Create a MCObjectFileInfo implementation for the specified target
  /// triple.
  ///
  /// \param Ctx The machine code context.
  /// \param PIC Whether position-independent code is requested.
  /// \param LargeCodeModel Whether the large code model is in use.
  /// \return A new MCObjectFileInfo for the target.
  MCObjectFileInfo *createMCObjectFileInfo(MCContext &Ctx, bool PIC,
                                           bool LargeCodeModel = false) const {
    if (!MCObjectFileInfoCtorFn) {
      MCObjectFileInfo *MOFI = new MCObjectFileInfo();
      MOFI->initMCObjectFileInfo(Ctx, PIC, LargeCodeModel);
      return MOFI;
    }
    return MCObjectFileInfoCtorFn(Ctx, PIC, LargeCodeModel);
  }

  /// createMCInstrInfo - Create a MCInstrInfo implementation.
  ///
  /// \return A new MCInstrInfo, or nullptr if none is registered.
  MCInstrInfo *createMCInstrInfo() const {
    if (!MCInstrInfoCtorFn)
      return nullptr;
    return MCInstrInfoCtorFn();
  }

  /// createMCInstrAnalysis - Create a MCInstrAnalysis implementation.
  ///
  /// \param Info The instruction information to analyze.
  /// \return A new MCInstrAnalysis, or nullptr if none is registered.
  MCInstrAnalysis *createMCInstrAnalysis(const MCInstrInfo *Info) const {
    if (!MCInstrAnalysisCtorFn)
      return nullptr;
    return MCInstrAnalysisCtorFn(Info);
  }

  /// Create a MCRegisterInfo implementation.
  ///
  /// \param TT The target triple.
  /// \return A new MCRegisterInfo, or nullptr if none is registered.
  MCRegisterInfo *createMCRegInfo(const Triple &TT) const {
    if (!MCRegInfoCtorFn)
      return nullptr;
    return MCRegInfoCtorFn(TT);
  }

  /// createMCSubtargetInfo - Create a MCSubtargetInfo implementation.
  ///
  /// \param TheTriple This argument is used to determine the target machine
  /// feature set; it should always be provided. Generally this should be
  /// either the target triple from the module, or the target triple of the
  /// host if that does not exist.
  /// \param CPU This specifies the name of the target CPU.
  /// \param Features This specifies the string representation of the
  /// additional target features.
  /// \return A new MCSubtargetInfo, or nullptr if unavailable or Features is
  /// invalid.
  MCSubtargetInfo *createMCSubtargetInfo(const Triple &TheTriple, StringRef CPU,
                                         StringRef Features) const {
    if (!MCSubtargetInfoCtorFn)
      return nullptr;
    if (!isValidFeatureListFormat(Features))
      return nullptr;
    return MCSubtargetInfoCtorFn(TheTriple, CPU, Features);
  }

  /// createTargetMachine - Create a target specific machine implementation
  /// for the specified \p Triple.
  ///
  /// \param TT This argument is used to determine the target machine
  /// feature set; it should always be provided. Generally this should be
  /// either the target triple from the module, or the target triple of the
  /// host if that does not exist.
  /// \param CPU The name of the target CPU.
  /// \param Features The string representation of additional target features.
  /// \param Options Target-independent code generation options.
  /// \param RM Optional relocation model; nullopt selects the default.
  /// \param CM Optional code model; nullopt selects the default.
  /// \param OL The optimization level for code generation.
  /// \param JIT Whether the machine is being created for JIT compilation.
  /// \return A new TargetMachine, or nullptr if none is registered.
  TargetMachine *createTargetMachine(
      const Triple &TT, StringRef CPU, StringRef Features,
      const TargetOptions &Options, std::optional<Reloc::Model> RM,
      std::optional<CodeModel::Model> CM = std::nullopt,
      CodeGenOptLevel OL = CodeGenOptLevel::Default, bool JIT = false) const {
    if (!TargetMachineCtorFn)
      return nullptr;
    return TargetMachineCtorFn(*this, TT, CPU, Features, Options, RM, CM, OL,
                               JIT);
  }

  /// createMCAsmBackend - Create a target specific assembly backend.
  ///
  /// \param STI The subtarget information.
  /// \param MRI The register information.
  /// \param Options Target-specific assembly options.
  /// \return A new MCAsmBackend, or nullptr if none is registered.
  MCAsmBackend *createMCAsmBackend(const MCSubtargetInfo &STI,
                                   const MCRegisterInfo &MRI,
                                   const MCTargetOptions &Options) const {
    if (!MCAsmBackendCtorFn)
      return nullptr;
    return MCAsmBackendCtorFn(*this, STI, MRI, Options);
  }

  /// createMCAsmParser - Create a target specific assembly parser.
  ///
  /// \param STI The subtarget information.
  /// \param Parser The target independent parser implementation to use for
  /// parsing and lexing.
  /// \param MII The instruction information.
  /// \return A new MCTargetAsmParser, or nullptr if none is registered.
  MCTargetAsmParser *createMCAsmParser(const MCSubtargetInfo &STI,
                                       MCAsmParser &Parser,
                                       const MCInstrInfo &MII) const {
    if (!MCAsmParserCtorFn)
      return nullptr;
    return MCAsmParserCtorFn(STI, Parser, MII);
  }

  /// createAsmPrinter - Create a target specific assembly printer pass.  This
  /// takes ownership of the MCStreamer object.
  ///
  /// \param TM The target machine.
  /// \param Streamer The machine code streamer. Takes ownership.
  /// \return A new AsmPrinter, or nullptr if none is registered.
  AsmPrinter *createAsmPrinter(TargetMachine &TM,
                               std::unique_ptr<MCStreamer> &&Streamer) const {
    if (!AsmPrinterCtorFn)
      return nullptr;
    return AsmPrinterCtorFn(TM, std::move(Streamer));
  }

  /// Create a target-specific MCDisassembler.
  ///
  /// \param STI The subtarget information.
  /// \param Ctx The machine code context.
  /// \return A new MCDisassembler, or nullptr if none is registered.
  MCDisassembler *createMCDisassembler(const MCSubtargetInfo &STI,
                                       MCContext &Ctx) const {
    if (!MCDisassemblerCtorFn)
      return nullptr;
    return MCDisassemblerCtorFn(*this, STI, Ctx);
  }

  /// Create a target-specific MCInstPrinter.
  ///
  /// \param T The target triple.
  /// \param SyntaxVariant The assembly syntax variant to use.
  /// \param MAI The assembly information.
  /// \param MII The instruction information.
  /// \param MRI The register information.
  /// \return A new MCInstPrinter, or nullptr if none is registered.
  MCInstPrinter *createMCInstPrinter(const Triple &T, unsigned SyntaxVariant,
                                     const MCAsmInfo &MAI,
                                     const MCInstrInfo &MII,
                                     const MCRegisterInfo &MRI) const {
    if (!MCInstPrinterCtorFn)
      return nullptr;
    return MCInstPrinterCtorFn(T, SyntaxVariant, MAI, MII, MRI);
  }

  /// createMCCodeEmitter - Create a target specific code emitter.
  ///
  /// \param II The instruction information.
  /// \param Ctx The machine code context.
  /// \return A new MCCodeEmitter, or nullptr if none is registered.
  MCCodeEmitter *createMCCodeEmitter(const MCInstrInfo &II,
                                     MCContext &Ctx) const {
    if (!MCCodeEmitterCtorFn)
      return nullptr;
    return MCCodeEmitterCtorFn(II, Ctx);
  }

  /// Create a target specific MCStreamer.
  ///
  /// \param T The target triple.
  /// \param Ctx The target context.
  /// \param TAB The target assembler backend object. Takes ownership.
  /// \param OW The stream object.
  /// \param Emitter The target independent assembler object.Takes ownership.
  /// \param STI The subtarget information.
  /// \return A new object MCStreamer for the target.
  LLVM_ABI MCStreamer *createMCObjectStreamer(
      const Triple &T, MCContext &Ctx, std::unique_ptr<MCAsmBackend> TAB,
      std::unique_ptr<MCObjectWriter> OW,
      std::unique_ptr<MCCodeEmitter> Emitter, const MCSubtargetInfo &STI) const;

  /// Create a target-specific assembly streamer.
  ///
  /// \param Ctx The machine code context.
  /// \param OS The formatted output stream. Takes ownership.
  /// \param IP The instruction printer. Takes ownership.
  /// \param CE The code emitter. Takes ownership.
  /// \param TAB The target assembler backend. Takes ownership.
  /// \return A new assembly MCStreamer for the target.
  LLVM_ABI MCStreamer *
  createAsmStreamer(MCContext &Ctx, std::unique_ptr<formatted_raw_ostream> OS,
                    std::unique_ptr<MCInstPrinter> IP,
                    std::unique_ptr<MCCodeEmitter> CE,
                    std::unique_ptr<MCAsmBackend> TAB) const;

  /// Create a target-specific assembly TargetStreamer.
  ///
  /// \param S The streamer to attach to.
  /// \param OS The formatted output stream.
  /// \param InstPrint The instruction printer, or nullptr.
  /// \return A new assembly TargetStreamer, or nullptr if none is registered.
  MCTargetStreamer *createAsmTargetStreamer(MCStreamer &S,
                                            formatted_raw_ostream &OS,
                                            MCInstPrinter *InstPrint) const {
    if (AsmTargetStreamerCtorFn)
      return AsmTargetStreamerCtorFn(S, OS, InstPrint);
    return nullptr;
  }

  /// Create a no-op streamer that discards all output.
  ///
  /// \param Ctx The machine code context.
  /// \return A null streamer that discards all output.
  MCStreamer *createNullStreamer(MCContext &Ctx) const {
    MCStreamer *S = llvm::createNullStreamer(Ctx);
    createNullTargetStreamer(*S);
    return S;
  }

  /// Create a null TargetStreamer for \p S, if one is registered.
  ///
  /// \param S The streamer to attach to.
  /// \return A new null TargetStreamer, or nullptr if none is registered.
  MCTargetStreamer *createNullTargetStreamer(MCStreamer &S) const {
    if (NullTargetStreamerCtorFn)
      return NullTargetStreamerCtorFn(S);
    return nullptr;
  }

  /// Create a target-specific MCLFIRewriter.
  ///
  /// \param Ctx The machine code context.
  /// \param RegInfo The register information. Takes ownership.
  /// \param InstInfo The instruction information. Takes ownership.
  /// \return A new MCLFIRewriter, or nullptr if none is registered.
  MCLFIRewriter *
  createMCLFIRewriter(MCContext &Ctx, std::unique_ptr<MCRegisterInfo> &&RegInfo,
                      std::unique_ptr<MCInstrInfo> &&InstInfo) const {
    if (MCLFIRewriterCtorFn)
      return MCLFIRewriterCtorFn(Ctx, std::move(RegInfo), std::move(InstInfo));
    return nullptr;
  }

  /// createMCRelocationInfo - Create a target specific MCRelocationInfo.
  ///
  /// \param TT The target triple.
  /// \param Ctx The target context.
  /// \return A new MCRelocationInfo for the target.
  MCRelocationInfo *createMCRelocationInfo(const Triple &TT,
                                           MCContext &Ctx) const {
    MCRelocationInfoCtorTy Fn = MCRelocationInfoCtorFn
                                    ? MCRelocationInfoCtorFn
                                    : llvm::createMCRelocationInfo;
    return Fn(TT, Ctx);
  }

  /// createMCSymbolizer - Create a target specific MCSymbolizer.
  ///
  /// \param TT The target triple.
  /// \param GetOpInfo The function to get the symbolic information for
  /// operands.
  /// \param SymbolLookUp The function to lookup a symbol name.
  /// \param DisInfo The pointer to the block of symbolic information for above
  /// call
  /// back.
  /// \param Ctx The target context.
  /// \param RelInfo The relocation information for this target. Takes
  /// ownership.
  /// \return A new MCSymbolizer for the target.
  MCSymbolizer *
  createMCSymbolizer(const Triple &TT, LLVMOpInfoCallback GetOpInfo,
                     LLVMSymbolLookupCallback SymbolLookUp, void *DisInfo,
                     MCContext *Ctx,
                     std::unique_ptr<MCRelocationInfo> &&RelInfo) const {
    MCSymbolizerCtorTy Fn =
        MCSymbolizerCtorFn ? MCSymbolizerCtorFn : llvm::createMCSymbolizer;
    return Fn(TT, GetOpInfo, SymbolLookUp, DisInfo, Ctx, std::move(RelInfo));
  }

  /// createCustomBehaviour - Create a target specific CustomBehaviour.
  /// This class is used by llvm-mca and requires backend functionality.
  ///
  /// \param STI The subtarget information.
  /// \param SrcMgr The MCA source manager.
  /// \param MCII The instruction information.
  /// \return A new CustomBehaviour, or nullptr if none is registered.
  mca::CustomBehaviour *createCustomBehaviour(const MCSubtargetInfo &STI,
                                              const mca::SourceMgr &SrcMgr,
                                              const MCInstrInfo &MCII) const {
    if (CustomBehaviourCtorFn)
      return CustomBehaviourCtorFn(STI, SrcMgr, MCII);
    return nullptr;
  }

  /// createInstrPostProcess - Create a target specific InstrPostProcess.
  /// This class is used by llvm-mca and requires backend functionality.
  ///
  /// \param STI The subtarget information.
  /// \param MCII The instruction information.
  /// \return A new InstrPostProcess, or nullptr if none is registered.
  mca::InstrPostProcess *createInstrPostProcess(const MCSubtargetInfo &STI,
                                                const MCInstrInfo &MCII) const {
    if (InstrPostProcessCtorFn)
      return InstrPostProcessCtorFn(STI, MCII);
    return nullptr;
  }

  /// createInstrumentManager - Create a target specific
  /// InstrumentManager. This class is used by llvm-mca and requires
  /// backend functionality.
  ///
  /// \param STI The subtarget information.
  /// \param MCII The instruction information.
  /// \return A new InstrumentManager, or nullptr if none is registered.
  mca::InstrumentManager *
  createInstrumentManager(const MCSubtargetInfo &STI,
                          const MCInstrInfo &MCII) const {
    if (InstrumentManagerCtorFn)
      return InstrumentManagerCtorFn(STI, MCII);
    return nullptr;
  }

  /// Check that \p FeaturesString has a valid feature-list format.
  ///
  /// The expected format is:
  ///   "+attr1,+attr2,-attr3,...,+attrN"
  /// A comma separates each feature from the next (all lowercase).
  /// Each of the remaining features is prefixed with '+' or '-' indicating
  /// whether that feature should be enabled or disabled contrary to the cpu
  /// specification.
  /// The string must match exactly that format otherwise
  /// MCSubtargetInfo::ApplyFeatureFlag will fail.
  /// For example feature string "+a,+m,c" is accepted, and results in feature
  /// list {"+a", "+m", "c"}. Later in ApplyFeatureFlag, it asserts
  /// that all features must start with '+' or '-' and assert is failed.
  ///
  /// \param FeaturesString Feature list string to validate.
  /// \return True if \p FeaturesString has a valid feature-list format.
  LLVM_ABI static bool isValidFeatureListFormat(StringRef FeaturesString);

  /// @}
};

/// TargetRegistry - Generic interface to target specific features.
struct TargetRegistry {
  // FIXME: Make this a namespace, probably just move all the Register*
  // functions into Target (currently they all just set members on the Target
  // anyway, and Target friends this class so those functions can...
  // function).
  /// TargetRegistry cannot be constructed; use its static methods only.
  TargetRegistry() = delete;

  /// Forward iterator over registered Target entries.
  class iterator {
    friend struct TargetRegistry;

    const Target *Current = nullptr;

    explicit iterator(Target *T) : Current(T) {}

  public:
    /// Iterator category tag for std::iterator_traits.
    using iterator_category = std::forward_iterator_tag;
    /// Type of the referenced Target.
    using value_type = Target;
    /// Type used for iterator distance.
    using difference_type = std::ptrdiff_t;
    /// Pointer to the referenced Target.
    using pointer = value_type *;
    /// Reference to the referenced Target.
    using reference = value_type &;

    /// Construct an end (singular) iterator.
    iterator() = default;

    /// Return true if both iterators refer to the same target.
    ///
    /// \param x Iterator to compare against.
    /// \return True if both iterators refer to the same target.
    bool operator==(const iterator &x) const { return Current == x.Current; }
    /// Return true if the iterators refer to different targets.
    ///
    /// \param x Iterator to compare against.
    /// \return True if the iterators refer to different targets.
    bool operator!=(const iterator &x) const { return !operator==(x); }

    // Iterator traversal: forward iteration only
    /// Advance to the next registered target (pre-increment).
    ///
    /// \return A reference to this iterator after advancing.
    iterator &operator++() { // Preincrement
      assert(Current && "Cannot increment end iterator!");
      Current = Current->getNext();
      return *this;
    }
    /// Advance to the next registered target (post-increment).
    ///
    /// \param Unused Distinguishes post-increment from pre-increment.
    /// \return A copy of the iterator before advancing.
    iterator operator++(int Unused) { // Postincrement
      iterator tmp = *this;
      ++*this;
      return tmp;
    }

    /// Return a reference to the current Target.
    ///
    /// \return A reference to the Target at the current position.
    const Target &operator*() const {
      assert(Current && "Cannot dereference end iterator!");
      return *Current;
    }

    /// Return a pointer to the current Target.
    ///
    /// \return A pointer to the Target at the current position.
    const Target *operator->() const { return &operator*(); }
  };

  /// printRegisteredTargetsForVersion - Print the registered targets
  /// appropriately for inclusion in a tool's version output.
  ///
  /// \param OS The stream to write the version text to.
  LLVM_ABI static void printRegisteredTargetsForVersion(raw_ostream &OS);

  /// @name Registry Access
  /// @{

  /// Return an iterator range over all registered targets.
  ///
  /// \return A range covering every registered Target.
  LLVM_ABI static iterator_range<iterator> targets();

  /// lookupTarget - Lookup a target based on a target triple.
  ///
  /// \param TheTriple - The triple to use for finding a target.
  /// \param Error - On failure, an error string describing why no target was
  /// found.
  /// \return The matching Target, or nullptr if none was found.
  LLVM_ABI static const Target *lookupTarget(const Triple &TheTriple,
                                             std::string &Error);

  /// Lookup a target by architecture name or target triple.
  ///
  /// If the architecture name is non-empty, then the lookup is done by
  /// architecture. Otherwise, the target triple is used.
  ///
  /// \param ArchName - The architecture to use for finding a target.
  /// \param TheTriple - The triple to use for finding a target.  The
  /// triple is updated with canonical architecture name if a lookup
  /// by architecture is done.
  /// \param Error - On failure, an error string describing why no target was
  /// found.
  /// \return The matching Target, or nullptr if none was found.
  LLVM_ABI static const Target *
  lookupTarget(StringRef ArchName, Triple &TheTriple, std::string &Error);

  /// @}
  /// @name Target Registration
  /// @{

  /// RegisterTarget - Register the given target. Attempts to register a
  /// target which has already been registered will be ignored.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Name - The target name. This should be a static string.
  /// @param ShortDesc - A short target description. This should be a static
  /// string.
  /// @param BackendName - The name of the backend. This should be a static
  /// string that is the same for all targets that share a backend
  /// implementation and must match the name used in the 'def X : Target ...' in
  /// TableGen.
  /// @param ArchMatchFn - The arch match checking function for this target.
  /// @param HasJIT - Whether the target supports JIT code
  /// generation.
  LLVM_ABI static void RegisterTarget(Target &T, const char *Name,
                                      const char *ShortDesc,
                                      const char *BackendName,
                                      Target::ArchMatchFnTy ArchMatchFn,
                                      bool HasJIT = false);

  /// RegisterMCAsmInfo - Register a MCAsmInfo implementation for the
  /// given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a MCAsmInfo for the target.
  static void RegisterMCAsmInfo(Target &T, Target::MCAsmInfoCtorFnTy Fn) {
    T.MCAsmInfoCtorFn = Fn;
  }

  /// Register a MCObjectFileInfo implementation for the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a MCObjectFileInfo for the target.
  static void RegisterMCObjectFileInfo(Target &T,
                                       Target::MCObjectFileInfoCtorFnTy Fn) {
    T.MCObjectFileInfoCtorFn = Fn;
  }

  /// RegisterMCInstrInfo - Register a MCInstrInfo implementation for the
  /// given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a MCInstrInfo for the target.
  static void RegisterMCInstrInfo(Target &T, Target::MCInstrInfoCtorFnTy Fn) {
    T.MCInstrInfoCtorFn = Fn;
  }

  /// RegisterMCInstrAnalysis - Register a MCInstrAnalysis implementation for
  /// the given target.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a MCInstrAnalysis for the target.
  static void RegisterMCInstrAnalysis(Target &T,
                                      Target::MCInstrAnalysisCtorFnTy Fn) {
    T.MCInstrAnalysisCtorFn = Fn;
  }

  /// RegisterMCRegInfo - Register a MCRegisterInfo implementation for the
  /// given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a MCRegisterInfo for the target.
  static void RegisterMCRegInfo(Target &T, Target::MCRegInfoCtorFnTy Fn) {
    T.MCRegInfoCtorFn = Fn;
  }

  /// RegisterMCSubtargetInfo - Register a MCSubtargetInfo implementation for
  /// the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a MCSubtargetInfo for the target.
  static void RegisterMCSubtargetInfo(Target &T,
                                      Target::MCSubtargetInfoCtorFnTy Fn) {
    T.MCSubtargetInfoCtorFn = Fn;
  }

  /// RegisterTargetMachine - Register a TargetMachine implementation for the
  /// given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a TargetMachine for the target.
  static void RegisterTargetMachine(Target &T, Target::TargetMachineCtorTy Fn) {
    T.TargetMachineCtorFn = Fn;
  }

  /// RegisterMCAsmBackend - Register a MCAsmBackend implementation for the
  /// given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an AsmBackend for the target.
  static void RegisterMCAsmBackend(Target &T, Target::MCAsmBackendCtorTy Fn) {
    T.MCAsmBackendCtorFn = Fn;
  }

  /// RegisterMCAsmParser - Register a MCTargetAsmParser implementation for
  /// the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an MCTargetAsmParser for the target.
  static void RegisterMCAsmParser(Target &T, Target::MCAsmParserCtorTy Fn) {
    T.MCAsmParserCtorFn = Fn;
  }

  /// RegisterAsmPrinter - Register an AsmPrinter implementation for the given
  /// target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an AsmPrinter for the target.
  static void RegisterAsmPrinter(Target &T, Target::AsmPrinterCtorTy Fn) {
    T.AsmPrinterCtorFn = Fn;
  }

  /// RegisterMCDisassembler - Register a MCDisassembler implementation for
  /// the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an MCDisassembler for the target.
  static void RegisterMCDisassembler(Target &T,
                                     Target::MCDisassemblerCtorTy Fn) {
    T.MCDisassemblerCtorFn = Fn;
  }

  /// RegisterMCInstPrinter - Register a MCInstPrinter implementation for the
  /// given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an MCInstPrinter for the target.
  static void RegisterMCInstPrinter(Target &T, Target::MCInstPrinterCtorTy Fn) {
    T.MCInstPrinterCtorFn = Fn;
  }

  /// RegisterMCCodeEmitter - Register a MCCodeEmitter implementation for the
  /// given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an MCCodeEmitter for the target.
  static void RegisterMCCodeEmitter(Target &T, Target::MCCodeEmitterCtorTy Fn) {
    T.MCCodeEmitterCtorFn = Fn;
  }

  /// Register a COFF streamer constructor for the given target.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a COFF streamer for the target.
  static void RegisterCOFFStreamer(Target &T, Target::COFFStreamerCtorTy Fn) {
    T.COFFStreamerCtorFn = Fn;
  }

  /// Register a Mach-O streamer constructor for the given target.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a Mach-O streamer for the target.
  static void RegisterMachOStreamer(Target &T, Target::MachOStreamerCtorTy Fn) {
    T.MachOStreamerCtorFn = Fn;
  }

  /// Register an ELF streamer constructor for the given target.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an ELF streamer for the target.
  static void RegisterELFStreamer(Target &T, Target::ELFStreamerCtorTy Fn) {
    T.ELFStreamerCtorFn = Fn;
  }

  /// Register an XCOFF streamer constructor for the given target.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an XCOFF streamer for the target.
  static void RegisterXCOFFStreamer(Target &T, Target::XCOFFStreamerCtorTy Fn) {
    T.XCOFFStreamerCtorFn = Fn;
  }

  /// Register a null TargetStreamer constructor for the given target.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a null TargetStreamer for the target.
  static void RegisterNullTargetStreamer(Target &T,
                                         Target::NullTargetStreamerCtorTy Fn) {
    T.NullTargetStreamerCtorFn = Fn;
  }

  /// Register an assembly streamer constructor for the given target.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an assembly streamer for the target.
  static void RegisterAsmStreamer(Target &T, Target::AsmStreamerCtorTy Fn) {
    T.AsmStreamerCtorFn = Fn;
  }

  /// Register an assembly TargetStreamer constructor for the given target.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an assembly TargetStreamer.
  static void RegisterAsmTargetStreamer(Target &T,
                                        Target::AsmTargetStreamerCtorTy Fn) {
    T.AsmTargetStreamerCtorFn = Fn;
  }

  /// Register an object TargetStreamer constructor for the given target.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an object TargetStreamer.
  static void
  RegisterObjectTargetStreamer(Target &T,
                               Target::ObjectTargetStreamerCtorTy Fn) {
    T.ObjectTargetStreamerCtorFn = Fn;
  }

  /// RegisterMCRelocationInfo - Register an MCRelocationInfo
  /// implementation for the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an MCRelocationInfo for the target.
  static void RegisterMCRelocationInfo(Target &T,
                                       Target::MCRelocationInfoCtorTy Fn) {
    T.MCRelocationInfoCtorFn = Fn;
  }

  /// RegisterMCSymbolizer - Register an MCSymbolizer
  /// implementation for the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an MCSymbolizer for the target.
  static void RegisterMCSymbolizer(Target &T, Target::MCSymbolizerCtorTy Fn) {
    T.MCSymbolizerCtorFn = Fn;
  }

  /// RegisterCustomBehaviour - Register a CustomBehaviour
  /// implementation for the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a CustomBehaviour for the target.
  static void RegisterCustomBehaviour(Target &T,
                                      Target::CustomBehaviourCtorTy Fn) {
    T.CustomBehaviourCtorFn = Fn;
  }

  /// RegisterInstrPostProcess - Register an InstrPostProcess
  /// implementation for the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an InstrPostProcess for the target.
  static void RegisterInstrPostProcess(Target &T,
                                       Target::InstrPostProcessCtorTy Fn) {
    T.InstrPostProcessCtorFn = Fn;
  }

  /// RegisterInstrumentManager - Register an InstrumentManager
  /// implementation for the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an InstrumentManager for the
  /// target.
  static void RegisterInstrumentManager(Target &T,
                                        Target::InstrumentManagerCtorTy Fn) {
    T.InstrumentManagerCtorFn = Fn;
  }

  /// Register an MCLFIRewriter implementation for the given target.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an MCLFIRewriter for the target.
  static void RegisterMCLFIRewriter(Target &T, Target::MCLFIRewriterCtorTy Fn) {
    T.MCLFIRewriterCtorFn = Fn;
  }

  /// @}
};

//===--------------------------------------------------------------------===//

/// RegisterTarget - Helper template for registering a target, for use in the
/// target's initialization function. Usage:
///
///
/// Target &getTheFooTarget() { // The global target instance.
///   static Target TheFooTarget;
///   return TheFooTarget;
/// }
/// extern "C" void LLVMInitializeFooTargetInfo() {
///   RegisterTarget<Triple::foo> X(getTheFooTarget(), "foo", "Foo
///   description", "Foo" /* Backend Name */);
/// }
template <Triple::ArchType TargetArchType = Triple::UnknownArch,
          bool HasJIT = false>
struct RegisterTarget {
  /// Register target \p T with the given identity and backend name.
  ///
  /// \param T The target instance to register.
  /// \param Name The target name (static string).
  /// \param Desc A short target description (static string).
  /// \param BackendName The backend name matching TableGen.
  RegisterTarget(Target &T, const char *Name, const char *Desc,
                 const char *BackendName) {
    TargetRegistry::RegisterTarget(T, Name, Desc, BackendName, &getArchMatch,
                                   HasJIT);
  }

  /// Return true if \p Arch matches this registration's architecture.
  ///
  /// \param Arch Architecture type to test.
  /// \return True if \p Arch equals this registration's architecture type.
  static bool getArchMatch(Triple::ArchType Arch) {
    return Arch == TargetArchType;
  }
};

/// Helper template for registering a target assembly info implementation.
///
/// This invokes the static "Create" method on the class to actually do the
/// construction. Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCAsmInfo<FooMCAsmInfo> X(TheFooTarget);
/// }
template <class MCAsmInfoImpl> struct RegisterMCAsmInfo {
  /// Register MCAsmInfoImpl as the MCAsmInfo for target \p T.
  ///
  /// \param T The target being registered.
  RegisterMCAsmInfo(Target &T) {
    TargetRegistry::RegisterMCAsmInfo(T, &Allocator);
  }

private:
  static MCAsmInfo *Allocator(const MCRegisterInfo & /*MRI*/, const Triple &TT,
                              const MCTargetOptions &Options) {
    return new MCAsmInfoImpl(TT, Options);
  }
};

/// RegisterMCAsmInfoFn - Helper template for registering a target assembly info
/// implementation.  This invokes the specified function to do the
/// construction.  Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCAsmInfoFn X(TheFooTarget, TheFunction);
/// }
struct RegisterMCAsmInfoFn {
  /// Register constructor \p Fn as the MCAsmInfo for target \p T.
  ///
  /// \param T The target being registered.
  /// \param Fn Function that constructs the MCAsmInfo.
  RegisterMCAsmInfoFn(Target &T, Target::MCAsmInfoCtorFnTy Fn) {
    TargetRegistry::RegisterMCAsmInfo(T, Fn);
  }
};

/// Helper template for registering a target object file info implementation.
///
/// This invokes the static "Create" method on the class to actually do the
/// construction. Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCObjectFileInfo<FooMCObjectFileInfo> X(TheFooTarget);
/// }
template <class MCObjectFileInfoImpl> struct RegisterMCObjectFileInfo {
  /// Register MCObjectFileInfoImpl as the MCObjectFileInfo for target \p T.
  ///
  /// \param T The target being registered.
  RegisterMCObjectFileInfo(Target &T) {
    TargetRegistry::RegisterMCObjectFileInfo(T, &Allocator);
  }

private:
  static MCObjectFileInfo *Allocator(MCContext &Ctx, bool PIC,
                                     bool LargeCodeModel = false) {
    return new MCObjectFileInfoImpl(Ctx, PIC, LargeCodeModel);
  }
};

/// Helper template for registering a target object file info implementation.
/// This invokes the specified function to do the construction.  Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCObjectFileInfoFn X(TheFooTarget, TheFunction);
/// }
struct RegisterMCObjectFileInfoFn {
  /// Register constructor \p Fn as the MCObjectFileInfo for target \p T.
  ///
  /// \param T The target being registered.
  /// \param Fn Function that constructs the MCObjectFileInfo.
  RegisterMCObjectFileInfoFn(Target &T, Target::MCObjectFileInfoCtorFnTy Fn) {
    TargetRegistry::RegisterMCObjectFileInfo(T, Fn);
  }
};

/// Helper template for registering a target instruction info implementation.
///
/// This invokes the static "Create" method on the class to actually do the
/// construction. Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCInstrInfo<FooMCInstrInfo> X(TheFooTarget);
/// }
template <class MCInstrInfoImpl> struct RegisterMCInstrInfo {
  /// Register MCInstrInfoImpl as the MCInstrInfo for target \p T.
  ///
  /// \param T The target being registered.
  RegisterMCInstrInfo(Target &T) {
    TargetRegistry::RegisterMCInstrInfo(T, &Allocator);
  }

private:
  static MCInstrInfo *Allocator() { return new MCInstrInfoImpl(); }
};

/// Helper template for registering a target instruction info implementation.
///
/// This invokes the specified function to do the construction. Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCInstrInfoFn X(TheFooTarget, TheFunction);
/// }
struct RegisterMCInstrInfoFn {
  /// Register constructor \p Fn as the MCInstrInfo for target \p T.
  ///
  /// \param T The target being registered.
  /// \param Fn Function that constructs the MCInstrInfo.
  RegisterMCInstrInfoFn(Target &T, Target::MCInstrInfoCtorFnTy Fn) {
    TargetRegistry::RegisterMCInstrInfo(T, Fn);
  }
};

/// Helper template for registering a target instruction analyzer
/// implementation.
///
/// This invokes the static "Create" method on the class to actually do the
/// construction. Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCInstrAnalysis<FooMCInstrAnalysis> X(TheFooTarget);
/// }
template <class MCInstrAnalysisImpl> struct RegisterMCInstrAnalysis {
  /// Register MCInstrAnalysisImpl as the MCInstrAnalysis for target \p T.
  ///
  /// \param T The target being registered.
  RegisterMCInstrAnalysis(Target &T) {
    TargetRegistry::RegisterMCInstrAnalysis(T, &Allocator);
  }

private:
  static MCInstrAnalysis *Allocator(const MCInstrInfo *Info) {
    return new MCInstrAnalysisImpl(Info);
  }
};

/// Helper template for registering a target instruction analyzer
/// implementation.
///
/// This invokes the specified function to do the construction. Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCInstrAnalysisFn X(TheFooTarget, TheFunction);
/// }
struct RegisterMCInstrAnalysisFn {
  /// Register constructor \p Fn as the MCInstrAnalysis for target \p T.
  ///
  /// \param T The target being registered.
  /// \param Fn Function that constructs the MCInstrAnalysis.
  RegisterMCInstrAnalysisFn(Target &T, Target::MCInstrAnalysisCtorFnTy Fn) {
    TargetRegistry::RegisterMCInstrAnalysis(T, Fn);
  }
};

/// Helper template for registering a target register info implementation.
///
/// This invokes the static "Create" method on the class to actually do the
/// construction. Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCRegInfo<FooMCRegInfo> X(TheFooTarget);
/// }
template <class MCRegisterInfoImpl> struct RegisterMCRegInfo {
  /// Register MCRegisterInfoImpl as the MCRegisterInfo for target \p T.
  ///
  /// \param T The target being registered.
  RegisterMCRegInfo(Target &T) {
    TargetRegistry::RegisterMCRegInfo(T, &Allocator);
  }

private:
  static MCRegisterInfo *Allocator(const Triple & /*TT*/) {
    return new MCRegisterInfoImpl();
  }
};

/// RegisterMCRegInfoFn - Helper template for registering a target register
/// info implementation.  This invokes the specified function to do the
/// construction.  Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCRegInfoFn X(TheFooTarget, TheFunction);
/// }
struct RegisterMCRegInfoFn {
  /// Register constructor \p Fn as the MCRegisterInfo for target \p T.
  ///
  /// \param T The target being registered.
  /// \param Fn Function that constructs the MCRegisterInfo.
  RegisterMCRegInfoFn(Target &T, Target::MCRegInfoCtorFnTy Fn) {
    TargetRegistry::RegisterMCRegInfo(T, Fn);
  }
};

/// Helper template for registering a target subtarget info implementation.
///
/// This invokes the static "Create" method on the class to actually do the
/// construction. Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCSubtargetInfo<FooMCSubtargetInfo> X(TheFooTarget);
/// }
template <class MCSubtargetInfoImpl> struct RegisterMCSubtargetInfo {
  /// Register MCSubtargetInfoImpl as the MCSubtargetInfo for target \p T.
  ///
  /// \param T The target being registered.
  RegisterMCSubtargetInfo(Target &T) {
    TargetRegistry::RegisterMCSubtargetInfo(T, &Allocator);
  }

private:
  static MCSubtargetInfo *Allocator(const Triple & /*TT*/, StringRef /*CPU*/,
                                    StringRef /*FS*/) {
    return new MCSubtargetInfoImpl();
  }
};

/// Helper template for registering a target subtarget info implementation.
///
/// This invokes the specified function to do the construction. Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCSubtargetInfoFn X(TheFooTarget, TheFunction);
/// }
struct RegisterMCSubtargetInfoFn {
  /// Register constructor \p Fn as the MCSubtargetInfo for target \p T.
  ///
  /// \param T The target being registered.
  /// \param Fn Function that constructs the MCSubtargetInfo.
  RegisterMCSubtargetInfoFn(Target &T, Target::MCSubtargetInfoCtorFnTy Fn) {
    TargetRegistry::RegisterMCSubtargetInfo(T, Fn);
  }
};

/// RegisterTargetMachine - Helper template for registering a target machine
/// implementation, for use in the target machine initialization
/// function. Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterTargetMachine<FooTargetMachine> X(TheFooTarget);
/// }
template <class TargetMachineImpl> struct RegisterTargetMachine {
  /// Register TargetMachineImpl as the TargetMachine for target \p T.
  ///
  /// \param T The target being registered.
  RegisterTargetMachine(Target &T) {
    TargetRegistry::RegisterTargetMachine(T, &Allocator);
  }

private:
  static TargetMachine *
  Allocator(const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
            const TargetOptions &Options, std::optional<Reloc::Model> RM,
            std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT) {
    return new TargetMachineImpl(T, TT, CPU, FS, Options, RM, CM, OL, JIT);
  }
};

/// RegisterMCAsmBackend - Helper template for registering a target specific
/// assembler backend. Usage:
///
/// extern "C" void LLVMInitializeFooMCAsmBackend() {
///   extern Target TheFooTarget;
///   RegisterMCAsmBackend<FooAsmLexer> X(TheFooTarget);
/// }
template <class MCAsmBackendImpl> struct RegisterMCAsmBackend {
  /// Register MCAsmBackendImpl as the MCAsmBackend for target \p T.
  ///
  /// \param T The target being registered.
  RegisterMCAsmBackend(Target &T) {
    TargetRegistry::RegisterMCAsmBackend(T, &Allocator);
  }

private:
  static MCAsmBackend *Allocator(const Target &T, const MCSubtargetInfo &STI,
                                 const MCRegisterInfo &MRI,
                                 const MCTargetOptions &Options) {
    return new MCAsmBackendImpl(T, STI, MRI);
  }
};

/// RegisterMCAsmParser - Helper template for registering a target specific
/// assembly parser, for use in the target machine initialization
/// function. Usage:
///
/// extern "C" void LLVMInitializeFooMCAsmParser() {
///   extern Target TheFooTarget;
///   RegisterMCAsmParser<FooAsmParser> X(TheFooTarget);
/// }
template <class MCAsmParserImpl> struct RegisterMCAsmParser {
  /// Register MCAsmParserImpl as the MCAsmParser for target \p T.
  ///
  /// \param T The target being registered.
  RegisterMCAsmParser(Target &T) {
    TargetRegistry::RegisterMCAsmParser(T, &Allocator);
  }

private:
  static MCTargetAsmParser *Allocator(const MCSubtargetInfo &STI,
                                      MCAsmParser &P, const MCInstrInfo &MII) {
    return new MCAsmParserImpl(STI, P, MII);
  }
};

/// RegisterAsmPrinter - Helper template for registering a target specific
/// assembly printer, for use in the target machine initialization
/// function. Usage:
///
/// extern "C" void LLVMInitializeFooAsmPrinter() {
///   extern Target TheFooTarget;
///   RegisterAsmPrinter<FooAsmPrinter> X(TheFooTarget);
/// }
template <class AsmPrinterImpl> struct RegisterAsmPrinter {
  /// Register AsmPrinterImpl as the AsmPrinter for target \p T.
  ///
  /// \param T The target being registered.
  RegisterAsmPrinter(Target &T) {
    TargetRegistry::RegisterAsmPrinter(T, &Allocator);
  }

private:
  static AsmPrinter *Allocator(TargetMachine &TM,
                               std::unique_ptr<MCStreamer> &&Streamer) {
    return new AsmPrinterImpl(TM, std::move(Streamer));
  }
};

/// RegisterMCCodeEmitter - Helper template for registering a target specific
/// machine code emitter, for use in the target initialization
/// function. Usage:
///
/// extern "C" void LLVMInitializeFooMCCodeEmitter() {
///   extern Target TheFooTarget;
///   RegisterMCCodeEmitter<FooCodeEmitter> X(TheFooTarget);
/// }
template <class MCCodeEmitterImpl> struct RegisterMCCodeEmitter {
  /// Register MCCodeEmitterImpl as the MCCodeEmitter for target \p T.
  ///
  /// \param T The target being registered.
  RegisterMCCodeEmitter(Target &T) {
    TargetRegistry::RegisterMCCodeEmitter(T, &Allocator);
  }

private:
  static MCCodeEmitter *Allocator(const MCInstrInfo & /*II*/,
                                  MCContext & /*Ctx*/) {
    return new MCCodeEmitterImpl();
  }
};

} // end namespace llvm

#endif // LLVM_MC_TARGETREGISTRY_H
