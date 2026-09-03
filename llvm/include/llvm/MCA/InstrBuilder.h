//===--------------------- InstrBuilder.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// A builder class for instructions that are statically analyzed by llvm-mca.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_INSTRBUILDER_H
#define LLVM_MCA_INSTRBUILDER_H

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MCA/CustomBehaviour.h"
#include "llvm/MCA/Instruction.h"
#include "llvm/MCA/Support.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace mca {

/// Error indicating that an mca::Instruction was recycled rather than newly
/// created.
class RecycledInstErr : public ErrorInfo<RecycledInstErr> {
  Instruction *RecycledInst;

public:
  /// RTTI identifier used by ErrorInfo::classID.
  LLVM_ABI static char ID;

  /// Construct an error carrying recycled instruction \p Inst.
  ///
  /// \param Inst Recycled instruction that should be reused.
  explicit RecycledInstErr(Instruction *Inst) : RecycledInst(Inst) {}
  /// Deleted; a recycled instruction must always be provided.
  RecycledInstErr() = delete;

  /// Return the recycled instruction carried by this error.
  ///
  /// \return Pointer to the recycled Instruction.
  Instruction *getInst() const { return RecycledInst; }

  /// Write this error's message to \p OS.
  ///
  /// \param OS Stream to receive the message.
  void log(raw_ostream &OS) const override {
    OS << "Instruction is recycled\n";
  }

  /// Convert this error to a \c std::error_code.
  ///
  /// \return An inconvertible error code; this error has no errno equivalent.
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }
};

/// A builder class that knows how to construct Instruction objects.
///
/// Every llvm-mca Instruction is described by an object of class InstrDesc.
/// An InstrDesc describes which registers are read/written by the instruction,
/// as well as the instruction latency and hardware resources consumed.
///
/// This class is used by the tool to construct Instructions and instruction
/// descriptors (i.e. InstrDesc objects).
/// Information from the machine scheduling model is used to identify processor
/// resources that are consumed by an instruction.
class InstrBuilder {
  const MCSubtargetInfo &STI;
  const MCInstrInfo &MCII;
  const MCRegisterInfo &MRI;
  const MCInstrAnalysis *MCIA;
  const InstrumentManager &IM;
  SmallVector<uint64_t, 8> ProcResourceMasks;

  // Key is the MCI.Opcode and SchedClassID the describe the value InstrDesc
  DenseMap<std::pair<unsigned short, unsigned>,
           std::unique_ptr<const InstrDesc>>
      Descriptors;

  // Key is a hash of the MCInstruction and a SchedClassID that describe the
  // value InstrDesc
  DenseMap<std::pair<hash_code, unsigned>, std::unique_ptr<const InstrDesc>>
      VariantDescriptors;

  // These descriptors are customized for particular instructions and cannot
  // be reused
  SmallVector<std::unique_ptr<const InstrDesc>> CustomDescriptors;

  bool FirstCallInst;
  bool FirstReturnInst;
  unsigned CallLatency;

  using InstRecycleCallback = std::function<Instruction *(const InstrDesc &)>;
  InstRecycleCallback InstRecycleCB;

  Expected<unsigned> getVariantSchedClassID(const MCInst &MCI, unsigned SchedClassID);
  Expected<const InstrDesc &>
  createInstrDescImpl(const MCInst &MCI, const SmallVector<Instrument *> &IVec);
  Expected<const InstrDesc &>
  getOrCreateInstrDesc(const MCInst &MCI,
                       const SmallVector<Instrument *> &IVec);

  InstrBuilder(const InstrBuilder &) = delete;
  InstrBuilder &operator=(const InstrBuilder &) = delete;

  void populateWrites(InstrDesc &ID, const MCInst &MCI, unsigned SchedClassID);
  void populateReads(InstrDesc &ID, const MCInst &MCI, unsigned SchedClassID);
  Error verifyInstrDesc(const InstrDesc &ID, const MCInst &MCI) const;

public:
  /// Construct an instruction builder for the given target and call model.
  ///
  /// \param STI Subtarget information providing the scheduling model.
  /// \param MCII Target instruction information.
  /// \param RI Target register information.
  /// \param IA Optional instruction analysis for branch and idiom detection.
  /// \param IM Instrument manager used for variant instruction descriptors.
  /// \param CallLatency Latency assigned when a call has no known latency.
  LLVM_ABI InstrBuilder(const MCSubtargetInfo &STI, const MCInstrInfo &MCII,
                        const MCRegisterInfo &RI, const MCInstrAnalysis *IA,
                        const InstrumentManager &IM, unsigned CallLatency);

  /// Clear cached instruction descriptors and first-call/return tracking.
  void clear() {
    Descriptors.clear();
    VariantDescriptors.clear();
    FirstCallInst = true;
    FirstReturnInst = true;
  }

  /// Set a callback which is invoked to retrieve a recycled mca::Instruction
  /// or null if there isn't any.
  ///
  /// \param CB Callback that returns a recycled instruction for a descriptor,
  /// or null if none is available.
  void setInstRecycleCallback(InstRecycleCallback CB) { InstRecycleCB = CB; }

  /// Create an mca::Instruction for \p MCI using instruments in \p IVec.
  ///
  /// \param MCI Machine instruction to analyze.
  /// \param IVec Instruments that may customize the instruction descriptor.
  /// \return A new or recycled Instruction, or an error on failure.
  LLVM_ABI Expected<std::unique_ptr<Instruction>>
  createInstruction(const MCInst &MCI, const SmallVector<Instrument *> &IVec);
};
} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_INSTRBUILDER_H
