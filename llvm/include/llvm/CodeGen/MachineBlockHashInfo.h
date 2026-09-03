//===- llvm/CodeGen/MachineBlockHashInfo.h ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Compute the hashes of basic blocks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEBLOCKHASHINFO_H
#define LLVM_CODEGEN_MACHINEBLOCKHASHINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

/// An object wrapping several components of a basic block hash.
///
/// The combined (blended) hash is represented and stored as one uint64_t,
/// while individual components are of smaller size (e.g., uint16_t or
/// uint8_t).
struct BlendedBlockHash {
public:
  /// Construct a blended hash from individual component values.
  ///
  /// \param Offset Offset of the basic block from the function start.
  /// \param OpcodeHash Hash of the basic block instructions, excluding
  ///        operands.
  /// \param InstrHash Hash of the basic block instructions, including opcodes
  ///        and operands.
  /// \param NeighborHash OpcodeHash of the block together with those of its
  ///        successors and predecessors.
  explicit BlendedBlockHash(uint16_t Offset, uint16_t OpcodeHash,
                            uint16_t InstrHash, uint16_t NeighborHash)
      : Offset(Offset), OpcodeHash(OpcodeHash), InstrHash(InstrHash),
        NeighborHash(NeighborHash) {}

  /// Construct a blended hash by unpacking a combined 64-bit value.
  ///
  /// \param CombinedHash Packed hash produced by \ref combine.
  explicit BlendedBlockHash(uint64_t CombinedHash) {
    Offset = CombinedHash & 0xffff;
    CombinedHash >>= 16;
    OpcodeHash = CombinedHash & 0xffff;
    CombinedHash >>= 16;
    InstrHash = CombinedHash & 0xffff;
    CombinedHash >>= 16;
    NeighborHash = CombinedHash & 0xffff;
  }

  /// Combine the blended hash into uint64_t.
  ///
  /// \return Packed 64-bit hash of all blended components.
  uint64_t combine() const {
    uint64_t Hash = 0;
    Hash |= uint64_t(NeighborHash);
    Hash <<= 16;
    Hash |= uint64_t(InstrHash);
    Hash <<= 16;
    Hash |= uint64_t(OpcodeHash);
    Hash <<= 16;
    Hash |= uint64_t(Offset);
    return Hash;
  }

  /// Compute a distance between two given blended hashes.
  ///
  /// The smaller the distance, the more similar two blocks are. For identical
  /// basic blocks, the distance is zero.
  /// Since OpcodeHash is highly stable, we consider a match good only if
  /// the OpcodeHashes are identical. Mismatched OpcodeHashes lead to low
  /// matching accuracy, and poor matches undermine the quality of final
  /// inference. Notably, during inference, we also consider the matching
  /// ratio of basic blocks. For MachineFunctions with a low matching
  /// ratio, we directly skip optimization to reduce the impact of
  /// mismatches. This ensures even very poor profiles won’t cause negative
  /// optimization.
  /// In the context of matching, we consider NeighborHash to be more
  /// important. This is especially true when accounting for inlining
  /// scenarios, where the position of a basic block in the control
  /// flow graph is more critical.
  ///
  /// \param BBH Other blended hash to compare against.
  /// \return Distance between this hash and \p BBH; zero if they match.
  uint64_t distance(const BlendedBlockHash &BBH) const {
    assert(OpcodeHash == BBH.OpcodeHash &&
           "incorrect blended hash distance computation");
    uint64_t Dist = 0;
    // Account for NeighborHash
    Dist += NeighborHash == BBH.NeighborHash ? 0 : 1;
    Dist <<= 16;
    // Account for InstrHash
    Dist += InstrHash == BBH.InstrHash ? 0 : 1;
    Dist <<= 16;
    // Account for Offset
    Dist += (Offset >= BBH.Offset ? Offset - BBH.Offset : BBH.Offset - Offset);
    return Dist;
  }

  /// Return the opcode hash component of this blended hash.
  ///
  /// \return Opcode hash of the basic block instructions, excluding operands.
  uint16_t getOpcodeHash() const { return OpcodeHash; }

private:
  /// The offset of the basic block from the function start.
  uint16_t Offset{0};
  /// Hash of the basic block instructions, excluding operands.
  uint16_t OpcodeHash{0};
  /// Hash of the basic block instructions, including opcodes and
  /// operands.
  uint16_t InstrHash{0};
  /// OpcodeHash of the basic block together with OpcodeHashes of its
  /// successors and predecessors.
  uint16_t NeighborHash{0};
};

/// Result object for MachineBlockHashInfo.
class MachineBlockHashInfoResult {
  DenseMap<const MachineBasicBlock *, uint64_t> MBBHashInfo;

public:
  /// Construct an empty hash-info result.
  LLVM_ABI MachineBlockHashInfoResult();
  /// Compute basic-block hashes for machine function \p MBB.
  ///
  /// \param MBB Machine function whose basic blocks are hashed.
  LLVM_ABI explicit MachineBlockHashInfoResult(const MachineFunction &MBB);
  /// Return the blended hash for machine basic block \p MBB.
  ///
  /// \param MBB Basic block whose hash is requested.
  /// \return Combined blended hash for \p MBB.
  LLVM_ABI uint64_t getMBBHash(const MachineBasicBlock &MBB) const;
};

/// Analysis pass that computes blended hashes for machine basic blocks.
class MachineBlockHashInfoAnalysis
    : public AnalysisInfoMixin<MachineBlockHashInfoAnalysis> {
  friend AnalysisInfoMixin<MachineBlockHashInfoAnalysis>;
  static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = MachineBlockHashInfoResult;
  /// Compute basic-block hashes for machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \param MFAM Analysis manager for the machine function.
  /// \return Hash info result for \p MF.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
};

/// Printer pass for the \c MachineBlockHashInfoAnalysis results.
class MachineBlockHashInfoPrinterPass
    : public RequiredPassInfoMixin<MachineBlockHashInfoPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  ///
  /// \param OS Output stream for the hash info dump.
  explicit MachineBlockHashInfoPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print MachineBlockHashInfoAnalysis results for \p MF.
  ///
  /// \param MF Machine function whose hashes are printed.
  /// \param MFAM Analysis manager providing MachineBlockHashInfoAnalysis.
  /// \return All analyses preserved; this pass does not transform \p MF.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

/// Legacy MachineFunctionPass for MachineBlockHashInfo.
class LLVM_ABI MachineBlockHashInfo : public MachineFunctionPass {
  MachineBlockHashInfoResult Result;

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct the machine basic-block hash info pass.
  MachineBlockHashInfo();

  /// Return the name of this pass.
  ///
  /// \return Name of this pass.
  StringRef getPassName() const override { return "Basic Block Hash Compute"; }

  /// Declare analyses required and preserved by this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Compute basic-block hashes for machine function \p F.
  ///
  /// \param F Machine function to analyze.
  /// \return False; this analysis does not modify the machine function.
  bool runOnMachineFunction(MachineFunction &F) override;

  /// Return the blended hash for machine basic block \p MBB.
  ///
  /// \param MBB Basic block whose hash is requested.
  /// \return Combined blended hash for \p MBB.
  uint64_t getMBBHash(const MachineBasicBlock &MBB) const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEBLOCKHASHINFO_H
