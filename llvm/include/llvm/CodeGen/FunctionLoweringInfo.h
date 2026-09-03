//===- FunctionLoweringInfo.h - Lower functions from LLVM IR ---*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements routines for translating functions from LLVM IR into
// Machine IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_FUNCTIONLOWERINGINFO_H
#define LLVM_CODEGEN_FUNCTIONLOWERINGINFO_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/KnownBits.h"
#include <cassert>
#include <utility>
#include <vector>

namespace llvm {

class Argument;
class BasicBlock;
class BranchProbabilityInfo;
class Function;
class Instruction;
class MachineFunction;
class MachineInstr;
class MachineRegisterInfo;
class MVT;
class SelectionDAG;
class TargetLowering;

template <typename T> class GenericSSAContext;
/// SSA context specialized for LLVM IR functions.
using SSAContext = GenericSSAContext<Function>;
template <typename T> class GenericUniformityInfo;
using UniformityInfo = GenericUniformityInfo<SSAContext>;

//===--------------------------------------------------------------------===//
/// FunctionLoweringInfo - This contains information that is global to a
/// function that is used when lowering a region of the function.
///
class FunctionLoweringInfo {
public:
  /// The LLVM IR function currently being lowered.
  const Function *Fn;
  /// The MachineFunction being constructed for \c Fn.
  MachineFunction *MF;
  /// Target lowering hooks for the current target.
  const TargetLowering *TLI;
  /// Register information for the current MachineFunction.
  MachineRegisterInfo *RegInfo;
  /// Optional branch-probability analysis used during lowering.
  BranchProbabilityInfo *BPI;
  /// Optional uniformity analysis used during divergence-aware isel.
  const UniformityInfo *UA;
  /// CanLowerReturn - true iff the function's return value can be lowered to
  /// registers.
  bool CanLowerReturn;

  /// True if part of the CSRs will be handled via explicit copies.
  bool SplitCSR;

  /// DemoteRegister - if CanLowerReturn is false, DemoteRegister is a vreg
  /// allocated to hold a pointer to the hidden sret parameter.
  Register DemoteRegister;

  /// A mapping from LLVM basic block number to their machine block.
  SmallVector<MachineBasicBlock *> MBBMap;

  /// ValueMap - Since we emit code for the function a basic block at a time,
  /// we must remember which virtual registers hold the values for
  /// cross-basic-block values.
  DenseMap<const Value *, Register> ValueMap;

  /// Inverse of \c ValueMap from virtual register to IR value.
  ///
  /// VirtReg2Value map is needed by the Divergence Analysis driven
  /// instruction selection. It is reverted ValueMap. It is computed
  /// in lazy style - on demand. It is used to get the Value corresponding
  /// to the live in virtual register and is called from the
  /// TargetLowerinInfo::isSDNodeSourceOfDivergence.
  DenseMap<Register, const Value*> VirtReg2Value;

  /// This method is called from TargetLowerinInfo::isSDNodeSourceOfDivergence
  /// to get the Value corresponding to the live-in virtual register.
  ///
  /// \param Vreg Live-in virtual register to look up.
  /// \return IR value corresponding to \p Vreg, or nullptr if none.
  LLVM_ABI const Value *getValueFromVirtualReg(Register Vreg);

  /// Track virtual registers created for exception pointers.
  DenseMap<const Value *, Register> CatchPadExceptionPointers;

  /// Helper object to track which of three possible relocation mechanisms are
  /// used for a particular value being relocated over a statepoint.
  struct StatepointRelocationRecord {
    /// Strategy used to relocate a value across a statepoint.
    enum RelocType {
      /// Value did not need to be relocated and can be used directly.
      NoRelocate,
      /// Value was spilled to stack and needs filled at the gc.relocate.
      Spill,
      /// Value was lowered to tied def and gc.relocate should be replaced with
      /// copy from vreg.
      VReg,
      /// Value was lowered to tied def and gc.relocate should be replaced with
      /// SDValue kept in StatepointLoweringInfo structure. This valid for local
      /// relocates only.
      SDValueNode,
    } type = NoRelocate; ///< Relocation strategy for this value.
    /// Frame index or virtual register carrying the relocated value.
    ///
    /// Payload contains either frame index of the stack slot in which the value
    /// was spilled, or virtual register which contains the re-definition.
    union payload_t {
      /// Construct an empty payload with an invalid frame index.
      payload_t() : FI(-1) {}
      /// Frame index of the stack slot holding a spilled relocated value.
      int FI;
      /// Virtual register holding a re-defined relocated value.
      Register Reg;
    } payload; ///< Spill slot or vreg for the relocated value.
  };

  /// Map from relocated IR values to their statepoint relocation records.
  ///
  /// Keep track of each value which was relocated and the strategy used to
  /// relocate that value.  This information is required when visiting
  /// gc.relocates which may appear in following blocks.
  using StatepointSpillMapTy =
    DenseMap<const Value *, StatepointRelocationRecord>;
  /// Per-statepoint maps of relocated values to relocation records.
  DenseMap<const Instruction *, StatepointSpillMapTy> StatepointRelocationMaps;

  /// Frame indices for fixed-size entry-block allocas.
  ///
  /// StaticAllocaMap - Keep track of frame indices for fixed sized allocas in
  /// the entry block.  This allows the allocas to be efficiently referenced
  /// anywhere in the function.
  DenseMap<const AllocaInst*, int> StaticAllocaMap;

  /// ByValArgFrameIndexMap - Keep track of frame indices for byval arguments.
  DenseMap<const Argument*, int> ByValArgFrameIndexMap;

  /// ArgDbgValues - A list of DBG_VALUE instructions created during isel for
  /// function arguments that are inserted after scheduling is completed.
  SmallVector<MachineInstr*, 8> ArgDbgValues;

  /// Bitvector with a bit set if corresponding argument is described in
  /// ArgDbgValues. Using arg numbers according to Argument numbering.
  BitVector DescribedArgs;

  /// RegFixups - Registers which need to be replaced after isel is done.
  DenseMap<Register, Register> RegFixups;

  /// Set of virtual registers that have pending register fixups.
  DenseSet<Register> RegsWithFixups;

  /// Temporary stack slots used to spill values at statepoints.
  ///
  /// StatepointStackSlots - A list of temporary stack slots (frame indices)
  /// used to spill values at a statepoint.  We store them here to enable
  /// reuse of the same stack slots across different statepoints in different
  /// basic blocks.
  SmallVector<unsigned, 50> StatepointStackSlots;

  /// MBB - The current block.
  MachineBasicBlock *MBB;

  /// MBB - The current insert position inside the current block.
  MachineBasicBlock::iterator InsertPt;

  /// Known-bits and sign-bit info for a live-out virtual register.
  struct LiveOutInfo {
    /// Number of known high sign bits for the live-out value.
    unsigned NumSignBits : 31;
    /// True if this LiveOutInfo is still valid.
    unsigned IsValid : 1;
    /// Known zero and one bits for the live-out value.
    KnownBits Known;

    /// Construct default live-out info with one bit of unknown known-bits.
    LiveOutInfo() : NumSignBits(0), IsValid(true), Known(1) {}
  };

  /// Record the preferred extend type (ISD::SIGN_EXTEND or ISD::ZERO_EXTEND)
  /// for a value.
  DenseMap<const Value *, ISD::NodeType> PreferredExtendType;

  /// The set of basic blocks visited thus far by instruction selection. Indexed
  /// by basic block number.
  SmallVector<bool> VisitedBBs;

  /// PHI machine instructions whose operands need updating after this block.
  ///
  /// PHINodesToUpdate - A list of phi instructions whose operand list will
  /// be updated after processing the current basic block.
  /// TODO: This isn't per-function state, it's per-basic-block state. But
  /// there's no other convenient place for it to live right now.
  std::vector<std::pair<MachineInstr*, Register>> PHINodesToUpdate;
  /// Number of PHI nodes to update when processing of this block began.
  unsigned OrigNumPHINodesToUpdate;

  /// Virtual registers for the exception pointer and selector in a landing pad.
  ///
  /// If the current MBB is a landing pad, the exception pointer and exception
  /// selector registers are copied into these virtual registers by
  /// SelectionDAGISel::PrepareEHLandingPad().
  Register ExceptionPointerVirtReg, ExceptionSelectorVirtReg;

  /// The current call site index being processed, if any. 0 if none.
  unsigned CurCallSite = 0;

  /// Collection of dbg_declare instructions handled after argument
  /// lowering and before ISel proper.
  SmallPtrSet<const DbgVariableRecord *, 8> PreprocessedDVRDeclares;

  /// set - Initialize this FunctionLoweringInfo with the given Function
  /// and its associated MachineFunction.
  ///
  /// \param Fn Function being lowered.
  /// \param MF MachineFunction being constructed for \p Fn.
  /// \param DAG SelectionDAG used during lowering.
  LLVM_ABI void set(const Function &Fn, MachineFunction &MF, SelectionDAG *DAG);

  /// clear - Clear out all the function-specific state. This returns this
  /// FunctionLoweringInfo to an empty state, ready to be used for a
  /// different function.
  LLVM_ABI void clear();

  /// isExportedInst - Return true if the specified value is an instruction
  /// exported from its block.
  ///
  /// \param V Value to test for cross-block export.
  /// \return True if \p V is exported from its block via ValueMap.
  bool isExportedInst(const Value *V) const {
    return ValueMap.count(V);
  }

  /// Return the MachineBasicBlock corresponding to IR basic block \p BB.
  ///
  /// \param BB IR basic block to look up.
  /// \return Machine basic block corresponding to \p BB.
  MachineBasicBlock *getMBB(const BasicBlock *BB) const {
    assert(BB->getNumber() < MBBMap.size() && "uninitialized MBBMap?");
    return MBBMap[BB->getNumber()];
  }

  /// Create a new virtual register of type \p VT.
  ///
  /// \param VT Machine value type of the register.
  /// \param isDivergent True if the register holds a divergent value.
  /// \return Newly created virtual register.
  LLVM_ABI Register CreateReg(MVT VT, bool isDivergent = false);

  /// Create virtual registers needed to hold IR value \p V.
  ///
  /// \param V IR value for which registers are created.
  /// \return First virtual register created for \p V.
  LLVM_ABI Register CreateRegs(const Value *V);

  /// Create virtual registers needed to hold a value of type \p Ty.
  ///
  /// \param Ty IR type for which registers are created.
  /// \param isDivergent True if the value is divergent.
  /// \return First virtual register created for type \p Ty.
  LLVM_ABI Register CreateRegs(Type *Ty, bool isDivergent = false);

  /// Allocate and record the virtual register for IR value \p V in ValueMap.
  ///
  /// \param V IR value to assign a register.
  /// \return Virtual register allocated for \p V.
  LLVM_ABI Register InitializeRegForValue(const Value *V);

  /// GetLiveOutRegInfo - Gets LiveOutInfo for a register, returning NULL if the
  /// register is a PHI destination and the PHI's LiveOutInfo is not valid.
  ///
  /// \param Reg Virtual register whose live-out info is requested.
  /// \return Live-out info for \p Reg, or nullptr if missing or invalid.
  const LiveOutInfo *GetLiveOutRegInfo(Register Reg) {
    if (!LiveOutRegInfo.inBounds(Reg))
      return nullptr;

    const LiveOutInfo *LOI = &LiveOutRegInfo[Reg];
    if (!LOI->IsValid)
      return nullptr;

    return LOI;
  }

  /// Get live-out info for \p Reg, optionally extended to \p BitWidth.
  ///
  /// Gets LiveOutInfo for a register, returning NULL if the register is a PHI
  /// destination and the PHI's LiveOutInfo is not valid. If the register's
  /// LiveOutInfo is for a smaller bit width, it is extended to the larger bit
  /// width by zero extension. The bit width must be no smaller than the
  /// LiveOutInfo's existing bit width.
  ///
  /// \param Reg Virtual register whose live-out info is requested.
  /// \param BitWidth Desired bit width; may zero-extend a narrower KnownBits.
  /// \return Live-out info for \p Reg, possibly zero-extended, or nullptr.
  LLVM_ABI const LiveOutInfo *GetLiveOutRegInfo(Register Reg,
                                                unsigned BitWidth);

  /// AddLiveOutRegInfo - Adds LiveOutInfo for a register.
  ///
  /// \param Reg Virtual register to record live-out info for.
  /// \param NumSignBits Number of known high sign bits.
  /// \param Known Known zero and one bits for the value.
  void AddLiveOutRegInfo(Register Reg, unsigned NumSignBits,
                         const KnownBits &Known) {
    // Only install this information if it tells us something.
    if (NumSignBits == 1 && Known.isUnknown())
      return;

    LiveOutRegInfo.grow(Reg);
    LiveOutInfo &LOI = LiveOutRegInfo[Reg];
    LOI.NumSignBits = NumSignBits;
    LOI.Known.One = Known.One;
    LOI.Known.Zero = Known.Zero;
  }

  /// ComputePHILiveOutRegInfo - Compute LiveOutInfo for a PHI's destination
  /// register based on the LiveOutInfo of its operands.
  ///
  /// \param PN PHI whose destination live-out info should be computed.
  LLVM_ABI void ComputePHILiveOutRegInfo(const PHINode *PN);

  /// InvalidatePHILiveOutRegInfo - Invalidates a PHI's LiveOutInfo, to be
  /// called when a block is visited before all of its predecessors.
  ///
  /// \param PN PHI whose live-out info should be marked invalid.
  void InvalidatePHILiveOutRegInfo(const PHINode *PN) {
    // PHIs with no uses have no ValueMap entry.
    auto It = ValueMap.find(PN);
    if (It == ValueMap.end())
      return;

    Register Reg = It->second;
    if (Reg == 0)
      return;

    LiveOutRegInfo.grow(Reg);
    LiveOutRegInfo[Reg].IsValid = false;
  }

  /// setArgumentFrameIndex - Record frame index for the byval
  /// argument.
  ///
  /// \param A Byval argument whose frame index is recorded.
  /// \param FI Frame index assigned to \p A.
  LLVM_ABI void setArgumentFrameIndex(const Argument *A, int FI);

  /// getArgumentFrameIndex - Get frame index for the byval argument.
  ///
  /// \param A Byval argument whose frame index is requested.
  /// \return Frame index for \p A, or INT_MAX if none is recorded.
  LLVM_ABI int getArgumentFrameIndex(const Argument *A);

  /// Return or create the virtual register for catchpad exception pointer \p CPI.
  ///
  /// \param CPI Catchpad or related value identifying the exception pointer.
  /// \param RC Register class for the exception pointer vreg.
  /// \return Virtual register holding the catchpad exception pointer.
  LLVM_ABI Register getCatchPadExceptionPointerVReg(
      const Value *CPI, const TargetRegisterClass *RC);

  /// Set the call site currently being processed.
  ///
  /// \param Site Call site index to record, or 0 if none.
  void setCurrentCallSite(unsigned Site) { CurCallSite = Site; }

  /// Get the call site currently being processed, if any. Return zero if none.
  ///
  /// \return The current call site index, or 0 if none.
  unsigned getCurrentCallSite() { return CurCallSite; }

private:
  /// LiveOutRegInfo - Information about live out vregs.
  IndexedMap<LiveOutInfo, VirtReg2IndexFunctor> LiveOutRegInfo;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_FUNCTIONLOWERINGINFO_H
