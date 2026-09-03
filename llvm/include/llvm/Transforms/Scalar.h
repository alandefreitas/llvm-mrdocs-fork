//===-- Scalar.h - Scalar Transformations -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file defines prototypes for accessor functions that expose passes
// in the Scalar transformations library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_H
#define LLVM_TRANSFORMS_SCALAR_H

#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Utils/SimplifyCFGOptions.h"
#include <functional>

namespace llvm {

class Function;
class FunctionPass;
class Pass;

//===----------------------------------------------------------------------===//
/// Create a pass that eliminates dead code via a worklist-driven walk.
///
/// More powerful than DeadInstElimination because it can revisit instructions
/// when their operands become dead, eliminating chains of dead computations.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createDeadCodeEliminationPass();

//===----------------------------------------------------------------------===//
/// Create a pass that deletes stores post-dominated by must-aliased stores.
///
/// Removes stores that are not loaded between themselves and a later
/// must-aliased store.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createDeadStoreEliminationPass();

//===----------------------------------------------------------------------===//
/// Create a pass that replaces aggregates with scalar SSA values (SROA).
///
/// \param PreserveCFG If true, the pass must not modify the CFG.
/// \param AggregateToVector If true, try converting homogeneous struct allocas
///        into vector allocas.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createSROAPass(bool PreserveCFG = true,
                                      bool AggregateToVector = false);

//===----------------------------------------------------------------------===//
/// Create a loop-invariant code motion and memory promotion pass.
/// \return The newly created Pass.
LLVM_ABI Pass *createLICMPass();

//===----------------------------------------------------------------------===//
/// Create a pass that strength-reduces GEPs using a loop induction variable.
///
/// Strength-reduces GEP instructions that use a loop's canonical induction
/// variable as one of their indices.
/// \return The newly created Pass.
LLVM_ABI Pass *createLoopStrengthReducePass();

//===----------------------------------------------------------------------===//
/// Create a pass that folds the last IV use in a loop terminator.
///
/// Attempts to eliminate the last use of an IV in a loop terminator by
/// rewriting it in terms of another IV. Expected to run immediately after LSR.
/// \return The newly created Pass.
LLVM_ABI Pass *createLoopTermFoldPass();

//===----------------------------------------------------------------------===//
/// Create a simple loop unrolling pass.
///
/// \param OptLevel Optimization level used to tune unrolling aggressiveness.
/// \param OnlyWhenForced If true, only unroll loops that request it via
///        metadata; otherwise use a cost model.
/// \param ForgetAllSCEV If true, forget all loops when unrolling; otherwise
///        forget only the top-most processed loop.
/// \param Threshold Cost threshold for unrolling, or -1 to use the default.
/// \param AllowPartial Whether to allow partial unrolling, or -1 for default.
/// \param Runtime Whether to allow runtime-trip-count unrolling, or -1 for
///        default.
/// \param UpperBound Whether to use trip-count upper bounds, or -1 for default.
/// \param AllowPeeling Whether to allow loop peeling, or -1 for default.
/// \return The newly created Pass.
LLVM_ABI Pass *createLoopUnrollPass(int OptLevel = 2,
                                    bool OnlyWhenForced = false,
                                    bool ForgetAllSCEV = false,
                                    int Threshold = -1, int AllowPartial = -1,
                                    int Runtime = -1, int UpperBound = -1,
                                    int AllowPeeling = -1);

//===----------------------------------------------------------------------===//
/// Create a pass that reassociates commutative expressions.
///
/// Reassociates expressions in an order designed to promote better constant
/// propagation, GCSE, LICM, PRE, and similar transforms.
///
/// For example:  4 + (x + 5)  ->  x + (4 + 5)
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createReassociatePass();

//===----------------------------------------------------------------------===//
/// Create a pass that simplifies the CFG of a function.
///
/// Merges basic blocks, eliminates unreachable blocks, simplifies terminators,
/// converts switches to lookup tables, and related cleanups.
///
/// \param Options Configuration for which CFG simplifications to apply.
/// \param Ftor Optional predicate; when set, only functions for which it
///        returns true are simplified.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createCFGSimplificationPass(
    SimplifyCFGOptions Options = SimplifyCFGOptions(),
    std::function<bool(const Function &)> Ftor = nullptr);

//===----------------------------------------------------------------------===//
/// Create a pass that flattens the CFG using parallel-and/or idioms.
///
/// Reduces the number of conditional branches by combining conditions with
/// parallel-and and parallel-or forms.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createFlattenCFGPass();

//===----------------------------------------------------------------------===//
/// Create a pass that removes irreducible control flow from the CFG.
///
/// \param SkipUniformRegions If true, do not structurize regions that only
///        contain uniform branches.
/// \return The newly created Pass.
LLVM_ABI Pass *createStructurizeCFGPass(bool SkipUniformRegions = false);

//===----------------------------------------------------------------------===//
/// Create a pass that eliminates recursive tail calls before returns.
///
/// Removes call instructions to the current function that occur immediately
/// before return instructions.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createTailCallEliminationPass();

//===----------------------------------------------------------------------===//
/// Create a fast common-subexpression elimination pass over the dominator tree.
///
/// \param UseMemorySSA If true, use MemorySSA to catch more load/store CSEs.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createEarlyCSEPass(bool UseMemorySSA = false);

//===----------------------------------------------------------------------===//
/// Create a pass that prepares a function for expensive constants.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createConstantHoistingPass();

//===----------------------------------------------------------------------===//
/// Create a pass that sinks instructions to reduce register pressure.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createSinkingPass();

//===----------------------------------------------------------------------===//
/// Create a pass that lowers atomic intrinsics to non-atomic form.
/// \return The newly created Pass.
LLVM_ABI Pass *createLowerAtomicPass();

//===----------------------------------------------------------------------===//
/// Create a pass that infers better address spaces for pointer users.
///
/// Modifies users of addrspacecast instructions to use the source address space
/// when the destination address space is slower on the target.
///
/// \param AddressSpace Flat address space to use, or ~0u to obtain it from
///        TargetTransformInfo.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *
createInferAddressSpacesPass(unsigned AddressSpace = ~0u);

/// Pass ID for the InferAddressSpaces pass.
LLVM_ABI extern char &InferAddressSpacesID;

//===----------------------------------------------------------------------===//
/// Create a pass that partially inlines fast paths of library calls.
///
/// Tries to inline the fast path of library calls such as sqrt.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createPartiallyInlineLibCallsPass();

//===----------------------------------------------------------------------===//
/// Create a pass that splits GEPs into a base and constant offset.
///
/// Improves CSE by separating constant offsets from GEP address calculations.
///
/// \param LowerGEP If true, also lower multi-index GEPs to single-index GEPs
///        and extract struct-field offsets.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *
createSeparateConstOffsetFromGEPPass(bool LowerGEP = false);

//===----------------------------------------------------------------------===//
/// Create a pass that hoists instructions for speculative execution.
///
/// Aggressively hoists instructions to enable speculative execution on targets
/// where branches are expensive.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createSpeculativeExecutionPass();

/// Create a speculative-execution pass that runs only with branch divergence.
///
/// Same as createSpeculativeExecutionPass, but does nothing unless
/// TargetTransformInfo::hasBranchDivergence() is true.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createSpeculativeExecutionIfHasBranchDivergencePass();

//===----------------------------------------------------------------------===//
/// Create a pass that strength-reduces patterns in straight-line code.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createStraightLineStrengthReducePass();

//===----------------------------------------------------------------------===//
/// Create a pass that simplifies n-ary operations by reassociation.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createNaryReassociatePass();

//===----------------------------------------------------------------------===//
/// Create a pass that inserts data prefetching in loops.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createLoopDataPrefetchPass();

//===----------------------------------------------------------------------===//
/// Create a legacy pass that simplifies each instruction in a function.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createInstSimplifyLegacyPass();

//===----------------------------------------------------------------------===//
/// Create a pass that scalarizes unsupported masked memory intrinsics.
///
/// Replaces masked load, store, gather, and scatter intrinsics with scalar
/// code when the target does not support them.
/// \return The newly created FunctionPass.
LLVM_ABI FunctionPass *createScalarizeMaskedMemIntrinLegacyPass();
} // End llvm namespace

#endif
