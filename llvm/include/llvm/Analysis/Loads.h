//===- Loads.h - Local load analysis --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares simple local analyses for load instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOADS_H
#define LLVM_ANALYSIS_LOADS_H

#include "llvm/ADT/APInt.h"
#include "llvm/Analysis/SimplifyQuery.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/GEPNoWrapFlags.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class BatchAAResults;
class AssumptionCache;
class DataLayout;
class DominatorTree;
class Instruction;
class LoadInst;
class Loop;
class MemoryLocation;
class SCEV;
class ScalarEvolution;
class SCEVPredicate;
template <typename T> class SmallVectorImpl;
class TargetLibraryInfo;

/// Returns true if V is always a dereferenceable pointer with sufficient
/// alignment.
///
/// The alignment must be greater or equal than requested. If the context
/// instruction is specified performs context-sensitive analysis and returns
/// true if the pointer is dereferenceable at the specified instruction.
/// If \p IgnoreFree is set, ignore potential frees of the object.
/// @param V Pointer value to check.
/// @param Ty Type used to determine the access size.
/// @param Alignment Minimum required alignment.
/// @param Q Query providing data layout and optional context instruction.
/// @param IgnoreFree When true, ignore potential frees of the object.
/// @return True if \p V is always a dereferenceable pointer with sufficient
/// alignment.
LLVM_ABI bool isDereferenceableAndAlignedPointer(const Value *V, Type *Ty,
                                                 Align Alignment,
                                                 const SimplifyQuery &Q,
                                                 bool IgnoreFree = false);

/// Returns true if V is always dereferenceable for Size bytes with sufficient
/// alignment.
///
/// The alignment must be greater or equal than requested. If the context
/// instruction is specified performs context-sensitive analysis and returns
/// true if the pointer is dereferenceable at the specified instruction.
/// If \p IgnoreFree is set, ignore potential frees of the object.
/// @param V Pointer value to check.
/// @param Alignment Minimum required alignment.
/// @param Size Number of bytes that must be dereferenceable.
/// @param Q Query providing data layout and optional context instruction.
/// @param IgnoreFree When true, ignore potential frees of the object.
/// @return True if \p V is always dereferenceable for \p Size bytes with
/// sufficient alignment.
LLVM_ABI bool isDereferenceableAndAlignedPointer(const Value *V,
                                                 Align Alignment,
                                                 const APInt &Size,
                                                 const SimplifyQuery &Q,
                                                 bool IgnoreFree = false);

/// Equivalent to isDereferenceableAndAlignedPointer with an alignment of 1.
/// @param V Pointer value to check.
/// @param Ty Type used to determine the access size.
/// @param Q Query providing data layout and optional context instruction.
/// @param IgnoreFree When true, ignore potential frees of the object.
/// @return True if \p V is always a dereferenceable pointer.
LLVM_ABI bool isDereferenceablePointer(const Value *V, Type *Ty,
                                       const SimplifyQuery &Q,
                                       bool IgnoreFree = false);

/// Equivalent to isDereferenceableAndAlignedPointer with an alignment of 1.
/// @param V Pointer value to check.
/// @param Size Number of bytes that must be dereferenceable.
/// @param Q Query providing data layout and optional context instruction.
/// @param IgnoreFree When true, ignore potential frees of the object.
/// @return True if \p V is always a dereferenceable pointer for \p Size bytes.
LLVM_ABI bool isDereferenceablePointer(const Value *V, const APInt &Size,
                                       const SimplifyQuery &Q,
                                       bool IgnoreFree = false);

/// Return true if we know that executing a load from this value cannot trap.
///
/// If SQ.CxtI is specified this method performs context-sensitive analysis
/// and returns true if it is safe to load immediately before SQ.CxtI.
///
/// If it is not obviously safe to load from the specified pointer, we do a
/// quick local scan of the basic block containing SQ.CxtI, to determine if
/// the address is already accessed.
/// @param V Pointer value to load from.
/// @param Alignment Alignment of the prospective load.
/// @param Size Size in bytes of the prospective load.
/// @param SQ Query providing data layout and optional context instruction.
/// @return True if executing a load from this value cannot trap.
LLVM_ABI bool isSafeToLoadUnconditionally(Value *V, Align Alignment,
                                          const APInt &Size,
                                          const SimplifyQuery &SQ);

/// Return true if a loop load is always dereferenceable and aligned.
///
/// The given load (which is assumed to be within the specified loop) would
/// access only dereferenceable memory, and be properly aligned on every
/// iteration of the specified loop regardless of its placement within the
/// loop. (i.e. does not require predication beyond that required by the header
/// itself and could be hoisted into the header if desired.) This is more
/// powerful than the variants above when the address loaded from is
/// analyzeable by SCEV.
/// @param LI Load instruction assumed to be within \p L.
/// @param L Loop in which the load executes.
/// @param SE Scalar evolution analysis used to analyze the address.
/// @param DT Dominator tree used for context-sensitive reasoning.
/// @param AC Optional assumption cache to strengthen proofs.
/// @param Predicates Optional vector that collects required SCEV predicates.
/// @return True if the loop load is always dereferenceable and aligned.
LLVM_ABI bool isDereferenceableAndAlignedInLoop(
    LoadInst *LI, Loop *L, ScalarEvolution &SE, DominatorTree &DT,
    AssumptionCache *AC = nullptr,
    SmallVectorImpl<const SCEVPredicate *> *Predicates = nullptr);

/// Overload for isDereferenceableAndAlignedInLoop taking the pointer and access
/// size directly as SCEVs.
/// @param PtrSCEV SCEV for the pointer being accessed.
/// @param Alignment Minimum required alignment.
/// @param EltSizeSCEV SCEV for the access size in bytes.
/// @param L Loop in which the access executes.
/// @param SE Scalar evolution analysis used to analyze the address.
/// @param DT Dominator tree used for context-sensitive reasoning.
/// @param AC Optional assumption cache to strengthen proofs.
/// @param Predicates Optional vector that collects required SCEV predicates.
/// @return True if the access is always dereferenceable and aligned in \p L.
LLVM_ABI bool isDereferenceableAndAlignedInLoop(
    const SCEV *PtrSCEV, Align Alignment, const SCEV *EltSizeSCEV, Loop *L,
    ScalarEvolution &SE, DominatorTree &DT, AssumptionCache *AC = nullptr,
    SmallVectorImpl<const SCEVPredicate *> *Predicates = nullptr);

/// Returns true if the loop contains read-only memory accesses and doesn't
/// throw. Puts loads that may fault into \p NonDereferenceableAndAlignedLoads.
/// @param L Loop to analyze.
/// @param SE Scalar evolution analysis used for dereferenceability proofs.
/// @param DT Dominator tree used for context-sensitive reasoning.
/// @param AC Assumption cache used to strengthen proofs.
/// @param NonDereferenceableAndAlignedLoads Loads that may fault are appended
/// here.
/// @param Predicates Optional vector that collects required SCEV predicates.
/// @return True if the loop is read-only and does not throw.
LLVM_ABI bool
isReadOnlyLoop(Loop *L, ScalarEvolution *SE, DominatorTree *DT,
               AssumptionCache *AC,
               SmallVectorImpl<LoadInst *> &NonDereferenceableAndAlignedLoads,
               SmallVectorImpl<const SCEVPredicate *> *Predicates = nullptr);

/// Return true if we know that executing a load from this value cannot trap.
///
/// If SQ.CxtI is specified this method performs context-sensitive analysis
/// and returns true if it is safe to load immediately before SQ.CxtI.
///
/// If it is not obviously safe to load from the specified pointer, we do a
/// quick local scan of the basic block containing SQ.CxtI, to determine if
/// the address is already accessed.
/// @param V Pointer value to load from.
/// @param Ty Type of the prospective load, used to determine size.
/// @param Alignment Alignment of the prospective load.
/// @param SQ Query providing data layout and optional context instruction.
/// @return True if executing a load from this value cannot trap.
LLVM_ABI bool isSafeToLoadUnconditionally(Value *V, Type *Ty, Align Alignment,
                                          const SimplifyQuery &SQ);

/// Return true if load speculation must be suppressed for sanitizers.
///
/// Speculation of the given load must be suppressed to avoid ordering or
/// interfering with an active sanitizer. If not suppressed,
/// dereferenceability and alignment must be proven separately. Note: This is
/// only needed for raw reasoning; if you use the interface below
/// (isSafeToSpeculativelyExecute), this is handled internally.
/// @param LI Load whose speculation is being considered.
/// @return True if load speculation must be suppressed for sanitizers.
LLVM_ABI bool mustSuppressSpeculation(const LoadInst &LI);

/// The default number of maximum instructions to scan in the block, used by
/// FindAvailableLoadedValue().
LLVM_ABI extern cl::opt<unsigned> DefMaxInstsToScan;

/// Scan backwards to see if we have the value of the given load available
/// locally within a small number of instructions.
///
/// You can use this function to scan across multiple blocks: after you call
/// this function, if ScanFrom points at the beginning of the block, it's safe
/// to continue scanning the predecessors.
///
/// Note that performing load CSE requires special care to make sure the
/// metadata is set appropriately.  In particular, aliasing metadata needs
/// to be merged.  (This doesn't matter for store-to-load forwarding because
/// the only relevant load gets deleted.)
///
/// \param Load The load we want to replace.
/// \param ScanBB The basic block to scan.
/// \param [in,out] ScanFrom The location to start scanning from. When this
/// function returns, it points at the last instruction scanned.
/// \param MaxInstsToScan The maximum number of instructions to scan. If this
/// is zero, the whole block will be scanned.
/// \param AA Optional pointer to alias analysis, to make the scan more
/// precise.
/// \param [out] IsLoadCSE Whether the returned value is a load from the same
/// location in memory, as opposed to the value operand of a store.
/// \param [out] NumScanedInst Optional count of instructions examined.
///
/// \returns The found value, or nullptr if no value is found.
LLVM_ABI Value *FindAvailableLoadedValue(
    LoadInst *Load, BasicBlock *ScanBB, BasicBlock::iterator &ScanFrom,
    unsigned MaxInstsToScan = DefMaxInstsToScan, BatchAAResults *AA = nullptr,
    bool *IsLoadCSE = nullptr, unsigned *NumScanedInst = nullptr);

/// Find an available loaded value without reporting the closest clobber.
///
/// This overload provides a more efficient implementation of
/// FindAvailableLoadedValue() for the case where we are not interested in
/// finding the closest clobbering instruction if no available load is found.
/// This overload cannot be used to scan across multiple blocks.
/// @param Load The load we want to replace.
/// @param AA Alias analysis used to make the scan more precise.
/// @param [out] IsLoadCSE Whether the returned value is a load from the same
/// location in memory, as opposed to the value operand of a store.
/// @param MaxInstsToScan The maximum number of instructions to scan. If this
/// is zero, the whole block will be scanned.
/// @return The found value, or nullptr if no value is found.
LLVM_ABI Value *
FindAvailableLoadedValue(LoadInst *Load, BatchAAResults &AA, bool *IsLoadCSE,
                         unsigned MaxInstsToScan = DefMaxInstsToScan);

/// Scan backwards to see if we have the value of the given pointer available
/// locally within a small number of instructions.
///
/// You can use this function to scan across multiple blocks: after you call
/// this function, if ScanFrom points at the beginning of the block, it's safe
/// to continue scanning the predecessors.
///
/// \param Loc The location we want the load and store to originate from.
/// \param AccessTy The access type of the pointer.
/// \param AtLeastAtomic Are we looking for at-least an atomic load/store ? In
/// case it is false, we can return an atomic or non-atomic load or store. In
/// case it is true, we need to return an atomic load or store.
/// \param ScanBB The basic block to scan.
/// \param [in,out] ScanFrom The location to start scanning from. When this
/// function returns, it points at the last instruction scanned.
/// \param MaxInstsToScan The maximum number of instructions to scan. If this
/// is zero, the whole block will be scanned.
/// \param AA Optional pointer to alias analysis, to make the scan more
/// precise.
/// \param [out] IsLoadCSE Whether the returned value is a load from the same
/// location in memory, as opposed to the value operand of a store.
/// \param [out] NumScanedInst Optional count of instructions examined.
///
/// \returns The found value, or nullptr if no value is found.
LLVM_ABI Value *findAvailablePtrLoadStore(
    const MemoryLocation &Loc, Type *AccessTy, bool AtLeastAtomic,
    BasicBlock *ScanBB, BasicBlock::iterator &ScanFrom, unsigned MaxInstsToScan,
    BatchAAResults *AA, bool *IsLoadCSE, unsigned *NumScanedInst);

/// Returns true if pointer \p From can be replaced with equal pointer \p To.
///
/// The pointers are deemed equal through some means (e.g. information from
/// conditions). NOTE: The current implementation allows replacement in Icmp and
/// PtrToInt instructions, as well as when we are replacing with a null pointer.
/// Additionally it also allows replacement of pointers when both pointers have
/// the same underlying object.
/// @param From Pointer value that would be replaced.
/// @param To Pointer value that would replace \p From.
/// @param DL Data layout used for pointer-size reasoning.
/// @return True if pointer \p From can be replaced with equal pointer \p To.
LLVM_ABI bool canReplacePointersIfEqual(const Value *From, const Value *To,
                                        const DataLayout &DL);
/// Returns true if use \p U of a pointer can be rewritten to use \p To.
/// @param U Use of a pointer being considered for replacement.
/// @param To Pointer value that would replace the used value.
/// @param DL Data layout used for pointer-size reasoning.
/// @return True if use \p U can be rewritten to use \p To.
LLVM_ABI bool canReplacePointersInUseIfEqual(const Use &U, const Value *To,
                                             const DataLayout &DL);

/// Linear expression of the form BasePtr + Index * Scale + Offset.
///
/// Index, Scale and Offset all have the same bit width, which matches the
/// pointer index size of BasePtr. Index may be nullptr if Scale is 0.
struct LinearExpression {
  /// Base pointer of the linear expression.
  Value *BasePtr;
  /// Optional index multiplied by Scale; nullptr when Scale is 0.
  Value *Index = nullptr;
  /// Scale applied to Index.
  APInt Scale;
  /// Constant offset added to the scaled index.
  APInt Offset;
  /// No-wrap flags that apply to the linear addressing expression.
  GEPNoWrapFlags Flags = GEPNoWrapFlags::all();

  /// Construct a zero-scale, zero-offset expression over \p BasePtr.
  /// @param BasePtr Base pointer of the expression.
  /// @param BitWidth Bit width for Scale and Offset (pointer index size).
  LinearExpression(Value *BasePtr, unsigned BitWidth)
      : BasePtr(BasePtr), Scale(BitWidth, 0), Offset(BitWidth, 0) {}
};

/// Decompose a pointer into a linear expression. This may look through
/// multiple GEPs.
/// @param DL Data layout used for pointer index size and GEP analysis.
/// @param Ptr Pointer value to decompose.
/// @return The decomposed linear expression for \p Ptr.
LLVM_ABI LinearExpression decomposeLinearExpression(const DataLayout &DL,
                                                    Value *Ptr);
}

#endif
