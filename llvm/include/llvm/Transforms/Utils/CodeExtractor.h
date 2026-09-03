//===- Transform/Utils/CodeExtractor.h - Code extraction util ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A utility to support extracting code from one function into its own
// stand-alone function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_CODEEXTRACTOR_H
#define LLVM_TRANSFORMS_UTILS_CODEEXTRACTOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/Compiler.h"
#include <limits>

namespace llvm {

template <typename PtrType> class SmallPtrSetImpl;
class AddrSpaceCastInst;
class AllocaInst;
class BlockFrequency;
class BlockFrequencyInfo;
class BranchProbabilityInfo;
class AssumptionCache;
class CallInst;
class DominatorTree;
class Function;
class Instruction;
class Module;
class Type;
class Value;
class StructType;

/// A cache of analysis results for CodeExtractor.
///
/// The operation \ref CodeExtractor::extractCodeRegion is guaranteed not to
/// invalidate this object. This object should conservatively be considered
/// invalid if any other mutating operations on the IR occur.
///
/// Constructing this object is O(n) in the size of the function.
class CodeExtractorAnalysisCache {
  /// The allocas in the function.
  SmallVector<AllocaInst *, 16> Allocas;

  /// Base memory addresses of load/store instructions, grouped by block.
  DenseMap<BasicBlock *, DenseSet<Value *>> BaseMemAddrs;

  /// Blocks which contain instructions which may have unknown side-effects
  /// on memory.
  DenseSet<BasicBlock *> SideEffectingBlocks;

  void findSideEffectInfoForBlock(BasicBlock &BB);

public:
  /// Construct an analysis cache for a function.
  ///
  /// \param F Function to analyze.
  LLVM_ABI CodeExtractorAnalysisCache(Function &F);

  /// Get the allocas recorded when this analysis was created.
  ///
  /// Note that some of these allocas may no longer be present in the function,
  /// due to \ref CodeExtractor::extractCodeRegion.
  ///
  /// \returns The allocas recorded for this function.
  ArrayRef<AllocaInst *> getAllocas() const { return Allocas; }

  /// Check whether \p BB contains an instruction thought to load from, store
  /// to, or otherwise clobber the alloca \p Addr.
  ///
  /// \param BB Basic block to inspect for clobbers.
  /// \param Addr Alloca whose address may be clobbered.
  /// \returns True if \p BB may clobber \p Addr.
  LLVM_ABI bool doesBlockContainClobberOfAddr(BasicBlock &BB,
                                              AllocaInst *Addr) const;
};

/// Utility class for extracting code into a new function.
///
/// This utility provides a simple interface for extracting some sequence of
/// code into its own function, replacing it with a call to that function. It
/// also provides various methods to query about the nature and result of such a
/// transformation.
///
/// The rough algorithm used is:
/// 1) Find both the inputs and outputs for the extracted region.
/// 2) Pass the inputs as arguments, remapping them within the extracted
///    function to arguments.
/// 3) Add allocas for any scalar outputs, adding all of the outputs' allocas as
///    arguments, and inserting stores to the arguments for any scalars.
class LLVM_ABI CodeExtractor {
  using ValueSet = SetVector<Value *>;

  // Various bits of state computed on construction.
  DominatorTree *const DT;
  const bool AggregateArgs;
  BlockFrequencyInfo *BFI;
  BranchProbabilityInfo *BPI;
  AssumptionCache *AC;

  /// A block outside of the extraction set where any intermediate allocations
  /// will be placed inside. If this is null, allocations will be placed in the
  /// entry block of the function.
  BasicBlock *AllocationBlock;

  /// A set of blocks outside of the extraction set where deallocations for
  /// intermediate allocations should be placed. Not used for automatically
  /// deallocated memory (e.g. `alloca`), which is the default.
  ///
  /// If it is empty and needed, the end of the replacement basic block will be
  /// used to place deallocations.
  SmallVector<BasicBlock *> DeallocationBlocks;

  /// If true, varargs functions can be extracted.
  bool AllowVarArgs;

  /// Bits of intermediate state computed at various phases of extraction.
  SetVector<BasicBlock *> Blocks;

  /// Lists of blocks that are branched from the code region to be extracted,
  /// also called the exit blocks. Each block is contained at most once. Its
  /// order defines the return value of the extracted function.
  ///
  /// When there is just one (or no) exit block, the return value is irrelevant.
  ///
  /// When there are exactly two exit blocks, the extracted function returns a
  /// boolean. For ExtractedFuncRetVals[0], it returns 'true'. For
  /// ExtractedFuncRetVals[1] it returns 'false'.
  /// NOTE: Since a boolean is represented by i1, ExtractedFuncRetVals[0]
  ///       returns 1 and ExtractedFuncRetVals[1] returns 0, which opposite of
  ///       the regular pattern below.
  ///
  /// When there are 3 or more exit blocks, leaving the extracted function via
  /// the first block it returns 0. When leaving via the second entry it returns
  /// 1, etc.
  SmallVector<BasicBlock *> ExtractedFuncRetVals;

  /// Suffix to use when creating extracted function (appended to the original
  /// function name + "."). If empty, the default is to use the entry block
  /// label, if non-empty, otherwise "extracted".
  std::string Suffix;

  /// If true, the outlined function has aggregate argument in zero address
  /// space.
  bool ArgsInZeroAddressSpace;

  // If true, the outlined function always return void even when there is only
  // one output.
  bool VoidReturnWithSingleOutput;

  // If set, the return value of the outline function.
  Value *FuncRetVal = nullptr;

public:
  /// Create a code extractor for a sequence of blocks.
  ///
  /// Given a sequence of basic blocks where the first block in the sequence
  /// dominates the rest, prepare a code extractor object for pulling this
  /// sequence out into its new function. When a DominatorTree is also given,
  /// extra checking and transformations are enabled. If AllowVarArgs is true,
  /// vararg functions can be extracted. This is safe, if all vararg handling
  /// code is extracted, including vastart. If AllowAlloca is true, then
  /// extraction of blocks containing alloca instructions would be possible,
  /// however code extractor won't validate whether extraction is legal. Any new
  /// allocations will be placed in the AllocationBlock, unless it is null, in
  /// which case it will be placed in the entry block of the function from which
  /// the code is being extracted. Explicit deallocations for the aforementioned
  /// allocations will be placed, if needed, in all blocks in DeallocationBlocks
  /// or the end of the replacement block. If ArgsInZeroAddressSpace param is
  /// set to true, then the aggregate param pointer of the outlined function is
  /// declared in zero address space. If VoidReturnWithSingleOutput is set to
  /// true, then the return type of the outlined function is set void even if
  /// there is only one output.
  ///
  /// \param BBs Sequence of basic blocks to extract; the first must dominate
  /// the rest.
  /// \param DT Optional dominator tree enabling extra checks and transforms.
  /// \param AggregateArgs Whether to pack scalar args into an aggregate.
  /// \param BFI Optional block frequency info for the extraction.
  /// \param BPI Optional branch probability info for the extraction.
  /// \param AC Optional assumption cache for the extraction.
  /// \param AllowVarArgs Whether vararg functions may be extracted.
  /// \param AllowAlloca Whether blocks containing allocas may be extracted.
  /// \param AllocationBlock Block for new allocations, or null to use the
  /// entry block.
  /// \param DeallocationBlocks Blocks where explicit deallocations are placed.
  /// \param Suffix Suffix appended to the extracted function name.
  /// \param ArgsInZeroAddressSpace Whether the aggregate arg pointer is in
  /// address space zero.
  /// \param VoidReturnWithSingleOutput Whether a single-output outline returns
  /// void.
  CodeExtractor(ArrayRef<BasicBlock *> BBs, DominatorTree *DT = nullptr,
                bool AggregateArgs = false, BlockFrequencyInfo *BFI = nullptr,
                BranchProbabilityInfo *BPI = nullptr,
                AssumptionCache *AC = nullptr, bool AllowVarArgs = false,
                bool AllowAlloca = false, BasicBlock *AllocationBlock = nullptr,
                ArrayRef<BasicBlock *> DeallocationBlocks = {},
                std::string Suffix = "", bool ArgsInZeroAddressSpace = false,
                bool VoidReturnWithSingleOutput = true);

  /// Destroy the code extractor.
  virtual ~CodeExtractor() = default;

  /// Perform the extraction, returning the new function.
  ///
  /// \param CEAC - Cache to speed up operations for the CodeExtractor when
  /// hoisting, and extracting lifetime values and assumes.
  /// \returns The new function, or null when isEligible returns false.
  Function *extractCodeRegion(const CodeExtractorAnalysisCache &CEAC);

  /// Perform the extraction, returning the new function and providing an
  /// interface to see what was categorized as inputs and outputs.
  ///
  /// \param CEAC - Cache to speed up operations for the CodeExtractor when
  /// hoisting, and extracting lifetime values and assumes.
  /// \param Inputs [in/out] - filled with  values marked as inputs to the newly
  /// outlined function.
  /// \param Outputs [out] - filled with values marked as outputs to the newly
  /// outlined function.
  /// \returns zero when called on a CodeExtractor instance where isEligible
  /// returns false.
  Function *extractCodeRegion(const CodeExtractorAnalysisCache &CEAC,
                              ValueSet &Inputs, ValueSet &Outputs);

  /// Verify that the assumption cache is not stale after extraction.
  ///
  /// AssumptionCache is passed as a parameter to make this function
  /// stateless.
  ///
  /// \param OldFunc Original function before extraction.
  /// \param NewFunc Newly extracted function.
  /// \param AC Assumption cache to verify.
  /// \returns True when the verifier finds errors.
  static bool verifyAssumptionCache(const Function &OldFunc,
                                    const Function &NewFunc,
                                    AssumptionCache *AC);

  /// Test whether this code extractor is eligible.
  ///
  /// Based on the blocks used when constructing the code extractor, determine
  /// whether it is eligible for extraction.
  ///
  /// Checks that varargs handling (with vastart and vaend) is only done in the
  /// outlined blocks.
  ///
  /// \returns True if the selected blocks are eligible for extraction.
  bool isEligible() const;

  /// Compute the set of input values and output values for the code.
  ///
  /// These can be used either when performing the extraction or to evaluate the
  /// expected size of a call to the extracted function. Note that this work
  /// cannot be cached between the two as once we decide to extract a code
  /// sequence, that sequence is modified, including changing these sets, before
  /// extraction occurs. These modifications won't have any significant impact
  /// on the cost however.
  ///
  /// \param Inputs [out] Values marked as inputs to the region.
  /// \param Outputs [out] Values marked as outputs from the region.
  /// \param Allocas Allocas already known to belong to the region.
  /// \param CollectGlobalInputs Whether to treat used globals as inputs.
  void findInputsOutputs(ValueSet &Inputs, ValueSet &Outputs,
                         const ValueSet &Allocas,
                         bool CollectGlobalInputs = false);

  /// Check if life time marker nodes can be hoisted/sunk into the outline
  /// region.
  ///
  /// \param CEAC Analysis cache used to check for clobbers.
  /// \param AllocaAddr Address of the alloca whose lifetime markers are
  /// considered.
  /// \returns True if it is safe to do the code motion.
  bool
  isLegalToShrinkwrapLifetimeMarkers(const CodeExtractorAnalysisCache &CEAC,
                                     Instruction *AllocaAddr) const;

  /// Find the set of allocas whose life ranges are contained within the
  /// outlined region.
  ///
  /// Allocas which have life_time markers contained in the outlined region
  /// should be pushed to the outlined function. The address bitcasts that are
  /// used by the lifetime markers are also candidates for shrink-wrapping. The
  /// instructions that need to be sunk are collected in 'Allocas'.
  ///
  /// \param CEAC Analysis cache used when inspecting lifetime markers.
  /// \param SinkCands [out] Instructions that should be sunk into the outlined
  /// region.
  /// \param HoistCands [out] Instructions that should be hoisted into the
  /// outlined region.
  /// \param ExitBlock [out] Exit block associated with the lifetime analysis.
  void findAllocas(const CodeExtractorAnalysisCache &CEAC, ValueSet &SinkCands,
                   ValueSet &HoistCands, BasicBlock *&ExitBlock) const;

  /// Find or create a block within the outline region for placing hoisted code.
  ///
  /// CommonExitBlock is block outside the outline region. It is the common
  /// successor of blocks inside the region. If there exists a single block
  /// inside the region that is the predecessor of CommonExitBlock, that block
  /// will be returned. Otherwise CommonExitBlock will be split and the original
  /// block will be added to the outline region.
  ///
  /// \param CommonExitBlock Common successor of blocks inside the outline
  /// region.
  /// \returns The block inside the outline region where hoisted code should be
  /// placed.
  BasicBlock *findOrCreateBlockForHoisting(BasicBlock *CommonExitBlock);

  /// Exclude a value from aggregate argument passing when extracting a code
  /// region, passing it instead as a scalar.
  ///
  /// \param Arg Value to pass as a scalar instead of packing into the
  /// aggregate.
  void excludeArgFromAggregate(Value *Arg);

protected:
  /// Allocate an intermediate variable at the specified point.
  ///
  /// \param AllocaIP Insertion point for the allocation.
  /// \param VarType Type of the variable to allocate.
  /// \param Name Optional name for the allocated value.
  /// \param CastedAlloc Optional out-parameter set to any addrspace cast of
  /// the allocation.
  /// \returns The allocated variable instruction.
  virtual Instruction *allocateVar(IRBuilder<>::InsertPoint AllocaIP,
                                   Type *VarType, const Twine &Name = Twine(""),
                                   AddrSpaceCastInst **CastedAlloc = nullptr);

  /// Deallocate a previously-allocated intermediate variable at the specified
  /// point.
  ///
  /// \param DeallocIP Insertion point for the deallocation.
  /// \param Var Value to deallocate.
  /// \param VarType Type of the variable being deallocated.
  /// \returns The deallocation instruction, or nullptr if none is needed.
  virtual Instruction *deallocateVar(IRBuilder<>::InsertPoint DeallocIP,
                                     Value *Var, Type *VarType);

private:
  struct LifetimeMarkerInfo {
    bool SinkLifeStart = false;
    bool HoistLifeEnd = false;
    Instruction *LifeStart = nullptr;
    Instruction *LifeEnd = nullptr;
  };

  ValueSet ExcludeArgsFromAggregate;

  LifetimeMarkerInfo getLifetimeMarkers(const CodeExtractorAnalysisCache &CEAC,
                                        Instruction *Addr,
                                        BasicBlock *ExitBlock) const;

  /// Updates the list of SwitchCases (corresponding to exit blocks) after
  /// changes of the control flow or the Blocks list.
  void computeExtractedFuncRetVals();

  /// Return the type used for the return code of the extracted function to
  /// indicate which exit block to jump to.
  Type *getSwitchType();

  void severSplitPHINodesOfEntry(BasicBlock *&Header);
  void severSplitPHINodesOfExits();
  void splitReturnBlocks();

  void moveCodeToFunction(Function *newFunction);

  void calculateNewCallTerminatorWeights(
      BasicBlock *CodeReplacer,
      const DenseMap<BasicBlock *, BlockFrequency> &ExitWeights,
      BranchProbabilityInfo *BPI);

  /// Normalizes the control flow of the extracted regions, such as ensuring
  /// that the extracted region does not contain a return instruction.
  void normalizeCFGForExtraction(BasicBlock *&header);

  /// Generates the function declaration for the function containing the
  /// extracted code.
  Function *
  constructFunctionDeclaration(const ValueSet &inputs, const ValueSet &outputs,
                               BlockFrequency EntryFreq, const Twine &Name,
                               ValueSet &StructValues, StructType *&StructTy);

  /// Generates the code for the extracted function. That is: a prolog, the
  /// moved or copied code from the original function, and epilogs for each
  /// exit.
  void emitFunctionBody(const ValueSet &inputs, const ValueSet &outputs,
                        const ValueSet &StructValues, Function *newFunction,
                        StructType *StructArgTy, BasicBlock *header,
                        const ValueSet &SinkingCands,
                        SmallVectorImpl<Value *> &NewValues);

  /// Generates a Basic Block that calls the extracted function.
  CallInst *emitReplacerCall(const ValueSet &inputs, const ValueSet &outputs,
                             const ValueSet &StructValues,
                             Function *newFunction, StructType *StructArgTy,
                             Function *oldFunction, BasicBlock *ReplIP,
                             BlockFrequency EntryFreq,
                             ArrayRef<Value *> LifetimesStart,
                             std::vector<Value *> &Reloads);

  /// Connects the basic block containing the call to the extracted function
  /// into the original function's control flow.
  void
  insertReplacerCall(Function *oldFunction, BasicBlock *header,
                     CallInst *ReplacerCall, const ValueSet &outputs,
                     ArrayRef<Value *> Reloads,
                     const DenseMap<BasicBlock *, BlockFrequency> &ExitWeights);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_CODEEXTRACTOR_H
