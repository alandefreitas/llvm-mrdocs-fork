//===- llvm/MC/MCSymbolizer.h - MCSymbolizer class --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCSymbolizer class, which is used
// to symbolize instructions decoded from an object, that is, transform their
// immediate operands to MCExprs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCDISASSEMBLER_MCSYMBOLIZER_H
#define LLVM_MC_MCDISASSEMBLER_MCSYMBOLIZER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/MCDisassembler/MCRelocationInfo.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <memory>
#include <utility>

namespace llvm {

class MCContext;
class MCInst;
class raw_ostream;

/// Symbolize and annotate disassembled instructions.
///
/// For now this mimics the old symbolization logic (from both ARM and x86), that
/// relied on user-provided (C API) callbacks to do the actual symbol lookup in
/// the object file. This was moved to MCExternalSymbolizer.
/// A better API would not rely on actually calling the two methods here from
/// inside each disassembler, but would use the instr info to determine what
/// operands are actually symbolizable, and in what way. I don't think this
/// information exists right now.
class LLVM_ABI MCSymbolizer {
protected:
  /// Context used to create symbolic MCExprs.
  MCContext &Ctx;
  /// Relocation info used to enrich symbolic expressions; owned by this.
  std::unique_ptr<MCRelocationInfo> RelInfo;

public:
  /// Construct an MCSymbolizer, taking ownership of \p RelInfo.
  ///
  /// \param Ctx - Context used to create symbolic MCExprs.
  /// \param RelInfo - Relocation info; ownership is transferred to this.
  MCSymbolizer(MCContext &Ctx, std::unique_ptr<MCRelocationInfo> RelInfo)
    : Ctx(Ctx), RelInfo(std::move(RelInfo)) {
  }

  /// Deleted copy constructor.
  ///
  /// \param Other - Unused; copy construction is deleted.
  MCSymbolizer(const MCSymbolizer &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other - Unused; copy assignment is deleted.
  MCSymbolizer &operator=(const MCSymbolizer &Other) = delete;
  /// Destroy the symbolizer.
  virtual ~MCSymbolizer();

  /// Try to add a symbolic operand instead of \p Value to the MCInst.
  ///
  /// Instead of having a difficult to read immediate, a symbolic operand would
  /// represent this immediate in a more understandable way, for instance as a
  /// symbol or an offset from a symbol. Relocations can also be used to enrich
  /// the symbolic expression.
  /// \param Inst      - The MCInst where to insert the symbolic operand.
  /// \param cStream   - Stream to print comments and annotations on.
  /// \param Value     - Operand value, pc-adjusted by the caller if necessary.
  /// \param Address   - Load address of the instruction.
  /// \param IsBranch  - Is the instruction a branch?
  /// \param Offset    - Byte offset of the operand inside the inst.
  /// \param OpSize    - Size of the operand in bytes.
  /// \param InstSize  - Size of the instruction in bytes.
  /// \return Whether a symbolic operand was added.
  virtual bool tryAddingSymbolicOperand(MCInst &Inst, raw_ostream &cStream,
                                        int64_t Value, uint64_t Address,
                                        bool IsBranch, uint64_t Offset,
                                        uint64_t OpSize, uint64_t InstSize) = 0;

  /// Try to add a comment on the PC-relative load.
  ///
  /// For instance, in Mach-O, this is used to add annotations to instructions
  /// that use C string literals, as found in __cstring.
  /// \param cStream - Stream to print comments and annotations on.
  /// \param Value - Immediate value used as a PC-relative load address.
  /// \param Address - Load address of the instruction.
  virtual void tryAddingPcLoadReferenceComment(raw_ostream &cStream,
                                               int64_t Value,
                                               uint64_t Address) = 0;

  /// Return addresses referenced by unresolved symbolizable operands.
  ///
  /// The caller (some code that is disassembling a section or other chunk of
  /// code) would typically create a synthetic label at each address and add
  /// them to its list of symbols in the section, before creating a new
  /// MCSymbolizer with the enhanced symbol list and retrying disassembling the
  /// section.
  /// The returned array is unordered and may have duplicates.
  /// The returned ArrayRef stops being valid on any call to or destruction of
  /// the MCSymbolizer object.
  /// \return Addresses referenced by unresolved symbolizable operands.
  virtual ArrayRef<uint64_t> getReferencedAddresses() const { return {}; }
};

} // end namespace llvm

#endif // LLVM_MC_MCDISASSEMBLER_MCSYMBOLIZER_H
