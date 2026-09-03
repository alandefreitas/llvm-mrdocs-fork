//===- Attributor.h --- Module-wide attribute deduction ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Attributor: An inter procedural (abstract) "attribute" deduction framework.
//
// The Attributor framework is an inter procedural abstract analysis (fixpoint
// iteration analysis). The goal is to allow easy deduction of new attributes as
// well as information exchange between abstract attributes in-flight.
//
// The Attributor class is the driver and the link between the various abstract
// attributes. The Attributor will iterate until a fixpoint state is reached by
// all abstract attributes in-flight, or until it will enforce a pessimistic fix
// point because an iteration limit is reached.
//
// Abstract attributes, derived from the AbstractAttribute class, actually
// describe properties of the code. They can correspond to actual LLVM-IR
// attributes, or they can be more general, ultimately unrelated to LLVM-IR
// attributes. The latter is useful when an abstract attributes provides
// information to other abstract attributes in-flight but we might not want to
// manifest the information. The Attributor allows to query in-flight abstract
// attributes through the `Attributor::getAAFor` method (see the method
// description for an example). If the method is used by an abstract attribute
// P, and it results in an abstract attribute Q, the Attributor will
// automatically capture a potential dependence from Q to P. This dependence
// will cause P to be reevaluated whenever Q changes in the future.
//
// The Attributor will only reevaluate abstract attributes that might have
// changed since the last iteration. That means that the Attribute will not
// revisit all instructions/blocks/functions in the module but only query
// an update from a subset of the abstract attributes.
//
// The update method `AbstractAttribute::updateImpl` is implemented by the
// specific "abstract attribute" subclasses. The method is invoked whenever the
// currently assumed state (see the AbstractState class) might not be valid
// anymore. This can, for example, happen if the state was dependent on another
// abstract attribute that changed. In every invocation, the update method has
// to adjust the internal state of an abstract attribute to a point that is
// justifiable by the underlying IR and the current state of abstract attributes
// in-flight. Since the IR is given and assumed to be valid, the information
// derived from it can be assumed to hold. However, information derived from
// other abstract attributes is conditional on various things. If the justifying
// state changed, the `updateImpl` has to revisit the situation and potentially
// find another justification or limit the optimistic assumes made.
//
// Change is the key in this framework. Until a state of no-change, thus a
// fixpoint, is reached, the Attributor will query the abstract attributes
// in-flight to re-evaluate their state. If the (current) state is too
// optimistic, hence it cannot be justified anymore through other abstract
// attributes or the state of the IR, the state of the abstract attribute will
// have to change. Generally, we assume abstract attribute state to be a finite
// height lattice and the update function to be monotone. However, these
// conditions are not enforced because the iteration limit will guarantee
// termination. If an optimistic fixpoint is reached, or a pessimistic fix
// point is enforced after a timeout, the abstract attributes are tasked to
// manifest their result in the IR for passes to come.
//
// Attribute manifestation is not mandatory. If desired, there is support to
// generate a single or multiple LLVM-IR attributes already in the helper struct
// IRAttribute. In the simplest case, a subclass inherits from IRAttribute with
// a proper Attribute::AttrKind as template parameter. The Attributor
// manifestation framework will then create and place a new attribute if it is
// allowed to do so (based on the abstract state). Other use cases can be
// achieved by overloading AbstractAttribute or IRAttribute methods.
//
//
// The "mechanics" of adding a new "abstract attribute":
// - Define a class (transitively) inheriting from AbstractAttribute and one
//   (which could be the same) that (transitively) inherits from AbstractState.
//   For the latter, consider the already available BooleanState and
//   {Inc,Dec,Bit}IntegerState if they fit your needs, e.g., you require only a
//   number tracking or bit-encoding.
// - Implement all pure methods. Also use overloading if the attribute is not
//   conforming with the "default" behavior: A (set of) LLVM-IR attribute(s) for
//   an argument, call site argument, function return value, or function. See
//   the class and method descriptions for more information on the two
//   "Abstract" classes and their respective methods.
// - Register opportunities for the new abstract attribute in the
//   `Attributor::identifyDefaultAbstractAttributes` method if it should be
//   counted as a 'default' attribute.
// - Add sufficient tests.
// - Add a Statistics object for bookkeeping. If it is a simple (set of)
//   attribute(s) manifested through the Attributor manifestation framework, see
//   the bookkeeping function in Attributor.cpp.
// - If instructions with a certain opcode are interesting to the attribute, add
//   that opcode to the switch in `Attributor::identifyAbstractAttributes`. This
//   will make it possible to query all those instructions through the
//   `InformationCache::getOpcodeInstMapForFunction` interface and eliminate the
//   need to traverse the IR repeatedly.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_ATTRIBUTOR_H
#define LLVM_TRANSFORMS_IPO_ATTRIBUTOR_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Analysis/AssumeBundleQueries.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/MustExecute.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/AbstractCallSite.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ModRef.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/TypeSize.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/CallGraphUpdater.h"

#include <limits>
#include <map>
#include <optional>

namespace llvm {

class DataLayout;
class LLVMContext;
class Pass;
template <typename Fn> class function_ref;
/// Node in the Attributor dependence graph.
struct AADepGraphNode;
struct AADepGraph;
struct Attributor;
struct AbstractAttribute;
/// Cache of IR information used by the Attributor.
struct InformationCache;
/// Abstract attribute determining if IR is dead.
struct AAIsDead;
/// Call graph built from Attributor call-edge information.
struct AttributorCallGraph;
struct IRPosition;

class Function;

/// Abstract Attribute helper functions.
namespace AA {
/// Set of instructions excluded from reachability queries.
using InstExclusionSetTy = SmallPtrSet<Instruction *, 4>;

/// Return true iff \p M target a GPU (and we can use GPU AS reasoning).
///
/// \return True iff \p M target a GPU (and we can use GPU AS reasoning).
///
/// \param M The module of interest.
LLVM_ABI bool isGPU(const Module &M);

/// Check if the given address space \p AS corresponds to a GPU generic
/// address space for the target triple in module \p M.
///
/// \return True if the given address space \p AS corresponds to a GPU generic address space for the target triple in module \p M.
///
/// \param M The module of interest.
/// \param AS The address space to classify.
LLVM_ABI bool isGPUGenericAddressSpace(const Module &M, unsigned AS);

/// Check if the given address space \p AS corresponds to a GPU global
/// address space for the target triple in module \p M.
///
/// \return True if the given address space \p AS corresponds to a GPU global address space for the target triple in module \p M.
///
/// \param M The module of interest.
/// \param AS The address space to classify.
LLVM_ABI bool isGPUGlobalAddressSpace(const Module &M, unsigned AS);

/// Check if the given address space \p AS corresponds to a GPU shared
/// address space for the target triple in module \p M.
///
/// \return True if the given address space \p AS corresponds to a GPU shared address space for the target triple in module \p M.
///
/// \param M The module of interest.
/// \param AS The address space to classify.
LLVM_ABI bool isGPUSharedAddressSpace(const Module &M, unsigned AS);

/// Check if the given address space \p AS corresponds to a GPU constant
/// address space for the target triple in module \p M.
///
/// \return True if the given address space \p AS corresponds to a GPU constant address space for the target triple in module \p M.
///
/// \param M The module of interest.
/// \param AS The address space to classify.
LLVM_ABI bool isGPUConstantAddressSpace(const Module &M, unsigned AS);

/// Check if the given address space \p AS corresponds to a GPU local/private
/// address space for the target triple in module \p M.
///
/// \return True if the given address space \p AS corresponds to a GPU local/private address space for the target triple in module \p M.
///
/// \param M The module of interest.
/// \param AS The address space to classify.
LLVM_ABI bool isGPULocalAddressSpace(const Module &M, unsigned AS);

/// Scope flags for intra- versus inter-procedural value queries.
///
/// Note that information can be valid for both and therefore both bits might
/// be set.
enum ValueScope : uint8_t {
  Intraprocedural = 1,  ///< Query is restricted to the current function.
  Interprocedural = 2,  ///< Query may span across functions.
  AnyScope = Intraprocedural | Interprocedural,  ///< Query is valid in either scope.
};

/// A value paired with an optional context instruction.
struct ValueAndContext : public std::pair<Value *, const Instruction *> {
  /// Underlying pair type holding the value and context.
  using Base = std::pair<Value *, const Instruction *>;
  /// Construct from an existing value/context pair \p B.
  ///
  /// \param B The underlying value/context pair.
  ValueAndContext(const Base &B) : Base(B) {}
  /// Construct from value \p V and optional context \p CtxI.
  ///
  /// \param V The value of interest.
  /// \param CtxI Optional context instruction for the query.
  ValueAndContext(Value &V, const Instruction *CtxI) : Base(&V, CtxI) {}
  /// Construct from value \p V and context instruction \p CtxI.
  ///
  /// \param V The value of interest.
  /// \param CtxI The context instruction.
  ValueAndContext(Value &V, const Instruction &CtxI) : Base(&V, &CtxI) {}

  /// Assign from another value/context pair.
  ///
  /// \return A reference to this object.
  ///
  /// \param Other The value/context pair to assign from.
  ValueAndContext &operator=(const ValueAndContext &Other) = default;
  /// Move-assign from another value/context pair.
  ///
  /// \return A reference to this object.
  ///
  /// \param Other The value/context pair to move from.
  ValueAndContext &operator=(ValueAndContext &&Other) = default;

  /// Swap this value/context pair with \p Other.
  ///
  /// \param Other The other value/context pair.
  void swap(ValueAndContext &Other) { Base::swap(Other); }

  /// Return the value.
  ///
  /// \return The value.
  Value *getValue() const { return this->first; }
  /// Return the context instruction, or null.
  ///
  /// \return The context instruction, or null.
  const Instruction *getCtxI() const { return this->second; }
};

/// Return true if \p I is a `nosync` instruction. Use generic reasoning and
/// potentially the corresponding AANoSync.
///
/// \return True if \p I is a `nosync` instruction. Use generic reasoning and potentially the corresponding AANoSync.
///
/// \param A The Attributor managing the analysis.
/// \param I The instruction of interest.
/// \param QueryingAA The abstract attribute issuing the query.
LLVM_ABI bool isNoSyncInst(Attributor &A, const Instruction &I,
                           const AbstractAttribute &QueryingAA);

/// Return true if \p V is dynamically unique at runtime.
///
/// That is, there are no two "instances" of \p V at runtime with different
/// values. Note: If \p ForAnalysisOnly is set we only check that the Attributor
/// will never use \p V to represent two "instances" not that \p V could not
/// technically represent them.
///
/// \return True if \p V is dynamically unique at runtime.
///
/// \param A The Attributor managing the analysis.
/// \param QueryingAA The abstract attribute issuing the query.
/// \param V The value of interest.
/// \param ForAnalysisOnly If true, only require uniqueness for analysis.
LLVM_ABI bool isDynamicallyUnique(Attributor &A,
                                  const AbstractAttribute &QueryingAA,
                                  const Value &V, bool ForAnalysisOnly = true);

/// Return true if \p V is a valid value in \p Scope, that is a constant or an
/// instruction/argument of \p Scope.
///
/// \return True if \p V is a valid value in \p Scope, that is a constant or an instruction/argument of \p Scope.
///
/// \param V The value of interest.
/// \param Scope The value scope for the query.
LLVM_ABI bool isValidInScope(const Value &V, const Function *Scope);

/// Return true if the value of \p VAC is valid at its context position.
///
/// That is, the value is a constant, an argument of the same function, or an
/// instruction in that function that dominates the position.
///
/// \return True if the value of \p VAC is valid at its context position.
///
/// \param VAC The value-and-context pair to validate.
/// \param InfoCache The information cache used for validation.
LLVM_ABI bool isValidAtPosition(const ValueAndContext &VAC,
                                InformationCache &InfoCache);

/// Try to convert \p V to type \p Ty without introducing new instructions.
///
/// If this is not possible return nullptr. Note: this function basically knows
/// how to cast various constants.
///
/// \return The value converted to \p Ty, or null if conversion is not possible.
///
/// \param V The value of interest.
/// \param Ty The destination type.
LLVM_ABI Value *getWithType(Value &V, Type &Ty);

/// Return the combination of \p A and \p B such that the result is a possible
/// value of both. \p B is potentially casted to match the type \p Ty or the
/// type of \p A if \p Ty is null.
///
/// \return The combination of \p A and \p B such that the result is a possible value of both.
///
/// \param A The first optional value.
/// \param B The second optional value.
/// \param Ty The type used when combining values.
LLVM_ABI std::optional<Value *>
combineOptionalValuesInAAValueLatice(const std::optional<Value *> &A,
                                     const std::optional<Value *> &B, Type *Ty);

/// Helper to represent an access offset and size, with logic to deal with
/// uncertainty and check for overlapping accesses.
struct RangeTy {
  /// Byte offset from the base pointer.
  int64_t Offset = Unassigned;
  /// Access size in bytes.
  int64_t Size = Unassigned;

  /// Construct a range with \p Offset and \p Size.
  ///
  /// \param Offset The byte offset.
  /// \param Size The byte size.
  RangeTy(int64_t Offset, int64_t Size) : Offset(Offset), Size(Size) {}
  /// Construct an unassigned range.
  RangeTy() = default;
  /// Return a fully unknown range.
  ///
  /// \return A fully unknown range.
  static RangeTy getUnknown() { return RangeTy{Unknown, Unknown}; }

  /// Return true if offset or size are unknown.
  ///
  /// \return True if offset or size are unknown.
  bool offsetOrSizeAreUnknown() const {
    return Offset == RangeTy::Unknown || Size == RangeTy::Unknown;
  }

  /// Return true if offset and size are unknown, thus this is the default
  /// unknown object.
  ///
  /// \return True if offset and size are unknown, thus this is the default unknown object.
  bool offsetAndSizeAreUnknown() const {
    return Offset == RangeTy::Unknown && Size == RangeTy::Unknown;
  }

  /// Return true if the offset and size are unassigned.
  ///
  /// \return True if the offset and size are unassigned.
  bool isUnassigned() const {
    assert((Offset == RangeTy::Unassigned) == (Size == RangeTy::Unassigned) &&
           "Inconsistent state!");
    return Offset == RangeTy::Unassigned;
  }

  /// Return true if this offset and size pair might describe an address that
  /// overlaps with \p Range.
  ///
  /// \return True if this offset and size pair might describe an address that overlaps with \p Range.
  ///
  /// \param Range The access range of interest.
  bool mayOverlap(const RangeTy &Range) const {
    // Any unknown value and we are giving up -> overlap.
    if (offsetOrSizeAreUnknown() || Range.offsetOrSizeAreUnknown())
      return true;

    // Check if one offset point is in the other interval [offset,
    // offset+size].
    return Range.Offset + Range.Size > Offset && Range.Offset < Offset + Size;
  }

  /// Intersect this range with \p R in place.
  ///
  /// \return The computed result.
  ///
  /// \param R The other range.
  RangeTy &operator&=(const RangeTy &R) {
    if (R.isUnassigned())
      return *this;
    if (isUnassigned())
      return *this = R;
    if (Offset == Unknown || R.Offset == Unknown)
      Offset = Unknown;
    if (Size == Unknown || R.Size == Unknown)
      Size = Unknown;
    if (offsetAndSizeAreUnknown())
      return *this;
    if (Offset == Unknown) {
      Size = std::max(Size, R.Size);
    } else if (Size == Unknown) {
      Offset = std::min(Offset, R.Offset);
    } else {
      Offset = std::min(Offset, R.Offset);
      Size = std::max(Offset + Size, R.Offset + R.Size) - Offset;
    }
    return *this;
  }

  /// Comparison for sorting ranges.
  ///
  /// Returns true if the offset of \p L is less than that of \p R. If the two
  /// offsets are same, compare the sizes instead.
  ///
  /// \return True if the offset of \p L is less than that of \p R, or the size is smaller when offsets are equal.
  ///
  /// \param L The left-hand side.
  /// \param R The other operand or state.
  inline static bool LessThan(const RangeTy &L, const RangeTy &R) {
    if (L.Offset < R.Offset)
      return true;
    if (L.Offset == R.Offset)
      return L.Size < R.Size;
    return false;
  }

  /// Sentinel for an unassigned offset or size.
  ///
  /// Constants used to represent special offsets or sizes.
  /// - We cannot assume that Offsets and Size are non-negative.
  /// - The constants should not clash with DenseMapInfo, such as EmptyKey
  ///   (INT64_MAX).
  /// We use values "in the middle" of the 64 bit range to represent these
  /// special cases.
  ///
  /// \return The computed result.
  static constexpr int64_t Unassigned = std::numeric_limits<int32_t>::min();
  /// Sentinel for an unknown offset or size.
  ///
  /// \return The computed result.
  static constexpr int64_t Unknown = std::numeric_limits<int32_t>::max();
};

/// Print a range to an output stream.
///
/// \return The output stream \p OS.
///
/// \param OS The output stream.
/// \param R The range to print.
inline raw_ostream &operator<<(raw_ostream &OS, const RangeTy &R) {
  OS << "[" << R.Offset << ", " << R.Size << "]";
  return OS;
}

/// Return true if the two ranges are equal.
///
/// \return True if the two ranges are equal.
///
/// \param A The first range.
/// \param B The second range.
inline bool operator==(const RangeTy &A, const RangeTy &B) {
  return A.Offset == B.Offset && A.Size == B.Size;
}

/// Return true if the two ranges differ.
///
/// \return True if the two ranges differ.
///
/// \param A The first range.
/// \param B The second range.
inline bool operator!=(const RangeTy &A, const RangeTy &B) { return !(A == B); }

/// Return the initial value of \p Obj with type \p Ty if that is a constant.
///
/// \return The initial value of \p Obj with type \p Ty if that is a constant.
///
/// \param A The Attributor managing the analysis.
/// \param QueryingAA The abstract attribute issuing the query.
/// \param Obj The memory object of interest.
/// \param Ty The type of the initial value to materialize.
/// \param TLI Optional target library info.
/// \param DL The data layout.
/// \param RangePtr Optional access range within the object.
LLVM_ABI Constant *
getInitialValueForObj(Attributor &A, const AbstractAttribute &QueryingAA,
                      Value &Obj, Type &Ty, const TargetLibraryInfo *TLI,
                      const DataLayout &DL, RangeTy *RangePtr = nullptr);

/// Collect all potential values \p LI could read into \p PotentialValues.
///
/// The only values read by \p LI are assumed known and all are placed in
/// \p PotentialValues. \p PotentialValueOrigins receives corresponding origins.
///
/// \return True if all potential values were collected successfully.
///
/// \param A The Attributor managing the analysis.
/// \param LI The load instruction of interest.
/// \param PotentialValues Collection receiving potential values.
/// \param PotentialValueOrigins Collection receiving value origins.
/// \param QueryingAA The abstract attribute issuing the query.
/// \param UsedAssumedInformation Set if assumed information was used.
/// \param OnlyExact If true, require exact information.
LLVM_ABI bool getPotentiallyLoadedValues(
    Attributor &A, LoadInst &LI, SmallSetVector<Value *, 4> &PotentialValues,
    SmallSetVector<Instruction *, 4> &PotentialValueOrigins,
    const AbstractAttribute &QueryingAA, bool &UsedAssumedInformation,
    bool OnlyExact = false);

/// Collect all potential copies of the value stored by \p SI.
///
/// The only copies made via the store are assumed known and all are placed
/// in \p PotentialCopies.
///
/// \return True if all potential values were collected successfully.
///
/// \param A The Attributor managing the analysis.
/// \param SI The store instruction of interest.
/// \param PotentialCopies Collection receiving potential copies.
/// \param QueryingAA The abstract attribute issuing the query.
/// \param UsedAssumedInformation Set if assumed information was used.
/// \param OnlyExact If true, require exact information.
LLVM_ABI bool getPotentialCopiesOfStoredValue(
    Attributor &A, StoreInst &SI, SmallSetVector<Value *, 4> &PotentialCopies,
    const AbstractAttribute &QueryingAA, bool &UsedAssumedInformation,
    bool OnlyExact = false);

/// Return true if \p IRP is readonly. This will query respective AAs that
/// deduce the information and introduce dependences for \p QueryingAA.
///
/// \return True if \p IRP is readonly. This will query respective AAs that deduce the information and introduce dependences for \p QueryingAA.
///
/// \param A The Attributor managing the analysis.
/// \param IRP The IR position of interest.
/// \param QueryingAA The abstract attribute issuing the query.
/// \param IsKnown Set to true if the attribute is known, not only assumed.
LLVM_ABI bool isAssumedReadOnly(Attributor &A, const IRPosition &IRP,
                                const AbstractAttribute &QueryingAA,
                                bool &IsKnown);

/// Return true if \p IRP is readnone. This will query respective AAs that
/// deduce the information and introduce dependences for \p QueryingAA.
///
/// \return True if \p IRP is readnone. This will query respective AAs that deduce the information and introduce dependences for \p QueryingAA.
///
/// \param A The Attributor managing the analysis.
/// \param IRP The IR position of interest.
/// \param QueryingAA The abstract attribute issuing the query.
/// \param IsKnown Set to true if the attribute is known, not only assumed.
LLVM_ABI bool isAssumedReadNone(Attributor &A, const IRPosition &IRP,
                                const AbstractAttribute &QueryingAA,
                                bool &IsKnown);

/// Return true if \p ToI is potentially reachable from \p FromI.
///
/// Reachability must not run into any instruction in \p ExclusionSet. The two
/// instructions do not need to be in the same function. \p GoBackwardsCB can be
/// provided to convey domain knowledge about the "lifespan" the user is
/// interested in. By default, the callers of \p FromI are checked as well to
/// determine if \p ToI can be reached. If the query is not interested in
/// callers beyond a certain point, e.g., a GPU kernel entry or the function
/// containing an alloca, the \p GoBackwardsCB should return false.
///
/// \return True if \p ToI is potentially reachable from \p FromI.
///
/// \param A The Attributor managing the analysis.
/// \param FromI The source instruction.
/// \param ToI The destination instruction.
/// \param QueryingAA The abstract attribute issuing the query.
/// \param ExclusionSet Optional instructions that block reachability.
/// \param GoBackwardsCB Optional callback controlling inter-procedural search.
LLVM_ABI bool isPotentiallyReachable(
    Attributor &A, const Instruction &FromI, const Instruction &ToI,
    const AbstractAttribute &QueryingAA,
    const AA::InstExclusionSetTy *ExclusionSet = nullptr,
    std::function<bool(const Function &F)> GoBackwardsCB = nullptr);

/// Return true if any instruction in \p ToFn is potentially reachable from \p FromI.
///
/// \return True if any instruction in \p ToFn is potentially reachable from \p FromI.
///
/// \param A The Attributor managing the analysis.
/// \param FromI The source instruction.
/// \param ToFn The destination function.
/// \param QueryingAA The abstract attribute issuing the query.
/// \param ExclusionSet Optional instructions that block reachability.
/// \param GoBackwardsCB Optional callback controlling inter-procedural search.
LLVM_ABI bool isPotentiallyReachable(
    Attributor &A, const Instruction &FromI, const Function &ToFn,
    const AbstractAttribute &QueryingAA,
    const AA::InstExclusionSetTy *ExclusionSet = nullptr,
    std::function<bool(const Function &F)> GoBackwardsCB = nullptr);

/// Return true if \p Obj is assumed to be a thread local object.
///
/// \return True if \p Obj is assumed to be a thread local object.
///
/// \param A The Attributor managing the analysis.
/// \param Obj The memory object of interest.
/// \param QueryingAA The abstract attribute issuing the query.
LLVM_ABI bool isAssumedThreadLocalObject(Attributor &A, Value &Obj,
                                         const AbstractAttribute &QueryingAA);

/// Return true if \p I is potentially affected by a barrier.
///
/// \return True if \p I is potentially affected by a barrier.
///
/// \param A The Attributor managing the analysis.
/// \param I The instruction of interest.
/// \param QueryingAA The abstract attribute issuing the query.
LLVM_ABI bool
isPotentiallyAffectedByBarrier(Attributor &A, const Instruction &I,
                               const AbstractAttribute &QueryingAA);
/// Return true if any pointer in \p Ptrs is potentially affected by a barrier.
///
/// \return True if any pointer in \p Ptrs is potentially affected by a barrier.
///
/// \param A The Attributor managing the analysis.
/// \param Ptrs The pointers of interest.
/// \param QueryingAA The abstract attribute issuing the query.
/// \param CtxI Optional context instruction for the query.
LLVM_ABI bool
isPotentiallyAffectedByBarrier(Attributor &A, ArrayRef<const Value *> Ptrs,
                               const AbstractAttribute &QueryingAA,
                               const Instruction *CtxI);
} // namespace AA

/// DenseMapInfo specialization for AA::ValueAndContext keys.
template <>
struct DenseMapInfo<AA::ValueAndContext>
    : public DenseMapInfo<AA::ValueAndContext::Base> {
  /// Base type of this abstract attribute.
  using Base = DenseMapInfo<AA::ValueAndContext::Base>;
  /// Return the hash value for the given key.
  ///
  /// \return The hash value for the given key.
  ///
  /// \param VAC The value-and-context key to hash.
  static unsigned getHashValue(const AA::ValueAndContext &VAC) {
    return Base::getHashValue(VAC);
  }

  /// Return true if the two keys are equal.
  ///
  /// \return True if the two keys are equal.
  ///
  /// \param LHS The first value-and-context key.
  /// \param RHS The second value-and-context key.
  static bool isEqual(const AA::ValueAndContext &LHS,
                      const AA::ValueAndContext &RHS) {
    return Base::isEqual(LHS, RHS);
  }
};

/// DenseMapInfo specialization for AA::ValueScope keys.
template <>
struct DenseMapInfo<AA::ValueScope> : public DenseMapInfo<unsigned char> {
  /// Base type of this abstract attribute.
  using Base = DenseMapInfo<unsigned char>;
  /// Return the hash value for the given key.
  ///
  /// \return The hash value for the given key.
  ///
  /// \param S The value-scope key to hash.
  static unsigned getHashValue(const AA::ValueScope &S) {
    return Base::getHashValue(S);
  }

  /// Return true if the two keys are equal.
  ///
  /// \return True if the two keys are equal.
  ///
  /// \param LHS The first value-scope key.
  /// \param RHS The second value-scope key.
  static bool isEqual(const AA::ValueScope &LHS, const AA::ValueScope &RHS) {
    return Base::isEqual(LHS, RHS);
  }
};

/// DenseMapInfo specialization for instruction-exclusion set pointers.
template <>
struct DenseMapInfo<const AA::InstExclusionSetTy *>
    : public DenseMapInfo<void *> {
  /// Return the hash value for the given key.
  ///
  /// \return The hash value for the given key.
  ///
  /// \param BES The instruction-exclusion set to hash, or null.
  static unsigned getHashValue(const AA::InstExclusionSetTy *BES) {
    unsigned H = 0;
    if (BES)
      for (const auto *II : *BES)
        H += DenseMapInfo<const Instruction *>::getHashValue(II);
    return H;
  }
  /// Return true if the two keys are equal.
  ///
  /// \return True if the two keys are equal.
  ///
  /// \param LHS The first instruction-exclusion set, or null.
  /// \param RHS The second instruction-exclusion set, or null.
  static bool isEqual(const AA::InstExclusionSetTy *LHS,
                      const AA::InstExclusionSetTy *RHS) {
    if (LHS == RHS)
      return true;
    auto SizeLHS = LHS ? LHS->size() : 0;
    auto SizeRHS = RHS ? RHS->size() : 0;
    if (SizeLHS != SizeRHS)
      return false;
    if (SizeRHS == 0)
      return true;
    return llvm::set_is_subset(*LHS, *RHS);
  }
};

/// The value passed to the line option that defines the maximal initialization
/// chain length.
LLVM_ABI extern unsigned MaxInitializationChainLength;

/// Indicator whether an abstract state or the IR changed.
enum class ChangeStatus {
  CHANGED,  ///< The abstract state or IR changed.
  UNCHANGED,  ///< The abstract state or IR did not change.
};

/// Return CHANGED if either operand is CHANGED.
///
/// \return CHANGED if either operand is CHANGED.
///
/// \param l The left-hand change status.
/// \param r The right-hand change status.
LLVM_ABI ChangeStatus operator|(ChangeStatus l, ChangeStatus r);
/// Assign CHANGED if either operand is CHANGED.
///
/// \return Whether the state or IR changed.
///
/// \param l The left-hand change status (updated in place).
/// \param r The right-hand change status.
LLVM_ABI ChangeStatus &operator|=(ChangeStatus &l, ChangeStatus r);
/// Return CHANGED only if both operands are CHANGED.
///
/// \return CHANGED only if both operands are CHANGED.
///
/// \param l The left-hand change status.
/// \param r The right-hand change status.
LLVM_ABI ChangeStatus operator&(ChangeStatus l, ChangeStatus r);
/// Assign CHANGED only if both operands are CHANGED.
///
/// \return Whether the state or IR changed.
///
/// \param l The left-hand change status (updated in place).
/// \param r The right-hand change status.
LLVM_ABI ChangeStatus &operator&=(ChangeStatus &l, ChangeStatus r);

/// Dependence class between abstract attributes.
enum class DepClassTy {
  REQUIRED, ///< The target cannot be valid if the source is not.
  OPTIONAL, ///< The target may be valid if the source is not.
  NONE,     ///< Do not track a dependence between source and target.
};
///}

/// The data structure for the nodes of a dependency graph
struct AADepGraphNode {
public:
  /// Virtual destructor.
  virtual ~AADepGraphNode() = default;
  /// Dependence edge with an optional-bit tag.
  using DepTy = PointerIntPair<AADepGraphNode *, 1>;
  /// Set of dependence edges from this node.
  using DepSetTy = SmallSetVector<DepTy, 2>;

protected:
  /// Set of dependency graph nodes which should be updated if this one
  /// is updated. The bit encodes if it is optional.
  DepSetTy Deps;

  /// Return the dependence-graph node from edge \p DT.
  ///
  /// \return The dependence-graph node from edge \p DT.
  ///
  /// \param DT The dependence edge.
  static AADepGraphNode *DepGetVal(const DepTy &DT) { return DT.getPointer(); }
  /// Return the abstract attribute from dependence edge \p DT.
  ///
  /// \return The abstract attribute from dependence edge \p DT.
  ///
  /// \param DT The dependence edge.
  static AbstractAttribute *DepGetValAA(const DepTy &DT) {
    return cast<AbstractAttribute>(DT.getPointer());
  }

  /// Cast this node to an AbstractAttribute pointer.
  ///
  /// \return This node as an AbstractAttribute pointer.
  operator AbstractAttribute *() { return cast<AbstractAttribute>(this); }

public:
  /// Iterator over dependent graph nodes.
  using iterator = mapped_iterator<DepSetTy::iterator, decltype(&DepGetVal)>;
  /// Iterator over dependent abstract attributes.
  using aaiterator =
      mapped_iterator<DepSetTy::iterator, decltype(&DepGetValAA)>;

  /// Return an iterator to the first dependent abstract attribute.
  ///
  /// \return An iterator to the first dependent abstract attribute.
  aaiterator begin() { return aaiterator(Deps.begin(), &DepGetValAA); }
  /// Return an iterator past the last dependent abstract attribute.
  ///
  /// \return An iterator past the last dependent abstract attribute.
  aaiterator end() { return aaiterator(Deps.end(), &DepGetValAA); }
  /// Return an iterator to the first child dependence node.
  ///
  /// \return An iterator to the first child dependence node.
  iterator child_begin() { return iterator(Deps.begin(), &DepGetVal); }
  /// Return an iterator past the last child dependence node.
  ///
  /// \return An iterator past the last child dependence node.
  iterator child_end() { return iterator(Deps.end(), &DepGetVal); }

  /// Print this node to \p OS.
  ///
  /// \param OS The output stream.
  void print(raw_ostream &OS) const { print(nullptr, OS); }
  /// Print this node to \p OS, optionally with Attributor context.
  ///
  /// \param A Optional Attributor providing print context.
  /// \param OS The output stream.
  virtual void print(Attributor *A, raw_ostream &OS) const {
    OS << "AADepNode Impl\n";
  }
  /// Return the set of dependence edges.
  ///
  /// \return The set of dependence edges.
  DepSetTy &getDeps() { return Deps; }

  friend struct Attributor;
  friend struct AADepGraph;
};

/// The data structure for the dependency graph
///
/// Note that in this graph if there is an edge from A to B (A -> B),
/// then it means that B depends on A, and when the state of A is
/// updated, node B should also be updated
struct AADepGraph {
  /// Construct an empty dependence graph.
  AADepGraph() = default;
  /// Destroy the dependence graph.
  ~AADepGraph() = default;

  /// Dependence edge type used by the graph.
  using DepTy = AADepGraphNode::DepTy;
  /// Return the dependence-graph node from edge \p DT.
  ///
  /// \return The dependence-graph node from edge \p DT.
  ///
  /// \param DT The dependence edge.
  static AADepGraphNode *DepGetVal(const DepTy &DT) { return DT.getPointer(); }
  /// Iterator over dependence-graph nodes.
  using iterator =
      mapped_iterator<AADepGraphNode::DepSetTy::iterator, decltype(&DepGetVal)>;

  /// Synthetic root node that depends on every real graph node.
  ///
  /// There is no natural root for the dependency graph, but SCCIterator
  /// requires a single entry point, so we maintain this fake root.
  AADepGraphNode SyntheticRoot;
  /// Return the synthetic entry node for SCC iteration.
  ///
  /// \return The synthetic entry node for SCC iteration.
  AADepGraphNode *GetEntryNode() { return &SyntheticRoot; }

  /// Return an iterator to the first graph node.
  ///
  /// \return An iterator to the first graph node.
  iterator begin() { return SyntheticRoot.child_begin(); }
  /// Return an iterator past the last graph node.
  ///
  /// \return An iterator past the last graph node.
  iterator end() { return SyntheticRoot.child_end(); }

  /// Open a graphical view of the dependence graph.
  LLVM_ABI void viewGraph();

  /// Dump graph to file
  LLVM_ABI void dumpGraph();

  /// Print dependency graph
  LLVM_ABI void print();
};

/// Helper to describe and deal with positions in the LLVM-IR.
///
/// A position in the IR is described by an anchor value and an "offset" that
/// could be the argument number, for call sites and arguments, or an indicator
/// of the "position kind". The kinds, specified in the Kind enum below, include
/// the locations in the attribute list, i.a., function scope and return value,
/// as well as a distinction between call sites and functions. Finally, there
/// are floating values that do not have a corresponding attribute list
/// position.
struct IRPosition {
  // NOTE: In the future this definition can be changed to support recursive
  // functions.
  /// Call-base context associated with an IR position.
  using CallBaseContext = CallBase;

  /// The positions we distinguish in the IR.
  enum Kind : char {
    IRP_INVALID,  ///< An invalid position.
    IRP_FLOAT,    ///< A position that is not associated with a spot suitable
                  ///< for attributes. This could be any value or instruction.
    IRP_RETURNED, ///< An attribute for the function return value.
    IRP_CALL_SITE_RETURNED, ///< An attribute for a call site return value.
    IRP_FUNCTION,           ///< An attribute for a function (scope).
    IRP_CALL_SITE,          ///< An attribute for a call site (function scope).
    IRP_ARGUMENT,           ///< An attribute for a function argument.
    IRP_CALL_SITE_ARGUMENT, ///< An attribute for a call site argument.
  };

  /// Construct an invalid IR position.
  ///
  /// Available to create invalid positions implicitly. All other positions
  /// need to be created explicitly through the appropriate static member
  /// function.
  IRPosition() : Enc(nullptr, ENC_VALUE) { verify(); }

  /// Create a position describing the value of \p V.
  ///
  /// \return A reference to the requested object.
  ///
  /// \param V The value of interest.
  /// \param CBContext Optional call-base context for call-site-specific analysis.
  static const IRPosition value(const Value &V,
                                const CallBaseContext *CBContext = nullptr) {
    if (auto *Arg = dyn_cast<Argument>(&V))
      return IRPosition::argument(*Arg, CBContext);
    if (auto *CB = dyn_cast<CallBase>(&V))
      return IRPosition::callsite_returned(*CB);
    return IRPosition(const_cast<Value &>(V), IRP_FLOAT, CBContext);
  }

  /// Create a position describing the instruction \p I.
  ///
  /// This is different from the value version because call sites are treated as
  /// instructions rather than their return value in this function.
  ///
  /// \return A reference to the requested object.
  ///
  /// \param I The instruction of interest.
  /// \param CBContext Optional call-base context for call-site-specific analysis.
  static const IRPosition inst(const Instruction &I,
                               const CallBaseContext *CBContext = nullptr) {
    return IRPosition(const_cast<Instruction &>(I), IRP_FLOAT, CBContext);
  }

  /// Create a position describing the function scope of \p F.
  ///
  /// \return A reference to the requested object.
  ///
  /// \param F The function of interest.
  /// \param CBContext Optional call-base context for call-site-specific analysis.
  static const IRPosition function(const Function &F,
                                   const CallBaseContext *CBContext = nullptr) {
    return IRPosition(const_cast<Function &>(F), IRP_FUNCTION, CBContext);
  }

  /// Create a position describing the returned value of \p F.
  ///
  /// \return A reference to the requested object.
  ///
  /// \param F The function of interest.
  /// \param CBContext Optional call-base context for call-site-specific analysis.
  static const IRPosition returned(const Function &F,
                                   const CallBaseContext *CBContext = nullptr) {
    return IRPosition(const_cast<Function &>(F), IRP_RETURNED, CBContext);
  }

  /// Create a position describing the argument \p Arg.
  ///
  /// \return A reference to the requested object.
  ///
  /// \param Arg The argument of interest.
  /// \param CBContext Optional call-base context for call-site-specific analysis.
  static const IRPosition argument(const Argument &Arg,
                                   const CallBaseContext *CBContext = nullptr) {
    return IRPosition(const_cast<Argument &>(Arg), IRP_ARGUMENT, CBContext);
  }

  /// Create a position describing the function scope of \p CB.
  ///
  /// \return A reference to the requested object.
  ///
  /// \param CB The call base of interest.
  static const IRPosition callsite_function(const CallBase &CB) {
    return IRPosition(const_cast<CallBase &>(CB), IRP_CALL_SITE);
  }

  /// Create a position describing the returned value of \p CB.
  ///
  /// \return A reference to the requested object.
  ///
  /// \param CB The call base of interest.
  static const IRPosition callsite_returned(const CallBase &CB) {
    return IRPosition(const_cast<CallBase &>(CB), IRP_CALL_SITE_RETURNED);
  }

  /// Create a position describing the argument of \p CB at position \p ArgNo.
  ///
  /// \return A reference to the requested object.
  ///
  /// \param CB The call base of interest.
  /// \param ArgNo The call-site argument index.
  static const IRPosition callsite_argument(const CallBase &CB,
                                            unsigned ArgNo) {
    return IRPosition(const_cast<Use &>(CB.getArgOperandUse(ArgNo)),
                      IRP_CALL_SITE_ARGUMENT);
  }

  /// Create a position describing the argument of \p ACS at position \p ArgNo.
  ///
  /// \return The computed result.
  ///
  /// \param ACS The abstract call site of interest.
  /// \param ArgNo The call-site argument index.
  static const IRPosition callsite_argument(AbstractCallSite ACS,
                                            unsigned ArgNo) {
    if (ACS.getNumArgOperands() <= ArgNo)
      return IRPosition();
    int CSArgNo = ACS.getCallArgOperandNo(ArgNo);
    if (CSArgNo >= 0)
      return IRPosition::callsite_argument(
          cast<CallBase>(*ACS.getInstruction()), CSArgNo);
    return IRPosition();
  }

  /// Create a function-scope position matching the context of \p IRP.
  ///
  /// If \p IRP is a call site (see isAnyCallSitePosition()) then the result
  /// will be a call site position, otherwise the function position of the
  /// associated function.
  ///
  /// \return A pointer to the requested object, or null if unavailable.
  ///
  /// \param IRP The IR position providing context.
  /// \param CBContext Optional call-base context for call-site-specific analysis.
  static const IRPosition
  function_scope(const IRPosition &IRP,
                 const CallBaseContext *CBContext = nullptr) {
    if (IRP.isAnyCallSitePosition()) {
      return IRPosition::callsite_function(
          cast<CallBase>(IRP.getAnchorValue()));
    }
    assert(IRP.getAssociatedFunction());
    return IRPosition::function(*IRP.getAssociatedFunction(), CBContext);
  }

  /// Return true if this equals \p RHS.
  ///
  /// \return True if this equals \p RHS.
  ///
  /// \param RHS The other operand or state.
  bool operator==(const IRPosition &RHS) const {
    return Enc == RHS.Enc && RHS.CBContext == CBContext;
  }
  /// Return true if this differs from \p RHS.
  ///
  /// \return True if this differs from \p RHS.
  ///
  /// \param RHS The other operand or state.
  bool operator!=(const IRPosition &RHS) const { return !(*this == RHS); }

  /// Return the value this abstract attribute is anchored with.
  ///
  /// The anchor value might not be the associated value if the latter is not
  /// sufficient to determine where arguments will be manifested. This is, so
  /// far, only the case for call site arguments as the value is not sufficient
  /// to pinpoint them. Instead, we can use the call site as an anchor.
  ///
  /// \return The value this abstract attribute is anchored with.
  Value &getAnchorValue() const {
    switch (getEncodingBits()) {
    case ENC_VALUE:
    case ENC_RETURNED_VALUE:
    case ENC_FLOATING_FUNCTION:
      return *getAsValuePtr();
    case ENC_CALL_SITE_ARGUMENT_USE:
      return *(getAsUsePtr()->getUser());
    default:
      llvm_unreachable("Unkown encoding!");
    };
  }

  /// Return the associated function, if any.
  ///
  /// \return The associated function, if any.
  Function *getAssociatedFunction() const {
    if (auto *CB = dyn_cast<CallBase>(&getAnchorValue())) {
      // We reuse the logic that associates callback calles to arguments of a
      // call site here to identify the callback callee as the associated
      // function.
      if (Argument *Arg = getAssociatedArgument())
        return Arg->getParent();
      return dyn_cast_if_present<Function>(
          CB->getCalledOperand()->stripPointerCasts());
    }
    return getAnchorScope();
  }

  /// Return the associated argument, if any.
  ///
  /// \return The associated argument, if any.
  LLVM_ABI Argument *getAssociatedArgument() const;

  /// Return true if the position refers to a function interface, that is the
  /// function scope, the function return, or an argument.
  ///
  /// \return True if the position refers to a function interface, that is the function scope, the function return, or an argument.
  bool isFnInterfaceKind() const {
    switch (getPositionKind()) {
    case IRPosition::IRP_FUNCTION:
    case IRPosition::IRP_RETURNED:
    case IRPosition::IRP_ARGUMENT:
      return true;
    default:
      return false;
    }
  }

  /// Return true if this is a function or call site position.
  ///
  /// \return True if this is a function or call site position.
  bool isFunctionScope() const {
    switch (getPositionKind()) {
    case IRPosition::IRP_CALL_SITE:
    case IRPosition::IRP_FUNCTION:
      return true;
    default:
      return false;
    };
  }

  /// Return the Function surrounding the anchor value.
  ///
  /// \return The Function surrounding the anchor value.
  Function *getAnchorScope() const {
    Value &V = getAnchorValue();
    if (isa<Function>(V))
      return &cast<Function>(V);
    if (isa<Argument>(V))
      return cast<Argument>(V).getParent();
    if (isa<Instruction>(V))
      return cast<Instruction>(V).getFunction();
    return nullptr;
  }

  /// Return the context instruction, if any.
  ///
  /// \return The context instruction, if any.
  Instruction *getCtxI() const {
    Value &V = getAnchorValue();
    if (auto *I = dyn_cast<Instruction>(&V))
      return I;
    if (auto *Arg = dyn_cast<Argument>(&V))
      if (!Arg->getParent()->isDeclaration())
        return &Arg->getParent()->getEntryBlock().front();
    if (auto *F = dyn_cast<Function>(&V))
      if (!F->isDeclaration())
        return &(F->getEntryBlock().front());
    return nullptr;
  }

  /// Return the value this abstract attribute is associated with.
  ///
  /// \return The value this abstract attribute is associated with.
  Value &getAssociatedValue() const {
    if (getCallSiteArgNo() < 0 || isa<Argument>(&getAnchorValue()))
      return getAnchorValue();
    assert(isa<CallBase>(&getAnchorValue()) && "Expected a call base!");
    return *cast<CallBase>(&getAnchorValue())
                ->getArgOperand(getCallSiteArgNo());
  }

  /// Return the type this abstract attribute is associated with.
  ///
  /// \return The type this abstract attribute is associated with.
  Type *getAssociatedType() const {
    if (getPositionKind() == IRPosition::IRP_RETURNED)
      return getAssociatedFunction()->getReturnType();
    return getAssociatedValue().getType();
  }

  /// Return the callee-side argument number, or a negative value.
  ///
  /// If the associated value is an argument or call site argument, return its
  /// callee argument number; otherwise return a negative value. In contrast to
  /// `getCallSiteArgNo` this method will always return the "argument number"
  /// from the perspective of the callee. This may not be the same as the call
  /// site if this is a callback call.
  ///
  /// \return The callee-side argument number, or a negative value.
  int getCalleeArgNo() const {
    return getArgNo(/* CallbackCalleeArgIfApplicable */ true);
  }

  /// Return the call-site argument number of the associated value.
  ///
  /// If it is an argument or call site argument, otherwise a negative value.
  /// In contrast to getCalleeArgNo this method will always return the
  /// operand index in the call base.
  ///
  /// \return The call-site argument number of the associated value.
  int getCallSiteArgNo() const {
    return getArgNo(/* CallbackCalleeArgIfApplicable */ false);
  }

  /// Return the index in the attribute list for this position.
  ///
  /// \return The index in the attribute list for this position.
  unsigned getAttrIdx() const {
    switch (getPositionKind()) {
    case IRPosition::IRP_INVALID:
    case IRPosition::IRP_FLOAT:
      break;
    case IRPosition::IRP_FUNCTION:
    case IRPosition::IRP_CALL_SITE:
      return AttributeList::FunctionIndex;
    case IRPosition::IRP_RETURNED:
    case IRPosition::IRP_CALL_SITE_RETURNED:
      return AttributeList::ReturnIndex;
    case IRPosition::IRP_ARGUMENT:
      return getCalleeArgNo() + AttributeList::FirstArgIndex;
    case IRPosition::IRP_CALL_SITE_ARGUMENT:
      return getCallSiteArgNo() + AttributeList::FirstArgIndex;
    }
    llvm_unreachable(
        "There is no attribute index for a floating or invalid position!");
  }

  /// Return the value attributes are attached to.
  ///
  /// \return The value attributes are attached to.
  Value *getAttrListAnchor() const {
    if (auto *CB = dyn_cast<CallBase>(&getAnchorValue()))
      return CB;
    return getAssociatedFunction();
  }

  /// Return the attributes associated with this function or call site scope.
  ///
  /// \return The attributes associated with this function or call site scope.
  AttributeList getAttrList() const {
    if (auto *CB = dyn_cast<CallBase>(&getAnchorValue()))
      return CB->getAttributes();
    return getAssociatedFunction()->getAttributes();
  }

  /// Update the attributes associated with this function or call site scope.
  ///
  /// \param AttrList The attribute list to install.
  void setAttrList(const AttributeList &AttrList) const {
    if (auto *CB = dyn_cast<CallBase>(&getAnchorValue()))
      return CB->setAttributes(AttrList);
    return getAssociatedFunction()->setAttributes(AttrList);
  }

  /// Return the number of arguments associated with this function or call site
  /// scope.
  ///
  /// \return The number of arguments associated with this function or call site scope.
  unsigned getNumArgs() const {
    assert((getPositionKind() == IRP_CALL_SITE ||
            getPositionKind() == IRP_FUNCTION) &&
           "Only valid for function/call site positions!");
    if (auto *CB = dyn_cast<CallBase>(&getAnchorValue()))
      return CB->arg_size();
    return getAssociatedFunction()->arg_size();
  }

  /// Return theargument \p ArgNo associated with this function or call site
  /// scope.
  ///
  /// \return Theargument \p ArgNo associated with this function or call site scope.
  ///
  /// \param ArgNo The argument number.
  Value *getArg(unsigned ArgNo) const {
    assert((getPositionKind() == IRP_CALL_SITE ||
            getPositionKind() == IRP_FUNCTION) &&
           "Only valid for function/call site positions!");
    if (auto *CB = dyn_cast<CallBase>(&getAnchorValue()))
      return CB->getArgOperand(ArgNo);
    return getAssociatedFunction()->getArg(ArgNo);
  }

  /// Return the associated position kind.
  ///
  /// \return The associated position kind.
  Kind getPositionKind() const {
    char EncodingBits = getEncodingBits();
    if (EncodingBits == ENC_CALL_SITE_ARGUMENT_USE)
      return IRP_CALL_SITE_ARGUMENT;
    if (EncodingBits == ENC_FLOATING_FUNCTION)
      return IRP_FLOAT;

    Value *V = getAsValuePtr();
    if (!V)
      return IRP_INVALID;
    if (isa<Argument>(V))
      return IRP_ARGUMENT;
    if (isa<Function>(V))
      return isReturnPosition(EncodingBits) ? IRP_RETURNED : IRP_FUNCTION;
    if (isa<CallBase>(V))
      return isReturnPosition(EncodingBits) ? IRP_CALL_SITE_RETURNED
                                            : IRP_CALL_SITE;
    return IRP_FLOAT;
  }

  /// Return true if this is any call-site-related position.
  ///
  /// \return True if this is any call-site-related position.
  bool isAnyCallSitePosition() const {
    switch (getPositionKind()) {
    case IRPosition::IRP_CALL_SITE:
    case IRPosition::IRP_CALL_SITE_RETURNED:
    case IRPosition::IRP_CALL_SITE_ARGUMENT:
      return true;
    default:
      return false;
    }
  }

  /// Return true if the position is an argument or call site argument.
  ///
  /// \return True if the position is an argument or call site argument.
  bool isArgumentPosition() const {
    switch (getPositionKind()) {
    case IRPosition::IRP_ARGUMENT:
    case IRPosition::IRP_CALL_SITE_ARGUMENT:
      return true;
    default:
      return false;
    }
  }

  /// Return the same position without the call base context.
  ///
  /// \return The same position without the call base context.
  IRPosition stripCallBaseContext() const {
    IRPosition Result = *this;
    Result.CBContext = nullptr;
    return Result;
  }

  /// Get the call base context from the position.
  ///
  /// \return A pointer to the requested object, or null if unavailable.
  const CallBaseContext *getCallBaseContext() const { return CBContext; }

  /// Check if the position has any call base context.
  ///
  /// \return True if the position has any call base context.
  bool hasCallBaseContext() const { return CBContext != nullptr; }

  /// Conversion into a void * to allow reuse of pointer hashing.
  ///
  /// \return An opaque pointer encoding of this position.
  operator void *() const { return Enc.getOpaqueValue(); }

private:
  /// Private constructor for special values only!
  ///
  /// \return A pointer to the requested object, or null if unavailable.
  explicit IRPosition(void *Ptr, const CallBaseContext *CBContext = nullptr)
      : CBContext(CBContext) {
    Enc.setFromOpaqueValue(Ptr);
  }

  /// IRPosition anchored at \p AnchorVal with kind/argument numbet \p PK.
  ///
  /// \return A pointer to the requested object, or null if unavailable.
  explicit IRPosition(Value &AnchorVal, Kind PK,
                      const CallBaseContext *CBContext = nullptr)
      : CBContext(CBContext) {
    switch (PK) {
    case IRPosition::IRP_INVALID:
      llvm_unreachable("Cannot create invalid IRP with an anchor value!");
      break;
    case IRPosition::IRP_FLOAT:
      // Special case for floating functions.
      if (isa<Function>(AnchorVal) || isa<CallBase>(AnchorVal))
        Enc = {&AnchorVal, ENC_FLOATING_FUNCTION};
      else
        Enc = {&AnchorVal, ENC_VALUE};
      break;
    case IRPosition::IRP_FUNCTION:
    case IRPosition::IRP_CALL_SITE:
      Enc = {&AnchorVal, ENC_VALUE};
      break;
    case IRPosition::IRP_RETURNED:
    case IRPosition::IRP_CALL_SITE_RETURNED:
      Enc = {&AnchorVal, ENC_RETURNED_VALUE};
      break;
    case IRPosition::IRP_ARGUMENT:
      Enc = {&AnchorVal, ENC_VALUE};
      break;
    case IRPosition::IRP_CALL_SITE_ARGUMENT:
      llvm_unreachable(
          "Cannot create call site argument IRP with an anchor value!");
      break;
    }
    /// Assert internal invariants of this access.
    verify();
  }

  /// Return the callee argument number of the associated value if it is an
  /// argument or call site argument. See also `getCalleeArgNo` and
  /// `getCallSiteArgNo`.
  ///
  /// \return The callee argument number of the associated value if it is an argument or call site argument.
  int getArgNo(bool CallbackCalleeArgIfApplicable) const {
    if (CallbackCalleeArgIfApplicable)
      if (Argument *Arg = getAssociatedArgument())
        return Arg->getArgNo();
    switch (getPositionKind()) {
    case IRPosition::IRP_ARGUMENT:
      return cast<Argument>(getAsValuePtr())->getArgNo();
    case IRPosition::IRP_CALL_SITE_ARGUMENT: {
      Use &U = *getAsUsePtr();
      return cast<CallBase>(U.getUser())->getArgOperandNo(&U);
    }
    default:
      return -1;
    }
  }

  /// IRPosition for the use \p U. The position kind \p PK needs to be
  /// IRP_CALL_SITE_ARGUMENT, the anchor value is the user, the associated value
  /// the used value.
  ///
  /// \return The computed result.
  explicit IRPosition(Use &U, Kind PK) {
    assert(PK == IRP_CALL_SITE_ARGUMENT &&
           "Use constructor is for call site arguments only!");
    Enc = {&U, ENC_CALL_SITE_ARGUMENT_USE};
    /// Assert internal invariants of this access.
    verify();
  }

  /// Verify internal invariants.
  LLVM_ABI void verify();

  /// Return the underlying pointer as Value *, valid for all positions but
  /// IRP_CALL_SITE_ARGUMENT.
  ///
  /// \return The underlying pointer as Value *, valid for all positions but IRP_CALL_SITE_ARGUMENT.
  Value *getAsValuePtr() const {
    assert(getEncodingBits() != ENC_CALL_SITE_ARGUMENT_USE &&
           "Not a value pointer!");
    return reinterpret_cast<Value *>(Enc.getPointer());
  }

  /// Return the underlying pointer as Use *, valid only for
  /// IRP_CALL_SITE_ARGUMENT positions.
  ///
  /// \return The underlying pointer as Use *, valid only for IRP_CALL_SITE_ARGUMENT positions.
  Use *getAsUsePtr() const {
    assert(getEncodingBits() == ENC_CALL_SITE_ARGUMENT_USE &&
           "Not a value pointer!");
    return reinterpret_cast<Use *>(Enc.getPointer());
  }

  /// Return true if \p EncodingBits describe a returned or call site returned
  /// position.
  ///
  /// \return True if \p EncodingBits describe a returned or call site returned position.
  static bool isReturnPosition(char EncodingBits) {
    return EncodingBits == ENC_RETURNED_VALUE;
  }

  /// Return true if the encoding bits describe a returned or call site returned
  /// position.
  ///
  /// \return True if the encoding bits describe a returned or call site returned position.
  bool isReturnPosition() const { return isReturnPosition(getEncodingBits()); }

  /// The encoding of the IRPosition is a combination of a pointer and two
  /// encoding bits. The values of the encoding bits are defined in the enum
  /// below. The pointer is either a Value* (for the first three encoding bit
  /// combinations) or Use* (for ENC_CALL_SITE_ARGUMENT_USE).
  ///
  ///{
  enum {
    ENC_VALUE = 0b00,
    ENC_RETURNED_VALUE = 0b01,
    ENC_FLOATING_FUNCTION = 0b10,
    ENC_CALL_SITE_ARGUMENT_USE = 0b11,
  };

  // Reserve the maximal amount of bits so there is no need to mask out the
  // remaining ones. We will not encode anything else in the pointer anyway.
  static constexpr int NumEncodingBits =
      PointerLikeTypeTraits<void *>::NumLowBitsAvailable;
  static_assert(NumEncodingBits >= 2, "At least two bits are required!");

  /// The pointer with the encoding bits.
  PointerIntPair<void *, NumEncodingBits, char> Enc;
  ///}

  /// Call base context. Used for callsite specific analysis.
  const CallBaseContext *CBContext = nullptr;

  /// Return the encoding bits.
  ///
  /// \return The encoding bits.
  char getEncodingBits() const { return Enc.getInt(); }
};

/// Helper that allows IRPosition as a key in a DenseMap.
/// DenseMapInfo specialization for IRPosition keys.
template <> struct DenseMapInfo<IRPosition> {
  /// Return the hash value for the given key.
  ///
  /// \return The hash value for the given key.
  ///
  /// \param IRP The IR position to hash.
  static unsigned getHashValue(const IRPosition &IRP) {
    return (DenseMapInfo<void *>::getHashValue(IRP) << 4) ^
           (DenseMapInfo<Value *>::getHashValue(IRP.getCallBaseContext()));
  }

  /// Return true if the two keys are equal.
  ///
  /// \return True if the two keys are equal.
  ///
  /// \param a The first IR position.
  /// \param b The second IR position.
  static bool isEqual(const IRPosition &a, const IRPosition &b) {
    return a == b;
  }
};

/// A visitor class for IR positions.
///
/// Given a position P, the SubsumingPositionIterator allows to visit "subsuming
/// positions" wrt. attributes/information. Thus, if a piece of information
/// holds for a subsuming position, it also holds for the position P.
///
/// The subsuming positions always include the initial position and then,
/// depending on the position kind, additionally the following ones:
/// - for IRP_RETURNED:
///   - the function (IRP_FUNCTION)
/// - for IRP_ARGUMENT:
///   - the function (IRP_FUNCTION)
/// - for IRP_CALL_SITE:
///   - the callee (IRP_FUNCTION), if known
/// - for IRP_CALL_SITE_RETURNED:
///   - the callee (IRP_RETURNED), if known
///   - the call site (IRP_FUNCTION)
///   - the callee (IRP_FUNCTION), if known
/// - for IRP_CALL_SITE_ARGUMENT:
///   - the argument of the callee (IRP_ARGUMENT), if known
///   - the callee (IRP_FUNCTION), if known
///   - the position the call site argument is associated with if it is not
///     anchored to the call site, e.g., if it is an argument then the argument
///     (IRP_ARGUMENT)
class SubsumingPositionIterator {
  SmallVector<IRPosition, 4> IRPositions;
  using iterator = decltype(IRPositions)::iterator;

public:
  /// Construct an iterator over subsuming IR positions.
  ///
  /// \return The requested iterator.
  ///
  /// \param IRP The IR position whose subsuming positions are visited.
  LLVM_ABI SubsumingPositionIterator(const IRPosition &IRP);
  /// Return an iterator to the first subsuming IR position.
  ///
  /// \return An iterator to the first subsuming IR position.
  iterator begin() { return IRPositions.begin(); }
  /// Return an iterator past the last subsuming IR position.
  ///
  /// \return An iterator past the last subsuming IR position.
  iterator end() { return IRPositions.end(); }
};

/// Wrapper for FunctionAnalysisManager and legacy pass-manager analysis lookup.
struct AnalysisGetter {
  // The client may be running the old pass manager, in which case, we need to
  // map the requested Analysis to its equivalent wrapper in the old pass
  // manager. The scheme implemented here does not require every Analysis to be
  // updated. Only those new analyses that the client cares about in the old
  // pass manager need to expose a LegacyWrapper type, and that wrapper should
  // support a getResult() method that matches the new Analysis.
  //
  // We need SFINAE to check for the LegacyWrapper, but function templates don't
  // allow partial specialization, which is needed in this case. So instead, we
  // use a constexpr bool to perform the SFINAE, and then use this information
  // inside the function template.
  template <typename, typename = void>
  /// True if this analysis has a legacy pass manager wrapper.
  static constexpr bool HasLegacyWrapper = false;

  /// Return analysis \p Analysis for function \p F, or null if unavailable.
  ///
  /// \return Analysis \p Analysis for function \p F, or null if unavailable.
  ///
  /// \param F The function to analyze.
  /// \param RequestCachedOnly If true, return only cached results.
  template <typename Analysis>
  typename Analysis::Result *getAnalysis(const Function &F,
                                         bool RequestCachedOnly = false) {
    if (!LegacyPass && !FAM)
      return nullptr;
    if (FAM) {
      if (CachedOnly || RequestCachedOnly)
        return FAM->getCachedResult<Analysis>(const_cast<Function &>(F));
      return &FAM->getResult<Analysis>(const_cast<Function &>(F));
    }
    if constexpr (HasLegacyWrapper<Analysis>) {
      if (!CachedOnly && !RequestCachedOnly)
        return &LegacyPass
                    ->getAnalysis<typename Analysis::LegacyWrapper>(
                        const_cast<Function &>(F))
                    .getResult();
      if (auto *P =
              LegacyPass
                  ->getAnalysisIfAvailable<typename Analysis::LegacyWrapper>())
        return &P->getResult();
    }
    return nullptr;
  }

  /// Invalidates the analyses. Valid only when using the new pass manager.
  void invalidateAnalyses() {
    assert(FAM && "Can only be used from the new PM!");
    FAM->clear();
  }

  /// Construct an analysis getter backed by the new pass manager.
  ///
  /// \param FAM The function analysis manager.
  /// \param CachedOnly If true, only return cached analysis results.
  AnalysisGetter(FunctionAnalysisManager &FAM, bool CachedOnly = false)
      : FAM(&FAM), CachedOnly(CachedOnly) {}
  /// Construct an analysis getter backed by the legacy pass manager.
  ///
  /// \param P The legacy pass providing analyses.
  /// \param CachedOnly If true, only return cached analysis results.
  AnalysisGetter(Pass *P, bool CachedOnly = false)
      : LegacyPass(P), CachedOnly(CachedOnly) {}
  /// Construct a default analysis getter.
  AnalysisGetter() = default;

private:
  FunctionAnalysisManager *FAM = nullptr;
  Pass *LegacyPass = nullptr;

  /// If \p CachedOnly is true, no pass is created, just existing results are
  /// used. Also available per request.
  bool CachedOnly = false;
};

/// Specialization: true when Analysis exposes a LegacyWrapper nested type.
template <typename Analysis>
constexpr bool AnalysisGetter::HasLegacyWrapper<
    Analysis, std::void_t<typename Analysis::LegacyWrapper>> = true;

/// Data structure to hold cached (LLVM-IR) information.
///
/// All attributes are given an InformationCache object at creation time to
/// avoid inspection of the IR by all of them individually. This default
/// InformationCache will hold information required by 'default' attributes,
/// thus the ones deduced when Attributor::identifyDefaultAbstractAttributes(..)
/// is called.
///
/// If custom abstract attributes, registered manually through
/// Attributor::registerAA(...), need more information, especially if it is not
/// reusable, it is advised to inherit from the InformationCache and cast the
/// instance down in the abstract attributes.
struct InformationCache {
  /// Construct an information cache for the module.
  ///
  /// \param M The module being analyzed.
  /// \param AG Analysis getter used to populate cached analyses.
  /// \param Allocator Bump-pointer allocator for cache-owned objects.
  /// \param CGSCC CG-SCC being analyzed, or null for a module pass.
  /// \param UseExplorer If true, create a MustBeExecutedContextExplorer.
  InformationCache(const Module &M, AnalysisGetter &AG,
                   BumpPtrAllocator &Allocator, SetVector<Function *> *CGSCC,
                   bool UseExplorer = true)
      : CGSCC(CGSCC), M(M), Allocator(Allocator), AG(AG) {
    if (UseExplorer)
      Explorer = new (Allocator) MustBeExecutedContextExplorer(
          /* ExploreInterBlock */
          true, /* ExploreCFGForward */ true,
          /* ExploreCFGBackward */ true,
          /* LIGetter */
          [&](const Function &F) { return AG.getAnalysis<LoopAnalysis>(F); },
          /* DTGetter */
          [&](const Function &F) {
            return AG.getAnalysis<DominatorTreeAnalysis>(F);
          },
          /* PDTGetter */
          [&](const Function &F) {
            return AG.getAnalysis<PostDominatorTreeAnalysis>(F);
          });
  }

  /// Destroy the information cache.
  ///
  /// \return The computed result.
  virtual ~InformationCache() {
    // The FunctionInfo objects are allocated via a BumpPtrAllocator, we call
    // the destructor manually.
    for (auto &It : FuncInfoMap)
      It.getSecond()->~FunctionInfo();
    // Same is true for the instruction exclusions sets.
    using AA::InstExclusionSetTy;
    for (auto *BES : BESets)
      BES->~InstExclusionSetTy();
    if (Explorer)
      Explorer->~MustBeExecutedContextExplorer();
  }

  /// Apply \p CB to each use of \p F, optionally through constant expressions.
  ///
  /// If \p LookThroughConstantExprUses is true, constant expression users are
  /// not given to \p CB but their uses are traversed transitively.
  ///
  /// \param F The function whose uses are visited.
  /// \param CB Callback invoked for each visited use.
  /// \param LookThroughConstantExprUses If true, traverse through constant expr uses.
  template <typename CBTy>
  static void foreachUse(Function &F, CBTy CB,
                         bool LookThroughConstantExprUses = true) {
    SmallVector<Use *, 8> Worklist(make_pointer_range(F.uses()));

    for (unsigned Idx = 0; Idx < Worklist.size(); ++Idx) {
      Use &U = *Worklist[Idx];

      // Allow use in constant bitcasts and simply look through them.
      if (LookThroughConstantExprUses && isa<ConstantExpr>(U.getUser())) {
        for (Use &CEU : cast<ConstantExpr>(U.getUser())->uses())
          /// Append a range to the list.
          Worklist.push_back(&CEU);
        continue;
      }

      CB(U);
    }
  }

  /// The CG-SCC the pass is run on, or nullptr if it is a module pass.
  const SetVector<Function *> *const CGSCC = nullptr;

  /// A vector type to hold instructions.
  using InstructionVectorTy = SmallVector<Instruction *, 8>;

  /// A map type from opcodes to instructions with this opcode.
  using OpcodeInstMapTy = DenseMap<unsigned, InstructionVectorTy *>;

  /// Return the map that relates "interesting" opcodes with all instructions
  /// with that opcode in \p F.
  ///
  /// \return The map that relates "interesting" opcodes with all instructions with that opcode in \p F.
  ///
  /// \param F The function of interest.
  OpcodeInstMapTy &getOpcodeInstMapForFunction(const Function &F) {
    return getFunctionInfo(F).OpcodeInstMap;
  }

  /// Return the instructions in \p F that may read or write memory.
  ///
  /// \return The instructions in \p F that may read or write memory.
  ///
  /// \param F The function of interest.
  InstructionVectorTy &getReadOrWriteInstsForFunction(const Function &F) {
    return getFunctionInfo(F).RWInsts;
  }

  /// Return the must-be-executed context explorer, if constructed.
  ///
  /// \return The must-be-executed context explorer, if constructed.
  MustBeExecutedContextExplorer *getMustBeExecutedContextExplorer() {
    return Explorer;
  }

  /// Return TargetLibraryInfo for function \p F.
  ///
  /// \return TargetLibraryInfo for function \p F.
  ///
  /// \param F The function of interest.
  TargetLibraryInfo *getTargetLibraryInfoForFunction(const Function &F) {
    return AG.getAnalysis<TargetLibraryAnalysis>(F);
  }

  /// Return true if \p F has the "kernel" function attribute
  ///
  /// \return True if \p F has the "kernel" function attribute.
  ///
  /// \param F The function of interest.
  bool isKernel(const Function &F) {
    FunctionInfo &FI = getFunctionInfo(F);
    return FI.IsKernel;
  }

  /// Return true if \p Arg is involved in a must-tail call, thus the argument
  /// of the caller or callee.
  ///
  /// \return True if \p Arg is involved in a must-tail call, thus the argument of the caller or callee.
  ///
  /// \param Arg The argument being rewritten or inspected.
  bool isInvolvedInMustTailCall(const Argument &Arg) {
    FunctionInfo &FI = getFunctionInfo(*Arg.getParent());
    return FI.CalledViaMustTail || FI.ContainsMustTailCall;
  }

  /// Return true if the value is only used by llvm.assume.
  ///
  /// \return True if the value is only used by llvm.assume.
  ///
  /// \param I The instruction to inspect.
  bool isOnlyUsedByAssume(const Instruction &I) const {
    return AssumeOnlyValues.contains(&I);
  }

  /// Invalidates the cached analyses. Valid only when using the new pass
  /// manager.
  void invalidateAnalyses() { AG.invalidateAnalyses(); }

  /// Return the analysis result from a pass \p AP for function \p F.
  ///
  /// \return The analysis result from a pass \p AP for function \p F.
  ///
  /// \param F The function to analyze.
  /// \param CachedOnly If true, return only cached analysis results.
  template <typename AP>
  typename AP::Result *getAnalysisResultForFunction(const Function &F,
                                                    bool CachedOnly = false) {
    return AG.getAnalysis<AP>(F, CachedOnly);
  }

  /// Return the cached module.
  ///
  /// \return The cached module.
  const Module &getModule() const { return M; }

  /// Return datalayout used in the module.
  ///
  /// \return Datalayout used in the module.
  const DataLayout &getDL() const { return M.getDataLayout(); }

  /// Return the map conaining all the knowledge we have from `llvm.assume`s.
  ///
  /// \return The map conaining all the knowledge we have from `llvm.assume`s.
  const RetainedKnowledgeMap &getKnowledgeMap() const { return KnowledgeMap; }

  /// Given \p BES, return a uniqued version.
  ///
  /// \return A pointer to the requested object, or null if unavailable.
  ///
  /// \param BES The block-execution exclusion set to uniquify.
  const AA::InstExclusionSetTy *
  getOrCreateUniqueBlockExecutionSet(const AA::InstExclusionSetTy *BES) {
    auto It = BESets.find(BES);
    if (It != BESets.end())
      return *It;
    auto *UniqueBES = new (Allocator) AA::InstExclusionSetTy(*BES);
    bool Success = BESets.insert(UniqueBES).second;
    (void)Success;
    assert(Success && "Expected only new entries to be added");
    return UniqueBES;
  }

  /// Return true if the stack (llvm::Alloca) can be accessed by other threads.
  ///
  /// \return True if the stack (llvm::Alloca) can be accessed by other threads.
  bool stackIsAccessibleByOtherThreads() { return !IsTargetGPU(); }

  /// Return true if the target is a GPU.
  ///
  /// \return True if the target is a GPU.
  bool IsTargetGPU() const { return M.getTargetTriple().isGPU(); }

  /// Return all functions that might be called indirectly, only valid for
  /// closed world modules (see isClosedWorldModule).
  ///
  /// \return All functions that might be called indirectly, only valid for closed world modules (see isClosedWorldModule).
  ///
  /// \param A The Attributor managing the analysis.
  LLVM_ABI ArrayRef<Function *>
  getIndirectlyCallableFunctions(Attributor &A) const;

  /// Return the flat address space if the associated target has.
  ///
  /// \return The flat address space if the associated target has.
  LLVM_ABI std::optional<unsigned> getFlatAddressSpace() const;

  /// Return the maximum address space ID used in the module.
  ///
  /// \return The maximum address space ID used in the module.
  virtual unsigned getMaxAddrSpace() const { return ~0U; }

private:
  struct FunctionInfo {
    LLVM_ABI ~FunctionInfo();

    /// A nested map that remembers all instructions in a function with a
    /// certain instruction opcode (Instruction::getOpcode()).
    OpcodeInstMapTy OpcodeInstMap;

    /// A map from functions to their instructions that may read or write
    /// memory.
    InstructionVectorTy RWInsts;

    /// Function is called by a `musttail` call.
    bool CalledViaMustTail;

    /// Function contains a `musttail` call.
    bool ContainsMustTailCall;

    /// Function has the `"kernel"` attribute
    bool IsKernel;
  };

  /// A map type from functions to informatio about it.
  DenseMap<const Function *, FunctionInfo *> FuncInfoMap;

  /// Return information about the function \p F, potentially by creating it.
  ///
  /// \return Information about the function \p F, potentially by creating it.
  FunctionInfo &getFunctionInfo(const Function &F) {
    FunctionInfo *&FI = FuncInfoMap[&F];
    if (!FI) {
      FI = new (Allocator) FunctionInfo();
      initializeInformationCache(F, *FI);
    }
    return *FI;
  }

  /// Vector of functions that might be callable indirectly, i.a., via a
  /// function pointer.
  SmallVector<Function *> IndirectlyCallableFunctions;

  /// Initialize the function information cache \p FI for the function \p F.
  ///
  /// This method needs to be called for all function that might be looked at
  /// through the information cache interface *prior* to looking at them.
  LLVM_ABI void initializeInformationCache(const Function &F, FunctionInfo &FI);

  /// The module.
  const Module &M;

  /// The allocator used to allocate memory, e.g. for `FunctionInfo`s.
  BumpPtrAllocator &Allocator;

  /// MustBeExecutedContextExplorer
  MustBeExecutedContextExplorer *Explorer = nullptr;

  /// A map with knowledge retained in `llvm.assume` instructions.
  RetainedKnowledgeMap KnowledgeMap;

  /// A container for all instructions that are only used by `llvm.assume`.
  SetVector<const Instruction *> AssumeOnlyValues;

  /// Cache for block sets to allow reuse.
  DenseSet<const AA::InstExclusionSetTy *> BESets;

  /// Getters for analysis.
  AnalysisGetter &AG;

  /// Set of inlineable functions
  SmallPtrSet<const Function *, 8> InlineableFunctions;

  /// Give the Attributor access to the members so
  /// Attributor::identifyDefaultAbstractAttributes(...) can initialize them.
  friend struct Attributor;
};

/// Configuration for the Attributor.
struct AttributorConfig {

  /// Construct an Attributor configuration.
  ///
  /// \param CGUpdater Helper used to update the call graph.
  AttributorConfig(CallGraphUpdater &CGUpdater) : CGUpdater(CGUpdater) {}

  /// True if the Attributor user is a module pass.
  ///
  /// This determines what IR we can look at and modify. If it is a module
  /// pass we might deduce facts outside the initial function set and modify
  /// functions outside that set.
  bool IsModulePass = true;

  /// Flag to determine if we can delete functions or keep dead ones around.
  bool DeleteFns = true;

  /// Flag to determine if we rewrite function signatures.
  bool RewriteSignatures = true;

  /// Flag to determine if we want to initialize all default AAs for an internal
  /// function marked live. See also: InitializationCallback>
  bool DefaultInitializeLiveInternals = true;

  /// Flag to determine if we should skip all liveness checks early on.
  bool UseLiveness = true;

  /// Flag to indicate if the entire world is contained in this module, that
  /// is, no outside functions exist.
  bool IsClosedWorldModule = false;

  /// Callback function to be invoked on internal functions marked live.
  ///
  /// \return A reference to the requested object.
  std::function<void(Attributor &A, const Function &F)> InitializationCallback =
      nullptr;

  /// Callback function to determine if an indirect call targets should be made
  /// direct call targets (with an if-cascade).
  ///
  /// \return True if the condition described by this query holds.
  std::function<bool(Attributor &A, const AbstractAttribute &AA, CallBase &CB,
                     Function &AssumedCallee, unsigned NumAssumedCallees)>
      IndirectCalleeSpecializationCallback = nullptr;

  /// Helper to update an underlying call graph and to delete functions.
  CallGraphUpdater &CGUpdater;

  /// If not null, a set limiting the attribute opportunities.
  DenseSet<const char *> *Allowed = nullptr;

  /// Maximum number of iterations to run until fixpoint.
  std::optional<unsigned> MaxFixpointIterations;

  /// Callback type that returns an ORE object from a Function pointer.
  using OptimizationRemarkGetter =
      function_ref<OptimizationRemarkEmitter &(Function *)>;
  /// Callback that returns an OptimizationRemarkEmitter for a function.
  OptimizationRemarkGetter OREGetter = nullptr;

  /// The name of the pass running the attributor, used to emit remarks.
  const char *PassName = nullptr;

  /// Callback type determining if a function is IPO-amendable.
  using IPOAmendableCBTy = std::function<bool(const Function &F)>;
  /// Callback determining if a function is IPO-amendable.
  IPOAmendableCBTy IPOAmendableCB;
};

/// A debug counter to limit the number of AAs created.
DEBUG_COUNTER(NumAbstractAttributes, "num-abstract-attributes",
              "How many AAs should be initialized");

/// The fixpoint analysis framework that orchestrates the attribute deduction.
///
/// The Attributor provides a general abstract analysis framework (guided
/// fixpoint iteration) as well as helper functions for the deduction of
/// (LLVM-IR) attributes. However, also other code properties can be deduced,
/// propagated, and ultimately manifested through the Attributor framework. This
/// is particularly useful if these properties interact with attributes and a
/// co-scheduled deduction allows to improve the solution. Even if not, thus if
/// attributes/properties are completely isolated, they should use the
/// Attributor framework to reduce the number of fixpoint iteration frameworks
/// in the code base. Note that the Attributor design makes sure that isolated
/// attributes are not impacted, in any way, by others derived at the same time
/// if there is no cross-reasoning performed.
///
/// The public facing interface of the Attributor is kept simple and basically
/// allows abstract attributes to one thing, query abstract attributes
/// in-flight. There are two reasons to do this:
///    a) The optimistic state of one abstract attribute can justify an
///       optimistic state of another, allowing to framework to end up with an
///       optimistic (=best possible) fixpoint instead of one based solely on
///       information in the IR.
///    b) This avoids reimplementing various kinds of lookups, e.g., to check
///       for existing IR attributes, in favor of a single lookups interface
///       provided by an abstract attribute subclass.
///
/// NOTE: The mechanics of adding a new "concrete" abstract attribute are
///       described in the file comment.
struct Attributor {

  /// Constructor
  ///
  /// \return A pointer to the requested object, or null if unavailable.
  ///
  /// \param Functions The set of functions we are deriving attributes for.
  /// \param InfoCache Cache to hold various information accessible for
  ///                  the abstract attributes.
  /// \param Configuration The Attributor configuration which determines what
  ///                      generic features to use.
  LLVM_ABI Attributor(SetVector<Function *> &Functions,
                      InformationCache &InfoCache,
                      AttributorConfig Configuration);

  /// Destroy the Attributor and its abstract attributes.
  ///
  /// \return The computed result.
  LLVM_ABI ~Attributor();

  /// Run the analyses until a fixpoint is reached or enforced (timeout).
  ///
  /// The attributes registered with this Attributor can be used after as long
  /// as the Attributor is not destroyed (it owns the attributes now).
  ///
  /// \Returns CHANGED if the IR was changed, otherwise UNCHANGED.
  LLVM_ABI ChangeStatus run();

  /// Lookup an abstract attribute of type \p AAType at position \p IRP.
  ///
  /// This method is the only (supported) way an abstract attribute can retrieve
  /// information from another abstract attribute. As an example, take an
  /// abstract attribute that determines the memory access behavior for a
  /// argument (readnone, readonly, ...). It should use `getAAFor` to get the
  /// most optimistic information for other abstract attributes in-flight, e.g.
  /// the one reasoning about the "captured" state for the argument or the one
  /// reasoning on the memory access behavior of the function as a whole.
  ///
  /// While no abstract attribute is found equivalent positions are checked,
  /// see SubsumingPositionIterator. Thus, the returned abstract attribute
  /// might be anchored at a different position, e.g., the callee if \p IRP
  /// is a call base.
  ///
  /// If the DepClass enum is set to `DepClassTy::None` the dependence from
  /// \p QueryingAA to the return abstract attribute is not automatically
  /// recorded. This should only be used if the caller will record the
  /// dependence explicitly if necessary, thus if it the returned abstract
  /// attribute is used for reasoning. To record the dependences explicitly use
  /// the `Attributor::recordDependence` method.
  ///
  /// \return A pointer to the requested abstract attribute, or null if
  ///         unavailable.
  ///
  /// \param QueryingAA The abstract attribute issuing the query.
  /// \param IRP The IR position to look up.
  /// \param DepClass The dependence class to record for the query.
  template <typename AAType>
  const AAType *getAAFor(const AbstractAttribute &QueryingAA,
                         const IRPosition &IRP, DepClassTy DepClass) {
    return getOrCreateAAFor<AAType>(IRP, &QueryingAA, DepClass,
                                    /* ForceUpdate */ false);
  }

  /// Get or create an abstract attribute of type AAType at \p IRP.
  ///
  /// Allows omitting a querying abstract attribute. Using this after
  /// Attributor started running is restricted to the Attributor itself.
  /// Initial seeding of AAs can be done via this function.
  /// NOTE: ForceUpdate is ignored outside the update stage.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param IRP The IR position to look up or create.
  /// \param QueryingAA The abstract attribute issuing the query, or null.
  /// \param DepClass The dependence class to record for the query.
  /// \param ForceUpdate If true, force an update of the abstract attribute.
  /// \param UpdateAfterInit If true, update the AA after initialization.
  template <typename AAType>
  const AAType *getOrCreateAAFor(IRPosition IRP,
                                 const AbstractAttribute *QueryingAA,
                                 DepClassTy DepClass, bool ForceUpdate = false,
                                 bool UpdateAfterInit = true) {
    if (!shouldPropagateCallBaseContext(IRP))
      IRP = IRP.stripCallBaseContext();

    if (AAType *AAPtr = lookupAAFor<AAType>(IRP, QueryingAA, DepClass,
                                            /* AllowInvalidState */ true)) {
      if (ForceUpdate && Phase == AttributorPhase::UPDATE)
        updateAA(*AAPtr);
      return AAPtr;
    }

    bool ShouldUpdateAA;
    if (!shouldInitialize<AAType>(IRP, ShouldUpdateAA))
      return nullptr;

    if (!DebugCounter::shouldExecute(NumAbstractAttributes))
      return nullptr;

    // No matching attribute found, create one.
    // Use the static create method.
    auto &AA = AAType::createForPosition(IRP, *this);

    // Always register a new attribute to make sure we clean up the allocated
    // memory properly.
    registerAA(AA);

    // If we are currenty seeding attributes, enforce seeding rules.
    if (Phase == AttributorPhase::SEEDING && !shouldSeedAttribute(AA)) {
      AA.getState().indicatePessimisticFixpoint();
      return &AA;
    }

    // Bootstrap the new attribute with an initial update to propagate
    // information, e.g., function -> call site.
    {
      TimeTraceScope TimeScope("initialize", [&]() {
        return AA.getName().str() +
               std::to_string(AA.getIRPosition().getPositionKind());
      });
      ++InitializationChainLength;
      AA.initialize(*this);
      --InitializationChainLength;
    }

    if (!ShouldUpdateAA) {
      AA.getState().indicatePessimisticFixpoint();
      return &AA;
    }

    // Allow seeded attributes to declare dependencies.
    // Remember the seeding state.
    if (UpdateAfterInit) {
      AttributorPhase OldPhase = Phase;
      Phase = AttributorPhase::UPDATE;

      updateAA(AA);

      Phase = OldPhase;
    }

    if (QueryingAA && AA.getState().isValidState())
      recordDependence(AA, const_cast<AbstractAttribute &>(*QueryingAA),
                       DepClass);
    return &AA;
  }

  template <typename AAType>
  /// Lookup or create an abstract attribute of type AAType at \p IRP.
  ///
  /// \return A reference to the requested object.
  ///
  /// \param IRP The IR position to look up or create.
  const AAType *getOrCreateAAFor(const IRPosition &IRP) {
    return getOrCreateAAFor<AAType>(IRP, /* QueryingAA */ nullptr,
                                    DepClassTy::NONE);
  }

  /// Return the attribute of \p AAType for \p IRP if existing and valid.
  ///
  /// This also allows non-AA users to perform lookup.
  ///
  /// \return The attribute of \p AAType for \p IRP if existing and valid.
  ///
  /// \param IRP The IR position to look up.
  /// \param QueryingAA The abstract attribute issuing the query, or null.
  /// \param DepClass The dependence class to record for the query.
  /// \param AllowInvalidState If true, return attributes in an invalid state.
  template <typename AAType>
  AAType *lookupAAFor(const IRPosition &IRP,
                      const AbstractAttribute *QueryingAA = nullptr,
                      DepClassTy DepClass = DepClassTy::OPTIONAL,
                      bool AllowInvalidState = false) {
    static_assert(std::is_base_of<AbstractAttribute, AAType>::value,
                  "Cannot query an attribute with a type not derived from "
                  "'AbstractAttribute'!");
    // Lookup the abstract attribute of type AAType. If found, return it after
    // registering a dependence of QueryingAA on the one returned attribute.
    AbstractAttribute *AAPtr = AAMap.lookup({&AAType::ID, IRP});
    if (!AAPtr)
      return nullptr;

    AAType *AA = static_cast<AAType *>(AAPtr);

    // Do not register a dependence on an attribute with an invalid state.
    if (DepClass != DepClassTy::NONE && QueryingAA &&
        AA->getState().isValidState())
      recordDependence(*AA, const_cast<AbstractAttribute &>(*QueryingAA),
                       DepClass);

    // Return nullptr if this attribute has an invalid state.
    if (!AllowInvalidState && !AA->getState().isValidState())
      return nullptr;
    return AA;
  }

  /// Allows a query AA to request an update if a new query was received.
  ///
  /// \param AA The abstract attribute to inspect.
  LLVM_ABI void registerForUpdate(AbstractAttribute &AA);

  /// Explicitly record a dependence from \p FromAA to \p ToAA, that is if
  /// \p FromAA changes \p ToAA should be updated as well.
  ///
  /// This method should be used in conjunction with the `getAAFor` method and
  /// with the DepClass enum passed to the method set to None. This can
  /// be beneficial to avoid false dependences but it requires the users of
  /// `getAAFor` to explicitly record true dependences through this method.
  /// The \p DepClass flag indicates if the dependence is striclty necessary.
  /// That means for required dependences, if \p FromAA changes to an invalid
  /// state, \p ToAA can be moved to a pessimistic fixpoint because it required
  /// information from \p FromAA but none are available anymore.
  ///
  /// \param FromAA The dependence source abstract attribute.
  /// \param ToAA The dependence target abstract attribute.
  /// \param DepClass The dependence class to record for the query.
  LLVM_ABI void recordDependence(const AbstractAttribute &FromAA,
                                 const AbstractAttribute &ToAA,
                                 DepClassTy DepClass);

  /// Introduce a new abstract attribute into the fixpoint analysis.
  ///
  /// Note that ownership of the attribute is given to the Attributor. It will
  /// invoke delete for the Attributor on destruction of the Attributor.
  ///
  /// Attributes are identified by their IR position (AAType::getIRPosition())
  /// and the address of their static member (see AAType::ID).
  ///
  /// \return A reference to the requested object.
  ///
  /// \param AA The abstract attribute to register.
  template <typename AAType> AAType &registerAA(AAType &AA) {
    static_assert(std::is_base_of<AbstractAttribute, AAType>::value,
                  "Cannot register an attribute with a type not derived from "
                  "'AbstractAttribute'!");
    // Put the attribute in the lookup map structure and the container we use to
    // keep track of all attributes.
    const IRPosition &IRP = AA.getIRPosition();
    AbstractAttribute *&AAPtr = AAMap[{&AAType::ID, IRP}];

    assert(!AAPtr && "Attribute already in map!");
    AAPtr = &AA;

    // Register AA with the synthetic root only before the manifest stage.
    if (Phase == AttributorPhase::SEEDING || Phase == AttributorPhase::UPDATE)
      DG.SyntheticRoot.Deps.insert(
          AADepGraphNode::DepTy(&AA, unsigned(DepClassTy::REQUIRED)));

    return AA;
  }

  /// Return the internal information cache.
  ///
  /// \return The internal information cache.
  InformationCache &getInfoCache() { return InfoCache; }

  /// Return the module.
  ///
  /// \return The module.
  const Module &getModule() { return InfoCache.getModule(); }

  /// Return true if this is a module pass, false otherwise.
  ///
  /// \return True if this is a module pass, false otherwise.
  bool isModulePass() const { return Configuration.IsModulePass; }

  /// Return true if we should specialize the call site \b CB for the potential
  /// callee \p Fn.
  ///
  /// \return True if we should specialize the call site \b CB for the potential callee \p Fn.
  ///
  /// \param AA The abstract attribute to inspect.
  /// \param CB The call base of interest.
  /// \param Callee The callee function.
  /// \param NumAssumedCallees The number of assumed callees.
  bool shouldSpecializeCallSiteForCallee(const AbstractAttribute &AA,
                                         CallBase &CB, Function &Callee,
                                         unsigned NumAssumedCallees) {
    return Configuration.IndirectCalleeSpecializationCallback
               ? Configuration.IndirectCalleeSpecializationCallback(
                     *this, AA, CB, Callee, NumAssumedCallees)
               : true;
  }

  /// Return true if the module contains the whole world, thus, no outside
  /// functions exist.
  ///
  /// \return True if the module contains the whole world, thus, no outside functions exist.
  LLVM_ABI bool isClosedWorldModule() const;

  /// Return true if we derive attributes for \p Fn.
  ///
  /// \return True if we derive attributes for \p Fn.
  ///
  /// \param Fn The function of interest.
  bool isRunOn(Function &Fn) const { return isRunOn(&Fn); }
  /// Return true if we derive attributes for \p Fn.
  ///
  /// \return True if we derive attributes for \p Fn.
  ///
  /// \param Fn The function of interest, or null.
  bool isRunOn(Function *Fn) const {
    return Functions.empty() || Functions.count(Fn);
  }

  /// Return true if the abstract attribute should be updated.
  ///
  /// \return True if the abstract attribute should be updated.
  ///
  /// \param IRP The IR position of the abstract attribute.
  template <typename AAType> bool shouldUpdateAA(const IRPosition &IRP) {
    // If this is queried in the manifest stage, we force the AA to indicate
    // pessimistic fixpoint immediately.
    if (Phase == AttributorPhase::MANIFEST || Phase == AttributorPhase::CLEANUP)
      return false;

    Function *AssociatedFn = IRP.getAssociatedFunction();

    if (IRP.isAnyCallSitePosition()) {
      // Check if we require a callee but there is none.
      if (!AssociatedFn && AAType::requiresCalleeForCallBase())
        return false;

      // Check if we require non-asm but it is inline asm.
      if (AAType::requiresNonAsmForCallBase() &&
          cast<CallBase>(IRP.getAnchorValue()).isInlineAsm())
        return false;
    }

    // Check if we require a calles but we can't see all.
    if (AAType::requiresCallersForArgOrFunction())
      if (IRP.getPositionKind() == IRPosition::IRP_FUNCTION ||
          IRP.getPositionKind() == IRPosition::IRP_ARGUMENT)
        if (!AssociatedFn->hasLocalLinkage())
          return false;

    if (!AAType::isValidIRPositionForUpdate(*this, IRP))
      return false;

    // We update only AAs associated with functions in the Functions set or
    // call sites of them.
    return (!AssociatedFn || isModulePass() || isRunOn(AssociatedFn) ||
            isRunOn(IRP.getAnchorScope()));
  }

  /// Return true if the abstract attribute should be initialized.
  ///
  /// \return True if the abstract attribute should be initialized.
  ///
  /// \param IRP The IR position of interest.
  /// \param ShouldUpdateAA Set to true if the attribute should also be updated.
  template <typename AAType>
  bool shouldInitialize(const IRPosition &IRP, bool &ShouldUpdateAA) {
    if (!AAType::isValidIRPositionForInit(*this, IRP))
      return false;

    if (Configuration.Allowed && !Configuration.Allowed->count(&AAType::ID))
      return false;

    // For now we skip anything in naked and optnone functions.
    const Function *AnchorFn = IRP.getAnchorScope();
    if (AnchorFn && (AnchorFn->hasFnAttribute(Attribute::Naked) ||
                     AnchorFn->hasFnAttribute(Attribute::OptimizeNone)))
      return false;

    // Avoid too many nested initializations to prevent a stack overflow.
    if (InitializationChainLength > MaxInitializationChainLength)
      return false;

    ShouldUpdateAA = shouldUpdateAA<AAType>(IRP);

    return !AAType::hasTrivialInitializer() || ShouldUpdateAA;
  }

  /// Determine opportunities to derive 'default' attributes in \p F and create
  /// abstract attribute objects for them.
  ///
  /// \param F The function that is checked for attribute opportunities.
  ///
  /// Note that abstract attribute instances are generally created even if the
  /// IR already contains the information they would deduce. The most important
  /// reason for this is the single interface, the one of the abstract attribute
  /// instance, which can be queried without the need to look at the IR in
  /// various places.
  LLVM_ABI void identifyDefaultAbstractAttributes(Function &F);

  /// Determine whether the function \p F is IPO amendable
  ///
  /// If a function is exactly defined or it has alwaysinline attribute
  /// and is viable to be inlined, we say it is IPO amendable
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param F The function of interest.
  bool isFunctionIPOAmendable(const Function &F) {
    return F.hasExactDefinition() || InfoCache.InlineableFunctions.count(&F) ||
           (Configuration.IPOAmendableCB && Configuration.IPOAmendableCB(F));
  }

  /// Mark the internal function \p F as live.
  ///
  /// This will trigger the identification and initialization of attributes for
  /// \p F.
  ///
  /// \param F The function of interest.
  void markLiveInternalFunction(const Function &F) {
    assert(F.hasLocalLinkage() &&
           "Only local linkage is assumed dead initially.");

    if (Configuration.DefaultInitializeLiveInternals)
      identifyDefaultAbstractAttributes(const_cast<Function &>(F));
    if (Configuration.InitializationCallback)
      Configuration.InitializationCallback(*this, F);
  }

  /// Record that \p U is to be replaces with \p NV after information was
  /// manifested. This also triggers deletion of trivially dead istructions.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param U The use of interest.
  /// \param NV The new value replacing the old one.
  bool changeUseAfterManifest(Use &U, Value &NV) {
    Value *&V = ToBeChangedUses[&U];
    if (V && (V->stripPointerCasts() == NV.stripPointerCasts() ||
              isa_and_nonnull<UndefValue>(V)))
      return false;
    assert((!V || V == &NV || isa<UndefValue>(NV)) &&
           "Use was registered twice for replacement with different values!");
    V = &NV;
    return true;
  }

  /// Replace all uses associated with \p IRP with \p NV after manifestation.
  ///
  /// Return true if there is any change. The flag \p ChangeDroppable indicates
  /// if droppable uses should be changed too.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param IRP The IR position of interest.
  /// \param NV The new value replacing the old one.
  /// \param ChangeDroppable If true, also change droppable uses.
  bool changeAfterManifest(const IRPosition IRP, Value &NV,
                           bool ChangeDroppable = true) {
    if (IRP.getPositionKind() == IRPosition::IRP_CALL_SITE_ARGUMENT) {
      auto *CB = cast<CallBase>(IRP.getCtxI());
      return changeUseAfterManifest(
          CB->getArgOperandUse(IRP.getCallSiteArgNo()), NV);
    }
    Value &V = IRP.getAssociatedValue();
    auto &Entry = ToBeChangedValues[&V];
    Value *CurNV = get<0>(Entry);
    if (CurNV && (CurNV->stripPointerCasts() == NV.stripPointerCasts() ||
                  isa<UndefValue>(CurNV)))
      return false;
    assert((!CurNV || CurNV == &NV || isa<UndefValue>(NV)) &&
           "Value replacement was registered twice with different values!");
    Entry = {&NV, ChangeDroppable};
    return true;
  }

  /// Record that \p I is to be replaced with `unreachable` after information
  /// was manifested.
  ///
  /// \param I The instruction of interest.
  void changeToUnreachableAfterManifest(Instruction *I) {
    ToBeChangedToUnreachableInsts.insert(I);
  }

  /// Record that \p II has at least one dead successor block. This information
  /// is used, e.g., to replace \p II with a call, after information was
  /// manifested.
  ///
  /// \param II The invoke instruction with a dead successor.
  void registerInvokeWithDeadSuccessor(InvokeInst &II) {
    InvokeWithDeadSuccessor.insert(&II);
  }

  /// Record that \p I is deleted after information was manifested. This also
  /// triggers deletion of trivially dead istructions.
  ///
  /// \param I The instruction of interest.
  void deleteAfterManifest(Instruction &I) { ToBeDeletedInsts.insert(&I); }

  /// Record that \p BB is deleted after information was manifested. This also
  /// triggers deletion of trivially dead istructions.
  ///
  /// \param BB The basic block of interest.
  void deleteAfterManifest(BasicBlock &BB) { ToBeDeletedBlocks.insert(&BB); }

  /// Register a basic block added during manifestation.
  ///
  /// Added basic blocks are preserved in the IR.
  ///
  /// \param BB The basic block added during manifestation.
  void registerManifestAddedBasicBlock(BasicBlock &BB) {
    ManifestAddedBlocks.insert(&BB);
  }

  /// Record that \p F is deleted after information was manifested.
  ///
  /// \param F The function of interest.
  void deleteAfterManifest(Function &F) {
    if (Configuration.DeleteFns)
      ToBeDeletedFunctions.insert(&F);
  }

  /// Return the attributes of kind \p AK existing in the IR as operand bundles
  /// of an llvm.assume.
  ///
  /// \return The attributes of kind \p AK existing in the IR as operand bundles of an llvm.assume.
  ///
  /// \param IRP The IR position of interest.
  /// \param AK The attribute kind to query.
  /// \param Attrs The vector receiving deduced attributes.
  LLVM_ABI bool getAttrsFromAssumes(const IRPosition &IRP,
                                    Attribute::AttrKind AK,
                                    SmallVectorImpl<Attribute> &Attrs);

  /// Return true if any kind in \p AKs affects position \p IRP.
  ///
  /// See also getAttrs(...).
  ///
  /// \return True if any kind in \p AKs affects position \p IRP.
  ///
  /// \param IRP The IR position of interest.
  /// \param AKs The attribute kinds to query.
  /// \param IgnoreSubsumingPositions If true, ignore subsuming IR positions.
  /// \param ImpliedAttributeKind Optional IR attribute kind implied by a hit.
  LLVM_ABI bool
  hasAttr(const IRPosition &IRP, ArrayRef<Attribute::AttrKind> AKs,
          bool IgnoreSubsumingPositions = false,
          Attribute::AttrKind ImpliedAttributeKind = Attribute::None);

  /// Collect attributes of kinds \p AKs that affect position \p IRP.
  ///
  /// While each position can only have a single attribute of any kind in
  /// \p AKs, there are "subsuming" positions that could have an attribute as
  /// well. This method returns all attributes found in \p Attrs.
  ///
  /// \param IRP The IR position of interest.
  /// \param AKs The attribute kinds to query.
  /// \param Attrs The vector receiving matching attributes.
  /// \param IgnoreSubsumingPositions If true, ignore subsuming IR positions.
  LLVM_ABI void getAttrs(const IRPosition &IRP,
                         ArrayRef<Attribute::AttrKind> AKs,
                         SmallVectorImpl<Attribute> &Attrs,
                         bool IgnoreSubsumingPositions = false);

  /// Remove all \p AttrKinds attached to \p IRP.
  ///
  /// \return Whether the state or IR changed.
  ///
  /// \param IRP The IR position of interest.
  /// \param AttrKinds The attribute kinds to remove.
  LLVM_ABI ChangeStatus removeAttrs(const IRPosition &IRP,
                                    ArrayRef<Attribute::AttrKind> AttrKinds);
  /// Remove all named attributes in \p Attrs attached to \p IRP.
  ///
  /// \return The string value.
  ///
  /// \param IRP The IR position of interest.
  /// \param Attrs The attribute names to remove.
  LLVM_ABI ChangeStatus removeAttrs(const IRPosition &IRP,
                                    ArrayRef<StringRef> Attrs);

  /// Attach \p DeducedAttrs to \p IRP, if \p ForceReplace is set we do this
  /// even if the same attribute kind was already present.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param IRP The IR position of interest.
  /// \param DeducedAttrs The attributes to manifest.
  /// \param ForceReplace If true, replace existing attributes.
  LLVM_ABI ChangeStatus manifestAttrs(const IRPosition &IRP,
                                      ArrayRef<Attribute> DeducedAttrs,
                                      bool ForceReplace = false);

private:
  /// Helper to check \p Attrs for \p AK, if not found, check if \p
  /// AAType::isImpliedByIR is true, and if not, create AAType for \p IRP.
  /// If \p SkipHasAttrCheck is true, don't check whether the attribute is set
  /// first. This should be used if only some values of a complex IR attribute
  /// imply the AAType.
  template <Attribute::AttrKind AK, typename AAType>
  void checkAndQueryIRAttr(const IRPosition &IRP, AttributeSet Attrs,
                           bool SkipHasAttrCheck = false);

  /// Helper to apply \p CB on all attributes of type \p AttrDescs of \p IRP.
  ///
  /// \return True if the condition described by this query holds.
  template <typename DescTy>
  ChangeStatus updateAttrMap(const IRPosition &IRP, ArrayRef<DescTy> AttrDescs,
                             function_ref<bool(const DescTy &, AttributeSet,
                                               AttributeMask &, AttrBuilder &)>
                                 CB);

  /// Mapping from functions/call sites to their attributes.
  DenseMap<Value *, AttributeList> AttrsMap;

public:
  /// If \p IRP is assumed to be a constant, return it, if it is unclear yet,
  /// return std::nullopt, otherwise return `nullptr`.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param IRP The IR position of interest.
  /// \param AA The abstract attribute issuing the query.
  /// \param UsedAssumedInformation Set if assumed information was used.
  LLVM_ABI std::optional<Constant *>
  getAssumedConstant(const IRPosition &IRP, const AbstractAttribute &AA,
                     bool &UsedAssumedInformation);
  /// Overload querying a value rather than an IR position.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param V The value of interest.
  /// \param AA The abstract attribute issuing the query.
  /// \param UsedAssumedInformation Set if assumed information was used.
  std::optional<Constant *> getAssumedConstant(const Value &V,
                                               const AbstractAttribute &AA,
                                               bool &UsedAssumedInformation) {
    return getAssumedConstant(IRPosition::value(V), AA, UsedAssumedInformation);
  }

  /// Return the assumed simplified value at the given position.
  ///
  /// If the value is unclear yet, return std::nullopt; otherwise return
  /// nullptr when it cannot be simplified.
  ///
  /// \return The assumed simplified value at the given position.
  ///
  /// \param IRP The IR position of interest.
  /// \param AA The abstract attribute issuing the query.
  /// \param UsedAssumedInformation Set if assumed information was used.
  /// \param S The value scope for the query.
  std::optional<Value *> getAssumedSimplified(const IRPosition &IRP,
                                              const AbstractAttribute &AA,
                                              bool &UsedAssumedInformation,
                                              AA::ValueScope S) {
    return getAssumedSimplified(IRP, &AA, UsedAssumedInformation, S);
  }
  /// Overload querying a value rather than an IR position.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param V The value of interest.
  /// \param AA The abstract attribute issuing the query.
  /// \param UsedAssumedInformation Set if assumed information was used.
  /// \param S The value scope for the query.
  std::optional<Value *> getAssumedSimplified(const Value &V,
                                              const AbstractAttribute &AA,
                                              bool &UsedAssumedInformation,
                                              AA::ValueScope S) {
    return getAssumedSimplified(IRPosition::value(V), AA,
                                UsedAssumedInformation, S);
  }

  /// Return the assumed simplified value at \p V without requiring a querying AA.
  ///
  /// If \p V is assumed simplified, return it; if it is unclear yet, return
  /// std::nullopt; otherwise return nullptr. Same as the public version except
  /// that it can be used without recording dependences on any \p AA.
  ///
  /// \return The assumed simplified value at \p V without requiring a querying AA.
  ///
  /// \param V The IR position to simplify.
  /// \param AA The abstract attribute issuing the query, or null.
  /// \param UsedAssumedInformation Set if assumed information was used.
  /// \param S The value scope for the query.
  LLVM_ABI std::optional<Value *>
  getAssumedSimplified(const IRPosition &V, const AbstractAttribute *AA,
                       bool &UsedAssumedInformation, AA::ValueScope S);

  /// Try to simplify \p IRP and in the scope \p S. If successful, true is
  /// returned and all potential values \p IRP can take are put into \p Values.
  /// If the result in \p Values contains select or PHI instructions it means
  /// those could not be simplified to a single value. Recursive calls with
  /// these instructions will yield their respective potential values. If false
  /// is returned no other information is valid.
  LLVM_ABI bool
  /// Try to simplify \p IRP in scope \p S into \p Values.
  ///
  /// If successful, true is returned and all potential values \p IRP can take
  /// are put into \p Values. Select or PHI results may allow further
  /// simplification.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param IRP The IR position to simplify.
  /// \param AA The abstract attribute issuing the query.
  /// \param Values The collection receiving potential values.
  /// \param S The value scope for the query.
  /// \param UsedAssumedInformation Set if assumed information was used.
  /// \param RecurseForSelectAndPHI If true, recurse through select and PHI.
  getAssumedSimplifiedValues(const IRPosition &IRP, const AbstractAttribute *AA,
                             SmallVectorImpl<AA::ValueAndContext> &Values,
                             AA::ValueScope S, bool &UsedAssumedInformation,
                             bool RecurseForSelectAndPHI = true);

  /// Callback type for value simplification registered by abstract attributes.
  using SimplifictionCallbackTy = std::function<std::optional<Value *>(
      const IRPosition &, const AbstractAttribute *, bool &)>;
  /// Register \p CB as a simplification callback.
  ///
  /// `Attributor::getAssumedSimplified` will use these callbacks before
  /// it asks `AAValueSimplify`. Call this before
  /// `identifyDefaultAbstractAttributes`, if that method is used.
  ///
  /// \param IRP The IR position receiving the callback.
  /// \param CB The simplification callback to register.
  void registerSimplificationCallback(const IRPosition &IRP,
                                      const SimplifictionCallbackTy &CB) {
    SimplificationCallbacks[IRP].emplace_back(CB);
  }

  /// Return true if there is a simplification callback for \p IRP.
  ///
  /// \return True if there is a simplification callback for \p IRP.
  ///
  /// \param IRP The IR position of interest.
  bool hasSimplificationCallback(const IRPosition &IRP) {
    return SimplificationCallbacks.count(IRP);
  }

  /// Register \p CB as a simplification callback.
  /// Similar to \p registerSimplificationCallback, the call back will be called
  /// first when we simplify a global variable \p GV.
  using GlobalVariableSimplifictionCallbackTy =
      std::function<std::optional<Constant *>(
          const GlobalVariable &, const AbstractAttribute *, bool &)>;
  /// Register a simplification callback for a global variable.
  ///
  /// \param GV The global variable receiving the callback.
  /// \param CB The simplification callback to register.
  void registerGlobalVariableSimplificationCallback(
      const GlobalVariable &GV,
      const GlobalVariableSimplifictionCallbackTy &CB) {
    GlobalVariableSimplificationCallbacks[&GV].emplace_back(CB);
  }

  /// Return true if there is a simplification callback for \p GV.
  ///
  /// \return True if there is a simplification callback for \p GV.
  ///
  /// \param GV The global variable of interest.
  bool hasGlobalVariableSimplificationCallback(const GlobalVariable &GV) {
    return GlobalVariableSimplificationCallbacks.count(&GV);
  }

  /// Return an assumed initializer for \p GV from registered callbacks.
  ///
  /// Return std::nullopt if there is no callback registered for \p GV or the
  /// callback is still not sure if \p GV can be simplified. Return nullptr if
  /// \p GV can't be simplified.
  ///
  /// \return An assumed initializer for \p GV from registered callbacks.
  ///
  /// \param GV The global variable of interest.
  /// \param AA The abstract attribute issuing the query, or null.
  /// \param UsedAssumedInformation Set if assumed information was used.
  std::optional<Constant *>
  getAssumedInitializerFromCallBack(const GlobalVariable &GV,
                                    const AbstractAttribute *AA,
                                    bool &UsedAssumedInformation) {
    assert(GlobalVariableSimplificationCallbacks.contains(&GV));
    for (auto &CB : GlobalVariableSimplificationCallbacks.lookup(&GV)) {
      auto SimplifiedGV = CB(GV, AA, UsedAssumedInformation);
      // For now we assume the call back will not return a std::nullopt.
      assert(SimplifiedGV.has_value() && "SimplifiedGV has not value");
      return *SimplifiedGV;
    }
    llvm_unreachable("there must be a callback registered");
  }

  /// Callback type for virtual-use notifications.
  using VirtualUseCallbackTy =
      std::function<bool(Attributor &, const AbstractAttribute *)>;
  /// Register a callback for virtual uses of a value.
  ///
  /// \param V The value whose virtual uses are tracked.
  /// \param CB The callback invoked for each virtual use.
  void registerVirtualUseCallback(const Value &V,
                                  const VirtualUseCallbackTy &CB) {
    VirtualUseCallbacks[&V].emplace_back(CB);
  }

private:
  /// The vector with all simplification callbacks registered by outside AAs.
  DenseMap<IRPosition, SmallVector<SimplifictionCallbackTy, 1>>
      SimplificationCallbacks;

  /// The vector with all simplification callbacks for global variables
  /// registered by outside AAs.
  DenseMap<const GlobalVariable *,
           SmallVector<GlobalVariableSimplifictionCallbackTy, 1>>
      GlobalVariableSimplificationCallbacks;

  DenseMap<const Value *, SmallVector<VirtualUseCallbackTy, 1>>
      VirtualUseCallbacks;

public:
  /// Translate \p V from the callee context into the call site context.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param V The callee value to translate, if known.
  /// \param CB The call site providing the callee context.
  /// \param AA The abstract attribute issuing the query.
  /// \param UsedAssumedInformation Set if assumed information was used.
  LLVM_ABI std::optional<Value *>
  translateArgumentToCallSiteContent(std::optional<Value *> V, CallBase &CB,
                                     const AbstractAttribute &AA,
                                     bool &UsedAssumedInformation);

  /// Return true if \p AA (or its context instruction) is assumed dead.
  ///
  /// If \p LivenessAA is not provided it is queried.
  ///
  /// \return True if \p AA (or its context instruction) is assumed dead.
  ///
  /// \param AA The abstract attribute to inspect.
  /// \param LivenessAA Optional liveness abstract attribute.
  /// \param UsedAssumedInformation Set if assumed information was used.
  /// \param CheckBBLivenessOnly If true, only check basic-block liveness.
  /// \param DepClass The dependence class to record for the query.
  LLVM_ABI bool isAssumedDead(const AbstractAttribute &AA,
                              const AAIsDead *LivenessAA,
                              bool &UsedAssumedInformation,
                              bool CheckBBLivenessOnly = false,
                              DepClassTy DepClass = DepClassTy::OPTIONAL);

  /// Return true if \p I is assumed dead.
  ///
  /// If \p LivenessAA is not provided it is queried.
  ///
  /// \return True if \p I is assumed dead.
  ///
  /// \param I The instruction of interest.
  /// \param QueryingAA The abstract attribute issuing the query.
  /// \param LivenessAA Optional liveness abstract attribute.
  /// \param UsedAssumedInformation Set if assumed information was used.
  /// \param CheckBBLivenessOnly If true, only check basic-block liveness.
  /// \param DepClass The dependence class to record for the query.
  /// \param CheckForDeadStore If true, treat dead stores as dead instructions.
  LLVM_ABI bool isAssumedDead(const Instruction &I,
                              const AbstractAttribute *QueryingAA,
                              const AAIsDead *LivenessAA,
                              bool &UsedAssumedInformation,
                              bool CheckBBLivenessOnly = false,
                              DepClassTy DepClass = DepClassTy::OPTIONAL,
                              bool CheckForDeadStore = false);

  /// Return true if \p U is assumed dead.
  ///
  /// If \p FnLivenessAA is not provided it is queried.
  ///
  /// \return True if \p U is assumed dead.
  ///
  /// \param U The use of interest.
  /// \param QueryingAA The abstract attribute issuing the query.
  /// \param FnLivenessAA Optional function-liveness abstract attribute.
  /// \param UsedAssumedInformation Set if assumed information was used.
  /// \param CheckBBLivenessOnly If true, only check basic-block liveness.
  /// \param DepClass The dependence class to record for the query.
  LLVM_ABI bool isAssumedDead(const Use &U, const AbstractAttribute *QueryingAA,
                              const AAIsDead *FnLivenessAA,
                              bool &UsedAssumedInformation,
                              bool CheckBBLivenessOnly = false,
                              DepClassTy DepClass = DepClassTy::OPTIONAL);

  /// Return true if \p IRP is assumed dead.
  ///
  /// If \p FnLivenessAA is not provided it is queried.
  ///
  /// \return True if \p IRP is assumed dead.
  ///
  /// \param IRP The IR position of interest.
  /// \param QueryingAA The abstract attribute issuing the query.
  /// \param FnLivenessAA Optional function-liveness abstract attribute.
  /// \param UsedAssumedInformation Set if assumed information was used.
  /// \param CheckBBLivenessOnly If true, only check basic-block liveness.
  /// \param DepClass The dependence class to record for the query.
  LLVM_ABI bool isAssumedDead(const IRPosition &IRP,
                              const AbstractAttribute *QueryingAA,
                              const AAIsDead *FnLivenessAA,
                              bool &UsedAssumedInformation,
                              bool CheckBBLivenessOnly = false,
                              DepClassTy DepClass = DepClassTy::OPTIONAL);

  /// Return true if \p BB is assumed dead.
  ///
  /// If \p LivenessAA is not provided it is queried.
  ///
  /// \return True if \p BB is assumed dead.
  ///
  /// \param BB The basic block of interest.
  /// \param QueryingAA The abstract attribute issuing the query.
  /// \param FnLivenessAA Optional function-liveness abstract attribute.
  /// \param DepClass The dependence class to record for the query.
  LLVM_ABI bool isAssumedDead(const BasicBlock &BB,
                              const AbstractAttribute *QueryingAA,
                              const AAIsDead *FnLivenessAA,
                              DepClassTy DepClass = DepClassTy::OPTIONAL);

  /// Check \p Pred on all potential Callees of \p CB.
  ///
  /// This method will evaluate \p Pred with all potential callees of \p CB as
  /// input and return true if \p Pred does. If some callees might be unknown
  /// this function will return false.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param Pred The callback predicate applied to matching items.
  /// \param QueryingAA The abstract attribute issuing the query.
  /// \param CB The call base of interest.
  LLVM_ABI bool checkForAllCallees(
      function_ref<bool(ArrayRef<const Function *> Callees)> Pred,
      const AbstractAttribute &QueryingAA, const CallBase &CB);

  /// Check \p Pred on all (transitive) uses of \p V.
  ///
  /// This method will evaluate \p Pred on all (transitive) uses of the
  /// associated value and return true if \p Pred holds every time.
  /// If uses are skipped in favor of equivalent ones, e.g., if we look through
  /// memory, the \p EquivalentUseCB will be used to give the caller an idea
  /// what original used was replaced by a new one (or new ones). The visit is
  /// cut short if \p EquivalentUseCB returns false and the function will return
  /// false as well.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param Pred The callback predicate applied to matching items.
  /// \param QueryingAA The abstract attribute issuing the query.
  /// \param V The value of interest.
  /// \param CheckBBLivenessOnly If true, only check basic-block liveness.
  /// \param LivenessDepClass Dependence class for liveness queries.
  /// \param IgnoreDroppableUses If true, ignore droppable uses.
  /// \param EquivalentUseCB Optional callback for equivalent uses.
  LLVM_ABI bool checkForAllUses(
      function_ref<bool(const Use &, bool &)> Pred,
      const AbstractAttribute &QueryingAA, const Value &V,
      bool CheckBBLivenessOnly = false,
      DepClassTy LivenessDepClass = DepClassTy::OPTIONAL,
      bool IgnoreDroppableUses = true,
      function_ref<bool(const Use &OldU, const Use &NewU)> EquivalentUseCB =
          nullptr);

  /// Emit a remark generically.
  ///
  /// This template function can be used to generically emit a remark. The
  /// RemarkKind should be one of the following:
  ///   - OptimizationRemark to indicate a successful optimization attempt
  ///   - OptimizationRemarkMissed to report a failed optimization attempt
  ///   - OptimizationRemarkAnalysis to provide additional information about an
  ///     optimization attempt
  ///
  /// The remark is built using a callback function \p RemarkCB that takes a
  /// RemarkKind as input and returns a RemarkKind.
  ///
  /// \param I The instruction the remark refers to.
  /// \param RemarkName The remark identifier.
  /// \param RemarkCB Callback building the remark object.
  template <typename RemarkKind, typename RemarkCallBack>
  void emitRemark(Instruction *I, StringRef RemarkName,
                  RemarkCallBack &&RemarkCB) const {
    if (!Configuration.OREGetter)
      return;

    Function *F = I->getFunction();
    auto &ORE = Configuration.OREGetter(F);

    if (RemarkName.starts_with("OMP"))
      ORE.emit([&]() {
        return RemarkCB(RemarkKind(Configuration.PassName, RemarkName, I))
               << " [" << RemarkName << "]";
      });
    else
      ORE.emit([&]() {
        return RemarkCB(RemarkKind(Configuration.PassName, RemarkName, I));
      });
  }

  /// Emit a remark on a function.
  ///
  /// \param F The function the remark refers to.
  /// \param RemarkName The remark identifier.
  /// \param RemarkCB Callback building the remark object.
  template <typename RemarkKind, typename RemarkCallBack>
  void emitRemark(Function *F, StringRef RemarkName,
                  RemarkCallBack &&RemarkCB) const {
    if (!Configuration.OREGetter)
      return;

    auto &ORE = Configuration.OREGetter(F);

    if (RemarkName.starts_with("OMP"))
      ORE.emit([&]() {
        return RemarkCB(RemarkKind(Configuration.PassName, RemarkName, F))
               << " [" << RemarkName << "]";
      });
    else
      ORE.emit([&]() {
        return RemarkCB(RemarkKind(Configuration.PassName, RemarkName, F));
      });
  }

  /// Communication record for function-signature rewrite requests.
  ///
  /// Used between an abstract attribute (AA) that wants to change the signature
  /// of a function and the Attributor which applies the changes. The struct is
  /// partially initialized with the information from the AA (see the
  /// constructor). All other members are provided by the Attributor prior to
  /// invoking any callbacks.
  struct ArgumentReplacementInfo {
    /// Callee repair callback type
    ///
    /// The function repair callback is invoked once to rewire the replacement
    /// arguments in the body of the new function. The argument replacement info
    /// is passed, as build from the registerFunctionSignatureRewrite call, as
    /// well as the replacement function and an iteratore to the first
    /// replacement argument.
    using CalleeRepairCBTy = std::function<void(
        const ArgumentReplacementInfo &, Function &, Function::arg_iterator)>;

    /// Abstract call site (ACS) repair callback type
    ///
    /// The abstract call site repair callback is invoked once on every abstract
    /// call site of the replaced function (\see ReplacedFn). The callback needs
    /// to provide the operands for the call to the new replacement function.
    /// The number and type of the operands appended to the provided vector
    /// (second argument) is defined by the number and types determined through
    /// the replacement type vector (\see ReplacementTypes). The first argument
    /// is the ArgumentReplacementInfo object registered with the Attributor
    /// through the registerFunctionSignatureRewrite call.
    using ACSRepairCBTy =
        std::function<void(const ArgumentReplacementInfo &, AbstractCallSite,
                           SmallVectorImpl<Value *> &)>;

    /// Simple getters, see the corresponding members for details.
    ///{
    ///
    /// \return A reference to the requested object.

    Attributor &getAttributor() const { return A; }
    /// Return the function whose argument is replaced.
    ///
    /// \return The function whose argument is replaced.
    const Function &getReplacedFn() const { return ReplacedFn; }
    /// Return the argument being replaced.
    ///
    /// \return The argument being replaced.
    const Argument &getReplacedArg() const { return ReplacedArg; }
    /// Return the number of replacement arguments.
    ///
    /// \return The number of replacement arguments.
    unsigned getNumReplacementArgs() const { return ReplacementTypes.size(); }
    /// Return the replacement argument types.
    ///
    /// \return The replacement argument types.
    const SmallVectorImpl<Type *> &getReplacementTypes() const {
      return ReplacementTypes;
    }

    ///}

  private:
    /// Constructor that takes the argument to be replaced, the types of
    /// the replacement arguments, as well as callbacks to repair the call sites
    /// and new function after the replacement happened.
    ArgumentReplacementInfo(Attributor &A, Argument &Arg,
                            ArrayRef<Type *> ReplacementTypes,
                            CalleeRepairCBTy &&CalleeRepairCB,
                            ACSRepairCBTy &&ACSRepairCB)
        : A(A), ReplacedFn(*Arg.getParent()), ReplacedArg(Arg),
          ReplacementTypes(ReplacementTypes),
          CalleeRepairCB(std::move(CalleeRepairCB)),
          ACSRepairCB(std::move(ACSRepairCB)) {}

    /// Reference to the attributor to allow access from the callbacks.
    Attributor &A;

    /// The "old" function replaced by ReplacementFn.
    const Function &ReplacedFn;

    /// The "old" argument replaced by new ones defined via ReplacementTypes.
    const Argument &ReplacedArg;

    /// The types of the arguments replacing ReplacedArg.
    const SmallVector<Type *, 8> ReplacementTypes;

    /// Callee repair callback, see CalleeRepairCBTy.
    const CalleeRepairCBTy CalleeRepairCB;

    /// Abstract call site (ACS) repair callback, see ACSRepairCBTy.
    const ACSRepairCBTy ACSRepairCB;

    /// Allow access to the private members from the Attributor.
    friend struct Attributor;
  };

  /// Check if we can rewrite a function signature.
  ///
  /// The argument \p Arg is replaced with new ones defined by the number,
  /// order, and types in \p ReplacementTypes.
  ///
  /// \returns True, if the replacement can be registered, via
  /// registerFunctionSignatureRewrite, false otherwise.
  ///
  /// \param Arg The argument being rewritten or inspected.
  /// \param ReplacementTypes The replacement argument types.
  LLVM_ABI bool
  isValidFunctionSignatureRewrite(Argument &Arg,
                                  ArrayRef<Type *> ReplacementTypes);

  /// Register a rewrite for a function signature.
  ///
  /// The argument \p Arg is replaced with new ones defined by the number,
  /// order, and types in \p ReplacementTypes. The rewiring at the call sites is
  /// done through \p ACSRepairCB and at the callee site through
  /// \p CalleeRepairCB.
  ///
  /// \returns True, if the replacement was registered, false otherwise.
  ///
  /// \param Arg The argument being rewritten or inspected.
  /// \param ReplacementTypes The replacement argument types.
  /// \param CalleeRepairCB Callback to repair the callee after rewrite.
  /// \param ACSRepairCB Callback to repair call sites after rewrite.
  LLVM_ABI bool registerFunctionSignatureRewrite(
      Argument &Arg, ArrayRef<Type *> ReplacementTypes,
      ArgumentReplacementInfo::CalleeRepairCBTy &&CalleeRepairCB,
      ArgumentReplacementInfo::ACSRepairCBTy &&ACSRepairCB);

  /// Check \p Pred on all function call sites.
  ///
  /// This method will evaluate \p Pred on call sites and return
  /// true if \p Pred holds in every call sites. However, this is only possible
  /// all call sites are known, hence the function has internal linkage.
  /// If true is returned, \p UsedAssumedInformation is set if assumed
  /// information was used to skip or simplify potential call sites.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param Pred The callback predicate applied to matching items.
  /// \param QueryingAA The abstract attribute issuing the query.
  /// \param RequireAllCallSites If true, require information for all call sites.
  /// \param UsedAssumedInformation Set if assumed information was used.
  LLVM_ABI bool checkForAllCallSites(function_ref<bool(AbstractCallSite)> Pred,
                                     const AbstractAttribute &QueryingAA,
                                     bool RequireAllCallSites,
                                     bool &UsedAssumedInformation);

  /// Check \p Pred on all call sites of \p Fn.
  ///
  /// This method will evaluate \p Pred on call sites and return
  /// true if \p Pred holds in every call sites. However, this is only possible
  /// all call sites are known, hence the function has internal linkage.
  /// If true is returned, \p UsedAssumedInformation is set if assumed
  /// information was used to skip or simplify potential call sites.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param Pred The callback predicate applied to matching items.
  /// \param Fn The function of interest.
  /// \param RequireAllCallSites If true, require information for all call sites.
  /// \param QueryingAA The abstract attribute issuing the query.
  /// \param UsedAssumedInformation Set if assumed information was used.
  /// \param CheckPotentiallyDead If true, also consider potentially dead code.
  LLVM_ABI bool checkForAllCallSites(function_ref<bool(AbstractCallSite)> Pred,
                                     const Function &Fn,
                                     bool RequireAllCallSites,
                                     const AbstractAttribute *QueryingAA,
                                     bool &UsedAssumedInformation,
                                     bool CheckPotentiallyDead = false);

  /// Check \p Pred on all values potentially returned by the function
  /// associated with \p QueryingAA.
  ///
  /// This is the context insensitive version of the method above.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param Pred The callback predicate applied to each returned value.
  /// \param QueryingAA The abstract attribute issuing the query.
  /// \param S The value scope for the query.
  /// \param RecurseForSelectAndPHI If true, recurse through select and PHI.
  LLVM_ABI bool
  checkForAllReturnedValues(function_ref<bool(Value &)> Pred,
                            const AbstractAttribute &QueryingAA,
                            AA::ValueScope S = AA::ValueScope::Intraprocedural,
                            bool RecurseForSelectAndPHI = true);

  /// Check \p Pred on all instructions in \p Fn with an opcode present in
  /// \p Opcodes.
  ///
  /// This method will evaluate \p Pred on all instructions with an opcode
  /// present in \p Opcode and return true if \p Pred holds on all of them.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param Pred The callback predicate applied to matching items.
  /// \param Fn The function of interest.
  /// \param QueryingAA The abstract attribute issuing the query.
  /// \param Opcodes The instruction opcodes to match.
  /// \param UsedAssumedInformation Set if assumed information was used.
  /// \param CheckBBLivenessOnly If true, only check basic-block liveness.
  /// \param CheckPotentiallyDead If true, also consider potentially dead code.
  LLVM_ABI bool checkForAllInstructions(function_ref<bool(Instruction &)> Pred,
                                        const Function *Fn,
                                        const AbstractAttribute *QueryingAA,
                                        ArrayRef<unsigned> Opcodes,
                                        bool &UsedAssumedInformation,
                                        bool CheckBBLivenessOnly = false,
                                        bool CheckPotentiallyDead = false);

  /// Check \p Pred on all instructions with an opcode present in \p Opcodes.
  ///
  /// This method will evaluate \p Pred on all instructions with an opcode
  /// present in \p Opcode and return true if \p Pred holds on all of them.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param Pred The callback predicate applied to matching items.
  /// \param QueryingAA The abstract attribute issuing the query.
  /// \param Opcodes The instruction opcodes to match.
  /// \param UsedAssumedInformation Set if assumed information was used.
  /// \param CheckBBLivenessOnly If true, only check basic-block liveness.
  /// \param CheckPotentiallyDead If true, also consider potentially dead code.
  LLVM_ABI bool checkForAllInstructions(function_ref<bool(Instruction &)> Pred,
                                        const AbstractAttribute &QueryingAA,
                                        ArrayRef<unsigned> Opcodes,
                                        bool &UsedAssumedInformation,
                                        bool CheckBBLivenessOnly = false,
                                        bool CheckPotentiallyDead = false);

  /// Check \p Pred on all call-like instructions (=CallBased derived).
  ///
  /// See checkForAllCallLikeInstructions(...) for more information.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param Pred The callback predicate applied to matching items.
  /// \param QueryingAA The abstract attribute issuing the query.
  /// \param UsedAssumedInformation Set if assumed information was used.
  /// \param CheckBBLivenessOnly If true, only check basic-block liveness.
  /// \param CheckPotentiallyDead If true, also consider potentially dead code.
  bool checkForAllCallLikeInstructions(function_ref<bool(Instruction &)> Pred,
                                       const AbstractAttribute &QueryingAA,
                                       bool &UsedAssumedInformation,
                                       bool CheckBBLivenessOnly = false,
                                       bool CheckPotentiallyDead = false) {
    return checkForAllInstructions(
        Pred, QueryingAA,
        {(unsigned)Instruction::Invoke, (unsigned)Instruction::CallBr,
         (unsigned)Instruction::Call},
        UsedAssumedInformation, CheckBBLivenessOnly, CheckPotentiallyDead);
  }

  /// Check \p Pred on all Read/Write instructions.
  ///
  /// This method will evaluate \p Pred on all instructions that read or write
  /// to memory present in the information cache and return true if \p Pred
  /// holds on all of them.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param Pred The callback predicate applied to matching instructions.
  /// \param QueryingAA The abstract attribute issuing the query.
  /// \param UsedAssumedInformation Set if assumed information was used.
  LLVM_ABI bool
  checkForAllReadWriteInstructions(function_ref<bool(Instruction &)> Pred,
                                   AbstractAttribute &QueryingAA,
                                   bool &UsedAssumedInformation);

  /// Create a shallow wrapper for \p F such that \p F has internal linkage
  /// afterwards. It also sets the original \p F 's name to anonymous
  ///
  /// A wrapper is a function with the same type (and attributes) as \p F
  /// that will only call \p F and return the result, if any.
  ///
  /// Assuming the declaration of looks like:
  ///   rty F(aty0 arg0, ..., atyN argN);
  ///
  /// The wrapper will then look as follows:
  ///   rty wrapper(aty0 arg0, ..., atyN argN) {
  ///     return F(arg0, ..., argN);
  ///   }
  ///
  ///
  /// \param F The function of interest.
  LLVM_ABI static void createShallowWrapper(Function &F);

  /// Returns true if the function \p F can be internalized. i.e. it has a
  /// compatible linkage.
  ///
  /// \return True if the function \p F can be internalized. i.e. it has a compatible linkage.
  ///
  /// \param F The function of interest.
  LLVM_ABI static bool isInternalizable(Function &F);

  /// Create an internal-linkage copy of \p F and redirect uses to it.
  ///
  /// Only non-locally linked functions that have `linkonce_odr` or `weak_odr`
  /// linkage can be internalized because these linkages guarantee that other
  /// definitions with the same name have the same semantics as this one.
  ///
  /// This will only be run if the `attributor-allow-deep-wrappers` option is
  /// set, or if the function is called with \p Force set to true.
  ///
  /// If the function \p F failed to be internalized the return value will be a
  /// null pointer.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param F The function of interest.
  /// \param Force If true, force internalization.
  LLVM_ABI static Function *internalizeFunction(Function &F,
                                                bool Force = false);

  /// Internalize each function in \p FnSet and record copies in \p FnMap.
  ///
  /// Only non-locally linked functions that have `linkonce_odr` or `weak_odr`
  /// linkage can be internalized because these linkages guarantee that other
  /// definitions with the same name have the same semantics as this one.
  ///
  /// This version will internalize all the functions in the set \p FnSet at
  /// once and then replace the uses. This prevents internalized functions being
  /// called by external functions when there is an internalized version in the
  /// module.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param FnSet The set of functions to internalize.
  /// \param FnMap Mapping from original functions to internalized copies.
  LLVM_ABI static bool
  internalizeFunctions(SmallPtrSetImpl<Function *> &FnSet,
                       DenseMap<Function *, Function *> &FnMap);

  /// Return the data layout associated with the anchor scope.
  ///
  /// \return The data layout associated with the anchor scope.
  const DataLayout &getDataLayout() const { return InfoCache.getDL(); }

  /// The allocator used to allocate memory, e.g. for `AbstractAttribute`s.
  BumpPtrAllocator &Allocator;

  /// Return the set of functions modified by the Attributor.
  ///
  /// \return The set of functions modified by the Attributor.
  const SmallSetVector<Function *, 8> &getModifiedFunctions() {
    return CGModifiedFunctions;
  }

private:
  /// This method will do fixpoint iteration until fixpoint or the
  /// maximum iteration count is reached.
  ///
  /// If the maximum iteration count is reached, This method will
  /// indicate pessimistic fixpoint on attributes that transitively depend
  /// on attributes that were scheduled for an update.
  void runTillFixpoint();

  /// Gets called after scheduling, manifests attributes to the LLVM IR.
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus manifestAttributes();

  /// Gets called after attributes have been manifested, cleans up the IR.
  /// Deletes dead functions, blocks and instructions.
  /// Rewrites function signitures and updates the call graph.
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus cleanupIR();

  /// Identify internal functions that are effectively dead, thus not reachable
  /// from a live entry point. The functions are added to ToBeDeletedFunctions.
  void identifyDeadInternalFunctions();

  /// Run `::update` on \p AA and track the dependences queried while doing so.
  /// Also adjust the state if we know further updates are not necessary.
  ///
  /// \return Whether the state or IR changed.
  LLVM_ABI ChangeStatus updateAA(AbstractAttribute &AA);

  /// Remember the dependences on the top of the dependence stack such that they
  /// may trigger further updates. (\see DependenceStack)
  void rememberDependences();

  /// Determine if CallBase context in \p IRP should be propagated.
  ///
  /// \return True if the condition described by this query holds.
  LLVM_ABI bool shouldPropagateCallBaseContext(const IRPosition &IRP);

  /// Apply all requested function signature rewrites
  /// (\see registerFunctionSignatureRewrite) and return Changed if the module
  /// was altered.
  ChangeStatus
  rewriteFunctionSignatures(SmallSetVector<Function *, 8> &ModifiedFns);

  /// Check if the Attribute \p AA should be seeded.
  /// See getOrCreateAAFor.
  ///
  /// \return True if the Attribute \p AA should be seeded.
  LLVM_ABI bool shouldSeedAttribute(AbstractAttribute &AA);

  /// A nested map to lookup abstract attributes based on the argument position
  /// on the outer level, and the addresses of the static member (AAType::ID) on
  /// the inner level.
  ///{
  using AAMapKeyTy = std::pair<const char *, IRPosition>;
  DenseMap<AAMapKeyTy, AbstractAttribute *> AAMap;
  ///}

  /// Map to remember all requested signature changes (= argument replacements).
  DenseMap<Function *, SmallVector<std::unique_ptr<ArgumentReplacementInfo>, 8>>
      ArgumentReplacementMap;

  /// The set of functions we are deriving attributes for.
  SetVector<Function *> &Functions;

  /// The information cache that holds pre-processed (LLVM-IR) information.
  InformationCache &InfoCache;

  /// Abstract Attribute dependency graph
  AADepGraph DG;

  /// Set of functions for which we modified the content such that it might
  /// impact the call graph.
  SmallSetVector<Function *, 8> CGModifiedFunctions;

  /// Information about a dependence. If FromAA is changed ToAA needs to be
  /// updated as well.
  struct DepInfo {
    const AbstractAttribute *FromAA;
    const AbstractAttribute *ToAA;
    DepClassTy DepClass;
  };

  /// The dependence stack is used to track dependences during an
  /// `AbstractAttribute::update` call. As `AbstractAttribute::update` can be
  /// recursive we might have multiple vectors of dependences in here. The stack
  /// size, should be adjusted according to the expected recursion depth and the
  /// inner dependence vector size to the expected number of dependences per
  /// abstract attribute. Since the inner vectors are actually allocated on the
  /// stack we can be generous with their size.
  using DependenceVector = SmallVector<DepInfo, 8>;
  SmallVector<DependenceVector *, 16> DependenceStack;

  /// A set to remember the functions we already assume to be live and visited.
  DenseSet<const Function *> VisitedFunctions;

  /// Uses we replace with a new value after manifest is done. We will remove
  /// then trivially dead instructions as well.
  SmallMapVector<Use *, Value *, 32> ToBeChangedUses;

  /// Values we replace with a new value after manifest is done. We will remove
  /// then trivially dead instructions as well.
  SmallMapVector<Value *, PointerIntPair<Value *, 1, bool>, 32>
      ToBeChangedValues;

  /// Instructions we replace with `unreachable` insts after manifest is done.
  SmallSetVector<WeakVH, 16> ToBeChangedToUnreachableInsts;

  /// Invoke instructions with at least a single dead successor block.
  SmallSetVector<WeakVH, 16> InvokeWithDeadSuccessor;

  /// A flag that indicates which stage of the process we are in. Initially, the
  /// phase is SEEDING. Phase is changed in `Attributor::run()`
  enum class AttributorPhase {
    SEEDING,
    UPDATE,
    MANIFEST,
    CLEANUP,
  } Phase = AttributorPhase::SEEDING;

  /// The current initialization chain length. Tracked to avoid stack overflows.
  unsigned InitializationChainLength = 0;

  /// Functions, blocks, and instructions we delete after manifest is done.
  ///
  ///{
  SmallPtrSet<BasicBlock *, 8> ManifestAddedBlocks;
  SmallSetVector<Function *, 8> ToBeDeletedFunctions;
  SmallSetVector<BasicBlock *, 8> ToBeDeletedBlocks;
  SmallSetVector<WeakVH, 8> ToBeDeletedInsts;
  ///}

  /// Container with all the query AAs that requested an update via
  /// registerForUpdate.
  SmallSetVector<AbstractAttribute *, 16> QueryAAsAwaitingUpdate;

  /// User provided configuration for this Attributor instance.
  const AttributorConfig Configuration;

  friend AADepGraph;
  friend AttributorCallGraph;
};

/// An interface to query the internal state of an abstract attribute.
///
/// The abstract state is a minimal interface that allows the Attributor to
/// communicate with the abstract attributes about their internal state without
/// enforcing or exposing implementation details, e.g., the (existence of an)
/// underlying lattice.
///
/// It is sufficient to be able to query if a state is (1) valid or invalid, (2)
/// at a fixpoint, and to indicate to the state that (3) an optimistic fixpoint
/// was reached or (4) a pessimistic fixpoint was enforced.
///
/// All methods need to be implemented by the subclass. For the common use case,
/// a single boolean state or a bit-encoded state, the BooleanState and
/// {Inc,Dec,Bit}IntegerState classes are already provided. An abstract
/// attribute can inherit from them to get the abstract state interface and
/// additional methods to directly modify the state based if needed. See the
/// class comments for help.
struct AbstractState {
  /// Virtual destructor.
  virtual ~AbstractState() = default;

  /// Return if this abstract state is in a valid state. If false, no
  /// information provided should be used.
  ///
  /// \return If this abstract state is in a valid state. If false, no information provided should be used.
  virtual bool isValidState() const = 0;

  /// Return if this abstract state is fixed, thus does not need to be updated
  /// if information changes as it cannot change itself.
  ///
  /// \return If this abstract state is fixed, thus does not need to be updated if information changes as it cannot change itself.
  virtual bool isAtFixpoint() const = 0;

  /// Indicate that the abstract state should converge to the optimistic state.
  ///
  /// This will usually make the optimistically assumed state the known to be
  /// true state.
  ///
  /// \returns ChangeStatus::UNCHANGED as the assumed value should not change.
  virtual ChangeStatus indicateOptimisticFixpoint() = 0;

  /// Indicate that the abstract state should converge to the pessimistic state.
  ///
  /// This will usually revert the optimistically assumed state to the known to
  /// be true state.
  ///
  /// \returns ChangeStatus::CHANGED as the assumed value may change.
  virtual ChangeStatus indicatePessimisticFixpoint() = 0;
};

/// Simple state with integers encoding.
///
/// The interface ensures that the assumed bits are always a subset of the known
/// bits. Users can only add known bits and, except through adding known bits,
/// they can only remove assumed bits. This should guarantee monotonicity and
/// thereby the existence of a fixpoint (if used correctly). The fixpoint is
/// reached when the assumed and known state/bits are equal. Users can
/// force/inidicate a fixpoint. If an optimistic one is indicated, the known
/// state will catch up with the assumed one, for a pessimistic fixpoint it is
/// the other way around.
template <typename base_ty, base_ty BestState, base_ty WorstState>
/// Base encoding for integer-like abstract states.
struct IntegerStateBase : public AbstractState {
  /// Underlying integer storage type.
  using base_t = base_ty;

  /// Construct an integer state.
  IntegerStateBase() = default;
  /// Construct an integer state with an initial assumed encoding.
  ///
  /// \param Assumed The initial assumed state encoding.
  IntegerStateBase(base_t Assumed) : Assumed(Assumed) {}

  /// Return the best possible representable state.
  ///
  /// \return The best possible representable state.
  static constexpr base_t getBestState() { return BestState; }
  /// Return the best state, ignoring \p Unused.
  ///
  /// \return The best state, ignoring \p Unused.
  ///
  /// \param Unused Unused; present for API uniformity with other states.
  static constexpr base_t getBestState(const IntegerStateBase &Unused) {
    return getBestState();
  }

  /// Return the worst possible representable state.
  ///
  /// \return The worst possible representable state.
  static constexpr base_t getWorstState() { return WorstState; }
  /// Return the worst state, ignoring \p Unused.
  ///
  /// \return The worst state, ignoring \p Unused.
  ///
  /// \param Unused Unused; present for API uniformity with other states.
  static constexpr base_t getWorstState(const IntegerStateBase &Unused) {
    return getWorstState();
  }

  /// See AbstractState::isValidState()
  /// NOTE: For now we simply pretend that the worst possible state is invalid.
  ///
  /// \return True if the condition described by this query holds.
  bool isValidState() const override { return Assumed != getWorstState(); }

  /// See AbstractState::isAtFixpoint()
  ///
  /// \return True if the condition described by this query holds.
  bool isAtFixpoint() const override { return Assumed == Known; }

  /// See AbstractState::indicateOptimisticFixpoint(...)
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus indicateOptimisticFixpoint() override {
    Known = Assumed;
    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractState::indicatePessimisticFixpoint(...)
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus indicatePessimisticFixpoint() override {
    Assumed = Known;
    return ChangeStatus::CHANGED;
  }

  /// Return the known state encoding
  ///
  /// \return The known state encoding.
  base_t getKnown() const { return Known; }

  /// Return the assumed state encoding.
  ///
  /// \return The assumed state encoding.
  base_t getAssumed() const { return Assumed; }

  /// Return true if this equals \p R.
  ///
  /// \return True if this equals \p R.
  ///
  /// \param R The other operand or state.
  bool operator==(
      const IntegerStateBase<base_t, BestState, WorstState> &R) const {
    return this->getAssumed() == R.getAssumed() &&
           this->getKnown() == R.getKnown();
  }

  /// Return true if this differs from \p R.
  ///
  /// \return True if this differs from \p R.
  ///
  /// \param R The other operand or state.
  bool operator!=(
      const IntegerStateBase<base_t, BestState, WorstState> &R) const {
    return !(*this == R);
  }

  /// Clamp this state with \p R.
  ///
  /// Only information assumed in both states remains assumed afterwards.
  ///
  /// \return The computed result.
  ///
  /// \param R The other operand or state.
  void operator^=(const IntegerStateBase<base_t, BestState, WorstState> &R) {
    handleNewAssumedValue(R.getAssumed());
  }

  /// "Clamp" this state with \p R. The result is subtype dependent but it is
  /// intended that information known in either state will be known in
  /// this one afterwards.
  ///
  /// \return The computed result.
  ///
  /// \param R The other operand or state.
  void operator+=(const IntegerStateBase<base_t, BestState, WorstState> &R) {
    handleNewKnownValue(R.getKnown());
  }

  /// Join assumed and known information with \p R using bitwise OR.
  ///
  /// \return The computed result.
  ///
  /// \param R The other operand or state.
  void operator|=(const IntegerStateBase<base_t, BestState, WorstState> &R) {
    joinOR(R.getAssumed(), R.getKnown());
  }

  /// Join assumed and known information with \p R using bitwise AND.
  ///
  /// \return The computed result.
  ///
  /// \param R The other operand or state.
  void operator&=(const IntegerStateBase<base_t, BestState, WorstState> &R) {
    joinAND(R.getAssumed(), R.getKnown());
  }

protected:
  /// Handle a new assumed value \p Value. Subtype dependent.
  ///
  /// \param Value The new state value.
  virtual void handleNewAssumedValue(base_t Value) = 0;

  /// Handle a new known value \p Value. Subtype dependent.
  ///
  /// \param Value The new state value.
  virtual void handleNewKnownValue(base_t Value) = 0;

  /// Handle a  value \p Value. Subtype dependent.
  ///
  /// \param AssumedValue The assumed integer encoding.
  /// \param KnownValue The known integer encoding.
  virtual void joinOR(base_t AssumedValue, base_t KnownValue) = 0;

  /// Handle a new assumed value \p Value. Subtype dependent.
  ///
  /// \param AssumedValue The assumed integer encoding.
  /// \param KnownValue The known integer encoding.
  virtual void joinAND(base_t AssumedValue, base_t KnownValue) = 0;

  /// The known state encoding in an integer of type base_t.
  ///
  /// \return The computed result.
  base_t Known = getWorstState();

  /// The assumed state encoding in an integer of type base_t.
  ///
  /// \return The computed result.
  base_t Assumed = getBestState();
};

/// Specialization of the integer state for a bit-wise encoding.
///
/// \return The computed result.
template <typename base_ty = uint32_t, base_ty BestState = ~base_ty(0),
          base_ty WorstState = 0>
struct BitIntegerState
    : public IntegerStateBase<base_ty, BestState, WorstState> {
  /// Superclass type alias.
  using super = IntegerStateBase<base_ty, BestState, WorstState>;
  /// Underlying integer storage type.
  using base_t = base_ty;
  /// Construct a bit-integer state.
  BitIntegerState() = default;
  /// Construct a bit-integer state with an initial assumed encoding.
  ///
  /// \param Assumed The initial assumed state encoding.
  BitIntegerState(base_t Assumed) : super(Assumed) {}

  /// Return true if the bits set in \p BitsEncoding are "known bits".
  ///
  /// \return True if the bits set in \p BitsEncoding are "known bits".
  ///
  /// \param BitsEncoding The bit encoding of interest.
  bool isKnown(base_t BitsEncoding = BestState) const {
    return (this->Known & BitsEncoding) == BitsEncoding;
  }

  /// Return true if the bits set in \p BitsEncoding are "assumed bits".
  ///
  /// \return True if the bits set in \p BitsEncoding are "assumed bits".
  ///
  /// \param BitsEncoding The bit encoding of interest.
  bool isAssumed(base_t BitsEncoding = BestState) const {
    return (this->Assumed & BitsEncoding) == BitsEncoding;
  }

  /// Add the bits in \p BitsEncoding to the "known bits".
  ///
  /// \return A reference to the requested object.
  ///
  /// \param Bits The bits to add as known.
  BitIntegerState &addKnownBits(base_t Bits) {
    // Make sure we never miss any "known bits".
    this->Assumed |= Bits;
    this->Known |= Bits;
    return *this;
  }

  /// Remove the bits in \p BitsEncoding from the "assumed bits" if not known.
  ///
  /// \return A reference to the requested object.
  ///
  /// \param BitsEncoding The bit encoding of interest.
  BitIntegerState &removeAssumedBits(base_t BitsEncoding) {
    return intersectAssumedBits(~BitsEncoding);
  }

  /// Remove the bits in \p BitsEncoding from the "known bits".
  ///
  /// \return A reference to the requested object.
  ///
  /// \param BitsEncoding The bit encoding of interest.
  BitIntegerState &removeKnownBits(base_t BitsEncoding) {
    this->Known = (this->Known & ~BitsEncoding);
    return *this;
  }

  /// Keep only "assumed bits" also set in \p BitsEncoding but all known ones.
  ///
  /// \return A reference to the requested object.
  ///
  /// \param BitsEncoding The bit encoding of interest.
  BitIntegerState &intersectAssumedBits(base_t BitsEncoding) {
    // Make sure we never lose any "known bits".
    this->Assumed = (this->Assumed & BitsEncoding) | this->Known;
    return *this;
  }

private:
  void handleNewAssumedValue(base_t Value) override {
    intersectAssumedBits(Value);
  }
  void handleNewKnownValue(base_t Value) override { addKnownBits(Value); }
  void joinOR(base_t AssumedValue, base_t KnownValue) override {
    this->Known |= KnownValue;
    this->Assumed |= AssumedValue;
  }
  void joinAND(base_t AssumedValue, base_t KnownValue) override {
    this->Known &= KnownValue;
    this->Assumed &= AssumedValue;
  }
};

/// Specialization of the integer state for an increasing value, hence ~0u is
/// the best state and 0 the worst.
///
/// \return The computed result.
template <typename base_ty = uint32_t, base_ty BestState = ~base_ty(0),
          base_ty WorstState = 0>
struct IncIntegerState
    : public IntegerStateBase<base_ty, BestState, WorstState> {
  /// Superclass type alias.
  using super = IntegerStateBase<base_ty, BestState, WorstState>;
  /// Underlying integer storage type.
  using base_t = base_ty;

  /// Construct an increasing-integer state.
  IncIntegerState() : super() {}
  /// Construct an increasing-integer state with an initial assumed value.
  ///
  /// \param Assumed The initial assumed state encoding.
  IncIntegerState(base_t Assumed) : super(Assumed) {}

  /// Return the best possible representable state.
  ///
  /// \return The best possible representable state.
  static constexpr base_t getBestState() { return BestState; }
  /// Return the best state, ignoring \p Unused.
  ///
  /// \return The best state, ignoring \p Unused.
  ///
  /// \param Unused Unused; present for API uniformity with other states.
  static constexpr base_t getBestState(
      const IncIntegerState<base_ty, BestState, WorstState> &Unused) {
    return getBestState();
  }

  /// Take minimum of assumed and \p Value.
  ///
  /// \return A reference to the requested object.
  ///
  /// \param Value The new state value.
  IncIntegerState &takeAssumedMinimum(base_t Value) {
    // Make sure we never lose "known value".
    this->Assumed = std::max(std::min(this->Assumed, Value), this->Known);
    return *this;
  }

  /// Take maximum of known and \p Value.
  ///
  /// \return A reference to the requested object.
  ///
  /// \param Value The new state value.
  IncIntegerState &takeKnownMaximum(base_t Value) {
    // Make sure we never lose "known value".
    this->Assumed = std::max(Value, this->Assumed);
    this->Known = std::max(Value, this->Known);
    return *this;
  }

private:
  void handleNewAssumedValue(base_t Value) override {
    takeAssumedMinimum(Value);
  }
  void handleNewKnownValue(base_t Value) override { takeKnownMaximum(Value); }
  void joinOR(base_t AssumedValue, base_t KnownValue) override {
    this->Known = std::max(this->Known, KnownValue);
    this->Assumed = std::max(this->Assumed, AssumedValue);
  }
  void joinAND(base_t AssumedValue, base_t KnownValue) override {
    this->Known = std::min(this->Known, KnownValue);
    this->Assumed = std::min(this->Assumed, AssumedValue);
  }
};

/// Specialization of the integer state for a decreasing value, hence 0 is the
/// best state and ~0u the worst.
///
/// \return The computed result.
template <typename base_ty = uint32_t>
struct DecIntegerState : public IntegerStateBase<base_ty, 0, ~base_ty(0)> {
  /// Underlying integer storage type.
  using base_t = base_ty;

  /// Construct a decreasing-integer state.
  DecIntegerState() = default;
  /// Construct a decreasing-integer state with an initial assumed value.
  ///
  /// \param Assumed The initial assumed state encoding.
  DecIntegerState(base_t Assumed) : IntegerStateBase(Assumed) {}

  /// Take maximum of assumed and \p Value.
  ///
  /// \return A reference to the requested object.
  ///
  /// \param Value The new state value.
  DecIntegerState &takeAssumedMaximum(base_t Value) {
    // Make sure we never lose "known value".
    this->Assumed = std::min(std::max(this->Assumed, Value), this->Known);
    return *this;
  }

  /// Take minimum of known and \p Value.
  ///
  /// \return A reference to the requested object.
  ///
  /// \param Value The new state value.
  DecIntegerState &takeKnownMinimum(base_t Value) {
    // Make sure we never lose "known value".
    this->Assumed = std::min(Value, this->Assumed);
    this->Known = std::min(Value, this->Known);
    return *this;
  }

private:
  void handleNewAssumedValue(base_t Value) override {
    takeAssumedMaximum(Value);
  }
  void handleNewKnownValue(base_t Value) override { takeKnownMinimum(Value); }
  void joinOR(base_t AssumedValue, base_t KnownValue) override {
    this->Assumed = std::min(this->Assumed, KnownValue);
    this->Assumed = std::min(this->Assumed, AssumedValue);
  }
  void joinAND(base_t AssumedValue, base_t KnownValue) override {
    this->Assumed = std::max(this->Assumed, KnownValue);
    this->Assumed = std::max(this->Assumed, AssumedValue);
  }
};

/// Simple wrapper for a single bit (boolean) state.
struct BooleanState : public IntegerStateBase<bool, true, false> {
  /// Superclass type alias.
  using super = IntegerStateBase<bool, true, false>;
  /// Underlying integer storage type.
  using base_t = IntegerStateBase::base_t;

  /// Construct a boolean state.
  BooleanState() = default;
  /// Construct a boolean state with an initial assumed value.
  ///
  /// \param Assumed The initial assumed state encoding.
  BooleanState(base_t Assumed) : super(Assumed) {}

  /// Set the assumed value to \p Value but never below the known one.
  ///
  /// \param Value The new state value.
  void setAssumed(bool Value) { Assumed &= (Known | Value); }

  /// Set the known and asssumed value to \p Value.
  ///
  /// \param Value The new state value.
  void setKnown(bool Value) {
    Known |= Value;
    Assumed |= Value;
  }

  /// Return true if the state is assumed to hold.
  ///
  /// \return True if the state is assumed to hold.
  bool isAssumed() const { return getAssumed(); }

  /// Return true if the state is known to hold.
  ///
  /// \return True if the state is known to hold.
  bool isKnown() const { return getKnown(); }

private:
  void handleNewAssumedValue(base_t Value) override {
    if (!Value)
      Assumed = Known;
  }
  void handleNewKnownValue(base_t Value) override {
    if (Value)
      Known = (Assumed = Value);
  }
  void joinOR(base_t AssumedValue, base_t KnownValue) override {
    Known |= KnownValue;
    Assumed |= AssumedValue;
  }
  void joinAND(base_t AssumedValue, base_t KnownValue) override {
    Known &= KnownValue;
    Assumed &= AssumedValue;
  }
};

/// State for an integer range.
struct IntegerRangeState : public AbstractState {

  /// Bitwidth of the associated value.
  uint32_t BitWidth;

  /// State representing assumed range, initially set to empty.
  ConstantRange Assumed;

  /// State representing known range, initially set to [-inf, inf].
  ConstantRange Known;

  /// Construct an integer-range state.
  /// Construct an integer-range state for the given bit width.
  ///
  /// \param BitWidth The bit width of values tracked by this state.
  IntegerRangeState(uint32_t BitWidth)
      : BitWidth(BitWidth), Assumed(ConstantRange::getEmpty(BitWidth)),
        Known(ConstantRange::getFull(BitWidth)) {}

  /// Construct an integer-range state from an assumed constant range.
  ///
  /// \param CR The initial assumed constant range.
  IntegerRangeState(const ConstantRange &CR)
      : BitWidth(CR.getBitWidth()), Assumed(CR),
        Known(getWorstState(CR.getBitWidth())) {}

  /// Return the worst possible representable state.
  ///
  /// \return The worst possible representable state.
  ///
  /// \param BitWidth The bit width of the range.
  static ConstantRange getWorstState(uint32_t BitWidth) {
    return ConstantRange::getFull(BitWidth);
  }

  /// Return the best possible representable state.
  ///
  /// \return The best possible representable state.
  ///
  /// \param BitWidth The bit width of the range.
  static ConstantRange getBestState(uint32_t BitWidth) {
    return ConstantRange::getEmpty(BitWidth);
  }
  /// Return the best state for the bit width of \p IRS.
  ///
  /// \return The best state for the bit width of \p IRS.
  ///
  /// \param IRS The integer-range state whose bit width is used.
  static ConstantRange getBestState(const IntegerRangeState &IRS) {
    return getBestState(IRS.getBitWidth());
  }

  /// Return associated values' bit width.
  ///
  /// \return Associated values' bit width.
  uint32_t getBitWidth() const { return BitWidth; }

  /// See AbstractState::isValidState()
  ///
  /// \return True if the condition described by this query holds.
  bool isValidState() const override {
    return BitWidth > 0 && !Assumed.isFullSet();
  }

  /// See AbstractState::isAtFixpoint()
  ///
  /// \return True if the condition described by this query holds.
  bool isAtFixpoint() const override { return Assumed == Known; }

  /// See AbstractState::indicateOptimisticFixpoint(...)
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus indicateOptimisticFixpoint() override {
    Known = Assumed;
    return ChangeStatus::CHANGED;
  }

  /// See AbstractState::indicatePessimisticFixpoint(...)
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus indicatePessimisticFixpoint() override {
    Assumed = Known;
    return ChangeStatus::CHANGED;
  }

  /// Return the known state encoding
  ///
  /// \return The known state encoding.
  ConstantRange getKnown() const { return Known; }

  /// Return the assumed state encoding.
  ///
  /// \return The assumed state encoding.
  ConstantRange getAssumed() const { return Assumed; }

  /// Unite assumed range with the passed state.
  ///
  /// \param R The other operand or state.
  void unionAssumed(const ConstantRange &R) {
    // Don't lose a known range.
    Assumed = Assumed.unionWith(R).intersectWith(Known);
  }

  /// See IntegerRangeState::unionAssumed(..).
  ///
  /// \param R The other operand or state.
  void unionAssumed(const IntegerRangeState &R) {
    unionAssumed(R.getAssumed());
  }

  /// Intersect known range with the passed state.
  ///
  /// \param R The other operand or state.
  void intersectKnown(const ConstantRange &R) {
    Assumed = Assumed.intersectWith(R);
    Known = Known.intersectWith(R);
  }

  /// See IntegerRangeState::intersectKnown(..).
  ///
  /// \param R The other operand or state.
  void intersectKnown(const IntegerRangeState &R) {
    intersectKnown(R.getKnown());
  }

  /// Equality for IntegerRangeState.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param R The other operand or state.
  bool operator==(const IntegerRangeState &R) const {
    return getAssumed() == R.getAssumed() && getKnown() == R.getKnown();
  }

  /// Clamp this state with \p R.
  ///
  /// Only information assumed in both states remains assumed afterwards.
  ///
  /// \return The computed result.
  ///
  /// \param R The other operand or state.
  IntegerRangeState operator^=(const IntegerRangeState &R) {
    // NOTE: `^=` operator seems like `intersect` but in this case, we need to
    // take `union`.
    unionAssumed(R);
    return *this;
  }

  /// Join assumed and known ranges with \p R using set union.
  ///
  /// \return The computed result.
  ///
  /// \param R The other operand or state.
  IntegerRangeState operator&=(const IntegerRangeState &R) {
    // NOTE: `&=` operator seems like `intersect` but in this case, we need to
    // take `union`.
    Known = Known.unionWith(R.getKnown());
    Assumed = Assumed.unionWith(R.getAssumed());
    return *this;
  }
};

/// Simple state for a set.
///
/// This represents a state containing a set of values. The interface supports
/// modelling sets that contain all possible elements. The state's internal
/// value is modified using union or intersection operations.
template <typename BaseTy> struct SetState : public AbstractState {
  /// A wrapper around a set that has semantics for handling unions and
  /// intersections with a "universal" set that contains all elements.
  struct SetContents {
    /// Creates a universal set with no concrete elements or an empty set.
    ///
    /// \param Universal Whether the set contents are universal.
    SetContents(bool Universal) : Universal(Universal) {}

    /// Creates a non-universal set with concrete values.
    ///
    /// \param Assumptions The assumed set contents.
    SetContents(const DenseSet<BaseTy> &Assumptions)
        : Universal(false), Set(Assumptions) {}

    /// Create set contents with explicit universality and elements.
    ///
    /// \return A reference to the created abstract attribute.
    ///
    /// \param Universal Whether the set contents are universal.
    /// \param Assumptions The assumed set contents.
    SetContents(bool Universal, const DenseSet<BaseTy> &Assumptions)
        : Universal(Universal), Set(Assumptions) {}

    /// Return the underlying set.
    ///
    /// \return The underlying set.
    const DenseSet<BaseTy> &getSet() const { return Set; }

    /// Return true if the set is universal.
    ///
    /// \return True if the set is universal.
    bool isUniversal() const { return Universal; }

    /// Return true if the set has no concrete elements and is not universal.
    ///
    /// \return True if the set has no concrete elements and is not universal.
    bool empty() const { return Set.empty() && !Universal; }

    /// Finds A := A ^ B where A or B could be the "Universal" set which
    /// contains every possible attribute. Returns true if changes were made.
    ///
    /// \return True if the condition described by this query holds.
    ///
    /// \param RHS The other operand or state.
    bool getIntersection(const SetContents &RHS) {
      bool IsUniversal = Universal;
      unsigned Size = Set.size();

      // A := A ^ U = A
      if (RHS.isUniversal())
        return false;

      // A := U ^ B = B
      if (Universal)
        Set = RHS.getSet();
      else
        set_intersect(Set, RHS.getSet());

      Universal &= RHS.isUniversal();
      return IsUniversal != Universal || Size != Set.size();
    }

    /// Finds A := A u B where A or B could be the "Universal" set which
    /// contains every possible attribute. returns true if changes were made.
    ///
    /// \return True if the condition described by this query holds.
    ///
    /// \param RHS The other operand or state.
    bool getUnion(const SetContents &RHS) {
      bool IsUniversal = Universal;
      unsigned Size = Set.size();

      // A := A u U = U = U u B
      if (!RHS.isUniversal() && !Universal)
        set_union(Set, RHS.getSet());

      Universal |= RHS.isUniversal();
      return IsUniversal != Universal || Size != Set.size();
    }

  private:
    /// Indicates if this set is "universal", containing every possible element.
    bool Universal;

    /// The set of currently active assumptions.
    DenseSet<BaseTy> Set;
  };

  /// Construct a set state.
  SetState() : Known(false), Assumed(true), IsAtFixedpoint(false) {}

  /// Initializes the known state with an initial set and initializes the
  /// assumed state as universal.
  ///
  /// \param Known The known-set contents used to initialize the state.
  SetState(const DenseSet<BaseTy> &Known)
      : Known(Known), Assumed(true), IsAtFixedpoint(false) {}

  /// See AbstractState::isValidState()
  ///
  /// \return True if the condition described by this query holds.
  bool isValidState() const override { return !Assumed.empty(); }

  /// See AbstractState::isAtFixpoint()
  ///
  /// \return True if the condition described by this query holds.
  bool isAtFixpoint() const override { return IsAtFixedpoint; }

  /// See AbstractState::indicateOptimisticFixpoint(...)
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus indicateOptimisticFixpoint() override {
    IsAtFixedpoint = true;
    Known = Assumed;
    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractState::indicatePessimisticFixpoint(...)
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus indicatePessimisticFixpoint() override {
    IsAtFixedpoint = true;
    Assumed = Known;
    return ChangeStatus::CHANGED;
  }

  /// Return the known state encoding.
  ///
  /// \return The known state encoding.
  const SetContents &getKnown() const { return Known; }

  /// Return the assumed state encoding.
  ///
  /// \return The assumed state encoding.
  const SetContents &getAssumed() const { return Assumed; }

  /// Returns if the set state contains the element.
  ///
  /// \return If the set state contains the element.
  ///
  /// \param Elem The element to test for membership.
  bool setContains(const BaseTy &Elem) const {
    return Assumed.getSet().contains(Elem) || Known.getSet().contains(Elem);
  }

  /// Performs the set intersection between this set and \p RHS. Returns true if
  /// changes were made.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param RHS The other operand or state.
  bool getIntersection(const SetContents &RHS) {
    bool IsUniversal = Assumed.isUniversal();
    unsigned SizeBefore = Assumed.getSet().size();

    // Get intersection and make sure that the known set is still a proper
    // subset of the assumed set. A := K u (A ^ R).
    Assumed.getIntersection(RHS);
    Assumed.getUnion(Known);

    return SizeBefore != Assumed.getSet().size() ||
           IsUniversal != Assumed.isUniversal();
  }

  /// Performs the set union between this set and \p RHS. Returns true if
  /// changes were made.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param RHS The other operand or state.
  bool getUnion(const SetContents &RHS) { return Assumed.getUnion(RHS); }

private:
  /// The set of values known for this state.
  SetContents Known;

  /// The set of assumed values for this state.
  SetContents Assumed;

  bool IsAtFixedpoint;
};

/// Helper to tie a abstract state implementation to an abstract attribute.
template <typename StateTy, typename BaseType, class... Ts>
/// Mixin wrapping an abstract state into an abstract attribute.
struct StateWrapper : public BaseType, public StateTy {
  /// Provide static access to the type of the state.
  using StateType = StateTy;

  /// Construct a state wrapper around an IR position.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param Args Extra arguments forwarded to the state constructor.
  StateWrapper(const IRPosition &IRP, Ts... Args)
      : BaseType(IRP), StateTy(Args...) {}

  /// See AbstractAttribute::getState(...).
  ///
  /// \return The abstract state associated with this attribute.
  StateType &getState() override { return *this; }

  /// See AbstractAttribute::getState(...).
  ///
  /// \return The abstract state associated with this attribute.
  const StateType &getState() const override { return *this; }
};

/// Helper class that provides common functionality to manifest IR attributes.
template <Attribute::AttrKind AK, typename BaseType, typename AAType>
/// Helper base for abstract attributes corresponding to IR attributes.
struct IRAttribute : public BaseType {
  /// Construct an IR-attribute abstract attribute.
  ///
  /// \param IRP The IR position of this abstract attribute.
  IRAttribute(const IRPosition &IRP) : BaseType(IRP) {}

  /// Most boolean IRAttribute AAs don't do anything non-trivial
  /// in their initializers while non-boolean ones often do. Subclasses can
  /// change this.
  ///
  /// \return True if the condition described by this query holds.
  static bool hasTrivialInitializer() { return Attribute::isEnumAttrKind(AK); }

  /// Compile time access to the IR attribute kind.
  static constexpr Attribute::AttrKind IRAttributeKind = AK;

  /// Return true if the IR attribute(s) associated with this AA are implied for
  /// an undef value.
  ///
  /// \return True if the IR attribute(s) associated with this AA are implied for an undef value.
  static bool isImpliedByUndef() { return true; }

  /// Return true if the IR attribute(s) associated with this AA are implied for
  /// an poison value.
  ///
  /// \return True if the IR attribute(s) associated with this AA are implied for an poison value.
  static bool isImpliedByPoison() { return true; }

  /// Return true if the IR already implies this attribute.
  ///
  /// \return True if the IR already implies this attribute.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  /// \param ImpliedAttributeKind The IR attribute kind being checked.
  /// \param IgnoreSubsumingPositions If true, ignore subsuming IR positions.
  static bool isImpliedByIR(Attributor &A, const IRPosition &IRP,
                            Attribute::AttrKind ImpliedAttributeKind = AK,
                            bool IgnoreSubsumingPositions = false) {
    if (AAType::isImpliedByUndef() && isa<UndefValue>(IRP.getAssociatedValue()))
      return true;
    if (AAType::isImpliedByPoison() &&
        isa<PoisonValue>(IRP.getAssociatedValue()))
      return true;
    return A.hasAttr(IRP, {ImpliedAttributeKind}, IgnoreSubsumingPositions,
                     ImpliedAttributeKind);
  }

  /// See AbstractAttribute::manifest(...).
  ///
  /// \return Whether the state or IR changed.
  ///
  /// \param A The Attributor managing the analysis.
  ChangeStatus manifest(Attributor &A) override {
    if (isa<UndefValue>(this->getIRPosition().getAssociatedValue()))
      return ChangeStatus::UNCHANGED;
    SmallVector<Attribute, 4> DeducedAttrs;
    getDeducedAttributes(A, this->getAnchorValue().getContext(), DeducedAttrs);
    if (DeducedAttrs.empty())
      return ChangeStatus::UNCHANGED;
    return A.manifestAttrs(this->getIRPosition(), DeducedAttrs);
  }

  /// Return the kind that identifies the abstract attribute implementation.
  ///
  /// \return The kind that identifies the abstract attribute implementation.
  Attribute::AttrKind getAttrKind() const { return AK; }

  /// Return the deduced attributes in \p Attrs.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param Ctx The LLVM context used to construct attributes.
  /// \param Attrs The vector receiving deduced attributes.
  virtual void getDeducedAttributes(Attributor &A, LLVMContext &Ctx,
                                    SmallVectorImpl<Attribute> &Attrs) const {
    Attrs.emplace_back(Attribute::get(Ctx, getAttrKind()));
  }
};

/// Base struct for all "concrete attribute" deductions.
///
/// The abstract attribute is a minimal interface that allows the Attributor to
/// orchestrate the abstract/fixpoint analysis. The design allows to hide away
/// implementation choices made for the subclasses but also to structure their
/// implementation and simplify the use of other abstract attributes in-flight.
///
/// To allow easy creation of new attributes, most methods have default
/// implementations. The ones that do not are generally straight forward, except
/// `AbstractAttribute::updateImpl` which is the location of most reasoning
/// associated with the abstract attribute. The update is invoked by the
/// Attributor in case the situation used to justify the current optimistic
/// state might have changed. The Attributor determines this automatically
/// by monitoring the `Attributor::getAAFor` calls made by abstract attributes.
///
/// The `updateImpl` method should inspect the IR and other abstract attributes
/// in-flight to justify the best possible (=optimistic) state. The actual
/// implementation is, similar to the underlying abstract state encoding, not
/// exposed. In the most common case, the `updateImpl` will go through a list of
/// reasons why its optimistic state is valid given the current information. If
/// any combination of them holds and is sufficient to justify the current
/// optimistic state, the method shall return UNCHAGED. If not, the optimistic
/// state is adjusted to the situation and the method shall return CHANGED.
///
/// If the manifestation of the "concrete attribute" deduced by the subclass
/// differs from the "default" behavior, which is a (set of) LLVM-IR
/// attribute(s) for an argument, call site argument, function return value, or
/// function, the `AbstractAttribute::manifest` method should be overloaded.
///
/// NOTE: If the state obtained via getState() is INVALID, thus if
///       AbstractAttribute::getState().isValidState() returns false, no
///       information provided by the methods of this class should be used.
/// NOTE: The Attributor currently has certain limitations to what we can do.
///       As a general rule of thumb, "concrete" abstract attributes should *for
///       now* only perform "backward" information propagation. That means
///       optimistic information obtained through abstract attributes should
///       only be used at positions that precede the origin of the information
///       with regards to the program flow. More practically, information can
///       *now* be propagated from instructions to their enclosing function, but
///       *not* from call sites to the called function. The mechanisms to allow
///       both directions will be added in the future.
/// NOTE: The mechanics of adding a new "concrete" abstract attribute are
///       described in the file comment.
struct LLVM_ABI AbstractAttribute : public IRPosition, public AADepGraphNode {
  /// State type associated with this abstract attribute.
  using StateType = AbstractState;

  /// Create an abstract attribute anchored at \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  AbstractAttribute(const IRPosition &IRP) : IRPosition(IRP) {}

  /// Virtual destructor.
  ~AbstractAttribute() override = default;

  /// Compile time access to the IR attribute kind.
  static constexpr Attribute::AttrKind IRAttributeKind = Attribute::None;

  /// Return true if \p DGN is an AbstractAttribute.
  ///
  /// We eagerly return true here because all AADepGraphNodes except for
  /// the Synthetic Node are of type AbstractAttribute.
  ///
  /// \return True if \p DGN is an AbstractAttribute.
  ///
  /// \param DGN The dependency-graph node to classify.
  static bool classof(const AADepGraphNode *DGN) { return true; }

  /// Return false if this AA does anything non-trivial (hence not done by
  /// default) in its initializer.
  ///
  /// \return False if this AA does anything non-trivial (hence not done by default) in its initializer.
  static bool hasTrivialInitializer() { return false; }

  /// Return true if this AA requires a "callee" (or an associted function) for
  /// a call site positon. Default is optimistic to minimize AAs.
  ///
  /// \return True if this AA requires a "callee" (or an associted function) for a call site positon. Default is optimistic to minimize AAs.
  static bool requiresCalleeForCallBase() { return false; }

  /// Return true if this AA requires non-asm "callee" for a call site positon.
  ///
  /// \return True if this AA requires non-asm "callee" for a call site positon.
  static bool requiresNonAsmForCallBase() { return true; }

  /// Return true if this AA requires all callees for an argument or function
  /// positon.
  ///
  /// \return True if this AA requires all callees for an argument or function positon.
  static bool requiresCallersForArgOrFunction() { return false; }

  /// Return false if an AA should not be created for \p IRP.
  ///
  /// \return False if an AA should not be created for \p IRP.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    return true;
  }

  /// Return false if an AA should not be updated for \p IRP.
  ///
  /// \return False if an AA should not be updated for \p IRP.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForUpdate(Attributor &A, const IRPosition &IRP) {
    Function *AssociatedFn = IRP.getAssociatedFunction();
    bool IsFnInterface = IRP.isFnInterfaceKind();
    assert((!IsFnInterface || AssociatedFn) &&
           "Function interface without a function?");

    // TODO: Not all attributes require an exact definition. Find a way to
    //       enable deduction for some but not all attributes in case the
    //       definition might be changed at runtime, see also
    //       http://lists.llvm.org/pipermail/llvm-dev/2018-February/121275.html.
    // TODO: We could always determine abstract attributes and if sufficient
    //       information was found we could duplicate the functions that do not
    //       have an exact definition.
    return !IsFnInterface || A.isFunctionIPOAmendable(*AssociatedFn);
  }

  /// Initialize the state with the information in the Attributor \p A.
  ///
  /// This function is called by the Attributor once all abstract attributes
  /// have been identified. It can and shall be used for task like:
  ///  - identify existing knowledge in the IR and use it for the "known state"
  ///  - perform any work that is not going to change over time, e.g., determine
  ///    a subset of the IR, or attributes in-flight, that have to be looked at
  ///    in the `updateImpl` method.
  ///
  /// \param A The Attributor managing the analysis.
  virtual void initialize(Attributor &A) {}

  /// Return true if this is a query abstract attribute.
  ///
  /// A query AA is always scheduled as long as we do updates because it does
  /// lazy computation that cannot be determined to be done from the outside.
  /// However, while query AAs will not be fixed if they do not have outstanding
  /// dependences, we will only schedule them like other AAs. If a query AA that
  /// received a new query it needs to request an update via
  /// `Attributor::requestUpdateForAA`.
  ///
  /// \return True if this is a query abstract attribute.
  virtual bool isQueryAA() const { return false; }

  /// Return the internal abstract state for inspection.
  ///
  /// \return The internal abstract state for inspection.
  virtual StateType &getState() = 0;
  /// Return the internal abstract state for inspection.
  ///
  /// \return The internal abstract state for inspection.
  virtual const StateType &getState() const = 0;

  /// Return an IR position, see struct IRPosition.
  ///
  /// \return An IR position, see struct IRPosition.
  const IRPosition &getIRPosition() const { return *this; };
  /// Return an IR position, see struct IRPosition.
  ///
  /// \return An IR position, see struct IRPosition.
  IRPosition &getIRPosition() { return *this; };

  /// Helper functions, for debug purposes only.
  ///{
  /// Print this abstract attribute to \p OS.
  ///
  /// \param OS The output stream.
  void print(raw_ostream &OS) const { print(nullptr, OS); }
  /// Print this abstract attribute to \p OS, optionally with Attributor context.
  ///
  /// \param A Optional Attributor providing print context.
  /// \param OS The output stream.
  void print(Attributor *A, raw_ostream &OS) const override;
  /// Print this attribute and its dependences.
  ///
  /// \param OS The output stream.
  virtual void printWithDeps(raw_ostream &OS) const;
  /// Dump this abstract attribute to stderr.
  void dump() const { this->print(dbgs()); }

  /// This function should return the "summarized" assumed state as string.
  ///
  /// \return The "summarized" assumed state as string.
  ///
  /// \param A The Attributor managing the analysis.
  virtual const std::string getAsStr(Attributor *A) const = 0;

  /// This function should return the name of the AbstractAttribute
  ///
  /// \return The name of the AbstractAttribute.
  virtual StringRef getName() const = 0;

  /// This function should return the address of the ID of the AbstractAttribute
  ///
  /// \return The address of the ID of the AbstractAttribute.
  virtual const char *getIdAddr() const = 0;
  ///}

  /// Allow the Attributor access to the protected methods.
  friend struct Attributor;

protected:
  /// Hook for the Attributor to trigger an update of the internal state.
  ///
  /// If this attribute is already fixed, this method will return UNCHANGED,
  /// otherwise it delegates to `AbstractAttribute::updateImpl`.
  ///
  /// \Return CHANGED if the internal state changed, otherwise UNCHANGED.
  ///
  /// \param A The Attributor managing the analysis.
  ChangeStatus update(Attributor &A);

  /// Hook for the Attributor to trigger the manifestation of the information
  /// represented by the abstract attribute in the LLVM-IR.
  ///
  /// \Return CHANGED if the IR was altered, otherwise UNCHANGED.
  ///
  /// \param A The Attributor managing the analysis.
  virtual ChangeStatus manifest(Attributor &A) {
    return ChangeStatus::UNCHANGED;
  }

  /// Hook to enable custom statistic tracking, called after manifest that
  /// resulted in a change if statistics are enabled.
  ///
  /// We require subclasses to provide an implementation so we remember to
  /// add statistics for them.
  virtual void trackStatistics() const = 0;

  /// The actual update/transfer function which has to be implemented by the
  /// derived classes.
  ///
  /// If it is called, the environment has changed and we have to determine if
  /// the current information is still valid or adjust it otherwise.
  ///
  /// \Return CHANGED if the internal state changed, otherwise UNCHANGED.
  ///
  /// \param A The Attributor managing the analysis.
  virtual ChangeStatus updateImpl(Attributor &A) = 0;
};

/// Stream operators for Attributor debug printing.
///
///{
/// Print an abstract attribute to an output stream.
///
/// \return The output stream \p OS.
///
/// \param OS The output stream.
/// \param AA The abstract attribute to print.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const AbstractAttribute &AA);
/// Print a change status to an output stream.
///
/// \return The output stream \p OS.
///
/// \param OS The output stream.
/// \param S The change status to print.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, ChangeStatus S);
/// Print an IR-position kind to an output stream.
///
/// \return The output stream \p OS.
///
/// \param OS The output stream.
/// \param Kind The IR-position kind to print.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, IRPosition::Kind Kind);
/// Print an IR position to an output stream.
///
/// \return The output stream \p OS.
///
/// \param OS The output stream.
/// \param IRP The IR position to print.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const IRPosition &IRP);
/// Print an abstract state to an output stream.
///
/// \return The output stream \p OS.
///
/// \param OS The output stream.
/// \param State The abstract state to print.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const AbstractState &State);
/// Print an integer state to an output stream.
///
/// \return The output stream \p OS.
///
/// \param OS The output stream.
/// \param S The integer state to print.
template <typename base_ty, base_ty BestState, base_ty WorstState>
raw_ostream &
operator<<(raw_ostream &OS,
           const IntegerStateBase<base_ty, BestState, WorstState> &S) {
  return OS << "(" << S.getKnown() << "-" << S.getAssumed() << ")"
            << static_cast<const AbstractState &>(S);
}
/// Print an integer-range state to an output stream.
///
/// \return The output stream \p OS.
///
/// \param OS The output stream.
/// \param State The integer-range state to print.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const IntegerRangeState &State);
///}

/// Module pass that runs the full Attributor.
struct AttributorPass : public OptionalPassInfoMixin<AttributorPass> {
  /// Run the Attributor on module \p M.
  ///
  /// \return The set of analyses preserved by this pass.
  ///
  /// \param M The module to analyze.
  /// \param AM The module analysis manager.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};
/// CGSCC pass that runs the Attributor.
struct AttributorCGSCCPass : public OptionalPassInfoMixin<AttributorCGSCCPass> {
  /// Run the Attributor on SCC \p C.
  ///
  /// \return The set of analyses preserved by this pass.
  ///
  /// \param C The SCC to analyze.
  /// \param AM The CGSCC analysis manager.
  /// \param CG The lazy call graph.
  /// \param UR The CGSCC update result.
  LLVM_ABI PreservedAnalyses run(LazyCallGraph::SCC &C,
                                 CGSCCAnalysisManager &AM, LazyCallGraph &CG,
                                 CGSCCUpdateResult &UR);
};

/// Lightweight Attributor module pass that only runs attribute inference.
struct AttributorLightPass : public OptionalPassInfoMixin<AttributorLightPass> {
  /// Run the lightweight Attributor on module \p M.
  ///
  /// \return The set of analyses preserved by this pass.
  ///
  /// \param M The module to analyze.
  /// \param AM The module analysis manager.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Lightweight Attributor CGSCC pass that only runs attribute inference.
struct AttributorLightCGSCCPass
    : public OptionalPassInfoMixin<AttributorLightCGSCCPass> {
  /// Run the lightweight Attributor on SCC \p C.
  ///
  /// \return The set of analyses preserved by this pass.
  ///
  /// \param C The SCC to analyze.
  /// \param AM The CGSCC analysis manager.
  /// \param CG The lazy call graph.
  /// \param UR The CGSCC update result.
  LLVM_ABI PreservedAnalyses run(LazyCallGraph::SCC &C,
                                 CGSCCAnalysisManager &AM, LazyCallGraph &CG,
                                 CGSCCUpdateResult &UR);
};

/// Clamp state \p S with \p R and return whether \p S changed.
///
/// \return Whether the state or IR changed.
///
/// \param S The state being clamped (updated in place).
/// \param R The state providing information to clamp with.
template <typename StateType>
ChangeStatus clampStateAndIndicateChange(StateType &S, const StateType &R) {
  auto Assumed = S.getAssumed();
  S ^= R;
  return Assumed == S.getAssumed() ? ChangeStatus::UNCHANGED
                                   : ChangeStatus::CHANGED;
}

/// ----------------------------------------------------------------------------
//                       Abstract Attribute Classes
// ----------------------------------------------------------------------------

/// Abstract attribute for the nounwind function attribute.
struct AANoUnwind
    : public IRAttribute<Attribute::NoUnwind,
                         StateWrapper<BooleanState, AbstractAttribute>,
                         AANoUnwind> {
  /// Create a AANoUnwind for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AANoUnwind(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// Returns true if nounwind is assumed.
  ///
  /// \return True if nounwind is assumed.
  bool isAssumedNoUnwind() const { return getAssumed(); }

  /// Returns true if nounwind is known.
  ///
  /// \return True if nounwind is known.
  bool isKnownNoUnwind() const { return getKnown(); }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AANoUnwind &createForPosition(const IRPosition &IRP,
                                                Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AANoUnwind"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AANoUnwind
  ///
  /// \return True if \p AA is a AANoUnwind.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// Abstract attribute for the nosync function attribute.
struct AANoSync
    : public IRAttribute<Attribute::NoSync,
                         StateWrapper<BooleanState, AbstractAttribute>,
                         AANoSync> {
  /// Create a AANoSync for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AANoSync(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// Return true if the IR already implies this attribute.
  ///
  /// \return True if the IR already implies this attribute.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  /// \param ImpliedAttributeKind The IR attribute kind being checked.
  /// \param IgnoreSubsumingPositions If true, ignore subsuming IR positions.
  static bool isImpliedByIR(Attributor &A, const IRPosition &IRP,
                            Attribute::AttrKind ImpliedAttributeKind,
                            bool IgnoreSubsumingPositions = false) {
    // Note: This is also run for non-IPO amendable functions.
    assert(ImpliedAttributeKind == Attribute::NoSync);
    if (A.hasAttr(IRP, {Attribute::NoSync}, IgnoreSubsumingPositions,
                  Attribute::NoSync))
      return true;

    // Check for readonly + non-convergent.
    // TODO: We should be able to use hasAttr for Attributes, not only
    // AttrKinds.
    Function *F = IRP.getAssociatedFunction();
    if (!F || F->isConvergent())
      return false;

    SmallVector<Attribute, 2> Attrs;
    A.getAttrs(IRP, {Attribute::Memory}, Attrs, IgnoreSubsumingPositions);

    MemoryEffects ME = MemoryEffects::unknown();
    for (const Attribute &Attr : Attrs)
      ME &= Attr.getMemoryEffects();

    if (!ME.onlyReadsMemory())
      return false;

    A.manifestAttrs(IRP, Attribute::get(F->getContext(), Attribute::NoSync));
    return true;
  }

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.isFunctionScope() &&
        !IRP.getAssociatedType()->isPtrOrPtrVectorTy())
      return false;
    return IRAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// Returns true if "nosync" is assumed.
  ///
  /// \return True if "nosync" is assumed.
  bool isAssumedNoSync() const { return getAssumed(); }

  /// Returns true if "nosync" is known.
  ///
  /// \return True if "nosync" is known.
  bool isKnownNoSync() const { return getKnown(); }

  /// Return true if \p I is a non-relaxed atomic instruction.
  ///
  /// In other words, if an atomic instruction does not have unordered or
  /// monotonic ordering.
  ///
  /// \return True if \p I is a non-relaxed atomic instruction.
  ///
  /// \param I The instruction to inspect.
  LLVM_ABI static bool isNonRelaxedAtomic(const Instruction *I);

  /// Return true if \p CB is an aligned (GPU) barrier.
  ///
  /// Aligned barriers have to be executed by all threads. The flag
  /// \p ExecutedAligned indicates if the call is executed by all threads
  /// in a (thread-)block.
  ///
  /// \return True if \p CB is an aligned (GPU) barrier.
  ///
  /// \param CB The call base to inspect.
  /// \param ExecutedAligned Whether the call is executed by all threads.
  LLVM_ABI static bool isAlignedBarrier(const CallBase &CB,
                                        bool ExecutedAligned);

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AANoSync &createForPosition(const IRPosition &IRP,
                                              Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AANoSync"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AANoSync
  ///
  /// \return True if \p AA is a AANoSync.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract interface for all nonnull attributes.
struct AAMustProgress
    : public IRAttribute<Attribute::MustProgress,
                         StateWrapper<BooleanState, AbstractAttribute>,
                         AAMustProgress> {
  /// Create a AAMustProgress for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAMustProgress(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// Return true if the IR already implies this attribute.
  ///
  /// \return True if the IR already implies this attribute.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  /// \param ImpliedAttributeKind The IR attribute kind being checked.
  /// \param IgnoreSubsumingPositions If true, ignore subsuming IR positions.
  static bool isImpliedByIR(Attributor &A, const IRPosition &IRP,
                            Attribute::AttrKind ImpliedAttributeKind,
                            bool IgnoreSubsumingPositions = false) {
    // Note: This is also run for non-IPO amendable functions.
    assert(ImpliedAttributeKind == Attribute::MustProgress);
    return A.hasAttr(IRP, {Attribute::MustProgress, Attribute::WillReturn},
                     IgnoreSubsumingPositions, Attribute::MustProgress);
  }

  /// Return true if we assume that the underlying value is nonnull.
  ///
  /// \return True if we assume that the underlying value is nonnull.
  bool isAssumedMustProgress() const { return getAssumed(); }

  /// Return true if we know that underlying value is nonnull.
  ///
  /// \return True if we know that underlying value is nonnull.
  bool isKnownMustProgress() const { return getKnown(); }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAMustProgress &createForPosition(const IRPosition &IRP,
                                                    Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAMustProgress"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAMustProgress
  ///
  /// \return True if \p AA is a AAMustProgress.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract interface for all nonnull attributes.
struct AANonNull
    : public IRAttribute<Attribute::NonNull,
                         StateWrapper<BooleanState, AbstractAttribute>,
                         AANonNull> {
  /// Create a AANonNull for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AANonNull(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// See AbstractAttribute::hasTrivialInitializer.
  ///
  /// \return True if this attribute has a trivial initializer.
  static bool hasTrivialInitializer() { return false; }

  /// See IRAttribute::isImpliedByUndef.
  /// Undef is not necessarily nonnull as nonnull + noundef would cause poison.
  /// Poison implies nonnull though.
  ///
  /// \return True if undef implies this attribute.
  static bool isImpliedByUndef() { return false; }

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.getAssociatedType()->isPtrOrPtrVectorTy())
      return false;
    return IRAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// See AbstractAttribute::isImpliedByIR(...).
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  /// \param ImpliedAttributeKind The IR attribute kind being checked.
  /// \param IgnoreSubsumingPositions If true, ignore subsuming IR positions.
  LLVM_ABI static bool isImpliedByIR(Attributor &A, const IRPosition &IRP,
                                     Attribute::AttrKind ImpliedAttributeKind,
                                     bool IgnoreSubsumingPositions = false);

  /// Return true if we assume that the underlying value is nonnull.
  ///
  /// \return True if we assume that the underlying value is nonnull.
  bool isAssumedNonNull() const { return getAssumed(); }

  /// Return true if we know that underlying value is nonnull.
  ///
  /// \return True if we know that underlying value is nonnull.
  bool isKnownNonNull() const { return getKnown(); }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AANonNull &createForPosition(const IRPosition &IRP,
                                               Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AANonNull"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AANonNull
  ///
  /// \return True if \p AA is a AANonNull.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract attribute for norecurse.
struct AANoRecurse
    : public IRAttribute<Attribute::NoRecurse,
                         StateWrapper<BooleanState, AbstractAttribute>,
                         AANoRecurse> {
  /// Create a AANoRecurse for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AANoRecurse(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// Return true if "norecurse" is assumed.
  ///
  /// \return True if "norecurse" is assumed.
  bool isAssumedNoRecurse() const { return getAssumed(); }

  /// Return true if "norecurse" is known.
  ///
  /// \return True if "norecurse" is known.
  bool isKnownNoRecurse() const { return getKnown(); }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AANoRecurse &createForPosition(const IRPosition &IRP,
                                                 Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AANoRecurse"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AANoRecurse
  ///
  /// \return True if \p AA is a AANoRecurse.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract attribute for willreturn.
struct AAWillReturn
    : public IRAttribute<Attribute::WillReturn,
                         StateWrapper<BooleanState, AbstractAttribute>,
                         AAWillReturn> {
  /// Create a AAWillReturn for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAWillReturn(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// Return true if the IR already implies this attribute.
  ///
  /// \return True if the IR already implies this attribute.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  /// \param ImpliedAttributeKind The IR attribute kind being checked.
  /// \param IgnoreSubsumingPositions If true, ignore subsuming IR positions.
  static bool isImpliedByIR(Attributor &A, const IRPosition &IRP,
                            Attribute::AttrKind ImpliedAttributeKind,
                            bool IgnoreSubsumingPositions = false) {
    // Note: This is also run for non-IPO amendable functions.
    assert(ImpliedAttributeKind == Attribute::WillReturn);
    if (IRAttribute::isImpliedByIR(A, IRP, ImpliedAttributeKind,
                                   IgnoreSubsumingPositions))
      return true;
    if (!isImpliedByMustprogressAndReadonly(A, IRP))
      return false;
    A.manifestAttrs(IRP, Attribute::get(IRP.getAnchorValue().getContext(),
                                        Attribute::WillReturn));
    return true;
  }

  /// Check for `mustprogress` and `readonly` as they imply `willreturn`.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isImpliedByMustprogressAndReadonly(Attributor &A,
                                                 const IRPosition &IRP) {
    // Check for `mustprogress` in the scope and the associated function which
    // might be different if this is a call site.
    if (!A.hasAttr(IRP, {Attribute::MustProgress}))
      return false;

    SmallVector<Attribute, 2> Attrs;
    A.getAttrs(IRP, {Attribute::Memory}, Attrs,
               /* IgnoreSubsumingPositions */ false);

    MemoryEffects ME = MemoryEffects::unknown();
    for (const Attribute &Attr : Attrs)
      ME &= Attr.getMemoryEffects();
    return ME.onlyReadsMemory();
  }

  /// Return true if "willreturn" is assumed.
  ///
  /// \return True if "willreturn" is assumed.
  bool isAssumedWillReturn() const { return getAssumed(); }

  /// Return true if "willreturn" is known.
  ///
  /// \return True if "willreturn" is known.
  bool isKnownWillReturn() const { return getKnown(); }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAWillReturn &createForPosition(const IRPosition &IRP,
                                                  Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAWillReturn"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AAWillReturn
  ///
  /// \return True if \p AA is a AAWillReturn.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract attribute for undefined behavior.
struct AAUndefinedBehavior
    : public StateWrapper<BooleanState, AbstractAttribute> {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<BooleanState, AbstractAttribute>;
  /// Create a AAUndefinedBehavior for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAUndefinedBehavior(const IRPosition &IRP, Attributor &A) : Base(IRP) {}

  /// Return true if "undefined behavior" is assumed.
  ///
  /// \return True if "undefined behavior" is assumed.
  bool isAssumedToCauseUB() const { return getAssumed(); }

  /// Return true if "undefined behavior" is assumed for a specific instruction.
  ///
  /// \return True if "undefined behavior" is assumed for a specific instruction.
  ///
  /// \param I The instruction of interest.
  virtual bool isAssumedToCauseUB(Instruction *I) const = 0;

  /// Return true if "undefined behavior" is known.
  ///
  /// \return True if "undefined behavior" is known.
  bool isKnownToCauseUB() const { return getKnown(); }

  /// Return true if "undefined behavior" is known for a specific instruction.
  ///
  /// \return True if "undefined behavior" is known for a specific instruction.
  ///
  /// \param I The instruction of interest.
  virtual bool isKnownToCauseUB(Instruction *I) const = 0;

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAUndefinedBehavior &createForPosition(const IRPosition &IRP,
                                                         Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAUndefinedBehavior"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAUndefineBehavior
  ///
  /// \return True if \p AA is a AAUndefineBehavior.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract interface to determine reachability of point A to B.
struct AAIntraFnReachability
    : public StateWrapper<BooleanState, AbstractAttribute> {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<BooleanState, AbstractAttribute>;
  /// Create a AAIntraFnReachability for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAIntraFnReachability(const IRPosition &IRP, Attributor &A) : Base(IRP) {}

  /// Return true if \p From is assumed to reach \p To within the function.
  ///
  /// Users should provide two positions they are interested in, and the class
  /// determines (and caches) reachability.
  ///
  /// \return True if \p From is assumed to reach \p To within the function.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param From The source instruction.
  /// \param To The destination instruction.
  /// \param ExclusionSet Optional instructions excluded from reachability.
  virtual bool isAssumedReachable(
      Attributor &A, const Instruction &From, const Instruction &To,
      const AA::InstExclusionSetTy *ExclusionSet = nullptr) const = 0;

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAIntraFnReachability &
  createForPosition(const IRPosition &IRP, Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAIntraFnReachability"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAIntraFnReachability
  ///
  /// \return True if \p AA is a AAIntraFnReachability.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract interface for all noalias attributes.
struct AANoAlias
    : public IRAttribute<Attribute::NoAlias,
                         StateWrapper<BooleanState, AbstractAttribute>,
                         AANoAlias> {
  /// Create a AANoAlias for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AANoAlias(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.getAssociatedType()->isPointerTy())
      return false;
    return IRAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// See IRAttribute::isImpliedByIR
  ///
  /// \return True if the IR already implies the attribute for \p IRP.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  /// \param ImpliedAttributeKind The IR attribute kind being checked.
  /// \param IgnoreSubsumingPositions If true, ignore subsuming IR positions.
  LLVM_ABI static bool isImpliedByIR(Attributor &A, const IRPosition &IRP,
                                     Attribute::AttrKind ImpliedAttributeKind,
                                     bool IgnoreSubsumingPositions = false);

  /// See AbstractAttribute::requiresCallersForArgOrFunction
  ///
  /// \return True if callers are required for argument or function positions.
  static bool requiresCallersForArgOrFunction() { return true; }

  /// Return true if we assume that the underlying value is alias.
  ///
  /// \return True if we assume that the underlying value is alias.
  bool isAssumedNoAlias() const { return getAssumed(); }

  /// Return true if we know that underlying value is noalias.
  ///
  /// \return True if we know that underlying value is noalias.
  bool isKnownNoAlias() const { return getKnown(); }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AANoAlias &createForPosition(const IRPosition &IRP,
                                               Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AANoAlias"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AANoAlias
  ///
  /// \return True if \p AA is a AANoAlias.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An AbstractAttribute for nofree.
struct AANoFree
    : public IRAttribute<Attribute::NoFree,
                         StateWrapper<BooleanState, AbstractAttribute>,
                         AANoFree> {
  /// Create a AANoFree for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AANoFree(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// See IRAttribute::isImpliedByIR
  ///
  /// \return True if the IR already implies the attribute for \p IRP.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  /// \param ImpliedAttributeKind The IR attribute kind being checked.
  /// \param IgnoreSubsumingPositions If true, ignore subsuming IR positions.
  static bool isImpliedByIR(Attributor &A, const IRPosition &IRP,
                            Attribute::AttrKind ImpliedAttributeKind,
                            bool IgnoreSubsumingPositions = false) {
    // Note: This is also run for non-IPO amendable functions.
    assert(ImpliedAttributeKind == Attribute::NoFree);
    return A.hasAttr(
        IRP, {Attribute::ReadNone, Attribute::ReadOnly, Attribute::NoFree},
        IgnoreSubsumingPositions, Attribute::NoFree);
  }

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.isFunctionScope() &&
        !IRP.getAssociatedType()->isPtrOrPtrVectorTy())
      return false;
    return IRAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// Return true if "nofree" is assumed.
  ///
  /// \return True if "nofree" is assumed.
  bool isAssumedNoFree() const { return getAssumed(); }

  /// Return true if "nofree" is known.
  ///
  /// \return True if "nofree" is known.
  bool isKnownNoFree() const { return getKnown(); }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AANoFree &createForPosition(const IRPosition &IRP,
                                              Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AANoFree"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AANoFree
  ///
  /// \return True if \p AA is a AANoFree.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An AbstractAttribute for noreturn.
struct AANoReturn
    : public IRAttribute<Attribute::NoReturn,
                         StateWrapper<BooleanState, AbstractAttribute>,
                         AANoReturn> {
  /// Create a AANoReturn for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AANoReturn(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// Return true if the underlying object is assumed to never return.
  ///
  /// \return True if the underlying object is assumed to never return.
  bool isAssumedNoReturn() const { return getAssumed(); }

  /// Return true if the underlying object is known to never return.
  ///
  /// \return True if the underlying object is known to never return.
  bool isKnownNoReturn() const { return getKnown(); }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AANoReturn &createForPosition(const IRPosition &IRP,
                                                Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AANoReturn"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AANoReturn
  ///
  /// \return True if \p AA is a AANoReturn.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract interface for liveness abstract attribute.
struct AAIsDead
    : public StateWrapper<BitIntegerState<uint8_t, 3, 0>, AbstractAttribute> {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<BitIntegerState<uint8_t, 3, 0>, AbstractAttribute>;
  /// Create a AAIsDead for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAIsDead(const IRPosition &IRP, Attributor &A) : Base(IRP) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (IRP.getPositionKind() == IRPosition::IRP_FUNCTION)
      return isa<Function>(IRP.getAnchorValue()) &&
             !cast<Function>(IRP.getAnchorValue()).isDeclaration();
    return true;
  }

  /// State encoding bits. A set bit in the state means the property holds.
  enum {
    HAS_NO_EFFECT = 1 << 0,  ///< The instruction has no observable effect.
    IS_REMOVABLE = 1 << 1,  ///< The instruction may be removed.

    IS_DEAD = HAS_NO_EFFECT | IS_REMOVABLE,  ///< The instruction is dead.
  };
  static_assert(IS_DEAD == getBestState(), "Unexpected BEST_STATE value");

protected:
  /// The query functions are protected such that other attributes need to go
  /// through the Attributor interfaces: `Attributor::isAssumedDead(...)`

  /// Returns true if the underlying value is assumed dead.
  ///
  /// \return True if the underlying value is assumed dead.
  virtual bool isAssumedDead() const = 0;

  /// Returns true if the underlying value is known dead.
  ///
  /// \return True if the underlying value is known dead.
  virtual bool isKnownDead() const = 0;

  /// Returns true if \p BB is known dead.
  ///
  /// \return True if \p BB is known dead.
  ///
  /// \param BB The basic block of interest.
  virtual bool isKnownDead(const BasicBlock *BB) const = 0;

  /// Returns true if \p I is assumed dead.
  ///
  /// \return True if \p I is assumed dead.
  ///
  /// \param I The instruction of interest.
  virtual bool isAssumedDead(const Instruction *I) const = 0;

  /// Returns true if \p I is known dead.
  ///
  /// \return True if \p I is known dead.
  ///
  /// \param I The instruction of interest.
  virtual bool isKnownDead(const Instruction *I) const = 0;

  /// Return true if the underlying value is a known-removable store.
  ///
  /// This is different from dead stores as the removable store can have an
  /// effect on live values, especially loads, but that effect is propagated
  /// which allows us to remove the store in turn.
  ///
  /// \return True if the underlying value is a known-removable store.
  virtual bool isRemovableStore() const { return false; }

  /// This method is used to check if at least one instruction in a collection
  /// of instructions is live.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param begin Iterator to the start of the instruction range.
  /// \param end Iterator past the end of the instruction range.
  template <typename T> bool isLiveInstSet(T begin, T end) const {
    for (const auto &I : llvm::make_range(begin, end)) {
      assert(I->getFunction() == getIRPosition().getAssociatedFunction() &&
             "Instruction must be in the same anchor scope function.");

      if (!isAssumedDead(I))
        return true;
    }

    return false;
  }

public:
  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAIsDead &createForPosition(const IRPosition &IRP,
                                              Attributor &A);

  /// Determine if \p F might catch asynchronous exceptions.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param F The function of interest.
  static bool mayCatchAsynchronousExceptions(const Function &F) {
    return F.hasPersonalityFn() && !canSimplifyInvokeNoUnwind(&F);
  }

  /// Returns true if \p BB is assumed dead.
  ///
  /// \return True if \p BB is assumed dead.
  ///
  /// \param BB The basic block of interest.
  virtual bool isAssumedDead(const BasicBlock *BB) const = 0;

  /// Return if the edge from \p From BB to \p To BB is assumed dead.
  /// This is specifically useful in AAReachability.
  ///
  /// \return If the edge from \p From BB to \p To BB is assumed dead. This is specifically useful in AAReachability.
  ///
  /// \param From The source basic block or instruction.
  /// \param To The destination basic block or instruction.
  virtual bool isEdgeDead(const BasicBlock *From, const BasicBlock *To) const {
    return false;
  }

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAIsDead"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AAIsDead
  ///
  /// \return True if \p AA is a AAIsDead.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;

  friend struct Attributor;
};

/// State for dereferenceable attribute information.
struct DerefState : AbstractState {

  /// Return the best possible representable state.
  ///
  /// \return The best possible representable state.
  static DerefState getBestState() { return DerefState(); }
  /// Return the best state, ignoring \p Unused.
  ///
  /// \return The best state, ignoring \p Unused.
  ///
  /// \param Unused Unused; present for API uniformity with other states.
  static DerefState getBestState(const DerefState &Unused) {
    return getBestState();
  }

  /// Return the worst possible representable state.
  ///
  /// \return The worst possible representable state.
  static DerefState getWorstState() {
    DerefState DS;
    DS.indicatePessimisticFixpoint();
    return DS;
  }
  /// Return the worst state, ignoring \p Unused.
  ///
  /// \return The worst state, ignoring \p Unused.
  ///
  /// \param Unused Unused; present for API uniformity with other states.
  static DerefState getWorstState(const DerefState &Unused) {
    return getWorstState();
  }

  /// State representing for dereferenceable bytes.
  IncIntegerState<> DerefBytesState;

  /// Map from accessed memory offsets to access sizes.
  ///
  /// A key is Offset and a value is size. If there is a load/store such as
  /// p[offset]=v, (offset, sizeof(v)) is inserted into this map.
  std::map<int64_t, uint64_t> AccessedBytesMap;

  /// Helper function to calculate dereferenceable bytes from current known
  /// bytes and accessed bytes.
  ///
  /// int f(int *A){
  ///    *A = 0;
  ///    *(A+2) = 2;
  ///    *(A+1) = 1;
  ///    *(A+10) = 10;
  /// }
  /// ```
  /// In that case, AccessedBytesMap is `{0:4, 4:4, 8:4, 40:4}`.
  /// AccessedBytesMap is std::map so it is iterated in accending order on
  /// key(Offset). So KnownBytes will be updated like this:
  ///
  /// |Access | KnownBytes
  /// |(0, 4)| 0 -> 4
  /// |(4, 4)| 4 -> 8
  /// |(8, 4)| 8 -> 12
  /// |(40, 4) | 12 (break)
  void computeKnownDerefBytesFromAccessedMap() {
    int64_t KnownBytes = DerefBytesState.getKnown();
    for (auto &Access : AccessedBytesMap) {
      if (KnownBytes < Access.first)
        break;
      KnownBytes = std::max(KnownBytes, Access.first + (int64_t)Access.second);
    }

    DerefBytesState.takeKnownMaximum(KnownBytes);
  }

  /// State representing that whether the value is globaly dereferenceable.
  BooleanState GlobalState;

  /// See AbstractState::isValidState()
  ///
  /// \return True if the condition described by this query holds.
  bool isValidState() const override { return DerefBytesState.isValidState(); }

  /// See AbstractState::isAtFixpoint()
  ///
  /// \return True if the condition described by this query holds.
  bool isAtFixpoint() const override {
    return !isValidState() ||
           (DerefBytesState.isAtFixpoint() && GlobalState.isAtFixpoint());
  }

  /// See AbstractState::indicateOptimisticFixpoint(...)
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus indicateOptimisticFixpoint() override {
    DerefBytesState.indicateOptimisticFixpoint();
    GlobalState.indicateOptimisticFixpoint();
    return ChangeStatus::UNCHANGED;
  }

  /// See AbstractState::indicatePessimisticFixpoint(...)
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus indicatePessimisticFixpoint() override {
    DerefBytesState.indicatePessimisticFixpoint();
    GlobalState.indicatePessimisticFixpoint();
    return ChangeStatus::CHANGED;
  }

  /// Update known dereferenceable bytes.
  ///
  /// \param Bytes The dereferenceable byte count.
  void takeKnownDerefBytesMaximum(uint64_t Bytes) {
    DerefBytesState.takeKnownMaximum(Bytes);

    // Known bytes might increase.
    computeKnownDerefBytesFromAccessedMap();
  }

  /// Update assumed dereferenceable bytes.
  ///
  /// \param Bytes The dereferenceable byte count.
  void takeAssumedDerefBytesMinimum(uint64_t Bytes) {
    DerefBytesState.takeAssumedMinimum(Bytes);
  }

  /// Add accessed bytes to the map.
  ///
  /// \param Offset The byte offset.
  /// \param Size The byte size.
  void addAccessedBytes(int64_t Offset, uint64_t Size) {
    uint64_t &AccessedBytes = AccessedBytesMap[Offset];
    AccessedBytes = std::max(AccessedBytes, Size);

    // Known bytes might increase.
    computeKnownDerefBytesFromAccessedMap();
  }

  /// Equality for DerefState.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param R The other operand or state.
  bool operator==(const DerefState &R) const {
    return this->DerefBytesState == R.DerefBytesState &&
           this->GlobalState == R.GlobalState;
  }

  /// Inequality for DerefState.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param R The other operand or state.
  bool operator!=(const DerefState &R) const { return !(*this == R); }

  /// See IntegerStateBase::operator^=
  ///
  /// \return The computed result.
  ///
  /// \param R The other operand or state.
  DerefState operator^=(const DerefState &R) {
    DerefBytesState ^= R.DerefBytesState;
    GlobalState ^= R.GlobalState;
    return *this;
  }

  /// See IntegerStateBase::operator+=
  ///
  /// \return The computed result.
  ///
  /// \param R The other operand or state.
  DerefState operator+=(const DerefState &R) {
    DerefBytesState += R.DerefBytesState;
    GlobalState += R.GlobalState;
    return *this;
  }

  /// See IntegerStateBase::operator&=
  ///
  /// \return The computed result.
  ///
  /// \param R The other operand or state.
  DerefState operator&=(const DerefState &R) {
    DerefBytesState &= R.DerefBytesState;
    GlobalState &= R.GlobalState;
    return *this;
  }

  /// See IntegerStateBase::operator|=
  ///
  /// \return The computed result.
  ///
  /// \param R The other operand or state.
  DerefState operator|=(const DerefState &R) {
    DerefBytesState |= R.DerefBytesState;
    GlobalState |= R.GlobalState;
    return *this;
  }
};

/// An abstract interface for all dereferenceable attribute.
struct AADereferenceable
    : public IRAttribute<Attribute::Dereferenceable,
                         StateWrapper<DerefState, AbstractAttribute>,
                         AADereferenceable> {
  /// Create a AADereferenceable for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AADereferenceable(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.getAssociatedType()->isPointerTy())
      return false;
    return IRAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// Return true if we assume that underlying value is
  /// dereferenceable(_or_null) globally.
  ///
  /// \return True if we assume that underlying value is dereferenceable(_or_null) globally.
  bool isAssumedGlobal() const { return GlobalState.getAssumed(); }

  /// Return true if we know that underlying value is
  /// dereferenceable(_or_null) globally.
  ///
  /// \return True if we know that underlying value is dereferenceable(_or_null) globally.
  bool isKnownGlobal() const { return GlobalState.getKnown(); }

  /// Return assumed dereferenceable bytes.
  ///
  /// \return Assumed dereferenceable bytes.
  uint32_t getAssumedDereferenceableBytes() const {
    return DerefBytesState.getAssumed();
  }

  /// Return known dereferenceable bytes.
  ///
  /// \return Known dereferenceable bytes.
  uint32_t getKnownDereferenceableBytes() const {
    return DerefBytesState.getKnown();
  }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AADereferenceable &createForPosition(const IRPosition &IRP,
                                                       Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AADereferenceable"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AADereferenceable
  ///
  /// \return True if \p AA is a AADereferenceable.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// Integer state type used by AAAlign.
using AAAlignmentStateType =
    IncIntegerState<uint64_t, Value::MaximumAlignment, 1>;
/// An abstract interface for all align attributes.
struct AAAlign
    : public IRAttribute<Attribute::Alignment,
                         StateWrapper<AAAlignmentStateType, AbstractAttribute>,
                         AAAlign> {
  /// Create a AAAlign for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAAlign(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.getAssociatedType()->isPtrOrPtrVectorTy())
      return false;
    return IRAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// Return assumed alignment.
  ///
  /// \return Assumed alignment.
  Align getAssumedAlign() const { return Align(getAssumed()); }

  /// Return known alignment.
  ///
  /// \return Known alignment.
  Align getKnownAlign() const { return Align(getKnown()); }

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAAlign"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AAAlign
  ///
  /// \return True if \p AA is a AAAlign.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAAlign &createForPosition(const IRPosition &IRP,
                                             Attributor &A);

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// Abstract attribute tracking whether a value leaves its defining function instance.
///
/// TODO: We should make it a ternary AA tracking uniqueness, and uniqueness
/// wrt. the Attributor analysis separately.
struct AAInstanceInfo : public StateWrapper<BooleanState, AbstractAttribute> {
  /// Create a AAInstanceInfo for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAInstanceInfo(const IRPosition &IRP, Attributor &A)
      : StateWrapper<BooleanState, AbstractAttribute>(IRP) {}

  /// Return true if the underlying value is known unique for analysis.
  ///
  /// That means it might not be unique but we can still use pointer equality
  /// without risking to represent two distinct instances.
  ///
  /// \return True if the underlying value is known unique for analysis.
  bool isKnownUniqueForAnalysis() const { return isKnown(); }

  /// Return true if the underlying value is assumed unique for analysis.
  ///
  /// That means it might not be unique but we can still use pointer equality
  /// without risking to represent two distinct instances.
  ///
  /// \return True if the underlying value is assumed unique for analysis.
  bool isAssumedUniqueForAnalysis() const { return isAssumed(); }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAInstanceInfo &createForPosition(const IRPosition &IRP,
                                                    Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAInstanceInfo"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAInstanceInfo
  ///
  /// \return True if \p AA is a AAInstanceInfo.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract interface for all nocapture attributes.
struct AANoCapture
    : public IRAttribute<
          Attribute::Captures,
          StateWrapper<BitIntegerState<uint16_t, 7, 0>, AbstractAttribute>,
          AANoCapture> {
  /// Create a AANoCapture for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AANoCapture(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// See IRAttribute::isImpliedByIR
  ///
  /// \return True if the IR already implies the attribute for \p IRP.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  /// \param ImpliedAttributeKind The IR attribute kind being checked.
  /// \param IgnoreSubsumingPositions If true, ignore subsuming IR positions.
  LLVM_ABI static bool isImpliedByIR(Attributor &A, const IRPosition &IRP,
                                     Attribute::AttrKind ImpliedAttributeKind,
                                     bool IgnoreSubsumingPositions = false);

  /// Update \p State according to the capture capabilities of \p F for position
  /// \p IRP.
  ///
  /// \param IRP The IR position of interest.
  /// \param F Parameter.
  /// \param State Parameter.
  LLVM_ABI static void
  determineFunctionCaptureCapabilities(const IRPosition &IRP, const Function &F,
                                       BitIntegerState &State);

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.getAssociatedType()->isPointerTy())
      return false;
    return IRAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// State encoding bits. A set bit in the state means the property holds.
  /// NO_CAPTURE is the best possible state, 0 the worst possible state.
  enum {
    NOT_CAPTURED_IN_MEM = 1 << 0,  ///< Not captured through memory.
    NOT_CAPTURED_IN_INT = 1 << 1,  ///< Not captured through integers.
    NOT_CAPTURED_IN_RET = 1 << 2,  ///< Not captured through the return value.

    /// If we do not capture the value in memory or through integers we can only
    /// communicate it back as a derived pointer.
    NO_CAPTURE_MAYBE_RETURNED = NOT_CAPTURED_IN_MEM | NOT_CAPTURED_IN_INT,

    /// If we do not capture the value in memory, through integers, or as a
    /// derived pointer we know it is not captured.
    NO_CAPTURE =
        NOT_CAPTURED_IN_MEM | NOT_CAPTURED_IN_INT | NOT_CAPTURED_IN_RET,
  };

  /// Return true if we know that the underlying value is not captured in its
  /// respective scope.
  ///
  /// \return True if we know that the underlying value is not captured in its respective scope.
  bool isKnownNoCapture() const { return isKnown(NO_CAPTURE); }

  /// Return true if we assume that the underlying value is not captured in its
  /// respective scope.
  ///
  /// \return True if we assume that the underlying value is not captured in its respective scope.
  bool isAssumedNoCapture() const { return isAssumed(NO_CAPTURE); }

  /// Return true if we know that the underlying value is not captured in its
  /// respective scope but we allow it to escape through a "return".
  ///
  /// \return True if we know that the underlying value is not captured in its respective scope but we allow it to escape through a "return".
  bool isKnownNoCaptureMaybeReturned() const {
    return isKnown(NO_CAPTURE_MAYBE_RETURNED);
  }

  /// Return true if we assume that the underlying value is not captured in its
  /// respective scope but we allow it to escape through a "return".
  ///
  /// \return True if we assume that the underlying value is not captured in its respective scope but we allow it to escape through a "return".
  bool isAssumedNoCaptureMaybeReturned() const {
    return isAssumed(NO_CAPTURE_MAYBE_RETURNED);
  }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AANoCapture &createForPosition(const IRPosition &IRP,
                                                 Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AANoCapture"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AANoCapture
  ///
  /// \return True if \p AA is a AANoCapture.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// State representing an assumed simplified IR value.
struct ValueSimplifyStateType : public AbstractState {

  /// Construct a value-simplify state.
  ///
  /// \param Ty The type of the value being simplified.
  ValueSimplifyStateType(Type *Ty) : Ty(Ty) {}

  /// Return the best possible representable state.
  ///
  /// \return The best possible representable state.
  ///
  /// \param Ty The type associated with the state.
  static ValueSimplifyStateType getBestState(Type *Ty) {
    return ValueSimplifyStateType(Ty);
  }
  /// Return the best state for the type of \p VS.
  ///
  /// \return The best state for the type of \p VS.
  ///
  /// \param VS The value-simplify state whose type is used.
  static ValueSimplifyStateType getBestState(const ValueSimplifyStateType &VS) {
    return getBestState(VS.Ty);
  }

  /// Return the worst possible representable state.
  ///
  /// \return The worst possible representable state.
  ///
  /// \param Ty The type associated with the state.
  static ValueSimplifyStateType getWorstState(Type *Ty) {
    ValueSimplifyStateType DS(Ty);
    DS.indicatePessimisticFixpoint();
    return DS;
  }
  /// Return the worst state for the type of \p VS.
  ///
  /// \return The worst state for the type of \p VS.
  ///
  /// \param VS The value-simplify state whose type is used.
  static ValueSimplifyStateType
  getWorstState(const ValueSimplifyStateType &VS) {
    return getWorstState(VS.Ty);
  }

  /// See AbstractState::isValidState(...)
  ///
  /// \return True if the condition described by this query holds.
  bool isValidState() const override { return BS.isValidState(); }

  /// See AbstractState::isAtFixpoint(...)
  ///
  /// \return True if the condition described by this query holds.
  bool isAtFixpoint() const override { return BS.isAtFixpoint(); }

  /// Return the assumed value-simplify state.
  ///
  /// \return The assumed value-simplify state.
  ValueSimplifyStateType getAssumed() { return *this; }
  /// Return the assumed value-simplify state.
  ///
  /// \return The assumed value-simplify state.
  const ValueSimplifyStateType &getAssumed() const { return *this; }

  /// See AbstractState::indicatePessimisticFixpoint(...)
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus indicatePessimisticFixpoint() override {
    return BS.indicatePessimisticFixpoint();
  }

  /// See AbstractState::indicateOptimisticFixpoint(...)
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus indicateOptimisticFixpoint() override {
    return BS.indicateOptimisticFixpoint();
  }

  /// Clamp this state with \p VS.
  ///
  /// Only information assumed in both states remains assumed afterwards.
  ///
  /// \return The computed result.
  ///
  /// \param VS The other value-simplify state.
  ValueSimplifyStateType operator^=(const ValueSimplifyStateType &VS) {
    BS ^= VS.BS;
    unionAssumed(VS.SimplifiedAssociatedValue);
    return *this;
  }

  /// Return true if this equals \p RHS.
  ///
  /// \return True if this equals \p RHS.
  ///
  /// \param RHS The other operand or state.
  bool operator==(const ValueSimplifyStateType &RHS) const {
    if (isValidState() != RHS.isValidState())
      return false;
    if (!isValidState() && !RHS.isValidState())
      return true;
    return SimplifiedAssociatedValue == RHS.SimplifiedAssociatedValue;
  }

protected:
  /// The type of the original value.
  Type *Ty;

  /// Merge \p Other into the currently assumed simplified value
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param Other The other simplified value.
  LLVM_ABI bool unionAssumed(std::optional<Value *> Other);

  /// Helper to track validity and fixpoint
  BooleanState BS;

  /// Assumed simplified value for the associated IR value.
  ///
  /// Initially, it is set to std::nullopt, which means that the value is not
  /// clear under current assumption. If in the pessimistic state,
  /// getAssumedSimplifiedValue doesn't return this value but returns the
  /// original associated value.
  std::optional<Value *> SimplifiedAssociatedValue;
};

/// An abstract interface for value simplify abstract attribute.
struct AAValueSimplify
    : public StateWrapper<ValueSimplifyStateType, AbstractAttribute, Type *> {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<ValueSimplifyStateType, AbstractAttribute, Type *>;
  /// Create a AAValueSimplify for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAValueSimplify(const IRPosition &IRP, Attributor &A)
      : Base(IRP, IRP.getAssociatedType()) {}

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAValueSimplify &createForPosition(const IRPosition &IRP,
                                                     Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAValueSimplify"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAValueSimplify
  ///
  /// \return True if \p AA is a AAValueSimplify.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;

private:
  /// Return an assumed simplified value if a single candidate is found. If
  /// there cannot be one, return original value. If it is not clear yet, return
  /// std::nullopt.
  ///
  /// Use `Attributor::getAssumedSimplified` for value simplification.
  ///
  /// \return An assumed simplified value if a single candidate is found.
  virtual std::optional<Value *>
  getAssumedSimplifiedValue(Attributor &A) const = 0;

  friend struct Attributor;
};

/// Abstract attribute for heap-to-stack conversion opportunities.
struct AAHeapToStack : public StateWrapper<BooleanState, AbstractAttribute> {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<BooleanState, AbstractAttribute>;
  /// Create a AAHeapToStack for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAHeapToStack(const IRPosition &IRP, Attributor &A) : Base(IRP) {}

  /// Returns true if HeapToStack conversion is assumed to be possible.
  ///
  /// \return True if HeapToStack conversion is assumed to be possible.
  ///
  /// \param CB The call base of interest.
  virtual bool isAssumedHeapToStack(const CallBase &CB) const = 0;

  /// Returns true if HeapToStack conversion is assumed and the CB is a
  /// callsite to a free operation to be removed.
  ///
  /// \return True if HeapToStack conversion is assumed and the CB is a callsite to a free operation to be removed.
  ///
  /// \param CB The call base of interest.
  virtual bool isAssumedHeapToStackRemovedFree(CallBase &CB) const = 0;

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAHeapToStack &createForPosition(const IRPosition &IRP,
                                                   Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAHeapToStack"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AAHeapToStack
  ///
  /// \return True if \p AA is a AAHeapToStack.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract interface for privatizability.
///
/// A pointer is privatizable if it can be replaced by a new, private one.
/// Privatizing pointer reduces the use count, interaction between unrelated
/// code parts.
///
/// In order for a pointer to be privatizable its value cannot be observed
/// (=nocapture), it is (for now) not written (=readonly & noalias), we know
/// what values are necessary to make the private copy look like the original
/// one, and the values we need can be loaded (=dereferenceable).
struct AAPrivatizablePtr
    : public StateWrapper<BooleanState, AbstractAttribute> {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<BooleanState, AbstractAttribute>;
  /// Create a AAPrivatizablePtr for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAPrivatizablePtr(const IRPosition &IRP, Attributor &A) : Base(IRP) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.getAssociatedType()->isPtrOrPtrVectorTy())
      return false;
    return AbstractAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// Returns true if pointer privatization is assumed to be possible.
  ///
  /// \return True if pointer privatization is assumed to be possible.
  bool isAssumedPrivatizablePtr() const { return getAssumed(); }

  /// Returns true if pointer privatization is known to be possible.
  ///
  /// \return True if pointer privatization is known to be possible.
  bool isKnownPrivatizablePtr() const { return getKnown(); }

  /// See AbstractAttribute::requiresCallersForArgOrFunction
  ///
  /// \return True if callers are required for argument or function positions.
  static bool requiresCallersForArgOrFunction() { return true; }

  /// Return the type we can choose for a private copy of the underlying
  /// value. std::nullopt means it is not clear yet, nullptr means there is
  /// none.
  ///
  /// \return The type we can choose for a private copy of the underlying value. std::nullopt means it is not clear yet, nullptr means there is none.
  virtual std::optional<Type *> getPrivatizableType() const = 0;

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAPrivatizablePtr &createForPosition(const IRPosition &IRP,
                                                       Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAPrivatizablePtr"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAPricatizablePtr
  ///
  /// \return True if \p AA is a AAPricatizablePtr.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract interface for memory access kind related attributes
/// (readnone/readonly/writeonly).
struct AAMemoryBehavior
    : public IRAttribute<
          Attribute::None,
          StateWrapper<BitIntegerState<uint8_t, 3>, AbstractAttribute>,
          AAMemoryBehavior> {
  /// Create a AAMemoryBehavior for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAMemoryBehavior(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// See AbstractAttribute::hasTrivialInitializer.
  ///
  /// \return True if this attribute has a trivial initializer.
  static bool hasTrivialInitializer() { return false; }

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.isFunctionScope() && !IRP.getAssociatedType()->isPointerTy())
      return false;
    return IRAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// State encoding bits. A set bit in the state means the property holds.
  /// BEST_STATE is the best possible state, 0 the worst possible state.
  enum {
    NO_READS = 1 << 0,  ///< No memory reads.
    NO_WRITES = 1 << 1,  ///< No memory writes.
    NO_ACCESSES = NO_READS | NO_WRITES,  ///< No memory accesses.

    BEST_STATE = NO_ACCESSES,  ///< Best (most optimistic) memory-behavior state.
  };
  static_assert(BEST_STATE == getBestState(), "Unexpected BEST_STATE value");

  /// Return true if we know that the underlying value is not read or accessed
  /// in its respective scope.
  ///
  /// \return True if we know that the underlying value is not read or accessed in its respective scope.
  bool isKnownReadNone() const { return isKnown(NO_ACCESSES); }

  /// Return true if we assume that the underlying value is not read or accessed
  /// in its respective scope.
  ///
  /// \return True if we assume that the underlying value is not read or accessed in its respective scope.
  bool isAssumedReadNone() const { return isAssumed(NO_ACCESSES); }

  /// Return true if we know that the underlying value is not accessed
  /// (=written) in its respective scope.
  ///
  /// \return True if we know that the underlying value is not accessed (=written) in its respective scope.
  bool isKnownReadOnly() const { return isKnown(NO_WRITES); }

  /// Return true if we assume that the underlying value is not accessed
  /// (=written) in its respective scope.
  ///
  /// \return True if we assume that the underlying value is not accessed (=written) in its respective scope.
  bool isAssumedReadOnly() const { return isAssumed(NO_WRITES); }

  /// Return true if we know that the underlying value is not read in its
  /// respective scope.
  ///
  /// \return True if we know that the underlying value is not read in its respective scope.
  bool isKnownWriteOnly() const { return isKnown(NO_READS); }

  /// Return true if we assume that the underlying value is not read in its
  /// respective scope.
  ///
  /// \return True if we assume that the underlying value is not read in its respective scope.
  bool isAssumedWriteOnly() const { return isAssumed(NO_READS); }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAMemoryBehavior &createForPosition(const IRPosition &IRP,
                                                      Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAMemoryBehavior"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAMemoryBehavior
  ///
  /// \return True if \p AA is a AAMemoryBehavior.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract interface for all memory location attributes
/// (readnone/argmemonly/inaccessiblememonly/inaccessibleorargmemonly).
struct AAMemoryLocation
    : public IRAttribute<
          Attribute::None,
          StateWrapper<BitIntegerState<uint32_t, 511>, AbstractAttribute>,
          AAMemoryLocation> {
  /// Bitmask type encoding memory location kinds.
  using MemoryLocationsKind = StateType::base_t;

  /// Create a AAMemoryLocation for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAMemoryLocation(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// See AbstractAttribute::requiresCalleeForCallBase.
  ///
  /// \return True if a callee is required for call-base positions.
  static bool requiresCalleeForCallBase() { return true; }

  /// See AbstractAttribute::hasTrivialInitializer.
  ///
  /// \return True if this attribute has a trivial initializer.
  static bool hasTrivialInitializer() { return false; }

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.isFunctionScope() &&
        !IRP.getAssociatedType()->isPtrOrPtrVectorTy())
      return false;
    return IRAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// Encoding of different locations that could be accessed by a memory
  /// access.
  enum {
    ALL_LOCATIONS = 0,  ///< May access all memory locations.
    NO_LOCAL_MEM = 1 << 0,  ///< Does not access local (stack) memory.
    NO_CONST_MEM = 1 << 1,  ///< Does not access constant memory.
    NO_GLOBAL_INTERNAL_MEM = 1 << 2,  ///< Does not access internal global memory.
    NO_GLOBAL_EXTERNAL_MEM = 1 << 3,  ///< Does not access external global memory.
    NO_GLOBAL_MEM = NO_GLOBAL_INTERNAL_MEM | NO_GLOBAL_EXTERNAL_MEM,  ///< Does not access global memory.
    NO_ARGUMENT_MEM = 1 << 4,  ///< Does not access argument memory.
    NO_INACCESSIBLE_MEM = 1 << 5,  ///< Does not access inaccessible memory.
    NO_MALLOCED_MEM = 1 << 6,  ///< Does not access malloc'ed memory.
    NO_UNKOWN_MEM = 1 << 7,  ///< Does not access unknown memory.
    NO_LOCATIONS = NO_LOCAL_MEM | NO_CONST_MEM | NO_GLOBAL_INTERNAL_MEM |  ///< Does not access any memory locations.
                   NO_GLOBAL_EXTERNAL_MEM | NO_ARGUMENT_MEM |  ///< Does not access external global memory.
                   NO_INACCESSIBLE_MEM | NO_MALLOCED_MEM | NO_UNKOWN_MEM,  ///< Does not access inaccessible memory.

    // Helper bit to track if we gave up or not.
    VALID_STATE = NO_LOCATIONS + 1,  ///< Bit indicating the state is valid.

    BEST_STATE = NO_LOCATIONS | VALID_STATE,  ///< Best (most optimistic) memory-location state.
  };
  static_assert(BEST_STATE == getBestState(), "Unexpected BEST_STATE value");

  /// Return true if we know that the associated functions has no observable
  /// accesses.
  ///
  /// \return True if we know that the associated functions has no observable accesses.
  bool isKnownReadNone() const { return isKnown(NO_LOCATIONS); }

  /// Return true if we assume that the associated functions has no observable
  /// accesses.
  ///
  /// \return True if we assume that the associated functions has no observable accesses.
  bool isAssumedReadNone() const {
    return isAssumed(NO_LOCATIONS) || isAssumedStackOnly();
  }

  /// Return true if we know that the associated functions has at most
  /// local/stack accesses.
  ///
  /// \return True if we know that the associated functions has at most local/stack accesses.
  bool isKnowStackOnly() const {
    return isKnown(inverseLocation(NO_LOCAL_MEM, true, true));
  }

  /// Return true if we assume that the associated functions has at most
  /// local/stack accesses.
  ///
  /// \return True if we assume that the associated functions has at most local/stack accesses.
  bool isAssumedStackOnly() const {
    return isAssumed(inverseLocation(NO_LOCAL_MEM, true, true));
  }

  /// Return true if we know that the underlying value will only access
  /// inaccesible memory only (see Attribute::InaccessibleMemOnly).
  ///
  /// \return True if we know that the underlying value will only access inaccesible memory only (see Attribute::InaccessibleMemOnly).
  bool isKnownInaccessibleMemOnly() const {
    return isKnown(inverseLocation(NO_INACCESSIBLE_MEM, true, true));
  }

  /// Return true if we assume that the underlying value will only access
  /// inaccesible memory only (see Attribute::InaccessibleMemOnly).
  ///
  /// \return True if we assume that the underlying value will only access inaccesible memory only (see Attribute::InaccessibleMemOnly).
  bool isAssumedInaccessibleMemOnly() const {
    return isAssumed(inverseLocation(NO_INACCESSIBLE_MEM, true, true));
  }

  /// Return true if we know that the underlying value will only access
  /// argument pointees (see Attribute::ArgMemOnly).
  ///
  /// \return True if we know that the underlying value will only access argument pointees (see Attribute::ArgMemOnly).
  bool isKnownArgMemOnly() const {
    return isKnown(inverseLocation(NO_ARGUMENT_MEM, true, true));
  }

  /// Return true if we assume that the underlying value will only access
  /// argument pointees (see Attribute::ArgMemOnly).
  ///
  /// \return True if we assume that the underlying value will only access argument pointees (see Attribute::ArgMemOnly).
  bool isAssumedArgMemOnly() const {
    return isAssumed(inverseLocation(NO_ARGUMENT_MEM, true, true));
  }

  /// Return true if we know that the underlying value will only access
  /// inaccesible memory or argument pointees (see
  /// Attribute::InaccessibleOrArgMemOnly).
  ///
  /// \return True if we know that the underlying value will only access inaccesible memory or argument pointees (see Attribute::InaccessibleOrArgMemOnly).
  bool isKnownInaccessibleOrArgMemOnly() const {
    return isKnown(
        inverseLocation(NO_INACCESSIBLE_MEM | NO_ARGUMENT_MEM, true, true));
  }

  /// Return true if we assume that the underlying value will only access
  /// inaccesible memory or argument pointees (see
  /// Attribute::InaccessibleOrArgMemOnly).
  ///
  /// \return True if we assume that the underlying value will only access inaccesible memory or argument pointees (see Attribute::InaccessibleOrArgMemOnly).
  bool isAssumedInaccessibleOrArgMemOnly() const {
    return isAssumed(
        inverseLocation(NO_INACCESSIBLE_MEM | NO_ARGUMENT_MEM, true, true));
  }

  /// Return true if the underlying value may access memory through arguement
  /// pointers of the associated function, if any.
  ///
  /// \return True if the underlying value may access memory through arguement pointers of the associated function, if any.
  bool mayAccessArgMem() const { return !isAssumed(NO_ARGUMENT_MEM); }

  /// Return true if only the memory locations specififed by \p MLK are assumed
  /// to be accessed by the associated function.
  ///
  /// \return True if only the memory locations specififed by \p MLK are assumed to be accessed by the associated function.
  ///
  /// \param MLK The memory-location kind bitmask.
  bool isAssumedSpecifiedMemOnly(MemoryLocationsKind MLK) const {
    return isAssumed(MLK);
  }

  /// Return the locations that are assumed to be not accessed by the associated
  /// function, if any.
  ///
  /// \return The locations that are assumed to be not accessed by the associated function, if any.
  MemoryLocationsKind getAssumedNotAccessedLocation() const {
    return getAssumed();
  }

  /// Return the inverse of memory-location kind \p Loc.
  ///
  /// For NO_XXX the return describes ONLY_XXX. The flags \p AndLocalMem and
  /// \p AndConstMem determine if local (=stack) and constant memory are
  /// allowed as well. Most of the time we do want them to be included, e.g.,
  /// argmemonly allows accesses via argument pointers or local or constant
  /// memory accesses.
  ///
  /// \return The inverse of memory-location kind \p Loc.
  ///
  /// \param Loc The memory-location kind to invert.
  /// \param AndLocalMem If true, allow local (stack) memory in the result.
  /// \param AndConstMem If true, allow constant memory in the result.
  static MemoryLocationsKind
  inverseLocation(MemoryLocationsKind Loc, bool AndLocalMem, bool AndConstMem) {
    return NO_LOCATIONS & ~(Loc | (AndLocalMem ? NO_LOCAL_MEM : 0) |
                            (AndConstMem ? NO_CONST_MEM : 0));
  };

  /// Return the locations encoded by \p MLK as a readable string.
  ///
  /// \return The locations encoded by \p MLK as a readable string.
  ///
  /// \param MLK The memory-location kind bitmask.
  LLVM_ABI static std::string getMemoryLocationsAsStr(MemoryLocationsKind MLK);

  /// Simple enum to distinguish read/write/read-write accesses.
  enum AccessKind {
    NONE = 0,  ///< No access.
    READ = 1 << 0,  ///< Read access.
    WRITE = 1 << 1,  ///< Write access.
    READ_WRITE = READ | WRITE,  ///< Read and write access.
  };

  /// Check \p Pred on all accesses to the memory kinds specified by \p MLK.
  ///
  /// This method will evaluate \p Pred on all accesses (access instruction +
  /// underlying accessed memory pointer) and it will return true if \p Pred
  /// holds every time.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param Pred The callback predicate applied to matching items.
  /// \param MLK The memory-location kind bitmask.
  virtual bool checkForAllAccessesToMemoryKind(
      function_ref<bool(const Instruction *, const Value *, AccessKind,
                        MemoryLocationsKind)>
          Pred,
      MemoryLocationsKind MLK) const = 0;

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAMemoryLocation &createForPosition(const IRPosition &IRP,
                                                      Attributor &A);

  /// See AbstractState::getAsStr(Attributor).
  ///
  /// \return A pointer to the requested object, or null if unavailable.
  ///
  /// \param A The Attributor managing the analysis.
  const std::string getAsStr(Attributor *A) const override {
    return getMemoryLocationsAsStr(getAssumedNotAccessedLocation());
  }

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAMemoryLocation"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAMemoryLocation
  ///
  /// \return True if \p AA is a AAMemoryLocation.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract interface for range value analysis.
struct AAValueConstantRange
    : public StateWrapper<IntegerRangeState, AbstractAttribute, uint32_t> {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<IntegerRangeState, AbstractAttribute, uint32_t>;
  /// Create a AAValueConstantRange for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAValueConstantRange(const IRPosition &IRP, Attributor &A)
      : Base(IRP, IRP.getAssociatedType()->getIntegerBitWidth()) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.getAssociatedType()->isIntegerTy())
      return false;
    return AbstractAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// See AbstractAttribute::requiresCallersForArgOrFunction
  ///
  /// \return True if callers are required for argument or function positions.
  static bool requiresCallersForArgOrFunction() { return true; }

  /// See AbstractAttribute::getState(...).
  ///
  /// \return The abstract state associated with this attribute.
  IntegerRangeState &getState() override { return *this; }
  /// See AbstractAttribute::getState(...).
  ///
  /// \return The abstract state associated with this attribute.
  const IntegerRangeState &getState() const override { return *this; }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAValueConstantRange &createForPosition(const IRPosition &IRP,
                                                          Attributor &A);

  /// Return an assumed range for the associated value at program point \p CtxI.
  ///
  /// If \p CtxI is nullptr, simply return an assumed range.
  ///
  /// \return An assumed range for the associated value at program point \p CtxI.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param CtxI Optional context instruction for the query.
  virtual ConstantRange
  getAssumedConstantRange(Attributor &A,
                          const Instruction *CtxI = nullptr) const = 0;

  /// Return a known range for the associated value at program point \p CtxI.
  ///
  /// If \p CtxI is nullptr, simply return a known range.
  ///
  /// \return A known range for the associated value at program point \p CtxI.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param CtxI Optional context instruction for the query.
  virtual ConstantRange
  getKnownConstantRange(Attributor &A,
                        const Instruction *CtxI = nullptr) const = 0;

  /// Return an assumed constant for the associated value at program point \p CtxI.
  ///
  /// \return An assumed constant for the associated value at program point \p CtxI.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param CtxI Optional context instruction for the query.
  std::optional<Constant *>
  getAssumedConstant(Attributor &A, const Instruction *CtxI = nullptr) const {
    ConstantRange RangeV = getAssumedConstantRange(A, CtxI);
    if (auto *C = RangeV.getSingleElement()) {
      Type *Ty = getAssociatedValue().getType();
      return cast_or_null<Constant>(
          AA::getWithType(*ConstantInt::get(Ty->getContext(), *C), *Ty));
    }
    if (RangeV.isEmptySet())
      return std::nullopt;
    return nullptr;
  }

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAValueConstantRange"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAValueConstantRange
  ///
  /// \return True if \p AA is a AAValueConstantRange.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// Set-based abstract state for potential values.
///
/// The assumed boolean state indicates whether the corresponding set is full
/// set or not. If the assumed state is false, this is the worst state. The
/// worst state (invalid state) of set of potential values is when the set
/// contains every possible value (i.e. we cannot in any way limit the value
/// that the target position can take). That never happens naturally, we only
/// force it. As for the conditions under which we force it, see
/// AAPotentialConstantValues.
template <typename MemberTy> struct PotentialValuesState : AbstractState {
  /// Underlying set type for potential values.
  using SetTy = SmallSetVector<MemberTy, 8>;

  /// Construct a potential-values state.
  PotentialValuesState() : IsValidState(true), UndefIsContained(false) {}

  /// Construct a potential-values state with explicit validity.
  ///
  /// \param IsValid Whether the state starts in a valid (non-universal) set.
  PotentialValuesState(bool IsValid)
      : IsValidState(IsValid), UndefIsContained(false) {}

  /// See AbstractState::isValidState(...)
  ///
  /// \return True if the condition described by this query holds.
  bool isValidState() const override { return IsValidState.isValidState(); }

  /// See AbstractState::isAtFixpoint(...)
  ///
  /// \return True if the condition described by this query holds.
  bool isAtFixpoint() const override { return IsValidState.isAtFixpoint(); }

  /// See AbstractState::indicatePessimisticFixpoint(...)
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus indicatePessimisticFixpoint() override {
    return IsValidState.indicatePessimisticFixpoint();
  }

  /// See AbstractState::indicateOptimisticFixpoint(...)
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus indicateOptimisticFixpoint() override {
    return IsValidState.indicateOptimisticFixpoint();
  }

  /// Return the assumed potential-values state.
  ///
  /// \return The assumed potential-values state.
  PotentialValuesState &getAssumed() { return *this; }
  /// Return the assumed potential-values state.
  ///
  /// \return The assumed potential-values state.
  const PotentialValuesState &getAssumed() const { return *this; }

  /// Return this set. We should check whether this set is valid or not by
  /// isValidState() before calling this function.
  ///
  /// \return This set. We should check whether this set is valid or not by isValidState() before calling this function.
  const SetTy &getAssumedSet() const {
    assert(isValidState() && "This set shoud not be used when it is invalid!");
    return Set;
  }

  /// Returns whether this state contains an undef value or not.
  ///
  /// \return Whether this state contains an undef value or not.
  bool undefIsContained() const {
    assert(isValidState() && "This flag shoud not be used when it is invalid!");
    return UndefIsContained;
  }

  /// Return true if this equals \p RHS.
  ///
  /// \return True if this equals \p RHS.
  ///
  /// \param RHS The other operand or state.
  bool operator==(const PotentialValuesState &RHS) const {
    if (isValidState() != RHS.isValidState())
      return false;
    if (!isValidState() && !RHS.isValidState())
      return true;
    if (undefIsContained() != RHS.undefIsContained())
      return false;
    return Set == RHS.getAssumedSet();
  }

  /// Maximum number of potential values to be tracked.
  /// This is set by -attributor-max-potential-values command line option
  static unsigned MaxPotentialValues;

  /// Return empty set as the best state of potential values.
  ///
  /// \return Empty set as the best state of potential values.
  static PotentialValuesState getBestState() {
    return PotentialValuesState(true);
  }

  /// Return the best state, ignoring \p PVS.
  ///
  /// \return The best state, ignoring \p PVS.
  ///
  /// \param PVS Unused; present for API uniformity with other states.
  static PotentialValuesState getBestState(const PotentialValuesState &PVS) {
    return getBestState();
  }

  /// Return full set as the worst state of potential values.
  ///
  /// \return Full set as the worst state of potential values.
  static PotentialValuesState getWorstState() {
    return PotentialValuesState(false);
  }

  /// Return the worst state, ignoring \p PVS.
  ///
  /// \return The worst state, ignoring \p PVS.
  ///
  /// \param PVS Unused; present for API uniformity with other states.
  static PotentialValuesState getWorstState(const PotentialValuesState &PVS) {
    return getWorstState();
  }

  /// Union assumed set with the passed value.
  ///
  /// \param C The candidate value to union.
  void unionAssumed(const MemberTy &C) { insert(C); }

  /// Union assumed set with assumed set of the passed state \p PVS.
  ///
  /// \param PVS The other potential-values state.
  void unionAssumed(const PotentialValuesState &PVS) { unionWith(PVS); }

  /// Union assumed set with an undef value.
  void unionAssumedWithUndef() { unionWithUndef(); }

  /// Clamp this state with \p PVS.
  ///
  /// Only information assumed in both states remains assumed afterwards.
  ///
  /// \return The computed result.
  ///
  /// \param PVS The other potential-values state.
  PotentialValuesState operator^=(const PotentialValuesState &PVS) {
    IsValidState ^= PVS.IsValidState;
    unionAssumed(PVS);
    return *this;
  }

  /// Intersect assumed information with \p PVS.
  ///
  /// \return The computed result.
  ///
  /// \param PVS The other potential-values state.
  PotentialValuesState operator&=(const PotentialValuesState &PVS) {
    IsValidState &= PVS.IsValidState;
    unionAssumed(PVS);
    return *this;
  }

  /// Return true if the assumed set contains the value.
  ///
  /// \return True if the assumed set contains the value.
  ///
  /// \param V The value to test for membership.
  bool contains(const MemberTy &V) const {
    return !isValidState() ? true : Set.contains(V);
  }

protected:
  /// Return the mutable assumed set of potential values.
  ///
  /// \return The mutable assumed set of potential values.
  SetTy &getAssumedSet() {
    assert(isValidState() && "This set shoud not be used when it is invalid!");
    return Set;
  }

private:
  /// Check the size of this set, and invalidate when the size is no
  /// less than \p MaxPotentialValues threshold.
  void checkAndInvalidate() {
    if (Set.size() >= MaxPotentialValues)
      indicatePessimisticFixpoint();
    else
      reduceUndefValue();
  }

  /// If this state contains both undef and not undef, we can reduce
  /// undef to the not undef value.
  void reduceUndefValue() { UndefIsContained = UndefIsContained & Set.empty(); }

  /// Insert an element into this set.
  void insert(const MemberTy &C) {
    if (!isValidState())
      return;
    Set.insert(C);
    checkAndInvalidate();
  }

  /// Take union with R.
  void unionWith(const PotentialValuesState &R) {
    /// If this is a full set, do nothing.
    if (!isValidState())
      return;
    /// If R is full set, change L to a full set.
    if (!R.isValidState()) {
      indicatePessimisticFixpoint();
      return;
    }
    Set.insert_range(R.Set);
    UndefIsContained |= R.undefIsContained();
    checkAndInvalidate();
  }

  /// Take union with an undef value.
  void unionWithUndef() {
    UndefIsContained = true;
    reduceUndefValue();
  }

  /// Take intersection with R.
  void intersectWith(const PotentialValuesState &R) {
    /// If R is a full set, do nothing.
    if (!R.isValidState())
      return;
    /// If this is a full set, change this to R.
    if (!isValidState()) {
      *this = R;
      return;
    }
    SetTy IntersectSet;
    for (const MemberTy &C : Set) {
      if (R.Set.count(C))
        IntersectSet.insert(C);
    }
    Set = IntersectSet;
    UndefIsContained &= R.undefIsContained();
    reduceUndefValue();
  }

  /// A helper state which indicate whether this state is valid or not.
  BooleanState IsValidState;

  /// Container for potential values
  SetTy Set;

  /// Flag for undef value
  bool UndefIsContained;
};

/// State for denormal floating-point math modes.
struct DenormalFPMathState : public AbstractState {
  /// Known denormal FP math mode configuration.
  struct DenormalState {
    /// Denormal mode for non-f32 floating-point types.
    ///
    /// \return The computed result.
    DenormalMode Mode = DenormalMode::getInvalid();
    /// Denormal mode for f32 floating-point type.
    ///
    /// \return The computed result.
    DenormalMode ModeF32 = DenormalMode::getInvalid();

    /// Return true if this equals \p Other.
    ///
    /// \return True if this equals \p Other.
    ///
    /// \param Other The other denormal state.
    bool operator==(const DenormalState Other) const {
      return Mode == Other.Mode && ModeF32 == Other.ModeF32;
    }

    /// Return true if this differs from \p Other.
    ///
    /// \return True if this differs from \p Other.
    ///
    /// \param Other The other denormal state.
    bool operator!=(const DenormalState Other) const {
      return Mode != Other.Mode || ModeF32 != Other.ModeF32;
    }

    /// Return true if this denormal state is valid.
    ///
    /// \return True if this denormal state is valid.
    bool isValid() const { return Mode.isValid() && ModeF32.isValid(); }

    static DenormalMode::DenormalModeKind
    /// Union two denormal-mode kind encodings.
    ///
    /// \return The computed result.
    ///
    /// \param Callee The callee-side denormal kind.
    /// \param Caller The caller-side denormal kind.
    unionDenormalKind(DenormalMode::DenormalModeKind Callee,
                      DenormalMode::DenormalModeKind Caller) {
      if (Caller == Callee)
        return Caller;
      if (Callee == DenormalMode::Dynamic)
        return Caller;
      if (Caller == DenormalMode::Dynamic)
        return Callee;
      return DenormalMode::Invalid;
    }

    /// Union assumed denormal modes from callee and caller.
    ///
    /// \return The computed result.
    ///
    /// \param Callee The callee-side denormal mode.
    /// \param Caller The caller-side denormal mode.
    static DenormalMode unionAssumed(DenormalMode Callee, DenormalMode Caller) {
      return DenormalMode{unionDenormalKind(Callee.Output, Caller.Output),
                          unionDenormalKind(Callee.Input, Caller.Input)};
    }

    /// Union this denormal state with caller-side information.
    ///
    /// \return The computed result.
    ///
    /// \param Caller The caller-side denormal state.
    DenormalState unionWith(DenormalState Caller) const {
      DenormalState Callee(*this);
      Callee.Mode = unionAssumed(Callee.Mode, Caller.Mode);
      Callee.ModeF32 = unionAssumed(Callee.ModeF32, Caller.ModeF32);
      return Callee;
    }
  };

  /// Known denormal FP math state.
  DenormalState Known;

  /// Explicitly track whether we've hit a fixed point.
  bool IsAtFixedpoint = false;

  /// Construct a denormal FP math state.
  DenormalFPMathState() = default;

  /// Return the known denormal FP math state.
  ///
  /// \return The known denormal FP math state.
  DenormalState getKnown() const { return Known; }

  /// Return the assumed denormal FP math state (same as known).
  ///
  /// \return The assumed denormal FP math state (same as known).
  DenormalState getAssumed() const { return Known; }

  /// See AbstractState::isValidState().
  ///
  /// \return True if the condition described by this query holds.
  bool isValidState() const override { return Known.isValid(); }

  /// Return true if there are no dynamic components to the denormal mode worth
  /// specializing.
  ///
  /// \return True if there are no dynamic components to the denormal mode worth specializing.
  bool isModeFixed() const {
    return Known.Mode.Input != DenormalMode::Dynamic &&
           Known.Mode.Output != DenormalMode::Dynamic &&
           Known.ModeF32.Input != DenormalMode::Dynamic &&
           Known.ModeF32.Output != DenormalMode::Dynamic;
  }

  /// See AbstractState::isAtFixpoint().
  ///
  /// \return True if the condition described by this query holds.
  bool isAtFixpoint() const override { return IsAtFixedpoint; }

  /// Indicate that the state has reached a fixpoint.
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus indicateFixpoint() {
    bool Changed = !IsAtFixedpoint;
    IsAtFixedpoint = true;
    return Changed ? ChangeStatus::CHANGED : ChangeStatus::UNCHANGED;
  }

  /// See AbstractState::indicateOptimisticFixpoint(...).
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus indicateOptimisticFixpoint() override {
    return indicateFixpoint();
  }

  /// See AbstractState::indicatePessimisticFixpoint(...).
  ///
  /// \return Whether the state or IR changed.
  ChangeStatus indicatePessimisticFixpoint() override {
    return indicateFixpoint();
  }

  /// Clamp this state with caller-side denormal information.
  ///
  /// \return The computed result.
  ///
  /// \param Caller The caller-side denormal FP math state.
  DenormalFPMathState operator^=(const DenormalFPMathState &Caller) {
    Known = Known.unionWith(Caller.getKnown());
    return *this;
  }
};

/// Potential-values state specialized for constant integers.
using PotentialConstantIntValuesState = PotentialValuesState<APInt>;
/// Potential-values state specialized for LLVM values and scopes.
using PotentialLLVMValuesState =
    PotentialValuesState<std::pair<AA::ValueAndContext, AA::ValueScope>>;

/// Print a potential constant-int values state to an output stream.
///
/// \return The output stream \p OS.
///
/// \param OS The output stream.
/// \param R The potential values state to print.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const PotentialConstantIntValuesState &R);
/// Print a potential LLVM values state to an output stream.
///
/// \return The output stream \p OS.
///
/// \param OS The output stream.
/// \param R The potential values state to print.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const PotentialLLVMValuesState &R);

/// An abstract interface for potential values analysis.
///
/// This AA collects potential values for each IR position.
/// An assumed set of potential values is initialized with the empty set (the
/// best state) and it will grow monotonically as we find more potential values
/// for this position.
/// The set might be forced to the worst state, that is, to contain every
/// possible value for this position in 2 cases.
///   1. We surpassed the \p MaxPotentialValues threshold. This includes the
///      case that this position is affected (e.g. because of an operation) by a
///      Value that is in the worst state.
///   2. We tried to initialize on a Value that we cannot handle (e.g. an
///      operator we do not currently handle).
///
/// For non constant integers see AAPotentialValues.
struct AAPotentialConstantValues
    : public StateWrapper<PotentialConstantIntValuesState, AbstractAttribute> {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<PotentialConstantIntValuesState, AbstractAttribute>;
  /// Create a AAPotentialConstantValues for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAPotentialConstantValues(const IRPosition &IRP, Attributor &A) : Base(IRP) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.getAssociatedType()->isIntegerTy())
      return false;
    return AbstractAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// See AbstractAttribute::requiresCallersForArgOrFunction
  ///
  /// \return True if callers are required for argument or function positions.
  static bool requiresCallersForArgOrFunction() { return true; }

  /// See AbstractAttribute::getState(...).
  ///
  /// \return The abstract state associated with this attribute.
  PotentialConstantIntValuesState &getState() override { return *this; }
  /// See AbstractAttribute::getState(...).
  ///
  /// \return The abstract state associated with this attribute.
  const PotentialConstantIntValuesState &getState() const override {
    return *this;
  }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAPotentialConstantValues &
  createForPosition(const IRPosition &IRP, Attributor &A);

  /// Return an assumed constant for the associated value.
  ///
  /// \return An assumed constant for the associated value.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param CtxI Optional context instruction for the query.
  std::optional<Constant *>
  getAssumedConstant(Attributor &A, const Instruction *CtxI = nullptr) const {
    if (!isValidState())
      return nullptr;
    if (getAssumedSet().size() == 1) {
      Type *Ty = getAssociatedValue().getType();
      return cast_or_null<Constant>(AA::getWithType(
          *ConstantInt::get(Ty->getContext(), *(getAssumedSet().begin())),
          *Ty));
    }
    if (getAssumedSet().size() == 0) {
      if (undefIsContained())
        return UndefValue::get(getAssociatedValue().getType());
      return std::nullopt;
    }

    return nullptr;
  }

  /// Return the minimum trailing zeros of potential constants
  ///
  /// \return The minimum trailing zeros of potential constants.
  unsigned getAssumedMinTrailingZeros() const {
    if (!isValidState() || getAssumedSet().empty())
      return 0;
    unsigned TrailingZeros = getAssumedSet().begin()->getBitWidth() + 1;
    for (const APInt &It : getAssumedSet()) {
      if (It.countTrailingZeros() < TrailingZeros)
        TrailingZeros = It.countTrailingZeros();
    }
    if (TrailingZeros > getAssumedSet().begin()->getBitWidth())
      return 0;
    return TrailingZeros;
  }
  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAPotentialConstantValues"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAPotentialConstantValues
  ///
  /// \return True if \p AA is a AAPotentialConstantValues.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// Abstract attribute tracking potential LLVM values.
struct AAPotentialValues
    : public StateWrapper<PotentialLLVMValuesState, AbstractAttribute> {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<PotentialLLVMValuesState, AbstractAttribute>;
  /// Create a AAPotentialValues for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAPotentialValues(const IRPosition &IRP, Attributor &A) : Base(IRP) {}

  /// See AbstractAttribute::requiresCallersForArgOrFunction
  ///
  /// \return True if callers are required for argument or function positions.
  static bool requiresCallersForArgOrFunction() { return true; }

  /// See AbstractAttribute::getState(...).
  ///
  /// \return The abstract state associated with this attribute.
  PotentialLLVMValuesState &getState() override { return *this; }
  /// See AbstractAttribute::getState(...).
  ///
  /// \return The abstract state associated with this attribute.
  const PotentialLLVMValuesState &getState() const override { return *this; }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAPotentialValues &createForPosition(const IRPosition &IRP,
                                                       Attributor &A);

  /// Extract the single value in \p Values if any.
  ///
  /// \return The value, or null if conversion is not possible.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param AA The abstract attribute issuing the query.
  /// \param IRP The IR position of interest.
  /// \param Values The collection of potential values.
  LLVM_ABI static Value *
  getSingleValue(Attributor &A, const AbstractAttribute &AA,
                 const IRPosition &IRP,
                 SmallVectorImpl<AA::ValueAndContext> &Values);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAPotentialValues"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAPotentialValues
  ///
  /// \return True if \p AA is a AAPotentialValues.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;

private:
  virtual bool getAssumedSimplifiedValues(
      Attributor &A, SmallVectorImpl<AA::ValueAndContext> &Values,
      AA::ValueScope, bool RecurseForSelectAndPHI = false) const = 0;

  friend struct Attributor;
};

/// An abstract interface for all noundef attributes.
struct AANoUndef
    : public IRAttribute<Attribute::NoUndef,
                         StateWrapper<BooleanState, AbstractAttribute>,
                         AANoUndef> {
  /// Create a AANoUndef for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AANoUndef(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// See IRAttribute::isImpliedByUndef
  ///
  /// \return True if undef implies this attribute.
  static bool isImpliedByUndef() { return false; }

  /// See IRAttribute::isImpliedByPoison
  ///
  /// \return True if poison implies this attribute.
  static bool isImpliedByPoison() { return false; }

  /// See IRAttribute::isImpliedByIR
  ///
  /// \return True if the IR already implies the attribute for \p IRP.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  /// \param ImpliedAttributeKind The IR attribute kind being checked.
  /// \param IgnoreSubsumingPositions If true, ignore subsuming IR positions.
  LLVM_ABI static bool isImpliedByIR(Attributor &A, const IRPosition &IRP,
                                     Attribute::AttrKind ImpliedAttributeKind,
                                     bool IgnoreSubsumingPositions = false);

  /// Return true if we assume that the underlying value is noundef.
  ///
  /// \return True if we assume that the underlying value is noundef.
  bool isAssumedNoUndef() const { return getAssumed(); }

  /// Return true if we know that underlying value is noundef.
  ///
  /// \return True if we know that underlying value is noundef.
  bool isKnownNoUndef() const { return getKnown(); }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AANoUndef &createForPosition(const IRPosition &IRP,
                                               Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AANoUndef"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AANoUndef
  ///
  /// \return True if \p AA is a AANoUndef.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// Abstract attribute for the nofpclass property.
struct AANoFPClass
    : public IRAttribute<
          Attribute::NoFPClass,
          StateWrapper<BitIntegerState<uint32_t, fcAllFlags, fcNone>,
                       AbstractAttribute>,
          AANoFPClass> {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<BitIntegerState<uint32_t, fcAllFlags, fcNone>,
                            AbstractAttribute>;

  /// Create a AANoFPClass for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AANoFPClass(const IRPosition &IRP, Attributor &A) : IRAttribute(IRP) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    return AttributeFuncs::isNoFPClassCompatibleType(IRP.getAssociatedType());
  }

  /// Return the underlying assumed nofpclass.
  ///
  /// \return The underlying assumed nofpclass.
  FPClassTest getAssumedNoFPClass() const {
    return static_cast<FPClassTest>(getAssumed());
  }
  /// Return the underlying known nofpclass.
  ///
  /// \return The underlying known nofpclass.
  FPClassTest getKnownNoFPClass() const {
    return static_cast<FPClassTest>(getKnown());
  }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AANoFPClass &createForPosition(const IRPosition &IRP,
                                                 Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AANoFPClass"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AANoFPClass
  ///
  /// \return True if \p AA is a AANoFPClass.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// Node in the Attributor call graph.
struct AACallGraphNode;
/// Abstract attribute tracking call edges for a function.
struct AACallEdges;

/// An Iterator for call edges, creates AACallEdges attributes in a lazy way.
///
/// This iterator becomes invalid if the underlying edge list changes, and
/// should not outlive an Attributor iteration.
class AACallEdgeIterator
    : public iterator_adaptor_base<AACallEdgeIterator,
                                   SetVector<Function *>::iterator> {
  AACallEdgeIterator(Attributor &A, SetVector<Function *>::iterator Begin)
      : iterator_adaptor_base(Begin), A(A) {}

public:
  /// Dereference the call-edge iterator.
  ///
  /// \return A pointer to the requested object, or null if unavailable.
  LLVM_ABI AACallGraphNode *operator*() const;

private:
  Attributor &A;
  friend AACallEdges;
  friend AttributorCallGraph;
};

struct AACallGraphNode {
  /// Construct a call-graph node.
  ///
  /// \param A The Attributor owning this call-graph node.
  AACallGraphNode(Attributor &A) : A(A) {}
  /// Destroy the call-graph node.
  ///
  /// \return The computed result.
  virtual ~AACallGraphNode() = default;

  /// Return an iterator to the first optimistic call edge.
  ///
  /// \return An iterator to the first optimistic call edge.
  virtual AACallEdgeIterator optimisticEdgesBegin() const = 0;
  /// Return an iterator past the last optimistic call edge.
  ///
  /// \return An iterator past the last optimistic call edge.
  virtual AACallEdgeIterator optimisticEdgesEnd() const = 0;

  /// Iterator range for exploring the call graph.
  ///
  /// \return The requested iterator.
  iterator_range<AACallEdgeIterator> optimisticEdgesRange() const {
    return iterator_range<AACallEdgeIterator>(optimisticEdgesBegin(),
                                              optimisticEdgesEnd());
  }

protected:
  /// Reference to Attributor needed for GraphTraits implementation.
  Attributor &A;
};

/// An abstract state for querying live call edges.
/// This interface uses the Attributor's optimistic liveness
/// information to compute the edges that are alive.
struct AACallEdges : public StateWrapper<BooleanState, AbstractAttribute>,
                     AACallGraphNode {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<BooleanState, AbstractAttribute>;

  /// Create a AACallEdges for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AACallEdges(const IRPosition &IRP, Attributor &A)
      : Base(IRP), AACallGraphNode(A) {}

  /// See AbstractAttribute::requiresNonAsmForCallBase.
  ///
  /// \return True if non-asm call bases are required.
  static bool requiresNonAsmForCallBase() { return false; }

  /// Get the optimistic edges.
  ///
  /// \return A reference to the requested object.
  virtual const SetVector<Function *> &getOptimisticEdges() const = 0;

  /// Is there any call with a unknown callee.
  ///
  /// \return True if the condition described by this query holds.
  virtual bool hasUnknownCallee() const = 0;

  /// Is there any call with a unknown callee, excluding any inline asm.
  ///
  /// \return True if the condition described by this query holds.
  virtual bool hasNonAsmUnknownCallee() const = 0;

  /// Iterator for exploring the call graph.
  ///
  /// \return The requested iterator.
  AACallEdgeIterator optimisticEdgesBegin() const override {
    return AACallEdgeIterator(A, getOptimisticEdges().begin());
  }

  /// Iterator for exploring the call graph.
  ///
  /// \return The requested iterator.
  AACallEdgeIterator optimisticEdgesEnd() const override {
    return AACallEdgeIterator(A, getOptimisticEdges().end());
  }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AACallEdges &createForPosition(const IRPosition &IRP,
                                                 Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AACallEdges"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AACallEdges.
  ///
  /// \return True if \p AA is a AACallEdges.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// Call graph rooted at the Attributor's initial function set.
struct AttributorCallGraph : public AACallGraphNode {
  /// Construct a call graph from the Attributor.
  ///
  /// \param A The Attributor providing the root function set.
  AttributorCallGraph(Attributor &A) : AACallGraphNode(A) {}
  /// Destroy the Attributor call graph.
  ~AttributorCallGraph() override = default;

  /// Return an iterator to the first optimistic root call edge.
  ///
  /// \return An iterator to the first optimistic root call edge.
  AACallEdgeIterator optimisticEdgesBegin() const override {
    return AACallEdgeIterator(A, A.Functions.begin());
  }

  /// Return an iterator past the last optimistic root call edge.
  ///
  /// \return An iterator past the last optimistic root call edge.
  AACallEdgeIterator optimisticEdgesEnd() const override {
    return AACallEdgeIterator(A, A.Functions.end());
  }

  /// Force populate the entire call graph.
  void populateAll() const {
    for (const AACallGraphNode *AA : optimisticEdgesRange()) {
      // Nothing else to do here.
      (void)AA;
    }
  }

  /// Print the Attributor call graph.
  LLVM_ABI void print();
};

/// GraphTraits specialization for the Attributor call graph.
template <> struct GraphTraits<AACallGraphNode *> {
  /// Reference type for a graph node.
  using NodeRef = AACallGraphNode *;
  /// Iterator type over child nodes.
  using ChildIteratorType = AACallEdgeIterator;

  /// Return an iterator to the first child call-graph node of \p Node.
  ///
  /// \return An iterator to the first child call-graph node of \p Node.
  ///
  /// \param Node The parent call-graph node.
  static AACallEdgeIterator child_begin(AACallGraphNode *Node) {
    return Node->optimisticEdgesBegin();
  }

  /// Return an iterator past the last child call-graph node of \p Node.
  ///
  /// \return An iterator past the last child call-graph node of \p Node.
  ///
  /// \param Node The parent call-graph node.
  static AACallEdgeIterator child_end(AACallGraphNode *Node) {
    return Node->optimisticEdgesEnd();
  }
};

/// GraphTraits specialization for the Attributor call graph.
template <>
struct GraphTraits<AttributorCallGraph *>
    : public GraphTraits<AACallGraphNode *> {
  /// Iterator type over graph nodes.
  using nodes_iterator = AACallEdgeIterator;

  /// Return the entry node of the graph.
  ///
  /// \return The entry node of the graph.
  ///
  /// \param G The Attributor call graph.
  static AACallGraphNode *getEntryNode(AttributorCallGraph *G) {
    return static_cast<AACallGraphNode *>(G);
  }

  /// Return an iterator to the first graph node.
  ///
  /// \return An iterator to the first graph node.
  ///
  /// \param G The Attributor call graph.
  static AACallEdgeIterator nodes_begin(const AttributorCallGraph *G) {
    return G->optimisticEdgesBegin();
  }

  /// Return an iterator past the last graph node.
  ///
  /// \return An iterator past the last graph node.
  ///
  /// \param G The Attributor call graph.
  static AACallEdgeIterator nodes_end(const AttributorCallGraph *G) {
    return G->optimisticEdgesEnd();
  }
};

/// DOT graph traits for Attributor call-graph visualization.
template <>
struct DOTGraphTraits<AttributorCallGraph *> : public DefaultDOTGraphTraits {
  /// Construct DOT graph traits.
  ///
  /// \param Simple If true, emit a simplified DOT graph.
  DOTGraphTraits(bool Simple = false) : DefaultDOTGraphTraits(Simple) {}

  /// Return the DOT label for the given call-graph node.
  ///
  /// \return The DOT label for the given call-graph node.
  ///
  /// \param Node The call-graph node to label.
  /// \param Graph The Attributor call graph being rendered.
  std::string getNodeLabel(const AACallGraphNode *Node,
                           const AttributorCallGraph *Graph) {
    const AACallEdges *AACE = static_cast<const AACallEdges *>(Node);
    return AACE->getAssociatedFunction()->getName().str();
  }

  /// Return true if the DOT node should be hidden.
  ///
  /// \return True if the DOT node should be hidden.
  ///
  /// \param Node The call-graph node being rendered.
  /// \param Graph The Attributor call graph being rendered.
  static bool isNodeHidden(const AACallGraphNode *Node,
                           const AttributorCallGraph *Graph) {
    // Hide the synth root.
    return static_cast<const AACallGraphNode *>(Graph) == Node;
  }
};

/// Abstract attribute describing thread execution domains.
struct AAExecutionDomain
    : public StateWrapper<BooleanState, AbstractAttribute> {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<BooleanState, AbstractAttribute>;
  /// Create a AAExecutionDomain for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAExecutionDomain(const IRPosition &IRP, Attributor &A) : Base(IRP) {}

  /// Summary about the execution domain of a block or instruction.
  struct ExecutionDomainTy {
    /// Set type for aligned barriers.
    using BarriersSetTy = SmallPtrSet<CallBase *, 2>;
    /// Set type for assume instructions.
    using AssumesSetTy = SmallPtrSet<AssumeInst *, 4>;

    /// Record an assume instruction in this domain.
    ///
    /// \param A The Attributor managing the analysis.
    /// \param AI The assume instruction to record.
    void addAssumeInst(Attributor &A, AssumeInst &AI) {
      EncounteredAssumes.insert(&AI);
    }

    /// Record an aligned barrier in this domain.
    ///
    /// \param A The Attributor managing the analysis.
    /// \param CB The aligned barrier call to record.
    void addAlignedBarrier(Attributor &A, CallBase &CB) {
      AlignedBarriers.insert(&CB);
    }

    /// Clear recorded assumes and aligned barriers.
    void clearAssumeInstAndAlignedBarriers() {
      EncounteredAssumes.clear();
      AlignedBarriers.clear();
    }

    bool IsExecutedByInitialThreadOnly = true; ///< True if only the initial thread executes this domain.
    bool IsReachedFromAlignedBarrierOnly = true; ///< True if reached only from aligned barriers.
    bool IsReachingAlignedBarrierOnly = true; ///< True if only reaching aligned barriers.
    bool EncounteredNonLocalSideEffect = false; ///< True if a non-local side effect was seen.
    /// Set of aligned barriers encountered.
    BarriersSetTy AlignedBarriers;
    /// Set of assume instructions encountered.
    AssumesSetTy EncounteredAssumes;
  };

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAExecutionDomain &createForPosition(const IRPosition &IRP,
                                                       Attributor &A);

  /// See AbstractAttribute::getName().
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAExecutionDomain"; }

  /// See AbstractAttribute::getIdAddr().
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// Check if an instruction is executed only by the initial thread.
  ///
  /// \return True if an instruction is executed only by the initial thread.
  ///
  /// \param I The instruction to inspect.
  bool isExecutedByInitialThreadOnly(const Instruction &I) const {
    return isExecutedByInitialThreadOnly(*I.getParent());
  }

  /// Check if a basic block is executed only by the initial thread.
  ///
  /// \return True if a basic block is executed only by the initial thread.
  ///
  /// \param BB The basic block to inspect.
  virtual bool isExecutedByInitialThreadOnly(const BasicBlock &BB) const = 0;

  /// Return true if \p I is executed in an aligned region.
  ///
  /// That is, the synchronizing effects before and after \p I are both
  /// aligned barriers, so all threads execute \p I together.
  ///
  /// \return True if \p I is executed in an aligned region.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param I The instruction to inspect.
  virtual bool isExecutedInAlignedRegion(Attributor &A,
                                         const Instruction &I) const = 0;

  /// Return the execution domain for the given basic block.
  ///
  /// \return The execution domain for the given basic block.
  ///
  /// \param BB The basic block to inspect.
  virtual ExecutionDomainTy getExecutionDomain(const BasicBlock &BB) const = 0;
  /// Return the execution domains at entry to and exit from call \p CB.
  ///
  /// \return The execution domains at entry to and exit from call \p CB.
  ///
  /// \param CB The call site to inspect.
  virtual std::pair<ExecutionDomainTy, ExecutionDomainTy>
  getExecutionDomain(const CallBase &CB) const = 0;
  /// Return the execution domain of the associated function.
  ///
  /// \return The execution domain of the associated function.
  virtual ExecutionDomainTy getFunctionExecutionDomain() const = 0;

  /// Helper function to determine if \p FI is a no-op given the information
  /// about its execution from \p ExecDomainAA.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param FI The fence instruction of interest.
  virtual bool isNoOpFence(const FenceInst &FI) const = 0;

  /// This function should return true if the type of the \p AA is
  /// AAExecutionDomain.
  ///
  /// \return True if \p AA is a AAExecutionDomain.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract Attribute for computing reachability between functions.
struct AAInterFnReachability
    : public StateWrapper<BooleanState, AbstractAttribute> {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<BooleanState, AbstractAttribute>;

  /// Create a AAInterFnReachability for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAInterFnReachability(const IRPosition &IRP, Attributor &A) : Base(IRP) {}

  /// If the function represented by this possition can reach \p Fn.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param Fn The function of interest.
  bool canReach(Attributor &A, const Function &Fn) const {
    Function *Scope = getAnchorScope();
    if (!Scope || Scope->isDeclaration())
      return true;
    return instructionCanReach(A, Scope->getEntryBlock().front(), Fn);
  }

  /// Can  \p Inst reach \p Fn.
  /// See also AA::isPotentiallyReachable.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param Inst The instruction of interest.
  /// \param Fn The function of interest.
  /// \param ExclusionSet Optional instructions excluded from reachability.
  virtual bool instructionCanReach(
      Attributor &A, const Instruction &Inst, const Function &Fn,
      const AA::InstExclusionSetTy *ExclusionSet = nullptr) const = 0;

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAInterFnReachability &
  createForPosition(const IRPosition &IRP, Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAInterFnReachability"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is AACallEdges.
  ///
  /// \return True if \p AA is a AACallEdges.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract Attribute for determining the necessity of the convergent
/// attribute.
struct AANonConvergent : public StateWrapper<BooleanState, AbstractAttribute> {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<BooleanState, AbstractAttribute>;

  /// Create a AANonConvergent for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AANonConvergent(const IRPosition &IRP, Attributor &A) : Base(IRP) {}

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AANonConvergent &createForPosition(const IRPosition &IRP,
                                                     Attributor &A);

  /// Return true if "non-convergent" is assumed.
  ///
  /// \return True if "non-convergent" is assumed.
  bool isAssumedNotConvergent() const { return getAssumed(); }

  /// Return true if "non-convergent" is known.
  ///
  /// \return True if "non-convergent" is known.
  bool isKnownNotConvergent() const { return getKnown(); }

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AANonConvergent"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AANonConvergent.
  ///
  /// \return True if \p AA is a AANonConvergent.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract interface for pointer struct and access information.
struct AAPointerInfo : public AbstractAttribute {
  /// Create an AAPointerInfo for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  AAPointerInfo(const IRPosition &IRP) : AbstractAttribute(IRP) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.getAssociatedType()->isPtrOrPtrVectorTy())
      return false;
    return AbstractAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// Kind of memory access described by an Access object.
  enum AccessKind {
    AK_MUST = 1 << 0, ///< Must-access bit.
    AK_MAY = 1 << 1, ///< May-access bit.

    AK_R = 1 << 2, ///< Read access.
    AK_W = 1 << 3, ///< Write access.
    AK_RW = AK_R | AK_W, ///< Read and write access.

    AK_ASSUMPTION = (1 << 4) | AK_MUST, ///< Assumption about memory content.

    AK_MAY_READ = AK_MAY | AK_R, ///< May-read access.
    AK_MAY_WRITE = AK_MAY | AK_W, ///< May-write access.
    AK_MAY_READ_WRITE = AK_MAY | AK_R | AK_W, ///< May read and write access.
    AK_MUST_READ = AK_MUST | AK_R, ///< Must-read access.
    AK_MUST_WRITE = AK_MUST | AK_W, ///< Must-write access.
    AK_MUST_READ_WRITE = AK_MUST | AK_R | AK_W, ///< Must read and write access.
  };

  /// Helper holding offsets computed for a Use.
  ///
  /// Ideally this list should be strictly ascending, but we ensure that only
  /// when we actually translate the list of offsets to a RangeList.
  struct OffsetInfo {
    /// Vector type storing offsets or ranges.
    using VecTy = SmallSet<int64_t, 4>;
    /// Const iterator over the container.
    using const_iterator = VecTy::const_iterator;
    /// List of computed offsets.
    VecTy Offsets;

    /// Return an iterator to the first element.
    ///
    /// \return An iterator to the first element.
    const_iterator begin() const { return Offsets.begin(); }
    /// Return an iterator past the last element.
    ///
    /// \return An iterator past the last element.
    const_iterator end() const { return Offsets.end(); }

    /// Return true if this equals \p RHS.
    ///
    /// \return True if this equals \p RHS.
    ///
    /// \param RHS The other operand or state.
    bool operator==(const OffsetInfo &RHS) const {
      return Offsets == RHS.Offsets;
    }

    /// Return true if this differs from \p RHS.
    ///
    /// \return True if this differs from \p RHS.
    ///
    /// \param RHS The other operand or state.
    bool operator!=(const OffsetInfo &RHS) const { return !(*this == RHS); }

    /// Insert \p Offset into the offset list.
    ///
    /// \return True if the condition described by this query holds.
    ///
    /// \param Offset The byte offset to insert.
    bool insert(int64_t Offset) { return Offsets.insert(Offset).second; }
    /// Return true if no offsets have been recorded yet.
    ///
    /// \return True if no offsets have been recorded yet.
    bool isUnassigned() const { return Offsets.size() == 0; }

    /// Return true if the offset is unknown.
    ///
    /// \return True if the offset is unknown.
    bool isUnknown() const {
      if (isUnassigned())
        return false;
      if (Offsets.size() == 1)
        return *Offsets.begin() == AA::RangeTy::Unknown;
      return false;
    }

    /// Mark the offset as unknown.
    void setUnknown() {
      Offsets.clear();
      Offsets.insert(AA::RangeTy::Unknown);
    }

    /// Add an increment to all offsets.
    ///
    /// \param Inc The amount added to every offset.
    void addToAll(int64_t Inc) {
      VecTy NewOffsets;
      for (auto &Offset : Offsets)
        NewOffsets.insert(Offset + Inc);
      Offsets = std::move(NewOffsets);
    }

    /// Copy offsets from \p R into the current list.
    ///
    /// Ideally all lists should be strictly ascending, but we defer that to the
    /// actual use of the list. So we just blindly append here.
    ///
    /// \return True if the condition described by this query holds.
    ///
    /// \param R The other operand or state.
    bool merge(const OffsetInfo &R) { return set_union(Offsets, R.Offsets); }
  };

  /// A container for a list of ranges.
  struct RangeList {
    // The set of ranges rarely contains more than one element, and is unlikely
    // to contain more than say four elements. So we find the middle-ground with
    // a sorted vector. This avoids hard-coding a rarely used number like "four"
    // into every instance of a SmallSet.
    /// Range type used by the range list.
    using RangeTy = AA::RangeTy;
    /// Vector type storing ranges.
    using VecTy = SmallVector<RangeTy>;
    /// Iterator over ranges.
    using iterator = VecTy::iterator;
    /// Const iterator over ranges.
    using const_iterator = VecTy::const_iterator;
    /// List of access ranges.
    VecTy Ranges;

    /// Construct a range list.
    ///
    /// \param R The initial range.
    RangeList(const RangeTy &R) { Ranges.push_back(R); }
    /// Construct a range list from offsets and a size.
    ///
    /// \param Offsets Strictly ascending offsets for each range.
    /// \param Size The byte size shared by every range.
    RangeList(ArrayRef<int64_t> Offsets, int64_t Size) {
      Ranges.reserve(Offsets.size());
      for (unsigned i = 0, e = Offsets.size(); i != e; ++i) {
        assert(((i + 1 == e) || Offsets[i] < Offsets[i + 1]) &&
               "Expected strictly ascending offsets.");
        Ranges.emplace_back(Offsets[i], Size);
      }
    }
    /// Construct an empty range list.
    RangeList() = default;

    /// Return an iterator to the first range.
    ///
    /// \return An iterator to the first range.
    iterator begin() { return Ranges.begin(); }
    /// Return an iterator past the last range.
    ///
    /// \return An iterator past the last range.
    iterator end() { return Ranges.end(); }
    /// Return a const iterator to the first range.
    ///
    /// \return A const iterator to the first range.
    const_iterator begin() const { return Ranges.begin(); }
    /// Return a const iterator past the last range.
    ///
    /// \return A const iterator past the last range.
    const_iterator end() const { return Ranges.end(); }

    // Helpers required for std::set_difference
    /// Value type of the range list.
    using value_type = RangeTy;
    /// Append \p R while preserving sorted order.
    ///
    /// \param R The range to append.
    void push_back(const RangeTy &R) {
      assert((Ranges.empty() || RangeTy::LessThan(Ranges.back(), R)) &&
             "Ensure the last element is the greatest.");
      Ranges.push_back(R);
    }

    /// Copy ranges from \p L that are not in \p R, into \p D.
    ///
    /// \param L The left-hand side.
    /// \param R The other operand or state.
    /// \param D The destination receiving the difference.
    static void set_difference(const RangeList &L, const RangeList &R,
                               RangeList &D) {
      std::set_difference(L.begin(), L.end(), R.begin(), R.end(),
                          std::back_inserter(D), RangeTy::LessThan);
    }

    /// Return the number of elements.
    ///
    /// \return The number of elements.
    unsigned size() const { return Ranges.size(); }

    /// Return true if this equals \p OI.
    ///
    /// \return True if this equals \p OI.
    ///
    /// \param OI The other operand.
    bool operator==(const RangeList &OI) const { return Ranges == OI.Ranges; }

    /// Merge the ranges in \p RHS into this list.
    ///
    /// - Merging a list of unknown ranges makes the current list unknown.
    /// - Ranges with the same offset are merged according to RangeTy::operator&
    ///
    /// \param RHS The range list to merge from.
    /// \return true if the current RangeList changed.
    bool merge(const RangeList &RHS) {
      if (isUnknown())
        return false;
      if (RHS.isUnknown()) {
        setUnknown();
        return true;
      }

      if (Ranges.empty()) {
        Ranges = RHS.Ranges;
        return true;
      }

      bool Changed = false;
      auto LPos = Ranges.begin();
      for (auto &R : RHS.Ranges) {
        auto Result = insert(LPos, R);
        if (isUnknown())
          return true;
        LPos = Result.first;
        Changed |= Result.second;
      }
      return Changed;
    }

    /// Insert \p R at the given iterator \p Pos, and merge if necessary.
    ///
    /// This assumes that all ranges before \p Pos are LessThan \p R, and
    /// then maintains the sorted order for the suffix list.
    ///
    /// \return The place of insertion and true iff anything changed.
    ///
    /// \param R The other operand or state.
    /// \param Pos The insertion position.
    std::pair<iterator, bool> insert(iterator Pos, const RangeTy &R) {
      if (isUnknown())
        return std::make_pair(Ranges.begin(), false);
      if (R.offsetOrSizeAreUnknown()) {
        return std::make_pair(setUnknown(), true);
      }

      // Maintain this as a sorted vector of unique entries.
      auto LB = std::lower_bound(Pos, Ranges.end(), R, RangeTy::LessThan);
      if (LB == Ranges.end() || LB->Offset != R.Offset)
        return std::make_pair(Ranges.insert(LB, R), true);
      bool Changed = *LB != R;
      *LB &= R;
      if (LB->offsetOrSizeAreUnknown())
        return std::make_pair(setUnknown(), true);
      return std::make_pair(LB, Changed);
    }

    /// Insert the given range \p R, maintaining sorted order.
    ///
    /// \return The place of insertion and true iff anything changed.
    ///
    /// \param R The other operand or state.
    std::pair<iterator, bool> insert(const RangeTy &R) {
      return insert(Ranges.begin(), R);
    }

    /// Add the increment \p Inc to the offset of every range.
    ///
    /// \param Inc The amount added to every offset.
    void addToAllOffsets(int64_t Inc) {
      assert(!isUnassigned() &&
             "Cannot increment if the offset is not yet computed!");
      if (isUnknown())
        return;
      for (auto &R : Ranges) {
        R.Offset += Inc;
      }
    }

    /// Return true iff there is exactly one range and it is known.
    ///
    /// \return True iff there is exactly one range and it is known.
    bool isUnique() const {
      return Ranges.size() == 1 && !Ranges.front().offsetOrSizeAreUnknown();
    }

    /// Return the unique range, assuming it exists.
    ///
    /// \return The unique range, assuming it exists.
    const RangeTy &getUnique() const {
      assert(isUnique() && "No unique range to return!");
      return Ranges.front();
    }

    /// Return true iff the list contains an unknown range.
    ///
    /// \return True iff the list contains an unknown range.
    bool isUnknown() const {
      if (isUnassigned())
        return false;
      if (Ranges.front().offsetOrSizeAreUnknown()) {
        assert(Ranges.size() == 1 && "Unknown is a singleton range.");
        return true;
      }
      return false;
    }

    /// Discard all ranges and insert a single unknown range.
    ///
    /// \return The requested iterator.
    iterator setUnknown() {
      Ranges.clear();
      Ranges.push_back(RangeTy::getUnknown());
      return Ranges.begin();
    }

    /// Return true if no ranges have been inserted.
    ///
    /// \return True if no ranges have been inserted.
    bool isUnassigned() const { return Ranges.size() == 0; }
  };

  /// An access description.
  struct Access {
    /// Construct an access descriptor.
    ///
    /// \param I The instruction causing the access.
    /// \param Offset The byte offset from the base pointer.
    /// \param Size The access size in bytes.
    /// \param Content Optional written value, if known.
    /// \param Kind The access kind bitset.
    /// \param Ty The accessed content type, if known.
    Access(Instruction *I, int64_t Offset, int64_t Size,
           std::optional<Value *> Content, AccessKind Kind, Type *Ty)
        : LocalI(I), RemoteI(I), Content(Content), Ranges(Offset, Size),
          Kind(Kind), Ty(Ty) {
      verify();
    }
    /// Construct an access from local/remote instructions and ranges.
    ///
    /// \param LocalI The local-scope instruction causing the access.
    /// \param RemoteI The remote instruction causing the access.
    /// \param Ranges The accessed ranges.
    /// \param Content Optional written value, if known.
    /// \param K The access kind bitset.
    /// \param Ty The accessed content type, if known.
    Access(Instruction *LocalI, Instruction *RemoteI, const RangeList &Ranges,
           std::optional<Value *> Content, AccessKind K, Type *Ty)
        : LocalI(LocalI), RemoteI(RemoteI), Content(Content), Ranges(Ranges),
          Kind(K), Ty(Ty) {
      if (Ranges.size() > 1) {
        Kind = AccessKind(Kind | AK_MAY);
        Kind = AccessKind(Kind & ~AK_MUST);
      }
      /// Assert internal invariants of this access.
      verify();
    }
    /// Construct an access from local/remote instructions and an offset.
    ///
    /// \param LocalI The local-scope instruction causing the access.
    /// \param RemoteI The remote instruction causing the access.
    /// \param Offset The byte offset from the base pointer.
    /// \param Size The access size in bytes.
    /// \param Content Optional written value, if known.
    /// \param Kind The access kind bitset.
    /// \param Ty The accessed content type, if known.
    Access(Instruction *LocalI, Instruction *RemoteI, int64_t Offset,
           int64_t Size, std::optional<Value *> Content, AccessKind Kind,
           Type *Ty)
        : LocalI(LocalI), RemoteI(RemoteI), Content(Content),
          Ranges(Offset, Size), Kind(Kind), Ty(Ty) {
      verify();
    }
    /// Copy-construct an access.
    ///
    /// \param Other The access to copy.
    Access(const Access &Other) = default;

    /// Assign from another access description.
    ///
    /// \return The computed result.
    ///
    /// \param Other The access to copy.
    Access &operator=(const Access &Other) = default;
    /// Return true if this equals \p R.
    ///
    /// \return True if this equals \p R.
    ///
    /// \param R The other operand or state.
    bool operator==(const Access &R) const {
      return LocalI == R.LocalI && RemoteI == R.RemoteI && Ranges == R.Ranges &&
             Content == R.Content && Kind == R.Kind;
    }
    /// Return true if this differs from \p R.
    ///
    /// \return True if this differs from \p R.
    ///
    /// \param R The other operand or state.
    bool operator!=(const Access &R) const { return !(*this == R); }

    /// Merge overlapping access information from \p R into this access.
    ///
    /// \return The computed result.
    ///
    /// \param R The other access description.
    Access &operator&=(const Access &R) {
      assert(RemoteI == R.RemoteI && "Expected same instruction!");
      assert(LocalI == R.LocalI && "Expected same instruction!");

      // Note that every Access object corresponds to a unique Value, and only
      // accesses to the same Value are merged. Hence we assume that all ranges
      // are the same size. If ranges can be different size, then the contents
      // must be dropped.
      Ranges.merge(R.Ranges);
      Content =
          AA::combineOptionalValuesInAAValueLatice(Content, R.Content, Ty);

      // Combine the access kind, which results in a bitwise union.
      // If there is more than one range, then this must be a MAY.
      // If we combine a may and a must access we clear the must bit.
      Kind = AccessKind(Kind | R.Kind);
      if ((Kind & AK_MAY) || Ranges.size() > 1) {
        Kind = AccessKind(Kind | AK_MAY);
        Kind = AccessKind(Kind & ~AK_MUST);
      }
      /// Assert internal invariants of this access.
      verify();
      return *this;
    }

    /// Assert internal invariants of this access.
    void verify() {
      assert(isMustAccess() + isMayAccess() == 1 &&
             "Expect must or may access, not both.");
      assert(isAssumption() + isWrite() <= 1 &&
             "Expect assumption access or write access, never both.");
      assert((isMayAccess() || Ranges.size() == 1) &&
             "Cannot be a must access if there are multiple ranges.");
    }

    /// Return the access kind.
    ///
    /// \return The access kind.
    AccessKind getKind() const { return Kind; }

    /// Return true if this is a read access.
    ///
    /// \return True if this is a read access.
    bool isRead() const { return Kind & AK_R; }

    /// Return true if this is a write access.
    ///
    /// \return True if this is a write access.
    bool isWrite() const { return Kind & AK_W; }

    /// Return true if this is a write access.
    ///
    /// \return True if this is a write access.
    bool isWriteOrAssumption() const { return isWrite() || isAssumption(); }

    /// Return true if this is an assumption access.
    ///
    /// \return True if this is an assumption access.
    bool isAssumption() const { return Kind == AK_ASSUMPTION; }

    /// Return true if this is a must-access.
    ///
    /// \return True if this is a must-access.
    bool isMustAccess() const {
      bool MustAccess = Kind & AK_MUST;
      assert((!MustAccess || Ranges.size() < 2) &&
             "Cannot be a must access if there are multiple ranges.");
      return MustAccess;
    }

    /// Return true if this is a may-access.
    ///
    /// \return True if this is a may-access.
    bool isMayAccess() const {
      bool MayAccess = Kind & AK_MAY;
      assert((MayAccess || Ranges.size() < 2) &&
             "Cannot be a must access if there are multiple ranges.");
      return MayAccess;
    }

    /// Return the instruction that causes the access with respect to the local
    /// scope of the associated attribute.
    ///
    /// \return The instruction that causes the access with respect to the local scope of the associated attribute.
    Instruction *getLocalInst() const { return LocalI; }

    /// Return the actual instruction that causes the access.
    ///
    /// \return The actual instruction that causes the access.
    Instruction *getRemoteInst() const { return RemoteI; }

    /// Return true if the value written is not known yet.
    ///
    /// \return True if the value written is not known yet.
    bool isWrittenValueYetUndetermined() const { return !Content; }

    /// Return true if the value written cannot be determined at all.
    ///
    /// \return True if the value written cannot be determined at all.
    bool isWrittenValueUnknown() const {
      return Content.has_value() && !*Content;
    }

    /// Set the value written to nullptr, i.e., unknown.
    void setWrittenValueUnknown() { Content = nullptr; }

    /// Return the type associated with the access, if known.
    ///
    /// \return The type associated with the access, if known.
    Type *getType() const { return Ty; }

    /// Return the value writen, if any.
    ///
    /// \return The value writen, if any.
    Value *getWrittenValue() const {
      assert(!isWrittenValueYetUndetermined() &&
             "Value needs to be determined before accessing it.");
      return *Content;
    }

    /// Return the written value which can be `llvm::null` if it is not yet
    /// determined.
    ///
    /// \return The written value which can be `llvm::null` if it is not yet determined.
    std::optional<Value *> getContent() const { return Content; }

    /// Return true if this access has a unique range.
    ///
    /// \return True if this access has a unique range.
    bool hasUniqueRange() const { return Ranges.isUnique(); }
    /// Return the unique access range.
    ///
    /// \return The unique access range.
    const AA::RangeTy &getUniqueRange() const { return Ranges.getUnique(); }

    /// Add a range accessed by this Access.
    ///
    /// If there are multiple ranges, then this is a "may access".
    ///
    /// \param Offset The byte offset.
    /// \param Size The byte size.
    void addRange(int64_t Offset, int64_t Size) {
      Ranges.insert({Offset, Size});
      if (!hasUniqueRange()) {
        Kind = AccessKind(Kind | AK_MAY);
        Kind = AccessKind(Kind & ~AK_MUST);
      }
    }

    /// Return the list of access ranges.
    ///
    /// \return The list of access ranges.
    const RangeList &getRanges() const { return Ranges; }

    /// Const iterator over the access ranges.
    using const_iterator = RangeList::const_iterator;
    /// Return a const iterator to the first access range.
    ///
    /// \return A const iterator to the first access range.
    const_iterator begin() const { return Ranges.begin(); }
    /// Return a const iterator past the last access range.
    ///
    /// \return A const iterator past the last access range.
    const_iterator end() const { return Ranges.end(); }

  private:
    /// The instruction responsible for the access with respect to the local
    /// scope of the associated attribute.
    Instruction *LocalI;

    /// The instruction responsible for the access.
    Instruction *RemoteI;

    /// The value written, if any. `std::nullopt` means "not known yet",
    /// `nullptr` cannot be determined.
    std::optional<Value *> Content;

    /// Set of potential ranges accessed from the base pointer.
    RangeList Ranges;

    /// The access kind, e.g., READ, as bitset (could be more than one).
    AccessKind Kind;

    /// The type of the content, thus the type read/written, can be null if not
    /// available.
    Type *Ty;
  };

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAPointerInfo &createForPosition(const IRPosition &IRP,
                                                   Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAPointerInfo"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// Map type from offset to access bin.
  using OffsetBinsTy = DenseMap<AA::RangeTy, SmallSet<unsigned, 4>>;
  /// Const iterator over offset bins.
  using const_bin_iterator = OffsetBinsTy::const_iterator;
  /// Return a const iterator to the first offset bin.
  ///
  /// \return A const iterator to the first offset bin.
  virtual const_bin_iterator begin() const = 0;
  /// Return a const iterator past the last offset bin.
  ///
  /// \return A const iterator past the last offset bin.
  virtual const_bin_iterator end() const = 0;
  /// Return the number of offset bins.
  ///
  /// \return The number of offset bins.
  virtual int64_t numOffsetBins() const = 0;
  /// Return true if the pointer may reach a return.
  ///
  /// \return True if the pointer may reach a return.
  virtual bool reachesReturn() const = 0;
  /// Add returned-pointer offsets into the given set.
  ///
  /// \param OI The offset set receiving returned-pointer offsets.
  virtual void addReturnedOffsetsTo(OffsetInfo &OI) const = 0;

  /// Call \p CB on all accesses that might interfere with \p Range.
  ///
  /// Return true if all such accesses were known and the callback returned
  /// true for all of them, false otherwise.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param Range The access range of interest.
  /// \param CB Callback applied to each interfering access.
  virtual bool forallInterferingAccesses(
      AA::RangeTy Range, function_ref<bool(const Access &, bool)> CB) const = 0;

  /// Call \p CB on all accesses that might interfere with \p I.
  ///
  /// Return true if all such accesses were known and the callback returned
  /// true for all of them, false otherwise.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param QueryingAA The abstract attribute issuing the query.
  /// \param I The instruction of interest.
  /// \param FindInterferingWrites Whether to include interfering writes.
  /// \param FindInterferingReads Whether to include interfering reads.
  /// \param CB Callback applied to each interfering access.
  /// \param HasBeenWrittenTo Set if a write may interfere.
  /// \param Range The access range of interest.
  /// \param SkipCB Optional callback to skip selected accesses.
  virtual bool forallInterferingAccesses(
      Attributor &A, const AbstractAttribute &QueryingAA, Instruction &I,
      bool FindInterferingWrites, bool FindInterferingReads,
      function_ref<bool(const Access &, bool)> CB, bool &HasBeenWrittenTo,
      AA::RangeTy &Range,
      function_ref<bool(const Access &)> SkipCB = nullptr) const = 0;

  /// This function should return true if the type of the \p AA is AAPointerInfo
  ///
  /// \return True if \p AA is a AAPointerInfo.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// Print an AAPointerInfo access description to an output stream.
///
/// \return The output stream \p OS.
///
/// \param OS The output stream.
/// \param Access The access description to print.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const AAPointerInfo::Access &Access);

/// An abstract attribute for getting assumption information.
struct AAAssumptionInfo
    : public StateWrapper<SetState<StringRef>, AbstractAttribute,
                          DenseSet<StringRef>> {
  /// Base type of this abstract attribute.
  using Base =
      StateWrapper<SetState<StringRef>, AbstractAttribute, DenseSet<StringRef>>;

  /// Create a AAAssumptionInfo for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  /// \param Known The initially known set of assumptions.
  AAAssumptionInfo(const IRPosition &IRP, Attributor &A,
                   const DenseSet<StringRef> &Known)
      : Base(IRP, Known) {}

  /// Returns true if the assumption set contains the assumption \p Assumption.
  ///
  /// \return True if the assumption set contains the assumption \p Assumption.
  ///
  /// \param Assumption The assumption string to look up.
  virtual bool hasAssumption(const StringRef Assumption) const = 0;

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAAssumptionInfo &createForPosition(const IRPosition &IRP,
                                                      Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAAssumptionInfo"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAAssumptionInfo
  ///
  /// \return True if \p AA is a AAAssumptionInfo.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract attribute for getting all assumption underlying objects.
struct AAUnderlyingObjects : AbstractAttribute {
  /// Create an AAUnderlyingObjects for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  AAUnderlyingObjects(const IRPosition &IRP) : AbstractAttribute(IRP) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.getAssociatedType()->isPtrOrPtrVectorTy())
      return false;
    return AbstractAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// See AbstractAttribute::requiresCallersForArgOrFunction
  ///
  /// \return True if callers are required for argument or function positions.
  static bool requiresCallersForArgOrFunction() { return true; }

  /// Create an abstract attribute biew for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAUnderlyingObjects &createForPosition(const IRPosition &IRP,
                                                         Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAUnderlyingObjects"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAUnderlyingObjects.
  ///
  /// \return True if \p AA is a AAUnderlyingObjects.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;

  /// Check \p Pred on all underlying objects in \p Scope collected so far.
  ///
  /// This method will evaluate \p Pred on all underlying objects in \p Scope
  /// collected so far and return true if \p Pred holds on all of them.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param Pred Callback applied to each underlying object.
  /// \param Scope The value scope for the query.
  virtual bool
  forallUnderlyingObjects(function_ref<bool(Value &)> Pred,
                          AA::ValueScope Scope = AA::Interprocedural) const = 0;
};

/// An abstract interface for identifying pointers from which loads can be
/// marked invariant.
struct AAInvariantLoadPointer : public AbstractAttribute {
  /// Create an AAInvariantLoadPointer for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  AAInvariantLoadPointer(const IRPosition &IRP) : AbstractAttribute(IRP) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.getAssociatedType()->isPointerTy())
      return false;

    return AbstractAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAInvariantLoadPointer &
  createForPosition(const IRPosition &IRP, Attributor &A);

  /// Return true if the pointer's contents are known to remain invariant.
  ///
  /// \return True if the pointer's contents are known to remain invariant.
  virtual bool isKnownInvariant() const = 0;
  /// Return true if the pointer is known locally invariant.
  ///
  /// \return True if the pointer is known locally invariant.
  virtual bool isKnownLocallyInvariant() const = 0;

  /// Return true if the pointer's contents are assumed to remain invariant.
  ///
  /// \return True if the pointer's contents are assumed to remain invariant.
  virtual bool isAssumedInvariant() const = 0;
  /// Return true if the pointer is assumed locally invariant.
  ///
  /// \return True if the pointer is assumed locally invariant.
  virtual bool isAssumedLocallyInvariant() const = 0;

  /// See AbstractAttribute::getName().
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAInvariantLoadPointer"; }

  /// See AbstractAttribute::getIdAddr().
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAInvariantLoadPointer
  ///
  /// \return True if \p AA is a AAInvariantLoadPointer.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address).
  LLVM_ABI static const char ID;
};

/// An abstract interface for address space information.
struct AAAddressSpace : public StateWrapper<BooleanState, AbstractAttribute> {
  /// Create a AAAddressSpace for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAAddressSpace(const IRPosition &IRP, Attributor &A)
      : StateWrapper<BooleanState, AbstractAttribute>(IRP) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.getAssociatedType()->isPtrOrPtrVectorTy())
      return false;
    return AbstractAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// See AbstractAttribute::requiresCallersForArgOrFunction
  ///
  /// \return True if callers are required for argument or function positions.
  static bool requiresCallersForArgOrFunction() { return true; }

  /// Return the address space of the associated value.
  ///
  /// NoAddressSpace is returned if the associated value is dead. This function
  /// is not supposed to be called if the AA is invalid.
  ///
  /// \return The address space of the associated value.
  virtual uint32_t getAddressSpace() const = 0;

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAAddressSpace &createForPosition(const IRPosition &IRP,
                                                    Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAAddressSpace"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAAddressSpace.
  ///
  /// \return True if \p AA is a AAAddressSpace.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;

protected:
  // Invalid address space which indicates the associated value is dead.
  /// Sentinel for an invalid or unknown address space.
  static const uint32_t InvalidAddressSpace = ~0U;
};

/// An abstract interface for potential address space information.
struct AANoAliasAddrSpace
    : public StateWrapper<BooleanState, AbstractAttribute> {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<BooleanState, AbstractAttribute>;
  /// Map from values to no-alias address-space ranges.
  using RangeMap = IntervalMap<unsigned, bool>;
  /// Create a AANoAliasAddrSpace for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AANoAliasAddrSpace(const IRPosition &IRP, Attributor &A)
      : Base(IRP), Map(Allocator) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.getAssociatedType()->isPtrOrPtrVectorTy())
      return false;
    return AbstractAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// See AbstractAttribute::requiresCallersForArgOrFunction
  ///
  /// \return True if callers are required for argument or function positions.
  static bool requiresCallersForArgOrFunction() { return true; }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AANoAliasAddrSpace &createForPosition(const IRPosition &IRP,
                                                        Attributor &A);
  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AANoAliasAddrSpace"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AANoAliasAddrSpace.
  ///
  /// \return True if \p AA is a AANoAliasAddrSpace.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;

protected:
  /// Allocator backing the no-alias address-space range map.
  RangeMap::Allocator Allocator;
  /// Mapping of no-alias address-space ranges.
  RangeMap Map;
};

/// Abstract attribute describing allocation size information.
struct AAAllocationInfo : public StateWrapper<BooleanState, AbstractAttribute> {
  /// Create a AAAllocationInfo for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAAllocationInfo(const IRPosition &IRP, Attributor &A)
      : StateWrapper<BooleanState, AbstractAttribute>(IRP) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (!IRP.getAssociatedType()->isPtrOrPtrVectorTy())
      return false;
    return AbstractAttribute::isValidIRPositionForInit(A, IRP);
  }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAAllocationInfo &createForPosition(const IRPosition &IRP,
                                                      Attributor &A);

  /// Return the assumed allocated size, or HasNoAllocationSize.
  ///
  /// \return The assumed allocated size, or HasNoAllocationSize.
  virtual std::optional<TypeSize> getAllocatedSize() const = 0;

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAAllocationInfo"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAAllocationInfo
  ///
  /// \return True if \p AA is a AAAllocationInfo.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Sentinel indicating no allocation size is known.
  constexpr static const std::optional<TypeSize> HasNoAllocationSize =
      std::make_optional<TypeSize>(-1, true);

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract interface for llvm::GlobalValue information interference.
struct AAGlobalValueInfo
    : public StateWrapper<BooleanState, AbstractAttribute> {
  /// Create a AAGlobalValueInfo for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAGlobalValueInfo(const IRPosition &IRP, Attributor &A)
      : StateWrapper<BooleanState, AbstractAttribute>(IRP) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (IRP.getPositionKind() != IRPosition::IRP_FLOAT)
      return false;
    auto *GV = dyn_cast<GlobalValue>(&IRP.getAnchorValue());
    if (!GV)
      return false;
    return GV->hasLocalLinkage();
  }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAGlobalValueInfo &createForPosition(const IRPosition &IRP,
                                                       Attributor &A);

  /// Return true iff \p U is a potential use of the associated global value.
  ///
  /// \return True iff \p U is a potential use of the associated global value.
  ///
  /// \param U The use of interest.
  virtual bool isPotentialUse(const Use &U) const = 0;

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAGlobalValueInfo"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAGlobalValueInfo
  ///
  /// \return True if \p AA is a AAGlobalValueInfo.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract interface for indirect call information interference.
struct AAIndirectCallInfo
    : public StateWrapper<BooleanState, AbstractAttribute> {
  /// Create a AAIndirectCallInfo for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AAIndirectCallInfo(const IRPosition &IRP, Attributor &A)
      : StateWrapper<BooleanState, AbstractAttribute>(IRP) {}

  /// See AbstractAttribute::isValidIRPositionForInit
  ///
  /// \return True if \p IRP is a valid initialization position.
  ///
  /// \param A The Attributor managing the analysis.
  /// \param IRP The IR position of interest.
  static bool isValidIRPositionForInit(Attributor &A, const IRPosition &IRP) {
    if (IRP.getPositionKind() != IRPosition::IRP_CALL_SITE)
      return false;
    auto *CB = cast<CallBase>(IRP.getCtxI());
    return CB->getOpcode() == Instruction::Call && CB->isIndirectCall() &&
           !CB->isMustTailCall();
  }

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AAIndirectCallInfo &createForPosition(const IRPosition &IRP,
                                                        Attributor &A);

  /// Call \p CB on each potential callee value and return true if all were known
  /// and \p CB returned true on all of them. Otherwise, return false.
  ///
  /// \return True if the condition described by this query holds.
  ///
  /// \param CB Callback applied to each potential callee.
  virtual bool foreachCallee(function_ref<bool(Function *)> CB) const = 0;

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AAIndirectCallInfo"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AAIndirectCallInfo.
  ///
  /// \return True if \p AA is a AAIndirectCallInfo.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// An abstract Attribute for specializing "dynamic" components of
/// denormal_fpenv to a known denormal mode.
struct AADenormalFPMath
    : public StateWrapper<DenormalFPMathState, AbstractAttribute> {
  /// Base type of this abstract attribute.
  using Base = StateWrapper<DenormalFPMathState, AbstractAttribute>;

  /// Create a AADenormalFPMath for the position \p IRP.
  ///
  /// \param IRP The IR position of this abstract attribute.
  /// \param A The Attributor this abstract attribute belongs to.
  AADenormalFPMath(const IRPosition &IRP, Attributor &A) : Base(IRP) {}

  /// Create an abstract attribute view for the position \p IRP.
  ///
  /// \return A reference to the created abstract attribute.
  ///
  /// \param IRP The IR position of interest.
  /// \param A The Attributor managing the analysis.
  LLVM_ABI static AADenormalFPMath &createForPosition(const IRPosition &IRP,
                                                      Attributor &A);

  /// See AbstractAttribute::getName()
  ///
  /// \return The name of this abstract attribute.
  StringRef getName() const override { return "AADenormalFPMath"; }

  /// See AbstractAttribute::getIdAddr()
  ///
  /// \return The address of this abstract attribute's unique ID.
  const char *getIdAddr() const override { return &ID; }

  /// This function should return true if the type of the \p AA is
  /// AADenormalFPMath.
  ///
  /// \return True if \p AA is a AADenormalFPMath.
  ///
  /// \param AA The abstract attribute to inspect.
  static bool classof(const AbstractAttribute *AA) {
    return (AA->getIdAddr() == &ID);
  }

  /// Unique ID (due to the unique address)
  LLVM_ABI static const char ID;
};

/// Run options, used by the pass manager.
enum AttributorRunOption {
  NONE = 0,  ///< Do not run the Attributor.
  MODULE = 1 << 0,  ///< Run the Attributor as a module pass.
  CGSCC = 1 << 1,  ///< Run the Attributor as a CGSCC pass.
  MODULE_LIGHT = 1 << 2,  ///< Run the lightweight Attributor as a module pass.
  CGSCC_LIGHT = 1 << 3,  ///< Run the lightweight Attributor as a CGSCC pass.

  FULL = MODULE | CGSCC,  ///< Run both module and CGSCC Attributor passes.
  LIGHT = MODULE_LIGHT | CGSCC_LIGHT  ///< Run both lightweight module and CGSCC passes.
};

namespace AA {
/// Return true if attribute \p AK is assumed for \p IRP.
///
/// Helper to avoid creating an AA for IR attributes that might already be set.
///
/// \return True if attribute \p AK is assumed for \p IRP.
///
/// \param A The Attributor managing the analysis.
/// \param QueryingAA The abstract attribute issuing the query.
/// \param IRP The IR position to query.
/// \param DepClass The dependence class to record for the query.
/// \param IsKnown Set to true if the attribute is known, not only assumed.
/// \param IgnoreSubsumingPositions If true, ignore subsuming IR positions.
/// \param AAPtr Optional out-parameter for the looked-up abstract attribute.
template <Attribute::AttrKind AK, typename AAType = AbstractAttribute>
bool hasAssumedIRAttr(Attributor &A, const AbstractAttribute *QueryingAA,
                      const IRPosition &IRP, DepClassTy DepClass, bool &IsKnown,
                      bool IgnoreSubsumingPositions = false,
                      const AAType **AAPtr = nullptr) {
  IsKnown = false;
  switch (AK) {
#define CASE(ATTRNAME, AANAME, ...)                                            \
  case Attribute::ATTRNAME: {                                                  \
    if (AANAME::isImpliedByIR(A, IRP, AK, IgnoreSubsumingPositions))           \
      return IsKnown = true;                                                   \
    if (!QueryingAA)                                                           \
      return false;                                                            \
    const auto *AA = A.getAAFor<AANAME>(*QueryingAA, IRP, DepClass);           \
    if (AAPtr)                                                                 \
      *AAPtr = reinterpret_cast<const AAType *>(AA);                           \
    if (!AA || !AA->isAssumed(__VA_ARGS__))                                    \
      return false;                                                            \
    IsKnown = AA->isKnown(__VA_ARGS__);                                        \
    return true;                                                               \
  }
    CASE(NoUnwind, AANoUnwind, );
    CASE(WillReturn, AAWillReturn, );
    CASE(NoFree, AANoFree, );
    CASE(Captures, AANoCapture, );
    CASE(NoRecurse, AANoRecurse, );
    CASE(NoReturn, AANoReturn, );
    CASE(NoSync, AANoSync, );
    CASE(NoAlias, AANoAlias, );
    CASE(NonNull, AANonNull, );
    CASE(MustProgress, AAMustProgress, );
    CASE(NoUndef, AANoUndef, );
    CASE(ReadNone, AAMemoryBehavior, AAMemoryBehavior::NO_ACCESSES);
    CASE(ReadOnly, AAMemoryBehavior, AAMemoryBehavior::NO_WRITES);
    CASE(WriteOnly, AAMemoryBehavior, AAMemoryBehavior::NO_READS);
#undef CASE
  default:
    llvm_unreachable("hasAssumedIRAttr not available for this attribute kind");
  };
}
} // namespace AA

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_ATTRIBUTOR_H
