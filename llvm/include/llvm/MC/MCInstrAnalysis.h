//===- llvm/MC/MCInstrAnalysis.h - InstrDesc target hooks -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MCInstrAnalysis class which the MCTargetDescs can
// derive from to give additional information to MC.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCINSTRANALYSIS_H
#define LLVM_MC_MCINSTRANALYSIS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <vector>

namespace llvm {

class MCRegisterInfo;
class Triple;

/// Target hooks that analyse machine instructions for MC clients.
///
/// MCTargetDescs derive from this class to supply branch evaluation, control-
/// flow classification, dependency-breaking idioms, and related helpers beyond
/// the static data in \c MCInstrDesc.
class LLVM_ABI MCInstrAnalysis {
protected:
  friend class Target;

  /// Instruction info describing the target instruction set.
  const MCInstrInfo *Info;

public:
  /// Construct an analysis object backed by \p Info.
  ///
  /// \param Info - Instruction info for the target.
  MCInstrAnalysis(const MCInstrInfo *Info) : Info(Info) {}
  /// Destroy the instruction analysis object.
  virtual ~MCInstrAnalysis() = default;

  /// Clear the internal state. See updateState for more information.
  virtual void resetState() {}

  /// Update internal state with \p Inst at \p Addr.
  ///
  /// For some types of analyses, inspecting a single instruction is not
  /// sufficient. Some examples are auipc/jalr pairs on RISC-V or adrp/ldr pairs
  /// on AArch64. To support inspecting multiple instructions, targets may keep
  /// track of an internal state while analysing instructions. Clients should
  /// call updateState for every instruction which allows later calls to one of
  /// the analysis functions to take previous instructions into account.
  /// Whenever state becomes irrelevant (e.g., when starting to disassemble a
  /// new function), clients should call resetState to clear it.
  ///
  /// \param Inst - Instruction to incorporate into the analysis state.
  /// \param STI - Subtarget information for \p Inst, or null.
  /// \param Addr - Address of \p Inst.
  virtual void updateState(const MCInst &Inst, const MCSubtargetInfo *STI,
                           uint64_t Addr) {}

  /// Return true if \p Inst is a branch instruction.
  ///
  /// \param Inst - Instruction to classify.
  /// \return True if \p Inst is a branch.
  virtual bool isBranch(const MCInst &Inst) const {
    return Info->get(Inst.getOpcode()).isBranch();
  }

  /// Return true if \p Inst is a conditional branch.
  ///
  /// \param Inst - Instruction to classify.
  /// \return True if \p Inst is a conditional branch.
  virtual bool isConditionalBranch(const MCInst &Inst) const {
    return Info->get(Inst.getOpcode()).isConditionalBranch();
  }

  /// Return true if \p Inst is an unconditional branch.
  ///
  /// \param Inst - Instruction to classify.
  /// \return True if \p Inst is an unconditional branch.
  virtual bool isUnconditionalBranch(const MCInst &Inst) const {
    return Info->get(Inst.getOpcode()).isUnconditionalBranch();
  }

  /// Return true if \p Inst is an indirect branch.
  ///
  /// \param Inst - Instruction to classify.
  /// \return True if \p Inst is an indirect branch.
  virtual bool isIndirectBranch(const MCInst &Inst) const {
    return Info->get(Inst.getOpcode()).isIndirectBranch();
  }

  /// Return true if \p Inst is a call instruction.
  ///
  /// \param Inst - Instruction to classify.
  /// \return True if \p Inst is a call.
  virtual bool isCall(const MCInst &Inst) const {
    return Info->get(Inst.getOpcode()).isCall();
  }

  /// Return true if \p Inst is a return instruction.
  ///
  /// \param Inst - Instruction to classify.
  /// \return True if \p Inst is a return.
  virtual bool isReturn(const MCInst &Inst) const {
    return Info->get(Inst.getOpcode()).isReturn();
  }

  /// Return true if \p Inst is a basic-block terminator.
  ///
  /// \param Inst - Instruction to classify.
  /// \return True if \p Inst is a terminator.
  virtual bool isTerminator(const MCInst &Inst) const {
    return Info->get(Inst.getOpcode()).isTerminator();
  }

  /// Return true if \p Inst is a barrier that stops fall-through.
  ///
  /// \param Inst - Instruction to classify.
  /// \return True if \p Inst is a barrier.
  virtual bool isBarrier(const MCInst &Inst) const {
    return Info->get(Inst.getOpcode()).isBarrier();
  }

  /// Return true if \p Inst may affect control flow.
  ///
  /// Considers branches, calls, returns, and writes to the program counter.
  ///
  /// \param Inst - Instruction to inspect.
  /// \param MCRI - Register info used to identify the program counter.
  /// \return True if \p Inst may affect control flow.
  virtual bool mayAffectControlFlow(const MCInst &Inst,
                                    const MCRegisterInfo &MCRI) const {
    if (isBranch(Inst) || isCall(Inst) || isReturn(Inst) ||
        isIndirectBranch(Inst))
      return true;
    MCRegister PC = MCRI.getProgramCounter();
    if (!PC)
      return false;
    return Info->get(Inst.getOpcode()).hasDefOfPhysReg(Inst, PC, MCRI);
  }

  /// Returns true if at least one of the register writes performed by
  /// \param Inst implicitly clears the upper portion of all super-registers.
  ///
  /// Example: on X86-64, a write to EAX implicitly clears the upper half of
  /// RAX. Also (still on x86) an XMM write perfomed by an AVX 128-bit
  /// instruction implicitly clears the upper portion of the correspondent
  /// YMM register.
  ///
  /// This method also updates an APInt which is used as mask of register
  /// writes. There is one bit for every explicit/implicit write performed by
  /// the instruction. If a write implicitly clears its super-registers, then
  /// the corresponding bit is set (vic. the corresponding bit is cleared).
  ///
  /// The first bits in the APint are related to explicit writes. The remaining
  /// bits are related to implicit writes. The sequence of writes follows the
  /// machine operand sequence. For implicit writes, the sequence is defined by
  /// the MCInstrDesc.
  ///
  /// The assumption is that the bit-width of the APInt is correctly set by
  /// the caller. The default implementation conservatively assumes that none of
  /// the writes clears the upper portion of a super-register.
  ///
  /// \param MRI - Register info used to interpret written registers.
  /// \param Writes - [out] Mask of writes that clear their super-registers.
  /// \return True if at least one write clears its super-registers.
  virtual bool clearsSuperRegisters(const MCRegisterInfo &MRI,
                                    const MCInst &Inst,
                                    APInt &Writes) const;

  /// Returns true if MI is a dependency breaking zero-idiom for the given
  /// subtarget.
  ///
  /// Mask is used to identify input operands that have their dependency
  /// broken. Each bit of the mask is associated with a specific input operand.
  /// Bits associated with explicit input operands are laid out first in the
  /// mask; implicit operands come after explicit operands.
  ///
  /// Dependencies are broken only for operands that have their corresponding bit
  /// set. Operands that have their bit cleared, or that don't have a
  /// corresponding bit in the mask don't have their dependency broken.  Note
  /// that Mask may not be big enough to describe all operands.  The assumption
  /// for operands that don't have a correspondent bit in the mask is that those
  /// are still data dependent.
  ///
  /// The only exception to the rule is for when Mask has all zeroes.
  /// A zero mask means: dependencies are broken for all explicit register
  /// operands.
  ///
  /// \param MI - Instruction to test as a zero-idiom.
  /// \param Mask - [out] Operand mask identifying broken dependencies.
  /// \param CPUID - Subtarget identifier selecting dependency rules.
  /// \return True if \p MI is a dependency-breaking zero-idiom for \p CPUID.
  virtual bool isZeroIdiom(const MCInst &MI, APInt &Mask,
                           unsigned CPUID) const {
    return false;
  }

  /// Returns true if MI is a dependency breaking instruction for the
  /// subtarget associated with CPUID .
  ///
  /// The value computed by a dependency breaking instruction is not dependent
  /// on the inputs. An example of dependency breaking instruction on X86 is
  /// `XOR %eax, %eax`.
  ///
  /// If MI is a dependency breaking instruction for subtarget CPUID, then Mask
  /// can be inspected to identify independent operands.
  ///
  /// Essentially, each bit of the mask corresponds to an input operand.
  /// Explicit operands are laid out first in the mask; implicit operands follow
  /// explicit operands. Bits are set for operands that are independent.
  ///
  /// Note that the number of bits in Mask may not be equivalent to the sum of
  /// explicit and implicit operands in MI. Operands that don't have a
  /// corresponding bit in Mask are assumed "not independente".
  ///
  /// The only exception is for when Mask is all zeroes. That means: explicit
  /// input operands of MI are independent.
  ///
  /// \param MI - Instruction to test as dependency-breaking.
  /// \param Mask - [out] Operand mask identifying independent operands.
  /// \param CPUID - Subtarget identifier selecting dependency rules.
  /// \return True if \p MI is dependency-breaking for \p CPUID.
  virtual bool isDependencyBreaking(const MCInst &MI, APInt &Mask,
                                    unsigned CPUID) const {
    return isZeroIdiom(MI, Mask, CPUID);
  }

  /// Returns true if MI is a candidate for move elimination.
  ///
  /// Different subtargets may apply different constraints to optimizable
  /// register moves. For example, on most X86 subtargets, a candidate for move
  /// elimination cannot specify the same register for both source and
  /// destination.
  ///
  /// \param MI - Instruction to test as a move-elimination candidate.
  /// \param CPUID - Subtarget identifier selecting move-elimination rules.
  /// \return True if \p MI is an optimizable register-move candidate.
  virtual bool isOptimizableRegisterMove(const MCInst &MI,
                                         unsigned CPUID) const {
    return false;
  }

  /// Given a branch instruction try to get the address the branch
  /// targets. Return true on success, and the address in Target.
  ///
  /// \param Inst - Branch instruction to evaluate.
  /// \param Addr - Address of \p Inst.
  /// \param Size - Encoded size of \p Inst in bytes.
  /// \param Target - [out] Resolved branch target address on success.
  /// \return True if the branch target was successfully resolved.
  virtual bool
  evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                 uint64_t &Target) const;

  /// Given an instruction tries to get the address of a memory operand. Returns
  /// the address on success.
  ///
  /// \param Inst - Instruction containing the memory operand.
  /// \param STI - Subtarget information for \p Inst, or null.
  /// \param Addr - Address of \p Inst.
  /// \param Size - Encoded size of \p Inst in bytes.
  /// \return Resolved memory-operand address, or \c std::nullopt on failure.
  virtual std::optional<uint64_t>
  evaluateMemoryOperandAddress(const MCInst &Inst, const MCSubtargetInfo *STI,
                               uint64_t Addr, uint64_t Size) const;

  /// Given an instruction with a memory operand that could require relocation,
  /// returns the offset within the instruction of that relocation.
  ///
  /// \param Inst - Instruction containing the relocatable memory operand.
  /// \param Size - Encoded size of \p Inst in bytes.
  /// \return Byte offset of the relocation within \p Inst, or \c std::nullopt.
  virtual std::optional<uint64_t>
  getMemoryOperandRelocationOffset(const MCInst &Inst, uint64_t Size) const;

  /// Returns (PLT virtual address, GOT virtual address) pairs for PLT entries.
  ///
  /// \param PltSectionVA - Virtual address of the PLT section.
  /// \param PltContents - Raw bytes of the PLT section.
  /// \param STI - Subtarget information used to decode PLT entries.
  /// \return Pairs of (PLT entry VA, GOT entry VA); empty if unsupported.
  virtual std::vector<std::pair<uint64_t, uint64_t>>
  findPltEntries(uint64_t PltSectionVA, ArrayRef<uint8_t> PltContents,
                 const MCSubtargetInfo &STI) const {
    return {};
  }
};

} // end namespace llvm

#endif // LLVM_MC_MCINSTRANALYSIS_H
