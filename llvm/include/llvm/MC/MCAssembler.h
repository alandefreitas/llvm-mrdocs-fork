//===- MCAssembler.h - Object File Generation -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASSEMBLER_H
#define LLVM_MC_MCASSEMBLER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SMLoc.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace llvm {

class MCBoundaryAlignFragment;
class MCCVDefRangeFragment;
class MCCVInlineLineTableFragment;
class MCFragment;
class MCFixup;
class MCSymbolRefExpr;
class raw_ostream;
class MCAsmBackend;
class MCContext;
class MCCodeEmitter;
class MCFragment;
class MCObjectWriter;
class MCSection;
class MCValue;

/// Assembles machine-code sections, lays them out, and writes object files.
class MCAssembler {
public:
  friend class MCObjectWriter;
  /// List of sections owned by this assembler.
  using SectionListType = SmallVector<MCSection *, 0>;
  /// Const iterator over sections in this assembler.
  using const_iterator = pointee_iterator<SectionListType::const_iterator>;

private:
  MCContext &Context;

  std::unique_ptr<MCAsmBackend> Backend;
  std::unique_ptr<MCCodeEmitter> Emitter;
  std::unique_ptr<MCObjectWriter> Writer;

  bool HasLayout = false;
  bool HasFinalLayout = false;
  bool RelaxAll = false;

  // Cumulative upstream size change during `relaxOnce`. Used to compensate
  // forward-reference displacements in `evaluateFixup`.
  int64_t Stretch = 0;

  /// Non-empty when aligned instruction bundling is enabled.
  MaybeAlign BundleAlign;

  SectionListType Sections;

  SmallVector<const MCSymbol *, 0> Symbols;

  struct RelocDirective {
    const MCExpr &Offset;
    const MCExpr *Expr;
    uint32_t Kind;
  };
  SmallVector<RelocDirective, 0> relocDirectives;

  mutable SmallVector<std::pair<SMLoc, std::string>, 0> PendingErrors;

  MCDwarfLineTableParams LTParams;

  /// The set of function symbols for which a .thumb_func directive has
  /// been seen.
  //
  // FIXME: We really would like this in target specific code rather than
  // here. Maybe when the relocation stuff moves to target specific,
  // this can go with it? The streamer would need some target specific
  // refactoring too.
  mutable SmallPtrSet<const MCSymbol *, 32> ThumbFuncs;

  /// Evaluate a fixup to a relocatable expression and the value which should be
  /// placed into the fixup.
  ///
  /// \param F The fragment the fixup is inside.
  /// \param Fixup The fixup to evaluate.
  /// \param Target [out] On return, the relocatable expression the fixup
  /// evaluates to.
  /// \param Value [out] On return, the value of the fixup as currently laid
  /// out.
  /// \param RecordReloc Record relocation if needed.
  /// relocation.
  bool evaluateFixup(const MCFragment &F, MCFixup &Fixup, MCValue &Target,
                     uint64_t &Value, bool RecordReloc, uint8_t *Data) const;

  /// Check whether a fixup can be satisfied, or whether it needs to be relaxed
  /// (increased in size, in order to hold its value correctly).
  bool fixupNeedsRelaxation(const MCFragment &, const MCFixup &) const;

  void layoutSection(MCSection &Sec);
  /// Perform one layout iteration and return the index of the first stable
  /// section for subsequent optimization.
  unsigned relaxOnce(unsigned FirstStable);

  /// Perform relaxation on a single fragment.
  void relaxFragment(MCFragment &F);
  void relaxAlign(MCFragment &F);
  void relaxPrefAlign(MCFragment &F);
  void relaxInstruction(MCFragment &F);
  void relaxLEB(MCFragment &F);
  void relaxBoundaryAlign(MCBoundaryAlignFragment &BF);
  void relaxDwarfLineAddr(MCFragment &F);
  void relaxDwarfCallFrameFragment(MCFragment &F);
  void relaxSFrameFragment(MCFragment &DF);

public:
  /// Construct a new assembler instance.
  ///
  /// FIXME: How are we going to parameterize this? Two obvious options are stay
  /// concrete and require clients to pass in a target like object. The other
  /// option is to make this abstract, and have targets provide concrete
  /// implementations as we do with AsmParser.
  ///
  /// \param Context The MC context that owns symbols and sections.
  /// \param Backend The target-specific assembler backend.
  /// \param Emitter The target-specific code emitter.
  /// \param Writer The object writer that emits the final object file.
  LLVM_ABI MCAssembler(MCContext &Context,
                       std::unique_ptr<MCAsmBackend> Backend,
                       std::unique_ptr<MCCodeEmitter> Emitter,
                       std::unique_ptr<MCObjectWriter> Writer);
  /// Deleted copy constructor.
  ///
  /// \param Other The assembler that would be copied.
  MCAssembler(const MCAssembler &Other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param Other The assembler that would be assigned from.
  MCAssembler &operator=(const MCAssembler &Other) = delete;

  /// Compute the effective fragment size.
  ///
  /// \param F The fragment whose size to compute.
  /// \return The size of the fragment in bytes.
  LLVM_ABI uint64_t computeFragmentSize(const MCFragment &F) const;

  /// Get the offset of the given fragment inside its containing section.
  ///
  /// \param F The fragment whose offset to return.
  /// \return The fragment's offset within its section.
  uint64_t getFragmentOffset(const MCFragment &F) const { return F.Offset; }

  /// Get the address size of a section in the current layout.
  ///
  /// \param Sec The section whose address size to return.
  /// \return The section's address size in bytes.
  LLVM_ABI uint64_t getSectionAddressSize(const MCSection &Sec) const;
  /// Get the file size of a section in the current layout.
  ///
  /// \param Sec The section whose file size to return.
  /// \return The section's file size in bytes.
  LLVM_ABI uint64_t getSectionFileSize(const MCSection &Sec) const;

  /// Get the offset of the given symbol, as computed in the current layout.
  ///
  /// \param S The symbol whose offset to compute.
  /// \param Val [out] On success, set to the symbol offset.
  /// \return True on success.
  LLVM_ABI bool getSymbolOffset(const MCSymbol &S, uint64_t &Val) const;

  /// Get the offset of a symbol, or report a fatal error if not computable.
  ///
  /// \param S The symbol whose offset to compute.
  /// \return The symbol offset in the current layout.
  LLVM_ABI uint64_t getSymbolOffset(const MCSymbol &S) const;

  /// If this symbol is equivalent to A + Constant, return A.
  ///
  /// \param Symbol The symbol to resolve to a base symbol.
  /// \return The base symbol, or null if none applies.
  LLVM_ABI const MCSymbol *getBaseSymbol(const MCSymbol &Symbol) const;

  /// Emit the section contents to \p OS.
  ///
  /// \param OS The output stream to write section contents to.
  /// \param Section The section whose contents to emit.
  LLVM_ABI void writeSectionData(raw_ostream &OS,
                                 const MCSection *Section) const;

  /// Check whether a given symbol has been flagged with .thumb_func.
  ///
  /// \param Func The symbol to test for a .thumb_func flag.
  /// \return True if the symbol is marked as a Thumb function.
  LLVM_ABI bool isThumbFunc(const MCSymbol *Func) const;

  /// Flag a function symbol as the target of a .thumb_func directive.
  ///
  /// \param Func The function symbol to mark as a Thumb function.
  void setIsThumbFunc(const MCSymbol *Func) { ThumbFuncs.insert(Func); }

  /// Reuse an assembler instance
  ///
  LLVM_ABI void reset();

  /// Get the MC context associated with this assembler.
  ///
  /// \return The MC context that owns symbols and sections.
  MCContext &getContext() const { return Context; }

  /// Get a pointer to the assembler backend, or null if none is set.
  ///
  /// \return A pointer to the backend, or null if none is set.
  MCAsmBackend *getBackendPtr() const { return Backend.get(); }

  /// Get a pointer to the code emitter, or null if none is set.
  ///
  /// \return A pointer to the code emitter, or null if none is set.
  MCCodeEmitter *getEmitterPtr() const { return Emitter.get(); }

  /// Get a reference to the assembler backend.
  ///
  /// \return A reference to the target-specific assembler backend.
  MCAsmBackend &getBackend() const { return *Backend; }

  /// Get a reference to the code emitter.
  ///
  /// \return A reference to the target-specific code emitter.
  MCCodeEmitter &getEmitter() const { return *Emitter; }

  /// Get a reference to the object writer.
  ///
  /// \return A reference to the object writer.
  MCObjectWriter &getWriter() const { return *Writer; }

  /// Get the DWARF line-table parameters used by this assembler.
  ///
  /// \return The DWARF line-table parameters.
  MCDwarfLineTableParams getDWARFLinetableParams() const { return LTParams; }

  /// Finish final processing and write the object to the output stream.
  ///
  /// Writer is used for custom object writer (as the MCJIT does), if not
  /// specified it is automatically created from backend.
  LLVM_ABI void Finish();

  /// Layout all sections and prepare them for emission.
  LLVM_ABI void layout();

  /// Return true if a layout has been computed.
  ///
  /// \return True if a layout has been computed.
  bool hasLayout() const { return HasLayout; }
  /// Return true if the final layout has been computed.
  ///
  /// \return True if the final layout has been computed.
  bool hasFinalLayout() const { return HasFinalLayout; }
  /// Return whether all instructions should be fully relaxed.
  ///
  /// \return True if all instructions should be fully relaxed.
  bool getRelaxAll() const { return RelaxAll; }
  /// Set whether all instructions should be fully relaxed.
  ///
  /// \param Value True to relax all instructions.
  void setRelaxAll(bool Value) { RelaxAll = Value; }
  /// Get the cumulative size stretch from the last layout iteration.
  ///
  /// \return The cumulative upstream size change from the last layout
  /// iteration.
  int64_t getStretch() const { return Stretch; }

  /// Return true if aligned instruction bundling is enabled.
  ///
  /// \return True if aligned instruction bundling is enabled.
  bool isBundlingEnabled() const { return bool(BundleAlign); }
  /// Get the bundle alignment when bundling is enabled.
  ///
  /// \return The required alignment for instruction bundles.
  Align getBundleAlign() const {
    assert(BundleAlign && "bundling is not enabled");
    return *BundleAlign;
  }
  /// Enable bundling and set the required bundle alignment.
  ///
  /// \param Value The alignment required for instruction bundles.
  void setBundleAlign(Align Value) { BundleAlign = Value; }

  /// Get an iterator to the first section.
  ///
  /// \return A const iterator to the first section.
  const_iterator begin() const { return Sections.begin(); }
  /// Get an iterator past the last section.
  ///
  /// \return A const iterator past the last section.
  const_iterator end() const { return Sections.end(); }

  /// Get the mutable list of registered symbols.
  ///
  /// \return A mutable reference to the list of registered symbols.
  SmallVectorImpl<const MCSymbol *> &getSymbols() { return Symbols; }
  /// Get a range over the registered symbols.
  ///
  /// \return An iterator range over the registered symbols.
  iterator_range<
      pointee_iterator<SmallVector<const MCSymbol *, 0>::const_iterator>>
  symbols() const {
    return make_pointee_range(Symbols);
  }

  /// Register a section with this assembler.
  ///
  /// \param Section The section to register.
  /// \return True if the section was newly registered.
  LLVM_ABI bool registerSection(MCSection &Section);
  /// Register a symbol with this assembler.
  ///
  /// \param Symbol The symbol to register.
  /// \return True if the symbol was newly registered.
  LLVM_ABI bool registerSymbol(const MCSymbol &Symbol);
  /// Record a relocation directive for later emission.
  ///
  /// \param RD The relocation directive to add.
  LLVM_ABI void addRelocDirective(RelocDirective RD);

  /// Report an error at the given source location.
  ///
  /// \param L The source location of the error.
  /// \param Msg The error message to report.
  LLVM_ABI void reportError(SMLoc L, const Twine &Msg) const;
  /// Record a pending error during layout iteration.
  ///
  /// Pending errors may go away once the layout is finalized.
  ///
  /// \param L The source location of the error.
  /// \param Msg The error message to record.
  LLVM_ABI void recordError(SMLoc L, const Twine &Msg) const;
  /// Flush any pending errors recorded during layout.
  LLVM_ABI void flushPendingErrors() const;

  /// Dump assembler state for debugging.
  LLVM_ABI void dump() const;
};

} // end namespace llvm

#endif // LLVM_MC_MCASSEMBLER_H
