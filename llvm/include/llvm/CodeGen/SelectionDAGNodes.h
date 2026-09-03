//===- llvm/CodeGen/SelectionDAGNodes.h - SelectionDAG Nodes ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SDNode class and derived classes, which are used to
// represent the nodes and operations present in a SelectionDAG.  These nodes
// and operations are machine code level operations, with some similarities to
// the GCC RTL representation.
//
// Clients should include the SelectionDAG.h file instead of this file directly.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SELECTIONDAGNODES_H
#define LLVM_CODEGEN_SELECTIONDAGNODES_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TypeSize.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>

namespace llvm {

class APInt;
class Constant;
class GlobalValue;
class MachineBasicBlock;
class MachineConstantPoolValue;
class MCSymbol;
class raw_ostream;
class SDNode;
class SelectionDAG;
class Type;
class Value;

/// Check the subgraph rooted at \p N for cycles.
///
/// When \p DAG is non-null, cycle diagnostics may include DAG context. When
/// \p force is true, the check runs even in builds that normally skip it.
/// @param N Root node of the subgraph to check.
/// @param DAG Optional SelectionDAG used for diagnostics.
/// @param force Force the cycle check when true.
LLVM_ABI void checkForCycles(const SDNode *N, const SelectionDAG *DAG = nullptr,
                             bool force = false);

/// Interned list of value types returned by SelectionDAG::getVTList.
///
/// Instances of this simple value class are returned by
/// SelectionDAG::getVTList(...).
struct SDVTList {
  /// Pointer to the interned value-type array.
  const EVT *VTs;
  /// Number of entries in \p VTs.
  unsigned int NumVTs;
};

namespace ISD {

  /// Node predicates

/// Return true if \p N is a constant splat BUILD_VECTOR or SPLAT_VECTOR.
///
/// If \p N is a BUILD_VECTOR or SPLAT_VECTOR node whose elements are all the
/// same constant or undefined, return true and return the constant value in
/// \p SplatValue.
/// @param N Node to inspect.
/// @param SplatValue Set to the splat constant when the predicate holds.
/// @return True if N is a constant splat BUILD_VECTOR or SPLAT_VECTOR.
LLVM_ABI bool isConstantSplatVector(const SDNode *N, APInt &SplatValue);

/// Return true if \p N is an all-ones constant splat vector.
///
/// Return true if the specified node is a BUILD_VECTOR or SPLAT_VECTOR where
/// all of the elements are ~0 or undef. If \p BuildVectorOnly is set to
/// true, it only checks BUILD_VECTOR.
/// @param N Node to inspect.
/// @param BuildVectorOnly When true, only accept BUILD_VECTOR nodes.
/// @return True if all elements are ~0 or undef.
LLVM_ABI bool isConstantSplatVectorAllOnes(const SDNode *N,
                                           bool BuildVectorOnly = false);

/// Return true if \p N is an all-zeros constant splat vector.
///
/// Return true if the specified node is a BUILD_VECTOR or SPLAT_VECTOR where
/// all of the elements are 0 or undef. If \p BuildVectorOnly is set to true, it
/// only checks BUILD_VECTOR.
/// @param N Node to inspect.
/// @param BuildVectorOnly When true, only accept BUILD_VECTOR nodes.
/// @return True if all elements are 0 or undef.
LLVM_ABI bool isConstantSplatVectorAllZeros(const SDNode *N,
                                            bool BuildVectorOnly = false);

/// Return true if \p N is a BUILD_VECTOR of all ones or undef.
/// @param N Node to inspect.
/// @return True if the specified node is a BUILD_VECTOR where all of the elements are ~0 or undef.
LLVM_ABI bool isBuildVectorAllOnes(const SDNode *N);

/// Return true if \p N is a BUILD_VECTOR of all zeros or undef.
/// @param N Node to inspect.
/// @return True if the specified node is a BUILD_VECTOR where all of the elements are 0 or undef.
LLVM_ABI bool isBuildVectorAllZeros(const SDNode *N);

/// Return true if \p N is a BUILD_VECTOR of ConstantSDNode or undef.
/// @param N Node to inspect.
/// @return True if the specified node is a BUILD_VECTOR node of all ConstantSDNode or undef.
LLVM_ABI bool isBuildVectorOfConstantSDNodes(const SDNode *N);

/// Return true if \p N is a BUILD_VECTOR of ConstantFPSDNode or undef.
/// @param N Node to inspect.
/// @return True if the specified node is a BUILD_VECTOR node of all ConstantFPSDNode or undef.
LLVM_ABI bool isBuildVectorOfConstantFPSDNodes(const SDNode *N);

/// Return true if vector elements of \p N shrink to \p NewEltSize.
///
/// Returns true if the specified node is a vector where all elements can
/// be truncated to the specified element size without a loss in meaning.
/// @param N Node to inspect.
/// @param NewEltSize Destination element size in bits.
/// @param Signed Whether truncation is signed.
/// @return True if elements can be truncated without loss of meaning.
LLVM_ABI bool isVectorShrinkable(const SDNode *N, unsigned NewEltSize,
                                 bool Signed);

/// Return true if all operands of \p N are ISD::UNDEF.
/// @param N Node to inspect.
/// @return True if the node has at least one operand and all operands of the specified node are ISD::UNDEF.
LLVM_ABI bool allOperandsUndef(const SDNode *N);

/// Return true if \p N is FREEZE(UNDEF).
/// @param N Node to inspect.
/// @return True if the specified node is FREEZE(UNDEF).
LLVM_ABI bool isFreezeUndef(const SDNode *N);

} // end namespace ISD

//===----------------------------------------------------------------------===//
/// Pair of an SDNode and a result number selecting one of its values.
///
/// Unlike LLVM values, Selection DAG nodes may return multiple
/// values as the result of a computation.  Many nodes return multiple values,
/// from loads (which define a token and a return value) to ADDC (which returns
/// a result and a carry value), to calls (which may return an arbitrary number
/// of values).
///
/// As such, each use of a SelectionDAG computation must indicate the node that
/// computes it as well as which return value to use from that node.  This pair
/// of information is represented with the SDValue value type.
class SDValue {
  friend struct DenseMapInfo<SDValue>;

  /// The node defining the value we are using.
  SDNode *Node = nullptr;
  /// Which return value of the node we are using.
  unsigned ResNo = 0;

public:
  /// Construct a null SDValue.
  SDValue() = default;
  /// Construct an SDValue referring to result \p resno of \p node.
  /// @param node Node producing the value.
  /// @param resno Result number within \p node.
  SDValue(SDNode *node, unsigned resno);

  /// Return the result index selecting a specific result in the SDNode.
  /// @return The index which selects a specific result in the SDNode.
  unsigned getResNo() const { return ResNo; }

  /// Return the SDNode which holds the desired result.
  /// @return The SDNode which holds the desired result.
  SDNode *getNode() const { return Node; }

  /// Set the SDNode referenced by this value.
  /// @param N Node to reference.
  void setNode(SDNode *N) { Node = N; }

  /// Return the referenced SDNode.
  /// @return The referenced SDNode.
  inline SDNode *operator->() const { return Node; }

  /// Return true if this SDValue equals \p O.
  /// @param O Other SDValue to compare.
  /// @return True if node and result number match.
  bool operator==(const SDValue &O) const {
    return Node == O.Node && ResNo == O.ResNo;
  }
  /// Return true if this SDValue differs from \p O.
  /// @param O Other SDValue to compare.
  /// @return True if node or result number differ.
  bool operator!=(const SDValue &O) const {
    return !operator==(O);
  }
  /// Return true if this SDValue is ordered before \p O.
  /// @param O Other SDValue to compare.
  /// @return True if (Node, ResNo) is less than \p O's pair.
  bool operator<(const SDValue &O) const {
    return std::tie(Node, ResNo) < std::tie(O.Node, O.ResNo);
  }
  /// Return true if this SDValue references a non-null node.
  /// @return True when Node is non-null.
  explicit operator bool() const {
    return Node != nullptr;
  }

  /// Return the SDValue for result \p R of the same node.
  /// @param R Result number to select.
  /// @return SDValue referring to result \p R of Node.
  SDValue getValue(unsigned R) const {
    return SDValue(Node, R);
  }

  /// Return true if the referenced return value is an operand of \p N.
  /// @param N Node whose operands are searched.
  /// @return True if the referenced return value is an operand of N.
  LLVM_ABI bool isOperandOf(const SDNode *N) const;

  /// Return the ValueType of the referenced return value.
  /// @return The ValueType of the referenced return value.
  inline EVT getValueType() const;

  /// Return the simple ValueType of the referenced return value.
  /// @return The simple ValueType of the referenced return value.
  MVT getSimpleValueType() const {
    return getValueType().getSimpleVT();
  }

  /// Return the size of the value in bits.
  ///
  /// If the value type is a scalable vector type, the scalable property will
  /// be set and the runtime size will be a positive integer multiple of the
  /// base size.
  /// @return The size of the value in bits.
  TypeSize getValueSizeInBits() const {
    return getValueType().getSizeInBits();
  }

  /// Return the fixed size in bits of the scalar value type.
  /// @return Scalar value size in bits.
  uint64_t getScalarValueSizeInBits() const {
    return getValueType().getScalarType().getFixedSizeInBits();
  }

  // Forwarding methods - These forward to the corresponding methods in SDNode.
  /// Return the opcode of the referenced node.
  /// @return Opcode of Node.
  inline unsigned getOpcode() const;
  /// Return the number of operands of the referenced node.
  /// @return Operand count of Node.
  inline unsigned getNumOperands() const;
  /// Return operand \p i of the referenced node.
  /// @param i Operand index.
  /// @return Operand \p i.
  inline const SDValue &getOperand(unsigned i) const;
  /// Return the integer value of ConstantSDNode operand \p i.
  /// @param i Operand index.
  /// @return Zero-extended constant integer value.
  inline uint64_t getConstantOperandVal(unsigned i) const;
  /// Return the APInt of ConstantSDNode operand \p i.
  /// @param i Operand index.
  /// @return APInt constant value.
  inline const APInt &getConstantOperandAPInt(unsigned i) const;
  /// Return true if the referenced node has a target-specific opcode.
  /// @return True for target opcodes.
  inline bool isTargetOpcode() const;
  /// Return true if the referenced node has a machine opcode.
  /// @return True for machine opcodes.
  inline bool isMachineOpcode() const;
  /// Return true if the referenced node is UNDEF or POISON.
  /// @return True for undef/poison nodes.
  inline bool isUndef() const;
  /// Return true if the referenced node is ADD or PTRADD.
  /// @return True for ADD or PTRADD.
  inline bool isAnyAdd() const;
  /// Return the machine opcode of the referenced node.
  /// @return MachineInstr opcode.
  inline unsigned getMachineOpcode() const;
  /// Return the debug location of the referenced node.
  /// @return DebugLoc of Node.
  inline const DebugLoc &getDebugLoc() const;
  /// Dump the referenced node for debugging.
  inline void dump() const;
  /// Dump the referenced node using SelectionDAG \p G.
  /// @param G SelectionDAG providing target formatting.
  inline void dump(const SelectionDAG *G) const;
  /// Recursively dump the referenced node and its use-def subgraph.
  inline void dumpr() const;
  /// Recursively dump using SelectionDAG \p G.
  /// @param G SelectionDAG providing target formatting.
  inline void dumpr(const SelectionDAG *G) const;

  /// Return true if this chain reaches \p Dest without side effects.
  ///
  /// Return true if this operand (which must be a chain) reaches the
  /// specified operand without crossing any side-effecting instructions.
  /// In practice, this looks through token factors and non-volatile loads.
  /// In order to remain efficient, this only
  /// looks a couple of nodes in, it does not do an exhaustive search.
  /// @param Dest Chain value to reach.
  /// @param Depth Maximum search depth (default 2).
  /// @return True if Dest is reached without side-effecting ops.
  LLVM_ABI bool reachesChainWithoutSideEffects(SDValue Dest,
                                               unsigned Depth = 2) const;

  /// Return true if there are no nodes using value ResNo of Node.
  /// @return True if there are no nodes using value ResNo of Node.
  inline bool use_empty() const;

  /// Return true if exactly one use references this value.
  ///
  /// Return true if there is exactly one node using value ResNo of Node, in
  /// exactly one operand.
  /// @return True if there is exactly one use of this value.
  inline bool hasOneUse() const;

  /// Return true if exactly one user references this value.
  ///
  /// Return true if there is exactly one node using value ResNo of Node, in
  /// potentially multiple operands.
  /// @return True if there is exactly one user of this value.
  inline bool hasOneUser() const;
};

/// DenseMapInfo specialization for SDValue keys.
template <> struct DenseMapInfo<SDValue> {
  /// Hash SDValue \p Val for DenseMap.
  /// @param Val Value to hash.
  /// @return Hash combining node pointer and result number.
  static unsigned getHashValue(const SDValue &Val) {
    return DenseMapInfo<const void *>::getHashValue(Val.getNode()) +
           Val.getResNo();
  }

  /// Return true if \p LHS equals \p RHS.
  /// @param LHS Left operand.
  /// @param RHS Right operand.
  /// @return True when the SDValues compare equal.
  static bool isEqual(const SDValue &LHS, const SDValue &RHS) {
    return LHS == RHS;
  }
};

/// Allow casting operators to work directly on SDValues as SDNode pointers.
template<> struct simplify_type<SDValue> {
  /// Simplified type used by casting operators.
  using SimpleType = SDNode *;

  /// Return the SDNode pointer for \p Val.
  /// @param Val SDValue to simplify.
  /// @return Underlying SDNode pointer.
  static SimpleType getSimplifiedValue(SDValue &Val) {
    return Val.getNode();
  }
};
/// Allow casting operators to work on const SDValues as SDNode pointers.
template<> struct simplify_type<const SDValue> {
  /// Simplified type used by casting operators.
  using SimpleType = /*const*/ SDNode *;

  /// Return the SDNode pointer for const \p Val.
  /// @param Val Const SDValue to simplify.
  /// @return Underlying SDNode pointer.
  static SimpleType getSimplifiedValue(const SDValue &Val) {
    return Val.getNode();
  }
};

/// Represents a use of an SDNode value in another node's operand list.
///
/// This class holds an SDValue, which records the SDNode being used and the
/// result number, a pointer to the SDNode using the value, and Next and Prev
/// pointers, which link together all the uses of an SDNode.
class SDUse {
  /// The value being used.
  SDValue Val;
  /// The user of this value.
  SDNode *User = nullptr;
  /// Pointers to the uses list of the SDNode referred by this operand.
  SDUse **Prev = nullptr;
  SDUse *Next = nullptr;

public:
  /// Construct an empty SDUse.
  SDUse() = default;
  /// SDUse is non-copyable.
  SDUse(const SDUse &U) = delete;
  /// SDUse is non-assignable.
  /// @param U Unused; copy assignment is deleted.
  SDUse &operator=(const SDUse &U) = delete;

  /// Implicitly convert to the held SDValue.
  /// @return The SDValue held by this use.
  operator const SDValue&() const { return Val; }

  /// Return the held SDValue.
  /// @return The SDValue held by this use.
  const SDValue &get() const { return Val; }

  /// Return the SDNode that contains this Use.
  /// @return The SDNode that contains this Use.
  SDNode *getUser() { return User; }
  /// Return the const SDNode that contains this Use.
  /// @return The const SDNode that contains this Use.
  const SDNode *getUser() const { return User; }

  /// Return the next SDUse in the use list.
  /// @return The next SDUse in the use list.
  SDUse *getNext() const { return Next; }

  /// Return the operand number of this use in its user.
  /// @return The operand # of this use in its user.
  inline unsigned getOperandNo() const;

  /// Return the SDNode producing the used value.
  /// @return The SDNode producing the used value.
  SDNode *getNode() const { return Val.getNode(); }
  /// Return the result number of the used value.
  /// @return The result number of the used value.
  unsigned getResNo() const { return Val.getResNo(); }
  /// Return the ValueType of the used value.
  /// @return The ValueType of the used value.
  EVT getValueType() const { return Val.getValueType(); }

  /// Return true if the held SDValue equals \p V.
  /// @param V Value to compare.
  /// @return True if the held SDValue equals \p V.
  bool operator==(const SDValue &V) const {
    return Val == V;
  }

  /// Return true if the held SDValue differs from \p V.
  /// @param V Value to compare.
  /// @return True if the held SDValue differs from \p V.
  bool operator!=(const SDValue &V) const {
    return Val != V;
  }

  /// Return true if the held SDValue is less than \p V.
  /// @param V Value to compare.
  /// @return True if the held SDValue is less than \p V.
  bool operator<(const SDValue &V) const {
    return Val < V;
  }

private:
  friend class SelectionDAG;
  friend class SDNode;
  // TODO: unfriend HandleSDNode once we fix its operand handling.
  friend class HandleSDNode;

  /// Set the user node containing this use.
  /// @param p User SDNode.
  void setUser(SDNode *p) { User = p; }

  /// Remove this use from its list, assign \p V, and reinsert.
  /// @param V New value to use.
  inline void set(const SDValue &V);
  /// Initialize a newly allocated SDUse with non-null \p V.
  /// @param V Initial value to use.
  inline void setInitial(const SDValue &V);
  /// Set only the Node portion of the value, leaving ResNo unchanged.
  /// @param N New node pointer.
  inline void setNode(SDNode *N);

  /// Insert this use at the front of \p List.
  /// @param List Head pointer of the use list.
  void addToList(SDUse **List) {
    Next = *List;
    if (Next) Next->Prev = &Next;
    Prev = List;
    *List = this;
  }

  /// Remove this use from its current use list.
  void removeFromList() {
    *Prev = Next;
    if (Next) Next->Prev = Prev;
  }
};

/// Allow casting operators to work directly on SDUse as SDNode pointers.
template<> struct simplify_type<SDUse> {
  /// Simplified type used by casting operators.
  using SimpleType = SDNode *;

  /// Return the SDNode pointer for \p Val.
  /// @param Val SDUse to simplify.
  /// @return Underlying SDNode pointer.
  static SimpleType getSimplifiedValue(SDUse &Val) {
    return Val.getNode();
  }
};

/// IR-level optimization flags that may be propagated to SDNodes.
///
/// TODO: This data structure should be shared by the IR optimizer and the
/// the backend.
struct SDNodeFlags {
private:
  friend class SDNode;

  unsigned Flags = 0;

  template <unsigned Flag> void setFlag(bool B) {
    Flags = (Flags & ~Flag) | (B ? Flag : 0);
  }

public:
  /// Bitmask flag values stored in SDNodeFlags.
  enum : unsigned {
    /// No flags set.
    None = 0,
    /// Result is defined for unsigned wrap (nuw).
    NoUnsignedWrap = 1 << 0,
    /// Result is defined for signed wrap (nsw).
    NoSignedWrap = 1 << 1,
    /// Combined no-wrap flags (nuw|nsw).
    NoWrap = NoUnsignedWrap | NoSignedWrap,
    /// Division or shift has an exact result.
    Exact = 1 << 2,
    /// Or operands are known disjoint.
    Disjoint = 1 << 3,
    /// Value is known non-negative.
    NonNeg = 1 << 4,
    /// FP operation assumes no NaNs.
    NoNaNs = 1 << 5,
    /// FP operation assumes no infinities.
    NoInfs = 1 << 6,
    /// FP operation may ignore the sign of zero.
    NoSignedZeros = 1 << 7,
    /// Reciprocal estimates are allowed.
    AllowReciprocal = 1 << 8,
    /// FP contraction (e.g. FMA) is allowed.
    AllowContract = 1 << 9,
    /// Approximate libm-style functions are allowed.
    ApproximateFuncs = 1 << 10,
    /// Reassociation of FP operations is allowed.
    AllowReassociation = 1 << 11,

    // We assume instructions do not raise floating-point exceptions by default,
    // and only those marked explicitly may do so.  We could choose to represent
    // this via a positive "FPExcept" flags like on the MI level, but having a
    // negative "NoFPExcept" flag here makes the flag intersection logic more
    // straightforward.
    /// Operation is known not to raise FP exceptions.
    NoFPExcept = 1 << 12,
    // Instructions with attached 'unpredictable' metadata on IR level.
    /// Operation is marked unpredictable in IR metadata.
    Unpredictable = 1 << 13,
    // Compare instructions which may carry the samesign flag.
    /// Compare operands are known to have the same sign.
    SameSign = 1 << 14,
    // ISD::PTRADD operations that remain in bounds, i.e., the left operand is
    // an address in a memory object in which the result of the operation also
    // lies. WARNING: Since SDAG generally uses integers instead of pointer
    // types, a PTRADD's pointer operand is effectively the result of an
    // implicit inttoptr cast. Therefore, when an inbounds PTRADD uses a
    // pointer P, transformations cannot assume that P has the provenance
    // implied by its producer as, e.g, operations between producer and PTRADD
    // that affect the provenance may have been optimized away.
    /// PTRADD result stays in bounds of the pointed-to object.
    InBounds = 1 << 15,

    // Call does not require convergence guarantees.
    /// Call does not require convergent execution guarantees.
    NoConvergent = 1 << 16,

    // NOTE: Please update LargestValue in LLVM_DECLARE_ENUM_AS_BITMASK below
    // the class definition when adding new flags.

    /// Flags that can produce poison when violated.
    PoisonGeneratingFlags = NoUnsignedWrap | NoSignedWrap | Exact | Disjoint |
                            NonNeg | NoNaNs | NoInfs | SameSign | InBounds,
    /// Subset of flags corresponding to IR fast-math flags.
    FastMathFlags = NoNaNs | NoInfs | NoSignedZeros | AllowReciprocal |
                    AllowContract | ApproximateFuncs | AllowReassociation,
  };

  /// Construct flags, optionally from raw \p Flags bits.
  /// @param Flags Initial flag bits (default none).
  SDNodeFlags(unsigned Flags = SDNodeFlags::None) : Flags(Flags) {}

  /// Propagate the fast-math-flags from an IR FPMathOperator.
  /// @param FPMO IR operator providing fast-math flags.
  void copyFMF(const FPMathOperator &FPMO) {
    setNoNaNs(FPMO.hasNoNaNs());
    setNoInfs(FPMO.hasNoInfs());
    setNoSignedZeros(FPMO.hasNoSignedZeros());
    setAllowReciprocal(FPMO.hasAllowReciprocal());
    setAllowContract(FPMO.hasAllowContract());
    setApproximateFuncs(FPMO.hasApproxFunc());
    setAllowReassociation(FPMO.hasAllowReassoc());
  }

  // These are mutators for each flag.
  /// Set or clear the no-unsigned-wrap flag.
  /// @param b New flag value.
  void setNoUnsignedWrap(bool b) { setFlag<NoUnsignedWrap>(b); }
  /// Set or clear the no-signed-wrap flag.
  /// @param b New flag value.
  void setNoSignedWrap(bool b) { setFlag<NoSignedWrap>(b); }
  /// Set or clear the exact flag.
  /// @param b New flag value.
  void setExact(bool b) { setFlag<Exact>(b); }
  /// Set or clear the disjoint flag.
  /// @param b New flag value.
  void setDisjoint(bool b) { setFlag<Disjoint>(b); }
  /// Set or clear the same-sign flag.
  /// @param b New flag value.
  void setSameSign(bool b) { setFlag<SameSign>(b); }
  /// Set or clear the non-negative flag.
  /// @param b New flag value.
  void setNonNeg(bool b) { setFlag<NonNeg>(b); }
  /// Set or clear the no-NaNs flag.
  /// @param b New flag value.
  void setNoNaNs(bool b) { setFlag<NoNaNs>(b); }
  /// Set or clear the no-infs flag.
  /// @param b New flag value.
  void setNoInfs(bool b) { setFlag<NoInfs>(b); }
  /// Set or clear the no-signed-zeros flag.
  /// @param b New flag value.
  void setNoSignedZeros(bool b) { setFlag<NoSignedZeros>(b); }
  /// Set or clear the allow-reciprocal flag.
  /// @param b New flag value.
  void setAllowReciprocal(bool b) { setFlag<AllowReciprocal>(b); }
  /// Set or clear the allow-contract flag.
  /// @param b New flag value.
  void setAllowContract(bool b) { setFlag<AllowContract>(b); }
  /// Set or clear the approximate-funcs flag.
  /// @param b New flag value.
  void setApproximateFuncs(bool b) { setFlag<ApproximateFuncs>(b); }
  /// Set or clear the allow-reassociation flag.
  /// @param b New flag value.
  void setAllowReassociation(bool b) { setFlag<AllowReassociation>(b); }
  /// Set or clear the no-FP-except flag.
  /// @param b New flag value.
  void setNoFPExcept(bool b) { setFlag<NoFPExcept>(b); }
  /// Set or clear the unpredictable flag.
  /// @param b New flag value.
  void setUnpredictable(bool b) { setFlag<Unpredictable>(b); }
  /// Set or clear the inbounds flag.
  /// @param b New flag value.
  void setInBounds(bool b) { setFlag<InBounds>(b); }
  /// Set or clear the no-convergent flag.
  /// @param b New flag value.
  void setNoConvergent(bool b) { setFlag<NoConvergent>(b); }

  // These are accessors for each flag.
  /// Return true if no-unsigned-wrap is set.
  /// @return True when NoUnsignedWrap is set.
  bool hasNoUnsignedWrap() const { return Flags & NoUnsignedWrap; }
  /// Return true if no-signed-wrap is set.
  /// @return True when NoSignedWrap is set.
  bool hasNoSignedWrap() const { return Flags & NoSignedWrap; }
  /// Return true if exact is set.
  /// @return True when Exact is set.
  bool hasExact() const { return Flags & Exact; }
  /// Return true if disjoint is set.
  /// @return True when Disjoint is set.
  bool hasDisjoint() const { return Flags & Disjoint; }
  /// Return true if same-sign is set.
  /// @return True when SameSign is set.
  bool hasSameSign() const { return Flags & SameSign; }
  /// Return true if non-negative is set.
  /// @return True when NonNeg is set.
  bool hasNonNeg() const { return Flags & NonNeg; }
  /// Return true if no-NaNs is set.
  /// @return True when NoNaNs is set.
  bool hasNoNaNs() const { return Flags & NoNaNs; }
  /// Return true if no-infs is set.
  /// @return True when NoInfs is set.
  bool hasNoInfs() const { return Flags & NoInfs; }
  /// Return true if no-signed-zeros is set.
  /// @return True when NoSignedZeros is set.
  bool hasNoSignedZeros() const { return Flags & NoSignedZeros; }
  /// Return true if allow-reciprocal is set.
  /// @return True when AllowReciprocal is set.
  bool hasAllowReciprocal() const { return Flags & AllowReciprocal; }
  /// Return true if allow-contract is set.
  /// @return True when AllowContract is set.
  bool hasAllowContract() const { return Flags & AllowContract; }
  /// Return true if approximate-funcs is set.
  /// @return True when ApproximateFuncs is set.
  bool hasApproximateFuncs() const { return Flags & ApproximateFuncs; }
  /// Return true if allow-reassociation is set.
  /// @return True when AllowReassociation is set.
  bool hasAllowReassociation() const { return Flags & AllowReassociation; }
  /// Return true if no-FP-except is set.
  /// @return True when NoFPExcept is set.
  bool hasNoFPExcept() const { return Flags & NoFPExcept; }
  /// Return true if unpredictable is set.
  /// @return True when Unpredictable is set.
  bool hasUnpredictable() const { return Flags & Unpredictable; }
  /// Return true if inbounds is set.
  /// @return True when InBounds is set.
  bool hasInBounds() const { return Flags & InBounds; }
  /// Return true if no-convergent is set.
  /// @return True when NoConvergent is set.
  bool hasNoConvergent() const { return Flags & NoConvergent; }

  /// Return true if these flags equal \p Other.
  /// @param Other Flags to compare.
  /// @return True when flag bits match.
  bool operator==(const SDNodeFlags &Other) const {
    return Flags == Other.Flags;
  }
  /// Intersect these flags with \p OtherFlags in place.
  /// @param OtherFlags Flags to AND with.
  void operator&=(const SDNodeFlags &OtherFlags) { Flags &= OtherFlags.Flags; }
  /// Union these flags with \p OtherFlags in place.
  /// @param OtherFlags Flags to OR with.
  void operator|=(const SDNodeFlags &OtherFlags) { Flags |= OtherFlags.Flags; }
};

/// Trait specialization marking SDNodeFlags bitmask enumerators as a bitmask enum.
template <> struct is_bitmask_enum<decltype(SDNodeFlags::None)> : std::true_type {};
/// Trait specialization giving the largest SDNodeFlags bitmask enumerator value.
template <> struct largest_bitmask_enum_bit<decltype(SDNodeFlags::None)> {
  /// Largest individual SDNodeFlags enumerator value.
  static constexpr std::underlying_type_t<decltype(SDNodeFlags::None)> value =
      SDNodeFlags::NoConvergent;
};

/// Return the union of flag sets \p LHS and \p RHS.
/// @param LHS Left-hand flags.
/// @param RHS Right-hand flags.
/// @return Bitwise OR of the flag sets.
inline SDNodeFlags operator|(SDNodeFlags LHS, SDNodeFlags RHS) {
  LHS |= RHS;
  return LHS;
}

/// Return the intersection of flag sets \p LHS and \p RHS.
/// @param LHS Left-hand flags.
/// @param RHS Right-hand flags.
/// @return Bitwise AND of the flag sets.
inline SDNodeFlags operator&(SDNodeFlags LHS, SDNodeFlags RHS) {
  LHS &= RHS;
  return LHS;
}

/// Represents one node in the SelectionDAG.
///
class SDNode : public FoldingSetNode, public ilist_node<SDNode> {
private:
  /// The operation that this node performs.
  int32_t NodeType;

  SDNodeFlags Flags;

protected:
  // We define a set of mini-helper classes to help us interpret the bits in our
  // SubclassData.  These are designed to fit within a uint16_t so they pack
  // with SDNodeFlags.

#if defined(_AIX) && (!defined(__GNUC__) || defined(__clang__))
// Except for GCC; by default, AIX compilers store bit-fields in 4-byte words
// and give the `pack` pragma push semantics.
#define BEGIN_TWO_BYTE_PACK() _Pragma("pack(2)")
#define END_TWO_BYTE_PACK() _Pragma("pack(pop)")
#else
#define BEGIN_TWO_BYTE_PACK()
#define END_TWO_BYTE_PACK()
#endif

BEGIN_TWO_BYTE_PACK()
  /// Bitfields shared by all SDNode subclasses in SubclassData.
  class SDNodeBitfields {
    friend class SDNode;
    friend class MemIntrinsicSDNode;
    friend class MemSDNode;
    friend class SelectionDAG;

    uint16_t HasDebugValue : 1;
    uint16_t IsMemIntrinsic : 1;
    uint16_t IsDivergent : 1;
  };
  /// Width of SDNodeBitfields in SubclassData.
  enum {
    /// Number of bits used by SDNodeBitfields.
    NumSDNodeBits = 3
  };

  /// Bitfields specific to ConstantSDNode.
  class ConstantSDNodeBitfields {
    friend class ConstantSDNode;

    uint16_t : NumSDNodeBits;

    uint16_t IsOpaque : 1;
  };

  /// Bitfields shared by memory-accessing SDNode subclasses.
  class MemSDNodeBitfields {
    friend class MemSDNode;
    friend class MemIntrinsicSDNode;
    friend class AtomicSDNode;

    uint16_t : NumSDNodeBits;

    uint16_t IsVolatile : 1;
    uint16_t IsNonTemporal : 1;
    uint16_t IsDereferenceable : 1;
    uint16_t IsInvariant : 1;
  };
  /// Width of MemSDNodeBitfields, including SDNode bits.
  enum {
    /// Number of bits used by MemSDNodeBitfields including SDNode bits.
    NumMemSDNodeBits = NumSDNodeBits + 4
  };

  /// Bitfields for load/store addressing mode and related hierarchies.
  class LSBaseSDNodeBitfields {
    friend class LSBaseSDNode;
    friend class VPBaseLoadStoreSDNode;
    friend class MaskedLoadStoreSDNode;
    friend class MaskedGatherScatterSDNode;
    friend class VPGatherScatterSDNode;
    friend class MaskedHistogramSDNode;

    uint16_t : NumMemSDNodeBits;

    // This storage is shared between disparate class hierarchies to hold an
    // enumeration specific to the class hierarchy in use.
    //   LSBaseSDNode => enum ISD::MemIndexedMode
    //   VPLoadStoreBaseSDNode => enum ISD::MemIndexedMode
    //   MaskedLoadStoreBaseSDNode => enum ISD::MemIndexedMode
    //   VPGatherScatterSDNode => enum ISD::MemIndexType
    //   MaskedGatherScatterSDNode => enum ISD::MemIndexType
    //   MaskedHistogramSDNode => enum ISD::MemIndexType
    uint16_t AddressingMode : 3;
  };
  /// Width of LSBaseSDNodeBitfields, including MemSDNode bits.
  enum {
    /// Number of bits used by LSBaseSDNodeBitfields including mem bits.
    NumLSBaseSDNodeBits = NumMemSDNodeBits + 3
  };

  /// Bitfields specific to load-like SDNode subclasses.
  class LoadSDNodeBitfields {
    friend class LoadSDNode;
    friend class AtomicSDNode;
    friend class VPLoadSDNode;
    friend class VPStridedLoadSDNode;
    friend class MaskedLoadSDNode;
    friend class MaskedGatherSDNode;
    friend class VPGatherSDNode;
    friend class MaskedHistogramSDNode;

    uint16_t : NumLSBaseSDNodeBits;

    uint16_t ExtTy : 2; // enum ISD::LoadExtType
    uint16_t IsExpanding : 1;
  };

  /// Bitfields specific to store-like SDNode subclasses.
  class StoreSDNodeBitfields {
    friend class StoreSDNode;
    friend class VPStoreSDNode;
    friend class VPStridedStoreSDNode;
    friend class MaskedStoreSDNode;
    friend class MaskedScatterSDNode;
    friend class VPScatterSDNode;

    uint16_t : NumLSBaseSDNodeBits;

    uint16_t IsTruncating : 1;
    uint16_t IsCompressing : 1;
  };

  union {
    /// Raw bytes overlaying subclass bitfields.
    char RawSDNodeBits[sizeof(uint16_t)];
    /// Base SDNode bitfields.
    SDNodeBitfields SDNodeBits;
    /// ConstantSDNode bitfields.
    ConstantSDNodeBitfields ConstantSDNodeBits;
    /// MemSDNode bitfields.
    MemSDNodeBitfields MemSDNodeBits;
    /// LSBaseSDNode bitfields.
    LSBaseSDNodeBitfields LSBaseSDNodeBits;
    /// LoadSDNode bitfields.
    LoadSDNodeBitfields LoadSDNodeBits;
    /// StoreSDNode bitfields.
    StoreSDNodeBitfields StoreSDNodeBits;
  };
END_TWO_BYTE_PACK()
#undef BEGIN_TWO_BYTE_PACK
#undef END_TWO_BYTE_PACK

  // RawSDNodeBits must cover the entirety of the union.  This means that all of
  // the union's members must have size <= RawSDNodeBits.  We write the RHS as
  // "2" instead of sizeof(RawSDNodeBits) because MSVC can't handle the latter.
  static_assert(sizeof(SDNodeBitfields) <= 2, "field too wide");
  static_assert(sizeof(ConstantSDNodeBitfields) <= 2, "field too wide");
  static_assert(sizeof(MemSDNodeBitfields) <= 2, "field too wide");
  static_assert(sizeof(LSBaseSDNodeBitfields) <= 2, "field too wide");
  static_assert(sizeof(LoadSDNodeBitfields) <= 2, "field too wide");
  static_assert(sizeof(StoreSDNodeBitfields) <= 2, "field too wide");

public:
  /// Unique persistent id per SDNode used for debug printing.
  ///
  /// We do not place that under `#if LLVM_ENABLE_ABI_BREAKING_CHECKS`
  /// intentionally because it adds unneeded complexity without noticeable
  /// benefits (see discussion with @thakis in D120714). Currently, there are
  /// two padding bytes after this field.
  uint16_t PersistentId = 0xffff;

private:
  friend class SelectionDAG;
  // TODO: unfriend HandleSDNode once we fix its operand handling.
  friend class HandleSDNode;

  /// Unique id per SDNode in the DAG.
  int NodeId = -1;

  /// The values that are used by this operation.
  SDUse *OperandList = nullptr;

  /// The types of the values this node defines.  SDNode's may
  /// define multiple values simultaneously.
  const EVT *ValueList;

  /// List of uses for this SDNode.
  SDUse *UseList = nullptr;

  /// The number of entries in the Operand/Value list.
  unsigned short NumOperands = 0;
  unsigned short NumValues;

  // The ordering of the SDNodes. It roughly corresponds to the ordering of the
  // original LLVM instructions.
  // This is used for turning off scheduling, because we'll forgo
  // the normal scheduling algorithms and output the instructions according to
  // this ordering.
  unsigned IROrder;

  /// Source line information.
  DebugLoc debugLoc;

  /// Return a pointer to the specified value type.
  LLVM_ABI static const EVT *getValueTypeList(MVT VT);

  union {
    /// DAGCombiner worklist index, or a negative sentinel.
    ///
    /// -1 = not in worklist; -2 = not in worklist, but has already been combined
    /// at least once.
    int CombinerWorklistIndex = -1;
    /// Visited state in ScheduleDAGSDNodes::BuildSchedUnits.
    bool SchedulerWorklistVisited;
  };

  uint32_t CFIType = 0;

public:
  //===--------------------------------------------------------------------===//
  //  Accessors
  //

  /// Return the SelectionDAG opcode value for this node.
  ///
  /// For pre-isel nodes (those for which isMachineOpcode returns false), these
  /// are the opcode values in the ISD and <target>ISD namespaces. For
  /// post-isel opcodes, see getMachineOpcode.
  /// @return The SelectionDAG opcode value for this node.
  unsigned getOpcode()  const { return (unsigned)NodeType; }

  /// Test if this node has a target-specific opcode (in the
  /// \<target\>ISD namespace).
  /// @return True if this node has a target-specific opcode.
  bool isTargetOpcode() const { return NodeType >= ISD::BUILTIN_OP_END; }

  /// Returns true if the node type is UNDEF or POISON.
  /// @return True if the node type is UNDEF or POISON.
  bool isUndef() const {
    return NodeType == ISD::UNDEF || NodeType == ISD::POISON;
  }

  /// Returns true if the node type is ADD or PTRADD.
  /// @return True if the node type is ADD or PTRADD.
  bool isAnyAdd() const {
    return NodeType == ISD::ADD || NodeType == ISD::PTRADD;
  }

  /// Test if this node is a memory intrinsic (with valid pointer information).
  /// @return True if this node is a memory intrinsic (with valid pointer information).
  bool isMemIntrinsic() const { return SDNodeBits.IsMemIntrinsic; }

  /// Test if this node is a strict floating point pseudo-op.
  /// @return True if this node is a strict floating point pseudo-op.
  bool isStrictFPOpcode() {
    switch (NodeType) {
      default:
        return false;
      case ISD::STRICT_FP16_TO_FP:
      case ISD::STRICT_FP_TO_FP16:
      case ISD::STRICT_BF16_TO_FP:
      case ISD::STRICT_FP_TO_BF16:
#define DAG_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
      case ISD::STRICT_##DAGN:
#include "llvm/IR/ConstrainedOps.def"
        return true;
    }
  }

  /// Test if this node is an assert operation.
  /// @return True if this node is an assert operation.
  bool isAssert() const {
    switch (NodeType) {
    default:
      return false;
    case ISD::AssertAlign:
    case ISD::AssertNoFPClass:
    case ISD::AssertSext:
    case ISD::AssertZext:
      return true;
    }
  }

  /// Test if this node is a vector predication operation.
  /// @return True if this node is a vector predication operation.
  bool isVPOpcode() const { return ISD::isVPOpcode(getOpcode()); }

  /// Test if this node has a post-isel opcode, directly
  /// corresponding to a MachineInstr opcode.
  /// @return True if this node has a post-isel opcode, directly corresponding to a MachineInstr opcode.
  bool isMachineOpcode() const { return NodeType < 0; }

  /// This may only be called if isMachineOpcode returns
  /// true. It returns the MachineInstr opcode value that the node's opcode
  /// corresponds to.
  /// @return The MachineInstr opcode corresponding to this node's opcode.
  unsigned getMachineOpcode() const {
    assert(isMachineOpcode() && "Not a MachineInstr opcode!");
    return ~NodeType;
  }

  /// Return true if this node has an associated debug value.
  /// @return True when HasDebugValue is set.
  bool getHasDebugValue() const { return SDNodeBits.HasDebugValue; }
  /// Set whether this node has an associated debug value.
  /// @param b New HasDebugValue flag.
  void setHasDebugValue(bool b) { SDNodeBits.HasDebugValue = b; }

  /// Return true if this node is divergent.
  /// @return True when the divergent bit is set.
  bool isDivergent() const { return SDNodeBits.IsDivergent; }

  /// Return true if there are no uses of this node.
  /// @return True if there are no uses of this node.
  bool use_empty() const { return UseList == nullptr; }

  /// Return true if there is exactly one use of this node.
  /// @return True if there is exactly one use of this node.
  bool hasOneUse() const { return hasSingleElement(uses()); }

  /// Return the number of uses of this node. This method takes
  /// time proportional to the number of uses.
  /// @return The number of uses of this node.
  size_t use_size() const { return std::distance(use_begin(), use_end()); }

  /// Return the unique node id.
  /// @return The unique node id.
  int getNodeId() const { return NodeId; }

  /// Set unique node id.
  /// @param Id New node identifier.
  void setNodeId(int Id) { NodeId = Id; }

  /// Get worklist index for DAGCombiner
  /// @return Worklist index for DAGCombiner.
  int getCombinerWorklistIndex() const { return CombinerWorklistIndex; }

  /// Set worklist index for DAGCombiner.
  /// @param Index New worklist index or sentinel.
  void setCombinerWorklistIndex(int Index) { CombinerWorklistIndex = Index; }

  /// Get visited state for ScheduleDAGSDNodes::BuildSchedUnits.
  /// @return Visited state for ScheduleDAGSDNodes::BuildSchedUnits.
  bool getSchedulerWorklistVisited() const { return SchedulerWorklistVisited; }

  /// Set visited state for ScheduleDAGSDNodes::BuildSchedUnits.
  /// @param Visited New visited flag.
  void setSchedulerWorklistVisited(bool Visited) {
    SchedulerWorklistVisited = Visited;
  }

  /// Return the node ordering.
  /// @return The node ordering.
  unsigned getIROrder() const { return IROrder; }

  /// Set the node ordering.
  /// @param Order New IR order.
  void setIROrder(unsigned Order) { IROrder = Order; }

  /// Return the source location info.
  /// @return The source location info.
  const DebugLoc &getDebugLoc() const { return debugLoc; }

  /// Set source location info.  Try to avoid this, putting
  /// it in the constructor is preferable.
  /// @param dl New debug location.
  void setDebugLoc(DebugLoc dl) { debugLoc = std::move(dl); }

  /// Iterator over SDUse operands that use a specific SDNode.
  class use_iterator {
    friend class SDNode;

    SDUse *Op = nullptr;

    /// Construct an iterator at use \p op.
    /// @param op Current use, or null for end.
    explicit use_iterator(SDUse *op) : Op(op) {}

  public:
    /// Iterator category tag.
    using iterator_category = std::forward_iterator_tag;
    /// Value type of the iterator.
    using value_type = SDUse;
    /// Difference type of the iterator.
    using difference_type = std::ptrdiff_t;
    /// Pointer type of the iterator.
    using pointer = value_type *;
    /// Reference type of the iterator.
    using reference = value_type &;

    /// Construct a singular (end) iterator.
    use_iterator() = default;
    /// Copy-construct a use iterator.
    /// @param I Iterator to copy.
    use_iterator(const use_iterator &I) = default;
    /// Copy-assign a use iterator.
    /// @param I Source iterator.
    /// @return Reference to this iterator.
    use_iterator &operator=(const use_iterator &I) = default;

    /// Return true if this iterator equals \p x.
    /// @param x Other iterator.
    /// @return True when both point to the same use.
    bool operator==(const use_iterator &x) const { return Op == x.Op; }
    /// Return true if this iterator differs from \p x.
    /// @param x Other iterator.
    /// @return True when the iterators differ.
    bool operator!=(const use_iterator &x) const {
      return !operator==(x);
    }

    // Iterator traversal: forward iteration only.
    /// Advance to the next use (preincrement).
    /// @return Reference to this iterator.
    use_iterator &operator++() {          // Preincrement
      assert(Op && "Cannot increment end iterator!");
      Op = Op->getNext();
      return *this;
    }

    /// Advance to the next use (postincrement).
    /// @param Unused Unused postfix-discriminator parameter.
    /// @return Copy of the prior iterator.
    use_iterator operator++(int Unused) {        // Postincrement
      use_iterator tmp = *this; ++*this; return tmp;
    }

    /// Retrieve a reference to the current SDUse.
    /// @return A reference to the current SDUse.
    SDUse &operator*() const {
      assert(Op && "Cannot dereference end iterator!");
      return *Op;
    }

    /// Retrieve a pointer to the current SDUse.
    /// @return Pointer to the current SDUse.
    SDUse *operator->() const { return &operator*(); }
  };

  /// Iterator over SDNodes that use a specific SDNode.
  class user_iterator {
    friend class SDNode;
    use_iterator UI;

    /// Construct a user iterator at use \p op.
    /// @param op Current use, or null for end.
    explicit user_iterator(SDUse *op) : UI(op) {};

  public:
    /// Iterator category tag.
    using iterator_category = std::forward_iterator_tag;
    /// Value type of the iterator.
    using value_type = SDNode *;
    /// Difference type of the iterator.
    using difference_type = std::ptrdiff_t;
    /// Pointer type of the iterator.
    using pointer = value_type *;
    /// Reference type of the iterator.
    using reference = value_type &;

    /// Construct a singular (end) iterator.
    user_iterator() = default;

    /// Return true if this iterator equals \p x.
    /// @param x Other iterator.
    /// @return True when both refer to the same use.
    bool operator==(const user_iterator &x) const { return UI == x.UI; }
    /// Return true if this iterator differs from \p x.
    /// @param x Other iterator.
    /// @return True when the iterators differ.
    bool operator!=(const user_iterator &x) const { return !operator==(x); }

    /// Advance to the next user (preincrement).
    /// @return Reference to this iterator.
    user_iterator &operator++() { // Preincrement
      ++UI;
      return *this;
    }

    /// Advance to the next user (postincrement).
    /// @param Unused Unused postfix-discriminator parameter.
    /// @return Copy of the prior iterator.
    user_iterator operator++(int Unused) { // Postincrement
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    // Retrieve a pointer to the current User.
    /// Return the current user SDNode.
    /// @return Pointer to the current user.
    SDNode *operator*() const { return UI->getUser(); }

    /// Return the current user SDNode.
    /// @return Pointer to the current user.
    SDNode *operator->() const { return operator*(); }

    /// Return the underlying SDUse.
    /// @return Reference to the current SDUse.
    SDUse &getUse() const { return *UI; }
  };

  /// Provide iteration support to walk over all uses of an SDNode.
  /// @return An iterator to the beginning of this node's use list.
  use_iterator use_begin() const {
    return use_iterator(UseList);
  }

  /// Return the end iterator for the use list.
  /// @return End use iterator.
  static use_iterator use_end() { return use_iterator(nullptr); }

  /// Return a range over all uses of this node.
  /// @return Iterator range of SDUse operands.
  inline iterator_range<use_iterator> uses() {
    return make_range(use_begin(), use_end());
  }
  /// Return a const range over all uses of this node.
  /// @return Const iterator range of SDUse operands.
  inline iterator_range<use_iterator> uses() const {
    return make_range(use_begin(), use_end());
  }

  /// Provide iteration support to walk over all users of an SDNode.
  /// @return An iterator to the beginning of this node's user list.
  user_iterator user_begin() const { return user_iterator(UseList); }

  /// Return the end iterator for the user list.
  /// @return End user iterator.
  static user_iterator user_end() { return user_iterator(nullptr); }

  /// Return a range over all users of this node.
  /// @return Iterator range of user SDNodes.
  inline iterator_range<user_iterator> users() {
    return make_range(user_begin(), user_end());
  }
  /// Return a const range over all users of this node.
  /// @return Const iterator range of user SDNodes.
  inline iterator_range<user_iterator> users() const {
    return make_range(user_begin(), user_end());
  }

  /// Return true if there are exactly NUSES uses of the indicated value.
  /// This method ignores uses of other values defined by this operation.
  /// @param NUses Exact number of uses required.
  /// @param Value Result number whose uses are counted.
  /// @return True if there are exactly NUSES uses of the indicated value.
  bool hasNUsesOfValue(unsigned NUses, unsigned Value) const {
    assert(Value < getNumValues() && "Bad value!");

    // TODO: Only iterate over uses of a given value of the node
    for (SDUse &U : uses()) {
      if (U.getResNo() == Value) {
        if (NUses == 0)
          return false;
        --NUses;
      }
    }

    // Found exactly the right number of uses?
    return NUses == 0;
  }

  /// Return true if there are any use of the indicated value.
  /// This method ignores uses of other values defined by this operation.
  /// @param Value Result number whose uses are queried.
  /// @return True if there are any use of the indicated value.
  LLVM_ABI bool hasAnyUseOfValue(unsigned Value) const;

  /// Return true if this node is the only use of N.
  /// @param N Node whose uses are inspected.
  /// @return True if this node is the only use of N.
  LLVM_ABI bool isOnlyUserOf(const SDNode *N) const;

  /// Return true if this node is an operand of N.
  /// @param N Potential user node.
  /// @return True if this node is an operand of N.
  LLVM_ABI bool isOperandOf(const SDNode *N) const;

  /// Return true if this node is a predecessor of N.
  /// NOTE: Implemented on top of hasPredecessor and every bit as
  /// expensive. Use carefully.
  /// @param N Potential successor node.
  /// @return True if this node is a predecessor of N.
  bool isPredecessorOf(const SDNode *N) const {
    return N->hasPredecessor(this);
  }

  /// Return true if N is a predecessor of this node.
  ///
  /// N is either an operand of this node, or can be reached by recursively
  /// traversing up the operands.
  /// NOTE: This is an expensive method. Use it carefully.
  /// @param N Potential predecessor node.
  /// @return True if N is a predecessor of this node.
  LLVM_ABI bool hasPredecessor(const SDNode *N) const;

  /// Return true if \p N is a predecessor of any node in \p Worklist.
  ///
  /// This helper keeps Visited and Worklist sets externally to allow unions
  /// searches to be performed in parallel, caching of results across
  /// queries and incremental addition to Worklist. Stops early if N is
  /// found but will resume. Remember to clear Visited and Worklists
  /// if DAG changes. MaxSteps gives a maximum number of nodes to visit before
  /// giving up. The TopologicalPrune flag signals that positive NodeIds are
  /// topologically ordered (Operands have strictly smaller node id) and search
  /// can be pruned leveraging this.
  /// @param N Node sought as a predecessor.
  /// @param Visited Set of already-visited nodes (updated).
  /// @param Worklist Nodes whose predecessors are searched (updated).
  /// @param MaxSteps Max nodes to visit, or 0 for unlimited.
  /// @param TopologicalPrune Enable pruning via positive NodeIds.
  /// @return True if N is a predecessor of any node in Worklist.
  static bool hasPredecessorHelper(const SDNode *N,
                                   SmallPtrSetImpl<const SDNode *> &Visited,
                                   SmallVectorImpl<const SDNode *> &Worklist,
                                   unsigned int MaxSteps = 0,
                                   bool TopologicalPrune = false) {
    if (Visited.count(N))
      return true;

    SmallVector<const SDNode *, 8> DeferredNodes;
    // Node Id's are assigned in three places: As a topological
    // ordering (> 0), during legalization (results in values set to
    // 0), new nodes (set to -1). If N has a topolgical id then we
    // know that all nodes with ids smaller than it cannot be
    // successors and we need not check them. Filter out all node
    // that can't be matches. We add them to the worklist before exit
    // in case of multiple calls. Note that during selection the topological id
    // may be violated if a node's predecessor is selected before it. We mark
    // this at selection negating the id of unselected successors and
    // restricting topological pruning to positive ids.

    int NId = N->getNodeId();
    // If we Invalidated the Id, reconstruct original NId.
    if (NId < -1)
      NId = -(NId + 1);

    bool Found = false;
    while (!Worklist.empty()) {
      const SDNode *M = Worklist.pop_back_val();
      int MId = M->getNodeId();
      if (TopologicalPrune && M->getOpcode() != ISD::TokenFactor && (NId > 0) &&
          (MId > 0) && (MId < NId)) {
        DeferredNodes.push_back(M);
        continue;
      }
      for (const SDValue &OpV : M->op_values()) {
        SDNode *Op = OpV.getNode();
        if (Visited.insert(Op).second)
          Worklist.push_back(Op);
        if (Op == N)
          Found = true;
      }
      if (Found)
        break;
      if (MaxSteps != 0 && Visited.size() >= MaxSteps)
        break;
    }
    // Push deferred nodes back on worklist.
    Worklist.append(DeferredNodes.begin(), DeferredNodes.end());
    // If we bailed early, conservatively return found.
    if (MaxSteps != 0 && Visited.size() >= MaxSteps)
      return true;
    return Found;
  }

  /// Return true if all the users of N are contained in Nodes.
  /// NOTE: Requires at least one match, but doesn't require them all.
  /// @param Nodes Candidate user nodes.
  /// @param N Node whose users are checked.
  /// @return True if all the users of N are contained in Nodes.
  LLVM_ABI static bool areOnlyUsersOf(ArrayRef<const SDNode *> Nodes,
                                      const SDNode *N);

  /// Return the number of values used by this operation.
  /// @return The number of values used by this operation.
  unsigned getNumOperands() const { return NumOperands; }

  /// Return the maximum number of operands that a SDNode can hold.
  /// @return The maximum number of operands that a SDNode can hold.
  static constexpr size_t getMaxNumOperands() {
    return std::numeric_limits<decltype(SDNode::NumOperands)>::max();
  }

  /// Helper method returns the integer value of a ConstantSDNode operand.
  /// @param Num Operand index.
  /// @return The integer value of a ConstantSDNode operand.
  inline uint64_t getConstantOperandVal(unsigned Num) const;

  /// Helper method returns the zero-extended integer value of a ConstantSDNode.
  /// @return The zero-extended integer value of a ConstantSDNode.
  inline uint64_t getAsZExtVal() const;

  /// Helper method returns the APInt of a ConstantSDNode operand.
  /// @param Num Operand index.
  /// @return The APInt of a ConstantSDNode operand.
  inline const APInt &getConstantOperandAPInt(unsigned Num) const;

  /// Helper method returns the APInt value of a ConstantSDNode.
  /// @return The APInt value of a ConstantSDNode.
  inline const APInt &getAsAPIntVal() const;

  /// Bitcast a constant integer or FP node to an APInt when possible.
  /// @return APInt bits when this node is a constant; otherwise nullopt.
  inline std::optional<APInt> bitcastToAPInt() const;

  /// Return operand \p Num of this node.
  /// @param Num Operand index.
  /// @return Reference to the operand SDValue.
  const SDValue &getOperand(unsigned Num) const {
    assert(Num < NumOperands && "Invalid child # of SDNode!");
    return OperandList[Num];
  }

  /// Iterator type over this node's SDUse operands.
  using op_iterator = SDUse *;

  /// Return an iterator to the first operand.
  /// @return Begin operand iterator.
  op_iterator op_begin() const { return OperandList; }
  /// Return an iterator past the last operand.
  /// @return End operand iterator.
  op_iterator op_end() const { return OperandList+NumOperands; }
  /// Return this node's operands as an ArrayRef of SDUse.
  /// @return Operand SDUse array.
  ArrayRef<SDUse> ops() const { return ArrayRef(op_begin(), op_end()); }

  /// Iterator for directly iterating over the operand SDValue's.
  struct value_op_iterator
      : iterator_adaptor_base<value_op_iterator, op_iterator,
                              std::random_access_iterator_tag, SDValue,
                              ptrdiff_t, value_op_iterator *,
                              value_op_iterator *> {
    /// Construct an operand-value iterator at \p U.
    /// @param U Operand SDUse to start at, or null.
    explicit value_op_iterator(SDUse *U = nullptr)
      : iterator_adaptor_base(U) {}

    /// Return the current operand as an SDValue.
    /// @return Current operand SDValue.
    const SDValue &operator*() const { return I->get(); }
  };

  /// Return a range over operand SDValues.
  /// @return Range of operand values.
  iterator_range<value_op_iterator> op_values() const {
    return make_range(value_op_iterator(op_begin()),
                      value_op_iterator(op_end()));
  }

  /// Return the SDVTList describing this node's result types.
  /// @return Interned value-type list for this node.
  SDVTList getVTList() const {
    SDVTList X = { ValueList, NumValues };
    return X;
  }

  /// If this node has a glue operand, return the node
  /// to which the glue operand points. Otherwise return NULL.
  /// @return The glued predecessor node, or nullptr if none.
  SDNode *getGluedNode() const {
    if (getNumOperands() != 0 &&
        getOperand(getNumOperands()-1).getValueType() == MVT::Glue)
      return getOperand(getNumOperands()-1).getNode();
    return nullptr;
  }

  /// If this node has a glue value with a user, return
  /// the user (there is at most one). Otherwise return NULL.
  /// @return The glue user node, or nullptr if none.
  SDNode *getGluedUser() const {
    for (SDUse &U : uses())
      if (U.getValueType() == MVT::Glue)
        return U.getUser();
    return nullptr;
  }

  /// Return the optimization flags attached to this node.
  /// @return Current SDNodeFlags.
  SDNodeFlags getFlags() const { return Flags; }
  /// Replace this node's optimization flags.
  /// @param NewFlags Flags to install.
  void setFlags(SDNodeFlags NewFlags) { Flags = NewFlags; }
  /// Clear flag bits set in \p Mask.
  /// @param Mask Bits to clear from Flags.
  void dropFlags(unsigned Mask) { Flags &= ~Mask; }

  /// Clear any flags in this node that aren't also set in Flags.
  /// If Flags is not in a defined state then this has no effect.
  /// @param Flags Flags to intersect with.
  LLVM_ABI void intersectFlagsWith(const SDNodeFlags Flags);

  /// Return true if any poison-generating flag is set.
  /// @return True when poison-generating flags are present.
  bool hasPoisonGeneratingFlags() const {
    return Flags.Flags & SDNodeFlags::PoisonGeneratingFlags;
  }

  /// Set the CFI type identifier for this node.
  /// @param Type CFI type id.
  void setCFIType(uint32_t Type) { CFIType = Type; }
  /// Return the CFI type identifier for this node.
  /// @return CFI type id.
  uint32_t getCFIType() const { return CFIType; }

  /// Return the number of values defined/returned by this operator.
  /// @return The number of values defined/returned by this operator.
  unsigned getNumValues() const { return NumValues; }

  /// Return the type of a specified result.
  /// @param ResNo Result number.
  /// @return The type of a specified result.
  EVT getValueType(unsigned ResNo) const {
    assert(ResNo < NumValues && "Illegal result number!");
    return ValueList[ResNo];
  }

  /// Return the type of a specified result as a simple type.
  /// @param ResNo Result number.
  /// @return The type of a specified result as a simple type.
  MVT getSimpleValueType(unsigned ResNo) const {
    return getValueType(ResNo).getSimpleVT();
  }

  /// Return the size in bits of result \p ResNo.
  ///
  /// If the value type is a scalable vector type, the scalable property will
  /// be set and the runtime size will be a positive integer multiple of the
  /// base size.
  /// @param ResNo Result number.
  /// @return The size of result \p ResNo in bits.
  TypeSize getValueSizeInBits(unsigned ResNo) const {
    return getValueType(ResNo).getSizeInBits();
  }

  /// Iterator type over this node's result value types.
  using value_iterator = const EVT *;

  /// Return an iterator to the first result value type.
  /// @return Begin value-type iterator.
  value_iterator value_begin() const { return ValueList; }
  /// Return an iterator past the last result value type.
  /// @return End value-type iterator.
  value_iterator value_end() const { return ValueList+NumValues; }
  /// Return a range over this node's result value types.
  /// @return Range of result EVTs.
  iterator_range<value_iterator> values() const {
    return llvm::make_range(value_begin(), value_end());
  }

  /// Return the opcode of this operation for printing.
  /// @param G Optional SelectionDAG for target formatting.
  /// @return The opcode of this operation for printing.
  LLVM_ABI std::string getOperationName(const SelectionDAG *G = nullptr) const;
  /// Return a printable name for indexed mode \p AM.
  /// @param AM Memory indexed mode.
  /// @return Printable mode name.
  LLVM_ABI static const char *getIndexedModeName(ISD::MemIndexedMode AM);
  /// Print this node's result types to \p OS.
  /// @param OS Output stream.
  /// @param G Optional SelectionDAG for target formatting.
  LLVM_ABI void print_types(raw_ostream &OS, const SelectionDAG *G) const;
  /// Print this node's details to \p OS.
  /// @param OS Output stream.
  /// @param G Optional SelectionDAG for target formatting.
  LLVM_ABI void print_details(raw_ostream &OS, const SelectionDAG *G) const;
  /// Print this node to \p OS.
  /// @param OS Output stream.
  /// @param G Optional SelectionDAG for target formatting.
  LLVM_ABI void print(raw_ostream &OS, const SelectionDAG *G = nullptr) const;
  /// Print this node recursively to \p OS.
  /// @param OS Output stream.
  /// @param G Optional SelectionDAG for target formatting.
  LLVM_ABI void printr(raw_ostream &OS, const SelectionDAG *G = nullptr) const;

  /// Print this node and all children down to the leaves.
  ///
  /// The given SelectionDAG allows target-specific nodes
  /// to be printed in human-readable form.  Unlike printr, this will
  /// print the whole DAG, including children that appear multiple
  /// times.
  /// @param O Output stream.
  /// @param G Optional SelectionDAG for target formatting.
  LLVM_ABI void printrFull(raw_ostream &O,
                           const SelectionDAG *G = nullptr) const;

  /// Print this node and children up to depth \p depth.
  ///
  /// The given SelectionDAG allows target-specific
  /// nodes to be printed in human-readable form.  Unlike printr, this
  /// will print children that appear multiple times wherever they are
  /// used.
  /// @param O Output stream.
  /// @param G Optional SelectionDAG for target formatting.
  /// @param depth Maximum recursion depth.
  LLVM_ABI void printrWithDepth(raw_ostream &O, const SelectionDAG *G = nullptr,
                                unsigned depth = 100) const;

  /// Dump this node, for debugging.
  LLVM_ABI void dump() const;

  /// Dump (recursively) this node and its use-def subgraph.
  LLVM_ABI void dumpr() const;

  /// Dump this node using SelectionDAG \p G for target formatting.
  /// @param G SelectionDAG providing target formatting.
  LLVM_ABI void dump(const SelectionDAG *G) const;

  /// Dump this node recursively using SelectionDAG \p G.
  /// @param G SelectionDAG providing target formatting.
  LLVM_ABI void dumpr(const SelectionDAG *G) const;

  /// Dump the full recursive print of this node to dbgs().
  ///
  /// The given SelectionDAG allows target-specific nodes to be printed
  /// in human-readable form. Unlike dumpr, this will print the whole DAG,
  /// including children that appear multiple times.
  /// @param G Optional SelectionDAG for target formatting.
  LLVM_ABI void dumprFull(const SelectionDAG *G = nullptr) const;

  /// Dump a depth-limited recursive print of this node to dbgs().
  ///
  /// The given SelectionDAG allows target-specific nodes to be printed in
  /// human-readable form. Unlike dumpr, this will print children that appear
  /// multiple times wherever they are used.
  /// @param G Optional SelectionDAG for target formatting.
  /// @param depth Maximum recursion depth.
  LLVM_ABI void dumprWithDepth(const SelectionDAG *G = nullptr,
                               unsigned depth = 100) const;

  /// Gather unique data for the node into folding-set id \p ID.
  /// @param ID Folding set node id to populate.
  LLVM_ABI void Profile(FoldingSetNodeID &ID) const;

  /// Add use \p U to this node's use list (SDUse only).
  /// @param U Use to register.
  void addUse(SDUse &U) { U.addToList(&UseList); }

protected:
  /// Return an SDVTList for single simple type \p VT.
  /// @param VT Simple value type.
  /// @return Interned one-element SDVTList.
  static SDVTList getSDVTList(MVT VT) {
    SDVTList Ret = { getValueTypeList(VT), 1 };
    return Ret;
  }

  /// Construct an SDNode with opcode, order, location, and types.
  ///
  /// SDNodes are created without any operands, and never own the operand
  /// storage. To add operands, see SelectionDAG::createOperands.
  /// @param Opc SelectionDAG opcode.
  /// @param Order IR order for scheduling.
  /// @param dl Debug location.
  /// @param VTs Result value-type list.
  SDNode(unsigned Opc, unsigned Order, DebugLoc dl, SDVTList VTs)
      : NodeType(Opc), ValueList(VTs.VTs), NumValues(VTs.NumVTs),
        IROrder(Order), debugLoc(std::move(dl)) {
    memset(&RawSDNodeBits, 0, sizeof(RawSDNodeBits));
    assert(NumValues == VTs.NumVTs &&
           "NumValues wasn't wide enough for its operands!");
  }

  /// Release the operands and set this node to have zero operands.
  LLVM_ABI void DropOperands();
};

/// IR ordering and DebugLoc wrapper passed into SDNode creation.
///
/// When an SDNode is created from the DAGBuilder, the DebugLoc is extracted
/// from the original Instruction, and IROrder is the ordinal position of
/// the instruction.
/// When an SDNode is created after the DAG is being built, both DebugLoc and
/// the IROrder are propagated from the original SDNode.
/// So SDLoc class provides two constructors besides the default one, one to
/// be used by the DAGBuilder, the other to be used by others.
class SDLoc {
private:
  DebugLoc DL;
  int IROrder = 0;

public:
  /// Construct an empty SDLoc.
  SDLoc() = default;
  /// Construct an SDLoc from existing node \p N.
  /// @param N Node providing DebugLoc and IROrder.
  SDLoc(const SDNode *N) : DL(N->getDebugLoc()), IROrder(N->getIROrder()) {}
  /// Construct an SDLoc from SDValue \p V.
  /// @param V Value whose node provides location info.
  SDLoc(const SDValue V) : SDLoc(V.getNode()) {}
  /// Construct an SDLoc from instruction \p I and IR order \p Order.
  /// @param I Instruction providing DebugLoc, or null.
  /// @param Order Non-negative IR ordering.
  SDLoc(const Instruction *I, int Order) : IROrder(Order) {
    assert(Order >= 0 && "bad IROrder");
    if (I)
      DL = I->getDebugLoc();
  }

  /// Return the IR ordering.
  /// @return IR order value.
  unsigned getIROrder() const { return IROrder; }
  /// Return the debug location.
  /// @return DebugLoc value.
  const DebugLoc &getDebugLoc() const { return DL; }
};

// Define inline functions from the SDValue class.

inline SDValue::SDValue(SDNode *node, unsigned resno)
    : Node(node), ResNo(resno) {
  // Explicitly check for !ResNo to avoid use-after-free, because there are
  // callers that use SDValue(N, 0) with a deleted N to indicate successful
  // combines.
  assert((!Node || !ResNo || ResNo < Node->getNumValues()) &&
         "Invalid result number for the given node!");
  assert(ResNo < -2U && "Cannot use result numbers reserved for DenseMaps.");
}

inline unsigned SDValue::getOpcode() const {
  return Node->getOpcode();
}

inline EVT SDValue::getValueType() const {
  return Node->getValueType(ResNo);
}

inline unsigned SDValue::getNumOperands() const {
  return Node->getNumOperands();
}

inline const SDValue &SDValue::getOperand(unsigned i) const {
  return Node->getOperand(i);
}

inline uint64_t SDValue::getConstantOperandVal(unsigned i) const {
  return Node->getConstantOperandVal(i);
}

inline const APInt &SDValue::getConstantOperandAPInt(unsigned i) const {
  return Node->getConstantOperandAPInt(i);
}

inline bool SDValue::isTargetOpcode() const {
  return Node->isTargetOpcode();
}

inline bool SDValue::isMachineOpcode() const {
  return Node->isMachineOpcode();
}

inline unsigned SDValue::getMachineOpcode() const {
  return Node->getMachineOpcode();
}

inline bool SDValue::isUndef() const {
  return Node->isUndef();
}

inline bool SDValue::isAnyAdd() const { return Node->isAnyAdd(); }

inline bool SDValue::use_empty() const {
  return !Node->hasAnyUseOfValue(ResNo);
}

inline bool SDValue::hasOneUse() const {
  return Node->hasNUsesOfValue(1, ResNo);
}

inline bool SDValue::hasOneUser() const {
  auto Uses = make_filter_range(Node->uses(),
                                [this](SDUse &U) { return U.get() == *this; });
  auto Users = map_range(Uses, [](SDUse &U) { return U.getUser(); });
  return all_equal(Users);
}

inline const DebugLoc &SDValue::getDebugLoc() const {
  return Node->getDebugLoc();
}

inline void SDValue::dump() const {
  return Node->dump();
}

inline void SDValue::dump(const SelectionDAG *G) const {
  return Node->dump(G);
}

inline void SDValue::dumpr() const {
  return Node->dumpr();
}

inline void SDValue::dumpr(const SelectionDAG *G) const {
  return Node->dumpr(G);
}

// Define inline functions from the SDUse class.
inline unsigned SDUse::getOperandNo() const {
  return this - getUser()->op_begin();
}

inline void SDUse::set(const SDValue &V) {
  if (Val.getNode()) removeFromList();
  Val = V;
  if (V.getNode())
    V->addUse(*this);
}

inline void SDUse::setInitial(const SDValue &V) {
  Val = V;
  V->addUse(*this);
}

inline void SDUse::setNode(SDNode *N) {
  if (Val.getNode()) removeFromList();
  Val.setNode(N);
  if (N) N->addUse(*this);
}

/// Persistent handle around another node updated across RAUW.
///
/// This node should be directly created by end-users and not added to
/// the AllNodes list.
class HandleSDNode : public SDNode {
  /// Operand storing the handled value.
  SDUse Op;

public:
  /// Construct a handle around value \p X.
  /// @param X Value to wrap.
  explicit HandleSDNode(SDValue X)
    : SDNode(ISD::HANDLENODE, 0, DebugLoc(), getSDVTList(MVT::Other)) {
    // HandleSDNodes are never inserted into the DAG, so they won't be
    // auto-numbered. Use ID 65535 as a sentinel.
    PersistentId = 0xffff;

    // Manually set up the operand list. This node type is special in that it's
    // always stack allocated and SelectionDAG does not manage its operands.
    // TODO: This should either (a) not be in the SDNode hierarchy, or (b) not
    // be so special.
    Op.setUser(this);
    Op.setInitial(X);
    NumOperands = 1;
    OperandList = &Op;
  }
  /// Destroy the handle node.
  LLVM_ABI ~HandleSDNode();

  /// Return the handled value.
  /// @return The wrapped SDValue.
  const SDValue &getValue() const { return Op; }
};

/// SDNode representing an address-space cast.
class AddrSpaceCastSDNode : public SDNode {
private:
  /// Source address space.
  unsigned SrcAddrSpace;
  /// Destination address space.
  unsigned DestAddrSpace;

public:
  /// Construct an addrspacecast node.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param SrcAS Source address space.
  /// @param DestAS Destination address space.
  AddrSpaceCastSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs,
                      unsigned SrcAS, unsigned DestAS)
      : SDNode(ISD::ADDRSPACECAST, Order, dl, VTs), SrcAddrSpace(SrcAS),
        DestAddrSpace(DestAS) {}

  /// Return the source address space.
  /// @return Source address space id.
  unsigned getSrcAddressSpace() const { return SrcAddrSpace; }
  /// Return the destination address space.
  /// @return Destination address space id.
  unsigned getDestAddressSpace() const { return DestAddrSpace; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::ADDRSPACECAST;
  }
};

/// This is an abstract virtual class for memory operations.
class MemSDNode : public SDNode {
private:
  // VT of in-memory value.
  EVT MemoryVT;

protected:
  /// Memory reference information (always at least one MMO).
  ///
  /// - MachineMemOperand*: exactly 1 MMO (common case)
  /// - MachineMemOperand**: pointer to array, size at offset -1
  PointerUnion<MachineMemOperand *, MachineMemOperand **> MemRefs;

public:
  /// Construct a memory SDNode with one or more MMOs.
  ///
  /// For a single MMO, pass the MMO pointer directly. For multiple MMOs,
  /// pre-allocate storage with count at offset -1 and pass pointer to array.
  /// @param Opc Opcode.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param memvt In-memory value type.
  /// @param memrefs One MMO or an array of MMOs.
  LLVM_ABI
  MemSDNode(unsigned Opc, unsigned Order, const DebugLoc &dl, SDVTList VTs,
            EVT memvt,
            PointerUnion<MachineMemOperand *, MachineMemOperand **> memrefs);

  /// Return true if this memory operation reads memory.
  /// @return True for loads.
  bool readMem() const { return getMemOperand()->isLoad(); }
  /// Return true if this memory operation writes memory.
  /// @return True for stores.
  bool writeMem() const { return getMemOperand()->isStore(); }

  /// Return the base alignment of the memory access.
  /// @return The base alignment of the memory access.
  Align getBaseAlign() const { return getMemOperand()->getBaseAlign(); }
  /// Return the alignment of the memory access.
  /// @return Alignment of the memory access.
  Align getAlign() const { return getMemOperand()->getAlign(); }

  /// Return SubclassData bits with HasDebugValue cleared.
  ///
  /// This contains an encoding of the volatile flag, as well as bits used by
  /// subclasses. This function should only be used to compute a FoldingSetNodeID
  /// value. The HasDebugValue bit is masked out because CSE map needs to match
  /// nodes with debug info with nodes without debug info. Same is about
  /// isDivergent bit.
  /// @return The SubclassData value, without HasDebugValue.
  unsigned getRawSubclassData() const {
    uint16_t Data;
    union {
      char RawSDNodeBits[sizeof(uint16_t)];
      SDNodeBitfields SDNodeBits;
    };
    memcpy(&RawSDNodeBits, &this->RawSDNodeBits, sizeof(this->RawSDNodeBits));
    SDNodeBits.HasDebugValue = 0;
    SDNodeBits.IsDivergent = false;
    memcpy(&Data, &RawSDNodeBits, sizeof(RawSDNodeBits));
    return Data;
  }

  /// Return true if the memory access is volatile.
  /// @return True when volatile.
  bool isVolatile() const { return MemSDNodeBits.IsVolatile; }
  /// Return true if the memory access is non-temporal.
  /// @return True when non-temporal.
  bool isNonTemporal() const { return MemSDNodeBits.IsNonTemporal; }
  /// Return true if the memory access is dereferenceable.
  /// @return True when dereferenceable.
  bool isDereferenceable() const { return MemSDNodeBits.IsDereferenceable; }
  /// Return true if the memory access is invariant.
  /// @return True when invariant.
  bool isInvariant() const { return MemSDNodeBits.IsInvariant; }

  /// Return the offset from the location of the access.
  /// @return Source value offset.
  int64_t getSrcValueOffset() const { return getMemOperand()->getOffset(); }

  /// Returns the AA info that describes the dereference.
  /// @return The AA info that describes the dereference.
  AAMDNodes getAAInfo() const { return getMemOperand()->getAAInfo(); }

  /// Returns the Ranges that describes the dereference.
  /// @return The Ranges that describes the dereference.
  const MDNode *getRanges() const { return getMemOperand()->getRanges(); }

  /// Returns the cache hint metadata for this memory access.
  /// @return The cache hint metadata for this memory access.
  const MDNode *getMemCacheHint() const {
    return getMemOperand()->getMemCacheHint();
  }

  /// Returns the synchronization scope ID for this memory operation.
  /// @return The synchronization scope ID for this memory operation.
  SyncScope::ID getSyncScopeID() const {
    return getMemOperand()->getSyncScopeID();
  }

  /// Return the atomic ordering requirements for this memory operation. For
  /// cmpxchg atomic operations, return the atomic ordering requirements when
  /// store occurs.
  /// @return The atomic ordering requirements for this memory operation.
  AtomicOrdering getSuccessOrdering() const {
    return getMemOperand()->getSuccessOrdering();
  }

  /// Return a single atomic ordering at least as strong as success and failure.
  ///
  /// For operations other than cmpxchg, this is equivalent to
  /// getSuccessOrdering().
  /// @return Merged atomic ordering for this memory operation.
  AtomicOrdering getMergedOrdering() const {
    return getMemOperand()->getMergedOrdering();
  }

  /// Return true if the memory operation ordering is Unordered or higher.
  /// @return True if the memory operation ordering is Unordered or higher.
  bool isAtomic() const { return getMemOperand()->isAtomic(); }

  /// Returns true if the memory operation doesn't imply any ordering
  /// constraints on surrounding memory operations beyond the normal memory
  /// aliasing rules.
  /// @return True if the memory operation doesn't imply any ordering constraints on surrounding memory operations beyond the normal memory aliasing rules.
  bool isUnordered() const { return getMemOperand()->isUnordered(); }

  /// Returns true if the memory operation is neither atomic or volatile.
  /// @return True if the memory operation is neither atomic or volatile.
  bool isSimple() const { return !isAtomic() && !isVolatile(); }

  /// Return the type of the in-memory value.
  /// @return The type of the in-memory value.
  EVT getMemoryVT() const { return MemoryVT; }

  /// Return the unique MachineMemOperand for this memory operation.
  ///
  /// Asserts if multiple MMOs are present - use memoperands() instead.
  /// @return The unique MachineMemOperand describing the memory reference.
  MachineMemOperand *getMemOperand() const {
    assert(!isa<MachineMemOperand **>(MemRefs) &&
           "Use memoperands() for nodes with multiple memory operands");
    return cast<MachineMemOperand *>(MemRefs);
  }

  /// Return the number of memory operands.
  /// @return The number of memory operands.
  size_t getNumMemOperands() const {
    if (isa<MachineMemOperand *>(MemRefs))
      return 1;
    MachineMemOperand **Array = cast<MachineMemOperand **>(MemRefs);
    return reinterpret_cast<size_t *>(Array)[-1];
  }

  /// Return true if this node has exactly one memory operand.
  /// @return True if this node has exactly one memory operand.
  bool hasUniqueMemOperand() const { return isa<MachineMemOperand *>(MemRefs); }

  /// Return the memory operands for this node.
  /// @return The memory operands for this node.
  ArrayRef<MachineMemOperand *> memoperands() const {
    if (isa<MachineMemOperand *>(MemRefs))
      return ArrayRef(MemRefs.getAddrOfPtr1(), 1);
    MachineMemOperand **Array = cast<MachineMemOperand **>(MemRefs);
    size_t Count = reinterpret_cast<size_t *>(Array)[-1];
    return ArrayRef(Array, Count);
  }

  /// Return pointer info for the memory operand.
  /// @return MachinePointerInfo for the access.
  const MachinePointerInfo &getPointerInfo() const {
    return getMemOperand()->getPointerInfo();
  }

  /// Return the address space for the associated pointer
  /// @return The address space for the associated pointer.
  unsigned getAddressSpace() const {
    return getPointerInfo().getAddrSpace();
  }

  /// Update MMOs to reflect greater alignment from \p NewMMOs.
  ///
  /// This must only be used when the new alignment applies to all users of
  /// these MachineMemOperands. The NewMMOs array must parallel memoperands().
  /// @param NewMMOs Parallel array of MMOs with candidate alignments.
  void refineAlignment(ArrayRef<MachineMemOperand *> NewMMOs) {
    ArrayRef<MachineMemOperand *> MMOs = memoperands();
    assert(NewMMOs.size() == MMOs.size() && "MMO count mismatch");
    for (auto [MMO, NewMMO] : zip(MMOs, NewMMOs))
      MMO->refineAlignment(NewMMO);
  }

  /// Refine alignment from a single MMO \p NewMMO.
  /// @param NewMMO Candidate MMO providing alignment.
  void refineAlignment(MachineMemOperand *NewMMO) {
    refineAlignment(ArrayRef(NewMMO));
  }

  /// Refine LLVM IR metadata for all MMOs from \p NewMMOs.
  ///
  /// The NewMMOs array must parallel memoperands(). For each pair, if metadata
  /// differs, the stored metadata is cleared conservatively.
  /// @param NewMMOs Parallel array of MMOs providing metadata.
  void refineMMOMetadata(ArrayRef<MachineMemOperand *> NewMMOs) {
    ArrayRef<MachineMemOperand *> MMOs = memoperands();
    assert(NewMMOs.size() == MMOs.size() && "MMO count mismatch");
    for (auto [MMO, NewMMO] : zip(MMOs, NewMMOs)) {
      // FIXME: Union the ranges instead?
      if (MMO->getRanges() && MMO->getRanges() != NewMMO->getRanges())
        MMO->clearRanges();
      if (MMO->getMemCacheHint() &&
          MMO->getMemCacheHint() != NewMMO->getMemCacheHint())
        MMO->clearMemCacheHint();
    }
  }

  /// Refine MMO metadata from a single MMO \p NewMMO.
  /// @param NewMMO Candidate MMO providing metadata.
  void refineMMOMetadata(MachineMemOperand *NewMMO) {
    refineMMOMetadata(ArrayRef(NewMMO));
  }

  /// Return the chain operand.
  /// @return Chain SDValue.
  const SDValue &getChain() const { return getOperand(0); }

  /// Return the base pointer operand.
  /// @return Base pointer SDValue.
  const SDValue &getBasePtr() const {
    switch (getOpcode()) {
    case ISD::STORE:
    case ISD::ATOMIC_STORE:
    case ISD::VP_STORE:
    case ISD::MSTORE:
    case ISD::VP_SCATTER:
    case ISD::EXPERIMENTAL_VP_STRIDED_STORE:
      return getOperand(2);
    case ISD::MGATHER:
    case ISD::MSCATTER:
    case ISD::EXPERIMENTAL_VECTOR_HISTOGRAM:
      return getOperand(3);
    default:
      return getOperand(1);
    }
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    // For some targets, we lower some target intrinsics to a MemIntrinsicNode
    // with either an intrinsic or a target opcode.
    switch (N->getOpcode()) {
    case ISD::LOAD:
    case ISD::STORE:
    case ISD::ATOMIC_CMP_SWAP:
    case ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS:
    case ISD::ATOMIC_SWAP:
    case ISD::ATOMIC_LOAD_ADD:
    case ISD::ATOMIC_LOAD_SUB:
    case ISD::ATOMIC_LOAD_AND:
    case ISD::ATOMIC_LOAD_CLR:
    case ISD::ATOMIC_LOAD_OR:
    case ISD::ATOMIC_LOAD_XOR:
    case ISD::ATOMIC_LOAD_NAND:
    case ISD::ATOMIC_LOAD_MIN:
    case ISD::ATOMIC_LOAD_MAX:
    case ISD::ATOMIC_LOAD_UMIN:
    case ISD::ATOMIC_LOAD_UMAX:
    case ISD::ATOMIC_LOAD_FADD:
    case ISD::ATOMIC_LOAD_FSUB:
    case ISD::ATOMIC_LOAD_FMAX:
    case ISD::ATOMIC_LOAD_FMIN:
    case ISD::ATOMIC_LOAD_FMAXIMUM:
    case ISD::ATOMIC_LOAD_FMINIMUM:
    case ISD::ATOMIC_LOAD_UINC_WRAP:
    case ISD::ATOMIC_LOAD_UDEC_WRAP:
    case ISD::ATOMIC_LOAD_USUB_COND:
    case ISD::ATOMIC_LOAD_USUB_SAT:
    case ISD::ATOMIC_LOAD:
    case ISD::ATOMIC_STORE:
    case ISD::MLOAD:
    case ISD::MSTORE:
    case ISD::MGATHER:
    case ISD::MSCATTER:
    case ISD::VP_LOAD:
    case ISD::VP_STORE:
    case ISD::VP_GATHER:
    case ISD::VP_SCATTER:
    case ISD::EXPERIMENTAL_VP_STRIDED_LOAD:
    case ISD::EXPERIMENTAL_VP_STRIDED_STORE:
    case ISD::GET_FPENV_MEM:
    case ISD::SET_FPENV_MEM:
    case ISD::EXPERIMENTAL_VECTOR_HISTOGRAM:
      return true;
    default:
      return N->isMemIntrinsic();
    }
  }
};

/// This is an SDNode representing atomic operations.
class AtomicSDNode : public MemSDNode {
public:
  /// Construct an atomic memory SDNode.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param Opc Atomic opcode.
  /// @param VTL Result types.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  /// @param ETy Load extension type (atomic loads only).
  AtomicSDNode(unsigned Order, const DebugLoc &dl, unsigned Opc, SDVTList VTL,
               EVT MemVT, MachineMemOperand *MMO, ISD::LoadExtType ETy)
      : MemSDNode(Opc, Order, dl, VTL, MemVT, MMO) {
    assert(((Opc != ISD::ATOMIC_LOAD && Opc != ISD::ATOMIC_STORE) ||
            MMO->isAtomic()) && "then why are we using an AtomicSDNode?");
    assert((Opc == ISD::ATOMIC_LOAD || ETy == ISD::NON_EXTLOAD) &&
           "Only atomic load uses ExtTy");
    LoadSDNodeBits.ExtTy = ETy;
  }

  /// Return the load extension type for an atomic load.
  /// @return Load extension type.
  ISD::LoadExtType getExtensionType() const {
    assert(getOpcode() == ISD::ATOMIC_LOAD && "Only used for atomic loads.");
    return static_cast<ISD::LoadExtType>(LoadSDNodeBits.ExtTy);
  }

  /// Return the base pointer operand.
  /// @return Base pointer SDValue.
  const SDValue &getBasePtr() const {
    return getOpcode() == ISD::ATOMIC_STORE ? getOperand(2) : getOperand(1);
  }
  /// Return the value operand for atomic store/rmw-style ops.
  /// @return Value SDValue.
  const SDValue &getVal() const {
    return getOpcode() == ISD::ATOMIC_STORE ? getOperand(1) : getOperand(2);
  }

  /// Return true if this SDNode represents a cmpxchg atomic operation.
  /// @return True if this SDNode represents cmpxchg atomic operation, false otherwise.
  bool isCompareAndSwap() const {
    unsigned Op = getOpcode();
    return Op == ISD::ATOMIC_CMP_SWAP ||
           Op == ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS;
  }

  /// For cmpxchg, return the atomic ordering when the store does not occur.
  /// @return The atomic ordering requirements when the store does not occur.
  AtomicOrdering getFailureOrdering() const {
    assert(isCompareAndSwap() && "Must be cmpxchg operation");
    return getMemOperand()->getFailureOrdering();
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::ATOMIC_CMP_SWAP ||
           N->getOpcode() == ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS ||
           N->getOpcode() == ISD::ATOMIC_SWAP ||
           N->getOpcode() == ISD::ATOMIC_LOAD_ADD ||
           N->getOpcode() == ISD::ATOMIC_LOAD_SUB ||
           N->getOpcode() == ISD::ATOMIC_LOAD_AND ||
           N->getOpcode() == ISD::ATOMIC_LOAD_CLR ||
           N->getOpcode() == ISD::ATOMIC_LOAD_OR ||
           N->getOpcode() == ISD::ATOMIC_LOAD_XOR ||
           N->getOpcode() == ISD::ATOMIC_LOAD_NAND ||
           N->getOpcode() == ISD::ATOMIC_LOAD_MIN ||
           N->getOpcode() == ISD::ATOMIC_LOAD_MAX ||
           N->getOpcode() == ISD::ATOMIC_LOAD_UMIN ||
           N->getOpcode() == ISD::ATOMIC_LOAD_UMAX ||
           N->getOpcode() == ISD::ATOMIC_LOAD_FADD ||
           N->getOpcode() == ISD::ATOMIC_LOAD_FSUB ||
           N->getOpcode() == ISD::ATOMIC_LOAD_FMAX ||
           N->getOpcode() == ISD::ATOMIC_LOAD_FMIN ||
           N->getOpcode() == ISD::ATOMIC_LOAD_FMAXIMUM ||
           N->getOpcode() == ISD::ATOMIC_LOAD_FMINIMUM ||
           N->getOpcode() == ISD::ATOMIC_LOAD_UINC_WRAP ||
           N->getOpcode() == ISD::ATOMIC_LOAD_UDEC_WRAP ||
           N->getOpcode() == ISD::ATOMIC_LOAD_USUB_COND ||
           N->getOpcode() == ISD::ATOMIC_LOAD_USUB_SAT ||
           N->getOpcode() == ISD::ATOMIC_LOAD ||
           N->getOpcode() == ISD::ATOMIC_STORE;
  }
};

/// Memory-touching target intrinsic SDNode with an MMO.
///
/// Its opcode may be INTRINSIC_VOID, INTRINSIC_W_CHAIN, PREFETCH, or a
/// target-specific memory-referencing opcode (see
/// `SelectionDAGTargetInfo::isTargetMemoryOpcode`).
class MemIntrinsicSDNode : public MemSDNode {
public:
  /// Construct a memory intrinsic SDNode.
  /// @param Opc Opcode.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param MemoryVT In-memory value type.
  /// @param MemRefs One or more machine mem operands.
  MemIntrinsicSDNode(
      unsigned Opc, unsigned Order, const DebugLoc &dl, SDVTList VTs,
      EVT MemoryVT,
      PointerUnion<MachineMemOperand *, MachineMemOperand **> MemRefs)
      : MemSDNode(Opc, Order, dl, VTs, MemoryVT, MemRefs) {
    SDNodeBits.IsMemIntrinsic = true;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    // We lower some target intrinsics to their target opcode
    // early a node with a target opcode can be of this class
    return N->isMemIntrinsic();
  }
};

/// SDNode for the shufflevector code-generator representation.
///
/// It combines elements from two input vectors into a new vector, with the
/// selection and ordering of elements determined by an array of integers,
/// referred to as the shuffle mask. For input vectors of width N, mask indices
/// of 0..N-1 refer to elements from the LHS input, and indices from N to 2N-1
/// the RHS. An index of -1 is treated as undef, such that the code generator
/// may put any value in the corresponding element of the result.
class ShuffleVectorSDNode : public SDNode {
  // The memory for Mask is owned by the SelectionDAG's OperandAllocator, and
  // is freed when the SelectionDAG object is destroyed.
  /// Shuffle mask indices owned by SelectionDAG.
  const int *Mask;

protected:
  friend class SelectionDAG;

  /// Construct a VECTOR_SHUFFLE node with mask \p M.
  /// @param VTs Result types.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param M Shuffle mask pointer owned by the DAG.
  ShuffleVectorSDNode(SDVTList VTs, unsigned Order, const DebugLoc &dl,
                      const int *M)
      : SDNode(ISD::VECTOR_SHUFFLE, Order, dl, VTs), Mask(M) {}

public:
  /// Return the shuffle mask.
  /// @return Array of mask indices.
  ArrayRef<int> getMask() const {
    EVT VT = getValueType(0);
    return ArrayRef(Mask, VT.getVectorNumElements());
  }

  /// Return mask element at index \p Idx.
  /// @param Idx Element index.
  /// @return Mask index at \p Idx.
  int getMaskElt(unsigned Idx) const {
    assert(Idx < getValueType(0).getVectorNumElements() && "Idx out of range!");
    return Mask[Idx];
  }

  /// Return true if the shuffle mask is a splat.
  /// @return True for splat masks.
  bool isSplat() const { return isSplatMask(getMask()); }

  /// Return the splat index for a splat mask.
  /// @return Splat source index.
  int getSplatIndex() const { return getSplatMaskIndex(getMask()); }

  /// Return true if \p Mask is a splat mask.
  /// @param Mask Shuffle mask to test.
  /// @return True when \p Mask is a splat.
  LLVM_ABI static bool isSplatMask(ArrayRef<int> Mask);

  /// Return the splat index for splat mask \p Mask.
  /// @param Mask Splat shuffle mask.
  /// @return Splat source index.
  static int getSplatMaskIndex(ArrayRef<int> Mask) {
    assert(isSplatMask(Mask) && "Cannot get splat index for non-splat!");
    for (int Elem : Mask)
      if (Elem >= 0)
        return Elem;

    // We can choose any index value here and be correct because all elements
    // are undefined. Return 0 for better potential for callers to simplify.
    return 0;
  }

  /// Commute shuffle mask \p Mask as if the vector operands were swapped.
  /// @param Mask Shuffle permute mask updated in place.
  static void commuteMask(MutableArrayRef<int> Mask) {
    unsigned NumElems = Mask.size();
    for (unsigned i = 0; i != NumElems; ++i) {
      int idx = Mask[i];
      if (idx < 0)
        continue;
      else if (idx < (int)NumElems)
        Mask[i] = idx + NumElems;
      else
        Mask[i] = idx - NumElems;
    }
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::VECTOR_SHUFFLE;
  }
};

/// SDNode representing an integer constant.
class ConstantSDNode : public SDNode {
  friend class SelectionDAG;

  /// Underlying LLVM ConstantInt.
  const ConstantInt *Value;

  /// Construct a (target) integer constant node.
  /// @param isTarget Whether this is a TargetConstant.
  /// @param isOpaque Whether the constant is opaque.
  /// @param val Underlying ConstantInt.
  /// @param VTs Result types.
  ConstantSDNode(bool isTarget, bool isOpaque, const ConstantInt *val,
                 SDVTList VTs)
      : SDNode(isTarget ? ISD::TargetConstant : ISD::Constant, 0, DebugLoc(),
               VTs),
        Value(val) {
    assert(!isa<VectorType>(val->getType()) && "Unexpected vector type!");
    ConstantSDNodeBits.IsOpaque = isOpaque;
  }

public:
  /// Return the underlying ConstantInt.
  /// @return ConstantInt pointer.
  const ConstantInt *getConstantIntValue() const { return Value; }
  /// Return the APInt value.
  /// @return Integer APInt.
  const APInt &getAPIntValue() const { return Value->getValue(); }
  /// Return the zero-extended value.
  /// @return Zero-extended integer.
  uint64_t getZExtValue() const { return Value->getZExtValue(); }
  /// Return the sign-extended value.
  /// @return Sign-extended integer.
  int64_t getSExtValue() const { return Value->getSExtValue(); }
  /// Return the value limited to \p Limit.
  /// @param Limit Maximum value.
  /// @return Limited integer value.
  uint64_t getLimitedValue(uint64_t Limit = UINT64_MAX) {
    return Value->getLimitedValue(Limit);
  }
  /// Return the maybe-align interpreted from this constant.
  /// @return Optional alignment.
  MaybeAlign getMaybeAlignValue() const { return Value->getMaybeAlignValue(); }
  /// Return the alignment interpreted from this constant.
  /// @return Alignment value.
  Align getAlignValue() const { return Value->getAlignValue(); }

  /// Return true if the constant is one.
  /// @return True when value is 1.
  bool isOne() const { return Value->isOne(); }
  /// Return true if the constant is zero.
  /// @return True when value is 0.
  bool isZero() const { return Value->isZero(); }
  /// Return true if the constant is all ones.
  /// @return True when value is -1 / all bits set.
  bool isAllOnes() const { return Value->isMinusOne(); }
  /// Return true if the constant is the max signed value.
  /// @return True for signed maximum.
  bool isMaxSignedValue() const { return Value->isMaxValue(true); }
  /// Return true if the constant is the min signed value.
  /// @return True for signed minimum.
  bool isMinSignedValue() const { return Value->isMinValue(true); }

  /// Return true if this constant is opaque.
  /// @return True when opaque.
  bool isOpaque() const { return ConstantSDNodeBits.IsOpaque; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::Constant ||
           N->getOpcode() == ISD::TargetConstant;
  }
};

uint64_t SDNode::getConstantOperandVal(unsigned Num) const {
  return cast<ConstantSDNode>(getOperand(Num))->getZExtValue();
}

uint64_t SDNode::getAsZExtVal() const {
  return cast<ConstantSDNode>(this)->getZExtValue();
}

const APInt &SDNode::getConstantOperandAPInt(unsigned Num) const {
  return cast<ConstantSDNode>(getOperand(Num))->getAPIntValue();
}

const APInt &SDNode::getAsAPIntVal() const {
  return cast<ConstantSDNode>(this)->getAPIntValue();
}

/// SDNode representing a floating-point constant.
class ConstantFPSDNode : public SDNode {
  friend class SelectionDAG;

  /// Underlying LLVM ConstantFP.
  const ConstantFP *Value;

  /// Construct a (target) FP constant node.
  /// @param isTarget Whether this is a TargetConstantFP.
  /// @param val Underlying ConstantFP.
  /// @param VTs Result types.
  ConstantFPSDNode(bool isTarget, const ConstantFP *val, SDVTList VTs)
      : SDNode(isTarget ? ISD::TargetConstantFP : ISD::ConstantFP, 0,
               DebugLoc(), VTs),
        Value(val) {
    assert(!isa<VectorType>(val->getType()) && "Unexpected vector type!");
  }

public:
  /// Return the APFloat value.
  /// @return Floating-point APFloat.
  const APFloat& getValueAPF() const { return Value->getValueAPF(); }
  /// Return the underlying ConstantFP.
  /// @return ConstantFP pointer.
  const ConstantFP *getConstantFPValue() const { return Value; }

  /// Return true if the value is positive or negative zero.
  /// @return True if the value is positive or negative zero.
  bool isZero() const { return Value->isZero(); }

  /// Return true if the value is positive zero.
  /// @return True if the value is positive zero.
  bool isPosZero() const { return Value->isPosZero(); }

  /// Return true if the value is negative zero.
  /// @return True if the value is negative zero.
  bool isNegZero() const { return Value->isNegZero(); }

  /// Return true if the value is a NaN.
  /// @return True if the value is a NaN.
  bool isNaN() const { return Value->isNaN(); }

  /// Return true if the value is an infinity
  /// @return True if the value is an infinity.
  bool isInfinity() const { return Value->isInfinity(); }

  /// Return true if the value is negative.
  /// @return True if the value is negative.
  bool isNegative() const { return Value->isNegative(); }

  /// Returns true if this value is exactly +1.0.
  /// @return True if this value is exactly +1.0.
  bool isOne() const { return Value->isOne(); }

  /// Returns true if this value is exactly -1.0.
  /// @return True if this value is exactly -1.0.
  bool isMinusOne() const { return Value->isMinusOne(); }

  /// We don't rely on operator== working on double values, as
  /// it returns true for things that are clearly not equal, like -0.0 and 0.0.
  /// As such, this method can be used to do an exact bit-for-bit comparison of
  /// two floating point values.

  /// Return true if this value is bit-for-bit identical to double \p V.
  ///
  /// We leave the version with the double argument here because it's just so
  /// convenient to write "2.0" and the like.  Without this function we'd
  /// have to duplicate its logic everywhere it's called.
  /// @param V Double value to compare.
  /// @return True if this value is bit-for-bit identical to \p V.
  bool isExactlyValue(double V) const {
    return Value->getValueAPF().isExactlyValue(V);
  }
  /// Return true if this value is bit-for-bit identical to \p V.
  /// @param V APFloat value to compare.
  /// @return True on exact bit match.
  LLVM_ABI bool isExactlyValue(const APFloat &V) const;

  /// Return true if \p Val is a valid ConstantFPSDNode value for type \p VT.
  /// @param VT Value type to validate against.
  /// @param Val APFloat candidate.
  /// @return True when \p Val fits in \p VT.
  LLVM_ABI static bool isValueValidForType(EVT VT, const APFloat &Val);

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::ConstantFP ||
           N->getOpcode() == ISD::TargetConstantFP;
  }
};

std::optional<APInt> SDNode::bitcastToAPInt() const {
  if (auto *CN = dyn_cast<ConstantSDNode>(this))
    return CN->getAPIntValue();
  if (auto *CFPN = dyn_cast<ConstantFPSDNode>(this))
    return CFPN->getValueAPF().bitcastToAPInt();
  return std::nullopt;
}

/// Returns true if \p V is a constant integer zero.
/// @param V Value to test.
/// @return True if \p V is a constant integer zero.
LLVM_ABI bool isNullConstant(SDValue V);

/// Returns true if \p V is a constant integer zero or an UNDEF node.
/// @param V Value to test.
/// @return True if \p V is a constant integer zero or an UNDEF node.
LLVM_ABI bool isNullConstantOrUndef(SDValue V);

/// Returns true if \p V is an FP constant with a value of positive zero.
/// @param V Value to test.
/// @return True if \p V is an FP constant with a value of positive zero.
LLVM_ABI bool isNullFPConstant(SDValue V);

/// Returns true if \p V is an integer constant with all bits set.
/// @param V Value to test.
/// @return True if \p V is an integer constant with all bits set.
LLVM_ABI bool isAllOnesConstant(SDValue V);

/// Returns true if \p V is a constant integer one.
/// @param V Value to test.
/// @return True if \p V is a constant integer one.
LLVM_ABI bool isOneConstant(SDValue V);

/// Returns true if \p V is a constant min signed integer value.
/// @param V Value to test.
/// @return True if \p V is a constant min signed integer value.
LLVM_ABI bool isMinSignedConstant(SDValue V);

/// Return the non-bitcasted source operand of \p V if it exists.
/// If \p V is not a bitcasted value, it is returned as-is.
/// @param V Value to peek through.
/// @return The non-bitcasted source operand of \p V if it exists.
LLVM_ABI SDValue peekThroughBitcasts(SDValue V);

/// Return the non-bitcasted and one-use source operand of \p V if it exists.
/// If \p V is not a bitcasted one-use value, it is returned as-is.
/// @param V Value to peek through.
/// @return The non-bitcasted and one-use source operand of \p V if it exists.
LLVM_ABI SDValue peekThroughOneUseBitcasts(SDValue V);

/// Return the non-extracted vector source operand of \p V if it exists.
/// If \p V is not an extracted subvector, it is returned as-is.
/// @param V Value to peek through.
/// @return The non-extracted vector source operand of \p V if it exists.
LLVM_ABI SDValue peekThroughExtractSubvectors(SDValue V);

/// Recursively peek through INSERT_VECTOR_ELT nodes.
///
/// Returning the source vector operand of \p V, as long as \p V is an
/// INSERT_VECTOR_ELT operation that do not insert into any of the demanded
/// vector elts.
/// @param V Value to peek through.
/// @param DemandedElts Elements that must remain undisturbed.
/// @return The source vector after peeling irrelevant INSERT_VECTOR_ELT nodes.
LLVM_ABI SDValue peekThroughInsertVectorElt(SDValue V,
                                            const APInt &DemandedElts);

/// Return the non-truncated source operand of \p V if it exists.
/// If \p V is not a truncation, it is returned as-is.
/// @param V Value to peek through.
/// @return The non-truncated source operand of \p V if it exists.
LLVM_ABI SDValue peekThroughTruncates(SDValue V);

/// Return the non-frozen source operand of \p V if it exists.
/// If \p V is not a freeze, it is returned as-is.
/// @param V Value to peek through.
/// @return The non-frozen source operand of \p V if it exists.
inline SDValue peekThroughFreeze(SDValue V) {
  if (V.getOpcode() == ISD::FREEZE)
    return V.getOperand(0);
  return V;
}

/// Return the non-frozen one-use source operand of \p V if it exists.
///
/// If \p V is not a single-use freeze, it is returned as-is.
/// @param V Value to peek through.
/// @return The non-frozen source operand of \p V if it exists and \p V has a single use.
inline SDValue peekThroughOneUseFreeze(SDValue V) {
  if (V.getOpcode() == ISD::FREEZE && V.hasOneUse())
    return V.getOperand(0);
  return V;
}

/// Returns true if \p V is a bitwise not operation.
///
/// Assumes that an all ones constant is canonicalized to be operand 1.
/// @param V Value to test.
/// @param AllowUndefs Whether undef bits are permitted.
/// @return True if \p V is a bitwise not operation.
LLVM_ABI bool isBitwiseNot(SDValue V, bool AllowUndefs = false);

/// If \p V is a bitwise not, return the inverted operand.
///
/// Otherwise returns an empty SDValue. Only bits set in \p Mask are required to
/// be inverted, other bits may be arbitrary.
/// @param V Value to inspect.
/// @param Mask Bits required to be inverted.
/// @param AllowUndefs Whether undef bits are permitted.
/// @return The inverted operand, or an empty SDValue if \p V is not a bitwise not.
LLVM_ABI SDValue getBitwiseNotOperand(SDValue V, SDValue Mask,
                                      bool AllowUndefs);

/// Returns the SDNode if it is a constant splat BuildVector or constant int.
/// @param N Value to inspect.
/// @param AllowUndefs Whether undef elements are permitted.
/// @param AllowTruncation Whether build-vector truncation is allowed.
/// @return The SDNode if it is a constant splat BuildVector or constant int.
LLVM_ABI ConstantSDNode *isConstOrConstSplat(SDValue N,
                                             bool AllowUndefs = false,
                                             bool AllowTruncation = false);

/// Returns the SDNode if it is a demanded constant splat BuildVector or constant int.
/// @param N Value to inspect.
/// @param DemandedElts Elements that must participate in the splat.
/// @param AllowUndefs Whether undef elements are permitted.
/// @param AllowTruncation Whether build-vector truncation is allowed.
/// @return The SDNode if it is a demanded constant splat BuildVector or constant int.
LLVM_ABI ConstantSDNode *isConstOrConstSplat(SDValue N,
                                             const APInt &DemandedElts,
                                             bool AllowUndefs = false,
                                             bool AllowTruncation = false);

/// Returns the SDNode if it is a constant splat BuildVector or constant float.
/// @param N Value to inspect.
/// @param AllowUndefs Whether undef elements are permitted.
/// @return The SDNode if it is a constant splat BuildVector or constant float.
LLVM_ABI ConstantFPSDNode *isConstOrConstSplatFP(SDValue N,
                                                 bool AllowUndefs = false);

/// Returns the SDNode if it is a demanded constant splat BuildVector or constant float.
/// @param N Value to inspect.
/// @param DemandedElts Elements that must participate in the splat.
/// @param AllowUndefs Whether undef elements are permitted.
/// @return The SDNode if it is a demanded constant splat BuildVector or constant float.
LLVM_ABI ConstantFPSDNode *isConstOrConstSplatFP(SDValue N,
                                                 const APInt &DemandedElts,
                                                 bool AllowUndefs = false);

/// Return true if the value is a constant 0 integer or a null splat.
///
/// Build vector implicit truncation is not an issue for null values.
/// @param V Value to test.
/// @param AllowUndefs Whether undef elements are permitted.
/// @return True if the value is a constant 0 integer or a splatted vector of a constant 0 integer (with no undefs by default).
LLVM_ABI bool isNullOrNullSplat(SDValue V, bool AllowUndefs = false);

/// Return true if the value is a constant 1 integer or a one splat.
///
/// Build vector implicit truncation is allowed, but the truncated bits need to
/// be zero.
/// @param V Value to test.
/// @param AllowUndefs Whether undef elements are permitted.
/// @return True if the value is a constant 1 integer or a splatted vector of a constant 1 integer (with no undefs).
LLVM_ABI bool isOneOrOneSplat(SDValue V, bool AllowUndefs = false);

/// Return true if the value is an FP 1.0 constant or splat.
/// @param V Value to test.
/// @param AllowUndefs Whether undef elements are permitted.
/// @return True if the value is a constant floating-point value, or a splatted vector of a constant floating-point value, of 1.0 (with no undefs).
LLVM_ABI bool isOneOrOneSplatFP(SDValue V, bool AllowUndefs = false);

/// Return true if the value is a constant -1 integer or splat.
///
/// Does not permit build vector implicit truncation.
/// @param V Value to test.
/// @param AllowUndefs Whether undef elements are permitted.
/// @return True if the value is a constant -1 integer or a splatted vector of a constant -1 integer (with no undefs).
LLVM_ABI bool isAllOnesOrAllOnesSplat(SDValue V, bool AllowUndefs = false);

/// Return true if the value is a constant 1 integer or splat without truncation.
///
/// Does not permit build vector implicit truncation.
/// @param N Value to test.
/// @param AllowUndefs Whether undef elements are permitted.
/// @return True if the value is a constant 1 integer or a splatted vector of a constant 1 integer (with no undefs).
LLVM_ABI bool isOnesOrOnesSplat(SDValue N, bool AllowUndefs = false);

/// Return true if the value is a constant 0 integer or splat.
///
/// Build vector implicit truncation is allowed.
/// @param N Value to test.
/// @param AllowUndefs Whether undef elements are permitted.
/// @return True if the value is a constant 0 integer or a splatted vector of a constant 0 integer (with no undefs).
LLVM_ABI bool isZeroOrZeroSplat(SDValue N, bool AllowUndefs = false);

/// Return true if the value is a (+/-)0.0 FP constant or splat.
/// @param N Value to test.
/// @param AllowUndefs Whether undef elements are permitted.
/// @return True if the value is a constant (+/-)0.0 floating-point value or a splatted vector thereof (with no undefs).
LLVM_ABI bool isZeroOrZeroSplatFP(SDValue N, bool AllowUndefs = false);

/// Return true if \p V is either a integer or FP constant.
/// @param V Value to test.
/// @return True if \p V is either a integer or FP constant.
inline bool isIntOrFPConstant(SDValue V) {
  return isa<ConstantSDNode>(V) || isa<ConstantFPSDNode>(V);
}

/// SDNode referencing a GlobalValue with optional offset and flags.
class GlobalAddressSDNode : public SDNode {
  friend class SelectionDAG;

  /// Referenced global value.
  const GlobalValue *TheGlobal;
  /// Byte offset from the global.
  int64_t Offset;
  /// Target-dependent flags.
  unsigned TargetFlags;

  /// Construct a global address node.
  /// @param Opc Opcode (GlobalAddress or TLS variant).
  /// @param Order IR order.
  /// @param DL Debug location.
  /// @param GA Global value.
  /// @param VTs Result types.
  /// @param o Offset in bytes.
  /// @param TF Target flags.
  GlobalAddressSDNode(unsigned Opc, unsigned Order, const DebugLoc &DL,
                      const GlobalValue *GA, SDVTList VTs, int64_t o,
                      unsigned TF)
      : SDNode(Opc, Order, DL, VTs), TheGlobal(GA), Offset(o), TargetFlags(TF) {
  }

public:
  /// Return the referenced global.
  /// @return GlobalValue pointer.
  const GlobalValue *getGlobal() const { return TheGlobal; }
  /// Return the byte offset.
  /// @return Offset from the global.
  int64_t getOffset() const { return Offset; }
  /// Return target-dependent flags.
  /// @return Target flags.
  unsigned getTargetFlags() const { return TargetFlags; }
  /// Return the address space this GlobalAddress belongs to.
  /// @return Address space id.
  LLVM_ABI unsigned getAddressSpace() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::GlobalAddress ||
           N->getOpcode() == ISD::TargetGlobalAddress ||
           N->getOpcode() == ISD::GlobalTLSAddress ||
           N->getOpcode() == ISD::TargetGlobalTLSAddress;
  }
};

/// SDNode representing a deactivation symbol global.
class DeactivationSymbolSDNode : public SDNode {
  friend class SelectionDAG;

  /// Referenced global value.
  const GlobalValue *TheGlobal;

  /// Construct a deactivation symbol node for \\p GV.
  /// @param GV Global value.
  /// @param VTs Result types.
  DeactivationSymbolSDNode(const GlobalValue *GV, SDVTList VTs)
      : SDNode(ISD::DEACTIVATION_SYMBOL, 0, DebugLoc(), VTs), TheGlobal(GV) {}

public:
  /// Return the referenced global.
  /// @return GlobalValue pointer.
  const GlobalValue *getGlobal() const { return TheGlobal; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::DEACTIVATION_SYMBOL;
  }
};

/// SDNode representing a stack frame index.
class FrameIndexSDNode : public SDNode {
  friend class SelectionDAG;

  /// Frame index.
  int FI;

  /// Construct a (target) frame index node.
  /// @param fi Frame index.
  /// @param VTs Result types.
  /// @param isTarg Whether this is a TargetFrameIndex.
  FrameIndexSDNode(int fi, SDVTList VTs, bool isTarg)
      : SDNode(isTarg ? ISD::TargetFrameIndex : ISD::FrameIndex, 0, DebugLoc(),
               VTs),
        FI(fi) {}

public:
  /// Return the frame index.
  /// @return Frame index.
  int getIndex() const { return FI; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::FrameIndex ||
           N->getOpcode() == ISD::TargetFrameIndex;
  }
};

/// This SDNode is used for LIFETIME_START/LIFETIME_END values.
class LifetimeSDNode : public SDNode {
  friend class SelectionDAG;

  /// Construct a lifetime start/end node.
  /// @param Opcode LIFETIME_START or LIFETIME_END.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  LifetimeSDNode(unsigned Opcode, unsigned Order, const DebugLoc &dl,
                 SDVTList VTs)
      : SDNode(Opcode, Order, dl, VTs) {}

public:
  /// Return the frame index operand.
  /// @return Frame index.
  int64_t getFrameIndex() const {
    return cast<FrameIndexSDNode>(getOperand(1))->getIndex();
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::LIFETIME_START ||
           N->getOpcode() == ISD::LIFETIME_END;
  }
};

/// Pseudo-probe SDNode holding function guid and block index.
///
/// A pseudo probe serves as a place holder and will be removed at the end of
/// compilation. It does not have any operand because we do not want the
/// instruction selection to deal with any.
class PseudoProbeSDNode : public SDNode {
  friend class SelectionDAG;
  /// Function GUID being probed.
  uint64_t Guid;
  /// Basic-block probe index.
  uint64_t Index;
  /// Probe attributes bitfield.
  uint32_t Attributes;

  /// Construct a pseudo-probe node.
  /// @param Opcode PSEUDO_PROBE opcode.
  /// @param Order IR order.
  /// @param Dl Debug location.
  /// @param VTs Result types.
  /// @param Guid Function GUID.
  /// @param Index Probe index.
  /// @param Attr Probe attributes.
  PseudoProbeSDNode(unsigned Opcode, unsigned Order, const DebugLoc &Dl,
                    SDVTList VTs, uint64_t Guid, uint64_t Index, uint32_t Attr)
      : SDNode(Opcode, Order, Dl, VTs), Guid(Guid), Index(Index),
        Attributes(Attr) {}

public:
  /// Return the function GUID.
  /// @return Function GUID.
  uint64_t getGuid() const { return Guid; }
  /// Return the probe index.
  /// @return Probe index.
  uint64_t getIndex() const { return Index; }
  /// Return probe attributes.
  /// @return Attributes bitfield.
  uint32_t getAttributes() const { return Attributes; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::PSEUDO_PROBE;
  }
};

/// SDNode referencing a jump table index.
class JumpTableSDNode : public SDNode {
  friend class SelectionDAG;

  /// Jump table index.
  int JTI;
  /// Target-dependent flags.
  unsigned TargetFlags;

  /// Construct a (target) jump table node.
  /// @param jti Jump table index.
  /// @param VTs Result types.
  /// @param isTarg Whether this is a TargetJumpTable.
  /// @param TF Target flags.
  JumpTableSDNode(int jti, SDVTList VTs, bool isTarg, unsigned TF)
      : SDNode(isTarg ? ISD::TargetJumpTable : ISD::JumpTable, 0, DebugLoc(),
               VTs),
        JTI(jti), TargetFlags(TF) {}

public:
  /// Return the jump table index.
  /// @return Jump table index.
  int getIndex() const { return JTI; }
  /// Return target-dependent flags.
  /// @return Target flags.
  unsigned getTargetFlags() const { return TargetFlags; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::JumpTable ||
           N->getOpcode() == ISD::TargetJumpTable;
  }
};

/// SDNode referencing a constant pool entry.
class ConstantPoolSDNode : public SDNode {
  friend class SelectionDAG;

  /// Constant or machine constant-pool value payload.
  union {
    /// IR constant value.
    const Constant *ConstVal;
    /// Machine constant pool value.
    MachineConstantPoolValue *MachineCPVal;
  } Val;
  /// Offset; top bit set means MachineConstantPoolValue.
  int Offset;  // It's a MachineConstantPoolValue if top bit is set.
  /// Minimum alignment requirement of the constant pool entry.
  Align Alignment; // Minimum alignment requirement of CP.
  /// Target-dependent flags.
  unsigned TargetFlags;

  /// Construct a constant-pool node from IR constant \p c.
  /// @param isTarget Whether this is a TargetConstantPool.
  /// @param c IR constant.
  /// @param VTs Result types.
  /// @param o Offset.
  /// @param Alignment Required alignment.
  /// @param TF Target flags.
  ConstantPoolSDNode(bool isTarget, const Constant *c, SDVTList VTs, int o,
                     Align Alignment, unsigned TF)
      : SDNode(isTarget ? ISD::TargetConstantPool : ISD::ConstantPool, 0,
               DebugLoc(), VTs),
        Offset(o), Alignment(Alignment), TargetFlags(TF) {
    assert(Offset >= 0 && "Offset is too large");
    Val.ConstVal = c;
  }

  /// Construct a constant-pool node from machine constant \p v.
  /// @param isTarget Whether this is a TargetConstantPool.
  /// @param v Machine constant pool value.
  /// @param VTs Result types.
  /// @param o Offset.
  /// @param Alignment Required alignment.
  /// @param TF Target flags.
  ConstantPoolSDNode(bool isTarget, MachineConstantPoolValue *v, SDVTList VTs,
                     int o, Align Alignment, unsigned TF)
      : SDNode(isTarget ? ISD::TargetConstantPool : ISD::ConstantPool, 0,
               DebugLoc(), VTs),
        Offset(o), Alignment(Alignment), TargetFlags(TF) {
    assert(Offset >= 0 && "Offset is too large");
    Val.MachineCPVal = v;
    Offset |= 1 << (sizeof(unsigned)*CHAR_BIT-1);
  }

public:
  /// Return true if this entry holds a MachineConstantPoolValue.
  /// @return True for machine constant pool entries.
  bool isMachineConstantPoolEntry() const {
    return Offset < 0;
  }

  /// Return the IR constant value.
  /// @return Constant pointer.
  const Constant *getConstVal() const {
    assert(!isMachineConstantPoolEntry() && "Wrong constantpool type");
    return Val.ConstVal;
  }

  /// Return the machine constant pool value.
  /// @return MachineConstantPoolValue pointer.
  MachineConstantPoolValue *getMachineCPVal() const {
    assert(isMachineConstantPoolEntry() && "Wrong constantpool type");
    return Val.MachineCPVal;
  }

  /// Return the constant pool offset with the machine-entry bit cleared.
  /// @return Offset value.
  int getOffset() const {
    return Offset & ~(1 << (sizeof(unsigned)*CHAR_BIT-1));
  }

  /// Return the alignment of this constant pool object.
  /// @return Alignment, or default alignment when zero-like.
  Align getAlign() const { return Alignment; }
  /// Return target-dependent flags.
  /// @return Target flags.
  unsigned getTargetFlags() const { return TargetFlags; }

  /// Return the type of the constant pool value.
  /// @return LLVM Type of the entry.
  LLVM_ABI Type *getType() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::ConstantPool ||
           N->getOpcode() == ISD::TargetConstantPool;
  }
};

/// Completely target-dependent object reference.
class TargetIndexSDNode : public SDNode {
  friend class SelectionDAG;

  /// Target-dependent flags.
  unsigned TargetFlags;
  /// Target index.
  int Index;
  /// Byte offset.
  int64_t Offset;

public:
  /// Construct a target index node.
  /// @param Idx Target index.
  /// @param VTs Result types.
  /// @param Ofs Byte offset.
  /// @param TF Target flags.
  TargetIndexSDNode(int Idx, SDVTList VTs, int64_t Ofs, unsigned TF)
      : SDNode(ISD::TargetIndex, 0, DebugLoc(), VTs), TargetFlags(TF),
        Index(Idx), Offset(Ofs) {}

  /// Return target-dependent flags.
  /// @return Target flags.
  unsigned getTargetFlags() const { return TargetFlags; }
  /// Return the target index.
  /// @return Target index.
  int getIndex() const { return Index; }
  /// Return the byte offset.
  /// @return Offset.
  int64_t getOffset() const { return Offset; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::TargetIndex;
  }
};

/// SDNode referencing a MachineBasicBlock.
class BasicBlockSDNode : public SDNode {
  friend class SelectionDAG;

  /// Referenced machine basic block.
  MachineBasicBlock *MBB;

  /// Construct a basic-block node for \\p mbb.
  ///
  /// Debug info is meaningful and potentially useful here, but we create
  /// blocks out of order when they're jumped to, which makes it a bit
  /// harder.  Let's see if we need it first.
  /// @param mbb Machine basic block.
  explicit BasicBlockSDNode(MachineBasicBlock *mbb)
    : SDNode(ISD::BasicBlock, 0, DebugLoc(), getSDVTList(MVT::Other)), MBB(mbb)
  {}

public:
  /// Return the machine basic block.
  /// @return MachineBasicBlock pointer.
  MachineBasicBlock *getBasicBlock() const { return MBB; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::BasicBlock;
  }
};

/// A "pseudo-class" with methods for operating on BUILD_VECTORs.
class BuildVectorSDNode : public SDNode {
public:
  /// BuildVectorSDNode is not directly constructible; cast from SDNode.
  explicit BuildVectorSDNode() = delete;

  /// Return true if this is a constant splat of a small element size.
  ///
  /// If so, find the smallest element size that splats the vector. If
  /// MinSplatBits is nonzero, the element size must be at least that large.
  /// Note that the splat element may be the entire vector (i.e., a one element
  /// vector). Returns the splat element value in SplatValue. Any undefined bits
  /// in that value are zero, and the corresponding bits in the SplatUndef mask
  /// are set. The SplatBitSize value is set to the splat element size in bits.
  /// HasAnyUndefs is set to true if any bits in the vector are undefined.
  /// isBigEndian describes the endianness of the target.
  /// @param SplatValue Set to the splat element value.
  /// @param SplatUndef Set for undefined bits in \p SplatValue.
  /// @param SplatBitSize Set to the splat element size in bits.
  /// @param HasAnyUndefs Set if any vector bits are undefined.
  /// @param MinSplatBits Minimum allowed splat element size, or 0.
  /// @param isBigEndian Whether the target is big-endian.
  /// @return True if this is a constant splat.
  LLVM_ABI bool isConstantSplat(APInt &SplatValue, APInt &SplatUndef,
                                unsigned &SplatBitSize, bool &HasAnyUndefs,
                                unsigned MinSplatBits = 0,
                                bool isBigEndian = false) const;

  /// Return the demanded splatted value, or null if this is not a splat.
  ///
  /// The DemandedElts mask indicates the elements that must be in the splat.
  /// If passed a non-null UndefElements bitvector, it will resize it to match
  /// the vector width and set the bits where elements are undef.
  /// @param DemandedElts Elements that must participate in the splat.
  /// @param UndefElements Optional bitvector of undef element positions.
  /// @return The demanded splatted value or a null value if this is not a splat.
  LLVM_ABI SDValue getSplatValue(const APInt &DemandedElts,
                                 BitVector *UndefElements = nullptr) const;

  /// Return the splatted value, or null if this is not a splat.
  ///
  /// If passed a non-null UndefElements bitvector, it will resize it to match
  /// the vector width and set the bits where elements are undef.
  /// @param UndefElements Optional bitvector of undef element positions.
  /// @return The splatted value or a null value if this is not a splat.
  LLVM_ABI SDValue getSplatValue(BitVector *UndefElements = nullptr) const;

  /// Find the shortest repeating sequence of demanded build-vector values.
  ///
  /// e.g. { u, X, u, X, u, u, X, u } -> { X }
  ///      { X, Y, u, Y, u, u, X, u } -> { X, Y }
  ///
  /// Currently this must be a power-of-2 build vector.
  /// The DemandedElts mask indicates the elements that must be present,
  /// undemanded elements in Sequence may be null (SDValue()). If passed a
  /// non-null UndefElements bitvector, it will resize it to match the original
  /// vector width and set the bits where elements are undef. If result is
  /// false, Sequence will be empty.
  /// @param DemandedElts Elements that must be present in the sequence.
  /// @param Sequence Filled with the repeating sequence on success.
  /// @param UndefElements Optional bitvector of undef element positions.
  /// @return True if a repeating sequence was found.
  LLVM_ABI bool getRepeatedSequence(const APInt &DemandedElts,
                                    SmallVectorImpl<SDValue> &Sequence,
                                    BitVector *UndefElements = nullptr) const;

  /// Find the shortest repeating sequence over all build-vector elements.
  ///
  /// e.g. { u, X, u, X, u, u, X, u } -> { X }
  ///      { X, Y, u, Y, u, u, X, u } -> { X, Y }
  ///
  /// Currently this must be a power-of-2 build vector.
  /// If passed a non-null UndefElements bitvector, it will resize it to match
  /// the original vector width and set the bits where elements are undef.
  /// If result is false, Sequence will be empty.
  /// @param Sequence Filled with the repeating sequence on success.
  /// @param UndefElements Optional bitvector of undef element positions.
  /// @return True if a repeating sequence was found.
  LLVM_ABI bool getRepeatedSequence(SmallVectorImpl<SDValue> &Sequence,
                                    BitVector *UndefElements = nullptr) const;

  /// Return the demanded splatted integer constant, or null if not a splat.
  ///
  /// The DemandedElts mask indicates the elements that must be in the splat.
  /// If passed a non-null UndefElements bitvector, it will resize it to match
  /// the vector width and set the bits where elements are undef.
  /// @param DemandedElts Elements that must participate in the splat.
  /// @param UndefElements Optional bitvector of undef element positions.
  /// @return The demanded splatted constant or null if this is not a constant splat.
  LLVM_ABI ConstantSDNode *
  getConstantSplatNode(const APInt &DemandedElts,
                       BitVector *UndefElements = nullptr) const;

  /// Return the splatted integer constant, or null if not a constant splat.
  ///
  /// If passed a non-null UndefElements bitvector, it will resize it to match
  /// the vector width and set the bits where elements are undef.
  /// @param UndefElements Optional bitvector of undef element positions.
  /// @return The splatted constant or null if this is not a constant splat.
  LLVM_ABI ConstantSDNode *
  getConstantSplatNode(BitVector *UndefElements = nullptr) const;

  /// Return the demanded splatted FP constant, or null if not a splat.
  ///
  /// The DemandedElts mask indicates the elements that must be in the splat.
  /// If passed a non-null UndefElements bitvector, it will resize it to match
  /// the vector width and set the bits where elements are undef.
  /// @param DemandedElts Elements that must participate in the splat.
  /// @param UndefElements Optional bitvector of undef element positions.
  /// @return The demanded splatted constant FP or null if this is not a constant FP splat.
  LLVM_ABI ConstantFPSDNode *
  getConstantFPSplatNode(const APInt &DemandedElts,
                         BitVector *UndefElements = nullptr) const;

  /// Return the splatted FP constant, or null if not a constant FP splat.
  ///
  /// If passed a non-null UndefElements bitvector, it will resize it to match
  /// the vector width and set the bits where elements are undef.
  /// @param UndefElements Optional bitvector of undef element positions.
  /// @return The splatted constant FP or null if this is not a constant FP splat.
  LLVM_ABI ConstantFPSDNode *
  getConstantFPSplatNode(BitVector *UndefElements = nullptr) const;

  /// Return log2 of an exact power-of-two FP splat, or -1.
  ///
  /// The BitWidth specifies the necessary bit precision.
  /// @param UndefElements Optional bitvector of undef element positions.
  /// @param BitWidth Required bit precision for the log2 result.
  /// @return The log base 2 integer value.
  LLVM_ABI int32_t getConstantFPSplatPow2ToLog2Int(BitVector *UndefElements,
                                                   uint32_t BitWidth) const;

  /// Extract raw bit data from constant or undef build-vector elements.
  ///
  /// Each raw bit element will be \p DstEltSizeInBits wide, undef elements are
  /// treated as zero, and entirely undefined elements are flagged in
  /// \p UndefElements.
  /// @param IsLittleEndian Whether to interpret bits little-endian.
  /// @param DstEltSizeInBits Width of each extracted raw element.
  /// @param RawBitElements Filled with extracted bit elements.
  /// @param UndefElements Set for entirely undefined elements.
  /// @return True if raw bit data was extracted successfully.
  LLVM_ABI bool getConstantRawBits(bool IsLittleEndian,
                                   unsigned DstEltSizeInBits,
                                   SmallVectorImpl<APInt> &RawBitElements,
                                   BitVector &UndefElements) const;

  /// Return true if every element is a constant or undef.
  /// @return True when this is a constant build vector.
  LLVM_ABI bool isConstant() const;

  /// Return the start and step if this is a constant arithmetic sequence.
  ///
  /// If this BuildVector is constant and represents an arithmetic sequence
  /// "<a, a+n, a+2n, a+3n, ...>" where a is integer and n is a non-zero
  /// integer, the value "<a, n>" is returned. Arithmetic is performed modulo
  /// 2^BitWidth, so this also matches sequences that wrap around. Poison
  /// elements are ignored and can take any value.
  /// @return The pair \<a, n\> if this is an arithmetic sequence; otherwise std::nullopt.
  LLVM_ABI std::optional<std::pair<APInt, APInt>> isArithmeticSequence() const;

  /// Recast raw bit elements to a different element width.
  ///
  /// Undef elements are treated as zero, and entirely undefined elements are
  /// flagged in \p DstUndefElements.
  /// @param IsLittleEndian Whether to interpret bits little-endian.
  /// @param DstEltSizeInBits Destination element width in bits.
  /// @param DstBitElements Filled with recast destination elements.
  /// @param SrcBitElements Source raw bit elements.
  /// @param DstUndefElements Set for entirely undefined destination elements.
  /// @param SrcUndefElements Undef mask for source elements.
  LLVM_ABI static void recastRawBits(bool IsLittleEndian,
                                     unsigned DstEltSizeInBits,
                                     SmallVectorImpl<APInt> &DstBitElements,
                                     ArrayRef<APInt> SrcBitElements,
                                     BitVector &DstUndefElements,
                                     const BitVector &SrcUndefElements);

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::BUILD_VECTOR;
  }
};

/// SDNode holding an arbitrary LLVM IR Value reference.
///
/// Used when the SelectionDAG needs to make a simple reference to something
/// in the LLVM IR representation.
class SrcValueSDNode : public SDNode {
  friend class SelectionDAG;

  /// Referenced LLVM IR value.
  const Value *V;

  /// Create a SrcValue for a general value.
  /// @param v IR value to reference.
  explicit SrcValueSDNode(const Value *v)
    : SDNode(ISD::SRCVALUE, 0, DebugLoc(), getSDVTList(MVT::Other)), V(v) {}

public:
  /// Return the contained Value.
  /// @return The contained Value.
  const Value *getValue() const { return V; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::SRCVALUE;
  }
};

/// SDNode wrapping an LLVM metadata node.
class MDNodeSDNode : public SDNode {
  friend class SelectionDAG;

  /// Referenced metadata node.
  const MDNode *MD;

  /// Construct an MDNode SDNode for \\p md.
  /// @param md Metadata node.
  explicit MDNodeSDNode(const MDNode *md)
  : SDNode(ISD::MDNODE_SDNODE, 0, DebugLoc(), getSDVTList(MVT::Other)), MD(md)
  {}

public:
  /// Return the metadata node.
  /// @return MDNode pointer.
  const MDNode *getMD() const { return MD; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MDNODE_SDNODE;
  }
};

/// SDNode representing a virtual or physical register.
class RegisterSDNode : public SDNode {
  friend class SelectionDAG;

  /// Register value.
  Register Reg;

  /// Construct a register node for \\p reg.
  /// @param reg Register.
  /// @param VTs Result types.
  RegisterSDNode(Register reg, SDVTList VTs)
      : SDNode(ISD::Register, 0, DebugLoc(), VTs), Reg(reg) {}

public:
  /// Return the register.
  /// @return Register value.
  Register getReg() const { return Reg; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::Register;
  }
};

/// SDNode holding a register mask bitvector.
class RegisterMaskSDNode : public SDNode {
  friend class SelectionDAG;

  // The memory for RegMask is not owned by the node.
  /// Register mask bits (not owned).
  const uint32_t *RegMask;

  /// Construct a register-mask node for \\p mask.
  /// @param mask Register mask bitvector.
  RegisterMaskSDNode(const uint32_t *mask)
    : SDNode(ISD::RegisterMask, 0, DebugLoc(), getSDVTList(MVT::Untyped)),
      RegMask(mask) {}

public:
  /// Return the register mask.
  /// @return Pointer to mask words.
  const uint32_t *getRegMask() const { return RegMask; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::RegisterMask;
  }
};

/// SDNode referencing a BlockAddress with optional offset and flags.
class BlockAddressSDNode : public SDNode {
  friend class SelectionDAG;

  /// Referenced block address.
  const BlockAddress *BA;
  /// Byte offset.
  int64_t Offset;
  /// Target-dependent flags.
  unsigned TargetFlags;

  /// Construct a block-address node.
  /// @param NodeTy Opcode.
  /// @param VTs Result types.
  /// @param ba Block address.
  /// @param o Offset.
  /// @param Flags Target flags.
  BlockAddressSDNode(unsigned NodeTy, SDVTList VTs, const BlockAddress *ba,
                     int64_t o, unsigned Flags)
      : SDNode(NodeTy, 0, DebugLoc(), VTs), BA(ba), Offset(o),
        TargetFlags(Flags) {}

public:
  /// Return the block address.
  /// @return BlockAddress pointer.
  const BlockAddress *getBlockAddress() const { return BA; }
  /// Return the byte offset.
  /// @return Offset.
  int64_t getOffset() const { return Offset; }
  /// Return target-dependent flags.
  /// @return Target flags.
  unsigned getTargetFlags() const { return TargetFlags; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::BlockAddress ||
           N->getOpcode() == ISD::TargetBlockAddress;
  }
};

/// SDNode wrapping an MCSymbol label.
class LabelSDNode : public SDNode {
  friend class SelectionDAG;

  /// Referenced MCSymbol.
  MCSymbol *Label;

  /// Construct a label node for \\p L.
  /// @param Opcode EH_LABEL or ANNOTATION_LABEL.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param L MCSymbol.
  LabelSDNode(unsigned Opcode, unsigned Order, const DebugLoc &dl, MCSymbol *L)
      : SDNode(Opcode, Order, dl, getSDVTList(MVT::Other)), Label(L) {
    assert(LabelSDNode::classof(this) && "not a label opcode");
  }

public:
  /// Return the label symbol.
  /// @return MCSymbol pointer.
  MCSymbol *getLabel() const { return Label; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::EH_LABEL ||
           N->getOpcode() == ISD::ANNOTATION_LABEL;
  }
};

/// SDNode referencing an external symbol name.
class ExternalSymbolSDNode : public SDNode {
  friend class SelectionDAG;

  /// External symbol name.
  const char *Symbol;
  /// Target-dependent flags.
  unsigned TargetFlags;

  /// Construct an (target) external symbol node.
  /// @param isTarget Whether this is a TargetExternalSymbol.
  /// @param Sym Symbol name.
  /// @param TF Target flags.
  /// @param VTs Result types.
  ExternalSymbolSDNode(bool isTarget, const char *Sym, unsigned TF,
                       SDVTList VTs)
      : SDNode(isTarget ? ISD::TargetExternalSymbol : ISD::ExternalSymbol, 0,
               DebugLoc(), VTs),
        Symbol(Sym), TargetFlags(TF) {}

public:
  /// Return the symbol name.
  /// @return C string symbol.
  const char *getSymbol() const { return Symbol; }
  /// Return target-dependent flags.
  /// @return Target flags.
  unsigned getTargetFlags() const { return TargetFlags; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::ExternalSymbol ||
           N->getOpcode() == ISD::TargetExternalSymbol;
  }
};

/// SDNode wrapping an MCSymbol.
class MCSymbolSDNode : public SDNode {
  friend class SelectionDAG;

  /// Referenced MCSymbol.
  MCSymbol *Symbol;

  /// Construct an MCSymbol node.
  /// @param Symbol MCSymbol.
  /// @param VTs Result types.
  MCSymbolSDNode(MCSymbol *Symbol, SDVTList VTs)
      : SDNode(ISD::MCSymbol, 0, DebugLoc(), VTs), Symbol(Symbol) {}

public:
  /// Return the MCSymbol.
  /// @return MCSymbol pointer.
  MCSymbol *getMCSymbol() const { return Symbol; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MCSymbol;
  }
};

/// SDNode holding an ISD condition code.
class CondCodeSDNode : public SDNode {
  friend class SelectionDAG;

  /// Condition code value.
  ISD::CondCode Condition;

  /// Construct a condition-code node for \\p Cond.
  /// @param Cond ISD condition code.
  explicit CondCodeSDNode(ISD::CondCode Cond)
    : SDNode(ISD::CONDCODE, 0, DebugLoc(), getSDVTList(MVT::Other)),
      Condition(Cond) {}

public:
  /// Return the condition code.
  /// @return ISD::CondCode value.
  ISD::CondCode get() const { return Condition; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::CONDCODE;
  }
};

/// This class is used to represent EVT's, which are used
/// to parameterize some operations.
class VTSDNode : public SDNode {
  friend class SelectionDAG;

  /// Contained value type.
  EVT ValueType;

  /// Construct a value-type node for \\p VT.
  /// @param VT Value type.
  explicit VTSDNode(EVT VT)
    : SDNode(ISD::VALUETYPE, 0, DebugLoc(), getSDVTList(MVT::Other)),
      ValueType(VT) {}

public:
  /// Return the value type.
  /// @return Contained EVT.
  EVT getVT() const { return ValueType; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::VALUETYPE;
  }
};

/// Base class for LoadSDNode and StoreSDNode.
class LSBaseSDNode : public MemSDNode {
public:
  /// Construct a load/store base node.
  /// @param NodeTy LOAD or STORE opcode.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param AM Indexed addressing mode.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  LSBaseSDNode(ISD::NodeType NodeTy, unsigned Order, const DebugLoc &dl,
               SDVTList VTs, ISD::MemIndexedMode AM, EVT MemVT,
               MachineMemOperand *MMO)
      : MemSDNode(NodeTy, Order, dl, VTs, MemVT, MMO) {
    LSBaseSDNodeBits.AddressingMode = AM;
    assert(getAddressingMode() == AM && "Value truncated");
  }

  /// Return the offset operand.
  /// @return Offset SDValue.
  const SDValue &getOffset() const {
    return getOperand(getOpcode() == ISD::LOAD ? 2 : 3);
  }

  /// Return the addressing mode for this load or store:
  /// unindexed, pre-inc, pre-dec, post-inc, or post-dec.
  /// @return The addressing mode for this load or store: unindexed, pre-inc, pre-dec, post-inc, or post-dec.
  ISD::MemIndexedMode getAddressingMode() const {
    return static_cast<ISD::MemIndexedMode>(LSBaseSDNodeBits.AddressingMode);
  }

  /// Return true if this is a pre/post inc/dec load/store.
  /// @return True if this is a pre/post inc/dec load/store.
  bool isIndexed() const { return getAddressingMode() != ISD::UNINDEXED; }

  /// Return true if this is NOT a pre/post inc/dec load/store.
  /// @return True if this is NOT a pre/post inc/dec load/store.
  bool isUnindexed() const { return getAddressingMode() == ISD::UNINDEXED; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::LOAD ||
           N->getOpcode() == ISD::STORE;
  }
};

/// This class is used to represent ISD::LOAD nodes.
class LoadSDNode : public LSBaseSDNode {
  friend class SelectionDAG;

  /// Construct a LOAD node.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param AM Indexed addressing mode.
  /// @param ETy Load extension type.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  LoadSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs,
             ISD::MemIndexedMode AM, ISD::LoadExtType ETy, EVT MemVT,
             MachineMemOperand *MMO)
      : LSBaseSDNode(ISD::LOAD, Order, dl, VTs, AM, MemVT, MMO) {
    LoadSDNodeBits.ExtTy = ETy;
    assert(readMem() && "Load MachineMemOperand is not a load!");
    assert(!writeMem() && "Load MachineMemOperand is a store!");
  }

public:
  /// Return the load extension type.
  /// @return Whether this is a plain node, or one of the varieties of value-extending loads.
  ISD::LoadExtType getExtensionType() const {
    return static_cast<ISD::LoadExtType>(LoadSDNodeBits.ExtTy);
  }

  /// Return the base pointer operand.
  /// @return Base pointer SDValue.
  const SDValue &getBasePtr() const { return getOperand(1); }
  /// Return the offset operand.
  /// @return Offset SDValue.
  const SDValue &getOffset() const { return getOperand(2); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::LOAD;
  }
};

/// This class is used to represent ISD::STORE nodes.
class StoreSDNode : public LSBaseSDNode {
  friend class SelectionDAG;

  /// Construct a STORE node.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param AM Indexed addressing mode.
  /// @param isTrunc Whether the store truncates.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  StoreSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs,
              ISD::MemIndexedMode AM, bool isTrunc, EVT MemVT,
              MachineMemOperand *MMO)
      : LSBaseSDNode(ISD::STORE, Order, dl, VTs, AM, MemVT, MMO) {
    StoreSDNodeBits.IsTruncating = isTrunc;
    assert(!readMem() && "Store MachineMemOperand is a load!");
    assert(writeMem() && "Store MachineMemOperand is not a store!");
  }

public:
  /// Return true if the op truncates before storing.
  ///
  /// For integers this is the same as doing a TRUNCATE and storing the result.
  /// For floats, it is the same as doing an FP_ROUND and storing the result.
  /// @return True if the op does a truncation before store.
  bool isTruncatingStore() const { return StoreSDNodeBits.IsTruncating; }

  /// Return the value operand being stored.
  /// @return Stored value.
  const SDValue &getValue() const { return getOperand(1); }
  /// Return the base pointer operand.
  /// @return Base pointer SDValue.
  const SDValue &getBasePtr() const { return getOperand(2); }
  /// Return the offset operand.
  /// @return Offset SDValue.
  const SDValue &getOffset() const { return getOperand(3); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::STORE;
  }
};

/// This base class is used to represent VP_LOAD, VP_STORE,
/// EXPERIMENTAL_VP_STRIDED_LOAD and EXPERIMENTAL_VP_STRIDED_STORE nodes
class VPBaseLoadStoreSDNode : public MemSDNode {
public:
  friend class SelectionDAG;

  /// Construct a vector-predicated load/store base node.
  /// @param NodeTy VP load/store opcode.
  /// @param Order IR order.
  /// @param DL Debug location.
  /// @param VTs Result types.
  /// @param AM Indexed addressing mode.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  VPBaseLoadStoreSDNode(ISD::NodeType NodeTy, unsigned Order,
                        const DebugLoc &DL, SDVTList VTs,
                        ISD::MemIndexedMode AM, EVT MemVT,
                        MachineMemOperand *MMO)
      : MemSDNode(NodeTy, Order, DL, VTs, MemVT, MMO) {
    LSBaseSDNodeBits.AddressingMode = AM;
    assert(getAddressingMode() == AM && "Value truncated");
  }

  // VPStridedStoreSDNode (Chain, Data, Ptr,    Offset, Stride, Mask, EVL)
  // VPStoreSDNode        (Chain, Data, Ptr,    Offset, Mask,   EVL)
  // VPStridedLoadSDNode  (Chain, Ptr,  Offset, Stride, Mask,   EVL)
  // VPLoadSDNode         (Chain, Ptr,  Offset, Mask,   EVL)
  // Mask is a vector of i1 elements;
  // the type of EVL is TLI.getVPExplicitVectorLengthTy().
  /// Return the offset operand.
  /// @return Offset SDValue.
  const SDValue &getOffset() const {
    return getOperand((getOpcode() == ISD::EXPERIMENTAL_VP_STRIDED_LOAD ||
                       getOpcode() == ISD::VP_LOAD)
                          ? 2
                          : 3);
  }
  /// Return the base pointer operand.
  /// @return Base pointer SDValue.
  const SDValue &getBasePtr() const {
    return getOperand((getOpcode() == ISD::EXPERIMENTAL_VP_STRIDED_LOAD ||
                       getOpcode() == ISD::VP_LOAD)
                          ? 1
                          : 2);
  }
  /// Return the predicate mask operand.
  /// @return Mask SDValue.
  const SDValue &getMask() const {
    switch (getOpcode()) {
    default:
      llvm_unreachable("Invalid opcode");
    case ISD::VP_LOAD:
      return getOperand(3);
    case ISD::VP_STORE:
    case ISD::EXPERIMENTAL_VP_STRIDED_LOAD:
      return getOperand(4);
    case ISD::EXPERIMENTAL_VP_STRIDED_STORE:
      return getOperand(5);
    }
  }
  /// Return the explicit vector length operand.
  /// @return EVL SDValue.
  const SDValue &getVectorLength() const {
    switch (getOpcode()) {
    default:
      llvm_unreachable("Invalid opcode");
    case ISD::VP_LOAD:
      return getOperand(4);
    case ISD::VP_STORE:
    case ISD::EXPERIMENTAL_VP_STRIDED_LOAD:
      return getOperand(5);
    case ISD::EXPERIMENTAL_VP_STRIDED_STORE:
      return getOperand(6);
    }
  }

  /// Return the addressing mode for this load or store:
  /// unindexed, pre-inc, pre-dec, post-inc, or post-dec.
  /// @return The addressing mode for this load or store: unindexed, pre-inc, pre-dec, post-inc, or post-dec.
  ISD::MemIndexedMode getAddressingMode() const {
    return static_cast<ISD::MemIndexedMode>(LSBaseSDNodeBits.AddressingMode);
  }

  /// Return true if this is a pre/post inc/dec load/store.
  /// @return True if this is a pre/post inc/dec load/store.
  bool isIndexed() const { return getAddressingMode() != ISD::UNINDEXED; }

  /// Return true if this is NOT a pre/post inc/dec load/store.
  /// @return True if this is NOT a pre/post inc/dec load/store.
  bool isUnindexed() const { return getAddressingMode() == ISD::UNINDEXED; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::EXPERIMENTAL_VP_STRIDED_LOAD ||
           N->getOpcode() == ISD::EXPERIMENTAL_VP_STRIDED_STORE ||
           N->getOpcode() == ISD::VP_LOAD || N->getOpcode() == ISD::VP_STORE;
  }
};

/// This class is used to represent a VP_LOAD node.
class VPLoadSDNode : public VPBaseLoadStoreSDNode {
public:
  friend class SelectionDAG;

  /// Construct a VP_LOAD node.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param AM Indexed addressing mode.
  /// @param ETy Load extension type.
  /// @param isExpanding Whether this is an expanding load.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  VPLoadSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs,
               ISD::MemIndexedMode AM, ISD::LoadExtType ETy, bool isExpanding,
               EVT MemVT, MachineMemOperand *MMO)
      : VPBaseLoadStoreSDNode(ISD::VP_LOAD, Order, dl, VTs, AM, MemVT, MMO) {
    LoadSDNodeBits.ExtTy = ETy;
    LoadSDNodeBits.IsExpanding = isExpanding;
  }

  /// Return the load extension type.
  /// @return Load extension type.
  ISD::LoadExtType getExtensionType() const {
    return static_cast<ISD::LoadExtType>(LoadSDNodeBits.ExtTy);
  }

  /// Return the base pointer operand.
  /// @return Base pointer SDValue.
  const SDValue &getBasePtr() const { return getOperand(1); }
  /// Return the offset operand.
  /// @return Offset SDValue.
  const SDValue &getOffset() const { return getOperand(2); }
  /// Return the mask operand.
  /// @return Mask SDValue.
  const SDValue &getMask() const { return getOperand(3); }
  /// Return the explicit vector length operand.
  /// @return Vector length SDValue.
  const SDValue &getVectorLength() const { return getOperand(4); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::VP_LOAD;
  }
  /// Return true if this is an expanding load.
  /// @return True when expanding.
  bool isExpandingLoad() const { return LoadSDNodeBits.IsExpanding; }
};

/// This class is used to represent an EXPERIMENTAL_VP_STRIDED_LOAD node.
class VPStridedLoadSDNode : public VPBaseLoadStoreSDNode {
public:
  friend class SelectionDAG;

  /// Construct an EXPERIMENTAL_VP_STRIDED_LOAD node.
  /// @param Order IR order.
  /// @param DL Debug location.
  /// @param VTs Result types.
  /// @param AM Indexed addressing mode.
  /// @param ETy Load extension type.
  /// @param IsExpanding Whether this is an expanding load.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  VPStridedLoadSDNode(unsigned Order, const DebugLoc &DL, SDVTList VTs,
                      ISD::MemIndexedMode AM, ISD::LoadExtType ETy,
                      bool IsExpanding, EVT MemVT, MachineMemOperand *MMO)
      : VPBaseLoadStoreSDNode(ISD::EXPERIMENTAL_VP_STRIDED_LOAD, Order, DL, VTs,
                              AM, MemVT, MMO) {
    LoadSDNodeBits.ExtTy = ETy;
    LoadSDNodeBits.IsExpanding = IsExpanding;
  }

  /// Return the load extension type.
  /// @return Load extension type.
  ISD::LoadExtType getExtensionType() const {
    return static_cast<ISD::LoadExtType>(LoadSDNodeBits.ExtTy);
  }

  /// Return the base pointer operand.
  /// @return Base pointer SDValue.
  const SDValue &getBasePtr() const { return getOperand(1); }
  /// Return the offset operand.
  /// @return Offset SDValue.
  const SDValue &getOffset() const { return getOperand(2); }
  /// Return the stride operand.
  /// @return Stride SDValue.
  const SDValue &getStride() const { return getOperand(3); }
  /// Return the mask operand.
  /// @return Mask SDValue.
  const SDValue &getMask() const { return getOperand(4); }
  /// Return the explicit vector length operand.
  /// @return Vector length SDValue.
  const SDValue &getVectorLength() const { return getOperand(5); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::EXPERIMENTAL_VP_STRIDED_LOAD;
  }
  /// Return true if this is an expanding load.
  /// @return True when expanding.
  bool isExpandingLoad() const { return LoadSDNodeBits.IsExpanding; }
};

/// This class is used to represent a VP_STORE node
class VPStoreSDNode : public VPBaseLoadStoreSDNode {
public:
  friend class SelectionDAG;

  /// Construct a VP_STORE node.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param AM Indexed addressing mode.
  /// @param isTrunc Whether the store truncates.
  /// @param isCompressing Whether the store compresses active elements.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  VPStoreSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs,
                ISD::MemIndexedMode AM, bool isTrunc, bool isCompressing,
                EVT MemVT, MachineMemOperand *MMO)
      : VPBaseLoadStoreSDNode(ISD::VP_STORE, Order, dl, VTs, AM, MemVT, MMO) {
    StoreSDNodeBits.IsTruncating = isTrunc;
    StoreSDNodeBits.IsCompressing = isCompressing;
  }

  /// Return true if this is a truncating store.
  ///
  /// For integers this is the same as doing a TRUNCATE and storing the result.
  /// For floats, it is the same as doing an FP_ROUND and storing the result.
  /// @return True if this is a truncating store.
  bool isTruncatingStore() const { return StoreSDNodeBits.IsTruncating; }

  /// Return true if the store compresses active vector elements.
  ///
  /// The node contiguously stores the active elements (integers or floats)
  /// in src (those with their respective bit set in writemask k) to unaligned
  /// memory at base_addr.
  /// @return True if the op does a compression to the vector before storing.
  bool isCompressingStore() const { return StoreSDNodeBits.IsCompressing; }

  /// Return the value operand being stored.
  /// @return Stored value.
  const SDValue &getValue() const { return getOperand(1); }
  /// Return the base pointer operand.
  /// @return Base pointer SDValue.
  const SDValue &getBasePtr() const { return getOperand(2); }
  /// Return the offset operand.
  /// @return Offset SDValue.
  const SDValue &getOffset() const { return getOperand(3); }
  /// Return the mask operand.
  /// @return Mask SDValue.
  const SDValue &getMask() const { return getOperand(4); }
  /// Return the explicit vector length operand.
  /// @return Vector length SDValue.
  const SDValue &getVectorLength() const { return getOperand(5); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::VP_STORE;
  }
};

/// This class is used to represent an EXPERIMENTAL_VP_STRIDED_STORE node.
class VPStridedStoreSDNode : public VPBaseLoadStoreSDNode {
public:
  friend class SelectionDAG;

  /// Construct an EXPERIMENTAL_VP_STRIDED_STORE node.
  /// @param Order IR order.
  /// @param DL Debug location.
  /// @param VTs Result types.
  /// @param AM Indexed addressing mode.
  /// @param IsTrunc Whether the store truncates.
  /// @param IsCompressing Whether the store compresses active elements.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  VPStridedStoreSDNode(unsigned Order, const DebugLoc &DL, SDVTList VTs,
                       ISD::MemIndexedMode AM, bool IsTrunc, bool IsCompressing,
                       EVT MemVT, MachineMemOperand *MMO)
      : VPBaseLoadStoreSDNode(ISD::EXPERIMENTAL_VP_STRIDED_STORE, Order, DL,
                              VTs, AM, MemVT, MMO) {
    StoreSDNodeBits.IsTruncating = IsTrunc;
    StoreSDNodeBits.IsCompressing = IsCompressing;
  }

  /// Return true if this is a truncating store.
  ///
  /// For integers this is the same as doing a TRUNCATE and storing the result.
  /// For floats, it is the same as doing an FP_ROUND and storing the result.
  /// @return True if this is a truncating store.
  bool isTruncatingStore() const { return StoreSDNodeBits.IsTruncating; }

  /// Return true if the store compresses active vector elements.
  ///
  /// The node contiguously stores the active elements (integers or floats)
  /// in src (those with their respective bit set in writemask k) to unaligned
  /// memory at base_addr.
  /// @return True if the op does a compression to the vector before storing.
  bool isCompressingStore() const { return StoreSDNodeBits.IsCompressing; }

  /// Return the value operand being stored.
  /// @return Stored value.
  const SDValue &getValue() const { return getOperand(1); }
  /// Return the base pointer operand.
  /// @return Base pointer SDValue.
  const SDValue &getBasePtr() const { return getOperand(2); }
  /// Return the offset operand.
  /// @return Offset SDValue.
  const SDValue &getOffset() const { return getOperand(3); }
  /// Return the stride operand.
  /// @return Stride SDValue.
  const SDValue &getStride() const { return getOperand(4); }
  /// Return the mask operand.
  /// @return Mask SDValue.
  const SDValue &getMask() const { return getOperand(5); }
  /// Return the explicit vector length operand.
  /// @return Vector length SDValue.
  const SDValue &getVectorLength() const { return getOperand(6); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::EXPERIMENTAL_VP_STRIDED_STORE;
  }
};

/// This base class is used to represent MLOAD and MSTORE nodes
class MaskedLoadStoreSDNode : public MemSDNode {
public:
  friend class SelectionDAG;

  /// Construct a masked load/store base node.
  /// @param NodeTy MLOAD or MSTORE opcode.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param AM Indexed addressing mode.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  MaskedLoadStoreSDNode(ISD::NodeType NodeTy, unsigned Order,
                        const DebugLoc &dl, SDVTList VTs,
                        ISD::MemIndexedMode AM, EVT MemVT,
                        MachineMemOperand *MMO)
      : MemSDNode(NodeTy, Order, dl, VTs, MemVT, MMO) {
    LSBaseSDNodeBits.AddressingMode = AM;
    assert(getAddressingMode() == AM && "Value truncated");
  }

  // MaskedLoadSDNode (Chain, ptr, offset, mask, passthru)
  // MaskedStoreSDNode (Chain, data, ptr, offset, mask)
  // Mask is a vector of i1 elements
  /// Return the offset operand.
  /// @return Offset SDValue.
  const SDValue &getOffset() const {
    return getOperand(getOpcode() == ISD::MLOAD ? 2 : 3);
  }
  /// Return the mask operand.
  /// @return Mask SDValue.
  const SDValue &getMask() const {
    return getOperand(getOpcode() == ISD::MLOAD ? 3 : 4);
  }

  /// Return the addressing mode for this load or store:
  /// unindexed, pre-inc, pre-dec, post-inc, or post-dec.
  /// @return The addressing mode for this load or store: unindexed, pre-inc, pre-dec, post-inc, or post-dec.
  ISD::MemIndexedMode getAddressingMode() const {
    return static_cast<ISD::MemIndexedMode>(LSBaseSDNodeBits.AddressingMode);
  }

  /// Return true if this is a pre/post inc/dec load/store.
  /// @return True if this is a pre/post inc/dec load/store.
  bool isIndexed() const { return getAddressingMode() != ISD::UNINDEXED; }

  /// Return true if this is NOT a pre/post inc/dec load/store.
  /// @return True if this is NOT a pre/post inc/dec load/store.
  bool isUnindexed() const { return getAddressingMode() == ISD::UNINDEXED; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MLOAD ||
           N->getOpcode() == ISD::MSTORE;
  }
};

/// This class is used to represent an MLOAD node
class MaskedLoadSDNode : public MaskedLoadStoreSDNode {
public:
  friend class SelectionDAG;

  /// Construct an MLOAD node.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param AM Indexed addressing mode.
  /// @param ETy Load extension type.
  /// @param IsExpanding Whether this is an expanding load.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  MaskedLoadSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs,
                   ISD::MemIndexedMode AM, ISD::LoadExtType ETy,
                   bool IsExpanding, EVT MemVT, MachineMemOperand *MMO)
      : MaskedLoadStoreSDNode(ISD::MLOAD, Order, dl, VTs, AM, MemVT, MMO) {
    LoadSDNodeBits.ExtTy = ETy;
    LoadSDNodeBits.IsExpanding = IsExpanding;
  }

  /// Return the load extension type.
  /// @return Load extension type.
  ISD::LoadExtType getExtensionType() const {
    return static_cast<ISD::LoadExtType>(LoadSDNodeBits.ExtTy);
  }

  /// Return the base pointer operand.
  /// @return Base pointer SDValue.
  const SDValue &getBasePtr() const { return getOperand(1); }
  /// Return the offset operand.
  /// @return Offset SDValue.
  const SDValue &getOffset() const { return getOperand(2); }
  /// Return the mask operand.
  /// @return Mask SDValue.
  const SDValue &getMask() const { return getOperand(3); }
  /// Return the pass-through operand.
  /// @return Pass-through SDValue.
  const SDValue &getPassThru() const { return getOperand(4); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MLOAD;
  }

  /// Return true if this is an expanding load.
  /// @return True when expanding.
  bool isExpandingLoad() const { return LoadSDNodeBits.IsExpanding; }
};

/// This class is used to represent an MSTORE node
class MaskedStoreSDNode : public MaskedLoadStoreSDNode {
public:
  friend class SelectionDAG;

  /// Construct an MSTORE node.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param AM Indexed addressing mode.
  /// @param isTrunc Whether the store truncates.
  /// @param isCompressing Whether the store compresses active elements.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  MaskedStoreSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs,
                    ISD::MemIndexedMode AM, bool isTrunc, bool isCompressing,
                    EVT MemVT, MachineMemOperand *MMO)
      : MaskedLoadStoreSDNode(ISD::MSTORE, Order, dl, VTs, AM, MemVT, MMO) {
    StoreSDNodeBits.IsTruncating = isTrunc;
    StoreSDNodeBits.IsCompressing = isCompressing;
  }

  /// Return true if the op truncates before storing.
  ///
  /// For integers this is the same as doing a TRUNCATE and storing the result.
  /// For floats, it is the same as doing an FP_ROUND and storing the result.
  /// @return True if the op does a truncation before store.
  bool isTruncatingStore() const { return StoreSDNodeBits.IsTruncating; }

  /// Return true if the store compresses active vector elements.
  ///
  /// The node contiguously stores the active elements (integers or floats)
  /// in src (those with their respective bit set in writemask k) to unaligned
  /// memory at base_addr.
  /// @return True if the op does a compression to the vector before storing.
  bool isCompressingStore() const { return StoreSDNodeBits.IsCompressing; }

  /// Return the value operand being stored.
  /// @return Stored value.
  const SDValue &getValue() const { return getOperand(1); }
  /// Return the base pointer operand.
  /// @return Base pointer SDValue.
  const SDValue &getBasePtr() const { return getOperand(2); }
  /// Return the offset operand.
  /// @return Offset SDValue.
  const SDValue &getOffset() const { return getOperand(3); }
  /// Return the mask operand.
  /// @return Mask SDValue.
  const SDValue &getMask() const { return getOperand(4); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MSTORE;
  }
};

/// This is a base class used to represent
/// VP_GATHER and VP_SCATTER nodes
///
class VPGatherScatterSDNode : public MemSDNode {
public:
  friend class SelectionDAG;

  /// Construct a VP gather/scatter base node.
  /// @param NodeTy VP_GATHER or VP_SCATTER opcode.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  /// @param IndexType How indices apply to the base pointer.
  VPGatherScatterSDNode(ISD::NodeType NodeTy, unsigned Order,
                        const DebugLoc &dl, SDVTList VTs, EVT MemVT,
                        MachineMemOperand *MMO, ISD::MemIndexType IndexType)
      : MemSDNode(NodeTy, Order, dl, VTs, MemVT, MMO) {
    LSBaseSDNodeBits.AddressingMode = IndexType;
    assert(getIndexType() == IndexType && "Value truncated");
  }

  /// How is Index applied to BasePtr when computing addresses.
  /// @return How Index is applied to BasePtr when computing addresses.
  ISD::MemIndexType getIndexType() const {
    return static_cast<ISD::MemIndexType>(LSBaseSDNodeBits.AddressingMode);
  }
  /// Return true if the index scale is not one.
  /// @return True when scaled addressing is used.
  bool isIndexScaled() const {
    return !cast<ConstantSDNode>(getScale())->isOne();
  }
  /// Return true if the index type is signed.
  /// @return True for signed index types.
  bool isIndexSigned() const { return isIndexTypeSigned(getIndexType()); }

  // In the both nodes address is Op1, mask is Op2:
  // VPGatherSDNode  (Chain, base, index, scale, mask, vlen)
  // VPScatterSDNode (Chain, value, base, index, scale, mask, vlen)
  // Mask is a vector of i1 elements
  /// Return the base pointer operand.
  /// @return Base pointer SDValue.
  const SDValue &getBasePtr() const {
    return getOperand((getOpcode() == ISD::VP_GATHER) ? 1 : 2);
  }
  /// Return the index operand.
  /// @return Index SDValue.
  const SDValue &getIndex() const {
    return getOperand((getOpcode() == ISD::VP_GATHER) ? 2 : 3);
  }
  /// Return the scale operand.
  /// @return Scale SDValue.
  const SDValue &getScale() const {
    return getOperand((getOpcode() == ISD::VP_GATHER) ? 3 : 4);
  }
  /// Return the mask operand.
  /// @return Mask SDValue.
  const SDValue &getMask() const {
    return getOperand((getOpcode() == ISD::VP_GATHER) ? 4 : 5);
  }
  /// Return the explicit vector length operand.
  /// @return Vector length SDValue.
  const SDValue &getVectorLength() const {
    return getOperand((getOpcode() == ISD::VP_GATHER) ? 5 : 6);
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::VP_GATHER ||
           N->getOpcode() == ISD::VP_SCATTER;
  }
};

/// This class is used to represent an VP_GATHER node
///
class VPGatherSDNode : public VPGatherScatterSDNode {
public:
  friend class SelectionDAG;

  /// Construct a VP_GATHER node.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  /// @param IndexType How indices apply to the base pointer.
  VPGatherSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs, EVT MemVT,
                 MachineMemOperand *MMO, ISD::MemIndexType IndexType)
      : VPGatherScatterSDNode(ISD::VP_GATHER, Order, dl, VTs, MemVT, MMO,
                              IndexType) {}

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::VP_GATHER;
  }
};

/// This class is used to represent an VP_SCATTER node
///
class VPScatterSDNode : public VPGatherScatterSDNode {
public:
  friend class SelectionDAG;

  /// Construct a VP_SCATTER node.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  /// @param IndexType How indices apply to the base pointer.
  VPScatterSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs, EVT MemVT,
                  MachineMemOperand *MMO, ISD::MemIndexType IndexType)
      : VPGatherScatterSDNode(ISD::VP_SCATTER, Order, dl, VTs, MemVT, MMO,
                              IndexType) {}

  /// Return the value operand being scattered.
  /// @return Value SDValue.
  const SDValue &getValue() const { return getOperand(1); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::VP_SCATTER;
  }
};

/// This is a base class used to represent
/// MGATHER and MSCATTER nodes
///
class MaskedGatherScatterSDNode : public MemSDNode {
public:
  friend class SelectionDAG;

  /// Construct a masked gather/scatter base node.
  /// @param NodeTy MGATHER, MSCATTER, or histogram opcode.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  /// @param IndexType How indices apply to the base pointer.
  MaskedGatherScatterSDNode(ISD::NodeType NodeTy, unsigned Order,
                            const DebugLoc &dl, SDVTList VTs, EVT MemVT,
                            MachineMemOperand *MMO, ISD::MemIndexType IndexType)
      : MemSDNode(NodeTy, Order, dl, VTs, MemVT, MMO) {
    LSBaseSDNodeBits.AddressingMode = IndexType;
    assert(getIndexType() == IndexType && "Value truncated");
  }

  /// How is Index applied to BasePtr when computing addresses.
  /// @return How Index is applied to BasePtr when computing addresses.
  ISD::MemIndexType getIndexType() const {
    return static_cast<ISD::MemIndexType>(LSBaseSDNodeBits.AddressingMode);
  }
  /// Return true if the index scale is not one.
  /// @return True when scaled addressing is used.
  bool isIndexScaled() const {
    return !cast<ConstantSDNode>(getScale())->isOne();
  }
  /// Return true if the index type is signed.
  /// @return True for signed index types.
  bool isIndexSigned() const { return isIndexTypeSigned(getIndexType()); }

  // In the both nodes address is Op1, mask is Op2:
  // MaskedGatherSDNode  (Chain, passthru, mask, base, index, scale)
  // MaskedScatterSDNode (Chain, value, mask, base, index, scale)
  // Mask is a vector of i1 elements
  /// Return the base pointer operand.
  /// @return Base pointer SDValue.
  const SDValue &getBasePtr() const { return getOperand(3); }
  /// Return the index operand.
  /// @return Index SDValue.
  const SDValue &getIndex()   const { return getOperand(4); }
  /// Return the mask operand.
  /// @return Mask SDValue.
  const SDValue &getMask()    const { return getOperand(2); }
  /// Return the scale operand.
  /// @return Scale SDValue.
  const SDValue &getScale()   const { return getOperand(5); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MGATHER || N->getOpcode() == ISD::MSCATTER ||
           N->getOpcode() == ISD::EXPERIMENTAL_VECTOR_HISTOGRAM;
  }
};

/// This class is used to represent an MGATHER node
///
class MaskedGatherSDNode : public MaskedGatherScatterSDNode {
public:
  friend class SelectionDAG;

  /// Construct an MGATHER node.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  /// @param IndexType How indices apply to the base pointer.
  /// @param ETy Load extension type.
  MaskedGatherSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs,
                     EVT MemVT, MachineMemOperand *MMO,
                     ISD::MemIndexType IndexType, ISD::LoadExtType ETy)
      : MaskedGatherScatterSDNode(ISD::MGATHER, Order, dl, VTs, MemVT, MMO,
                                  IndexType) {
    LoadSDNodeBits.ExtTy = ETy;
  }

  /// Return the pass-through operand.
  /// @return Pass-through SDValue.
  const SDValue &getPassThru() const { return getOperand(1); }

  /// Return the load extension type.
  /// @return Load extension type.
  ISD::LoadExtType getExtensionType() const {
    return ISD::LoadExtType(LoadSDNodeBits.ExtTy);
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MGATHER;
  }
};

/// This class is used to represent an MSCATTER node
///
class MaskedScatterSDNode : public MaskedGatherScatterSDNode {
public:
  friend class SelectionDAG;

  /// Construct an MSCATTER node.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  /// @param IndexType How indices apply to the base pointer.
  /// @param IsTrunc Whether the store truncates.
  MaskedScatterSDNode(unsigned Order, const DebugLoc &dl, SDVTList VTs,
                      EVT MemVT, MachineMemOperand *MMO,
                      ISD::MemIndexType IndexType, bool IsTrunc)
      : MaskedGatherScatterSDNode(ISD::MSCATTER, Order, dl, VTs, MemVT, MMO,
                                  IndexType) {
    StoreSDNodeBits.IsTruncating = IsTrunc;
  }

  /// Return true if the op truncates before storing.
  ///
  /// For integers this is the same as doing a TRUNCATE and storing the result.
  /// For floats, it is the same as doing an FP_ROUND and storing the result.
  /// @return True if the op does a truncation before store.
  bool isTruncatingStore() const { return StoreSDNodeBits.IsTruncating; }

  /// Return the value operand being scattered.
  /// @return Value SDValue.
  const SDValue &getValue() const { return getOperand(1); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::MSCATTER;
  }
};

/// SDNode for experimental vector histogram operations.
class MaskedHistogramSDNode : public MaskedGatherScatterSDNode {
public:
  friend class SelectionDAG;

  /// Construct a masked histogram node.
  /// @param Order IR order.
  /// @param DL Debug location.
  /// @param VTs Result types.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  /// @param IndexType How indices apply to the base pointer.
  MaskedHistogramSDNode(unsigned Order, const DebugLoc &DL, SDVTList VTs,
                        EVT MemVT, MachineMemOperand *MMO,
                        ISD::MemIndexType IndexType)
      : MaskedGatherScatterSDNode(ISD::EXPERIMENTAL_VECTOR_HISTOGRAM, Order, DL,
                                  VTs, MemVT, MMO, IndexType) {}

  /// Return how indices apply to the base pointer.
  /// @return Memory index type.
  ISD::MemIndexType getIndexType() const {
    return static_cast<ISD::MemIndexType>(LSBaseSDNodeBits.AddressingMode);
  }

  /// Return the base pointer operand.
  /// @return Base pointer SDValue.
  const SDValue &getBasePtr() const { return getOperand(3); }
  /// Return the index operand.
  /// @return Index SDValue.
  const SDValue &getIndex() const { return getOperand(4); }
  /// Return the mask operand.
  /// @return Mask SDValue.
  const SDValue &getMask() const { return getOperand(2); }
  /// Return the scale operand.
  /// @return Scale SDValue.
  const SDValue &getScale() const { return getOperand(5); }
  /// Return the increment operand.
  /// @return Increment SDValue.
  const SDValue &getInc() const { return getOperand(1); }
  /// Return the intrinsic id operand.
  /// @return Intrinsic id SDValue.
  const SDValue &getIntID() const { return getOperand(6); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::EXPERIMENTAL_VECTOR_HISTOGRAM;
  }
};

/// SDNode for VP_LOAD_FF (vector-predicated fault-only-first load).
class VPLoadFFSDNode : public MemSDNode {
public:
  friend class SelectionDAG;

  /// Construct a VP_LOAD_FF node.
  /// @param Order IR order.
  /// @param DL Debug location.
  /// @param VTs Result types.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  VPLoadFFSDNode(unsigned Order, const DebugLoc &DL, SDVTList VTs, EVT MemVT,
                 MachineMemOperand *MMO)
      : MemSDNode(ISD::VP_LOAD_FF, Order, DL, VTs, MemVT, MMO) {}

  /// Return the base pointer operand.
  /// @return Base pointer SDValue.
  const SDValue &getBasePtr() const { return getOperand(1); }
  /// Return the mask operand.
  /// @return Mask SDValue.
  const SDValue &getMask() const { return getOperand(2); }
  /// Return the explicit vector length operand.
  /// @return EVL SDValue.
  const SDValue &getVectorLength() const { return getOperand(3); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::VP_LOAD_FF;
  }
};

/// SDNode for get/set FP environment memory accesses.
class FPStateAccessSDNode : public MemSDNode {
public:
  friend class SelectionDAG;

  /// Construct an FP environment memory access node.
  /// @param NodeTy GET_FPENV_MEM or SET_FPENV_MEM.
  /// @param Order IR order.
  /// @param dl Debug location.
  /// @param VTs Result types.
  /// @param MemVT In-memory value type.
  /// @param MMO Memory operand.
  FPStateAccessSDNode(unsigned NodeTy, unsigned Order, const DebugLoc &dl,
                      SDVTList VTs, EVT MemVT, MachineMemOperand *MMO)
      : MemSDNode(NodeTy, Order, dl, VTs, MemVT, MMO) {
    assert((NodeTy == ISD::GET_FPENV_MEM || NodeTy == ISD::SET_FPENV_MEM) &&
           "Expected FP state access node");
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::GET_FPENV_MEM ||
           N->getOpcode() == ISD::SET_FPENV_MEM;
  }
};

/// An SDNode that represents everything that will be needed
/// to construct a MachineInstr. These nodes are created during the
/// instruction selection proper phase.
///
/// Note that the only supported way to set the `memoperands` is by calling the
/// `SelectionDAG::setNodeMemRefs` function as the memory management happens
/// inside the DAG rather than in the node.
class MachineSDNode : public SDNode {
private:
  friend class SelectionDAG;

  /// Construct a MachineSDNode.
  /// @param Opc Machine opcode (negative NodeType encoding).
  /// @param Order IR order.
  /// @param DL Debug location.
  /// @param VTs Result types.
  MachineSDNode(unsigned Opc, unsigned Order, const DebugLoc &DL, SDVTList VTs)
      : SDNode(Opc, Order, DL, VTs) {}

  // We use a pointer union between a single `MachineMemOperand` pointer and
  // a pointer to an array of `MachineMemOperand` pointers. This is null when
  // the number of these is zero, the single pointer variant used when the
  // number is one, and the array is used for larger numbers.
  //
  // The array is allocated via the `SelectionDAG`'s allocator and so will
  // always live until the DAG is cleaned up and doesn't require ownership here.
  //
  // We can't use something simpler like `TinyPtrVector` here because `SDNode`
  // subclasses aren't managed in a conforming C++ manner. See the comments on
  // `SelectionDAG::MorphNodeTo` which details what all goes on, but the
  // constraint here is that these don't manage memory with their constructor or
  // destructor and can be initialized to a good state even if they start off
  // uninitialized.
  PointerUnion<MachineMemOperand *, MachineMemOperand **> MemRefs = {};

  // Note that this could be folded into the above `MemRefs` member if doing so
  // is advantageous at some point. We don't need to store this in most cases.
  // However, at the moment this doesn't appear to make the allocation any
  // smaller and makes the code somewhat simpler to read.
  int NumMemRefs = 0;

public:
  /// Iterator over machine memory operands.
  using mmo_iterator = ArrayRef<MachineMemOperand *>::const_iterator;

  /// Return this node's machine memory operands.
  /// @return Array of MachineMemOperand pointers.
  ArrayRef<MachineMemOperand *> memoperands() const {
    // Special case the common cases.
    if (NumMemRefs == 0)
      return {};
    if (NumMemRefs == 1)
      return ArrayRef(MemRefs.getAddrOfPtr1(), 1);

    // Otherwise we have an actual array.
    return ArrayRef(cast<MachineMemOperand **>(MemRefs), NumMemRefs);
  }
  /// Return iterator to the first memory operand.
  /// @return Begin iterator.
  mmo_iterator memoperands_begin() const { return memoperands().begin(); }
  /// Return iterator past the last memory operand.
  /// @return End iterator.
  mmo_iterator memoperands_end() const { return memoperands().end(); }
  /// Return true if there are no memory operands.
  /// @return True when the MMO list is empty.
  bool memoperands_empty() const { return memoperands().empty(); }

  /// Clear out the memory reference descriptor list.
  void clearMemRefs() {
    MemRefs = nullptr;
    NumMemRefs = 0;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->isMachineOpcode();
  }
};

/// SDNode asserting that a value is aligned to a given alignment.
class AssertAlignSDNode : public SDNode {
  /// Guaranteed alignment.
  Align Alignment;

public:
  /// Construct an AssertAlign node.
  /// @param Order IR order.
  /// @param DL Debug location.
  /// @param VTs Result types.
  /// @param A Asserted alignment.
  AssertAlignSDNode(unsigned Order, const DebugLoc &DL, SDVTList VTs, Align A)
      : SDNode(ISD::AssertAlign, Order, DL, VTs), Alignment(A) {}

  /// Return the asserted alignment.
  /// @return Alignment value.
  Align getAlign() const { return Alignment; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// @param N Node to test.
  /// @return True if \p N is of this class.
  static bool classof(const SDNode *N) {
    return N->getOpcode() == ISD::AssertAlign;
  }
};

/// Forward iterator over the operand SDNodes of an SDNode.
class SDNodeIterator {
  /// Node whose operands are iterated.
  const SDNode *Node;
  /// Current operand index.
  unsigned Operand;

  /// Construct an iterator at operand \\p Op of \\p N.
  /// @param N Node whose operands are walked.
  /// @param Op Starting operand index.
  SDNodeIterator(const SDNode *N, unsigned Op) : Node(N), Operand(Op) {}

public:
  /// Iterator category tag.
  using iterator_category = std::forward_iterator_tag;
  /// Value type of the iterator.
  using value_type = SDNode;
  /// Difference type of the iterator.
  using difference_type = std::ptrdiff_t;
  /// Pointer type of the iterator.
  using pointer = value_type *;
  /// Reference type of the iterator.
  using reference = value_type &;

  /// Return true if this iterator equals \\p x.
  /// @param x Other iterator.
  /// @return True when operand indices match.
  bool operator==(const SDNodeIterator& x) const {
    return Operand == x.Operand;
  }
  /// Return true if this iterator differs from \\p x.
  /// @param x Other iterator.
  /// @return True when the iterators differ.
  bool operator!=(const SDNodeIterator& x) const { return !operator==(x); }

  /// Return the current operand SDNode.
  /// @return Pointer to the operand node.
  pointer operator*() const {
    return Node->getOperand(Operand).getNode();
  }
  /// Return the current operand SDNode.
  /// @return Pointer to the operand node.
  pointer operator->() const { return operator*(); }

  /// Advance to the next operand (preincrement).
  /// @return Reference to this iterator.
  SDNodeIterator& operator++() {                // Preincrement
    ++Operand;
    return *this;
  }
  /// Advance to the next operand (postincrement).
  /// @param Unused Unused postfix-discriminator parameter.
  /// @return Copy of the prior iterator.
  SDNodeIterator operator++(int Unused) { // Postincrement
    SDNodeIterator tmp = *this; ++*this; return tmp;
  }
  /// Return the distance to iterator \\p Other.
  /// @param Other Iterator into the same node.
  /// @return Operand index difference.
  size_t operator-(SDNodeIterator Other) const {
    assert(Node == Other.Node &&
           "Cannot compare iterators of two different nodes!");
    return Operand - Other.Operand;
  }

  /// Return a begin iterator for operands of \\p N.
  /// @param N Node to iterate.
  /// @return Begin iterator.
  static SDNodeIterator begin(const SDNode *N) { return SDNodeIterator(N, 0); }
  /// Return an end iterator for operands of \\p N.
  /// @param N Node to iterate.
  /// @return End iterator.
  static SDNodeIterator end  (const SDNode *N) {
    return SDNodeIterator(N, N->getNumOperands());
  }

  /// Return the current operand index.
  /// @return Operand index.
  unsigned getOperand() const { return Operand; }
  /// Return the node being iterated.
  /// @return SDNode pointer.
  const SDNode *getNode() const { return Node; }
};

/// GraphTraits specialization treating SDNode operands as children.
template <> struct GraphTraits<SDNode*> {
  /// Graph node reference type.
  using NodeRef = SDNode *;
  /// Child iterator type over operand nodes.
  using ChildIteratorType = SDNodeIterator;

  /// Return the entry node \\p N.
  /// @param N Graph entry.
  /// @return Entry node.
  static NodeRef getEntryNode(SDNode *N) { return N; }

  /// Return the begin child iterator for \\p N.
  /// @param N Parent node.
  /// @return Begin operand iterator.
  static ChildIteratorType child_begin(NodeRef N) {
    return SDNodeIterator::begin(N);
  }

  /// Return the end child iterator for \\p N.
  /// @param N Parent node.
  /// @return End operand iterator.
  static ChildIteratorType child_end(NodeRef N) {
    return SDNodeIterator::end(N);
  }
};

/// A representation of the largest SDNode, for use in sizeof().
///
/// This needs to be a union because the largest node differs on 32 bit systems
/// with 4 and 8 byte pointer alignment, respectively.
using LargestSDNode = AlignedCharArrayUnion<AtomicSDNode, TargetIndexSDNode,
                                            BlockAddressSDNode,
                                            GlobalAddressSDNode,
                                            PseudoProbeSDNode>;

/// The SDNode class with the greatest alignment requirement.
using MostAlignedSDNode = GlobalAddressSDNode;

namespace ISD {

  /// Returns true if the specified node is a non-extending and unindexed load.
  /// @param N Node to test.
  /// @return True if the specified node is a non-extending and unindexed load.
  inline bool isNormalLoad(const SDNode *N) {
    auto *Ld = dyn_cast<LoadSDNode>(N);
    return Ld && Ld->getExtensionType() == ISD::NON_EXTLOAD &&
           Ld->getAddressingMode() == ISD::UNINDEXED;
  }

  /// Returns true if the specified node is a non-extending load.
  /// @param N Node to test.
  /// @return True if the specified node is a non-extending load.
  inline bool isNON_EXTLoad(const SDNode *N) {
    auto *Ld = dyn_cast<LoadSDNode>(N);
    return Ld && Ld->getExtensionType() == ISD::NON_EXTLOAD;
  }

  /// Returns true if the specified node is a EXTLOAD.
  /// @param N Node to test.
  /// @return True if the specified node is a EXTLOAD.
  inline bool isEXTLoad(const SDNode *N) {
    auto *Ld = dyn_cast<LoadSDNode>(N);
    return Ld && Ld->getExtensionType() == ISD::EXTLOAD;
  }

  /// Returns true if the specified node is a SEXTLOAD.
  /// @param N Node to test.
  /// @return True if the specified node is a SEXTLOAD.
  inline bool isSEXTLoad(const SDNode *N) {
    auto *Ld = dyn_cast<LoadSDNode>(N);
    return Ld && Ld->getExtensionType() == ISD::SEXTLOAD;
  }

  /// Returns true if the specified node is a ZEXTLOAD.
  /// @param N Node to test.
  /// @return True if the specified node is a ZEXTLOAD.
  inline bool isZEXTLoad(const SDNode *N) {
    auto *Ld = dyn_cast<LoadSDNode>(N);
    return Ld && Ld->getExtensionType() == ISD::ZEXTLOAD;
  }

  /// Returns true if the specified node is an unindexed load.
  /// @param N Node to test.
  /// @return True if the specified node is an unindexed load.
  inline bool isUNINDEXEDLoad(const SDNode *N) {
    auto *Ld = dyn_cast<LoadSDNode>(N);
    return Ld && Ld->getAddressingMode() == ISD::UNINDEXED;
  }

  /// Returns true if the specified node is a non-truncating unindexed store.
  /// @param N Node to test.
  /// @return True if the specified node is a non-truncating and unindexed store.
  inline bool isNormalStore(const SDNode *N) {
    auto *St = dyn_cast<StoreSDNode>(N);
    return St && !St->isTruncatingStore() &&
           St->getAddressingMode() == ISD::UNINDEXED;
  }

  /// Returns true if the specified node is an unindexed store.
  /// @param N Node to test.
  /// @return True if the specified node is an unindexed store.
  inline bool isUNINDEXEDStore(const SDNode *N) {
    auto *St = dyn_cast<StoreSDNode>(N);
    return St && St->getAddressingMode() == ISD::UNINDEXED;
  }

  /// Returns true if \p N is a non-extending unindexed masked load.
  /// @param N Node to test.
  /// @return True if the specified node is a non-extending and unindexed masked load.
  inline bool isNormalMaskedLoad(const SDNode *N) {
    auto *Ld = dyn_cast<MaskedLoadSDNode>(N);
    return Ld && Ld->getExtensionType() == ISD::NON_EXTLOAD &&
           Ld->getAddressingMode() == ISD::UNINDEXED;
  }

  /// Returns true if \p N is a non-truncating unindexed masked store.
  /// @param N Node to test.
  /// @return True if the specified node is a non-extending and unindexed masked store.
  inline bool isNormalMaskedStore(const SDNode *N) {
    auto *St = dyn_cast<MaskedStoreSDNode>(N);
    return St && !St->isTruncatingStore() &&
           St->getAddressingMode() == ISD::UNINDEXED;
  }

  /// Match a unary predicate against scalar/splat/BUILD_VECTOR constants.
  ///
  /// The DemandedElts argument allows us to only collect the known bits that
  /// are shared by the requested vector elements. If AllowUndef is true, then
  /// UNDEF elements will pass nullptr to Match.
  /// @param Op Value to match against.
  /// @param DemandedElts Elements that must satisfy the predicate.
  /// @param Match Predicate invoked for each constant element.
  /// @param AllowUndefs Whether undef elements are allowed.
  /// @param AllowTruncation Whether build-vector truncation is allowed.
  /// @return True if the unary predicate matched.
  template <typename ConstNodeType>
  bool matchUnaryPredicateImpl(SDValue Op, const APInt &DemandedElts,
                               std::function<bool(ConstNodeType *)> Match,
                               bool AllowUndefs = false,
                               bool AllowTruncation = false);

  /// Match a ConstantSDNode unary predicate with demanded elements.
  /// @param Op Value to match against.
  /// @param DemandedElts Elements that must satisfy the predicate.
  /// @param Match Predicate invoked for each constant element.
  /// @param AllowUndefs Whether undef elements are allowed.
  /// @param AllowTruncation Whether build-vector truncation is allowed.
  /// @return True if the ConstantSDNode predicate matched.
  inline bool matchUnaryPredicate(SDValue Op, const APInt &DemandedElts,
                                  std::function<bool(ConstantSDNode *)> Match,
                                  bool AllowUndefs = false,
                                  bool AllowTruncation = false) {
    return matchUnaryPredicateImpl<ConstantSDNode>(
        Op, DemandedElts, Match, AllowUndefs, AllowTruncation);
  }

  /// Match a ConstantSDNode unary predicate over all elements.
  /// @param Op Value to match against.
  /// @param Match Predicate invoked for each constant element.
  /// @param AllowUndefs Whether undef elements are allowed.
  /// @param AllowTruncation Whether build-vector truncation is allowed.
  /// @return True if the ConstantSDNode predicate matched.
  inline bool matchUnaryPredicate(SDValue Op,
                                  std::function<bool(ConstantSDNode *)> Match,
                                  bool AllowUndefs = false,
                                  bool AllowTruncation = false) {
    EVT VT = Op.getValueType();
    APInt DemandedElts = VT.isFixedLengthVector()
                             ? APInt::getAllOnes(VT.getVectorNumElements())
                             : APInt(1, 1);
    return matchUnaryPredicate(Op, DemandedElts, Match, AllowUndefs,
                               AllowTruncation);
  }

  /// Match a ConstantFPSDNode unary predicate with demanded elements.
  /// @param Op Value to match against.
  /// @param DemandedElts Elements that must satisfy the predicate.
  /// @param Match Predicate invoked for each constant element.
  /// @param AllowUndefs Whether undef elements are allowed.
  /// @return True if the ConstantFPSDNode predicate matched.
  inline bool
  matchUnaryFpPredicate(SDValue Op, const APInt &DemandedElts,
                        std::function<bool(ConstantFPSDNode *)> Match,
                        bool AllowUndefs = false) {
    return matchUnaryPredicateImpl<ConstantFPSDNode>(Op, DemandedElts, Match,
                                                     AllowUndefs);
  }

  /// Match a ConstantFPSDNode unary predicate over all elements.
  /// @param Op Value to match against.
  /// @param Match Predicate invoked for each constant element.
  /// @param AllowUndefs Whether undef elements are allowed.
  /// @return True if the ConstantFPSDNode predicate matched.
  inline bool
  matchUnaryFpPredicate(SDValue Op,
                        std::function<bool(ConstantFPSDNode *)> Match,
                        bool AllowUndefs = false) {
    EVT VT = Op.getValueType();
    APInt DemandedElts = VT.isFixedLengthVector()
                             ? APInt::getAllOnes(VT.getVectorNumElements())
                             : APInt(1, 1);
    return matchUnaryFpPredicate(Op, DemandedElts, Match, AllowUndefs);
  }

  /// Match a binary predicate against paired scalar/splat/BUILD_VECTOR constants.
  ///
  /// The DemandedElts argument allows us to only collect the known bits that
  /// are shared by the requested vector elements. If AllowUndef is true, then
  /// UNDEF elements will pass nullptr to Match. If AllowTypeMismatch is true
  /// then RetType + ArgTypes don't need to match.
  /// @param LHS Left-hand value.
  /// @param RHS Right-hand value.
  /// @param DemandedElts Elements that must satisfy the predicate.
  /// @param Match Predicate invoked for each constant pair.
  /// @param AllowUndefs Whether undef elements are allowed.
  /// @param AllowTypeMismatch Whether operand types may differ.
  /// @return True if the binary predicate matched.
  LLVM_ABI bool matchBinaryPredicate(
      SDValue LHS, SDValue RHS, const APInt &DemandedElts,
      std::function<bool(ConstantSDNode *, ConstantSDNode *)> Match,
      bool AllowUndefs = false, bool AllowTypeMismatch = false);

  /// Match a binary predicate over all elements of \p LHS and \p RHS.
  /// @param LHS Left-hand value.
  /// @param RHS Right-hand value.
  /// @param Match Predicate invoked for each constant pair.
  /// @param AllowUndefs Whether undef elements are allowed.
  /// @param AllowTypeMismatch Whether operand types may differ.
  /// @return True if the binary predicate matched.
  inline bool matchBinaryPredicate(
      SDValue LHS, SDValue RHS,
      std::function<bool(ConstantSDNode *, ConstantSDNode *)> Match,
      bool AllowUndefs = false, bool AllowTypeMismatch = false) {
    EVT VT = LHS.getValueType();
    APInt DemandedElts = VT.isFixedLengthVector()
                             ? APInt::getAllOnes(VT.getVectorNumElements())
                             : APInt(1, 1);
    return matchBinaryPredicate(LHS, RHS, DemandedElts, Match, AllowUndefs,
                                AllowTypeMismatch);
  }

  /// Return true if \p Op is an overflow result of an overflow intrinsic.
  /// @param Op Value to test.
  /// @return True if the specified value is the overflow result from one of the overflow intrinsic nodes.
  inline bool isOverflowIntrOpRes(SDValue Op) {
    unsigned Opc = Op.getOpcode();
    return (Op.getResNo() == 1 &&
            (Opc == ISD::SADDO || Opc == ISD::UADDO || Opc == ISD::SSUBO ||
             Opc == ISD::USUBO || Opc == ISD::SMULO || Opc == ISD::UMULO));
  }

} // end namespace ISD

} // end namespace llvm

#endif // LLVM_CODEGEN_SELECTIONDAGNODES_H
