//===- MCStreamer.h - High-level Streaming Machine Code Output --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCStreamer class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSTREAMER_H
#define LLVM_MC_MCSTREAMER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCLinkerOptimizationHint.h"
#include "llvm/MC/MCPseudoProbe.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCWinEH.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TargetParser/ARMTargetParser.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class APInt;
class AssemblerConstantPools;
class MCAsmBackend;
class MCAssembler;
class MCLFIRewriter;
class MCContext;
class MCExpr;
class MCInst;
class MCInstPrinter;
class MCRegister;
class MCStreamer;
class MCSubtargetInfo;
class MCSymbol;
class MCSymbolRefExpr;
class Triple;
class Twine;
class raw_ostream;

namespace codeview {
struct DefRangeRegisterRelHeader;
struct DefRangeSubfieldRegisterHeader;
struct DefRangeRegisterHeader;
struct DefRangeFramePointerRelHeader;
struct DefRangeRegisterRelIndirHeader;
}

/// Pair of a section and a subsection index.
using MCSectionSubPair = std::pair<MCSection *, uint32_t>;

/// Target specific streamer interface. This is used so that targets can
/// implement support for target specific assembly directives.
///
/// If target foo wants to use this, it should implement 3 classes:
/// * FooTargetStreamer : public MCTargetStreamer
/// * FooTargetAsmStreamer : public FooTargetStreamer
/// * FooTargetELFStreamer : public FooTargetStreamer
///
/// FooTargetStreamer should have a pure virtual method for each directive. For
/// example, for a ".bar symbol_name" directive, it should have
/// virtual emitBar(const MCSymbol &Symbol) = 0;
///
/// The FooTargetAsmStreamer and FooTargetELFStreamer classes implement the
/// method. The assembly streamer just prints ".bar symbol_name". The object
/// streamer does whatever is needed to implement .bar in the object file.
///
/// In the assembly printer and parser the target streamer can be used by
/// calling getTargetStreamer and casting it to FooTargetStreamer:
///
/// MCTargetStreamer &TS = OutStreamer.getTargetStreamer();
/// FooTargetStreamer &ATS = static_cast<FooTargetStreamer &>(TS);
///
/// The base classes FooTargetAsmStreamer and FooTargetELFStreamer should
/// *never* be treated differently. Callers should always talk to a
/// FooTargetStreamer.
class LLVM_ABI MCTargetStreamer {
protected:
  /// Streamer this target streamer is attached to.
  MCStreamer &Streamer;

public:
  /// Construct a target streamer bound to \p S.
  /// \param S - The MCStreamer this target streamer extends.
  MCTargetStreamer(MCStreamer &S);
  /// Destroy the target streamer.
  virtual ~MCTargetStreamer();

  /// Return the MCStreamer this target streamer is attached to.
  /// \return The MCStreamer this target streamer is attached to.
  MCStreamer &getStreamer() { return Streamer; }
  /// Return the MC context of the attached streamer.
  /// \return The MC context of the attached streamer.
  MCContext &getContext();

  /// Allow a target to add behavior to the EmitLabel of MCStreamer.
  /// \param Symbol - The symbol being emitted as a label.
  virtual void emitLabel(MCSymbol *Symbol);
  /// Allow a target to add behavior to the emitAssignment of MCStreamer.
  /// \param Symbol - The symbol being assigned to.
  /// \param Value - The value assigned to \p Symbol.
  virtual void emitAssignment(MCSymbol *Symbol, const MCExpr *Value);

  /// Pretty-print \p Inst as assembly to \p OS.
  /// \param InstPrinter - Instruction printer used to format the instruction.
  /// \param Address - Address of the instruction, if known.
  /// \param Inst - Instruction to print.
  /// \param STI - Subtarget info in effect at this instruction.
  /// \param OS - Stream to print to.
  virtual void prettyPrintAsm(MCInstPrinter &InstPrinter, uint64_t Address,
                              const MCInst &Inst, const MCSubtargetInfo &STI,
                              raw_ostream &OS);

  /// Emit a DWARF .file directive string.
  /// \param Directive - The .file directive text to emit.
  virtual void emitDwarfFileDirective(StringRef Directive);

  /// Update streamer for a new active section.
  ///
  /// This is called by popSection and switchSection, if the current
  /// section changes.
  /// \param CurSection - Section being left, or null if none.
  /// \param Section - Section being entered.
  /// \param SubSection - Subsection index within \p Section.
  /// \param OS - Assembly output stream, if printing assembly.
  virtual void changeSection(const MCSection *CurSection, MCSection *Section,
                             uint32_t SubSection, raw_ostream &OS);

  /// Emit the expression \p Value as a target-specific value.
  /// \param Value - Expression to emit.
  virtual void emitValue(const MCExpr *Value);

  /// Emit the bytes in \p Data into the output.
  ///
  /// This is used to emit bytes in \p Data as sequence of .byte directives.
  /// \param Data - Bytes to emit.
  virtual void emitRawBytes(StringRef Data);

  /// Emit pending assembler constant pools.
  virtual void emitConstantPools();

  /// Finish emission for this target streamer.
  virtual void finish();
};

// FIXME: declared here because it is used from
// lib/CodeGen/AsmPrinter/ARMException.cpp.
/// ARM-specific target streamer for EHABI, EABI attributes, and WinCFI.
class LLVM_ABI ARMTargetStreamer : public MCTargetStreamer {
public:
  /// Construct an ARM target streamer bound to \p S.
  /// \param S - The MCStreamer this target streamer extends.
  ARMTargetStreamer(MCStreamer &S);
  /// Destroy the ARM target streamer.
  ~ARMTargetStreamer() override;

  /// Emit a .fnstart directive, beginning an ARM EHABI function.
  virtual void emitFnStart();
  /// Emit a .fnend directive, ending an ARM EHABI function.
  virtual void emitFnEnd();
  /// Emit a .cantunwind directive, marking the function as not unwindable.
  virtual void emitCantUnwind();
  /// Emit a .personality directive naming the personality routine.
  /// \param Personality - Symbol of the personality function.
  virtual void emitPersonality(const MCSymbol *Personality);
  /// Emit a .personalityindex directive selecting a compact personality.
  /// \param Index - Compact personality index.
  virtual void emitPersonalityIndex(unsigned Index);
  /// Emit a .handlerdata directive, switching to the exception table.
  virtual void emitHandlerData();
  /// Emit a .setfp directive relating the frame and stack pointers.
  /// \param FpReg - Frame-pointer register.
  /// \param SpReg - Stack-pointer register.
  /// \param Offset - Offset from \p SpReg to \p FpReg.
  virtual void emitSetFP(MCRegister FpReg, MCRegister SpReg,
                         int64_t Offset = 0);
  /// Emit a .movsp directive recording a stack-pointer move.
  /// \param Reg - Register the stack pointer is copied from.
  /// \param Offset - Additional offset applied to \p Reg.
  virtual void emitMovSP(MCRegister Reg, int64_t Offset = 0);
  /// Emit a .pad directive for stack allocation.
  /// \param Offset - Number of bytes allocated on the stack.
  virtual void emitPad(int64_t Offset);
  /// Emit a .save or .vsave directive for the given registers.
  /// \param RegList - Registers saved by this directive.
  /// \param isVector - True to emit .vsave rather than .save.
  virtual void emitRegSave(const SmallVectorImpl<MCRegister> &RegList,
                           bool isVector);
  /// Emit a .unwind_raw directive with the given opcodes.
  /// \param StackOffset - Stack offset encoded with the opcodes.
  /// \param Opcodes - Raw unwind opcodes to emit.
  virtual void emitUnwindRaw(int64_t StackOffset,
                             const SmallVectorImpl<uint8_t> &Opcodes);

  /// Switch the current EABI attribute vendor.
  /// \param Vendor - Vendor name, such as "aeabi".
  virtual void switchVendor(StringRef Vendor);
  /// Emit a numeric .eabi_attribute directive.
  /// \param Attribute - EABI attribute tag.
  /// \param Value - Integer value of the attribute.
  virtual void emitAttribute(unsigned Attribute, unsigned Value);
  /// Emit a string .eabi_attribute directive.
  /// \param Attribute - EABI attribute tag.
  /// \param String - String value of the attribute.
  virtual void emitTextAttribute(unsigned Attribute, StringRef String);
  /// Emit an .eabi_attribute with both an integer and a string value.
  /// \param Attribute - EABI attribute tag.
  /// \param IntValue - Integer value of the attribute.
  /// \param StringValue - Optional string value of the attribute.
  virtual void emitIntTextAttribute(unsigned Attribute, unsigned IntValue,
                                    StringRef StringValue = "");
  /// Emit a .fpu directive.
  /// \param FPU - FPU kind to record.
  virtual void emitFPU(ARM::FPUKind FPU);
  /// Emit a .arch directive.
  /// \param Arch - Architecture kind to record.
  virtual void emitArch(ARM::ArchKind Arch);
  /// Emit a .arch_extension directive.
  /// \param ArchExt - Architecture-extension bitmask.
  virtual void emitArchExtension(uint64_t ArchExt);
  /// Emit a .object_arch directive.
  /// \param Arch - Architecture recorded in the object file.
  virtual void emitObjectArch(ARM::ArchKind Arch);
  /// Emit EABI target attributes derived from \p STI.
  /// \param STI - Subtarget info used to choose attributes.
  void emitTargetAttributes(const MCSubtargetInfo &STI);
  /// Finish the current EABI attribute section.
  virtual void finishAttributeSection();
  /// Emit a .inst directive encoding a raw instruction.
  /// \param Inst - Instruction encoding.
  /// \param Suffix - Optional width suffix, such as 'n' or 'w'.
  virtual void emitInst(uint32_t Inst, char Suffix = '\0');

  /// Annotate a TLS descriptor sequence for the given symbol.
  /// \param SRE - TLS descriptor symbol reference.
  virtual void annotateTLSDescriptorSequence(const MCSymbolRefExpr *SRE);

  /// Emit a .syntax unified directive.
  virtual void emitSyntaxUnified();

  /// Emit a .code 16 directive, switching to Thumb.
  virtual void emitCode16();
  /// Emit a .code 32 directive, switching to ARM.
  virtual void emitCode32();

  /// Note in the output that the specified \p Symbol is a Thumb mode function.
  /// \param Symbol - Symbol marked as a Thumb function.
  virtual void emitThumbFunc(MCSymbol *Symbol);
  /// Emit a Thumb .set assignment of \p Value to \p Symbol.
  /// \param Symbol - Symbol being assigned.
  /// \param Value - Value assigned to \p Symbol.
  virtual void emitThumbSet(MCSymbol *Symbol, const MCExpr *Value);

  /// Emit pending ARM assembler constant pools.
  void emitConstantPools() override;

  /// Emit a Windows ARM .seh_stackalloc directive.
  /// \param Size - Number of bytes allocated.
  /// \param Wide - True to emit the wide form of the directive.
  virtual void emitARMWinCFIAllocStack(unsigned Size, bool Wide);
  /// Emit a Windows ARM .seh_save_regs directive.
  /// \param Mask - Bitmask of saved integer registers.
  /// \param Wide - True to emit the wide form of the directive.
  virtual void emitARMWinCFISaveRegMask(unsigned Mask, bool Wide);
  /// Emit a Windows ARM .seh_save_sp directive.
  /// \param Reg - Register the stack pointer is saved to.
  virtual void emitARMWinCFISaveSP(unsigned Reg);
  /// Emit a Windows ARM .seh_save_fregs directive.
  /// \param First - First saved floating-point register.
  /// \param Last - Last saved floating-point register.
  virtual void emitARMWinCFISaveFRegs(unsigned First, unsigned Last);
  /// Emit a Windows ARM .seh_save_lr directive.
  /// \param Offset - Stack offset of the saved link register.
  virtual void emitARMWinCFISaveLR(unsigned Offset);
  /// Emit a Windows ARM .seh_endprologue directive.
  /// \param Fragment - True if this prologue is a fragment.
  virtual void emitARMWinCFIPrologEnd(bool Fragment);
  /// Emit a Windows ARM .seh_nop directive.
  /// \param Wide - True to emit the wide form of the directive.
  virtual void emitARMWinCFINop(bool Wide);
  /// Emit a Windows ARM .seh_startepilogue directive.
  /// \param Condition - IT-block condition for a conditional epilogue.
  virtual void emitARMWinCFIEpilogStart(unsigned Condition);
  /// Emit a Windows ARM .seh_endepilogue directive.
  virtual void emitARMWinCFIEpilogEnd();
  /// Emit a Windows ARM .seh_custom unwind opcode.
  /// \param Opcode - Custom unwind opcode.
  virtual void emitARMWinCFICustom(unsigned Opcode);

  /// Reset any state between object emissions, i.e. the equivalent of
  /// MCStreamer's reset method.
  virtual void reset();

  /// Add a constant-pool entry for the ldr= pseudo.
  ///
  /// Callback used to implement the ldr= pseudo. Add a new entry to the
  /// constant pool for the current section and return an MCExpr that can be
  /// used to refer to the constant pool location.
  /// \param Expr - Constant value to intern in the pool.
  /// \param Loc - Source location for diagnostics.
  /// \return An expression referring to the constant-pool entry.
  const MCExpr *addConstantPoolEntry(const MCExpr *Expr, SMLoc Loc);

  /// Callback used to implement the .ltorg directive.
  /// Emit contents of constant pool for the current section.
  void emitCurrentConstantPool();

private:
  std::unique_ptr<AssemblerConstantPools> ConstantPools;
};

/// Streaming machine code generation interface.
///
/// This interface is intended to provide a programmatic interface that is very
/// similar to the level that an assembler .s file provides.  It has callbacks
/// to emit bytes, handle directives, etc.  The implementation of this interface
/// retains state to know what the current section is etc.
///
/// There are multiple implementations of this interface: one for writing out
/// a .s file, and implementations that write out .o files of various formats.
///
class LLVM_ABI MCStreamer {
  MCContext &Context;
  std::unique_ptr<MCTargetStreamer> TargetStreamer;

  // This is a pair of index into DwarfFrameInfos and the MCSection associated
  // with the frame. Note, we use an index instead of an iterator because they
  // can be invalidated in std::vector.
  SmallVector<std::pair<size_t, MCSection *>, 1> FrameInfoStack;
  MCDwarfFrameInfo *getCurrentDwarfFrameInfo();

  /// Similar to DwarfFrameInfos, but for SEH unwind info. Chained frames may
  /// refer to each other, so use std::unique_ptr to provide pointer stability.
  std::vector<std::unique_ptr<WinEH::FrameInfo>> WinFrameInfos;

  WinEH::FrameInfo *CurrentWinFrameInfo;
  size_t CurrentProcWinFrameInfoStartIndex;

  /// Default unwind version for new WinCFI frames.
  uint8_t DefaultWinCFIUnwindVersion = 1;

  /// This is stack of current and previous section values saved by
  /// pushSection.
  SmallVector<std::pair<MCSectionSubPair, MCSectionSubPair>, 4> SectionStack;

  /// Pointer to the parser's SMLoc if available. This is used to provide
  /// locations for diagnostics.
  const SMLoc *StartTokLocPtr = nullptr;

  /// The next unique ID to use when creating a WinCFI-related section (.pdata
  /// or .xdata). This ID ensures that we have a one-to-one mapping from
  /// code section to unwind info section, which MSVC's incremental linker
  /// requires.
  unsigned NextWinCFIID = 0;

  bool UseAssemblerInfoForParsing = true;

  /// Is the assembler allowed to insert padding automatically?  For
  /// correctness reasons, we sometimes need to ensure instructions aren't
  /// separated in unexpected ways.  At the moment, this feature is only
  /// useable from an integrated assembler, but assembly syntax is under
  /// discussion for future inclusion.
  bool AllowAutoPadding = false;

protected:
  /// True if this streamer is emitting an object file rather than assembly.
  bool IsObj = false;

  /// Symbol of the current epilog for which we are processing SEH directives.
  WinEH::FrameInfo::Epilog *CurrentWinEpilog = nullptr;

  /// Fragment currently being appended to.
  MCFragment *CurFrag = nullptr;

  /// DWARF CFI frames collected while streaming.
  SmallVector<MCDwarfFrameInfo, 0> DwarfFrameInfos;

  /// Construct a streamer bound to \p Ctx.
  /// \param Ctx - MC context that owns symbols and sections.
  MCStreamer(MCContext &Ctx);

  /// This is called by popSection and switchSection, if the current
  /// section changes.
  /// \param Section - Section being switched to.
  /// \param Subsec - Subsection index within \p Section.
  virtual void changeSection(MCSection *Section, uint32_t Subsec);

  /// Append fragment \p F to the current section.
  /// \param F - Fragment to append.
  void addFragment(MCFragment *F);

  /// Target-specific implementation of .cfi_startproc.
  /// \param Frame - Frame info being started.
  virtual void emitCFIStartProcImpl(MCDwarfFrameInfo &Frame);
  /// Target-specific implementation of .cfi_endproc.
  /// \param CurFrame - Frame info being ended.
  virtual void emitCFIEndProcImpl(MCDwarfFrameInfo &CurFrame);

  /// Return the current Windows EH frame, or null if none is active.
  /// \return The current Windows EH frame, or null if none is active.
  WinEH::FrameInfo *getCurrentWinFrameInfo() {
    return CurrentWinFrameInfo;
  }

  /// Emit Windows unwind tables for a single frame.
  /// \param Frame - Frame whose unwind tables should be emitted.
  virtual void emitWindowsUnwindTables(WinEH::FrameInfo *Frame);

  /// Emit Windows unwind tables for all collected frames.
  virtual void emitWindowsUnwindTables();

  /// Implementation of emitRawText, used by assembly streamers.
  /// \param String - Text to write to the .s file.
  virtual void emitRawTextImpl(StringRef String);

  /// Returns true if the .cv_loc directive is in the right section.
  /// \param FuncId - CodeView function id of the .cv_loc.
  /// \param Loc - Source location for diagnostics.
  /// \return True if the .cv_loc is valid for the current section.
  bool checkCVLocSection(unsigned FuncId, SMLoc Loc);

  /// Optional LFI rewriter applied to emitted instructions.
  std::unique_ptr<MCLFIRewriter> LFIRewriter;

public:
  /// Deleted copy constructor.
  ///
  /// \param Other - Unused; copy construction is deleted.
  MCStreamer(const MCStreamer &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other - Unused; copy assignment is deleted.
  MCStreamer &operator=(const MCStreamer &Other) = delete;
  /// Destroy the streamer.
  virtual ~MCStreamer();

  /// Visit symbols referenced by \p Expr.
  /// \param Expr - Expression whose used symbols are visited.
  void visitUsedExpr(const MCExpr &Expr);
  /// Visit a symbol used by a streamed expression or directive.
  /// \param Sym - Symbol that was used.
  virtual void visitUsedSymbol(const MCSymbol &Sym);

  /// Take ownership of target streamer \p TS.
  /// \param TS - Target streamer to attach; ownership is transferred.
  void setTargetStreamer(MCTargetStreamer *TS) {
    TargetStreamer.reset(TS);
  }

  /// Point at the parser's current token location for diagnostics.
  /// \param Loc - Pointer to the parser SMLoc, or null.
  void setStartTokLocPtr(const SMLoc *Loc) { StartTokLocPtr = Loc; }
  /// Return the parser's current token location, if available.
  /// \return The parser's current token location, or an empty SMLoc.
  SMLoc getStartTokLoc() const {
    return StartTokLocPtr ? *StartTokLocPtr : SMLoc();
  }

  /// Take ownership of LFI rewriter \p Rewriter.
  /// \param Rewriter - Rewriter to install, or null to clear.
  void setLFIRewriter(std::unique_ptr<MCLFIRewriter> Rewriter);

  /// Return the installed LFI rewriter, or null if none.
  /// \return The installed LFI rewriter, or null if none.
  MCLFIRewriter *getLFIRewriter() { return LFIRewriter.get(); }

  /// State management
  ///
  virtual void reset();

  /// Return the MC context this streamer is bound to.
  /// \return The MC context this streamer is bound to.
  MCContext &getContext() const { return Context; }
  /// Return true if this streamer emits an object file.
  /// \return True if this streamer emits an object file.
  bool isObj() const { return IsObj; }

  // MCObjectStreamer has an MCAssembler and allows more expression folding at
  // parse time.
  /// Return the assembler, or null if this streamer has none.
  /// \return The assembler, or null if this streamer has none.
  virtual MCAssembler *getAssemblerPtr() { return nullptr; }

  /// Set whether assembler info may be used during parsing.
  /// \param v - True to allow folding with assembler info at parse time.
  void setUseAssemblerInfoForParsing(bool v) { UseAssemblerInfoForParsing = v; }
  /// Return whether assembler info may be used during parsing.
  /// \return True if assembler info may be used during parsing.
  bool getUseAssemblerInfoForParsing() { return UseAssemblerInfoForParsing; }

  /// Return the attached target streamer, or null if none.
  /// \return The attached target streamer, or null if none.
  MCTargetStreamer *getTargetStreamer() {
    return TargetStreamer.get();
  }

  /// Set whether the assembler may insert padding automatically.
  /// \param v - True if automatic padding is allowed.
  void setAllowAutoPadding(bool v) { AllowAutoPadding = v; }
  /// Return whether the assembler may insert padding automatically.
  /// \return True if automatic padding is allowed.
  bool getAllowAutoPadding() const { return AllowAutoPadding; }

  /// Emit a temporary label and record it in the DWARF line table.
  /// \return The temporary label recorded in the DWARF line table.
  MCSymbol *emitLineTableLabel();

  /// When emitting an object file, create and emit a real label. When emitting
  /// textual assembly, this should do nothing to avoid polluting our output.
  /// \return A label for the current CFI position, or a dummy non-null value
  /// when emitting textual assembly.
  virtual MCSymbol *emitCFILabel();

  /// Retrieve the current frame info if one is available and it is not yet
  /// closed. Otherwise, issue an error and return null.
  /// \param Loc - Source location for diagnostics.
  /// \return The current Windows EH frame, or null on error.
  WinEH::FrameInfo *EnsureValidWinFrameInfo(SMLoc Loc);

  /// Return the number of DWARF CFI frames collected so far.
  /// \return The number of DWARF CFI frames collected so far.
  unsigned getNumFrameInfos();
  /// Return the collected DWARF CFI frames.
  /// \return The collected DWARF CFI frames.
  ArrayRef<MCDwarfFrameInfo> getDwarfFrameInfos() const;

  /// Return true if a DWARF CFI frame has been started but not ended.
  /// \return True if a DWARF CFI frame is unfinished.
  bool hasUnfinishedDwarfFrameInfo();

  /// Return the number of Windows EH frames collected so far.
  /// \return The number of Windows EH frames collected so far.
  unsigned getNumWinFrameInfos() { return WinFrameInfos.size(); }
  /// Return the collected Windows EH frames.
  /// \return The collected Windows EH frames.
  ArrayRef<std::unique_ptr<WinEH::FrameInfo>> getWinFrameInfos() const {
    return WinFrameInfos;
  }

  /// Return the Windows EH epilog currently being processed, or null.
  /// \return The current Windows EH epilog, or null if none.
  WinEH::FrameInfo::Epilog *getCurrentWinEpilog() const {
    return CurrentWinEpilog;
  }

  /// Return true if SEH directives are being recorded for an epilog.
  /// \return True if SEH directives are being recorded for an epilog.
  bool isInEpilogCFI() const { return CurrentWinEpilog; }

  /// Returns true if a WinCFI prolog has been completed (.seh_endprologue)
  /// in the current frame.
  /// \return True if the current frame's prolog has ended.
  bool isWinCFIPrologEnded() const {
    return CurrentWinFrameInfo && !CurrentWinFrameInfo->End &&
           CurrentWinFrameInfo->PrologEnd;
  }

  /// \name Assembly File Formatting.
  /// @{

  /// Return true if this streamer supports verbose assembly and if it is
  /// enabled.
  /// \return True if verbose assembly output is enabled.
  virtual bool isVerboseAsm() const { return false; }

  /// Return true if this asm streamer supports emitting unformatted text
  /// to the .s file with EmitRawText.
  /// \return True if EmitRawText is supported.
  virtual bool hasRawTextSupport() const { return false; }

  /// Is the integrated assembler required for this streamer to function
  /// correctly?
  /// \return True if the integrated assembler is required.
  virtual bool isIntegratedAssemblerRequired() const { return false; }

  /// Add a textual comment.
  ///
  /// Typically for comments that can be emitted to the generated .s
  /// file if applicable as a QoI issue to make the output of the compiler
  /// more readable.  This only affects the MCAsmStreamer, and only when
  /// verbose assembly output is enabled.
  ///
  /// If the comment includes embedded \n's, they will each get the comment
  /// prefix as appropriate.  The added comment should not end with a \n.
  /// By default, each comment is terminated with an end of line, i.e. the
  /// EOL param is set to true by default. If one prefers not to end the
  /// comment with a new line then the EOL param should be passed
  /// with a false value.
  /// \param T - Comment text; should not end with a newline.
  /// \param EOL - True to terminate the comment with a newline.
  virtual void AddComment(const Twine &T, bool EOL = true) {}

  /// Return a raw_ostream that comments can be written to. Unlike
  /// AddComment, you are required to terminate comments with \n if you use this
  /// method.
  /// \return A stream suitable for writing comments, terminated with newlines.
  virtual raw_ostream &getCommentOS();

  /// Print \p T immediately as a comment, optionally tab-prefixed.
  ///
  /// Print T and prefix it with the comment string (normally #) and
  /// optionally a tab. This prints the comment immediately, not at the end of
  /// the current line. It is basically a safe version of EmitRawText: since it
  /// only prints comments, the object streamer ignores it instead of asserting.
  /// \param T - Comment text to print.
  /// \param TabPrefix - True to prefix the comment with a tab.
  virtual void emitRawComment(const Twine &T, bool TabPrefix = true);

  /// Add explicit comment T. T is required to be a valid
  /// comment in the output and does not need to be escaped.
  /// \param T - Comment text already valid for the output syntax.
  virtual void addExplicitComment(const Twine &T);

  /// Emit added explicit comments.
  virtual void emitExplicitComments();

  /// Emit a blank line to a .s file to pretty it up.
  virtual void addBlankLine() {}

  /// @}

  /// \name Symbol & Section Management
  /// @{

  /// Return the current section that the streamer is emitting code to.
  /// \return The current section and subsection pair.
  MCSectionSubPair getCurrentSection() const {
    if (!SectionStack.empty())
      return SectionStack.back().first;
    return MCSectionSubPair();
  }
  /// Return only the current section, ignoring the subsection index.
  /// \return The current section, without the subsection index.
  MCSection *getCurrentSectionOnly() const {
    return CurFrag->getParent();
  }

  /// Return the previous section that the streamer is emitting code to.
  /// \return The previous section and subsection pair.
  MCSectionSubPair getPreviousSection() const {
    if (!SectionStack.empty())
      return SectionStack.back().second;
    return MCSectionSubPair();
  }

  /// Return the fragment currently being appended to.
  /// \return The fragment currently being appended to.
  MCFragment *getCurrentFragment() const {
    // Ensure consistency with the section stack.
    assert(!getCurrentSection().first ||
           CurFrag->getParent() == getCurrentSection().first);
    // Ensure we eagerly allocate an empty fragment after adding fragment with a
    // variable-size tail.
    assert(!CurFrag || CurFrag->getKind() == MCFragment::FT_Data);
    return CurFrag;
  }
  /// Return the fixed size of the current data fragment.
  /// \return The fixed size of the current data fragment in bytes.
  size_t getCurFragSize() const { return getCurrentFragment()->getFixedSize(); }
  /// Save the current and previous section on the section stack.
  void pushSection() {
    SectionStack.push_back(
        std::make_pair(getCurrentSection(), getPreviousSection()));
  }

  /// Restore the current and previous section from the section stack.
  /// Calls changeSection as needed.
  ///
  /// Returns false if the stack was empty.
  /// \return False if the section stack was empty; true otherwise.
  virtual bool popSection();

  /// Set the current section where code is being emitted to \p Section.  This
  /// is required to update CurSection.
  ///
  /// This corresponds to assembler directives like .section, .text, etc.
  /// \param Section - Section to switch to.
  /// \param Subsec - Subsection index within \p Section.
  virtual void switchSection(MCSection *Section, uint32_t Subsec = 0);
  /// Switch to \p Section using a subsection expression.
  /// \param Section - Section to switch to.
  /// \param Subsec - Subsection expression, or null for subsection 0.
  /// \return True on error; false on success.
  bool switchSection(MCSection *Section, const MCExpr *Subsec);

  /// Similar to switchSection, but does not print the section directive.
  /// \param Section - Section to switch to.
  void switchSectionNoPrint(MCSection *Section);

  /// Create the default sections and set the initial one.
  /// \param STI - Subtarget info used to initialize sections.
  virtual void initSections(const MCSubtargetInfo &STI);

  /// End \p Section and return its end symbol.
  /// \param Section - Section to end.
  /// \return The end symbol for \p Section.
  MCSymbol *endSection(MCSection *Section);

  /// Returns the mnemonic for \p MI, if the streamer has access to a
  /// instruction printer and returns an empty string otherwise.
  /// \param MI - Instruction whose mnemonic is requested.
  /// \return The mnemonic for \p MI, or an empty string if unavailable.
  virtual StringRef getMnemonic(const MCInst &MI) const { return ""; }

  /// Emit a label for \p Symbol into the current section.
  ///
  /// This corresponds to an assembler statement such as:
  ///   foo:
  ///
  /// \param Symbol - The symbol to emit. A given symbol should only be
  /// emitted as a label once, and symbols emitted as a label should never be
  /// used in an assignment.
  /// \param Loc - Source location for diagnostics.
  // FIXME: These emission are non-const because we mutate the symbol to
  // add the section we're emitting it to later.
  virtual void emitLabel(MCSymbol *Symbol, SMLoc Loc = SMLoc());

  /// Copy EH-related symbol attributes from \p Symbol to \p EHSymbol.
  /// \param Symbol - Original symbol whose attributes are copied.
  /// \param EHSymbol - EH symbol that receives the attributes.
  virtual void emitEHSymAttributes(const MCSymbol *Symbol, MCSymbol *EHSymbol);

  /// Emit a .subsection_via_symbols directive.
  virtual void emitSubsectionsViaSymbols();

  /// Emit the given list \p Options of strings as linker
  /// options into the output.
  /// \param Kind - Linker option strings to emit.
  virtual void emitLinkerOptions(ArrayRef<std::string> Kind) {}

  /// Note in the output the specified region \p Kind.
  /// \param Kind - Data-region kind to record.
  virtual void emitDataRegion(MCDataRegionType Kind) {}

  /// Specify the Mach-O minimum deployment target version.
  /// \param Type - Version-min directive kind.
  /// \param Major - Major version number.
  /// \param Minor - Minor version number.
  /// \param Update - Update version number.
  /// \param SDKVersion - SDK version tuple.
  virtual void emitVersionMin(MCVersionMinType Type, unsigned Major,
                              unsigned Minor, unsigned Update,
                              VersionTuple SDKVersion) {}

  /// Emit/Specify Mach-O build version command.
  /// \p Platform should be one of MachO::PlatformType.
  /// \param Platform - Mach-O platform identifier.
  /// \param Major - Major version number.
  /// \param Minor - Minor version number.
  /// \param Update - Update version number.
  /// \param SDKVersion - SDK version tuple.
  virtual void emitBuildVersion(unsigned Platform, unsigned Major,
                                unsigned Minor, unsigned Update,
                                VersionTuple SDKVersion) {}

  /// Emit a Mach-O build-version command for the Darwin target variant.
  /// \param Platform - Mach-O platform identifier.
  /// \param Major - Major version number.
  /// \param Minor - Minor version number.
  /// \param Update - Update version number.
  /// \param SDKVersion - SDK version tuple.
  virtual void emitDarwinTargetVariantBuildVersion(unsigned Platform,
                                                   unsigned Major,
                                                   unsigned Minor,
                                                   unsigned Update,
                                                   VersionTuple SDKVersion) {}

  /// Emit the target triple into the output.
  /// \param TargetTriple - Target triple string.
  virtual void emitTargetTriple(StringRef TargetTriple) {}

  /// Emit version directives appropriate for \p Target.
  /// \param Target - Primary target triple.
  /// \param SDKVersion - SDK version for \p Target.
  /// \param DarwinTargetVariantTriple - Optional Darwin target-variant triple.
  /// \param DarwinTargetVariantSDKVersion - SDK version for the variant triple.
  void emitVersionForTarget(const Triple &Target,
                            const VersionTuple &SDKVersion,
                            const Triple *DarwinTargetVariantTriple,
                            const VersionTuple &DarwinTargetVariantSDKVersion);

  /// Emit an assignment of \p Value to \p Symbol.
  ///
  /// This corresponds to an assembler statement such as:
  ///  symbol = value
  ///
  /// The assignment generates no code, but has the side effect of binding the
  /// value in the current context. For the assembly streamer, this prints the
  /// binding into the .s file.
  ///
  /// \param Symbol - The symbol being assigned to.
  /// \param Value - The value for the symbol.
  virtual void emitAssignment(MCSymbol *Symbol, const MCExpr *Value);

  /// Emit an assignment of \p Value to \p Symbol, but only if \p Value is also
  /// emitted.
  /// \param Symbol - The symbol being assigned to.
  /// \param Value - The value for the symbol.
  virtual void emitConditionalAssignment(MCSymbol *Symbol, const MCExpr *Value);

  /// Emit an weak reference from \p Alias to \p Symbol.
  ///
  /// This corresponds to an assembler statement such as:
  ///  .weakref alias, symbol
  ///
  /// \param Alias - The alias that is being created.
  /// \param Symbol - The symbol being aliased.
  virtual void emitWeakReference(MCSymbol *Alias, const MCSymbol *Symbol);

  /// Add the given \p Attribute to \p Symbol.
  /// \param Symbol - Symbol to attribute.
  /// \param Attribute - Attribute to add.
  /// \return True if the attribute was applied.
  virtual bool emitSymbolAttribute(MCSymbol *Symbol,
                                   MCSymbolAttr Attribute) = 0;

  /// Set the \p DescValue for the \p Symbol.
  ///
  /// \param Symbol - The symbol to have its n_desc field set.
  /// \param DescValue - The value to set into the n_desc field.
  virtual void emitSymbolDesc(MCSymbol *Symbol, unsigned DescValue);

  /// Start emitting COFF symbol definition
  ///
  /// \param Symbol - The symbol to have its External & Type fields set.
  virtual void beginCOFFSymbolDef(const MCSymbol *Symbol);

  /// Emit the storage class of the symbol.
  ///
  /// \param StorageClass - The storage class the symbol should have.
  virtual void emitCOFFSymbolStorageClass(int StorageClass);

  /// Emit the type of the symbol.
  ///
  /// \param Type - A COFF type identifier (see COFF::SymbolType in X86COFF.h)
  virtual void emitCOFFSymbolType(int Type);

  /// Marks the end of the symbol definition.
  virtual void endCOFFSymbolDef();

  /// Emit a COFF .safeseh directive for \p Symbol.
  /// \param Symbol - Symbol registered as SafeSEH-compatible.
  virtual void emitCOFFSafeSEH(MCSymbol const *Symbol);

  /// Emits the symbol table index of a Symbol into the current section.
  /// \param Symbol - Symbol whose table index is emitted.
  virtual void emitCOFFSymbolIndex(MCSymbol const *Symbol);

  /// Emits a COFF section index.
  ///
  /// \param Symbol - Symbol the section number relocation should point to.
  virtual void emitCOFFSectionIndex(MCSymbol const *Symbol);

  /// Emits a COFF section relative relocation.
  ///
  /// \param Symbol - Symbol the section relative relocation should point to.
  /// \param Offset - Offset from \p Symbol to apply to the relocation.
  virtual void emitCOFFSecRel32(MCSymbol const *Symbol, uint64_t Offset);

  /// Emits a COFF image relative relocation.
  ///
  /// \param Symbol - Symbol the image relative relocation should point to.
  /// \param Offset - Offset from \p Symbol to apply to the relocation.
  virtual void emitCOFFImgRel32(MCSymbol const *Symbol, int64_t Offset);

  /// Emits the physical number of the section containing the given symbol as
  /// assigned during object writing (i.e., this is not a runtime relocation).
  /// \param Symbol - Symbol whose containing section number is emitted.
  virtual void emitCOFFSecNumber(MCSymbol const *Symbol);

  /// Emits the offset of the symbol from the beginning of the section during
  /// object writing (i.e., this is not a runtime relocation).
  /// \param Symbol - Symbol whose section offset is emitted.
  virtual void emitCOFFSecOffset(MCSymbol const *Symbol);

  /// Emits an lcomm directive with XCOFF csect information.
  ///
  /// \param LabelSym - Label on the block of storage.
  /// \param Size - The size of the block of storage.
  /// \param CsectSym - Csect name for the block of storage.
  /// \param Alignment - The alignment of the symbol in bytes.
  virtual void emitXCOFFLocalCommonSymbol(MCSymbol *LabelSym, uint64_t Size,
                                          MCSymbol *CsectSym, Align Alignment);

  /// Emit a symbol's linkage and visibility with a linkage directive for XCOFF.
  ///
  /// \param Symbol - The symbol to emit.
  /// \param Linkage - The linkage of the symbol to emit.
  /// \param Visibility - The visibility of the symbol to emit or MCSA_Invalid
  /// if the symbol does not have an explicit visibility.
  virtual void emitXCOFFSymbolLinkageWithVisibility(MCSymbol *Symbol,
                                                    MCSymbolAttr Linkage,
                                                    MCSymbolAttr Visibility);

  /// Emit a XCOFF .rename directive which creates a synonym for an illegal or
  /// undesirable name.
  ///
  /// \param Name - The name used internally in the assembly for references to
  /// the symbol.
  /// \param Rename - The value to which the Name parameter is
  /// changed at the end of assembly.
  virtual void emitXCOFFRenameDirective(const MCSymbol *Name, StringRef Rename);

  /// Emit an XCOFF .except directive which adds information about
  /// a trap instruction to the object file exception section
  ///
  /// \param Symbol - The function containing the trap.
  /// \param Trap - The trap-instruction symbol.
  /// \param Lang - The language code for the exception entry.
  /// \param Reason - The reason code for the exception entry.
  /// \param FunctionSize - Size of the function containing the trap.
  /// \param hasDebug - True if the function has debug information.
  virtual void emitXCOFFExceptDirective(const MCSymbol *Symbol,
                                        const MCSymbol *Trap,
                                        unsigned Lang, unsigned Reason,
                                        unsigned FunctionSize, bool hasDebug);

  /// Emit a XCOFF .ref directive which creates R_REF type entry in the
  /// relocation table for one or more symbols.
  ///
  /// \param Symbol - The symbol on the .ref directive.
  virtual void emitXCOFFRefDirective(const MCSymbol *Symbol);

  /// Emit a C_INFO symbol with XCOFF embedded metadata to the .info section.
  ///
  /// \param Name - The embedded metadata name
  /// \param Metadata - The embedded metadata
  virtual void emitXCOFFCInfoSym(StringRef Name, StringRef Metadata);

  /// Emit an ELF .size directive.
  ///
  /// This corresponds to an assembler statement such as:
  ///  .size symbol, expression
  /// \param Symbol - Symbol whose size is set.
  /// \param Value - Expression giving the symbol size.
  virtual void emitELFSize(MCSymbol *Symbol, const MCExpr *Value);

  /// Emit an ELF .symver directive.
  ///
  /// This corresponds to an assembler statement such as:
  ///  .symver _start, foo@@SOME_VERSION
  /// \param OriginalSym - Original symbol being versioned.
  /// \param Name - Versioned name, such as "foo@@SOME_VERSION".
  /// \param KeepOriginalSym - True to also keep the original symbol.
  virtual void emitELFSymverDirective(const MCSymbol *OriginalSym,
                                      StringRef Name, bool KeepOriginalSym);

  /// Emit a Linker Optimization Hint (LOH) directive.
  /// \param Kind - LOH kind.
  /// \param Args - Arguments of the LOH.
  virtual void emitLOHDirective(MCLOHType Kind, const MCLOHArgs &Args) {}

  /// Emit a .gnu_attribute directive.
  /// \param Tag - GNU attribute tag.
  /// \param Value - Integer value of the attribute.
  virtual void emitGNUAttribute(unsigned Tag, unsigned Value) {}

  /// Emit a common symbol.
  ///
  /// \param Symbol - The common symbol to emit.
  /// \param Size - The size of the common symbol.
  /// \param ByteAlignment - The alignment of the symbol.
  virtual void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                Align ByteAlignment) = 0;

  /// Emit a local common (.lcomm) symbol.
  ///
  /// \param Symbol - The common symbol to emit.
  /// \param Size - The size of the common symbol.
  /// \param ByteAlignment - The alignment of the common symbol in bytes.
  virtual void emitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                     Align ByteAlignment);

  /// Emit the zerofill section and an optional symbol.
  ///
  /// \param Section - The zerofill section to create and or to put the symbol
  /// \param Symbol - The zerofill symbol to emit, if non-NULL.
  /// \param Size - The size of the zerofill symbol.
  /// \param ByteAlignment - The alignment of the zerofill symbol.
  /// \param Loc - Source location for diagnostics.
  virtual void emitZerofill(MCSection *Section, MCSymbol *Symbol = nullptr,
                            uint64_t Size = 0, Align ByteAlignment = Align(1),
                            SMLoc Loc = SMLoc());

  /// Emit a thread local bss (.tbss) symbol.
  ///
  /// \param Section - The thread local common section.
  /// \param Symbol - The thread local common symbol to emit.
  /// \param Size - The size of the symbol.
  /// \param ByteAlignment - The alignment of the thread local common symbol.
  virtual void emitTBSSSymbol(MCSection *Section, MCSymbol *Symbol,
                              uint64_t Size, Align ByteAlignment = Align(1));

  /// @}
  /// \name Generating Data
  /// @{

  /// Emit the bytes in \p Data into the output.
  ///
  /// This is used to implement assembler directives such as .byte, .ascii,
  /// etc.
  /// \param Data - Bytes to emit.
  virtual void emitBytes(StringRef Data);

  /// Functionally identical to EmitBytes. When emitting textual assembly, this
  /// method uses .byte directives instead of .ascii or .asciz for readability.
  /// \param Data - Bytes to emit.
  virtual void emitBinaryData(StringRef Data);

  /// Emit the expression \p Value into the output as a native
  /// integer of the given \p Size bytes.
  ///
  /// This is used to implement assembler directives such as .word, .quad,
  /// etc.
  ///
  /// \param Value - The value to emit.
  /// \param Size - The size of the integer (in bytes) to emit. This must
  /// match a native machine width.
  /// \param Loc - The location of the expression for error reporting.
  virtual void emitValueImpl(const MCExpr *Value, unsigned Size,
                             SMLoc Loc = SMLoc());

  /// Emit the expression \p Value as a native integer of \p Size bytes.
  /// \param Value - The value to emit.
  /// \param Size - The size of the integer (in bytes) to emit.
  /// \param Loc - The location of the expression for error reporting.
  void emitValue(const MCExpr *Value, unsigned Size, SMLoc Loc = SMLoc());

  /// Special case of EmitValue that avoids the client having
  /// to pass in a MCExpr for constant integers.
  /// \param Value - Integer value to emit.
  /// \param Size - Size of the integer (in bytes) to emit.
  virtual void emitIntValue(uint64_t Value, unsigned Size);
  /// Emit the bits of \p Value as a native integer.
  /// \param Value - APInt whose bit width determines the emitted size.
  virtual void emitIntValue(const APInt &Value);

  /// Special case of EmitValue that avoids the client having to pass
  /// in a MCExpr for constant integers & prints in Hex format for certain
  /// modes.
  /// \param Value - Integer value to emit.
  /// \param Size - Size of the integer (in bytes) to emit.
  virtual void emitIntValueInHex(uint64_t Value, unsigned Size) {
    emitIntValue(Value, Size);
  }

  /// Emit \p Value as a one-byte integer.
  /// \param Value - Integer value to emit.
  void emitInt8(uint64_t Value) { emitIntValue(Value, 1); }
  /// Emit \p Value as a two-byte integer.
  /// \param Value - Integer value to emit.
  void emitInt16(uint64_t Value) { emitIntValue(Value, 2); }
  /// Emit \p Value as a four-byte integer.
  /// \param Value - Integer value to emit.
  void emitInt32(uint64_t Value) { emitIntValue(Value, 4); }
  /// Emit \p Value as an eight-byte integer.
  /// \param Value - Integer value to emit.
  void emitInt64(uint64_t Value) { emitIntValue(Value, 8); }

  /// Emit a constant integer in hex, padded to \p Size bytes.
  ///
  /// Special case of EmitValue that avoids the client having to pass in a
  /// MCExpr for constant integers & prints in Hex format for certain modes,
  /// pads the field with leading zeros to Size width.
  /// \param Value - Integer value to emit.
  /// \param Size - Field width in bytes, including leading-zero padding.
  virtual void emitIntValueInHexWithPadding(uint64_t Value, unsigned Size) {
    emitIntValue(Value, Size);
  }

  /// Emit \p Value encoded as unsigned LEB128.
  /// \param Value - Expression to encode.
  virtual void emitULEB128Value(const MCExpr *Value);

  /// Emit \p Value encoded as signed LEB128.
  /// \param Value - Expression to encode.
  virtual void emitSLEB128Value(const MCExpr *Value);

  /// Special case of EmitULEB128Value that avoids the client having to
  /// pass in a MCExpr for constant integers.
  /// \param Value - Integer value to encode.
  /// \param PadTo - Minimum encoded size in bytes, or 0 for no padding.
  /// \return The number of bytes emitted.
  unsigned emitULEB128IntValue(uint64_t Value, unsigned PadTo = 0);

  /// Special case of EmitSLEB128Value that avoids the client having to
  /// pass in a MCExpr for constant integers.
  /// \param Value - Integer value to encode.
  /// \return The number of bytes emitted.
  unsigned emitSLEB128IntValue(int64_t Value);

  /// Special case of EmitValue that avoids the client having to pass in
  /// a MCExpr for MCSymbols.
  /// \param Sym - Symbol whose address is emitted.
  /// \param Size - Size of the address (in bytes) to emit.
  /// \param IsSectionRelative - True to emit a section-relative address.
  void emitSymbolValue(const MCSymbol *Sym, unsigned Size,
                       bool IsSectionRelative = false);

  /// Emit NumBytes bytes worth of the value specified by FillValue.
  /// This implements directives such as '.space'.
  /// \param NumBytes - Number of bytes to emit.
  /// \param FillValue - Byte value used to fill.
  void emitFill(uint64_t NumBytes, uint8_t FillValue);

  /// Emit \p Size bytes worth of the value specified by \p FillValue.
  ///
  /// This is used to implement assembler directives such as .space or .skip.
  ///
  /// \param NumBytes - The number of bytes to emit.
  /// \param FillValue - The value to use when filling bytes.
  /// \param Loc - The location of the expression for error reporting.
  virtual void emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                        SMLoc Loc = SMLoc());

  /// Emit \p NumValues copies of \p Size bytes. Each \p Size bytes is
  /// taken from the lowest order 4 bytes of \p Expr expression.
  ///
  /// This is used to implement assembler directives such as .fill.
  ///
  /// \param NumValues - The number of copies of \p Size bytes to emit.
  /// \param Size - The size (in bytes) of each repeated value.
  /// \param Expr - The expression from which \p Size bytes are used.
  /// \param Loc - The location of the expression for error reporting.
  virtual void emitFill(const MCExpr &NumValues, int64_t Size, int64_t Expr,
                        SMLoc Loc = SMLoc());

  /// Emit \p NumBytes bytes of NOP instructions.
  /// \param NumBytes - Number of bytes of NOPs to emit.
  /// \param ControlledNopLength - Preferred NOP length, or 0 for the default.
  /// \param Loc - Source location for diagnostics.
  /// \param STI - Subtarget info used to choose NOP encodings.
  virtual void emitNops(int64_t NumBytes, int64_t ControlledNopLength,
                        SMLoc Loc, const MCSubtargetInfo& STI);

  /// Emit NumBytes worth of zeros.
  /// This function properly handles data in virtual sections.
  /// \param NumBytes - Number of zero bytes to emit.
  void emitZeros(uint64_t NumBytes);

  /// Emit some number of copies of \p Value until the byte alignment \p
  /// ByteAlignment is reached.
  ///
  /// If the number of bytes need to emit for the alignment is not a multiple
  /// of \p ValueSize, then the contents of the emitted fill bytes is
  /// undefined.
  ///
  /// This used to implement the .align assembler directive.
  ///
  /// \param Alignment - The alignment to reach.
  /// \param Fill - The value to use when filling bytes.
  /// \param FillLen - The size of the integer (in bytes) to emit for
  /// \p Value. This must match a native machine width.
  /// \param MaxBytesToEmit - The maximum numbers of bytes to emit, or 0. If
  /// the alignment cannot be reached in this many bytes, no bytes are
  /// emitted.
  virtual void emitValueToAlignment(Align Alignment, int64_t Fill = 0,
                                    uint8_t FillLen = 1,
                                    unsigned MaxBytesToEmit = 0);

  /// Emit nops until the byte alignment \p ByteAlignment is reached.
  ///
  /// This used to align code where the alignment bytes may be executed.  This
  /// can emit different bytes for different sizes to optimize execution.
  ///
  /// \param Alignment - The alignment to reach.
  /// \param STI - The MCSubtargetInfo in operation when padding is emitted.
  /// \param MaxBytesToEmit - The maximum numbers of bytes to emit, or 0. If
  /// the alignment cannot be reached in this many bytes, no bytes are
  /// emitted.
  virtual void emitCodeAlignment(Align Alignment, const MCSubtargetInfo &STI,
                                 unsigned MaxBytesToEmit = 0);

  /// Align up to \p A using NOPs or fill, not past symbol \p End.
  /// \param A - Preferred alignment to reach.
  /// \param End - Symbol that must not be passed while padding.
  /// \param EmitNops - True to pad with NOPs rather than \p Fill.
  /// \param Fill - Fill byte used when \p EmitNops is false.
  /// \param STI - Subtarget info used when emitting NOPs.
  virtual void emitPrefAlign(Align A, const MCSymbol &End, bool EmitNops,
                             uint8_t Fill, const MCSubtargetInfo &STI);

  /// Emit some number of copies of \p Value until the byte offset \p
  /// Offset is reached.
  ///
  /// This is used to implement assembler directives such as .org.
  ///
  /// \param Offset - The offset to reach. This may be an expression, but the
  /// expression must be associated with the current section.
  /// \param Value - The value to use when filling bytes.
  /// \param Loc - The location of the expression for error reporting.
  virtual void emitValueToOffset(const MCExpr *Offset, unsigned char Value,
                                 SMLoc Loc);

  /// @}

  /// Switch to a new logical file.  This is used to implement the '.file
  /// "foo.c"' assembler directive.
  /// \param Filename - Logical source file name.
  virtual void emitFileDirective(StringRef Filename);

  /// Emit ".file assembler diretive with additioal info.
  /// \param Filename - Logical source file name.
  /// \param CompilerVersion - Compiler version string.
  /// \param TimeStamp - Timestamp string.
  /// \param Description - Additional description string.
  virtual void emitFileDirective(StringRef Filename, StringRef CompilerVersion,
                                 StringRef TimeStamp, StringRef Description);

  /// Emit the "identifiers" directive.  This implements the
  /// '.ident "version foo"' assembler directive.
  /// \param IdentString - Identification string to emit.
  virtual void emitIdent(StringRef IdentString) {}

  /// Associate a filename with a specified logical file number.  This
  /// implements the DWARF2 '.file 4 "foo.c"' assembler directive.
  /// \param FileNo - Logical file number.
  /// \param Directory - Directory component of the file path.
  /// \param Filename - File name component of the file path.
  /// \param Checksum - Optional MD5 checksum of the source file.
  /// \param Source - Optional embedded source text.
  /// \param CUID - DWARF compile-unit id.
  /// \return The assigned logical file number.
  unsigned emitDwarfFileDirective(
      unsigned FileNo, StringRef Directory, StringRef Filename,
      std::optional<MD5::MD5Result> Checksum = std::nullopt,
      std::optional<StringRef> Source = std::nullopt, unsigned CUID = 0) {
    return cantFail(
        tryEmitDwarfFileDirective(FileNo, Directory, Filename, Checksum,
                                  Source, CUID));
  }

  /// Associate a filename with a specified logical file number.
  ///
  /// Also associate a directory, optional checksum, and optional source
  /// text with the logical file.  This implements the DWARF2
  /// '.file 4 "dir/foo.c"' assembler directive, and the DWARF5
  /// '.file 4 "dir/foo.c" md5 "..." source "..."' assembler directive.
  /// \param FileNo - Logical file number.
  /// \param Directory - Directory component of the file path.
  /// \param Filename - File name component of the file path.
  /// \param Checksum - Optional MD5 checksum of the source file.
  /// \param Source - Optional embedded source text.
  /// \param CUID - DWARF compile-unit id.
  /// \return The assigned logical file number, or an error on failure.
  virtual Expected<unsigned> tryEmitDwarfFileDirective(
      unsigned FileNo, StringRef Directory, StringRef Filename,
      std::optional<MD5::MD5Result> Checksum = std::nullopt,
      std::optional<StringRef> Source = std::nullopt, unsigned CUID = 0);

  /// Specify the "root" file of the compilation, using the ".file 0" extension.
  /// \param Directory - Directory component of the file path.
  /// \param Filename - File name component of the file path.
  /// \param Checksum - Optional MD5 checksum of the source file.
  /// \param Source - Optional embedded source text.
  /// \param CUID - DWARF compile-unit id.
  virtual void emitDwarfFile0Directive(StringRef Directory, StringRef Filename,
                                       std::optional<MD5::MD5Result> Checksum,
                                       std::optional<StringRef> Source,
                                       unsigned CUID = 0);

  /// Emit a .cfi_b_key_frame directive.
  virtual void emitCFIBKeyFrame();
  /// Emit a .cfi_mte_tagged_frame directive.
  virtual void emitCFIMTETaggedFrame();

  /// This implements the DWARF2 '.loc fileno lineno ...' assembler
  /// directive.
  /// \param FileNo - Logical file number.
  /// \param Line - Source line number.
  /// \param Column - Source column number.
  /// \param Flags - DWARF location flags.
  /// \param Isa - Instruction-set architecture identifier.
  /// \param Discriminator - DWARF discriminator.
  /// \param FileName - File name associated with this location.
  /// \param Comment - Optional comment attached to the directive.
  virtual void emitDwarfLocDirective(unsigned FileNo, unsigned Line,
                                     unsigned Column, unsigned Flags,
                                     unsigned Isa, unsigned Discriminator,
                                     StringRef FileName,
                                     StringRef Comment = {});

  /// This is same as emitDwarfLocDirective, except it has the capability to
  /// add inlined_at information.
  /// \param FileNo - Logical file number of the current location.
  /// \param Line - Source line number of the current location.
  /// \param Column - Source column number of the current location.
  /// \param FileIA - Logical file number of the inlined-at location.
  /// \param LineIA - Source line number of the inlined-at location.
  /// \param ColumnIA - Source column number of the inlined-at location.
  /// \param Sym - Optional symbol associated with this location.
  /// \param Flags - DWARF location flags.
  /// \param Isa - Instruction-set architecture identifier.
  /// \param Discriminator - DWARF discriminator.
  /// \param FileName - File name associated with this location.
  /// \param Comment - Optional comment attached to the directive.
  virtual void emitDwarfLocDirectiveWithInlinedAt(
      unsigned FileNo, unsigned Line, unsigned Column, unsigned FileIA,
      unsigned LineIA, unsigned ColumnIA, const MCSymbol *Sym, unsigned Flags,
      unsigned Isa, unsigned Discriminator, StringRef FileName,
      StringRef Comment = {}) {}

  /// This implements the '.loc_label Name' directive.
  /// \param Loc - Source location for diagnostics.
  /// \param Name - Label name to associate with the current .loc.
  virtual void emitDwarfLocLabelDirective(SMLoc Loc, StringRef Name);

  /// Associate a filename and checksum with a CodeView logical file number.
  ///
  /// Associate a filename with a specified logical file number, and also
  /// specify that file's checksum information.  This implements the '.cv_file
  /// 4 "foo.c"' assembler directive. Returns true on success.
  /// \param FileNo - Logical file number.
  /// \param Filename - Source file name.
  /// \param Checksum - Checksum bytes of the source file.
  /// \param ChecksumKind - CodeView checksum kind identifier.
  /// \return True on success.
  virtual bool emitCVFileDirective(unsigned FileNo, StringRef Filename,
                                   ArrayRef<uint8_t> Checksum,
                                   unsigned ChecksumKind);

  /// Introduces a function id for use with .cv_loc.
  /// \param FunctionId - Function id to introduce.
  /// \return True if the function id was newly recorded.
  virtual bool emitCVFuncIdDirective(unsigned FunctionId);

  /// Introduces an inline call site id for use with .cv_loc. Includes
  /// extra information for inline line table generation.
  /// \param FunctionId - Inline site function id to introduce.
  /// \param IAFunc - Function id of the inlined-at function.
  /// \param IAFile - File number of the inlined-at location.
  /// \param IALine - Line number of the inlined-at location.
  /// \param IACol - Column number of the inlined-at location.
  /// \param Loc - Source location for diagnostics.
  /// \return True if the inline site id was newly recorded.
  virtual bool emitCVInlineSiteIdDirective(unsigned FunctionId, unsigned IAFunc,
                                           unsigned IAFile, unsigned IALine,
                                           unsigned IACol, SMLoc Loc);

  /// This implements the CodeView '.cv_loc' assembler directive.
  /// \param FunctionId - Function id of this location.
  /// \param FileNo - Logical file number.
  /// \param Line - Source line number.
  /// \param Column - Source column number.
  /// \param PrologueEnd - True if this location is a prologue end.
  /// \param IsStmt - True if this location is a recommended breakpoint.
  /// \param FileName - File name associated with this location.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCVLocDirective(unsigned FunctionId, unsigned FileNo,
                                  unsigned Line, unsigned Column,
                                  bool PrologueEnd, bool IsStmt,
                                  StringRef FileName, SMLoc Loc);

  /// This implements the CodeView '.cv_linetable' assembler directive.
  /// \param FunctionId - Function id whose line table is emitted.
  /// \param FnStart - Function start symbol.
  /// \param FnEnd - Function end symbol.
  virtual void emitCVLinetableDirective(unsigned FunctionId,
                                        const MCSymbol *FnStart,
                                        const MCSymbol *FnEnd);

  /// This implements the CodeView '.cv_inline_linetable' assembler
  /// directive.
  /// \param PrimaryFunctionId - Function id of the outermost function.
  /// \param SourceFileId - File id of the inline site.
  /// \param SourceLineNum - Line number of the inline site.
  /// \param FnStartSym - Start symbol of the inlined range.
  /// \param FnEndSym - End symbol of the inlined range.
  virtual void emitCVInlineLinetableDirective(unsigned PrimaryFunctionId,
                                              unsigned SourceFileId,
                                              unsigned SourceLineNum,
                                              const MCSymbol *FnStartSym,
                                              const MCSymbol *FnEndSym);

  /// This implements the CodeView '.cv_def_range' assembler
  /// directive.
  /// \param Ranges - Code ranges this definition covers.
  /// \param FixedSizePortion - Fixed-size def-range header bytes.
  virtual void emitCVDefRangeDirective(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      StringRef FixedSizePortion);

  /// Emit a .cv_def_range with a register-relative header.
  /// \param Ranges - Code ranges this definition covers.
  /// \param DRHdr - Register-relative def-range header.
  virtual void emitCVDefRangeDirective(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      codeview::DefRangeRegisterRelHeader DRHdr);

  /// Emit a .cv_def_range with a subfield-register header.
  /// \param Ranges - Code ranges this definition covers.
  /// \param DRHdr - Subfield-register def-range header.
  virtual void emitCVDefRangeDirective(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      codeview::DefRangeSubfieldRegisterHeader DRHdr);

  /// Emit a .cv_def_range with a register header.
  /// \param Ranges - Code ranges this definition covers.
  /// \param DRHdr - Register def-range header.
  virtual void emitCVDefRangeDirective(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      codeview::DefRangeRegisterHeader DRHdr);

  /// Emit a .cv_def_range with a frame-pointer-relative header.
  /// \param Ranges - Code ranges this definition covers.
  /// \param DRHdr - Frame-pointer-relative def-range header.
  virtual void emitCVDefRangeDirective(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      codeview::DefRangeFramePointerRelHeader DRHdr);

  /// Emit a .cv_def_range with a register-relative indirect header.
  /// \param Ranges - Code ranges this definition covers.
  /// \param DRHdr - Register-relative indirect def-range header.
  virtual void emitCVDefRangeDirective(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      codeview::DefRangeRegisterRelIndirHeader DRHdr);

  /// This implements the CodeView '.cv_stringtable' assembler directive.
  virtual void emitCVStringTableDirective() {}

  /// This implements the CodeView '.cv_filechecksums' assembler directive.
  virtual void emitCVFileChecksumsDirective() {}

  /// This implements the CodeView '.cv_filechecksumoffset' assembler
  /// directive.
  /// \param FileNo - Logical file number whose checksum offset is emitted.
  virtual void emitCVFileChecksumOffsetDirective(unsigned FileNo) {}

  /// This implements the CodeView '.cv_fpo_data' assembler directive.
  /// \param ProcSym - Procedure symbol whose FPO data is emitted.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCVFPOData(const MCSymbol *ProcSym, SMLoc Loc = {}) {}

  /// Emit the absolute difference between two symbols.
  ///
  /// \pre Offset of \c Hi is greater than the offset \c Lo.
  /// \param Hi - Symbol with the greater offset.
  /// \param Lo - Symbol with the lesser offset.
  /// \param Size - Size of the difference (in bytes) to emit.
  virtual void emitAbsoluteSymbolDiff(const MCSymbol *Hi, const MCSymbol *Lo,
                                      unsigned Size);

  /// Emit the absolute difference between two symbols encoded with ULEB128.
  /// \param Hi - Symbol with the greater offset.
  /// \param Lo - Symbol with the lesser offset.
  virtual void emitAbsoluteSymbolDiffAsULEB128(const MCSymbol *Hi,
                                               const MCSymbol *Lo);

  /// Return the DWARF line-table symbol for compile unit \p CUID.
  /// \param CUID - DWARF compile-unit id.
  /// \return The DWARF line-table start symbol for \p CUID.
  virtual MCSymbol *getDwarfLineTableSymbol(unsigned CUID);
  /// Select which CFI sections to emit.
  /// \param EH - True to emit .eh_frame.
  /// \param Debug - True to emit .debug_frame.
  /// \param SFrame - True to emit .sframe.
  virtual void emitCFISections(bool EH, bool Debug, bool SFrame);
  /// Emit a .cfi_startproc directive.
  /// \param IsSimple - True if this is a simple CFI procedure.
  /// \param Loc - Source location for diagnostics.
  void emitCFIStartProc(bool IsSimple, SMLoc Loc = SMLoc());
  /// Emit a .cfi_endproc directive.
  void emitCFIEndProc();
  /// Emit a .cfi_def_cfa directive.
  /// \param Register - CFA register.
  /// \param Offset - Offset from \p Register to the CFA.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFIDefCfa(int64_t Register, int64_t Offset, SMLoc Loc = {});
  /// Emit a .cfi_def_cfa_offset directive.
  /// \param Offset - New CFA offset.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFIDefCfaOffset(int64_t Offset, SMLoc Loc = {});
  /// Emit a .cfi_def_cfa_register directive.
  /// \param Register - New CFA register.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFIDefCfaRegister(int64_t Register, SMLoc Loc = {});
  /// Emit a .cfi_llvm_def_aspace_cfa directive.
  /// \param Register - CFA register.
  /// \param Offset - Offset from \p Register to the CFA.
  /// \param AddressSpace - Address space of the CFA.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFILLVMDefAspaceCfa(int64_t Register, int64_t Offset,
                                       int64_t AddressSpace, SMLoc Loc = {});
  /// Emit a .cfi_offset directive.
  /// \param Register - Register whose saved location is described.
  /// \param Offset - Offset from the CFA of the saved register.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFIOffset(int64_t Register, int64_t Offset, SMLoc Loc = {});
  /// Emit a .cfi_personality directive.
  /// \param Sym - Personality-routine symbol.
  /// \param Encoding - DWARF pointer encoding of \p Sym.
  virtual void emitCFIPersonality(const MCSymbol *Sym, unsigned Encoding);
  /// Emit a .cfi_lsda directive.
  /// \param Sym - LSDA symbol.
  /// \param Encoding - DWARF pointer encoding of \p Sym.
  virtual void emitCFILsda(const MCSymbol *Sym, unsigned Encoding);
  /// Emit a .cfi_remember_state directive.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFIRememberState(SMLoc Loc);
  /// Emit a .cfi_restore_state directive.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFIRestoreState(SMLoc Loc);
  /// Emit a .cfi_same_value directive.
  /// \param Register - Register that is unchanged.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFISameValue(int64_t Register, SMLoc Loc = {});
  /// Emit a .cfi_restore directive.
  /// \param Register - Register restored to its previous rule.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFIRestore(int64_t Register, SMLoc Loc = {});
  /// Emit a .cfi_rel_offset directive.
  /// \param Register - Register whose saved location is described.
  /// \param Offset - Offset from the current CFA register.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFIRelOffset(int64_t Register, int64_t Offset, SMLoc Loc);
  /// Emit a .cfi_adjust_cfa_offset directive.
  /// \param Adjustment - Amount added to the current CFA offset.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFIAdjustCfaOffset(int64_t Adjustment, SMLoc Loc = {});
  /// Emit a .cfi_escape directive with raw DWARF CFI bytes.
  /// \param Values - Raw DWARF CFI opcode bytes.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFIEscape(StringRef Values, SMLoc Loc = {});
  /// Emit a .cfi_return_column directive.
  /// \param Register - Return-address register column.
  virtual void emitCFIReturnColumn(int64_t Register);
  /// Emit a .cfi_gnu_args_size directive.
  /// \param Size - Number of bytes popped by the callee.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFIGnuArgsSize(int64_t Size, SMLoc Loc = {});
  /// Emit a .cfi_signal_frame directive.
  virtual void emitCFISignalFrame();
  /// Emit a .cfi_undefined directive.
  /// \param Register - Register that is not restorable.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFIUndefined(int64_t Register, SMLoc Loc = {});
  /// Emit a .cfi_register directive.
  /// \param Register1 - Register whose value is saved in \p Register2.
  /// \param Register2 - Register holding the saved value.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFIRegister(int64_t Register1, int64_t Register2,
                               SMLoc Loc = {});
  /// Emit a .cfi_window_save directive.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFIWindowSave(SMLoc Loc = {});
  /// Emit a .cfi_negate_ra_state directive.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFINegateRAState(SMLoc Loc = {});
  /// Emit a .cfi_llvm_register_pair directive.
  /// \param Register - Logical register being described.
  /// \param R1 - First physical register of the pair.
  /// \param R1SizeInBits - Size of \p R1 in bits.
  /// \param R2 - Second physical register of the pair.
  /// \param R2SizeInBits - Size of \p R2 in bits.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFILLVMRegisterPair(int64_t Register, int64_t R1,
                                       int64_t R1SizeInBits, int64_t R2,
                                       int64_t R2SizeInBits, SMLoc Loc = {});
  /// Emit a .cfi_llvm_vector_registers directive.
  /// \param Register - Logical register being described.
  /// \param VRs - Physical vector registers and lanes.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFILLVMVectorRegisters(
      int64_t Register, ArrayRef<MCCFIInstruction::VectorRegisterWithLane> VRs,
      SMLoc Loc = {});
  /// Emit a .cfi_llvm_vector_offset directive.
  /// \param Register - Vector register being described.
  /// \param RegisterSizeInBits - Size of \p Register in bits.
  /// \param MaskRegister - Predicate/mask register, or -1 if none.
  /// \param MaskRegisterSizeInBits - Size of \p MaskRegister in bits.
  /// \param Offset - Offset from the CFA of the saved vector.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFILLVMVectorOffset(int64_t Register,
                                       int64_t RegisterSizeInBits,
                                       int64_t MaskRegister,
                                       int64_t MaskRegisterSizeInBits,
                                       int64_t Offset, SMLoc Loc = {});
  /// Emit a .cfi_llvm_vector_register_mask directive.
  /// \param Register - Vector register being described.
  /// \param SpillRegister - Register holding the spilled value.
  /// \param SpillRegisterLaneSizeInBits - Lane size of \p SpillRegister.
  /// \param MaskRegister - Predicate/mask register.
  /// \param MaskRegisterSizeInBits - Size of \p MaskRegister in bits.
  /// \param Loc - Source location for diagnostics.
  virtual void
  emitCFILLVMVectorRegisterMask(int64_t Register, int64_t SpillRegister,
                                int64_t SpillRegisterLaneSizeInBits,
                                int64_t MaskRegister,
                                int64_t MaskRegisterSizeInBits, SMLoc Loc = {});

  /// Emit a .cfi_negate_ra_state_with_pc directive.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFINegateRAStateWithPC(SMLoc Loc = {});
  /// Emit a .cfi_llvm_set_ra_state directive using a PAC symbol.
  /// \param State - Return-address signing state.
  /// \param PACSym - Symbol of the PAC value.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFILLVMSetRAState(unsigned State, MCSymbol *PACSym,
                                     SMLoc Loc = {});
  /// Emit a .cfi_llvm_set_ra_state directive using a stack offset.
  /// \param State - Return-address signing state.
  /// \param Offset - Offset of the PAC value from the CFA.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFILLVMSetRAState(unsigned State, int64_t Offset,
                                     SMLoc Loc = {});
  /// Emit a .cfi_label directive.
  /// \param Loc - Source location for diagnostics.
  /// \param Name - Label name to emit.
  virtual void emitCFILabelDirective(SMLoc Loc, StringRef Name);
  /// Emit a .cfi_val_offset directive.
  /// \param Register - Register whose value is at CFA + \p Offset.
  /// \param Offset - Offset from the CFA.
  /// \param Loc - Source location for diagnostics.
  virtual void emitCFIValOffset(int64_t Register, int64_t Offset,
                                SMLoc Loc = {});

  /// Emit a .seh_proc directive starting a Windows CFI procedure.
  /// \param Symbol - Function symbol this procedure describes.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFIStartProc(const MCSymbol *Symbol, SMLoc Loc = SMLoc());
  /// Emit a .seh_endproc directive ending a Windows CFI procedure.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFIEndProc(SMLoc Loc = SMLoc());
  /// Emit a .seh_endfunclet directive for .xdata sizing.
  ///
  /// This is used on platforms, such as Windows on ARM64, that require function
  /// or funclet sizes to be emitted in .xdata before the End marker is emitted
  /// for the frame.  We cannot use the End marker, as it is not set at the
  /// point of emitting .xdata, in order to indicate that the frame is active.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFIFuncletOrFuncEnd(SMLoc Loc = SMLoc());
  /// Emit a .seh_splitchained directive.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFISplitChained(SMLoc Loc = SMLoc());
  /// Emit a .seh_pushreg directive.
  /// \param Register - Register pushed on the stack.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFIPushReg(MCRegister Register, SMLoc Loc = SMLoc());
  /// Emit a .seh_push2regs directive.
  /// \param Reg1 - First register pushed.
  /// \param Reg2 - Second register pushed.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFIPush2Regs(MCRegister Reg1, MCRegister Reg2,
                                   SMLoc Loc = SMLoc());
  /// Emit a .seh_setframe directive.
  /// \param Register - Frame-pointer register.
  /// \param Offset - Offset from RSP to the frame pointer.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFISetFrame(MCRegister Register, unsigned Offset,
                                  SMLoc Loc = SMLoc());
  /// Emit a .seh_stackalloc directive.
  /// \param Size - Number of bytes allocated on the stack.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFIAllocStack(unsigned Size, SMLoc Loc = SMLoc());
  /// Emit a .seh_savereg directive.
  /// \param Register - Register saved at \p Offset.
  /// \param Offset - Stack offset of the saved register.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFISaveReg(MCRegister Register, unsigned Offset,
                                 SMLoc Loc = SMLoc());
  /// Emit a .seh_savexmm directive.
  /// \param Register - XMM register saved at \p Offset.
  /// \param Offset - Stack offset of the saved register.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFISaveXMM(MCRegister Register, unsigned Offset,
                                 SMLoc Loc = SMLoc());
  /// Emit a .seh_pushframe directive.
  /// \param Code - True if this is a code-stack push frame.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFIPushFrame(bool Code, SMLoc Loc = SMLoc());
  /// Emit a .seh_endprologue directive.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFIEndProlog(SMLoc Loc = SMLoc());
  /// Emit a .seh_startepilogue directive.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFIBeginEpilogue(SMLoc Loc = SMLoc());
  /// Emit a .seh_endepilogue directive.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFIEndEpilogue(SMLoc Loc = SMLoc());
  /// Emit a .seh_unwindv2start directive.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFIUnwindV2Start(SMLoc Loc = SMLoc());
  /// Emit a .seh_unwindversion directive.
  /// \param Version - Unwind-info version.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinCFIUnwindVersion(uint8_t Version, SMLoc Loc = SMLoc());

  /// Set the default unwind version for new WinCFI frames.
  /// \param V - Default unwind-info version.
  void setDefaultWinCFIUnwindVersion(uint8_t V) {
    DefaultWinCFIUnwindVersion = V;
  }
  /// Return the default unwind version for new WinCFI frames.
  /// \return The default unwind-info version for new WinCFI frames.
  uint8_t getDefaultWinCFIUnwindVersion() const {
    return DefaultWinCFIUnwindVersion;
  }
  /// Emit a .seh_handler directive.
  /// \param Sym - Handler symbol.
  /// \param Unwind - True if the handler handles unwinding.
  /// \param Except - True if the handler handles exceptions.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinEHHandler(const MCSymbol *Sym, bool Unwind, bool Except,
                                SMLoc Loc = SMLoc());
  /// Emit a .seh_handlerdata directive.
  /// \param Loc - Source location for diagnostics.
  virtual void emitWinEHHandlerData(SMLoc Loc = SMLoc());

  /// Emit a call-graph profile edge.
  /// \param From - Caller symbol.
  /// \param To - Callee symbol.
  /// \param Count - Number of calls from \p From to \p To.
  virtual void emitCGProfileEntry(const MCSymbolRefExpr *From,
                                  const MCSymbolRefExpr *To, uint64_t Count);

  /// Return the .pdata section associated with \p TextSec.
  ///
  /// Typically the given section is either the main .text section or some other
  /// COMDAT .text section, but it may be any section containing code.
  /// \param TextSec - Code section whose .pdata is requested.
  /// \return The .pdata section associated with \p TextSec.
  MCSection *getAssociatedPDataSection(const MCSection *TextSec);

  /// Get the .xdata section used for the given section.
  /// \param TextSec - Code section whose .xdata is requested.
  /// \return The .xdata section associated with \p TextSec.
  MCSection *getAssociatedXDataSection(const MCSection *TextSec);

  /// Emit a `.{Syntax}_syntax` directive, such as `.intel_syntax`.
  /// \param Syntax - Syntax name, such as "intel" or "att".
  /// \param Options - Additional syntax options, such as "noprefix".
  virtual void emitSyntaxDirective(StringRef Syntax, StringRef Options);

  /// Record a relocation described by the .reloc directive.
  /// \param Offset - Offset in the current section of the relocation.
  /// \param Name - Relocation-kind name.
  /// \param Expr - Optional relocation addend.
  /// \param Loc - Source location for diagnostics.
  virtual void emitRelocDirective(const MCExpr &Offset, StringRef Name,
                                  const MCExpr *Expr, SMLoc Loc = {}) {}

  /// Emit a .addrsig directive starting an address-significance table.
  virtual void emitAddrsig() {}
  /// Add \p Sym to the address-significance table.
  /// \param Sym - Symbol marked address-significant.
  virtual void emitAddrsigSym(const MCSymbol *Sym) {}

  /// Emit the given \p Instruction into the current section.
  /// \param Inst - Instruction to emit.
  /// \param STI - Subtarget info in effect for \p Inst.
  virtual void emitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI);

  /// Emit the a pseudo probe into the current section.
  /// \param Guid - GUID of the originating function.
  /// \param Index - Probe index within that function.
  /// \param Type - Probe type.
  /// \param Attr - Attribute bitmask.
  /// \param Discriminator - Discriminator value.
  /// \param InlineStack - Inline context of the probe.
  /// \param FnSym - Function symbol associated with the probe.
  virtual void emitPseudoProbe(uint64_t Guid, uint64_t Index, uint64_t Type,
                               uint64_t Attr, uint64_t Discriminator,
                               const MCPseudoProbeInlineStack &InlineStack,
                               MCSymbol *FnSym);

  /// Enable aligned instruction bundling with the given bundle size.
  ///
  /// Enable aligned instruction bundling with the given bundle size, from
  /// this point onward. Once enabled, bundling cannot be disabled and the
  /// bundle size cannot be changed. \p Alignment must be within [2, 2^30].
  /// The default implementations report an error: silently emitting unbundled
  /// code would defeat the sandboxing schemes bundling exists for.
  /// \param Alignment - Bundle size in bytes.
  virtual void emitBundleAlignMode(Align Alignment);

  /// The following instructions are a bundle-locked group.
  ///
  /// \param AlignToEnd - If true, the bundle-locked group will be aligned to
  ///                     the end of a bundle.
  /// \param STI - Subtarget info in effect for the locked group.
  virtual void emitBundleLock(bool AlignToEnd, const MCSubtargetInfo &STI);

  /// Ends a bundle-locked group.
  /// \param STI - Subtarget info in effect for the locked group.
  virtual void emitBundleUnlock(const MCSubtargetInfo &STI);

  /// Dump \p String into the output assembly file.
  ///
  /// If this file is backed by a assembly streamer, this dumps the specified
  /// string in the output .s file.  This capability is indicated by the
  /// hasRawTextSupport() predicate.  By default this aborts.
  /// \param String - Text to write to the .s file.
  void emitRawText(const Twine &String);

  /// Streamer specific finalization.
  virtual void finishImpl();
  /// Finish emission of machine code.
  /// \param EndLoc - Source location for end-of-file diagnostics.
  void finish(SMLoc EndLoc = SMLoc());

  /// Return true if \p Sec may contain instructions.
  /// \param Sec - Section to query.
  /// \return True if \p Sec may contain instructions.
  virtual bool mayHaveInstructions(MCSection &Sec) const { return true; }

  /// Emit a special value of 0xffffffff if producing 64-bit debugging info.
  void maybeEmitDwarf64Mark();

  /// Emit a DWARF unit length field.
  ///
  /// The actual format, DWARF32 or DWARF64, is chosen according to the
  /// settings.
  /// \param Length - Unit length excluding the length field itself.
  /// \param Comment - Comment printed next to the length field.
  virtual void emitDwarfUnitLength(uint64_t Length, const Twine &Comment);

  /// Emit a DWARF unit length field and return the generated end symbol.
  ///
  /// The actual format, DWARF32 or DWARF64, is chosen according to the
  /// settings. The caller needs to emit the returned end symbol.
  /// \param Prefix - Prefix used to name the generated end symbol.
  /// \param Comment - Comment printed next to the length field.
  /// \return The generated end symbol that the caller must emit.
  virtual MCSymbol *emitDwarfUnitLength(const Twine &Prefix,
                                        const Twine &Comment);

  /// Emit the debug line start label.
  /// \param StartSym - Start symbol of the line-table unit.
  virtual void emitDwarfLineStartLabel(MCSymbol *StartSym);

  /// Emit the debug line end entry.
  /// \param Section - Section whose line-table contribution is being ended.
  /// \param LastLabel - Last line-table label emitted for \p Section.
  /// \param EndLabel - Optional explicit end label; created if null.
  virtual void emitDwarfLineEndEntry(MCSection *Section, MCSymbol *LastLabel,
                                     MCSymbol *EndLabel = nullptr) {}

  /// Emit a raw DWARF line-address advance from \p LastLabel to \p Label.
  ///
  /// If targets does not support representing debug line section by .loc/.file
  /// directives in assembly output, we need to populate debug line section with
  /// raw debug line contents.
  /// \param LineDelta - Change in line number since the previous entry.
  /// \param LastLabel - Previous address label, or null at the start.
  /// \param Label - Current address label.
  /// \param PointerSize - Size of a target address in bytes.
  virtual void emitDwarfAdvanceLineAddr(int64_t LineDelta,
                                        const MCSymbol *LastLabel,
                                        const MCSymbol *Label,
                                        unsigned PointerSize) {}
};

/// Return the MC context of the attached streamer.
/// \return The MC context of the attached streamer.
inline MCContext &MCTargetStreamer::getContext() {
  return Streamer.getContext();
}

/// Create a dummy machine code streamer, which does nothing. This is useful for
/// timing the assembler front end.
/// \param Ctx - MC context that owns symbols and sections.
/// \return A null streamer that discards all output.
LLVM_ABI MCStreamer *createNullStreamer(MCContext &Ctx);

} // end namespace llvm

#endif // LLVM_MC_MCSTREAMER_H
