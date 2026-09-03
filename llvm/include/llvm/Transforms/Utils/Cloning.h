//===- Cloning.h - Clone various parts of LLVM programs ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines various functions that are used to clone chunks of LLVM
// code for various purposes.  This varies from copying whole modules into new
// modules, to cloning functions with different arguments, to inlining
// functions, to copying basic blocks to support loop unrolling or superblock
// formation, etc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_CLONING_H
#define LLVM_TRANSFORMS_UTILS_CLONING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <functional>
#include <memory>
#include <vector>

namespace llvm {

class AAResults;
class AllocaInst;
class BasicBlock;
class BlockFrequencyInfo;
class DebugInfoFinder;
class DominatorTree;
class Function;
class Instruction;
class Loop;
class LoopInfo;
class Module;
class OptimizationRemarkEmitter;
class PGOContextualProfile;
class ProfileSummaryInfo;
class ReturnInst;
class DomTreeUpdater;

/// Return an exact copy of the specified module.
///
/// \param M Module to clone.
/// \return A new module that is an exact copy of \p M.
LLVM_ABI std::unique_ptr<Module> CloneModule(const Module &M);

/// Return a copy of the specified module, recording mappings in \p VMap.
///
/// \param M Module to clone.
/// \param VMap Mapping from original values to their clones; filled by this
/// call.
/// \return A new module that is a copy of \p M.
LLVM_ABI std::unique_ptr<Module> CloneModule(const Module &M,
                                             ValueToValueMapTy &VMap);

/// Return a copy of the specified module with selective definition cloning.
///
/// The ShouldCloneDefinition function controls whether a specific GlobalValue's
/// definition is cloned. If the function returns false, the module copy will
/// contain an external reference in place of the global definition.
///
/// \param M Module to clone.
/// \param VMap Mapping from original values to their clones; filled by this
/// call.
/// \param ShouldCloneDefinition Predicate that returns true when a global's
/// definition should be cloned rather than left as an external declaration.
/// \return A new module whose definitions are filtered by
/// \p ShouldCloneDefinition.
LLVM_ABI std::unique_ptr<Module>
CloneModule(const Module &M, ValueToValueMapTy &VMap,
            function_ref<bool(const GlobalValue *)> ShouldCloneDefinition);

/// This struct can be used to capture information about code
/// being cloned, while it is being cloned.
struct ClonedCodeInfo {
  /// This is set to true if the cloned code contains a normal call instruction.
  bool ContainsCalls = false;

  /// This is set to true if there is memprof related metadata (memprof or
  /// callsite metadata) in the cloned code.
  bool ContainsMemProfMetadata = false;

  /// Whether the cloned code contains a dynamic alloca.
  ///
  /// This is set to true if the cloned code contains a 'dynamic' alloca.
  /// Dynamic allocas are allocas that are either not in the entry block or they
  /// are in the entry block but are not a constant size.
  bool ContainsDynamicAllocas = false;

  /// Cloned call sites that had operand bundles attached.
  ///
  /// All cloned call sites that have operand bundles attached are appended to
  /// this vector.  This vector may contain nulls or undefs if some of the
  /// originally inserted callsites were DCE'ed after they were cloned.
  std::vector<WeakTrackingVH> OperandBundleCallSites;

  /// Unsimplified instruction mappings used by isSimplified().
  ///
  /// Like VMap, but maps only unsimplified instructions. Values in the map
  /// may be dangling, it is only intended to be used via isSimplified(), to
  /// check whether the main VMap mapping involves simplification or not.
  DenseMap<const Value *, const Value *> OrigVMap;

  /// Cloned calls that were originally an indirect call.
  ///
  /// They may be direct or indirect after cloning.
  SmallSetVector<const Value *, 4> OriginallyIndirectCalls;

  /// Construct a default-initialized ClonedCodeInfo.
  ClonedCodeInfo() = default;

  /// Return true if cloning mapped \p From to a simplified \p To.
  ///
  /// \param From Original value before cloning.
  /// \param To Mapped value after cloning.
  /// \return True if \p To is a simplified mapping of \p From.
  bool isSimplified(const Value *From, const Value *To) const {
    return OrigVMap.lookup(From) != To;
  }
};

/// Return a copy of the specified basic block without embedding it.
///
/// Return a copy of the specified basic block, but without embedding the block
/// into a particular function.  The block returned is an exact copy of the
/// specified basic block, without any remapping having been performed.  Because
/// of this, this is only suitable for applications where the basic block will
/// be inserted into the same function that it was cloned from (loop unrolling
/// would use this, for example).
///
/// Also, note that this function makes a direct copy of the basic block, and
/// can thus produce illegal LLVM code.  In particular, it will copy any PHI
/// nodes from the original block, even though there are no predecessors for the
/// newly cloned block (thus, phi nodes will have to be updated).  Also, this
/// block will branch to the old successors of the original block: these
/// successors will have to have any PHI nodes updated to account for the new
/// incoming edges.
///
/// The correlation between instructions in the source and result basic blocks
/// is recorded in the VMap map.
///
/// If you have a particular suffix you'd like to use to add to any cloned
/// names, specify it as the optional third parameter.
///
/// If you would like the basic block to be auto-inserted into the end of a
/// function, you can specify it as the optional fourth parameter.
///
/// If you would like to collect additional information about the cloned
/// function, you can specify a ClonedCodeInfo object with the optional fifth
/// parameter.
///
/// \p MapAtoms indicates whether source location atoms should be mapped for
/// later remapping. Must be true when you duplicate a code path and a source
/// location is intended to appear twice in the generated instructions. Can be
/// set to false if you are transplanting code from one place to another.
/// Setting true (default) is always safe (won't produce incorrect debug info)
/// but is sometimes unnecessary, causing extra work that could be avoided by
/// setting the parameter to false.
///
/// \param BB Basic block to clone.
/// \param VMap Mapping updated with correspondences from original instructions
/// to their clones.
/// \param NameSuffix Optional suffix appended to cloned value names.
/// \param F If non-null, the cloned block is appended to this function.
/// \param CodeInfo Optional collector for statistics about the cloned code.
/// \param MapAtoms Whether to map source-location atoms for later remapping.
/// \return A clone of \p BB, optionally appended to \p F.
LLVM_ABI BasicBlock *
CloneBasicBlock(const BasicBlock *BB, ValueToValueMapTy &VMap,
                const Twine &NameSuffix = "", Function *F = nullptr,
                ClonedCodeInfo *CodeInfo = nullptr, bool MapAtoms = true);

/// Mark a cloned instruction as a new instance so that its source loc can
/// be updated when remapped.
///
/// \param DL Debug location whose atom instance should be recorded.
/// \param VMap Mapping that records the new atom instance.
LLVM_ABI void mapAtomInstance(const DebugLoc &DL, ValueToValueMapTy &VMap);

/// Return a copy of the specified function and add it to its module.
///
/// Also, any references specified in the VMap are changed to refer to their
/// mapped value instead of the original one.  If any of the arguments to the
/// function are in the VMap, the arguments are deleted from the resultant
/// function.  The VMap is updated to include mappings from all of the
/// instructions and basicblocks in the function from their old to new values.
/// The final argument captures information about the cloned code if non-null.
///
/// \pre VMap contains no non-identity GlobalValue mappings.
///
/// \param F Function to clone.
/// \param VMap Mapping of values to rematerialize in the clone; updated with
/// new instruction and basic-block mappings.
/// \param CodeInfo Optional collector for statistics about the cloned code.
/// \return The newly cloned function, added to \p F's module.
LLVM_ABI Function *CloneFunction(Function *F, ValueToValueMapTy &VMap,
                                 ClonedCodeInfo *CodeInfo = nullptr);

/// Controls how much metadata and module state CloneFunctionInto updates.
enum class CloneFunctionChangeType {
  /// Only function-local remapping; no non-identity GlobalValue mappings.
  LocalChangesOnly,
  /// Clone referenced metadata as needed within the same module.
  GlobalChanges,
  /// Clone into a different module and update that module's \c !llvm.dbg.cu.
  DifferentModule,
  /// Clone into a different module; caller updates \c !llvm.dbg.cu.
  ClonedModule,
};

/// Clone OldFunc into NewFunc using the provided value map.
///
/// Clone OldFunc into NewFunc, transforming the old arguments into references
/// to VMap values.  Note that if NewFunc already has basic blocks, the ones
/// cloned into it will be added to the end of the function.  This function
/// fills in a list of return instructions, and can optionally remap types
/// and/or append the specified suffix to all values cloned.
///
/// If \p Changes is \a CloneFunctionChangeType::LocalChangesOnly, VMap is
/// required to contain no non-identity GlobalValue mappings. Otherwise,
/// referenced metadata will be cloned.
///
/// If \p Changes is less than \a CloneFunctionChangeType::DifferentModule
/// indicating cloning into the same module (even if it's LocalChangesOnly), if
/// debug info metadata transitively references a \a DISubprogram, it will be
/// cloned, effectively upgrading \p Changes to GlobalChanges while suppressing
/// cloning of types and compile units.
///
/// If \p Changes is \a CloneFunctionChangeType::DifferentModule, the new
/// module's \c !llvm.dbg.cu will get updated with any newly created compile
/// units. (\a CloneFunctionChangeType::ClonedModule leaves that work for the
/// caller.)
///
/// FIXME: Consider simplifying this function by splitting out \a
/// CloneFunctionMetadataInto() and expecting / updating callers to call it
/// first when / how it's needed.
///
/// \param NewFunc Destination function that receives the cloned body.
/// \param OldFunc Source function to clone from.
/// \param VMap Mapping from old values (including arguments) to new ones.
/// \param Changes How aggressively to clone metadata and update module state.
/// \param Returns Filled with return instructions from the cloned body.
/// \param NameSuffix Optional suffix appended to cloned value names.
/// \param CodeInfo Optional collector for statistics about the cloned code.
/// \param TypeMapper Optional remapper for types encountered while cloning.
/// \param Materializer Optional materializer for values missing from \p VMap.
LLVM_ABI void CloneFunctionInto(Function *NewFunc, const Function *OldFunc,
                                ValueToValueMapTy &VMap,
                                CloneFunctionChangeType Changes,
                                SmallVectorImpl<ReturnInst *> &Returns,
                                const char *NameSuffix = "",
                                ClonedCodeInfo *CodeInfo = nullptr,
                                ValueMapTypeRemapper *TypeMapper = nullptr,
                                ValueMaterializer *Materializer = nullptr);

/// Clone OldFunc's attributes into NewFunc, transforming values based on the
/// mappings in VMap.
///
/// \param NewFunc Destination function that receives the cloned attributes.
/// \param OldFunc Source function whose attributes are copied.
/// \param VMap Mapping used to remap attribute values.
/// \param ModuleLevelChanges Whether module-level value remapping is allowed.
/// \param TypeMapper Optional remapper for types encountered while cloning.
/// \param Materializer Optional materializer for values missing from \p VMap.
LLVM_ABI void
CloneFunctionAttributesInto(Function *NewFunc, const Function *OldFunc,
                            ValueToValueMapTy &VMap, bool ModuleLevelChanges,
                            ValueMapTypeRemapper *TypeMapper = nullptr,
                            ValueMaterializer *Materializer = nullptr);

/// Clone OldFunc's metadata into NewFunc.
///
/// The caller is expected to populate \p VMap beforehand and set an appropriate
/// \p RemapFlag. Subprograms/CUs/types that were already mapped to themselves
/// won't be duplicated.
///
/// NOTE: This function doesn't clone !llvm.dbg.cu when cloning into a different
/// module. Use CloneFunctionInto for that behavior.
///
/// \param NewFunc Destination function that receives the cloned metadata.
/// \param OldFunc Source function whose metadata is copied.
/// \param VMap Mapping used to remap metadata operands.
/// \param RemapFlag Flags controlling how metadata values are remapped.
/// \param TypeMapper Optional remapper for types encountered while cloning.
/// \param Materializer Optional materializer for values missing from \p VMap.
/// \param IdentityMD Optional predicate identifying metadata that maps to
/// itself.
LLVM_ABI void
CloneFunctionMetadataInto(Function &NewFunc, const Function &OldFunc,
                          ValueToValueMapTy &VMap, RemapFlags RemapFlag,
                          ValueMapTypeRemapper *TypeMapper = nullptr,
                          ValueMaterializer *Materializer = nullptr,
                          const MetadataPredicate *IdentityMD = nullptr);

/// Clone OldFunc's body into NewFunc.
///
/// \param NewFunc Destination function that receives the cloned body.
/// \param OldFunc Source function whose body is copied.
/// \param VMap Mapping used to remap instructions and operands.
/// \param RemapFlag Flags controlling how values are remapped.
/// \param Returns Filled with return instructions from the cloned body.
/// \param NameSuffix Optional suffix appended to cloned value names.
/// \param CodeInfo Optional collector for statistics about the cloned code.
/// \param TypeMapper Optional remapper for types encountered while cloning.
/// \param Materializer Optional materializer for values missing from \p VMap.
/// \param IdentityMD Optional predicate identifying metadata that maps to
/// itself.
LLVM_ABI void CloneFunctionBodyInto(
    Function &NewFunc, const Function &OldFunc, ValueToValueMapTy &VMap,
    RemapFlags RemapFlag, SmallVectorImpl<ReturnInst *> &Returns,
    const char *NameSuffix = "", ClonedCodeInfo *CodeInfo = nullptr,
    ValueMapTypeRemapper *TypeMapper = nullptr,
    ValueMaterializer *Materializer = nullptr,
    const MetadataPredicate *IdentityMD = nullptr);

/// Clone and prune OldFunc into NewFunc starting at a given instruction.
///
/// Behaves like CloneAndPruneFunctionInto, but begins cloning at
/// \p StartingInst rather than at the entry block.
///
/// \param NewFunc Destination function that receives the pruned clone.
/// \param OldFunc Source function to clone from.
/// \param StartingInst First instruction of OldFunc to begin cloning at.
/// \param VMap Mapping from old values to new ones; updated during cloning.
/// \param ModuleLevelChanges Whether module-level value remapping is allowed.
/// \param Returns Filled with return instructions from the cloned body.
/// \param NameSuffix Suffix appended to cloned value names.
/// \param CodeInfo Collector for statistics about the cloned code.
LLVM_ABI void
CloneAndPruneIntoFromInst(Function *NewFunc, const Function *OldFunc,
                          const Instruction *StartingInst,
                          ValueToValueMapTy &VMap, bool ModuleLevelChanges,
                          SmallVectorImpl<ReturnInst *> &Returns,
                          const char *NameSuffix, ClonedCodeInfo &CodeInfo);

/// Clone OldFunc into NewFunc with on-the-fly constant prop and DCE.
///
/// This works exactly like CloneFunctionInto, except that it does some simple
/// constant prop and DCE on the fly.  The effect of this is to copy
/// significantly less code in cases where (for example) a function call with
/// constant arguments is inlined, and those constant arguments cause a
/// significant amount of code in the callee to be dead.  Since this doesn't
/// produce an exactly copy of the input, it can't be used for things like
/// CloneFunction or CloneModule.
///
/// If ModuleLevelChanges is false, VMap contains no non-identity GlobalValue
/// mappings.
///
/// \param NewFunc Destination function that receives the pruned clone.
/// \param OldFunc Source function to clone from.
/// \param VMap Mapping from old values to new ones; updated during cloning.
/// \param ModuleLevelChanges Whether module-level value remapping is allowed.
/// \param Returns Filled with return instructions from the cloned body.
/// \param NameSuffix Suffix appended to cloned value names.
/// \param CodeInfo Collector for statistics about the cloned code.
LLVM_ABI void
CloneAndPruneFunctionInto(Function *NewFunc, const Function *OldFunc,
                          ValueToValueMapTy &VMap, bool ModuleLevelChanges,
                          SmallVectorImpl<ReturnInst *> &Returns,
                          const char *NameSuffix, ClonedCodeInfo &CodeInfo);

/// This class captures the data input to the InlineFunction call, and records
/// the auxiliary results produced by it.
class InlineFunctionInfo {
public:
  /// Construct inlining context with optional profile and assumption helpers.
  ///
  /// \param GetAssumptionCache Callback that returns the assumption cache for
  /// a function, or null if assumption caches are unused.
  /// \param PSI Optional profile summary information used when updating
  /// profiles.
  /// \param CallerBFI Optional block-frequency info for the caller.
  /// \param CalleeBFI Optional block-frequency info for the callee.
  /// \param UpdateProfile Whether callee and cloned profiles should be
  /// updated.
  explicit InlineFunctionInfo(
      function_ref<AssumptionCache &(Function &)> GetAssumptionCache = nullptr,
      ProfileSummaryInfo *PSI = nullptr,
      BlockFrequencyInfo *CallerBFI = nullptr,
      BlockFrequencyInfo *CalleeBFI = nullptr, bool UpdateProfile = true)
      : GetAssumptionCache(GetAssumptionCache), PSI(PSI), CallerBFI(CallerBFI),
        CalleeBFI(CalleeBFI), UpdateProfile(UpdateProfile) {}

  /// If non-null, InlineFunction will update the callgraph to reflect the
  /// changes it makes.
  function_ref<AssumptionCache &(Function &)> GetAssumptionCache;
  /// Profile summary information used when updating profiles during inlining.
  ProfileSummaryInfo *PSI;
  /// Block-frequency information for the caller, if available.
  BlockFrequencyInfo *CallerBFI;
  /// Block-frequency information for the callee, if available.
  BlockFrequencyInfo *CalleeBFI;

  /// InlineFunction fills this in with all static allocas that get copied into
  /// the caller.
  SmallVector<AllocaInst *, 4> StaticAllocas;

  /// All of the new call sites inlined into the caller.
  ///
  /// 'InlineFunction' fills this in by scanning the inlined instructions.
  SmallVector<CallBase *, 8> InlinedCallSites;

  /// Convergence control token from the callsite, if present.
  Value *ConvergenceControlToken = nullptr;
  /// Exception-handling pad associated with the callsite, if present.
  Instruction *CallSiteEHPad = nullptr;

  /// Update profile for callee as well as cloned version. We need to do this
  /// for regular inlining, but not for inlining from sample profile loader.
  bool UpdateProfile;

  /// Clear transient results produced by the last InlineFunction call.
  void reset() {
    StaticAllocas.clear();
    InlinedCallSites.clear();
    ConvergenceControlToken = nullptr;
    CallSiteEHPad = nullptr;
  }
};

/// Check if it is legal to perform inlining of the function called by \p CB
/// into the caller at this particular use, and sets fields in \p IFI.
///
/// This does not consider whether it is possible for the function callee itself
/// to be inlined; for that see isInlineViable.
///
/// \param CB Call site being considered for inlining.
/// \param IFI Inlining context updated with callsite-specific state on success.
/// \return Success if the callsite may be inlined, otherwise a failure reason.
LLVM_ABI InlineResult CanInlineCallSite(const CallBase &CB,
                                        InlineFunctionInfo &IFI);

/// This should generally not be used, use InlineFunction instead.
///
/// Perform mechanical inlining of \p CB into the caller.
///
/// This does not perform any legality or profitability checks for the
/// inlining. This assumes that CanInlineCallSite was already called, populated
/// \p IFI, and returned InlineResult::success.
///
/// Also assumes that isInlineViable returned InlineResult::success for the
/// called function.
///
/// \param CB Call site to inline mechanically into its caller.
/// \param IFI Inlining context previously populated by CanInlineCallSite.
/// \param MergeAttributes Whether to merge callee attributes into the caller.
/// \param CalleeAAR Optional alias-analysis results for the callee.
/// \param InsertLifetime Whether to insert lifetime markers for inlined
/// allocas.
/// \param TrackInlineHistory Whether to record inline history metadata.
/// \param ForwardVarArgsTo If set, forward varargs from the callsite to calls
/// to this function.
/// \param ORE Optional optimization-remark emitter for diagnostics.
LLVM_ABI void InlineFunctionImpl(CallBase &CB, InlineFunctionInfo &IFI,
                                 bool MergeAttributes = false,
                                 AAResults *CalleeAAR = nullptr,
                                 bool InsertLifetime = true,
                                 bool TrackInlineHistory = false,
                                 Function *ForwardVarArgsTo = nullptr,
                                 OptimizationRemarkEmitter *ORE = nullptr);

/// Inline the callee of \p CB into its caller's basic block.
///
/// This function inlines the called function into the basic block of the
/// caller.  This returns false if it is not possible to inline this call.  The
/// program is still in a well defined state if this occurs though.
///
/// Note that this only does one level of inlining.  For example, if the
/// instruction 'call B' is inlined, and 'B' calls 'C', then the call to 'C' now
/// exists in the instruction stream.  Similarly this will inline a recursive
/// function by one level.
///
/// Note that while this routine is allowed to cleanup and optimize the
/// *inlined* code to minimize the actual inserted code, it must not delete
/// code in the caller as users of this routine may have pointers to
/// instructions in the caller that need to remain stable.
///
/// If ForwardVarArgsTo is passed, inlining a function with varargs is allowed
/// and all varargs at the callsite will be passed to any calls to
/// ForwardVarArgsTo. The caller of InlineFunction has to make sure any varargs
/// are only used by ForwardVarArgsTo.
///
/// The callee's function attributes are merged into the callers' if
/// MergeAttributes is set to true.
///
/// \param CB Call site whose callee should be inlined.
/// \param IFI Inlining context and result collector.
/// \param MergeAttributes Whether to merge callee attributes into the caller.
/// \param CalleeAAR Optional alias-analysis results for the callee.
/// \param InsertLifetime Whether to insert lifetime markers for inlined
/// allocas.
/// \param TrackInlineHistory Whether to record inline history metadata.
/// \param ForwardVarArgsTo If set, forward varargs from the callsite to calls
/// to this function.
/// \param ORE Optional optimization-remark emitter for diagnostics.
/// \return Success if the call was inlined, otherwise a failure reason.
LLVM_ABI InlineResult InlineFunction(CallBase &CB, InlineFunctionInfo &IFI,
                                     bool MergeAttributes = false,
                                     AAResults *CalleeAAR = nullptr,
                                     bool InsertLifetime = true,
                                     bool TrackInlineHistory = false,
                                     Function *ForwardVarArgsTo = nullptr,
                                     OptimizationRemarkEmitter *ORE = nullptr);

/// Inline \p CB while also updating a contextual profile.
///
/// Same as above, but it will update the contextual profile. If the contextual
/// profile is invalid (i.e. not loaded because it is not present), it defaults
/// to the behavior of the non-contextual profile updating variant above. This
/// makes it easy to drop-in replace uses of the non-contextual overload.
///
/// \param CB Call site whose callee should be inlined.
/// \param IFI Inlining context and result collector.
/// \param CtxProf Contextual profile to update for the inlined callsite.
/// \param MergeAttributes Whether to merge callee attributes into the caller.
/// \param CalleeAAR Optional alias-analysis results for the callee.
/// \param InsertLifetime Whether to insert lifetime markers for inlined
/// allocas.
/// \param TrackInlineHistory Whether to record inline history metadata.
/// \param ForwardVarArgsTo If set, forward varargs from the callsite to calls
/// to this function.
/// \param ORE Optional optimization-remark emitter for diagnostics.
/// \return Success if the call was inlined, otherwise a failure reason.
LLVM_ABI InlineResult InlineFunction(CallBase &CB, InlineFunctionInfo &IFI,
                                     PGOContextualProfile &CtxProf,
                                     bool MergeAttributes = false,
                                     AAResults *CalleeAAR = nullptr,
                                     bool InsertLifetime = true,
                                     bool TrackInlineHistory = false,
                                     Function *ForwardVarArgsTo = nullptr,
                                     OptimizationRemarkEmitter *ORE = nullptr);

/// Clones a loop \p OrigLoop.
///
/// Updates LoopInfo and DominatorTree assuming the loop is dominated by block
/// \p LoopDomBB.  Insert the new blocks before block specified in \p Before.
/// The client needs to further update the CFG and DominatorTree after calling
/// this function, to ensure the IR remains valid.
/// Note: Only innermost loops are supported. The cloned blocks are also
/// recorded in \p Blocks.
///
/// \param Before Block before which newly cloned blocks are inserted.
/// \param LoopDomBB Block assumed to dominate the original loop.
/// \param OrigLoop Loop to clone.
/// \param VMap Mapping updated with correspondences for cloned values.
/// \param NameSuffix Suffix appended to cloned value and block names.
/// \param LI LoopInfo to update with the cloned loop.
/// \param DT DominatorTree to update for the cloned blocks.
/// \param Blocks Filled with the basic blocks belonging to the cloned loop.
/// \return The cloned loop.
LLVM_ABI Loop *cloneLoopWithPreheader(BasicBlock *Before, BasicBlock *LoopDomBB,
                                      Loop *OrigLoop, ValueToValueMapTy &VMap,
                                      const Twine &NameSuffix, LoopInfo *LI,
                                      DominatorTree *DT,
                                      SmallVectorImpl<BasicBlock *> &Blocks);

/// Remaps instructions in \p Blocks using the mapping in \p VMap.
///
/// \param Blocks Basic blocks whose instructions should be remapped.
/// \param VMap Mapping applied to instruction operands and metadata.
LLVM_ABI void remapInstructionsInBlocks(ArrayRef<BasicBlock *> Blocks,
                                        ValueToValueMapTy &VMap);

/// Split PredBB->BB and duplicate non-Phi instructions into the split block.
///
/// Split edge between BB and PredBB and duplicate all non-Phi instructions
/// from BB between its beginning and the StopAt instruction into the split
/// block. Phi nodes are not duplicated, but their uses are handled correctly:
/// we replace them with the uses of corresponding Phi inputs. ValueMapping
/// is used to map the original instructions from BB to their newly-created
/// copies.
///
/// \param BB Destination block whose early instructions are duplicated.
/// \param PredBB Predecessor connected to \p BB by the edge being split.
/// \param StopAt First instruction of \p BB not to duplicate into the split
/// block.
/// \param ValueMapping Mapping from original instructions in \p BB to their
/// copies in the split block.
/// \param DTU Dominator-tree updater notified about the CFG change.
/// \return The newly created split block.
LLVM_ABI BasicBlock *DuplicateInstructionsInSplitBetween(
    BasicBlock *BB, BasicBlock *PredBB, Instruction *StopAt,
    ValueToValueMapTy &ValueMapping, DomTreeUpdater &DTU);

/// Update callee profile counts after cloning or inlining.
///
/// Updates profile information by adjusting the entry count by adding
/// EntryDelta then scaling callsite information by the new count divided by the
/// old count. VMap is used during inlinng to also update the new clone.
///
/// \param Callee Function whose profile data should be updated.
/// \param EntryDelta Signed delta added to the callee's entry count.
/// \param VMap Optional mapping used during inlining to update the cloned
/// callee as well.
LLVM_ABI void updateProfileCallee(
    Function *Callee, int64_t EntryDelta,
    const ValueMap<const Value *, WeakTrackingVH> *VMap = nullptr);

/// Collect noalias scopes declared in the given basic blocks.
///
/// Find the 'llvm.experimental.noalias.scope.decl' intrinsics in the specified
/// basic blocks and extract their scope. These are candidates for duplication
/// when cloning.
///
/// \param BBs Basic blocks to scan for noalias scope declarations.
/// \param NoAliasDeclScopes Filled with the scopes declared in \p BBs.
LLVM_ABI void
identifyNoAliasScopesToClone(ArrayRef<BasicBlock *> BBs,
                             SmallVectorImpl<MDNode *> &NoAliasDeclScopes);

/// Collect noalias scopes declared in an instruction range.
///
/// Find the 'llvm.experimental.noalias.scope.decl' intrinsics in the specified
/// instruction range and extract their scope. These are candidates for
/// duplication when cloning.
///
/// \param Start Beginning of the instruction range to scan.
/// \param End End of the instruction range to scan (exclusive).
/// \param NoAliasDeclScopes Filled with the scopes declared in the range.
LLVM_ABI void
identifyNoAliasScopesToClone(BasicBlock::iterator Start,
                             BasicBlock::iterator End,
                             SmallVectorImpl<MDNode *> &NoAliasDeclScopes);

/// Duplicate the listed noalias declaration scopes.
///
/// Duplicate the specified list of noalias decl scopes. The 'Ext' string is
/// added as an extension to the name. Afterwards, the ClonedScopes contains the
/// mapping of the original scope MDNode onto the cloned scope. Be aware that
/// the cloned scopes are still part of the original scope domain.
///
/// \param NoAliasDeclScopes Noalias scopes to duplicate.
/// \param ClonedScopes Filled with a mapping from original scopes to clones.
/// \param Ext Suffix appended to the duplicated scope names.
/// \param Context LLVM context used to create the cloned metadata nodes.
LLVM_ABI void cloneNoAliasScopes(ArrayRef<MDNode *> NoAliasDeclScopes,
                                 DenseMap<MDNode *, MDNode *> &ClonedScopes,
                                 StringRef Ext, LLVMContext &Context);

/// Adapt an instruction's noalias metadata using a scope map.
///
/// Adapt the metadata for the specified instruction according to the provided
/// mapping. This is normally used after cloning an instruction, when some
/// noalias scopes needed to be cloned.
///
/// \param I Instruction whose noalias metadata should be rewritten.
/// \param ClonedScopes Mapping from original noalias scopes to their clones.
/// \param Context LLVM context used when rebuilding metadata.
LLVM_ABI void
adaptNoAliasScopes(llvm::Instruction *I,
                   const DenseMap<MDNode *, MDNode *> &ClonedScopes,
                   LLVMContext &Context);

/// Clone noalias scopes and adapt instructions in new blocks.
///
/// Clone the specified noalias decl scopes. Then adapt all instructions in the
/// NewBlocks basicblocks to the cloned versions. 'Ext' will be added to the
/// duplicate scope names.
///
/// \param NoAliasDeclScopes Noalias scopes to duplicate.
/// \param NewBlocks Blocks whose instructions should use the cloned scopes.
/// \param Context LLVM context used to create and adapt metadata.
/// \param Ext Suffix appended to the duplicated scope names.
LLVM_ABI void cloneAndAdaptNoAliasScopes(ArrayRef<MDNode *> NoAliasDeclScopes,
                                         ArrayRef<BasicBlock *> NewBlocks,
                                         LLVMContext &Context, StringRef Ext);

/// Clone noalias scopes and adapt instructions in a range.
///
/// Clone the specified noalias decl scopes. Then adapt all instructions in the
/// [IStart, IEnd] (IEnd included !) range to the cloned versions. 'Ext' will be
/// added to the duplicate scope names.
///
/// \param NoAliasDeclScopes Noalias scopes to duplicate.
/// \param IStart First instruction whose metadata should be adapted.
/// \param IEnd Last instruction whose metadata should be adapted (inclusive).
/// \param Context LLVM context used to create and adapt metadata.
/// \param Ext Suffix appended to the duplicated scope names.
LLVM_ABI void cloneAndAdaptNoAliasScopes(ArrayRef<MDNode *> NoAliasDeclScopes,
                                         Instruction *IStart, Instruction *IEnd,
                                         LLVMContext &Context, StringRef Ext);
} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_CLONING_H
