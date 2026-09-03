//===- llvm/CodeGen/SelectionDAG.h - InstSelection DAG ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SelectionDAG class, and transitively defines the
// SDNode class and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SELECTIONDAG_H
#define LLVM_CODEGEN_SELECTIONDAG_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/DAGCombine.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/RuntimeLibcalls.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ArrayRecycler.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownFPClass.h"
#include "llvm/Support/RecyclingAllocator.h"
#include "llvm/Support/UndefPoison.h"
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace llvm {

class DIExpression;
class DILabel;
class DIVariable;
class Function;
class Pass;
class Type;
template <class GraphType> struct GraphTraits;
template <typename T, unsigned int N> class SmallSetVector;
template <typename T, typename Enable> struct FoldingSetTrait;
class BatchAAResults;
class BlockAddress;
class BlockFrequencyInfo;
class Constant;
class ConstantFP;
class ConstantInt;
class DataLayout;
struct fltSemantics;
class FunctionLoweringInfo;
class FunctionVarLocs;
class GlobalValue;
struct KnownBits;
class LLVMContext;
class MachineBasicBlock;
class MachineConstantPoolValue;
class MachineModuleInfo;
class MCSymbol;
class OptimizationRemarkEmitter;
class ProfileSummaryInfo;
/// Debug-value descriptor kept off to the side of the DAG.
class SDDbgValue;
/// One location operand of an SDDbgValue.
class SDDbgOperand;
/// Debug-label descriptor kept off to the side of the DAG.
class SDDbgLabel;
class SelectionDAG;
class SelectionDAGTargetInfo;
class TargetLibraryInfo;
class TargetLowering;
class TargetMachine;
class TargetSubtargetInfo;
class Value;

template <typename T> class GenericSSAContext;
using SSAContext = GenericSSAContext<Function>;
template <typename T> class GenericUniformityInfo;
using UniformityInfo = GenericUniformityInfo<SSAContext>;

/// Interned list of EVTs used as a SelectionDAG node's result types.
class SDVTListNode : public FoldingSetNode {
  friend struct FoldingSetTrait<SDVTListNode>;

  /// A reference to an Interned FoldingSetNodeID for this node.
  /// The Allocator in SelectionDAG holds the data.
  /// SDVTList contains all types which are frequently accessed in SelectionDAG.
  /// The size of this list is not expected to be big so it won't introduce
  /// a memory penalty.
  FoldingSetNodeIDRef FastID;
  const EVT *VTs;
  unsigned int NumVTs;
  /// The hash value for SDVTList is fixed, so cache it to avoid
  /// hash calculation.
  unsigned HashValue;

public:
  /// Construct an interned VT list node.
  ///
  /// \param ID Interned folding-set identifier.
  /// \param VT Array of interned value types.
  /// \param Num Number of value types in \p VT.
  SDVTListNode(const FoldingSetNodeIDRef ID, const EVT *VT, unsigned int Num) :
      FastID(ID), VTs(VT), NumVTs(Num) {
    HashValue = ID.ComputeHash();
  }

  /// Return the interned SDVTList represented by this node.
  ///
  /// \return The interned SDVTList represented by this node.
  SDVTList getSDVTList() {
    SDVTList result = {VTs, NumVTs};
    return result;
  }
};

/// Specialize FoldingSetTrait for SDVTListNode
/// to avoid computing temp FoldingSetNodeID and hash value.
template<> struct FoldingSetTrait<SDVTListNode> : DefaultFoldingSetTrait<SDVTListNode> {
  /// Copy the interned identifier of \p X into \p ID.
  ///
  /// \param X Node whose cached identifier is copied.
  /// \param ID Destination folding-set identifier.
  static void Profile(const SDVTListNode &X, FoldingSetNodeID& ID) {
    ID = X.FastID;
  }

  /// Return true if \p X has the same interned identifier as \p ID.
  ///
  /// \param X Node to compare.
  /// \param ID Folding-set identifier to test against.
  /// \param IDHash Hash of \p ID used as a fast reject.
  /// \param TempID Unused; the interned identifier is compared directly.
  /// \return True if \p X has the same interned identifier as \p ID.
  static bool Equals(const SDVTListNode &X, const FoldingSetNodeID &ID,
                     unsigned IDHash, FoldingSetNodeID &TempID) {
    if (X.HashValue != IDHash)
      return false;
    return ID == X.FastID;
  }

  /// Return the cached hash of \p X.
  ///
  /// \param X Node whose cached hash is returned.
  /// \param TempID Unused; the hash is stored on the node.
  /// \return The cached hash of \p X.
  static unsigned ComputeHash(const SDVTListNode &X, FoldingSetNodeID &TempID) {
    return X.HashValue;
  }
};

/// ilist allocation traits for SDNode; nodes are not deleted through the list.
template <> struct ilist_alloc_traits<SDNode> {
  /// Reject attempts to delete an SDNode through the ilist.
  ///
  /// \param Node Unused; SDNodes are destroyed by SelectionDAG, not ilist.
  static void deleteNode(SDNode *Node) {
    llvm_unreachable("ilist_traits<SDNode> shouldn't see a deleteNode call!");
  }
};

/// Side table of dbg_value and dbg_label info for SelectionDAG ISel.
///
/// We do not build SDNodes for these so as not to perturb the generated code;
/// instead the info is kept off to the side in this structure. Each SDNode may
/// have one or more associated dbg_value entries. This information is kept in
/// DbgValMap.
/// Byval parameters are handled separately because they don't use alloca's,
/// which busts the normal mechanism.  There is good reason for handling all
/// parameters separately:  they may not have code generated for them, they
/// should always go at the beginning of the function regardless of other code
/// motion, and debug info for them is potentially useful even if the parameter
/// is unused.  Right now only byval parameters are handled separately.
class SDDbgInfo {
  BumpPtrAllocator Alloc;
  SmallVector<SDDbgValue*, 32> DbgValues;
  SmallVector<SDDbgValue*, 32> ByvalParmDbgValues;
  SmallVector<SDDbgLabel*, 4> DbgLabels;
  using DbgValMapType = DenseMap<const SDNode *, SmallVector<SDDbgValue *, 2>>;
  DbgValMapType DbgValMap;

public:
  /// Construct an empty debug-info table.
  SDDbgInfo() = default;
  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copy construction is deleted.
  SDDbgInfo(const SDDbgInfo &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other Unused; copy assignment is deleted.
  SDDbgInfo &operator=(const SDDbgInfo &Other) = delete;

  /// Record a dbg_value, optionally as a parameter.
  ///
  /// \param V Debug-value descriptor to add.
  /// \param isParameter True if the debug value describes a parameter.
  LLVM_ABI void add(SDDbgValue *V, bool isParameter);

  /// Record a dbg_label.
  ///
  /// \param L Debug-label descriptor to add.
  void add(SDDbgLabel *L) { DbgLabels.push_back(L); }

  /// Invalidate all DbgValues attached to the node and remove
  /// it from the Node-to-DbgValues map.
  ///
  /// \param Node Node whose debug values are erased.
  LLVM_ABI void erase(const SDNode *Node);

  /// Clear all recorded debug values, labels, and allocator state.
  void clear() {
    DbgValMap.clear();
    DbgValues.clear();
    ByvalParmDbgValues.clear();
    DbgLabels.clear();
    Alloc.Reset();
  }

  /// Return the allocator used for debug-info objects.
  ///
  /// \return The allocator used for debug-info objects.
  BumpPtrAllocator &getAlloc() { return Alloc; }

  /// Return true if there are no recorded debug values or labels.
  ///
  /// \return True if there are no recorded debug values or labels.
  bool empty() const {
    return DbgValues.empty() && ByvalParmDbgValues.empty() && DbgLabels.empty();
  }

  /// Return the debug values attached to \p Node, if any.
  ///
  /// \param Node Node whose debug values are requested.
  /// \return The debug values attached to \p Node, if any.
  ArrayRef<SDDbgValue*> getSDDbgValues(const SDNode *Node) const {
    auto I = DbgValMap.find(Node);
    if (I != DbgValMap.end())
      return I->second;
    return ArrayRef<SDDbgValue*>();
  }

  /// Iterator over SDDbgValue entries.
  using DbgIterator = SmallVectorImpl<SDDbgValue*>::iterator;
  /// Iterator over SDDbgLabel entries.
  using DbgLabelIterator = SmallVectorImpl<SDDbgLabel*>::iterator;

  /// Return an iterator to the first SDDbgValue.
  ///
  /// \return An iterator to the first SDDbgValue.
  DbgIterator DbgBegin() { return DbgValues.begin(); }
  /// Return an iterator one past the last SDDbgValue.
  ///
  /// \return An iterator one past the last SDDbgValue.
  DbgIterator DbgEnd()   { return DbgValues.end(); }
  /// Return an iterator to the first byval-parameter SDDbgValue.
  ///
  /// \return An iterator to the first byval-parameter SDDbgValue.
  DbgIterator ByvalParmDbgBegin() { return ByvalParmDbgValues.begin(); }
  /// Return an iterator one past the last byval-parameter SDDbgValue.
  ///
  /// \return An iterator one past the last byval-parameter SDDbgValue.
  DbgIterator ByvalParmDbgEnd()   { return ByvalParmDbgValues.end(); }
  /// Return an iterator to the first SDDbgLabel.
  ///
  /// \return An iterator to the first SDDbgLabel.
  DbgLabelIterator DbgLabelBegin() { return DbgLabels.begin(); }
  /// Return an iterator one past the last SDDbgLabel.
  ///
  /// \return An iterator one past the last SDDbgLabel.
  DbgLabelIterator DbgLabelEnd()   { return DbgLabels.end(); }
};

/// Abort if \p DAG contains a cycle.
///
/// \param DAG SelectionDAG to check.
/// \param force True to check even when cycle checks are otherwise disabled.
LLVM_ABI void checkForCycles(const SelectionDAG *DAG, bool force = false);

/// This is used to represent a portion of an LLVM function as a DAG.
///
/// The DAG is a low-level data-dependence representation suitable for
/// instruction selection. It is constructed as the first step of instruction
/// selection in order to allow implementation of machine specific
/// optimizations and code simplifications.
///
/// The representation used by the SelectionDAG is a target-independent
/// representation, which has some similarities to the GCC RTL representation,
/// but is significantly more simple, powerful, and is a graph form instead of a
/// linear form.
class SelectionDAG {
  const TargetMachine &TM;
  const SelectionDAGTargetInfo *TSI = nullptr;
  const TargetLowering *TLI = nullptr;
  const TargetLibraryInfo *LibInfo = nullptr;
  const RTLIB::RuntimeLibcallsInfo *RuntimeLibcallInfo = nullptr;
  const LibcallLoweringInfo *Libcalls = nullptr;

  const FunctionVarLocs *FnVarLocs = nullptr;
  MachineFunction *MF;
  MachineFunctionAnalysisManager *MFAM = nullptr;
  Pass *SDAGISelPass = nullptr;
  LLVMContext *Context;
  CodeGenOptLevel OptLevel;

  UniformityInfo *UA = nullptr;
  FunctionLoweringInfo * FLI = nullptr;

  /// The function-level optimization remark emitter.  Used to emit remarks
  /// whenever manipulating the DAG.
  OptimizationRemarkEmitter *ORE;

  ProfileSummaryInfo *PSI = nullptr;
  BlockFrequencyInfo *BFI = nullptr;
  MachineModuleInfo *MMI = nullptr;

  /// Extended EVTs used for single value VTLists.
  std::set<EVT, EVT::compareRawBits> EVTs;

  /// List of non-single value types.
  FoldingSet<SDVTListNode> VTListMap;

  /// Pool allocation for misc. objects that are created once per SelectionDAG.
  BumpPtrAllocator Allocator;

  /// The starting token.
  SDNode EntryNode;

  /// The root of the entire DAG.
  SDValue Root;

  /// A linked list of nodes in the current DAG.
  ilist<SDNode> AllNodes;

  /// The AllocatorType for allocating SDNodes. We use
  /// pool allocation with recycling.
  using NodeAllocatorType = RecyclingAllocator<BumpPtrAllocator, SDNode,
                                               sizeof(LargestSDNode),
                                               alignof(MostAlignedSDNode)>;

  /// Pool allocation for nodes.
  NodeAllocatorType NodeAllocator;

  /// This structure is used to memoize nodes, automatically performing
  /// CSE with existing nodes when a duplicate is requested.
  FoldingSet<SDNode> CSEMap;

  /// Pool allocation for machine-opcode SDNode operands.
  BumpPtrAllocator OperandAllocator;
  ArrayRecycler<SDUse> OperandRecycler;

  /// Tracks dbg_value and dbg_label information through SDISel.
  SDDbgInfo *DbgInfo;

  using CallSiteInfo = MachineFunction::CallSiteInfo;
  using CalledGlobalInfo = MachineFunction::CalledGlobalInfo;

  struct NodeExtraInfo {
    CallSiteInfo CSInfo;
    MDNode *HeapAllocSite = nullptr;
    MDNode *PCSections = nullptr;
    MDNode *MMRA = nullptr;
    CalledGlobalInfo CalledGlobal{};
    bool NoMerge = false;
  };
  /// Out-of-line extra information for SDNodes.
  DenseMap<const SDNode *, NodeExtraInfo> SDEI;

  /// PersistentId counter to be used when inserting the next
  /// SDNode to this SelectionDAG. We do not place that under
  /// `#if LLVM_ENABLE_ABI_BREAKING_CHECKS` intentionally because
  /// it adds unneeded complexity without noticeable
  /// benefits (see discussion with @thakis in D120714).
  uint16_t NextPersistentId = 0;

public:
  /// Listener for DAG updates such as node deletion, mutation, and insertion.
  ///
  /// Clients of various APIs that cause global effects on the DAG can
  /// optionally implement this interface. This allows the clients to handle
  /// the various sorts of updates that happen.
  ///
  /// A DAGUpdateListener automatically registers itself with DAG when it is
  /// constructed, and removes itself when destroyed in RAII fashion.
  struct LLVM_ABI DAGUpdateListener {
    /// Next listener in the stack, or null.
    DAGUpdateListener *const Next;
    /// SelectionDAG this listener is registered with.
    SelectionDAG &DAG;

    /// Register this listener on \p D.
    ///
    /// \param D SelectionDAG to listen to.
    explicit DAGUpdateListener(SelectionDAG &D)
      : Next(D.UpdateListeners), DAG(D) {
      DAG.UpdateListeners = this;
    }

    /// Unregister this listener from the DAG.
    virtual ~DAGUpdateListener() {
      assert(DAG.UpdateListeners == this &&
             "DAGUpdateListeners must be destroyed in LIFO order");
      DAG.UpdateListeners = Next;
    }

    /// Called when node \p N is deleted.
    ///
    /// \param N Node that was deleted.
    /// \param E Equivalent replacement node, or null if none.
    virtual void NodeDeleted(SDNode *N, SDNode *E);

    /// Called when node \p N is updated.
    ///
    /// \param N Node that was updated.
    virtual void NodeUpdated(SDNode *N);

    /// Called when node \p N is inserted.
    ///
    /// \param N Node that was inserted.
    virtual void NodeInserted(SDNode *N);
  };

  /// DAGUpdateListener that invokes a callback when a node is deleted.
  struct LLVM_ABI DAGNodeDeletedListener : public DAGUpdateListener {
    /// Callback invoked as Callback(deleted, replacement).
    std::function<void(SDNode *, SDNode *)> Callback;

    /// Construct a listener that calls \p Callback on node deletion.
    ///
    /// \param DAG SelectionDAG to listen to.
    /// \param Callback Function called with the deleted node and replacement.
    DAGNodeDeletedListener(SelectionDAG &DAG,
                           std::function<void(SDNode *, SDNode *)> Callback)
        : DAGUpdateListener(DAG), Callback(std::move(Callback)) {}

    /// Invoke \p Callback for the deleted node.
    ///
    /// \param N Node that was deleted.
    /// \param E Equivalent replacement node, or null if none.
    void NodeDeleted(SDNode *N, SDNode *E) override { Callback(N, E); }

   private:
    virtual void anchor();
  };

  /// DAGUpdateListener that invokes a callback when a node is inserted.
  struct LLVM_ABI DAGNodeInsertedListener : public DAGUpdateListener {
    /// Callback invoked as Callback(inserted).
    std::function<void(SDNode *)> Callback;

    /// Construct a listener that calls \p Callback on node insertion.
    ///
    /// \param DAG SelectionDAG to listen to.
    /// \param Callback Function called with the inserted node.
    DAGNodeInsertedListener(SelectionDAG &DAG,
                            std::function<void(SDNode *)> Callback)
        : DAGUpdateListener(DAG), Callback(std::move(Callback)) {}

    /// Invoke \p Callback for the inserted node.
    ///
    /// \param N Node that was inserted.
    void NodeInserted(SDNode *N) override { Callback(N); }

  private:
    virtual void anchor();
  };

  /// RAII helper that inserts SDNodeFlags on new nodes in the current scope.
  class FlagInserter {
    SelectionDAG &DAG;
    SDNodeFlags Flags;
    FlagInserter *LastInserter;

  public:
    /// Install this inserter on \p SDAG with the given flags.
    ///
    /// \param SDAG SelectionDAG that owns the inserter.
    /// \param Flags Flags to apply to new nodes.
    FlagInserter(SelectionDAG &SDAG, SDNodeFlags Flags)
        : DAG(SDAG), Flags(Flags),
          LastInserter(SDAG.getFlagInserter()) {
      SDAG.setFlagInserter(this);
    }
    /// Install this inserter using flags copied from \p N.
    ///
    /// \param SDAG SelectionDAG that owns the inserter.
    /// \param N Node whose flags are copied.
    FlagInserter(SelectionDAG &SDAG, SDNode *N)
        : FlagInserter(SDAG, N->getFlags()) {}

    /// Deleted copy constructor.
    ///
    /// \param Other Unused; copy construction is deleted.
    FlagInserter(const FlagInserter &Other) = delete;
    /// Deleted copy assignment.
    ///
    /// \param Other Unused; copy assignment is deleted.
    FlagInserter &operator=(const FlagInserter &Other) = delete;
    /// Restore the previous flag inserter.
    ~FlagInserter() { DAG.setFlagInserter(LastInserter); }

    /// Return the flags this inserter applies to new nodes.
    ///
    /// \return The flags this inserter applies to new nodes.
    SDNodeFlags getFlags() const { return Flags; }
  };

  /// True if newly created nodes must have legal types.
  ///
  /// When true, additional steps are taken to ensure that getConstant() and
  /// similar functions return DAG nodes that have legal types. This is
  /// important after type legalization since any illegally typed nodes
  /// generated after this point will not experience type legalization.
  bool NewNodesMustHaveLegalTypes = false;

private:
  /// DAGUpdateListener is a friend so it can manipulate the listener stack.
  friend struct DAGUpdateListener;

  /// Linked list of registered DAGUpdateListener instances.
  /// This stack is maintained by DAGUpdateListener RAII.
  DAGUpdateListener *UpdateListeners = nullptr;

  /// Implementation of setSubgraphColor.
  /// Return whether we had to truncate the search.
  bool setSubgraphColorHelper(SDNode *N, const char *Color,
                              DenseSet<SDNode *> &visited,
                              int level, bool &printed);

  template <typename SDNodeT, typename... ArgTypes>
  SDNodeT *newSDNode(ArgTypes &&... Args) {
    return new (NodeAllocator.template Allocate<SDNodeT>())
        SDNodeT(std::forward<ArgTypes>(Args)...);
  }

  /// Build a synthetic SDNodeT with the given args and extract its subclass
  /// data as an integer (e.g. for use in a folding set).
  ///
  /// The args to this function are the same as the args to SDNodeT's
  /// constructor, except the second arg (assumed to be a const DebugLoc&) is
  /// omitted.
  template <typename SDNodeT, typename... ArgTypes>
  static uint16_t getSyntheticNodeSubclassData(unsigned IROrder,
                                               ArgTypes &&... Args) {
    // The compiler can reduce this expression to a constant iff we pass an
    // empty DebugLoc.  Thankfully, the debug location doesn't have any bearing
    // on the subclass data.
    return SDNodeT(IROrder, DebugLoc(), std::forward<ArgTypes>(Args)...)
        .getRawSubclassData();
  }

  template <typename SDNodeTy>
  static uint16_t getSyntheticNodeSubclassData(unsigned Opc, unsigned Order,
                                               SDVTList VTs, EVT MemoryVT,
                                               MachineMemOperand *MMO) {
    return SDNodeTy(Opc, Order, DebugLoc(), VTs, MemoryVT, MMO)
        .getRawSubclassData();
  }

  template <typename SDNodeTy>
  static uint16_t getSyntheticNodeSubclassData(
      unsigned Opc, unsigned Order, SDVTList VTs, EVT MemoryVT,
      PointerUnion<MachineMemOperand *, MachineMemOperand **> MemRefs) {
    return SDNodeTy(Opc, Order, DebugLoc(), VTs, MemoryVT, MemRefs)
        .getRawSubclassData();
  }

  void createOperands(SDNode *Node, ArrayRef<SDValue> Vals);

  void removeOperands(SDNode *Node) {
    if (!Node->OperandList)
      return;
    OperandRecycler.deallocate(
        ArrayRecycler<SDUse>::Capacity::get(Node->NumOperands),
        Node->OperandList);
    Node->NumOperands = 0;
    Node->OperandList = nullptr;
  }
  void CreateTopologicalOrder(std::vector<SDNode*>& Order);

public:
  /// Maximum depth for recursive analysis such as computeKnownBits.
  static constexpr unsigned MaxRecursionDepth = 6;

  /// Return the maximum steps for SDNode->hasPredecessor() like searches.
  ///
  /// \return The maximum steps for hasPredecessor-like searches.
  LLVM_ABI static unsigned getHasPredecessorMaxSteps();

  /// Construct a SelectionDAG for the given target and optimization level.
  ///
  /// \param TM Target machine.
  /// \param OptLevel Optimization level.
  LLVM_ABI explicit SelectionDAG(const TargetMachine &TM,
                                 CodeGenOptLevel OptLevel);
  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copy construction is deleted.
  SelectionDAG(const SelectionDAG &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other Unused; copy assignment is deleted.
  SelectionDAG &operator=(const SelectionDAG &Other) = delete;
  /// Destroy the SelectionDAG and release its nodes.
  LLVM_ABI ~SelectionDAG();

  /// Prepare this SelectionDAG to process code in the given MachineFunction.
  ///
  /// \param NewMF Machine function being lowered.
  /// \param NewORE Optimization-remark emitter.
  /// \param PassPtr Instruction-selection pass, if any.
  /// \param LibraryInfo Target library info.
  /// \param LibcallsInfo Libcall lowering info.
  /// \param UA Uniformity analysis, if available.
  /// \param PSIin Profile summary info, if available.
  /// \param BFIin Block frequency info, if available.
  /// \param MMI Machine module info.
  /// \param FnVarLocs Assignment-tracking variable locations, if any.
  LLVM_ABI void init(MachineFunction &NewMF, OptimizationRemarkEmitter &NewORE,
                     Pass *PassPtr, const TargetLibraryInfo *LibraryInfo,
                     const LibcallLoweringInfo *LibcallsInfo,
                     UniformityInfo *UA, ProfileSummaryInfo *PSIin,
                     BlockFrequencyInfo *BFIin, MachineModuleInfo &MMI,
                     FunctionVarLocs const *FnVarLocs);

  /// Prepare this SelectionDAG using a MachineFunctionAnalysisManager.
  ///
  /// \param NewMF Machine function being lowered.
  /// \param NewORE Optimization-remark emitter.
  /// \param AM Machine function analysis manager.
  /// \param LibraryInfo Target library info.
  /// \param LibcallsInfo Libcall lowering info.
  /// \param UA Uniformity analysis, if available.
  /// \param PSIin Profile summary info, if available.
  /// \param BFIin Block frequency info, if available.
  /// \param MMI Machine module info.
  /// \param FnVarLocs Assignment-tracking variable locations, if any.
  void init(MachineFunction &NewMF, OptimizationRemarkEmitter &NewORE,
            MachineFunctionAnalysisManager &AM,
            const TargetLibraryInfo *LibraryInfo,
            const LibcallLoweringInfo *LibcallsInfo, UniformityInfo *UA,
            ProfileSummaryInfo *PSIin, BlockFrequencyInfo *BFIin,
            MachineModuleInfo &MMI, FunctionVarLocs const *FnVarLocs) {
    init(NewMF, NewORE, nullptr, LibraryInfo, LibcallsInfo, UA, PSIin, BFIin,
         MMI, FnVarLocs);
    MFAM = &AM;
  }

  /// Set the function-lowering info used by DAG building.
  ///
  /// \param FuncInfo Function lowering info.
  void setFunctionLoweringInfo(FunctionLoweringInfo * FuncInfo) {
    FLI = FuncInfo;
  }

  /// Clear state and free memory necessary to make this
  /// SelectionDAG ready to process a new block.
  LLVM_ABI void clear();

  /// Return the machine function currently being lowered.
  ///
  /// \return The machine function currently being lowered.
  MachineFunction &getMachineFunction() const { return *MF; }
  /// Return the instruction-selection pass, if any.
  ///
  /// \return The instruction-selection pass, if any.
  const Pass *getPass() const { return SDAGISelPass; }
  /// Return the machine-function analysis manager, if any.
  ///
  /// \return The machine-function analysis manager, if any.
  MachineFunctionAnalysisManager *getMFAM() { return MFAM; }

  /// Return true if the function has a swifterror argument.
  ///
  /// \return True if the function has a swifterror argument.
  bool hasSwiftErrorArg() const;

  /// Return the optimization level.
  ///
  /// \return The optimization level.
  CodeGenOptLevel getOptLevel() const { return OptLevel; }
  /// Return the data layout of the current module.
  ///
  /// \return The data layout of the current module.
  const DataLayout &getDataLayout() const { return MF->getDataLayout(); }
  /// Return the target machine.
  ///
  /// \return The target machine.
  const TargetMachine &getTarget() const { return TM; }
  /// Return the current function's subtarget.
  ///
  /// \return The current function's subtarget.
  const TargetSubtargetInfo &getSubtarget() const { return MF->getSubtarget(); }
  /// Return the current function's subtarget as a specific type.
  ///
  /// \return The current function's subtarget as a specific type.
  template <typename STC> const STC &getSubtarget() const {
    return MF->getSubtarget<STC>();
  }
  /// Return the target lowering info.
  ///
  /// \return The target lowering info.
  const TargetLowering &getTargetLoweringInfo() const { return *TLI; }
  /// Return the target library info.
  ///
  /// \return The target library info.
  const TargetLibraryInfo &getLibInfo() const { return *LibInfo; }

  /// Return the libcall lowering info.
  ///
  /// \return The libcall lowering info.
  const LibcallLoweringInfo &getLibcalls() const { return *Libcalls; }

  /// Return the runtime libcall info.
  ///
  /// \return The runtime libcall info.
  const RTLIB::RuntimeLibcallsInfo &getRuntimeLibcallInfo() const {
    return *RuntimeLibcallInfo;
  }

  /// Return the target-specific SelectionDAG info.
  ///
  /// \return The target-specific SelectionDAG info.
  const SelectionDAGTargetInfo &getSelectionDAGInfo() const { return *TSI; }
  /// Return the uniformity analysis, if available.
  ///
  /// \return The uniformity analysis, if available.
  const UniformityInfo *getUniformityInfo() const { return UA; }
  /// Returns the result of the AssignmentTrackingAnalysis pass if it's
  /// available, otherwise return nullptr.
  ///
  /// \return The result of the AssignmentTrackingAnalysis pass if it's available, otherwise return nullptr.
  const FunctionVarLocs *getFunctionVarLocs() const { return FnVarLocs; }
  /// Return the LLVM context.
  ///
  /// \return The LLVM context.
  LLVMContext *getContext() const { return Context; }
  /// Return the optimization-remark emitter.
  ///
  /// \return The optimization-remark emitter.
  OptimizationRemarkEmitter &getORE() const { return *ORE; }
  /// Return the profile summary info, if any.
  ///
  /// \return The profile summary info, if any.
  ProfileSummaryInfo *getPSI() const { return PSI; }
  /// Return the block frequency info, if any.
  ///
  /// \return The block frequency info, if any.
  BlockFrequencyInfo *getBFI() const { return BFI; }
  /// Return the machine module info.
  ///
  /// \return The machine module info.
  MachineModuleInfo *getMMI() const { return MMI; }

  /// Return the current flag inserter, if any.
  ///
  /// \return The current flag inserter, if any.
  FlagInserter *getFlagInserter() { return Inserter; }
  /// Set the current flag inserter.
  ///
  /// \param FI Flag inserter to install, or null.
  void setFlagInserter(FlagInserter *FI) { Inserter = FI; }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump the DAG as a GraphViz file at \p FileName.
  ///
  /// Does not open a viewer. \p FileName expects an absolute path; if it has no
  /// path separators the file is created in the current directory.
  ///
  /// \param FileName Destination path of the dumped graph.
  /// \param Title Graph title.
  LLVM_DUMP_METHOD void dumpDotGraph(const Twine &FileName, const Twine &Title);
#endif

  /// Pop up a GraphViz/gv window with the DAG rendered using 'dot'.
  ///
  /// \param Title Window title for the graph viewer.
  LLVM_ABI void viewGraph(const std::string &Title);
  /// Pop up a GraphViz/gv window with the DAG rendered using 'dot'.
  LLVM_ABI void viewGraph();

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  std::map<const SDNode *, std::string> NodeGraphAttrs;
#endif

  /// Clear all previously defined node graph attributes.
  /// Intended to be used from a debugging tool (eg. gdb).
  LLVM_ABI void clearGraphAttrs();

  /// Set graph attributes for a node. (eg. "color=red".)
  ///
  /// \param N Node whose attributes are set.
  /// \param Attrs GraphViz attribute string.
  LLVM_ABI void setGraphAttrs(const SDNode *N, const char *Attrs);

  /// Get graph attributes for a node. (eg. "color=red".)
  /// Used from getNodeAttributes.
  ///
  /// \param N Node whose attributes are requested.
  /// \return Graph attributes for a node.
  LLVM_ABI std::string getGraphAttrs(const SDNode *N) const;

  /// Convenience for setting node color attribute.
  ///
  /// \param N Node whose color is set.
  /// \param Color Color name.
  LLVM_ABI void setGraphColor(const SDNode *N, const char *Color);

  /// Convenience for setting subgraph color attribute.
  ///
  /// \param N Root of the subgraph whose color is set.
  /// \param Color Color name.
  LLVM_ABI void setSubgraphColor(SDNode *N, const char *Color);

  /// Const iterator over all nodes in the DAG.
  using allnodes_const_iterator = ilist<SDNode>::const_iterator;

  /// Return a const iterator to the first node.
  ///
  /// \return A const iterator to the first node.
  allnodes_const_iterator allnodes_begin() const { return AllNodes.begin(); }
  /// Return a const iterator one past the last node.
  ///
  /// \return A const iterator one past the last node.
  allnodes_const_iterator allnodes_end() const { return AllNodes.end(); }

  /// Iterator over all nodes in the DAG.
  using allnodes_iterator = ilist<SDNode>::iterator;

  /// Return an iterator to the first node.
  ///
  /// \return An iterator to the first node.
  allnodes_iterator allnodes_begin() { return AllNodes.begin(); }
  /// Return an iterator one past the last node.
  ///
  /// \return An iterator one past the last node.
  allnodes_iterator allnodes_end() { return AllNodes.end(); }

  /// Return the number of nodes in the DAG.
  ///
  /// \return The number of nodes in the DAG.
  ilist<SDNode>::size_type allnodes_size() const {
    return AllNodes.size();
  }

  /// Return a range over all nodes in the DAG.
  ///
  /// \return A range over all nodes in the DAG.
  iterator_range<allnodes_iterator> allnodes() {
    return make_range(allnodes_begin(), allnodes_end());
  }
  /// Return a const range over all nodes in the DAG.
  ///
  /// \return A const range over all nodes in the DAG.
  iterator_range<allnodes_const_iterator> allnodes() const {
    return make_range(allnodes_begin(), allnodes_end());
  }

  /// Return the root tag of the SelectionDAG.
  ///
  /// \return The root tag of the SelectionDAG.
  const SDValue &getRoot() const { return Root; }

  /// Return the token chain corresponding to the entry of the function.
  ///
  /// \return The token chain corresponding to the entry of the function.
  SDValue getEntryNode() const {
    return SDValue(const_cast<SDNode *>(&EntryNode), 0);
  }

  /// Set the current root tag of the SelectionDAG.
  ///
  /// \param N New root chain value.
  /// \return The new root value.
  const SDValue &setRoot(SDValue N) {
    assert((!N.getNode() || N.getValueType() == MVT::Other) &&
           "DAG root value is not a chain!");
    if (N.getNode())
      checkForCycles(N.getNode(), this);
    Root = N;
    if (N.getNode())
      checkForCycles(this);
    return Root;
  }

#if !defined(NDEBUG) && LLVM_ENABLE_ABI_BREAKING_CHECKS
  void VerifyDAGDivergence();
#endif

  /// Combine redundant or superfluous nodes in the DAG.
  ///
  /// Iterates over the nodes, folding certain types together or eliminating
  /// superfluous nodes. The Level argument controls whether Combine is allowed
  /// to produce nodes and types that are illegal on the target.
  ///
  /// \param Level Legalization level that Combine may target.
  /// \param BatchAA Optional batch alias-analysis results.
  /// \param OptLevel Optimization level.
  LLVM_ABI void Combine(CombineLevel Level, BatchAAResults *BatchAA,
                        CodeGenOptLevel OptLevel);

  /// This transforms the SelectionDAG into a SelectionDAG that
  /// only uses types natively supported by the target.
  /// Returns "true" if it made any changes.
  ///
  /// Note that this is an involved process that may invalidate pointers into
  /// the graph.
  ///
  /// \return True if any changes were made.
  LLVM_ABI bool LegalizeTypes();

  /// This transforms the SelectionDAG into a SelectionDAG that is
  /// compatible with the target instruction selector, as indicated by the
  /// TargetLowering object.
  ///
  /// Note that this is an involved process that may invalidate pointers into
  /// the graph.
  LLVM_ABI void Legalize();

  /// Legalize a single node and its operands for the target selector.
  ///
  /// Transforms a SelectionDAG node and any operands to it into a node that is
  /// compatible with the target instruction selector, as indicated by the
  /// TargetLowering object.
  ///
  /// \returns true if \c N is a valid, legal node after calling this.
  ///
  /// This essentially runs a single recursive walk of the \c Legalize process
  /// over the given node (and its operands). This can be used to incrementally
  /// legalize the DAG. All of the nodes which are directly replaced,
  /// potentially including N, are added to the output parameter \c
  /// UpdatedNodes so that the delta to the DAG can be understood by the
  /// caller.
  ///
  /// When this returns false, N has been legalized in a way that make the
  /// pointer passed in no longer valid. It may have even been deleted from the
  /// DAG, and so it shouldn't be used further. When this returns true, the
  /// N passed in is a legal node, and can be immediately processed as such.
  /// This may still have done some work on the DAG, and will still populate
  /// UpdatedNodes with any new nodes replacing those originally in the DAG.
  ///
  /// \param N Node to legalize.
  /// \param UpdatedNodes Nodes replaced during incremental legalization.
  LLVM_ABI bool LegalizeOp(SDNode *N,
                           SmallSetVector<SDNode *, 16> &UpdatedNodes);

  /// Legalize vector operations to those supported by the target.
  ///
  /// This transforms the SelectionDAG into a SelectionDAG that only uses vector
  /// math operations supported by the target. This is necessary as a separate
  /// step from Legalize because unrolling a vector operation can introduce
  /// illegal types, which requires running LegalizeTypes again.
  ///
  /// This returns true if it made any changes; in that case, LegalizeTypes
  /// is called again before Legalize.
  ///
  /// Note that this is an involved process that may invalidate pointers into
  /// the graph.
  ///
  /// \return True if any changes were made.
  LLVM_ABI bool LegalizeVectors();

  /// This method deletes all unreachable nodes in the SelectionDAG.
  LLVM_ABI void RemoveDeadNodes();

  /// Remove the specified node from the system.  This node must
  /// have no referrers.
  ///
  /// \param N Node to remove.
  LLVM_ABI void DeleteNode(SDNode *N);

  /// Return an SDVTList that represents the listed value types.
  ///
  /// \param VT Value type.
  /// \return An SDVTList that represents the listed value types.
  LLVM_ABI SDVTList getVTList(EVT VT);
  /// Return an SDVTList that represents the listed value types.
  ///
  /// \param VT1 First value type.
  /// \param VT2 Second value type.
  /// \return An SDVTList that represents the listed value types.
  LLVM_ABI SDVTList getVTList(EVT VT1, EVT VT2);
  /// Return an SDVTList that represents the listed value types.
  ///
  /// \param VT1 First value type.
  /// \param VT2 Second value type.
  /// \param VT3 Third value type.
  /// \return An SDVTList that represents the listed value types.
  LLVM_ABI SDVTList getVTList(EVT VT1, EVT VT2, EVT VT3);
  /// Return an SDVTList that represents the listed value types.
  ///
  /// \param VT1 First value type.
  /// \param VT2 Second value type.
  /// \param VT3 Third value type.
  /// \param VT4 Fourth value type.
  /// \return An SDVTList that represents the listed value types.
  LLVM_ABI SDVTList getVTList(EVT VT1, EVT VT2, EVT VT3, EVT VT4);
  /// Return an SDVTList that represents the listed value types.
  ///
  /// \param VTs Value types.
  /// \return An SDVTList that represents the listed value types.
  LLVM_ABI SDVTList getVTList(ArrayRef<EVT> VTs);

  //===--------------------------------------------------------------------===//
  // Node creation methods.

  /// Create a ConstantSDNode wrapping a constant value.
  /// If VT is a vector type, the constant is splatted into a BUILD_VECTOR.
  ///
  /// Create a ConstantSDNode wrapping a constant value.
  ///
  /// If only legal types can be produced, this does the necessary
  /// transformations (e.g., if the vector element type is illegal).
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param isTarget True to create a target node.
  /// \param isOpaque True to create an opaque constant.
  /// \return The ConstantSDNode wrapping a constant value.
  LLVM_ABI SDValue getConstant(uint64_t Val, const SDLoc &DL, EVT VT,
                               bool isTarget = false, bool isOpaque = false);
  /// Create a ConstantSDNode wrapping a constant value.
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param isTarget True to create a target node.
  /// \param isOpaque True to create an opaque constant.
  /// \return The ConstantSDNode wrapping a constant value.
  LLVM_ABI SDValue getConstant(const APInt &Val, const SDLoc &DL, EVT VT,
                               bool isTarget = false, bool isOpaque = false);

  /// Create a signed ConstantSDNode wrapping a constant value.
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param isTarget True to create a target node.
  /// \param isOpaque True to create an opaque constant.
  /// \return The signed ConstantSDNode wrapping a constant value.
  LLVM_ABI SDValue getSignedConstant(int64_t Val, const SDLoc &DL, EVT VT,
                                     bool isTarget = false,
                                     bool isOpaque = false);

  /// Create a constant of all-ones bits.
  ///
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param IsTarget True to create a target constant.
  /// \param IsOpaque True to create an opaque constant.
  /// \return The constant of all-ones bits.
  LLVM_ABI SDValue getAllOnesConstant(const SDLoc &DL, EVT VT,
                                      bool IsTarget = false,
                                      bool IsOpaque = false);

  /// Create a ConstantSDNode wrapping a constant value.
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param isTarget True to create a target node.
  /// \param isOpaque True to create an opaque constant.
  /// \return The ConstantSDNode wrapping a constant value.
  LLVM_ABI SDValue getConstant(const ConstantInt &Val, const SDLoc &DL, EVT VT,
                               bool isTarget = false, bool isOpaque = false);
  /// Create a pointer-sized integer constant.
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param isTarget True to create a target node.
  /// \return The pointer-sized integer constant.
  LLVM_ABI SDValue getIntPtrConstant(uint64_t Val, const SDLoc &DL,
                                     bool isTarget = false);
  /// Create a constant suitable for use as a shift amount.
  ///
  /// \param Val Constant or operand value.
  /// \param VT Value type.
  /// \param DL Debug location for the node.
  /// \return The constant suitable for use as a shift amount.
  LLVM_ABI SDValue getShiftAmountConstant(uint64_t Val, EVT VT,
                                          const SDLoc &DL);
  /// Create a constant suitable for use as a shift amount.
  ///
  /// \param Val Constant or operand value.
  /// \param VT Value type.
  /// \param DL Debug location for the node.
  /// \return The constant suitable for use as a shift amount.
  LLVM_ABI SDValue getShiftAmountConstant(const APInt &Val, EVT VT,
                                          const SDLoc &DL);
  /// Create a constant suitable for use as a vector index.
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param isTarget True to create a target node.
  /// \return The constant suitable for use as a vector index.
  LLVM_ABI SDValue getVectorIdxConstant(uint64_t Val, const SDLoc &DL,
                                        bool isTarget = false);

  /// Create a target ConstantSDNode wrapping a constant value.
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param isOpaque True to create an opaque constant.
  /// \return The target ConstantSDNode wrapping a constant value.
  SDValue getTargetConstant(uint64_t Val, const SDLoc &DL, EVT VT,
                            bool isOpaque = false) {
    return getConstant(Val, DL, VT, true, isOpaque);
  }
  /// Create a target ConstantSDNode wrapping a constant value.
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param isOpaque True to create an opaque constant.
  /// \return The target ConstantSDNode wrapping a constant value.
  SDValue getTargetConstant(const APInt &Val, const SDLoc &DL, EVT VT,
                            bool isOpaque = false) {
    return getConstant(Val, DL, VT, true, isOpaque);
  }
  /// Create a target ConstantSDNode wrapping a constant value.
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param isOpaque True to create an opaque constant.
  /// \return The target ConstantSDNode wrapping a constant value.
  SDValue getTargetConstant(const ConstantInt &Val, const SDLoc &DL, EVT VT,
                            bool isOpaque = false) {
    return getConstant(Val, DL, VT, true, isOpaque);
  }
  /// Create a signed target ConstantSDNode wrapping a constant value.
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param isOpaque True to create an opaque constant.
  /// \return The signed target ConstantSDNode wrapping a constant value.
  SDValue getSignedTargetConstant(int64_t Val, const SDLoc &DL, EVT VT,
                                  bool isOpaque = false) {
    return getSignedConstant(Val, DL, VT, true, isOpaque);
  }

  /// Create a true or false constant using the target BooleanContent.
  ///
  /// Create a true or false constant of type \p VT using the target's
  /// BooleanContent for type \p OpVT.
  ///
  /// \param V Value operand.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param OpVT Original operand type.
  /// \return The true or false constant using the target BooleanContent.
  LLVM_ABI SDValue getBoolConstant(bool V, const SDLoc &DL, EVT VT, EVT OpVT);

  /// Create a ConstantFPSDNode wrapping a constant value.
  /// If VT is a vector type, the constant is splatted into a BUILD_VECTOR.
  ///
  /// Create a ConstantFPSDNode wrapping a floating-point constant.
  ///
  /// If only legal types can be produced, this does the necessary
  /// transformations (e.g., if the vector element type is illegal).
  /// The forms that take a double should only be used for simple constants
  /// that can be exactly represented in VT.  No checks are made.
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param isTarget True to create a target node.
  /// \return The ConstantFPSDNode wrapping a constant value.
  LLVM_ABI SDValue getConstantFP(double Val, const SDLoc &DL, EVT VT,
                                 bool isTarget = false);
  /// Create a ConstantFPSDNode wrapping a floating-point constant.
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param isTarget True to create a target node.
  /// \return The ConstantFPSDNode wrapping a floating-point constant.
  LLVM_ABI SDValue getConstantFP(const APFloat &Val, const SDLoc &DL, EVT VT,
                                 bool isTarget = false);
  /// Create a ConstantFPSDNode wrapping a floating-point constant.
  ///
  /// \param V Value operand.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param isTarget True to create a target node.
  /// \return The ConstantFPSDNode wrapping a floating-point constant.
  LLVM_ABI SDValue getConstantFP(const ConstantFP &V, const SDLoc &DL, EVT VT,
                                 bool isTarget = false);
  /// Create a target ConstantFPSDNode wrapping a floating-point constant.
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return The target ConstantFPSDNode wrapping a floating-point constant.
  SDValue getTargetConstantFP(double Val, const SDLoc &DL, EVT VT) {
    return getConstantFP(Val, DL, VT, true);
  }
  /// Create a target ConstantFPSDNode wrapping a floating-point constant.
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return The target ConstantFPSDNode wrapping a floating-point constant.
  SDValue getTargetConstantFP(const APFloat &Val, const SDLoc &DL, EVT VT) {
    return getConstantFP(Val, DL, VT, true);
  }
  /// Create a target ConstantFPSDNode wrapping a floating-point constant.
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return The target ConstantFPSDNode wrapping a floating-point constant.
  SDValue getTargetConstantFP(const ConstantFP &Val, const SDLoc &DL, EVT VT) {
    return getConstantFP(Val, DL, VT, true);
  }

  /// Create a GlobalAddress node for a global value.
  ///
  /// \param GV Global value being referenced.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param offset Byte offset from the symbol.
  /// \param isTargetGA True to create a target global address.
  /// \param TargetFlags Target-specific operand flags.
  /// \return The GlobalAddress node for a global value.
  LLVM_ABI SDValue getGlobalAddress(const GlobalValue *GV, const SDLoc &DL,
                                    EVT VT, int64_t offset = 0,
                                    bool isTargetGA = false,
                                    unsigned TargetFlags = 0);
  /// Create a target GlobalAddress node for a global value.
  ///
  /// \param GV Global value being referenced.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param offset Byte offset from the symbol.
  /// \param TargetFlags Target-specific operand flags.
  /// \return The target GlobalAddress node for a global value.
  SDValue getTargetGlobalAddress(const GlobalValue *GV, const SDLoc &DL, EVT VT,
                                 int64_t offset = 0, unsigned TargetFlags = 0) {
    return getGlobalAddress(GV, DL, VT, offset, true, TargetFlags);
  }
  /// Create a deactivation-symbol node for a global value.
  ///
  /// \param GV Global value being referenced.
  /// \return The deactivation-symbol node for a global value.
  LLVM_ABI SDValue getDeactivationSymbol(const GlobalValue *GV);
  /// Create a FrameIndex node.
  ///
  /// \param FI Frame index.
  /// \param VT Value type.
  /// \param isTarget True to create a target node.
  /// \return The FrameIndex node.
  LLVM_ABI SDValue getFrameIndex(int FI, EVT VT, bool isTarget = false);
  /// Create a target FrameIndex node.
  ///
  /// \param FI Frame index.
  /// \param VT Value type.
  /// \return The target FrameIndex node.
  SDValue getTargetFrameIndex(int FI, EVT VT) {
    return getFrameIndex(FI, VT, true);
  }
  /// Create a JumpTable node.
  ///
  /// \param JTI Jump-table index.
  /// \param VT Value type.
  /// \param isTarget True to create a target node.
  /// \param TargetFlags Target-specific operand flags.
  /// \return The JumpTable node.
  LLVM_ABI SDValue getJumpTable(int JTI, EVT VT, bool isTarget = false,
                                unsigned TargetFlags = 0);
  /// Create a target JumpTable node.
  ///
  /// \param JTI Jump-table index.
  /// \param VT Value type.
  /// \param TargetFlags Target-specific operand flags.
  /// \return The target JumpTable node.
  SDValue getTargetJumpTable(int JTI, EVT VT, unsigned TargetFlags = 0) {
    return getJumpTable(JTI, VT, true, TargetFlags);
  }
  /// Create debug info for a jump table.
  ///
  /// \param JTI Jump-table index.
  /// \param Chain Incoming token chain.
  /// \param DL Debug location for the node.
  /// \return Debug info for a jump table.
  LLVM_ABI SDValue getJumpTableDebugInfo(int JTI, SDValue Chain,
                                         const SDLoc &DL);
  /// Create a ConstantPool node.
  ///
  /// \param C Constant or constant-pool value.
  /// \param VT Value type.
  /// \param Align Alignment.
  /// \param Offs Byte offset.
  /// \param isT True to create a target node.
  /// \param TargetFlags Target-specific operand flags.
  /// \return The ConstantPool node.
  LLVM_ABI SDValue getConstantPool(const Constant *C, EVT VT,
                                   MaybeAlign Align = std::nullopt,
                                   int Offs = 0, bool isT = false,
                                   unsigned TargetFlags = 0);
  /// Create a target ConstantPool node.
  ///
  /// \param C Constant or constant-pool value.
  /// \param VT Value type.
  /// \param Align Alignment.
  /// \param Offset Address offset.
  /// \param TargetFlags Target-specific operand flags.
  /// \return The target ConstantPool node.
  SDValue getTargetConstantPool(const Constant *C, EVT VT,
                                MaybeAlign Align = std::nullopt, int Offset = 0,
                                unsigned TargetFlags = 0) {
    return getConstantPool(C, VT, Align, Offset, true, TargetFlags);
  }
  /// Create a ConstantPool node.
  ///
  /// \param C Constant or constant-pool value.
  /// \param VT Value type.
  /// \param Align Alignment.
  /// \param Offs Byte offset.
  /// \param isT True to create a target node.
  /// \param TargetFlags Target-specific operand flags.
  /// \return The ConstantPool node.
  LLVM_ABI SDValue getConstantPool(MachineConstantPoolValue *C, EVT VT,
                                   MaybeAlign Align = std::nullopt,
                                   int Offs = 0, bool isT = false,
                                   unsigned TargetFlags = 0);
  /// Create a target ConstantPool node.
  ///
  /// \param C Constant or constant-pool value.
  /// \param VT Value type.
  /// \param Align Alignment.
  /// \param Offset Address offset.
  /// \param TargetFlags Target-specific operand flags.
  /// \return The target ConstantPool node.
  SDValue getTargetConstantPool(MachineConstantPoolValue *C, EVT VT,
                                MaybeAlign Align = std::nullopt, int Offset = 0,
                                unsigned TargetFlags = 0) {
    return getConstantPool(C, VT, Align, Offset, true, TargetFlags);
  }
  // When generating a branch to a BB, we don't in general know enough
  // to provide debug info for the BB at that time, so keep this one around.
  /// Create a BasicBlockSDNode for a machine basic block.
  ///
  /// \param MBB Machine basic block.
  /// \return The BasicBlockSDNode for a machine basic block.
  LLVM_ABI SDValue getBasicBlock(MachineBasicBlock *MBB);
  /// Create an ExternalSymbol node.
  ///
  /// \param Sym Symbol name or MCSymbol.
  /// \param VT Value type.
  /// \return The ExternalSymbol node.
  LLVM_ABI SDValue getExternalSymbol(const char *Sym, EVT VT);
  /// Create an ExternalSymbol node.
  ///
  /// \param LCImpl Runtime-library call implementation.
  /// \param VT Value type.
  /// \return The ExternalSymbol node.
  LLVM_ABI SDValue getExternalSymbol(RTLIB::LibcallImpl LCImpl, EVT VT);
  /// Create a target ExternalSymbol node.
  ///
  /// \param Sym Symbol name or MCSymbol.
  /// \param VT Value type.
  /// \param TargetFlags Target-specific operand flags.
  /// \return The target ExternalSymbol node.
  LLVM_ABI SDValue getTargetExternalSymbol(const char *Sym, EVT VT,
                                           unsigned TargetFlags = 0);
  /// Create a target ExternalSymbol node.
  ///
  /// \param LCImpl Runtime-library call implementation.
  /// \param VT Value type.
  /// \param TargetFlags Target-specific operand flags.
  /// \return The target ExternalSymbol node.
  LLVM_ABI SDValue getTargetExternalSymbol(RTLIB::LibcallImpl LCImpl, EVT VT,
                                           unsigned TargetFlags = 0);

  /// Create an MCSymbol node.
  ///
  /// \param Sym Symbol name or MCSymbol.
  /// \param VT Value type.
  /// \return The MCSymbol node.
  LLVM_ABI SDValue getMCSymbol(MCSymbol *Sym, EVT VT);

  /// Create a VTSDNode for a value type.
  ///
  /// \param VT Value type.
  /// \return The VTSDNode for a value type.
  LLVM_ABI SDValue getValueType(EVT VT);
  /// Create a RegisterSDNode.
  ///
  /// \param Reg Register.
  /// \param VT Value type.
  /// \return The RegisterSDNode.
  LLVM_ABI SDValue getRegister(Register Reg, EVT VT);
  /// Create a RegisterMaskSDNode.
  ///
  /// \param RegMask Register mask bitset.
  /// \return The RegisterMaskSDNode.
  LLVM_ABI SDValue getRegisterMask(const uint32_t *RegMask);
  /// Create an EH_LABEL node.
  ///
  /// \param dl Debug location for the node.
  /// \param Root Root chain of the label.
  /// \param Label Machine-code label.
  /// \return The EH_LABEL node.
  LLVM_ABI SDValue getEHLabel(const SDLoc &dl, SDValue Root, MCSymbol *Label);
  /// Create a label node with the given opcode.
  ///
  /// \param Opcode ISD opcode.
  /// \param dl Debug location for the node.
  /// \param Root Root chain of the label.
  /// \param Label Machine-code label.
  /// \return The label node with the given opcode.
  LLVM_ABI SDValue getLabelNode(unsigned Opcode, const SDLoc &dl, SDValue Root,
                                MCSymbol *Label);
  /// Create a BlockAddress node.
  ///
  /// \param BA Block address.
  /// \param VT Value type.
  /// \param Offset Address offset.
  /// \param isTarget True to create a target node.
  /// \param TargetFlags Target-specific operand flags.
  /// \return The BlockAddress node.
  LLVM_ABI SDValue getBlockAddress(const BlockAddress *BA, EVT VT,
                                   int64_t Offset = 0, bool isTarget = false,
                                   unsigned TargetFlags = 0);
  /// Create a target BlockAddress node.
  ///
  /// \param BA Block address.
  /// \param VT Value type.
  /// \param Offset Address offset.
  /// \param TargetFlags Target-specific operand flags.
  /// \return The target BlockAddress node.
  SDValue getTargetBlockAddress(const BlockAddress *BA, EVT VT,
                                int64_t Offset = 0, unsigned TargetFlags = 0) {
    return getBlockAddress(BA, VT, Offset, true, TargetFlags);
  }

  /// Create a CopyToReg node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Reg Destination register.
  /// \param N Value copied into the register.
  /// \return The CopyToReg node.
  SDValue getCopyToReg(SDValue Chain, const SDLoc &dl, Register Reg,
                       SDValue N) {
    return getNode(ISD::CopyToReg, dl, MVT::Other, Chain,
                   getRegister(Reg, N.getValueType()), N);
  }

  // This version of the getCopyToReg method takes an extra operand, which
  // indicates that there is potentially an incoming glue value (if Glue is not
  // null) and that there should be a glue result.
  /// Create a CopyToReg node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Reg Destination register.
  /// \param N Value copied into the register.
  /// \param Glue Optional incoming glue value.
  /// \return The CopyToReg node.
  SDValue getCopyToReg(SDValue Chain, const SDLoc &dl, Register Reg, SDValue N,
                       SDValue Glue) {
    SDVTList VTs = getVTList(MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain, getRegister(Reg, N.getValueType()), N, Glue };
    return getNode(ISD::CopyToReg, dl, VTs,
                   ArrayRef(Ops, Glue.getNode() ? 4 : 3));
  }

  // Similar to last getCopyToReg() except parameter Reg is a SDValue
  /// Create a CopyToReg node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Reg Destination register value.
  /// \param N Value copied into the register.
  /// \param Glue Optional incoming glue value.
  /// \return The CopyToReg node.
  SDValue getCopyToReg(SDValue Chain, const SDLoc &dl, SDValue Reg, SDValue N,
                       SDValue Glue) {
    SDVTList VTs = getVTList(MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain, Reg, N, Glue };
    return getNode(ISD::CopyToReg, dl, VTs,
                   ArrayRef(Ops, Glue.getNode() ? 4 : 3));
  }

  /// Create a CopyFromReg node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Reg Register.
  /// \param VT Value type.
  /// \return The CopyFromReg node.
  SDValue getCopyFromReg(SDValue Chain, const SDLoc &dl, Register Reg, EVT VT) {
    SDVTList VTs = getVTList(VT, MVT::Other);
    SDValue Ops[] = { Chain, getRegister(Reg, VT) };
    return getNode(ISD::CopyFromReg, dl, VTs, Ops);
  }

  // This version of the getCopyFromReg method takes an extra operand, which
  // indicates that there is potentially an incoming glue value (if Glue is not
  // null) and that there should be a glue result.
  /// Create a CopyFromReg node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Reg Register.
  /// \param VT Value type.
  /// \param Glue Optional incoming glue value.
  /// \return The CopyFromReg node.
  SDValue getCopyFromReg(SDValue Chain, const SDLoc &dl, Register Reg, EVT VT,
                         SDValue Glue) {
    SDVTList VTs = getVTList(VT, MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain, getRegister(Reg, VT), Glue };
    return getNode(ISD::CopyFromReg, dl, VTs,
                   ArrayRef(Ops, Glue.getNode() ? 3 : 2));
  }

  /// Create a CondCodeSDNode for a condition code.
  ///
  /// \param Cond Condition code.
  /// \return The CondCodeSDNode for a condition code.
  LLVM_ABI SDValue getCondCode(ISD::CondCode Cond);

  /// Return an ISD::VECTOR_SHUFFLE node.
  ///
  /// The number of elements in VT, which must be a vector type, must match the
  /// number of mask elements. An integer mask element equal to -1 is treated as
  /// undefined.
  ///
  /// \param VT Value type.
  /// \param dl Debug location for the node.
  /// \param N1 First operand.
  /// \param N2 Second operand.
  /// \param Mask Shuffle mask.
  /// \return An ISD::VECTOR_SHUFFLE node.
  LLVM_ABI SDValue getVectorShuffle(EVT VT, const SDLoc &dl, SDValue N1,
                                    SDValue N2, ArrayRef<int> Mask);

  /// Return an ISD::BUILD_VECTOR node.
  ///
  /// The number of elements in VT, which must be a vector type, must match the
  /// number of operands in Ops. The operands must have the same type as (or,
  /// for integers, a type wider than) VT's element type.
  ///
  /// \param VT Value type.
  /// \param DL Debug location for the node.
  /// \param Ops Operand list.
  /// \return An ISD::BUILD_VECTOR node.
  SDValue getBuildVector(EVT VT, const SDLoc &DL, ArrayRef<SDValue> Ops) {
    // VerifySDNode (via InsertNode) checks BUILD_VECTOR later.
    return getNode(ISD::BUILD_VECTOR, DL, VT, Ops);
  }

  /// Return an ISD::BUILD_VECTOR node.
  ///
  /// The number of elements in VT, which must be a vector type, must match the
  /// number of operands in Ops. The operands must have the same type as (or,
  /// for integers, a type wider than) VT's element type.
  ///
  /// \param VT Value type.
  /// \param DL Debug location for the node.
  /// \param Ops Operand list.
  /// \return An ISD::BUILD_VECTOR node.
  SDValue getBuildVector(EVT VT, const SDLoc &DL, ArrayRef<SDUse> Ops) {
    // VerifySDNode (via InsertNode) checks BUILD_VECTOR later.
    return getNode(ISD::BUILD_VECTOR, DL, VT, Ops);
  }

  /// Return a splat ISD::BUILD_VECTOR node.
  ///
  /// Return a splat ISD::BUILD_VECTOR node, consisting of Op splatted to all
  /// elements. VT must be a vector type. Op's type must be the same as (or,
  /// for integers, a type wider than) VT's element type.
  ///
  /// \param VT Value type.
  /// \param DL Debug location for the node.
  /// \param Op Operand value.
  /// \return A splat ISD::BUILD_VECTOR node.
  SDValue getSplatBuildVector(EVT VT, const SDLoc &DL, SDValue Op) {
    // VerifySDNode (via InsertNode) checks BUILD_VECTOR later.
    if (Op.isUndef()) {
      assert((VT.getVectorElementType() == Op.getValueType() ||
              (VT.isInteger() &&
               VT.getVectorElementType().bitsLE(Op.getValueType()))) &&
             "A splatted value must have a width equal or (for integers) "
             "greater than the vector element type!");
      return getNode(ISD::UNDEF, SDLoc(), VT);
    }

    SmallVector<SDValue, 16> Ops(VT.getVectorNumElements(), Op);
    return getNode(ISD::BUILD_VECTOR, DL, VT, Ops);
  }

  /// Return a splat ISD::SPLAT_VECTOR node.
  ///
  /// \param VT Value type.
  /// \param DL Debug location for the node.
  /// \param Op Operand value.
  /// \return A splat ISD::SPLAT_VECTOR node.
  SDValue getSplatVector(EVT VT, const SDLoc &DL, SDValue Op) {
    if (Op.isUndef()) {
      assert((VT.getVectorElementType() == Op.getValueType() ||
              (VT.isInteger() &&
               VT.getVectorElementType().bitsLE(Op.getValueType()))) &&
             "A splatted value must have a width equal or (for integers) "
             "greater than the vector element type!");
      return getNode(ISD::UNDEF, SDLoc(), VT);
    }
    return getNode(ISD::SPLAT_VECTOR, DL, VT, Op);
  }

  /// Return a splat of one value into all lanes of a vector type.
  ///
  /// Returns a node representing a splat of one value into all lanes
  /// of the provided vector type.  This is a utility which returns
  /// either a BUILD_VECTOR or SPLAT_VECTOR depending on the
  /// scalability of the desired vector type.
  ///
  /// \param VT Value type.
  /// \param DL Debug location for the node.
  /// \param Op Operand value.
  /// \return A splat of one value into all lanes of a vector type.
  SDValue getSplat(EVT VT, const SDLoc &DL, SDValue Op) {
    assert(VT.isVector() && "Can't splat to non-vector type");
    return VT.isScalableVector() ?
      getSplatVector(VT, DL, Op) : getSplatBuildVector(VT, DL, Op);
  }

  /// Return a vector of a linear step sequence.
  ///
  /// Returns a vector of type ResVT whose elements contain the linear sequence
  ///   <0, Step, Step * 2, Step * 3, ...>
  ///
  /// \param DL Debug location for the node.
  /// \param ResVT Result vector type.
  /// \param StepVal Step between consecutive elements.
  /// \return A vector of a linear step sequence.
  LLVM_ABI SDValue getStepVector(const SDLoc &DL, EVT ResVT,
                                 const APInt &StepVal);

  /// Return a vector of a linear step sequence.
  ///
  /// Returns a vector of type ResVT whose elements contain the linear sequence
  ///   <0, 1, 2, 3, ...>
  ///
  /// \param DL Debug location for the node.
  /// \param ResVT Result vector type.
  /// \return A vector of a linear step sequence.
  LLVM_ABI SDValue getStepVector(const SDLoc &DL, EVT ResVT);

  /// Returns an ISD::VECTOR_SHUFFLE node semantically equivalent to
  /// the shuffle node in input but with swapped operands.
  ///
  /// Return a VECTOR_SHUFFLE with swapped operands.
  ///
  /// Example: shuffle A, B, <0,5,2,7> -> shuffle B, A, <4,1,6,3>
  ///
  /// \param SV Shuffle node to commute.
  /// \return An ISD::VECTOR_SHUFFLE node semantically equivalent to the shuffle node in input but with swapped operands.
  LLVM_ABI SDValue getCommutedVectorShuffle(const ShuffleVectorSDNode &SV);

  /// Extract an element from a vector.
  ///
  /// Extract element at \p Idx from \p Vec.  See EXTRACT_VECTOR_ELT
  /// description for result type handling.
  ///
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param Vec Source vector.
  /// \param Idx Element or subvector index.
  /// \return Extract an element from a vector.
  SDValue getExtractVectorElt(const SDLoc &DL, EVT VT, SDValue Vec,
                              unsigned Idx) {
    return getNode(ISD::EXTRACT_VECTOR_ELT, DL, VT, Vec,
                   getVectorIdxConstant(Idx, DL));
  }

  /// Insert an element into a vector.
  ///
  /// Insert \p Elt into \p Vec at offset \p Idx.  See INSERT_VECTOR_ELT
  /// description for element type handling.
  ///
  /// \param DL Debug location for the node.
  /// \param Vec Source vector.
  /// \param Elt Inserted element.
  /// \param Idx Element or subvector index.
  /// \return Insert an element into a vector.
  SDValue getInsertVectorElt(const SDLoc &DL, SDValue Vec, SDValue Elt,
                             unsigned Idx) {
    return getNode(ISD::INSERT_VECTOR_ELT, DL, Vec.getValueType(), Vec, Elt,
                   getVectorIdxConstant(Idx, DL));
  }

  /// Insert a subvector into a vector.
  ///
  /// Insert \p SubVec at the \p Idx element of \p Vec.
  ///
  /// \param DL Debug location for the node.
  /// \param Vec Source vector.
  /// \param SubVec Inserted subvector.
  /// \param Idx Element or subvector index.
  /// \return Insert a subvector into a vector.
  SDValue getInsertSubvector(const SDLoc &DL, SDValue Vec, SDValue SubVec,
                             unsigned Idx) {
    return getNode(ISD::INSERT_SUBVECTOR, DL, Vec.getValueType(), Vec, SubVec,
                   getVectorIdxConstant(Idx, DL));
  }

  /// Extract a typed subvector from a vector.
  ///
  /// Return the \p VT typed sub-vector of \p Vec at \p Idx
  ///
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param Vec Source vector.
  /// \param Idx Element or subvector index.
  /// \return The \p VT typed sub-vector of \p Vec at \p Idx.
  SDValue getExtractSubvector(const SDLoc &DL, EVT VT, SDValue Vec,
                              unsigned Idx) {
    return getNode(ISD::EXTRACT_SUBVECTOR, DL, VT, Vec,
                   getVectorIdxConstant(Idx, DL));
  }

  /// Convert a float to another float by extending or rounding.
  ///
  /// Convert Op, which must be of float type, to the
  /// float type VT, by either extending or rounding (by truncation).
  ///
  /// \param Op Operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return The FP extend or round node.
  LLVM_ABI SDValue getFPExtendOrRound(SDValue Op, const SDLoc &DL, EVT VT);

  /// Convert a strict float op to another float by extending or rounding.
  ///
  /// Convert Op, which must be a STRICT operation of float type, to the
  /// float type VT, by either extending or rounding (by truncation).
  ///
  /// \param Op Operand value.
  /// \param Chain Incoming token chain.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return The strict FP extend or round node.
  LLVM_ABI std::pair<SDValue, SDValue>
  getStrictFPExtendOrRound(SDValue Op, SDValue Chain, const SDLoc &DL, EVT VT);

  /// Convert *_EXTEND_VECTOR_INREG to the matching *_EXTEND opcode.
  ///
  /// Convert *_EXTEND_VECTOR_INREG to *_EXTEND opcode.
  ///
  /// \param Opcode ISD opcode.
  /// \return The ISD opcode for the requested extend kind.
  static unsigned getOpcode_EXTEND(unsigned Opcode) {
    switch (Opcode) {
    case ISD::ANY_EXTEND:
    case ISD::ANY_EXTEND_VECTOR_INREG:
      return ISD::ANY_EXTEND;
    case ISD::ZERO_EXTEND:
    case ISD::ZERO_EXTEND_VECTOR_INREG:
      return ISD::ZERO_EXTEND;
    case ISD::SIGN_EXTEND:
    case ISD::SIGN_EXTEND_VECTOR_INREG:
      return ISD::SIGN_EXTEND;
    }
    llvm_unreachable("Unknown opcode");
  }

  /// Convert *_EXTEND to the matching *_EXTEND_VECTOR_INREG opcode.
  ///
  /// Convert *_EXTEND to *_EXTEND_VECTOR_INREG opcode.
  ///
  /// \param Opcode ISD opcode.
  /// \return The ISD opcode for the requested extend-vector-inreg kind.
  static unsigned getOpcode_EXTEND_VECTOR_INREG(unsigned Opcode) {
    switch (Opcode) {
    case ISD::ANY_EXTEND:
    case ISD::ANY_EXTEND_VECTOR_INREG:
      return ISD::ANY_EXTEND_VECTOR_INREG;
    case ISD::ZERO_EXTEND:
    case ISD::ZERO_EXTEND_VECTOR_INREG:
      return ISD::ZERO_EXTEND_VECTOR_INREG;
    case ISD::SIGN_EXTEND:
    case ISD::SIGN_EXTEND_VECTOR_INREG:
      return ISD::SIGN_EXTEND_VECTOR_INREG;
    }
    llvm_unreachable("Unknown opcode");
  }

  /// Any-extend or truncate an integer to VT.
  ///
  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by either any-extending or truncating it.
  ///
  /// \param Op Operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return Any-extend or truncate an integer to VT.
  LLVM_ABI SDValue getAnyExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Sign-extend or truncate an integer to VT.
  ///
  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by either sign-extending or truncating it.
  ///
  /// \param Op Operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return Sign-extend or truncate an integer to VT.
  LLVM_ABI SDValue getSExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Zero-extend or truncate an integer to VT.
  ///
  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by either zero-extending or truncating it.
  ///
  /// \param Op Operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return Zero-extend or truncate an integer to VT.
  LLVM_ABI SDValue getZExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Extend or truncate an integer to VT.
  ///
  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by either any/sign/zero-extending (depending on IsAny /
  /// IsSigned) or truncating it.
  ///
  /// \param Op Operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param Opcode ISD opcode.
  /// \return Extend or truncate an integer to VT.
  SDValue getExtOrTrunc(SDValue Op, const SDLoc &DL,
                        EVT VT, unsigned Opcode) {
    switch(Opcode) {
      case ISD::ANY_EXTEND:
        return getAnyExtOrTrunc(Op, DL, VT);
      case ISD::ZERO_EXTEND:
        return getZExtOrTrunc(Op, DL, VT);
      case ISD::SIGN_EXTEND:
        return getSExtOrTrunc(Op, DL, VT);
    }
    llvm_unreachable("Unsupported opcode");
  }

  /// Extend or truncate an integer to VT.
  ///
  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by either sign/zero-extending (depending on IsSigned) or
  /// truncating it.
  ///
  /// \param IsSigned True to sign-extend rather than zero-extend.
  /// \param Op Operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return Extend or truncate an integer to VT.
  SDValue getExtOrTrunc(bool IsSigned, SDValue Op, const SDLoc &DL, EVT VT) {
    return IsSigned ? getSExtOrTrunc(Op, DL, VT) : getZExtOrTrunc(Op, DL, VT);
  }

  /// Bitcast then any-extend or truncate an integer to VT.
  ///
  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by first bitcasting (from potential vector) to
  /// corresponding scalar type then either any-extending or truncating it.
  ///
  /// \param Op Operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return Bitcast then any-extend or truncate an integer to VT.
  LLVM_ABI SDValue getBitcastedAnyExtOrTrunc(SDValue Op, const SDLoc &DL,
                                             EVT VT);

  /// Bitcast then sign-extend or truncate an integer to VT.
  ///
  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by first bitcasting (from potential vector) to
  /// corresponding scalar type then either sign-extending or truncating it.
  ///
  /// \param Op Operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return Bitcast then sign-extend or truncate an integer to VT.
  LLVM_ABI SDValue getBitcastedSExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Bitcast then zero-extend or truncate an integer to VT.
  ///
  /// Convert Op, which must be of integer type, to the
  /// integer type VT, by first bitcasting (from potential vector) to
  /// corresponding scalar type then either zero-extending or truncating it.
  ///
  /// \param Op Operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return Bitcast then zero-extend or truncate an integer to VT.
  LLVM_ABI SDValue getBitcastedZExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Zero-extend Op in register from a narrower type.
  ///
  /// Return the expression required to zero extend the Op
  /// value assuming it was the smaller SrcTy value.
  ///
  /// \param Op Operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return The expression required to zero extend the Op value assuming it was the smaller SrcTy value.
  LLVM_ABI SDValue getZeroExtendInReg(SDValue Op, const SDLoc &DL, EVT VT);

  /// Extend or truncate an integer as a pointer.
  ///
  /// Convert Op, which must be of integer type, to the integer type VT, by
  /// either truncating it or performing either zero or sign extension as
  /// appropriate extension for the pointer's semantics.
  ///
  /// \param Op Operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return Extend or truncate an integer as a pointer.
  LLVM_ABI SDValue getPtrExtOrTrunc(SDValue Op, const SDLoc &DL, EVT VT);

  /// Extend Op in register as a pointer from a narrower type.
  ///
  /// Return the expression required to extend the Op as a pointer value
  /// assuming it was the smaller SrcTy value. This may be either a zero extend
  /// or a sign extend.
  ///
  /// \param Op Operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return The expression required to extend the Op as a pointer value assuming it was the smaller SrcTy value.
  LLVM_ABI SDValue getPtrExtendInReg(SDValue Op, const SDLoc &DL, EVT VT);

  /// Extend or truncate an integer using BooleanContent.
  ///
  /// Convert Op, which must be of integer type, to the integer type VT,
  /// by using an extension appropriate for the target's
  /// BooleanContent for type OpVT or truncating it.
  ///
  /// \param Op Operand value.
  /// \param SL Source location for the node.
  /// \param VT Value type.
  /// \param OpVT Original operand type.
  /// \return Extend or truncate an integer using BooleanContent.
  LLVM_ABI SDValue getBoolExtOrTrunc(SDValue Op, const SDLoc &SL, EVT VT,
                                     EVT OpVT);

  /// Create a negate as (SUB 0, Val).
  ///
  /// Create negative operation as (SUB 0, Val).
  ///
  /// \param Val Constant or operand value.
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \return The negate as (SUB 0, Val).
  LLVM_ABI SDValue getNegative(SDValue Val, const SDLoc &DL, EVT VT);

  /// Create a bitwise NOT as (XOR Val, -1).
  ///
  /// Create a bitwise NOT operation as (XOR Val, -1).
  ///
  /// \param DL Debug location for the node.
  /// \param Val Constant or operand value.
  /// \param VT Value type.
  /// \return The bitwise NOT as (XOR Val, -1).
  LLVM_ABI SDValue getNOT(const SDLoc &DL, SDValue Val, EVT VT);

  /// Create a logical NOT as (XOR Val, BooleanOne).
  ///
  /// Create a logical NOT operation as (XOR Val, BooleanOne).
  ///
  /// \param DL Debug location for the node.
  /// \param Val Constant or operand value.
  /// \param VT Value type.
  /// \return The logical NOT as (XOR Val, BooleanOne).
  LLVM_ABI SDValue getLogicalNOT(const SDLoc &DL, SDValue Val, EVT VT);

  /// Return the sum of a base pointer and an offset.
  ///
  /// Returns sum of the base pointer and offset.
  /// Unlike getObjectPtrOffset this does not set NoUnsignedWrap and InBounds by
  /// default.
  ///
  /// \param Base Base pointer.
  /// \param Offset Address offset.
  /// \param DL Debug location for the node.
  /// \param Flags Node flags.
  /// \return The sum of a base pointer and an offset.
  LLVM_ABI SDValue
  getMemBasePlusOffset(SDValue Base, TypeSize Offset, const SDLoc &DL,
                       const SDNodeFlags Flags = SDNodeFlags());
  /// Return the sum of a base pointer and an offset.
  ///
  /// \param Base Base pointer.
  /// \param Offset Address offset.
  /// \param DL Debug location for the node.
  /// \param Flags Node flags.
  /// \return The sum of a base pointer and an offset.
  LLVM_ABI SDValue
  getMemBasePlusOffset(SDValue Base, SDValue Offset, const SDLoc &DL,
                       const SDNodeFlags Flags = SDNodeFlags());

  /// Create an inbounds nuw add of a pointer and an object offset.
  ///
  /// Create an add instruction with appropriate flags when used for
  /// addressing some offset of an object. i.e. if a load is split into multiple
  /// components, create an add nuw (or ptradd nuw inbounds) from the base
  /// pointer to the offset.
  ///
  /// \param SL Source location for the node.
  /// \param Ptr Pointer operand.
  /// \param Offset Address offset.
  /// \return The inbounds nuw add of a pointer and an object offset.
  SDValue getObjectPtrOffset(const SDLoc &SL, SDValue Ptr, TypeSize Offset) {
    return getMemBasePlusOffset(
        Ptr, Offset, SL, SDNodeFlags::NoUnsignedWrap | SDNodeFlags::InBounds);
  }

  /// Create an inbounds nuw add of a pointer and an object offset.
  ///
  /// \param SL Source location for the node.
  /// \param Ptr Pointer operand.
  /// \param Offset Address offset.
  /// \return The inbounds nuw add of a pointer and an object offset.
  SDValue getObjectPtrOffset(const SDLoc &SL, SDValue Ptr, SDValue Offset) {
    // The object itself can't wrap around the address space, so it shouldn't be
    // possible for the adds of the offsets to the split parts to overflow.
    return getMemBasePlusOffset(
        Ptr, Offset, SL, SDNodeFlags::NoUnsignedWrap | SDNodeFlags::InBounds);
  }

  /// Return a CALLSEQ_START node that begins a call frame.
  ///
  /// Return a new CALLSEQ_START node, that starts new call frame, in which
  /// InSize bytes are set up inside CALLSEQ_START..CALLSEQ_END sequence and
  /// OutSize specifies part of the frame set up prior to the sequence.
  ///
  /// \param Chain Incoming token chain.
  /// \param InSize Bytes allocated inside the call sequence.
  /// \param OutSize Bytes allocated before the call sequence.
  /// \param DL Debug location for the node.
  /// \return A CALLSEQ_START node that begins a call frame.
  SDValue getCALLSEQ_START(SDValue Chain, uint64_t InSize, uint64_t OutSize,
                           const SDLoc &DL) {
    SDVTList VTs = getVTList(MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain,
                      getIntPtrConstant(InSize, DL, true),
                      getIntPtrConstant(OutSize, DL, true) };
    return getNode(ISD::CALLSEQ_START, DL, VTs, Ops);
  }

  /// Return a CALLSEQ_END node, which always has a glue result.
  ///
  /// Return a new CALLSEQ_END node, which always must have a
  /// glue result (to ensure it's not CSE'd).
  /// CALLSEQ_END does not have a useful SDLoc.
  ///
  /// \param Chain Incoming token chain.
  /// \param Op1 First operand.
  /// \param Op2 Second operand.
  /// \param InGlue Optional incoming glue.
  /// \param DL Debug location for the node.
  /// \return A CALLSEQ_END node, which always has a glue result.
  SDValue getCALLSEQ_END(SDValue Chain, SDValue Op1, SDValue Op2,
                         SDValue InGlue, const SDLoc &DL) {
    SDVTList NodeTys = getVTList(MVT::Other, MVT::Glue);
    SmallVector<SDValue, 4> Ops;
    Ops.push_back(Chain);
    Ops.push_back(Op1);
    Ops.push_back(Op2);
    if (InGlue.getNode())
      Ops.push_back(InGlue);
    return getNode(ISD::CALLSEQ_END, DL, NodeTys, Ops);
  }

  /// Return a CALLSEQ_END node, which always has a glue result.
  ///
  /// \param Chain Incoming token chain.
  /// \param Size1 First size in bytes.
  /// \param Size2 Second size in bytes.
  /// \param Glue Optional incoming glue value.
  /// \param DL Debug location for the node.
  /// \return A CALLSEQ_END node, which always has a glue result.
  SDValue getCALLSEQ_END(SDValue Chain, uint64_t Size1, uint64_t Size2,
                         SDValue Glue, const SDLoc &DL) {
    return getCALLSEQ_END(
        Chain, getIntPtrConstant(Size1, DL, /*isTarget=*/true),
        getIntPtrConstant(Size2, DL, /*isTarget=*/true), Glue, DL);
  }

  /// Return true if the result of this operation is always undefined.
  ///
  /// \param Opcode ISD opcode.
  /// \param Ops Operand list.
  /// \return True if the result of this operation is always undefined.
  LLVM_ABI bool isUndef(unsigned Opcode, ArrayRef<SDValue> Ops);

  /// Return an UNDEF node.
  ///
  /// \param VT Value type.
  /// \return An UNDEF node.
  SDValue getUNDEF(EVT VT) {
    return getNode(ISD::UNDEF, SDLoc(), VT);
  }

  /// Return a POISON node.
  ///
  /// \param VT Value type.
  /// \return A POISON node.
  SDValue getPOISON(EVT VT) { return getNode(ISD::POISON, SDLoc(), VT); }

  /// Return a node for the runtime scaling MulImm * RuntimeVL.
  ///
  /// Return a node that represents the runtime scaling 'MulImm * RuntimeVL'.
  ///
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param MulImm Runtime VL scale factor.
  /// \return A node for the runtime scaling MulImm * RuntimeVL.
  LLVM_ABI SDValue getVScale(const SDLoc &DL, EVT VT, APInt MulImm);

  /// Return a node for an ElementCount scaled by the runtime VL.
  ///
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param EC Element count.
  /// \return A node for an ElementCount scaled by the runtime VL.
  LLVM_ABI SDValue getElementCount(const SDLoc &DL, EVT VT, ElementCount EC);

  /// Return a node for a TypeSize scaled by the runtime VL.
  ///
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param TS Type size.
  /// \return A node for a TypeSize scaled by the runtime VL.
  LLVM_ABI SDValue getTypeSize(const SDLoc &DL, EVT VT, TypeSize TS);

  /// Return a mask with the first Len lanes true.
  ///
  /// Return a vector with the first 'Len' lanes set to true and remaining lanes
  /// set to false. The mask's ValueType is the same as when comparing vectors
  /// of type VT.
  ///
  /// \param DL Debug location for the node.
  /// \param VT Value type.
  /// \param Len Number of true mask lanes.
  /// \return A mask with the first Len lanes true.
  LLVM_ABI SDValue getMaskFromElementCount(const SDLoc &DL, EVT VT,
                                           ElementCount Len);

  /// Return a GLOBAL_OFFSET_TABLE node.
  ///
  /// \param VT Value type.
  /// \return A GLOBAL_OFFSET_TABLE node.
  SDValue getGLOBAL_OFFSET_TABLE(EVT VT) {
    return getNode(ISD::GLOBAL_OFFSET_TABLE, SDLoc(), VT);
  }

  /// Get or create a DAG node with operand uses.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param Ops Operand uses for the node.
  /// \return Or create a DAG node with operand uses.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT,
                           ArrayRef<SDUse> Ops);
  /// Get or create a DAG node with operands and explicit flags.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param Ops Operand values for the node.
  /// \param Flags Node flags for the operation.
  /// \return Or create a DAG node with operands and explicit flags.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT,
                           ArrayRef<SDValue> Ops, const SDNodeFlags Flags);
  /// Get or create a DAG node with multiple result types and flags.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param ResultTys Result value types.
  /// \param Ops Operand values for the node.
  /// \param Flags Node flags for the operation.
  /// \return Or create a DAG node with multiple result types and flags.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL,
                           ArrayRef<EVT> ResultTys, ArrayRef<SDValue> Ops,
                           const SDNodeFlags Flags);
  /// Get or create a DAG node with an SDVTList of results and flags.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VTList Result value types.
  /// \param Ops Operand values for the node.
  /// \param Flags Node flags for the operation.
  /// \return Or create a DAG node with an SDVTList of results and flags.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList,
                           ArrayRef<SDValue> Ops, const SDNodeFlags Flags);

  /// Get or create a DAG node, taking flags from the current flag inserter.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param Ops Operand values for the node.
  /// \return Or create a DAG node, taking flags from the current flag inserter.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT,
                           ArrayRef<SDValue> Ops);
  /// Get or create a DAG node with multiple result types.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param ResultTys Result value types.
  /// \param Ops Operand values for the node.
  /// \return Or create a DAG node with multiple result types.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL,
                           ArrayRef<EVT> ResultTys, ArrayRef<SDValue> Ops);
  /// Get or create a DAG node with an SDVTList of results.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VTList Result value types.
  /// \param Ops Operand values for the node.
  /// \return Or create a DAG node with an SDVTList of results.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList,
                           ArrayRef<SDValue> Ops);
  /// Get or create a DAG node with one operand.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param Operand Single operand.
  /// \return Or create a DAG node with one operand.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT,
                           SDValue Operand);
  /// Get or create a DAG node with two operands.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param N1 First operand.
  /// \param N2 Second operand.
  /// \return Or create a DAG node with two operands.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                           SDValue N2);
  /// Get or create a DAG node with three operands.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param N1 First operand.
  /// \param N2 Second operand.
  /// \param N3 Third operand.
  /// \return Or create a DAG node with three operands.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                           SDValue N2, SDValue N3);

  /// Get or create a DAG node with no operands.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \return Or create a DAG node with no operands.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT);
  /// Get or create a DAG node with one operand and explicit flags.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param Operand Single operand.
  /// \param Flags Node flags for the operation.
  /// \return Or create a DAG node with one operand and explicit flags.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT,
                           SDValue Operand, const SDNodeFlags Flags);
  /// Get or create a DAG node with two operands and explicit flags.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param N1 First operand.
  /// \param N2 Second operand.
  /// \param Flags Node flags for the operation.
  /// \return Or create a DAG node with two operands and explicit flags.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                           SDValue N2, const SDNodeFlags Flags);
  /// Get or create a DAG node with three operands and explicit flags.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param N1 First operand.
  /// \param N2 Second operand.
  /// \param N3 Third operand.
  /// \param Flags Node flags for the operation.
  /// \return Or create a DAG node with three operands and explicit flags.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                           SDValue N2, SDValue N3, const SDNodeFlags Flags);
  /// Get or create a DAG node with four operands.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param N1 First operand.
  /// \param N2 Second operand.
  /// \param N3 Third operand.
  /// \param N4 Fourth operand.
  /// \return Or create a DAG node with four operands.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                           SDValue N2, SDValue N3, SDValue N4);
  /// Get or create a DAG node with four operands and explicit flags.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param N1 First operand.
  /// \param N2 Second operand.
  /// \param N3 Third operand.
  /// \param N4 Fourth operand.
  /// \param Flags Node flags for the operation.
  /// \return Or create a DAG node with four operands and explicit flags.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                           SDValue N2, SDValue N3, SDValue N4,
                           const SDNodeFlags Flags);
  /// Get or create a DAG node with five operands.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param N1 First operand.
  /// \param N2 Second operand.
  /// \param N3 Third operand.
  /// \param N4 Fourth operand.
  /// \param N5 Fifth operand.
  /// \return Or create a DAG node with five operands.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                           SDValue N2, SDValue N3, SDValue N4, SDValue N5);
  /// Get or create a DAG node with five operands and explicit flags.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param N1 First operand.
  /// \param N2 Second operand.
  /// \param N3 Third operand.
  /// \param N4 Fourth operand.
  /// \param N5 Fifth operand.
  /// \param Flags Node flags for the operation.
  /// \return Or create a DAG node with five operands and explicit flags.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, EVT VT, SDValue N1,
                           SDValue N2, SDValue N3, SDValue N4, SDValue N5,
                           const SDNodeFlags Flags);

  /// Get or create a DAG node with an SDVTList and no operands.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VTList Result value types.
  /// \return Or create a DAG node with an SDVTList and no operands.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList);
  /// Get or create a DAG node with an SDVTList and one operand.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VTList Result value types.
  /// \param N Single operand.
  /// \return Or create a DAG node with an SDVTList and one operand.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList,
                           SDValue N);
  /// Get or create a DAG node with an SDVTList and two operands.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VTList Result value types.
  /// \param N1 First operand.
  /// \param N2 Second operand.
  /// \return Or create a DAG node with an SDVTList and two operands.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList,
                           SDValue N1, SDValue N2);
  /// Get or create a DAG node with an SDVTList and three operands.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VTList Result value types.
  /// \param N1 First operand.
  /// \param N2 Second operand.
  /// \param N3 Third operand.
  /// \return Or create a DAG node with an SDVTList and three operands.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList,
                           SDValue N1, SDValue N2, SDValue N3);
  /// Get or create a DAG node with an SDVTList and four operands.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VTList Result value types.
  /// \param N1 First operand.
  /// \param N2 Second operand.
  /// \param N3 Third operand.
  /// \param N4 Fourth operand.
  /// \return Or create a DAG node with an SDVTList and four operands.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList,
                           SDValue N1, SDValue N2, SDValue N3, SDValue N4);
  /// Get or create a DAG node with an SDVTList and five operands.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param DL Debug location for the node.
  /// \param VTList Result value types.
  /// \param N1 First operand.
  /// \param N2 Second operand.
  /// \param N3 Third operand.
  /// \param N4 Fourth operand.
  /// \param N5 Fifth operand.
  /// \return Or create a DAG node with an SDVTList and five operands.
  LLVM_ABI SDValue getNode(unsigned Opcode, const SDLoc &DL, SDVTList VTList,
                           SDValue N1, SDValue N2, SDValue N3, SDValue N4,
                           SDValue N5);

  /// Compute a TokenFactor that forces incoming stack arguments to be loaded.
  ///
  /// Used in tail call lowering to protect stack arguments from being
  /// clobbered.
  ///
  /// \param Chain Incoming token chain.
  /// \return A TokenFactor that forces incoming stack arguments to be loaded.
  LLVM_ABI SDValue getStackArgumentTokenFactor(SDValue Chain);

  /// Lower a memccpy operation into a target library call and return the
  /// resulting chain and call result as SelectionDAG SDValues.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the call.
  /// \param Dst Destination pointer.
  /// \param Src Source pointer.
  /// \param C Stop character.
  /// \param Size Maximum number of bytes to copy.
  /// \param CI Original call instruction, if any.
  /// \return The chain and call result of the memccpy.
  LLVM_ABI std::pair<SDValue, SDValue>
  getMemccpy(SDValue Chain, const SDLoc &dl, SDValue Dst, SDValue Src,
             SDValue C, SDValue Size, const CallInst *CI);

  /// Lower a memcmp operation into a target library call and return the
  /// resulting chain and call result as SelectionDAG SDValues.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the call.
  /// \param Dst First memory operand.
  /// \param Src Second memory operand.
  /// \param Size Number of bytes to compare.
  /// \param CI Original call instruction, if any.
  /// \return The chain and call result of the memcmp.
  LLVM_ABI std::pair<SDValue, SDValue> getMemcmp(SDValue Chain, const SDLoc &dl,
                                                 SDValue Dst, SDValue Src,
                                                 SDValue Size,
                                                 const CallInst *CI);

  /// Lower a strcmp operation into a target library call and return the
  /// resulting chain and call result as SelectionDAG SDValues.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the call.
  /// \param S0 First string pointer.
  /// \param S1 Second string pointer.
  /// \param CI Original call instruction, if any.
  /// \return The chain and call result of the strcmp.
  LLVM_ABI std::pair<SDValue, SDValue> getStrcmp(SDValue Chain, const SDLoc &dl,
                                                 SDValue S0, SDValue S1,
                                                 const CallInst *CI);

  /// Lower a strcpy operation into a target library call and return the
  /// resulting chain and call result as SelectionDAG SDValues.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the call.
  /// \param Dst Destination string pointer.
  /// \param Src Source string pointer.
  /// \param CI Original call instruction, if any.
  /// \return The chain and call result of the strcpy.
  LLVM_ABI std::pair<SDValue, SDValue> getStrcpy(SDValue Chain, const SDLoc &dl,
                                                 SDValue Dst, SDValue Src,
                                                 const CallInst *CI);

  /// Lower a strlen operation into a target library call and return the
  /// resulting chain and call result as SelectionDAG SDValues.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the call.
  /// \param Src String pointer.
  /// \param CI Original call instruction, if any.
  /// \return The chain and call result of the strlen.
  LLVM_ABI std::pair<SDValue, SDValue>
  getStrlen(SDValue Chain, const SDLoc &dl, SDValue Src, const CallInst *CI);

  /// Lower a strstr operation into a target library call and return the
  /// resulting chain and call result as SelectionDAG SDValues.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the call.
  /// \param S0 Haystack string pointer.
  /// \param S1 Needle string pointer.
  /// \param CI Original call instruction, if any.
  /// \return The chain and call result of the strstr.
  LLVM_ABI std::pair<SDValue, SDValue> getStrstr(SDValue Chain, const SDLoc &dl,
                                                 SDValue S0, SDValue S1,
                                                 const CallInst *CI);

  /// Lower memcpy into DAG nodes or a library call.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the copy.
  /// \param Dst Destination pointer.
  /// \param Src Source pointer.
  /// \param Size Number of bytes to copy.
  /// \param DstAlign Destination alignment.
  /// \param SrcAlign Source alignment.
  /// \param isVol True if the copy is volatile.
  /// \param AlwaysInline True to force an inline expansion.
  /// \param CI Original call instruction, if any.
  /// \param OverrideTailCall Optional override of the tail-call decision.
  /// \param DstPtrInfo Pointer info for the destination.
  /// \param SrcPtrInfo Pointer info for the source.
  /// \param AAInfo Alias-analysis metadata.
  /// \param BatchAA Optional batch alias-analysis results.
  /// \return The chain after the memcpy.
  LLVM_ABI SDValue getMemcpy(
      SDValue Chain, const SDLoc &dl, SDValue Dst, SDValue Src, SDValue Size,
      Align DstAlign, Align SrcAlign, bool isVol, bool AlwaysInline,
      const CallInst *CI, std::optional<bool> OverrideTailCall,
      MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo,
      const AAMDNodes &AAInfo = AAMDNodes(), BatchAAResults *BatchAA = nullptr);

  /// Lower memmove into DAG nodes or a library call.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the move.
  /// \param Dst Destination pointer.
  /// \param Src Source pointer.
  /// \param Size Number of bytes to move.
  /// \param DstAlign Destination alignment.
  /// \param SrcAlign Source alignment.
  /// \param isVol True if the move is volatile.
  /// \param CI Original call instruction, if any.
  /// \param OverrideTailCall Optional override of the tail-call decision.
  /// \param DstPtrInfo Pointer info for the destination.
  /// \param SrcPtrInfo Pointer info for the source.
  /// \param AAInfo Alias-analysis metadata.
  /// \param BatchAA Optional batch alias-analysis results.
  /// \return The chain after the memmove.
  LLVM_ABI SDValue getMemmove(SDValue Chain, const SDLoc &dl, SDValue Dst,
                              SDValue Src, SDValue Size, Align DstAlign,
                              Align SrcAlign, bool isVol, const CallInst *CI,
                              std::optional<bool> OverrideTailCall,
                              MachinePointerInfo DstPtrInfo,
                              MachinePointerInfo SrcPtrInfo,
                              const AAMDNodes &AAInfo = AAMDNodes(),
                              BatchAAResults *BatchAA = nullptr);

  /// Lower memset into DAG nodes or a library call.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the set.
  /// \param Dst Destination pointer.
  /// \param Src Fill value.
  /// \param Size Number of bytes to set.
  /// \param Alignment Destination alignment.
  /// \param isVol True if the set is volatile.
  /// \param AlwaysInline True to force an inline expansion.
  /// \param CI Original call instruction, if any.
  /// \param DstPtrInfo Pointer info for the destination.
  /// \param AAInfo Alias-analysis metadata.
  /// \return The chain after the memset.
  LLVM_ABI SDValue getMemset(SDValue Chain, const SDLoc &dl, SDValue Dst,
                             SDValue Src, SDValue Size, Align Alignment,
                             bool isVol, bool AlwaysInline, const CallInst *CI,
                             MachinePointerInfo DstPtrInfo,
                             const AAMDNodes &AAInfo = AAMDNodes());

  /// Create an atomic memcpy operation.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the copy.
  /// \param Dst Destination pointer.
  /// \param Src Source pointer.
  /// \param Size Number of bytes to copy.
  /// \param SizeTy Type of the size operand.
  /// \param ElemSz Element size used by the atomic copy.
  /// \param isTailCall True if the copy may be a tail call.
  /// \param DstPtrInfo Pointer info for the destination.
  /// \param SrcPtrInfo Pointer info for the source.
  /// \return The chain after the atomic memcpy.
  LLVM_ABI SDValue getAtomicMemcpy(SDValue Chain, const SDLoc &dl, SDValue Dst,
                                   SDValue Src, SDValue Size, Type *SizeTy,
                                   unsigned ElemSz, bool isTailCall,
                                   MachinePointerInfo DstPtrInfo,
                                   MachinePointerInfo SrcPtrInfo);

  /// Create an atomic memmove operation.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the move.
  /// \param Dst Destination pointer.
  /// \param Src Source pointer.
  /// \param Size Number of bytes to move.
  /// \param SizeTy Type of the size operand.
  /// \param ElemSz Element size used by the atomic move.
  /// \param isTailCall True if the move may be a tail call.
  /// \param DstPtrInfo Pointer info for the destination.
  /// \param SrcPtrInfo Pointer info for the source.
  /// \return The chain after the atomic memmove.
  LLVM_ABI SDValue getAtomicMemmove(SDValue Chain, const SDLoc &dl, SDValue Dst,
                                    SDValue Src, SDValue Size, Type *SizeTy,
                                    unsigned ElemSz, bool isTailCall,
                                    MachinePointerInfo DstPtrInfo,
                                    MachinePointerInfo SrcPtrInfo);

  /// Create an atomic memset operation.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the set.
  /// \param Dst Destination pointer.
  /// \param Value Fill value.
  /// \param Size Number of bytes to set.
  /// \param SizeTy Type of the size operand.
  /// \param ElemSz Element size used by the atomic set.
  /// \param isTailCall True if the set may be a tail call.
  /// \param DstPtrInfo Pointer info for the destination.
  /// \return The chain after the atomic memset.
  LLVM_ABI SDValue getAtomicMemset(SDValue Chain, const SDLoc &dl, SDValue Dst,
                                   SDValue Value, SDValue Size, Type *SizeTy,
                                   unsigned ElemSz, bool isTailCall,
                                   MachinePointerInfo DstPtrInfo);

  /// Helper function to make it easier to build SetCC's if you just have an
  /// ISD::CondCode instead of an SDValue.
  ///
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param LHS Left comparison operand.
  /// \param RHS Right comparison operand.
  /// \param Cond Comparison condition code.
  /// \param Chain Optional chain for a strict floating-point compare.
  /// \param IsSignaling True to use a signaling strict compare.
  /// \param Flags Node flags for the comparison.
  /// \return The SetCC node.
  SDValue getSetCC(const SDLoc &DL, EVT VT, SDValue LHS, SDValue RHS,
                   ISD::CondCode Cond, SDValue Chain = SDValue(),
                   bool IsSignaling = false, SDNodeFlags Flags = {}) {
    assert(LHS.getValueType().isVector() == RHS.getValueType().isVector() &&
           "Vector/scalar operand type mismatch for setcc");
    assert(LHS.getValueType().isVector() == VT.isVector() &&
           "Vector/scalar result type mismatch for setcc");
    assert(Cond != ISD::SETCC_INVALID &&
           "Cannot create a setCC of an invalid node.");
    if (Chain)
      return getNode(IsSignaling ? ISD::STRICT_FSETCCS : ISD::STRICT_FSETCC, DL,
                     {VT, MVT::Other}, {Chain, LHS, RHS, getCondCode(Cond)},
                     Flags);
    return getNode(ISD::SETCC, DL, VT, LHS, RHS, getCondCode(Cond), Flags);
  }

  /// Helper function to make it easier to build Select's if you just have
  /// operands and don't want to check for vector.
  ///
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param Cond Select condition.
  /// \param LHS True value.
  /// \param RHS False value.
  /// \param Flags Node flags for the select.
  /// \return The Select node.
  SDValue getSelect(const SDLoc &DL, EVT VT, SDValue Cond, SDValue LHS,
                    SDValue RHS, SDNodeFlags Flags = SDNodeFlags()) {
    assert(LHS.getValueType() == VT && RHS.getValueType() == VT &&
           "Cannot use select on differing types");
    auto Opcode = Cond.getValueType().isVector() ? ISD::VSELECT : ISD::SELECT;
    return getNode(Opcode, DL, VT, Cond, LHS, RHS, Flags);
  }

  /// Helper function to make it easier to build SelectCC's if you just have an
  /// ISD::CondCode instead of an SDValue.
  ///
  /// \param DL Debug location for the node.
  /// \param LHS Left comparison operand.
  /// \param RHS Right comparison operand.
  /// \param True Value selected when the condition is true.
  /// \param False Value selected when the condition is false.
  /// \param Cond Comparison condition code.
  /// \param Flags Node flags for the select.
  /// \return The SelectCC node.
  SDValue getSelectCC(const SDLoc &DL, SDValue LHS, SDValue RHS, SDValue True,
                      SDValue False, ISD::CondCode Cond,
                      SDNodeFlags Flags = SDNodeFlags()) {
    return getNode(ISD::SELECT_CC, DL, True.getValueType(), LHS, RHS, True,
                   False, getCondCode(Cond), Flags);
  }

  /// Try to simplify a select/vselect into 1 of its operands or a constant.
  ///
  /// \param Cond Select condition.
  /// \param TVal True value.
  /// \param FVal False value.
  /// \return The simplified select, or an empty SDValue.
  LLVM_ABI SDValue simplifySelect(SDValue Cond, SDValue TVal, SDValue FVal);

  /// Try to simplify a shift into 1 of its operands or a constant.
  ///
  /// \param X Value being shifted.
  /// \param Y Shift amount.
  /// \return The simplified shift, or an empty SDValue.
  LLVM_ABI SDValue simplifyShift(SDValue X, SDValue Y);

  /// Try to simplify a floating-point binary operation into 1 of its operands
  /// or a constant.
  ///
  /// \param Opcode Floating-point binary opcode.
  /// \param X Left operand.
  /// \param Y Right operand.
  /// \param Flags Node flags for the operation.
  /// \return The simplified FP binop, or an empty SDValue.
  LLVM_ABI SDValue simplifyFPBinop(unsigned Opcode, SDValue X, SDValue Y,
                                   SDNodeFlags Flags);

  /// VAArg produces a result and token chain, and takes a pointer
  /// and a source value as input.
  ///
  /// \param VT Result value type.
  /// \param dl Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer to the va_list.
  /// \param SV Source value describing the argument.
  /// \param Align Alignment of the argument in bytes.
  /// \return VAArg produces a result and token chain, and takes a pointer and a source value as input.
  LLVM_ABI SDValue getVAArg(EVT VT, const SDLoc &dl, SDValue Chain, SDValue Ptr,
                            SDValue SV, unsigned Align);

  /// Gets a node for an atomic cmpxchg op.
  ///
  /// There are two valid opcodes. ISD::ATOMIC_CMP_SWAP produces the value
  /// loaded and a chain result. ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS produces the
  /// value loaded, a success flag (initially i1), and a chain.
  ///
  /// \param Opcode ATOMIC_CMP_SWAP or ATOMIC_CMP_SWAP_WITH_SUCCESS.
  /// \param dl Debug location for the node.
  /// \param MemVT Memory value type of the atomic location.
  /// \param VTs Result value types.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer to the atomic location.
  /// \param Cmp Expected value.
  /// \param Swp Replacement value.
  /// \param MMO Memory operand describing the access.
  /// \return A node for an atomic cmpxchg op.
  LLVM_ABI SDValue getAtomicCmpSwap(unsigned Opcode, const SDLoc &dl, EVT MemVT,
                                    SDVTList VTs, SDValue Chain, SDValue Ptr,
                                    SDValue Cmp, SDValue Swp,
                                    MachineMemOperand *MMO);

  /// Gets a node for an atomic op, produces result (if relevant)
  /// and chain and takes 2 operands.
  ///
  /// \param Opcode Atomic ISD opcode.
  /// \param dl Debug location for the node.
  /// \param MemVT Memory value type of the atomic location.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer to the atomic location.
  /// \param Val Value operand of the atomic operation.
  /// \param MMO Memory operand describing the access.
  /// \return A node for an atomic op, produces result (if relevant) and chain and takes 2 operands.
  LLVM_ABI SDValue getAtomic(unsigned Opcode, const SDLoc &dl, EVT MemVT,
                             SDValue Chain, SDValue Ptr, SDValue Val,
                             MachineMemOperand *MMO);

  /// Gets a node for an atomic op, produces result and chain and takes N
  /// operands.
  ///
  /// \param Opcode Atomic ISD opcode.
  /// \param dl Debug location for the node.
  /// \param MemVT Memory value type of the atomic location.
  /// \param VTList Result value types.
  /// \param Ops Operand list of the atomic operation.
  /// \param MMO Memory operand describing the access.
  /// \param ExtType Load-extension kind, if the atomic load extends.
  /// \return A node for an atomic op, produces result and chain and takes N operands.
  LLVM_ABI SDValue getAtomic(unsigned Opcode, const SDLoc &dl, EVT MemVT,
                             SDVTList VTList, ArrayRef<SDValue> Ops,
                             MachineMemOperand *MMO,
                             ISD::LoadExtType ExtType = ISD::NON_EXTLOAD);

  /// Create an atomic load node.
  ///
  /// \param ExtType Load-extension kind.
  /// \param dl Debug location for the node.
  /// \param MemVT Memory value type of the load.
  /// \param VT Result value type.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer to the loaded location.
  /// \param MMO Memory operand describing the access.
  /// \return The atomic load node.
  LLVM_ABI SDValue getAtomicLoad(ISD::LoadExtType ExtType, const SDLoc &dl,
                                 EVT MemVT, EVT VT, SDValue Chain, SDValue Ptr,
                                 MachineMemOperand *MMO);

  /// Create a MemIntrinsicNode that may produce a result and takes operands.
  ///
  /// Opcode may be INTRINSIC_VOID, INTRINSIC_W_CHAIN, or a target-specific
  /// memory-referencing opcode (see SelectionDAGTargetInfo::isTargetMemoryOpcode).
  ///
  /// \param Opcode Intrinsic or target memory opcode.
  /// \param dl Debug location for the node.
  /// \param VTList Result value types.
  /// \param Ops Operand list.
  /// \param MemVT Memory value type of the access.
  /// \param PtrInfo Pointer info for the access.
  /// \param Alignment Alignment of the access.
  /// \param Flags Memory operand flags.
  /// \param Size Access size, or zero to infer from \p MemVT.
  /// \param AAInfo Alias-analysis metadata.
  /// \return The MemIntrinsicNode that may produce a result and takes operands.
  LLVM_ABI SDValue getMemIntrinsicNode(
      unsigned Opcode, const SDLoc &dl, SDVTList VTList, ArrayRef<SDValue> Ops,
      EVT MemVT, MachinePointerInfo PtrInfo, Align Alignment,
      MachineMemOperand::Flags Flags = MachineMemOperand::MOLoad |
                                       MachineMemOperand::MOStore,
      LocationSize Size = LocationSize::precise(0),
      const AAMDNodes &AAInfo = AAMDNodes());

  /// Create a MemIntrinsicNode, defaulting alignment from \p MemVT if omitted.
  ///
  /// \param Opcode Intrinsic or target memory opcode.
  /// \param dl Debug location for the node.
  /// \param VTList Result value types.
  /// \param Ops Operand list.
  /// \param MemVT Memory value type of the access.
  /// \param PtrInfo Pointer info for the access.
  /// \param Alignment Alignment of the access, or none to use \p MemVT.
  /// \param Flags Memory operand flags.
  /// \param Size Access size, or zero to infer from \p MemVT.
  /// \param AAInfo Alias-analysis metadata.
  /// \return The MemIntrinsicNode, defaulting alignment from \p MemVT if omitted.
  inline SDValue getMemIntrinsicNode(
      unsigned Opcode, const SDLoc &dl, SDVTList VTList, ArrayRef<SDValue> Ops,
      EVT MemVT, MachinePointerInfo PtrInfo,
      MaybeAlign Alignment = std::nullopt,
      MachineMemOperand::Flags Flags = MachineMemOperand::MOLoad |
                                       MachineMemOperand::MOStore,
      LocationSize Size = LocationSize::precise(0),
      const AAMDNodes &AAInfo = AAMDNodes()) {
    // Ensure that codegen never sees alignment 0
    return getMemIntrinsicNode(Opcode, dl, VTList, Ops, MemVT, PtrInfo,
                               Alignment.value_or(getEVTAlign(MemVT)), Flags,
                               Size, AAInfo);
  }

  /// Create a MemIntrinsicNode from an existing memory operand.
  ///
  /// \param Opcode Intrinsic or target memory opcode.
  /// \param dl Debug location for the node.
  /// \param VTList Result value types.
  /// \param Ops Operand list.
  /// \param MemVT Memory value type of the access.
  /// \param MMO Memory operand describing the access.
  /// \return The MemIntrinsicNode from an existing memory operand.
  LLVM_ABI SDValue getMemIntrinsicNode(unsigned Opcode, const SDLoc &dl,
                                       SDVTList VTList, ArrayRef<SDValue> Ops,
                                       EVT MemVT, MachineMemOperand *MMO);

  /// Create a MemIntrinsicNode with multiple memory operands.
  ///
  /// \param Opcode Intrinsic or target memory opcode.
  /// \param dl Debug location for the node.
  /// \param VTList Result value types.
  /// \param Ops Operand list.
  /// \param MemVT Memory value type of the access.
  /// \param MMOs Memory operands describing the accesses.
  /// \return The MemIntrinsicNode with multiple memory operands.
  LLVM_ABI SDValue getMemIntrinsicNode(unsigned Opcode, const SDLoc &dl,
                                       SDVTList VTList, ArrayRef<SDValue> Ops,
                                       EVT MemVT,
                                       ArrayRef<MachineMemOperand *> MMOs);

  /// Creates a LifetimeSDNode that starts (`IsStart==true`) or ends
  /// (`IsStart==false`) the lifetime of the `FrameIndex`.
  ///
  /// \param IsStart True to start the lifetime, false to end it.
  /// \param dl Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param FrameIndex Frame index whose lifetime is marked.
  /// \return The LifetimeSDNode that starts (`IsStart==true`) or ends (`IsStart==false`) the lifetime of the `FrameIndex`.
  LLVM_ABI SDValue getLifetimeNode(bool IsStart, const SDLoc &dl, SDValue Chain,
                                   int FrameIndex);

  /// Creates a PseudoProbeSDNode with function GUID `Guid` and
  /// the index of the block `Index` it is probing, as well as the attributes
  /// `attr` of the probe.
  ///
  /// \param Dl Debug location for the probe.
  /// \param Chain Incoming token chain.
  /// \param Guid Function GUID being probed.
  /// \param Index Block index of the probe.
  /// \param Attr Probe attributes.
  /// \return The PseudoProbeSDNode with function GUID `Guid` and the index of the block `Index` it is probing, as well as the attributes `attr` of the probe.
  LLVM_ABI SDValue getPseudoProbeNode(const SDLoc &Dl, SDValue Chain,
                                      uint64_t Guid, uint64_t Index,
                                      uint32_t Attr);

  /// Create a MERGE_VALUES node from the given operands.
  ///
  /// \param Ops Values to merge.
  /// \param dl Debug location for the node.
  /// \return The MERGE_VALUES node from the given operands.
  LLVM_ABI SDValue getMergeValues(ArrayRef<SDValue> Ops, const SDLoc &dl);

  /// Return poison values for each result, preserving any chain.
  ///
  /// Substitutes \p Chain for any result of type MVT::Other, merged into a
  /// single MERGE_VALUES node. Used to salvage a chain when an operation cannot
  /// be lowered due to an error, and the program will be discarded.
  ///
  /// \param ResultTypes Result types of the failed operation.
  /// \param Chain Incoming token chain to preserve.
  /// \param dl Debug location for the node.
  /// \return Poison values for each result, preserving any chain.
  LLVM_ABI SDValue getErrorMergeValues(ArrayRef<EVT> ResultTypes, SDValue Chain,
                                       const SDLoc &dl);

  /// Create a load node.
  ///
  /// Loads are not normal binary operators: their result type is not
  /// determined by their operands, and they produce a value AND a token chain.
  ///
  /// This function will set the MOLoad flag on MMOFlags, but you can set it if
  /// you want.  The MOStore flag must not be set.
  ///
  /// \param VT Result value type.
  /// \param dl Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param PtrInfo Pointer info for the access.
  /// \param Alignment Access alignment.
  /// \param MMOFlags Memory operand flags.
  /// \param Metadata Memory-operand metadata.
  /// \return The load node.
  LLVM_ABI SDValue
  getLoad(EVT VT, const SDLoc &dl, SDValue Chain, SDValue Ptr,
          MachinePointerInfo PtrInfo, MaybeAlign Alignment = MaybeAlign(),
          MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
          const MMOMetadata &Metadata = MMOMetadata());
  /// Create a load node.
  ///
  /// \param VT Result value type.
  /// \param dl Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param MMO Memory operand describing the access.
  /// \return The load node.
  LLVM_ABI SDValue getLoad(EVT VT, const SDLoc &dl, SDValue Chain, SDValue Ptr,
                           MachineMemOperand *MMO);
  /// Create an extending load node.
  ///
  /// \param ExtType Load-extension kind.
  /// \param dl Debug location for the node.
  /// \param VT Result value type.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param PtrInfo Pointer info for the access.
  /// \param MemVT Memory value type.
  /// \param Alignment Access alignment.
  /// \param MMOFlags Memory operand flags.
  /// \param Metadata Memory-operand metadata.
  /// \return The extending load node.
  LLVM_ABI SDValue
  getExtLoad(ISD::LoadExtType ExtType, const SDLoc &dl, EVT VT, SDValue Chain,
             SDValue Ptr, MachinePointerInfo PtrInfo, EVT MemVT,
             MaybeAlign Alignment = MaybeAlign(),
             MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
             const MMOMetadata &Metadata = MMOMetadata());
  /// Create an extending load node.
  ///
  /// \param ExtType Load-extension kind.
  /// \param dl Debug location for the node.
  /// \param VT Result value type.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param MemVT Memory value type.
  /// \param MMO Memory operand describing the access.
  /// \return The extending load node.
  LLVM_ABI SDValue getExtLoad(ISD::LoadExtType ExtType, const SDLoc &dl, EVT VT,
                              SDValue Chain, SDValue Ptr, EVT MemVT,
                              MachineMemOperand *MMO);
  /// Convert an existing load into an indexed load.
  ///
  /// \param OrigLoad Existing load being converted to indexed form.
  /// \param dl Debug location for the node.
  /// \param Base Base pointer of the indexed access.
  /// \param Offset Indexed addressing offset.
  /// \param AM Indexed addressing mode.
  /// \return The indexed load.
  LLVM_ABI SDValue getIndexedLoad(SDValue OrigLoad, const SDLoc &dl,
                                  SDValue Base, SDValue Offset,
                                  ISD::MemIndexedMode AM);
  /// Create a load node.
  ///
  /// \param AM Indexed addressing mode.
  /// \param ExtType Load-extension kind.
  /// \param VT Result value type.
  /// \param dl Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param Offset Indexed addressing offset.
  /// \param PtrInfo Pointer info for the access.
  /// \param MemVT Memory value type.
  /// \param Alignment Access alignment.
  /// \param MMOFlags Memory operand flags.
  /// \param Metadata Memory-operand metadata.
  /// \return The load node.
  LLVM_ABI SDValue
  getLoad(ISD::MemIndexedMode AM, ISD::LoadExtType ExtType, EVT VT,
          const SDLoc &dl, SDValue Chain, SDValue Ptr, SDValue Offset,
          MachinePointerInfo PtrInfo, EVT MemVT, Align Alignment,
          MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
          const MMOMetadata &Metadata = MMOMetadata());
  /// Create a load node.
  ///
  /// \param AM Indexed addressing mode.
  /// \param ExtType Load-extension kind.
  /// \param VT Result value type.
  /// \param dl Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param Offset Indexed addressing offset.
  /// \param PtrInfo Pointer info for the access.
  /// \param MemVT Memory value type.
  /// \param Alignment Access alignment.
  /// \param MMOFlags Memory operand flags.
  /// \param Metadata Memory-operand metadata.
  /// \return The load node.
  inline SDValue
  getLoad(ISD::MemIndexedMode AM, ISD::LoadExtType ExtType, EVT VT,
          const SDLoc &dl, SDValue Chain, SDValue Ptr, SDValue Offset,
          MachinePointerInfo PtrInfo, EVT MemVT,
          MaybeAlign Alignment = MaybeAlign(),
          MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
          const MMOMetadata &Metadata = MMOMetadata()) {
    // Ensures that codegen never sees a None Alignment.
    return getLoad(AM, ExtType, VT, dl, Chain, Ptr, Offset, PtrInfo, MemVT,
                   Alignment.value_or(getEVTAlign(MemVT)), MMOFlags, Metadata);
  }
  /// Create a load node.
  ///
  /// \param AM Indexed addressing mode.
  /// \param ExtType Load-extension kind.
  /// \param VT Result value type.
  /// \param dl Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param Offset Indexed addressing offset.
  /// \param MemVT Memory value type.
  /// \param MMO Memory operand describing the access.
  /// \return The load node.
  LLVM_ABI SDValue getLoad(ISD::MemIndexedMode AM, ISD::LoadExtType ExtType,
                           EVT VT, const SDLoc &dl, SDValue Chain, SDValue Ptr,
                           SDValue Offset, EVT MemVT, MachineMemOperand *MMO);

  /// Helper function to build ISD::STORE nodes.
  ///
  /// This function will set the MOStore flag on MMOFlags, but you can set it if
  /// you want.  The MOLoad and MOInvariant flags must not be set.

  /// Create a store node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Val Stored value.
  /// \param Ptr Pointer operand.
  /// \param PtrInfo Pointer info for the access.
  /// \param Alignment Access alignment.
  /// \param MMOFlags Memory operand flags.
  /// \param Metadata Memory-operand metadata.
  /// \return The store node.
  LLVM_ABI SDValue
  getStore(SDValue Chain, const SDLoc &dl, SDValue Val, SDValue Ptr,
           MachinePointerInfo PtrInfo, Align Alignment,
           MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
           const MMOMetadata &Metadata = MMOMetadata());
  /// Create a store node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Val Stored value.
  /// \param Ptr Pointer operand.
  /// \param PtrInfo Pointer info for the access.
  /// \param Alignment Access alignment.
  /// \param MMOFlags Memory operand flags.
  /// \param Metadata Memory-operand metadata.
  /// \return The store node.
  inline SDValue
  getStore(SDValue Chain, const SDLoc &dl, SDValue Val, SDValue Ptr,
           MachinePointerInfo PtrInfo, MaybeAlign Alignment = MaybeAlign(),
           MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
           const MMOMetadata &Metadata = MMOMetadata()) {
    return getStore(Chain, dl, Val, Ptr, PtrInfo,
                    Alignment.value_or(getEVTAlign(Val.getValueType())),
                    MMOFlags, Metadata);
  }
  /// Create a store node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Val Stored value.
  /// \param Ptr Pointer operand.
  /// \param MMO Memory operand describing the access.
  /// \return The store node.
  LLVM_ABI SDValue getStore(SDValue Chain, const SDLoc &dl, SDValue Val,
                            SDValue Ptr, MachineMemOperand *MMO);
  /// Create a store node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Val Stored value.
  /// \param Ptr Pointer operand.
  /// \param Offset Indexed addressing offset.
  /// \param MMO Memory operand describing the access.
  /// \return The store node.
  LLVM_ABI SDValue getStore(SDValue Chain, const SDLoc &dl, SDValue Val,
                            SDValue Ptr, SDValue Offset,
                            MachineMemOperand *MMO);
  /// Create a truncating store node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Val Stored value.
  /// \param Ptr Pointer operand.
  /// \param Offset Indexed addressing offset.
  /// \param PtrInfo Pointer info for the access.
  /// \param SVT Stored memory value type.
  /// \param Alignment Access alignment.
  /// \param MMOFlags Memory operand flags.
  /// \param Metadata Memory-operand metadata.
  /// \return The truncating store node.
  LLVM_ABI SDValue getTruncStore(
      SDValue Chain, const SDLoc &dl, SDValue Val, SDValue Ptr, SDValue Offset,
      MachinePointerInfo PtrInfo, EVT SVT, Align Alignment,
      MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
      const MMOMetadata &Metadata = MMOMetadata());
  /// Create a truncating store node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Val Stored value.
  /// \param Ptr Pointer operand.
  /// \param PtrInfo Pointer info for the access.
  /// \param SVT Stored memory value type.
  /// \param Alignment Access alignment.
  /// \param MMOFlags Memory operand flags.
  /// \param Metadata Memory-operand metadata.
  /// \return The truncating store node.
  LLVM_ABI SDValue
  getTruncStore(SDValue Chain, const SDLoc &dl, SDValue Val, SDValue Ptr,
                MachinePointerInfo PtrInfo, EVT SVT, Align Alignment,
                MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
                const MMOMetadata &Metadata = MMOMetadata());
  /// Create a truncating store node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Val Stored value.
  /// \param Ptr Pointer operand.
  /// \param Offset Indexed addressing offset.
  /// \param SVT Stored memory value type.
  /// \param MMO Memory operand describing the access.
  /// \return The truncating store node.
  LLVM_ABI SDValue getTruncStore(SDValue Chain, const SDLoc &dl, SDValue Val,
                                 SDValue Ptr, SDValue Offset, EVT SVT,
                                 MachineMemOperand *MMO);

  /// Create a truncating store node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Val Stored value.
  /// \param Ptr Pointer operand.
  /// \param PtrInfo Pointer info for the access.
  /// \param SVT Stored memory value type.
  /// \param Alignment Access alignment.
  /// \param MMOFlags Memory operand flags.
  /// \param Metadata Memory-operand metadata.
  /// \return The truncating store node.
  inline SDValue
  getTruncStore(SDValue Chain, const SDLoc &dl, SDValue Val, SDValue Ptr,
                MachinePointerInfo PtrInfo, EVT SVT,
                MaybeAlign Alignment = MaybeAlign(),
                MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
                const MMOMetadata &Metadata = MMOMetadata()) {
    return getTruncStore(Chain, dl, Val, Ptr, PtrInfo, SVT,
                         Alignment.value_or(getEVTAlign(SVT)), MMOFlags,
                         Metadata);
  }
  /// Create a truncating store node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Val Stored value.
  /// \param Ptr Pointer operand.
  /// \param SVT Stored memory value type.
  /// \param MMO Memory operand describing the access.
  /// \return The truncating store node.
  LLVM_ABI SDValue getTruncStore(SDValue Chain, const SDLoc &dl, SDValue Val,
                                 SDValue Ptr, EVT SVT, MachineMemOperand *MMO);
  /// Convert an existing store into an indexed store.
  ///
  /// \param OrigStore Existing store being converted to indexed form.
  /// \param dl Debug location for the node.
  /// \param Base Base pointer of the indexed access.
  /// \param Offset Indexed addressing offset.
  /// \param AM Indexed addressing mode.
  /// \return The indexed store.
  LLVM_ABI SDValue getIndexedStore(SDValue OrigStore, const SDLoc &dl,
                                   SDValue Base, SDValue Offset,
                                   ISD::MemIndexedMode AM);
  /// Create a store node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Val Stored value.
  /// \param Ptr Pointer operand.
  /// \param Offset Indexed addressing offset.
  /// \param SVT Stored memory value type.
  /// \param MMO Memory operand describing the access.
  /// \param AM Indexed addressing mode.
  /// \param IsTruncating True if the store truncates the value.
  /// \return The store node.
  LLVM_ABI SDValue getStore(SDValue Chain, const SDLoc &dl, SDValue Val,
                            SDValue Ptr, SDValue Offset, EVT SVT,
                            MachineMemOperand *MMO, ISD::MemIndexedMode AM,
                            bool IsTruncating = false);

  /// Create a predicated vector-length load.
  ///
  /// \param AM Indexed addressing mode.
  /// \param ExtType Load-extension kind.
  /// \param VT Result value type.
  /// \param dl Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param Offset Indexed addressing offset.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param PtrInfo Pointer info for the access.
  /// \param MemVT Memory value type.
  /// \param Alignment Access alignment.
  /// \param MMOFlags Memory operand flags.
  /// \param AAInfo Alias-analysis metadata.
  /// \param Ranges Optional range metadata.
  /// \param IsExpanding True if this is an expanding load.
  /// \return The predicated vector-length load.
  LLVM_ABI SDValue getLoadVP(ISD::MemIndexedMode AM, ISD::LoadExtType ExtType,
                             EVT VT, const SDLoc &dl, SDValue Chain,
                             SDValue Ptr, SDValue Offset, SDValue Mask,
                             SDValue EVL, MachinePointerInfo PtrInfo, EVT MemVT,
                             Align Alignment, MachineMemOperand::Flags MMOFlags,
                             const AAMDNodes &AAInfo,
                             const MDNode *Ranges = nullptr,
                             bool IsExpanding = false);
  /// Create a predicated vector-length load.
  ///
  /// \param AM Indexed addressing mode.
  /// \param ExtType Load-extension kind.
  /// \param VT Result value type.
  /// \param dl Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param Offset Indexed addressing offset.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param PtrInfo Pointer info for the access.
  /// \param MemVT Memory value type.
  /// \param Alignment Access alignment.
  /// \param MMOFlags Memory operand flags.
  /// \param AAInfo Alias-analysis metadata.
  /// \param Ranges Optional range metadata.
  /// \param IsExpanding True if this is an expanding load.
  /// \return The predicated vector-length load.
  inline SDValue
  getLoadVP(ISD::MemIndexedMode AM, ISD::LoadExtType ExtType, EVT VT,
            const SDLoc &dl, SDValue Chain, SDValue Ptr, SDValue Offset,
            SDValue Mask, SDValue EVL, MachinePointerInfo PtrInfo, EVT MemVT,
            MaybeAlign Alignment = MaybeAlign(),
            MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
            const AAMDNodes &AAInfo = AAMDNodes(),
            const MDNode *Ranges = nullptr, bool IsExpanding = false) {
    // Ensures that codegen never sees a None Alignment.
    return getLoadVP(AM, ExtType, VT, dl, Chain, Ptr, Offset, Mask, EVL,
                     PtrInfo, MemVT, Alignment.value_or(getEVTAlign(MemVT)),
                     MMOFlags, AAInfo, Ranges, IsExpanding);
  }
  /// Create a predicated vector-length load.
  ///
  /// \param AM Indexed addressing mode.
  /// \param ExtType Load-extension kind.
  /// \param VT Result value type.
  /// \param dl Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param Offset Indexed addressing offset.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param MemVT Memory value type.
  /// \param MMO Memory operand describing the access.
  /// \param IsExpanding True if this is an expanding load.
  /// \return The predicated vector-length load.
  LLVM_ABI SDValue getLoadVP(ISD::MemIndexedMode AM, ISD::LoadExtType ExtType,
                             EVT VT, const SDLoc &dl, SDValue Chain,
                             SDValue Ptr, SDValue Offset, SDValue Mask,
                             SDValue EVL, EVT MemVT, MachineMemOperand *MMO,
                             bool IsExpanding = false);
  /// Create a predicated vector-length load.
  ///
  /// \param VT Result value type.
  /// \param dl Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param PtrInfo Pointer info for the access.
  /// \param Alignment Access alignment.
  /// \param MMOFlags Memory operand flags.
  /// \param AAInfo Alias-analysis metadata.
  /// \param Ranges Optional range metadata.
  /// \param IsExpanding True if this is an expanding load.
  /// \return The predicated vector-length load.
  LLVM_ABI SDValue getLoadVP(EVT VT, const SDLoc &dl, SDValue Chain,
                             SDValue Ptr, SDValue Mask, SDValue EVL,
                             MachinePointerInfo PtrInfo, MaybeAlign Alignment,
                             MachineMemOperand::Flags MMOFlags,
                             const AAMDNodes &AAInfo,
                             const MDNode *Ranges = nullptr,
                             bool IsExpanding = false);
  /// Create a predicated vector-length load.
  ///
  /// \param VT Result value type.
  /// \param dl Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param MMO Memory operand describing the access.
  /// \param IsExpanding True if this is an expanding load.
  /// \return The predicated vector-length load.
  LLVM_ABI SDValue getLoadVP(EVT VT, const SDLoc &dl, SDValue Chain,
                             SDValue Ptr, SDValue Mask, SDValue EVL,
                             MachineMemOperand *MMO, bool IsExpanding = false);
  /// Create a predicated extending vector-length load.
  ///
  /// \param ExtType Load-extension kind.
  /// \param dl Debug location for the node.
  /// \param VT Result value type.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param PtrInfo Pointer info for the access.
  /// \param MemVT Memory value type.
  /// \param Alignment Access alignment.
  /// \param MMOFlags Memory operand flags.
  /// \param AAInfo Alias-analysis metadata.
  /// \param IsExpanding True if this is an expanding load.
  /// \return The predicated extending vector-length load.
  LLVM_ABI SDValue getExtLoadVP(
      ISD::LoadExtType ExtType, const SDLoc &dl, EVT VT, SDValue Chain,
      SDValue Ptr, SDValue Mask, SDValue EVL, MachinePointerInfo PtrInfo,
      EVT MemVT, MaybeAlign Alignment, MachineMemOperand::Flags MMOFlags,
      const AAMDNodes &AAInfo, bool IsExpanding = false);
  /// Create a predicated extending vector-length load.
  ///
  /// \param ExtType Load-extension kind.
  /// \param dl Debug location for the node.
  /// \param VT Result value type.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param MemVT Memory value type.
  /// \param MMO Memory operand describing the access.
  /// \param IsExpanding True if this is an expanding load.
  /// \return The predicated extending vector-length load.
  LLVM_ABI SDValue getExtLoadVP(ISD::LoadExtType ExtType, const SDLoc &dl,
                                EVT VT, SDValue Chain, SDValue Ptr,
                                SDValue Mask, SDValue EVL, EVT MemVT,
                                MachineMemOperand *MMO,
                                bool IsExpanding = false);
  /// Convert an existing VP load into an indexed VP load.
  ///
  /// \param OrigLoad Existing load being converted to indexed form.
  /// \param dl Debug location for the node.
  /// \param Base Base pointer of the indexed access.
  /// \param Offset Indexed addressing offset.
  /// \param AM Indexed addressing mode.
  /// \return The indexed VP load.
  LLVM_ABI SDValue getIndexedLoadVP(SDValue OrigLoad, const SDLoc &dl,
                                    SDValue Base, SDValue Offset,
                                    ISD::MemIndexedMode AM);
  /// Create a predicated vector-length store.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Val Stored value.
  /// \param Ptr Pointer operand.
  /// \param Offset Indexed addressing offset.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param MemVT Memory value type.
  /// \param MMO Memory operand describing the access.
  /// \param AM Indexed addressing mode.
  /// \param IsTruncating True if the store truncates the value.
  /// \param IsCompressing True if this is a compressing store.
  /// \return The predicated vector-length store.
  LLVM_ABI SDValue getStoreVP(SDValue Chain, const SDLoc &dl, SDValue Val,
                              SDValue Ptr, SDValue Offset, SDValue Mask,
                              SDValue EVL, EVT MemVT, MachineMemOperand *MMO,
                              ISD::MemIndexedMode AM, bool IsTruncating = false,
                              bool IsCompressing = false);
  /// Create a predicated truncating vector-length store.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Val Stored value.
  /// \param Ptr Pointer operand.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param PtrInfo Pointer info for the access.
  /// \param SVT Stored memory value type.
  /// \param Alignment Access alignment.
  /// \param MMOFlags Memory operand flags.
  /// \param AAInfo Alias-analysis metadata.
  /// \param IsCompressing True if this is a compressing store.
  /// \return The predicated truncating vector-length store.
  LLVM_ABI SDValue getTruncStoreVP(SDValue Chain, const SDLoc &dl, SDValue Val,
                                   SDValue Ptr, SDValue Mask, SDValue EVL,
                                   MachinePointerInfo PtrInfo, EVT SVT,
                                   Align Alignment,
                                   MachineMemOperand::Flags MMOFlags,
                                   const AAMDNodes &AAInfo,
                                   bool IsCompressing = false);
  /// Create a predicated truncating vector-length store.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Val Stored value.
  /// \param Ptr Pointer operand.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param SVT Stored memory value type.
  /// \param MMO Memory operand describing the access.
  /// \param IsCompressing True if this is a compressing store.
  /// \return The predicated truncating vector-length store.
  LLVM_ABI SDValue getTruncStoreVP(SDValue Chain, const SDLoc &dl, SDValue Val,
                                   SDValue Ptr, SDValue Mask, SDValue EVL,
                                   EVT SVT, MachineMemOperand *MMO,
                                   bool IsCompressing = false);
  /// Convert an existing VP store into an indexed VP store.
  ///
  /// \param OrigStore Existing store being converted to indexed form.
  /// \param dl Debug location for the node.
  /// \param Base Base pointer of the indexed access.
  /// \param Offset Indexed addressing offset.
  /// \param AM Indexed addressing mode.
  /// \return The indexed VP store.
  LLVM_ABI SDValue getIndexedStoreVP(SDValue OrigStore, const SDLoc &dl,
                                     SDValue Base, SDValue Offset,
                                     ISD::MemIndexedMode AM);

  /// Create a strided predicated vector-length load.
  ///
  /// \param AM Indexed addressing mode.
  /// \param ExtType Load-extension kind.
  /// \param VT Result value type.
  /// \param DL Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param Offset Indexed addressing offset.
  /// \param Stride Stride between accessed elements.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param MemVT Memory value type.
  /// \param MMO Memory operand describing the access.
  /// \param IsExpanding True if this is an expanding load.
  /// \return The strided predicated vector-length load.
  LLVM_ABI SDValue getStridedLoadVP(
      ISD::MemIndexedMode AM, ISD::LoadExtType ExtType, EVT VT, const SDLoc &DL,
      SDValue Chain, SDValue Ptr, SDValue Offset, SDValue Stride, SDValue Mask,
      SDValue EVL, EVT MemVT, MachineMemOperand *MMO, bool IsExpanding = false);
  /// Create a strided predicated vector-length load.
  ///
  /// \param VT Result value type.
  /// \param DL Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param Stride Stride between accessed elements.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param MMO Memory operand describing the access.
  /// \param IsExpanding True if this is an expanding load.
  /// \return The strided predicated vector-length load.
  LLVM_ABI SDValue getStridedLoadVP(EVT VT, const SDLoc &DL, SDValue Chain,
                                    SDValue Ptr, SDValue Stride, SDValue Mask,
                                    SDValue EVL, MachineMemOperand *MMO,
                                    bool IsExpanding = false);
  /// Create a strided predicated extending VP load.
  ///
  /// \param ExtType Load-extension kind.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param Stride Stride between accessed elements.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param MemVT Memory value type.
  /// \param MMO Memory operand describing the access.
  /// \param IsExpanding True if this is an expanding load.
  /// \return The strided predicated extending VP load.
  LLVM_ABI SDValue getExtStridedLoadVP(ISD::LoadExtType ExtType,
                                       const SDLoc &DL, EVT VT, SDValue Chain,
                                       SDValue Ptr, SDValue Stride,
                                       SDValue Mask, SDValue EVL, EVT MemVT,
                                       MachineMemOperand *MMO,
                                       bool IsExpanding = false);
  /// Create a strided predicated vector-length store.
  ///
  /// \param Chain Incoming token chain.
  /// \param DL Debug location for the node.
  /// \param Val Stored value.
  /// \param Ptr Pointer operand.
  /// \param Offset Indexed addressing offset.
  /// \param Stride Stride between accessed elements.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param MemVT Memory value type.
  /// \param MMO Memory operand describing the access.
  /// \param AM Indexed addressing mode.
  /// \param IsTruncating True if the store truncates the value.
  /// \param IsCompressing True if this is a compressing store.
  /// \return The strided predicated vector-length store.
  LLVM_ABI SDValue getStridedStoreVP(SDValue Chain, const SDLoc &DL,
                                     SDValue Val, SDValue Ptr, SDValue Offset,
                                     SDValue Stride, SDValue Mask, SDValue EVL,
                                     EVT MemVT, MachineMemOperand *MMO,
                                     ISD::MemIndexedMode AM,
                                     bool IsTruncating = false,
                                     bool IsCompressing = false);
  /// Create a strided predicated truncating VP store.
  ///
  /// \param Chain Incoming token chain.
  /// \param DL Debug location for the node.
  /// \param Val Stored value.
  /// \param Ptr Pointer operand.
  /// \param Stride Stride between accessed elements.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param SVT Stored memory value type.
  /// \param MMO Memory operand describing the access.
  /// \param IsCompressing True if this is a compressing store.
  /// \return The strided predicated truncating VP store.
  LLVM_ABI SDValue getTruncStridedStoreVP(SDValue Chain, const SDLoc &DL,
                                          SDValue Val, SDValue Ptr,
                                          SDValue Stride, SDValue Mask,
                                          SDValue EVL, EVT SVT,
                                          MachineMemOperand *MMO,
                                          bool IsCompressing = false);

  /// Create a predicated vector-length gather.
  ///
  /// \param VTs Result value types.
  /// \param VT Result value type.
  /// \param dl Debug location for the node.
  /// \param Ops Operand list.
  /// \param MMO Memory operand describing the access.
  /// \param IndexType Gather/scatter index type.
  /// \return The predicated vector-length gather.
  LLVM_ABI SDValue getGatherVP(SDVTList VTs, EVT VT, const SDLoc &dl,
                               ArrayRef<SDValue> Ops, MachineMemOperand *MMO,
                               ISD::MemIndexType IndexType);
  /// Create a predicated vector-length scatter.
  ///
  /// \param VTs Result value types.
  /// \param VT Result value type.
  /// \param dl Debug location for the node.
  /// \param Ops Operand list.
  /// \param MMO Memory operand describing the access.
  /// \param IndexType Gather/scatter index type.
  /// \return The predicated vector-length scatter.
  LLVM_ABI SDValue getScatterVP(SDVTList VTs, EVT VT, const SDLoc &dl,
                                ArrayRef<SDValue> Ops, MachineMemOperand *MMO,
                                ISD::MemIndexType IndexType);

  /// Create a masked load node.
  ///
  /// \param VT Result value type.
  /// \param dl Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param Base Base pointer of the indexed access.
  /// \param Offset Indexed addressing offset.
  /// \param Mask Predication mask.
  /// \param Src0 Passthru value for inactive lanes.
  /// \param MemVT Memory value type.
  /// \param MMO Memory operand describing the access.
  /// \param AM Indexed addressing mode.
  /// \param ExtType Load-extension kind.
  /// \param IsExpanding True if this is an expanding load.
  /// \return The masked load node.
  LLVM_ABI SDValue getMaskedLoad(EVT VT, const SDLoc &dl, SDValue Chain,
                                 SDValue Base, SDValue Offset, SDValue Mask,
                                 SDValue Src0, EVT MemVT,
                                 MachineMemOperand *MMO, ISD::MemIndexedMode AM,
                                 ISD::LoadExtType ExtType, bool IsExpanding = false);
  /// Convert an existing masked load into an indexed form.
  ///
  /// \param OrigLoad Existing load being converted to indexed form.
  /// \param dl Debug location for the node.
  /// \param Base Base pointer of the indexed access.
  /// \param Offset Indexed addressing offset.
  /// \param AM Indexed addressing mode.
  /// \return The indexed form.
  LLVM_ABI SDValue getIndexedMaskedLoad(SDValue OrigLoad, const SDLoc &dl,
                                        SDValue Base, SDValue Offset,
                                        ISD::MemIndexedMode AM);
  /// Create a masked store node.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Val Stored value.
  /// \param Base Base pointer of the indexed access.
  /// \param Offset Indexed addressing offset.
  /// \param Mask Predication mask.
  /// \param MemVT Memory value type.
  /// \param MMO Memory operand describing the access.
  /// \param AM Indexed addressing mode.
  /// \param IsTruncating True if the store truncates the value.
  /// \param IsCompressing True if this is a compressing store.
  /// \return The masked store node.
  LLVM_ABI SDValue getMaskedStore(SDValue Chain, const SDLoc &dl, SDValue Val,
                                  SDValue Base, SDValue Offset, SDValue Mask,
                                  EVT MemVT, MachineMemOperand *MMO,
                                  ISD::MemIndexedMode AM,
                                  bool IsTruncating = false,
                                  bool IsCompressing = false);
  /// Convert an existing masked store into an indexed form.
  ///
  /// \param OrigStore Existing store being converted to indexed form.
  /// \param dl Debug location for the node.
  /// \param Base Base pointer of the indexed access.
  /// \param Offset Indexed addressing offset.
  /// \param AM Indexed addressing mode.
  /// \return The indexed form.
  LLVM_ABI SDValue getIndexedMaskedStore(SDValue OrigStore, const SDLoc &dl,
                                         SDValue Base, SDValue Offset,
                                         ISD::MemIndexedMode AM);
  /// Create a masked gather node.
  ///
  /// \param VTs Result value types.
  /// \param MemVT Memory value type.
  /// \param dl Debug location for the node.
  /// \param Ops Operand list.
  /// \param MMO Memory operand describing the access.
  /// \param IndexType Gather/scatter index type.
  /// \param ExtTy Load-extension kind.
  /// \return The masked gather node.
  LLVM_ABI SDValue getMaskedGather(SDVTList VTs, EVT MemVT, const SDLoc &dl,
                                   ArrayRef<SDValue> Ops,
                                   MachineMemOperand *MMO,
                                   ISD::MemIndexType IndexType,
                                   ISD::LoadExtType ExtTy);
  /// Create a masked scatter node.
  ///
  /// \param VTs Result value types.
  /// \param MemVT Memory value type.
  /// \param dl Debug location for the node.
  /// \param Ops Operand list.
  /// \param MMO Memory operand describing the access.
  /// \param IndexType Gather/scatter index type.
  /// \param IsTruncating True if the store truncates the value.
  /// \return The masked scatter node.
  LLVM_ABI SDValue getMaskedScatter(SDVTList VTs, EVT MemVT, const SDLoc &dl,
                                    ArrayRef<SDValue> Ops,
                                    MachineMemOperand *MMO,
                                    ISD::MemIndexType IndexType,
                                    bool IsTruncating = false);
  /// Create a masked histogram node.
  ///
  /// \param VTs Result value types.
  /// \param MemVT Memory value type.
  /// \param dl Debug location for the node.
  /// \param Ops Operand list.
  /// \param MMO Memory operand describing the access.
  /// \param IndexType Gather/scatter index type.
  /// \return The masked histogram node.
  LLVM_ABI SDValue getMaskedHistogram(SDVTList VTs, EVT MemVT, const SDLoc &dl,
                                      ArrayRef<SDValue> Ops,
                                      MachineMemOperand *MMO,
                                      ISD::MemIndexType IndexType);
  /// Create a first-faulting predicated vector-length load.
  ///
  /// \param VT Result value type.
  /// \param DL Debug location for the node.
  /// \param Chain Incoming token chain.
  /// \param Ptr Pointer operand.
  /// \param Mask Predication mask.
  /// \param EVL Explicit vector length.
  /// \param MMO Memory operand describing the access.
  /// \return The first-faulting predicated vector-length load.
  LLVM_ABI SDValue getLoadFFVP(EVT VT, const SDLoc &DL, SDValue Chain,
                               SDValue Ptr, SDValue Mask, SDValue EVL,
                               MachineMemOperand *MMO);

  /// Create a node that reads the floating-point environment.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Ptr Pointer operand.
  /// \param MemVT Memory value type.
  /// \param MMO Memory operand describing the access.
  /// \return The node that reads the floating-point environment.
  LLVM_ABI SDValue getGetFPEnv(SDValue Chain, const SDLoc &dl, SDValue Ptr,
                               EVT MemVT, MachineMemOperand *MMO);
  /// Create a node that writes the floating-point environment.
  ///
  /// \param Chain Incoming token chain.
  /// \param dl Debug location for the node.
  /// \param Ptr Pointer operand.
  /// \param MemVT Memory value type.
  /// \param MMO Memory operand describing the access.
  /// \return The node that writes the floating-point environment.
  LLVM_ABI SDValue getSetFPEnv(SDValue Chain, const SDLoc &dl, SDValue Ptr,
                               EVT MemVT, MachineMemOperand *MMO);

  /// Construct a node to track a Value* through the backend.
  ///
  /// \param v IR value to track.
  /// \return An SDValue that tracks the Value* through the backend.
  LLVM_ABI SDValue getSrcValue(const Value *v);

  /// Return an MDNodeSDNode which holds an MDNode.
  ///
  /// \param MD Metadata node to wrap.
  /// \return An MDNodeSDNode which holds an MDNode.
  LLVM_ABI SDValue getMDNode(const MDNode *MD);

  /// Return a bitcast using the SDLoc of the value operand, and casting to the
  /// provided type. Use getNode to set a custom SDLoc.
  ///
  /// \param VT Result value type.
  /// \param V Value being bitcast.
  /// \return A bitcast using the SDLoc of the value operand, and casting to the provided type.
  LLVM_ABI SDValue getBitcast(EVT VT, SDValue V);

  /// Return an AddrSpaceCastSDNode.
  ///
  /// \param dl Debug location for the node.
  /// \param VT Result value type.
  /// \param Ptr Pointer being cast.
  /// \param SrcAS Source address space.
  /// \param DestAS Destination address space.
  /// \return An AddrSpaceCastSDNode.
  LLVM_ABI SDValue getAddrSpaceCast(const SDLoc &dl, EVT VT, SDValue Ptr,
                                    unsigned SrcAS, unsigned DestAS);

  /// Return a freeze using the SDLoc of the value operand.
  ///
  /// \param V Value to freeze.
  /// \return A freeze using the SDLoc of the value operand.
  LLVM_ABI SDValue getFreeze(SDValue V);

  /// Return a freeze of V if any of the demanded elts may be undef or poison.
  /// \p Kind can be used to selectively freeze poison and/or undef bits only.
  ///
  /// \param V Value to freeze.
  /// \param DemandedElts Vector elements that participate in the check.
  /// \param Kind Which of undef, poison, or both to freeze.
  /// \return A freeze of V if any of the demanded elts may be undef or poison.
  LLVM_ABI SDValue
  getFreeze(SDValue V, const APInt &DemandedElts,
            UndefPoisonKind Kind = UndefPoisonKind::UndefOrPoison);

  /// Return an AssertAlignSDNode.
  ///
  /// \param DL Debug location for the node.
  /// \param V Value whose alignment is asserted.
  /// \param A Asserted alignment.
  /// \return An AssertAlignSDNode.
  LLVM_ABI SDValue getAssertAlign(const SDLoc &DL, SDValue V, Align A);

  /// Swap N1 and N2 if Opcode is a commutative binary opcode
  /// and the canonical form expects the opposite order.
  ///
  /// \param Opcode Binary opcode that may be commutative.
  /// \param N1 First operand, possibly swapped.
  /// \param N2 Second operand, possibly swapped.
  LLVM_ABI void canonicalizeCommutativeBinop(unsigned Opcode, SDValue &N1,
                                             SDValue &N2) const;

  /// Return the specified value casted to
  /// the target's desired shift amount type.
  ///
  /// \param LHSTy Type of the shifted value.
  /// \param Op Shift-amount operand to cast.
  /// \return The specified value casted to the target's desired shift amount type.
  LLVM_ABI SDValue getShiftAmountOperand(EVT LHSTy, SDValue Op);

  /// Expand the specified \c ISD::VAARG node as the Legalize pass would.
  ///
  /// \param Node VAARG node to expand.
  /// \return The expanded va_arg result.
  LLVM_ABI SDValue expandVAArg(SDNode *Node);

  /// Expand the specified \c ISD::VACOPY node as the Legalize pass would.
  ///
  /// \param Node VACOPY node to expand.
  /// \return The expanded va_copy result.
  LLVM_ABI SDValue expandVACopy(SDNode *Node);

  /// Return a GlobalAddress for the function matching an ExternalSymbol.
  ///
  /// Looks up a function from the current module with name matching the given
  /// ExternalSymbol. Additionally can provide the matched function. Panics if
  /// the function doesn't exist.
  ///
  /// \param Op ExternalSymbol node whose name is matched.
  /// \param TargetFunction Optional out-parameter for the matched function.
  /// \return A GlobalAddress for the function matching an ExternalSymbol.
  LLVM_ABI SDValue getSymbolFunctionGlobalAddress(
      SDValue Op, Function **TargetFunction = nullptr);

  /// Mutate \p N in place to have the specified operands.
  ///
  /// If the resultant node already exists in the DAG, this does not modify the
  /// specified node, instead it returns the node that already exists. If the
  /// resultant node does not exist in the DAG, the input node is returned. As a
  /// degenerate case, if you specify the same input operands as the node
  /// already has, the input node is returned.
  ///
  /// \param N Node whose operands are replaced.
  /// \param Op New single operand.
  /// \return The updated node (possibly a CSE'd existing node).
  LLVM_ABI SDNode *UpdateNodeOperands(SDNode *N, SDValue Op);
  /// Mutate \p N in place to have the specified operands.
  ///
  /// \param N Node whose operands are replaced.
  /// \param Op1 First new operand.
  /// \param Op2 Second new operand.
  /// \return The updated node (possibly a CSE'd existing node).
  LLVM_ABI SDNode *UpdateNodeOperands(SDNode *N, SDValue Op1, SDValue Op2);
  /// Mutate \p N in place to have the specified operands.
  ///
  /// \param N Node whose operands are replaced.
  /// \param Op1 First new operand.
  /// \param Op2 Second new operand.
  /// \param Op3 Third new operand.
  /// \return The updated node (possibly a CSE'd existing node).
  LLVM_ABI SDNode *UpdateNodeOperands(SDNode *N, SDValue Op1, SDValue Op2,
                                      SDValue Op3);
  /// Mutate \p N in place to have the specified operands.
  ///
  /// \param N Node whose operands are replaced.
  /// \param Op1 First new operand.
  /// \param Op2 Second new operand.
  /// \param Op3 Third new operand.
  /// \param Op4 Fourth new operand.
  /// \return The updated node (possibly a CSE'd existing node).
  LLVM_ABI SDNode *UpdateNodeOperands(SDNode *N, SDValue Op1, SDValue Op2,
                                      SDValue Op3, SDValue Op4);
  /// Mutate \p N in place to have the specified operands.
  ///
  /// \param N Node whose operands are replaced.
  /// \param Op1 First new operand.
  /// \param Op2 Second new operand.
  /// \param Op3 Third new operand.
  /// \param Op4 Fourth new operand.
  /// \param Op5 Fifth new operand.
  /// \return The updated node (possibly a CSE'd existing node).
  LLVM_ABI SDNode *UpdateNodeOperands(SDNode *N, SDValue Op1, SDValue Op2,
                                      SDValue Op3, SDValue Op4, SDValue Op5);
  /// Mutate \p N in place to have the specified operands.
  ///
  /// \param N Node whose operands are replaced.
  /// \param Ops New operand list.
  /// \return The updated node (possibly a CSE'd existing node).
  LLVM_ABI SDNode *UpdateNodeOperands(SDNode *N, ArrayRef<SDValue> Ops);

  /// Create a TokenFactor for \p Vals, splitting at 64k operands if needed.
  ///
  /// If \p Vals contains 64k values or more, move values into new TokenFactors
  /// in 64k-1 blocks, until the final TokenFactor has less than 64k operands.
  ///
  /// \param DL Debug location for the TokenFactor.
  /// \param Vals Values combined by the TokenFactor.
  /// \return A TokenFactor combining the chain operands.
  LLVM_ABI SDValue getTokenFactor(const SDLoc &DL,
                                  SmallVectorImpl<SDValue> &Vals);

  /// *Mutate* the specified machine node's memory references to the provided
  /// list.
  ///
  /// \param N Machine node whose memory operands are replaced.
  /// \param NewMemRefs Replacement memory operands.
  LLVM_ABI void setNodeMemRefs(MachineSDNode *N,
                               ArrayRef<MachineMemOperand *> NewMemRefs);

  /// Calculate divergence of node \p N based on its operands.
  ///
  /// \param N Node whose divergence is computed.
  /// \return True if the node is divergent.
  LLVM_ABI bool calculateDivergence(SDNode *N);

  /// Propagate a change in divergence from \p N to its users.
  ///
  /// \param N Node whose divergence change is propagated.
  LLVM_ABI void updateDivergence(SDNode *N);

  /// Mutate \p N into a machine node with the given opcode and result type.
  ///
  /// Target selectors use these to mutate a node to have the specified return
  /// type, target opcode, and operands. Target opcodes are stored as
  /// ~TargetOpcode in the node opcode field. The resultant node is returned.
  ///
  /// \param N Node to mutate.
  /// \param MachineOpc Target machine opcode.
  /// \param VT Result value type.
  /// \return The selected machine node.
  LLVM_ABI SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT);
  /// Mutate \p N into a machine node with the given opcode, type, and operand.
  ///
  /// \param N Node to mutate.
  /// \param MachineOpc Target machine opcode.
  /// \param VT Result value type.
  /// \param Op1 First operand.
  /// \return The selected machine node.
  LLVM_ABI SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT,
                                SDValue Op1);
  /// Mutate \p N into a machine node with the given opcode, type, and operands.
  ///
  /// \param N Node to mutate.
  /// \param MachineOpc Target machine opcode.
  /// \param VT Result value type.
  /// \param Op1 First operand.
  /// \param Op2 Second operand.
  /// \return The selected machine node.
  LLVM_ABI SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT,
                                SDValue Op1, SDValue Op2);
  /// Mutate \p N into a machine node with the given opcode, type, and operands.
  ///
  /// \param N Node to mutate.
  /// \param MachineOpc Target machine opcode.
  /// \param VT Result value type.
  /// \param Op1 First operand.
  /// \param Op2 Second operand.
  /// \param Op3 Third operand.
  /// \return The selected machine node.
  LLVM_ABI SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT,
                                SDValue Op1, SDValue Op2, SDValue Op3);
  /// Mutate \p N into a machine node with the given opcode, type, and operands.
  ///
  /// \param N Node to mutate.
  /// \param MachineOpc Target machine opcode.
  /// \param VT Result value type.
  /// \param Ops Operand list.
  /// \return The selected machine node.
  LLVM_ABI SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT,
                                ArrayRef<SDValue> Ops);
  /// Mutate \p N into a machine node with two result types.
  ///
  /// \param N Node to mutate.
  /// \param MachineOpc Target machine opcode.
  /// \param VT1 First result type.
  /// \param VT2 Second result type.
  /// \return The selected machine node.
  LLVM_ABI SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT1,
                                EVT VT2);
  /// Mutate \p N into a machine node with two result types and operands.
  ///
  /// \param N Node to mutate.
  /// \param MachineOpc Target machine opcode.
  /// \param VT1 First result type.
  /// \param VT2 Second result type.
  /// \param Ops Operand list.
  /// \return The selected machine node.
  LLVM_ABI SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT1,
                                EVT VT2, ArrayRef<SDValue> Ops);
  /// Mutate \p N into a machine node with three result types and operands.
  ///
  /// \param N Node to mutate.
  /// \param MachineOpc Target machine opcode.
  /// \param VT1 First result type.
  /// \param VT2 Second result type.
  /// \param VT3 Third result type.
  /// \param Ops Operand list.
  /// \return The selected machine node.
  LLVM_ABI SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT1,
                                EVT VT2, EVT VT3, ArrayRef<SDValue> Ops);
  /// Mutate \p N into a machine node with two result types and two operands.
  ///
  /// \param N Node to mutate.
  /// \param MachineOpc Target machine opcode.
  /// \param VT1 First result type.
  /// \param VT2 Second result type.
  /// \param Op1 First operand.
  /// \param Op2 Second operand.
  /// \return The selected machine node.
  LLVM_ABI SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, EVT VT1,
                                EVT VT2, SDValue Op1, SDValue Op2);
  /// Mutate \p N into a machine node with the given type list and operands.
  ///
  /// \param N Node to mutate.
  /// \param MachineOpc Target machine opcode.
  /// \param VTs Result value types.
  /// \param Ops Operand list.
  /// \return The selected machine node.
  LLVM_ABI SDNode *SelectNodeTo(SDNode *N, unsigned MachineOpc, SDVTList VTs,
                                ArrayRef<SDValue> Ops);

  /// This *mutates* the specified node to have the specified
  /// return type, opcode, and operands.
  ///
  /// \param N Node to mutate.
  /// \param Opc New ISD or machine opcode.
  /// \param VTs Result value types.
  /// \param Ops New operand list.
  /// \return The morphed node.
  LLVM_ABI SDNode *MorphNodeTo(SDNode *N, unsigned Opc, SDVTList VTs,
                               ArrayRef<SDValue> Ops);

  /// Mutate a strict FP node to its non-strict equivalent.
  ///
  /// Unlinks the node from its chain and drops the metadata arguments. The
  /// node must be a strict FP node.
  ///
  /// \param Node Strict FP node to convert.
  /// \return The mutated non-strict FP node.
  LLVM_ABI SDNode *mutateStrictFPToFP(SDNode *Node);

  /// Create a machine node with the given opcode and a single result type.
  ///
  /// Target selectors use these to create a new node with specified return
  /// type(s), MachineInstr opcode, and operands. If a node of the specified
  /// opcode and operands already exists, that node is returned instead.
  ///
  /// \param Opcode Target machine opcode.
  /// \param dl Debug location for the node.
  /// \param VT Result value type.
  /// \return The machine SDNode.
  LLVM_ABI MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl,
                                         EVT VT);
  /// Create a machine node with one result type and one operand.
  ///
  /// \param Opcode Target machine opcode.
  /// \param dl Debug location for the node.
  /// \param VT Result value type.
  /// \param Op1 First operand.
  /// \return The machine SDNode.
  LLVM_ABI MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl,
                                         EVT VT, SDValue Op1);
  /// Create a machine node with one result type and two operands.
  ///
  /// \param Opcode Target machine opcode.
  /// \param dl Debug location for the node.
  /// \param VT Result value type.
  /// \param Op1 First operand.
  /// \param Op2 Second operand.
  /// \return The machine SDNode.
  LLVM_ABI MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl,
                                         EVT VT, SDValue Op1, SDValue Op2);
  /// Create a machine node with one result type and three operands.
  ///
  /// \param Opcode Target machine opcode.
  /// \param dl Debug location for the node.
  /// \param VT Result value type.
  /// \param Op1 First operand.
  /// \param Op2 Second operand.
  /// \param Op3 Third operand.
  /// \return The machine SDNode.
  LLVM_ABI MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl,
                                         EVT VT, SDValue Op1, SDValue Op2,
                                         SDValue Op3);
  /// Create a machine node with one result type and an operand list.
  ///
  /// \param Opcode Target machine opcode.
  /// \param dl Debug location for the node.
  /// \param VT Result value type.
  /// \param Ops Operand list.
  /// \return The machine SDNode.
  LLVM_ABI MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl,
                                         EVT VT, ArrayRef<SDValue> Ops);
  /// Create a machine node with two result types and two operands.
  ///
  /// \param Opcode Target machine opcode.
  /// \param dl Debug location for the node.
  /// \param VT1 First result type.
  /// \param VT2 Second result type.
  /// \param Op1 First operand.
  /// \param Op2 Second operand.
  /// \return The machine SDNode.
  LLVM_ABI MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl,
                                         EVT VT1, EVT VT2, SDValue Op1,
                                         SDValue Op2);
  /// Create a machine node with two result types and three operands.
  ///
  /// \param Opcode Target machine opcode.
  /// \param dl Debug location for the node.
  /// \param VT1 First result type.
  /// \param VT2 Second result type.
  /// \param Op1 First operand.
  /// \param Op2 Second operand.
  /// \param Op3 Third operand.
  /// \return The machine SDNode.
  LLVM_ABI MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl,
                                         EVT VT1, EVT VT2, SDValue Op1,
                                         SDValue Op2, SDValue Op3);
  /// Create a machine node with two result types and an operand list.
  ///
  /// \param Opcode Target machine opcode.
  /// \param dl Debug location for the node.
  /// \param VT1 First result type.
  /// \param VT2 Second result type.
  /// \param Ops Operand list.
  /// \return The machine SDNode.
  LLVM_ABI MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl,
                                         EVT VT1, EVT VT2,
                                         ArrayRef<SDValue> Ops);
  /// Create a machine node with three result types and two operands.
  ///
  /// \param Opcode Target machine opcode.
  /// \param dl Debug location for the node.
  /// \param VT1 First result type.
  /// \param VT2 Second result type.
  /// \param VT3 Third result type.
  /// \param Op1 First operand.
  /// \param Op2 Second operand.
  /// \return The machine SDNode.
  LLVM_ABI MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl,
                                         EVT VT1, EVT VT2, EVT VT3, SDValue Op1,
                                         SDValue Op2);
  /// Create a machine node with three result types and three operands.
  ///
  /// \param Opcode Target machine opcode.
  /// \param dl Debug location for the node.
  /// \param VT1 First result type.
  /// \param VT2 Second result type.
  /// \param VT3 Third result type.
  /// \param Op1 First operand.
  /// \param Op2 Second operand.
  /// \param Op3 Third operand.
  /// \return The machine SDNode.
  LLVM_ABI MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl,
                                         EVT VT1, EVT VT2, EVT VT3, SDValue Op1,
                                         SDValue Op2, SDValue Op3);
  /// Create a machine node with three result types and an operand list.
  ///
  /// \param Opcode Target machine opcode.
  /// \param dl Debug location for the node.
  /// \param VT1 First result type.
  /// \param VT2 Second result type.
  /// \param VT3 Third result type.
  /// \param Ops Operand list.
  /// \return The machine SDNode.
  LLVM_ABI MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl,
                                         EVT VT1, EVT VT2, EVT VT3,
                                         ArrayRef<SDValue> Ops);
  /// Create a machine node with a list of result types and operands.
  ///
  /// \param Opcode Target machine opcode.
  /// \param dl Debug location for the node.
  /// \param ResultTys Result value types.
  /// \param Ops Operand list.
  /// \return The machine SDNode.
  LLVM_ABI MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl,
                                         ArrayRef<EVT> ResultTys,
                                         ArrayRef<SDValue> Ops);
  /// Create a machine node with an SDVTList of result types and operands.
  ///
  /// \param Opcode Target machine opcode.
  /// \param dl Debug location for the node.
  /// \param VTs Result value types.
  /// \param Ops Operand list.
  /// \return The machine SDNode.
  LLVM_ABI MachineSDNode *getMachineNode(unsigned Opcode, const SDLoc &dl,
                                         SDVTList VTs, ArrayRef<SDValue> Ops);

  /// A convenience function for creating TargetInstrInfo::EXTRACT_SUBREG nodes.
  ///
  /// \param SRIdx Subregister index to extract.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param Operand Super-register operand.
  /// \return The target extract-subreg node.
  LLVM_ABI SDValue getTargetExtractSubreg(int SRIdx, const SDLoc &DL, EVT VT,
                                          SDValue Operand);

  /// A convenience function for creating TargetInstrInfo::INSERT_SUBREG nodes.
  ///
  /// \param SRIdx Subregister index to insert into.
  /// \param DL Debug location for the node.
  /// \param VT Result value type.
  /// \param Operand Super-register operand.
  /// \param Subreg Value inserted into the subregister.
  /// \return The target insert-subreg node.
  LLVM_ABI SDValue getTargetInsertSubreg(int SRIdx, const SDLoc &DL, EVT VT,
                                         SDValue Operand, SDValue Subreg);

  /// Get the specified node if it's already available, or else return NULL.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param VTList Result value types.
  /// \param Ops Operand list of the node.
  /// \param Flags Node flags that must match.
  /// \param AllowCommute True to also match commuted operands.
  /// \return The existing node, or null if none matches.
  LLVM_ABI SDNode *getNodeIfExists(unsigned Opcode, SDVTList VTList,
                                   ArrayRef<SDValue> Ops,
                                   const SDNodeFlags Flags,
                                   bool AllowCommute = false);
  /// Get the specified node if it's already available, or else return NULL.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param VTList Result value types.
  /// \param Ops Operand list of the node.
  /// \param AllowCommute True to also match commuted operands.
  /// \return The existing node, or null if none matches.
  LLVM_ABI SDNode *getNodeIfExists(unsigned Opcode, SDVTList VTList,
                                   ArrayRef<SDValue> Ops,
                                   bool AllowCommute = false);

  /// Check if a node exists without modifying its flags.
  ///
  /// \param Opcode ISD opcode of the node.
  /// \param VTList Result value types.
  /// \param Ops Operand list of the node.
  /// \return True if a matching node already exists.
  LLVM_ABI bool doesNodeExist(unsigned Opcode, SDVTList VTList,
                              ArrayRef<SDValue> Ops);

  /// Creates a SDDbgValue node.
  ///
  /// \param Var Debug variable being described.
  /// \param Expr Debug expression applied to the location.
  /// \param N Node that produces the value.
  /// \param R Result number of \p N to describe.
  /// \param IsIndirect True if the location is an indirect address.
  /// \param DL Debug location of the dbg_value.
  /// \param O IR order of the dbg_value.
  /// \return The SDDbgValue node.
  LLVM_ABI SDDbgValue *getDbgValue(DIVariable *Var, DIExpression *Expr,
                                   SDNode *N, unsigned R, bool IsIndirect,
                                   const DebugLoc &DL, unsigned O);

  /// Creates a constant SDDbgValue node.
  ///
  /// \param Var Debug variable being described.
  /// \param Expr Debug expression applied to the location.
  /// \param C Constant IR value.
  /// \param DL Debug location of the dbg_value.
  /// \param O IR order of the dbg_value.
  /// \return The constant SDDbgValue node.
  LLVM_ABI SDDbgValue *getConstantDbgValue(DIVariable *Var, DIExpression *Expr,
                                           const Value *C, const DebugLoc &DL,
                                           unsigned O);

  /// Creates a FrameIndex SDDbgValue node.
  ///
  /// \param Var Debug variable being described.
  /// \param Expr Debug expression applied to the location.
  /// \param FI Frame index of the location.
  /// \param IsIndirect True if the location is an indirect address.
  /// \param DL Debug location of the dbg_value.
  /// \param O IR order of the dbg_value.
  /// \return The FrameIndex SDDbgValue node.
  LLVM_ABI SDDbgValue *getFrameIndexDbgValue(DIVariable *Var,
                                             DIExpression *Expr, unsigned FI,
                                             bool IsIndirect,
                                             const DebugLoc &DL, unsigned O);

  /// Creates a FrameIndex SDDbgValue node.
  ///
  /// \param Var Debug variable being described.
  /// \param Expr Debug expression applied to the location.
  /// \param FI Frame index of the location.
  /// \param Dependencies Nodes that must stay alive for this debug value.
  /// \param IsIndirect True if the location is an indirect address.
  /// \param DL Debug location of the dbg_value.
  /// \param O IR order of the dbg_value.
  /// \return The FrameIndex SDDbgValue node.
  LLVM_ABI SDDbgValue *getFrameIndexDbgValue(DIVariable *Var,
                                             DIExpression *Expr, unsigned FI,
                                             ArrayRef<SDNode *> Dependencies,
                                             bool IsIndirect,
                                             const DebugLoc &DL, unsigned O);

  /// Creates a VReg SDDbgValue node.
  ///
  /// \param Var Debug variable being described.
  /// \param Expr Debug expression applied to the location.
  /// \param VReg Virtual register that holds the value.
  /// \param IsIndirect True if the location is an indirect address.
  /// \param DL Debug location of the dbg_value.
  /// \param O IR order of the dbg_value.
  /// \return The VReg SDDbgValue node.
  LLVM_ABI SDDbgValue *getVRegDbgValue(DIVariable *Var, DIExpression *Expr,
                                       Register VReg, bool IsIndirect,
                                       const DebugLoc &DL, unsigned O);

  /// Creates a SDDbgValue node from a list of locations.
  ///
  /// \param Var Debug variable being described.
  /// \param Expr Debug expression applied to the locations.
  /// \param Locs Debug locations that contribute to the value.
  /// \param Dependencies Nodes that must stay alive for this debug value.
  /// \param IsIndirect True if the location is an indirect address.
  /// \param DL Debug location of the dbg_value.
  /// \param O IR order of the dbg_value.
  /// \param IsVariadic True if the dbg_value is variadic.
  /// \return The SDDbgValue node from a list of locations.
  LLVM_ABI SDDbgValue *getDbgValueList(DIVariable *Var, DIExpression *Expr,
                                       ArrayRef<SDDbgOperand> Locs,
                                       ArrayRef<SDNode *> Dependencies,
                                       bool IsIndirect, const DebugLoc &DL,
                                       unsigned O, bool IsVariadic);

  /// Creates a SDDbgLabel node.
  ///
  /// \param Label Debug label metadata.
  /// \param DL Debug location of the label.
  /// \param O IR order of the label.
  /// \return The SDDbgLabel node.
  LLVM_ABI SDDbgLabel *getDbgLabel(DILabel *Label, const DebugLoc &DL,
                                   unsigned O);

  /// Transfer debug values from one node to another.
  ///
  /// Optionally generates fragment expressions for split-up values. If
  /// \p InvalidateDbg is set, debug values are invalidated after they are
  /// transferred.
  ///
  /// \param From Node whose debug values are moved.
  /// \param To Node that receives the debug values.
  /// \param OffsetInBits Bit offset of the fragment in \p From.
  /// \param SizeInBits Bit size of the fragment, or 0 for the whole value.
  /// \param InvalidateDbg True to invalidate transferred debug values.
  LLVM_ABI void transferDbgValues(SDValue From, SDValue To,
                                  unsigned OffsetInBits = 0,
                                  unsigned SizeInBits = 0,
                                  bool InvalidateDbg = true);

  /// Remove the specified node from the system. If any of its
  /// operands then becomes dead, remove them as well. Inform UpdateListener
  /// for each node deleted.
  ///
  /// \param N Node to remove.
  LLVM_ABI void RemoveDeadNode(SDNode *N);

  /// This method deletes the unreachable nodes in the
  /// given list, and any nodes that become unreachable as a result.
  ///
  /// \param DeadNodes Nodes known to be dead; more may be appended.
  LLVM_ABI void RemoveDeadNodes(SmallVectorImpl<SDNode *> &DeadNodes);

  /// Replace all uses of \p From with \p To, possibly merging DAG nodes.
  ///
  /// Use this overload when \p From is known to have a single result.
  /// These methods all take an optional UpdateListener, which (if not null) is
  /// informed about nodes that are deleted and modified due to recursive
  /// changes in the dag.
  ///
  /// These functions only replace all existing uses. It's possible that as
  /// these replacements are being performed, CSE may cause the From node
  /// to be given new uses. These new uses of From are left in place, and
  /// not automatically transferred to To.
  ///
  /// \param From Value whose uses are replaced.
  /// \param To Replacement value.
  LLVM_ABI void ReplaceAllUsesWith(SDValue From, SDValue To);
  /// Replace all uses of node \p From with node \p To.
  ///
  /// Use this overload when both nodes have identical results (or \p To has a
  /// superset of the results of \p From).
  ///
  /// \param From Node whose uses are replaced.
  /// \param To Replacement node.
  LLVM_ABI void ReplaceAllUsesWith(SDNode *From, SDNode *To);
  /// Replace all uses of node \p From with the values in \p To.
  ///
  /// Use this overload when \p From produces multiple values.
  ///
  /// \param From Node whose uses are replaced.
  /// \param To Array of replacement values, one per result of \p From.
  LLVM_ABI void ReplaceAllUsesWith(SDNode *From, const SDValue *To);

  /// Replace any uses of From with To, leaving
  /// uses of other values produced by From.getNode() alone.
  ///
  /// \param From Value whose uses are replaced.
  /// \param To Replacement value.
  LLVM_ABI void ReplaceAllUsesOfValueWith(SDValue From, SDValue To);

  /// Like ReplaceAllUsesOfValueWith, but for multiple values at once.
  ///
  /// This correctly handles the case where there is an overlap between the From
  /// values and the To values.
  ///
  /// \param From Array of values whose uses are replaced.
  /// \param To Array of replacement values.
  /// \param Num Number of values in \p From and \p To.
  LLVM_ABI void ReplaceAllUsesOfValuesWith(const SDValue *From,
                                           const SDValue *To, unsigned Num);

  /// Merge \p OldChain with \p NewMemOpChain so both keep the same order.
  ///
  /// If an existing load has uses of its chain, create a token factor node with
  /// that chain and the new memory node's chain and update users of the old
  /// chain to the token factor. This ensures that the new memory node will have
  /// the same relative memory dependency position as the old load. Returns the
  /// new merged load chain.
  ///
  /// \param OldChain Existing memory chain whose users are updated.
  /// \param NewMemOpChain Chain of the replacement memory operation.
  /// \return A chain that orders the loads equivalently.
  LLVM_ABI SDValue makeEquivalentMemoryOrdering(SDValue OldChain,
                                                SDValue NewMemOpChain);

  /// Merge \p OldLoad's chain with \p NewMemOp so both keep the same order.
  ///
  /// If an existing load has uses of its chain, create a token factor node with
  /// that chain and the new memory node's chain and update users of the old
  /// chain to the token factor. This ensures that the new memory node will have
  /// the same relative memory dependency position as the old load. Returns the
  /// new merged load chain.
  ///
  /// \param OldLoad Existing load whose chain users are updated.
  /// \param NewMemOp Replacement memory operation.
  /// \return A chain that orders the loads equivalently.
  LLVM_ABI SDValue makeEquivalentMemoryOrdering(LoadSDNode *OldLoad,
                                                SDValue NewMemOp);

  /// Get all the nodes in their topological order without modifying any states.
  ///
  /// \param SortedNodes Destination that receives nodes in topological order.
  LLVM_ABI void getTopologicallyOrderedNodes(
      SmallVectorImpl<const SDNode *> &SortedNodes) const;

  /// Topological-sort the AllNodes list and a
  /// assign a unique node id for each node in the DAG based on their
  /// topological order. Returns the number of nodes.
  ///
  /// \return The number of nodes in the topological ordering.
  LLVM_ABI unsigned AssignTopologicalOrder();

  /// Move node N in the AllNodes list to immediately before \p Position.
  ///
  /// This may be used to update the topological ordering when the list of nodes
  /// is modified.
  ///
  /// \param Position Insertion point in the AllNodes list.
  /// \param N Node to move.
  void RepositionNode(allnodes_iterator Position, SDNode *N) {
    AllNodes.insert(Position, AllNodes.remove(N));
  }

  /// Add a dbg_value to the DAG's side table.
  ///
  /// \param DB Debug-value descriptor to add.
  /// \param isParameter True if the debug value describes a parameter.
  LLVM_ABI void AddDbgValue(SDDbgValue *DB, bool isParameter);

  /// Add a dbg_label SDNode.
  ///
  /// \param DB Debug-label descriptor to add.
  LLVM_ABI void AddDbgLabel(SDDbgLabel *DB);

  /// Get the debug values which reference the given SDNode.
  ///
  /// \param SD Node whose attached debug values are requested.
  /// \return The debug values which reference the given SDNode.
  ArrayRef<SDDbgValue*> GetDbgValues(const SDNode* SD) const {
    return DbgInfo->getSDDbgValues(SD);
  }

public:
  /// Return true if there are any SDDbgValue nodes associated
  /// with this SelectionDAG.
  ///
  /// \return True if there are any SDDbgValue nodes associated with this SelectionDAG.
  bool hasDebugValues() const { return !DbgInfo->empty(); }

  /// Return an iterator to the first SDDbgValue.
  ///
  /// \return An iterator to the first SDDbgValue.
  SDDbgInfo::DbgIterator DbgBegin() const { return DbgInfo->DbgBegin(); }
  /// Return an iterator one past the last SDDbgValue.
  ///
  /// \return An iterator one past the last SDDbgValue.
  SDDbgInfo::DbgIterator DbgEnd() const  { return DbgInfo->DbgEnd(); }

  /// Return an iterator to the first byval-parameter SDDbgValue.
  ///
  /// \return An iterator to the first byval-parameter SDDbgValue.
  SDDbgInfo::DbgIterator ByvalParmDbgBegin() const {
    return DbgInfo->ByvalParmDbgBegin();
  }
  /// Return an iterator one past the last byval-parameter SDDbgValue.
  ///
  /// \return An iterator one past the last byval-parameter SDDbgValue.
  SDDbgInfo::DbgIterator ByvalParmDbgEnd() const {
    return DbgInfo->ByvalParmDbgEnd();
  }

  /// Return an iterator to the first SDDbgLabel.
  ///
  /// \return An iterator to the first SDDbgLabel.
  SDDbgInfo::DbgLabelIterator DbgLabelBegin() const {
    return DbgInfo->DbgLabelBegin();
  }
  /// Return an iterator one past the last SDDbgLabel.
  ///
  /// \return An iterator one past the last SDDbgLabel.
  SDDbgInfo::DbgLabelIterator DbgLabelEnd() const {
    return DbgInfo->DbgLabelEnd();
  }

  /// To be invoked on an SDNode that is slated to be erased. This
  /// function mirrors \c llvm::salvageDebugInfo.
  ///
  /// \param N Node being erased whose debug info may be salvaged.
  LLVM_ABI void salvageDebugInfo(SDNode &N);

  /// Dump the textual format of this DAG without sorting nodes.
  ///
  /// Overloaded instead of using a default so it is convenient to call from
  /// debuggers.
  LLVM_ABI void dump() const;

  /// Dump the textual format of this DAG.
  ///
  /// Print nodes in sorted order if \p Sorted is true.
  ///
  /// \param Sorted True to print nodes in topological order.
  LLVM_ABI void dump(bool Sorted) const;

  /// Return a reduced alignment for \p VT when the ABI alignment is too large.
  ///
  /// In most cases this function returns the ABI alignment for a given type,
  /// except for illegal vector types where the alignment exceeds that of the
  /// stack. In such cases we attempt to break the vector down to a legal type
  /// and return the ABI alignment for that instead.
  ///
  /// \param VT Type whose alignment is requested.
  /// \param UseABI True to start from the ABI alignment rather than preferred.
  /// \return The reduced alignment for the given type.
  LLVM_ABI Align getReducedAlign(EVT VT, bool UseABI);

  /// Create a stack temporary based on the size in bytes and the alignment.
  ///
  /// \param Bytes Size of the stack slot in bytes.
  /// \param Alignment Alignment of the stack slot.
  /// \return A frame-index SDValue for the stack temporary.
  LLVM_ABI SDValue CreateStackTemporary(TypeSize Bytes, Align Alignment);

  /// Create a stack temporary, suitable for holding the specified value type.
  /// If minAlign is specified, the slot size will have at least that alignment.
  ///
  /// \param VT Value type the slot must hold.
  /// \param minAlign Minimum alignment of the stack slot in bytes.
  /// \return A frame-index SDValue for the stack temporary.
  LLVM_ABI SDValue CreateStackTemporary(EVT VT, unsigned minAlign = 1);

  /// Create a stack temporary suitable for holding either of the specified
  /// value types.
  ///
  /// \param VT1 First value type the slot must hold.
  /// \param VT2 Second value type the slot must hold.
  /// \return A frame-index SDValue for the stack temporary.
  LLVM_ABI SDValue CreateStackTemporary(EVT VT1, EVT VT2);

  /// Fold a binary operation of a global address and a constant offset.
  ///
  /// \param Opcode Opcode of the addressing operation.
  /// \param VT Result value type.
  /// \param GA Global-address node on the left.
  /// \param N2 Right-hand operand, typically a constant offset.
  /// \return The folded symbol-offset value, or an empty SDValue.
  LLVM_ABI SDValue FoldSymbolOffset(unsigned Opcode, EVT VT,
                                    const GlobalAddressSDNode *GA,
                                    const SDNode *N2);

  /// Fold a constant arithmetic operation if all operands are constants.
  ///
  /// \param Opcode Arithmetic opcode to fold.
  /// \param DL Debug location for the folded node.
  /// \param VT Result value type.
  /// \param Ops Operands of the arithmetic operation.
  /// \param Flags Node flags for the operation.
  /// \return The folded arithmetic value, or an empty SDValue.
  LLVM_ABI SDValue FoldConstantArithmetic(unsigned Opcode, const SDLoc &DL,
                                          EVT VT, ArrayRef<SDValue> Ops,
                                          SDNodeFlags Flags = SDNodeFlags());

  /// Fold floating-point operations when all operands are constants and/or
  /// undefined.
  ///
  /// \param Opcode Floating-point opcode to fold.
  /// \param DL Debug location for the folded node.
  /// \param VT Result value type.
  /// \param Ops Operands of the floating-point operation.
  /// \return The folded FP math value, or an empty SDValue.
  LLVM_ABI SDValue foldConstantFPMath(unsigned Opcode, const SDLoc &DL, EVT VT,
                                      ArrayRef<SDValue> Ops);

  /// Fold BUILD_VECTOR of constants/undefs to the destination type
  /// BUILD_VECTOR of constants/undefs elements.
  ///
  /// \param BV BUILD_VECTOR node to fold.
  /// \param DL Debug location for the folded node.
  /// \param DstEltVT Destination element type.
  /// \return The folded build-vector value, or an empty SDValue.
  LLVM_ABI SDValue FoldConstantBuildVector(BuildVectorSDNode *BV,
                                           const SDLoc &DL, EVT DstEltVT);

  /// Constant fold a setcc to true or false.
  ///
  /// \param VT Result type of the setcc.
  /// \param N1 Left comparison operand.
  /// \param N2 Right comparison operand.
  /// \param Cond Comparison condition code.
  /// \param dl Debug location for the folded node.
  /// \param Flags Node flags for the comparison.
  /// \return The folded setcc value, or an empty SDValue if not folded.
  LLVM_ABI SDValue FoldSetCC(EVT VT, SDValue N1, SDValue N2, ISD::CondCode Cond,
                             const SDLoc &dl, SDNodeFlags Flags = {});

  /// Return true if the sign bit of Op is known to be zero.
  /// We use this predicate to simplify operations downstream.
  ///
  /// \param Op Value to test.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if the sign bit of Op is known to be zero.
  LLVM_ABI bool SignBitIsZero(SDValue Op, unsigned Depth = 0) const;

  /// Return true if the sign bit of Op is known to be zero, for a
  /// floating-point value.
  ///
  /// \param Op Floating-point value to test.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if the sign bit of Op is known to be zero, for a floating-point value.
  LLVM_ABI bool SignBitIsZeroFP(SDValue Op, unsigned Depth = 0) const;

  /// Return true if 'Op & Mask' is known to be zero.
  ///
  /// We use this predicate to simplify operations downstream. Op and Mask are
  /// known to be the same type.
  ///
  /// \param Op Value to test.
  /// \param Mask Bits that must be zero in \p Op.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if 'Op & Mask' is known to be zero.
  LLVM_ABI bool MaskedValueIsZero(SDValue Op, const APInt &Mask,
                                  unsigned Depth = 0) const;

  /// Return true if 'Op & Mask' is known to be zero in DemandedElts.
  ///
  /// We use this predicate to simplify operations downstream. Op and Mask are
  /// known to be the same type.
  ///
  /// \param Op Value to test.
  /// \param Mask Bits that must be zero in \p Op.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if 'Op & Mask' is known to be zero in DemandedElts.
  LLVM_ABI bool MaskedValueIsZero(SDValue Op, const APInt &Mask,
                                  const APInt &DemandedElts,
                                  unsigned Depth = 0) const;

  /// Return true if 'Op' is known to be zero in DemandedElts.  We
  /// use this predicate to simplify operations downstream.
  ///
  /// \param Op Value to test.
  /// \param DemandedElts Vector elements that must be zero.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if 'Op' is known to be zero in DemandedElts.
  LLVM_ABI bool MaskedVectorIsZero(SDValue Op, const APInt &DemandedElts,
                                   unsigned Depth = 0) const;

  /// Return true if '(Op & Mask) == Mask'.
  /// Op and Mask are known to be the same type.
  ///
  /// \param Op Value to test.
  /// \param Mask Bits that must all be set in \p Op.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if '(Op & Mask) == Mask'.
  LLVM_ABI bool MaskedValueIsAllOnes(SDValue Op, const APInt &Mask,
                                     unsigned Depth = 0) const;

  /// For each demanded element of a vector, see if it is known to be zero.
  ///
  /// \param Op Vector value to analyze.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return For each demanded element of a vector, see if it is known to be zero.
  LLVM_ABI APInt computeVectorKnownZeroElements(SDValue Op,
                                                const APInt &DemandedElts,
                                                unsigned Depth = 0) const;

  /// Determine which bits of \p Op are known to be either zero or one.
  ///
  /// For vectors, the known bits are those that are shared by every vector
  /// element. Targets can implement computeKnownBitsForTargetNode in
  /// TargetLowering to allow target nodes to be understood.
  ///
  /// \param Op Value whose known bits are computed.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if which bits of \p Op are known to be either zero or one.
  LLVM_ABI KnownBits computeKnownBits(SDValue Op, unsigned Depth = 0) const;

  /// Determine which bits of \p Op are known zero or one in demanded elements.
  ///
  /// The DemandedElts argument allows us to only collect the known bits that
  /// are shared by the requested vector elements. Targets can implement
  /// computeKnownBitsForTargetNode in TargetLowering to allow target nodes to
  /// be understood.
  ///
  /// \param Op Value whose known bits are computed.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if which bits of \p Op are known zero or one in demanded elements.
  LLVM_ABI KnownBits computeKnownBits(SDValue Op, const APInt &DemandedElts,
                                      unsigned Depth = 0) const;

  /// Determine the possible constant range of an integer or vector of integers.
  ///
  /// \param Op Integer or integer-vector value to analyze.
  /// \param ForSigned True to interpret the range as signed.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if the possible constant range of an integer or vector of integers.
  LLVM_ABI ConstantRange computeConstantRange(SDValue Op, bool ForSigned,
                                              unsigned Depth = 0) const;

  /// Determine the possible constant range of an integer or vector of integers.
  ///
  /// The DemandedElts argument allows us to only collect the known ranges that
  /// are shared by the requested vector elements.
  ///
  /// \param Op Integer or integer-vector value to analyze.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param ForSigned True to interpret the range as signed.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if the possible constant range of an integer or vector of integers.
  LLVM_ABI ConstantRange computeConstantRange(SDValue Op,
                                              const APInt &DemandedElts,
                                              bool ForSigned,
                                              unsigned Depth = 0) const;

  /// Combine constant ranges from computeConstantRange() and
  /// computeKnownBits().
  ///
  /// \param Op Value whose constant range is computed.
  /// \param ForSigned True to interpret the range as signed.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return Combine constant ranges from computeConstantRange() and computeKnownBits().
  LLVM_ABI ConstantRange computeConstantRangeIncludingKnownBits(
      SDValue Op, bool ForSigned, unsigned Depth = 0) const;

  /// Combine constant ranges from computeConstantRange() and
  /// computeKnownBits().
  ///
  /// The DemandedElts argument allows us to only collect the known ranges that
  /// are shared by the requested vector elements.
  ///
  /// \param Op Value whose constant range is computed.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param ForSigned True to interpret the range as signed.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return Combine constant ranges from computeConstantRange() and computeKnownBits().
  LLVM_ABI ConstantRange computeConstantRangeIncludingKnownBits(
      SDValue Op, const APInt &DemandedElts, bool ForSigned,
      unsigned Depth = 0) const;

  /// Possible overflow behavior of an integer operation.
  ///
  /// Never: the operation cannot overflow.
  /// Always: the operation will always overflow.
  /// Sometime: the operation may or may not overflow.
  enum OverflowKind {
    OFK_Never,    ///< The operation cannot overflow.
    OFK_Sometime, ///< The operation may or may not overflow.
    OFK_Always,   ///< The operation always overflows.
  };

  /// Determine if the result of the signed addition of 2 nodes can overflow.
  ///
  /// \param N0 First addend.
  /// \param N1 Second addend.
  /// \return True if the result of the signed addition of 2 nodes can overflow.
  LLVM_ABI OverflowKind computeOverflowForSignedAdd(SDValue N0,
                                                    SDValue N1) const;

  /// Determine if the result of the unsigned addition of 2 nodes can overflow.
  ///
  /// \param N0 First addend.
  /// \param N1 Second addend.
  /// \return True if the result of the unsigned addition of 2 nodes can overflow.
  LLVM_ABI OverflowKind computeOverflowForUnsignedAdd(SDValue N0,
                                                      SDValue N1) const;

  /// Determine if the result of the addition of 2 nodes can overflow.
  ///
  /// \param IsSigned True for signed add overflow.
  /// \param N0 First addend.
  /// \param N1 Second addend.
  /// \return True if the result of the addition of 2 nodes can overflow.
  OverflowKind computeOverflowForAdd(bool IsSigned, SDValue N0,
                                     SDValue N1) const {
    return IsSigned ? computeOverflowForSignedAdd(N0, N1)
                    : computeOverflowForUnsignedAdd(N0, N1);
  }

  /// Determine if the result of the addition of 2 nodes can never overflow.
  ///
  /// \param IsSigned True for signed add overflow.
  /// \param N0 First addend.
  /// \param N1 Second addend.
  /// \return True if the result of the addition of 2 nodes can never overflow.
  bool willNotOverflowAdd(bool IsSigned, SDValue N0, SDValue N1) const {
    return computeOverflowForAdd(IsSigned, N0, N1) == OFK_Never;
  }

  /// Determine if the result of the signed sub of 2 nodes can overflow.
  ///
  /// \param N0 Minuend.
  /// \param N1 Subtrahend.
  /// \return True if the result of the signed sub of 2 nodes can overflow.
  LLVM_ABI OverflowKind computeOverflowForSignedSub(SDValue N0,
                                                    SDValue N1) const;

  /// Determine if the result of the unsigned sub of 2 nodes can overflow.
  ///
  /// \param N0 Minuend.
  /// \param N1 Subtrahend.
  /// \return True if the result of the unsigned sub of 2 nodes can overflow.
  LLVM_ABI OverflowKind computeOverflowForUnsignedSub(SDValue N0,
                                                      SDValue N1) const;

  /// Determine if the result of the sub of 2 nodes can overflow.
  ///
  /// \param IsSigned True for signed subtract overflow.
  /// \param N0 Minuend.
  /// \param N1 Subtrahend.
  /// \return True if the result of the sub of 2 nodes can overflow.
  OverflowKind computeOverflowForSub(bool IsSigned, SDValue N0,
                                     SDValue N1) const {
    return IsSigned ? computeOverflowForSignedSub(N0, N1)
                    : computeOverflowForUnsignedSub(N0, N1);
  }

  /// Determine if the result of the sub of 2 nodes can never overflow.
  ///
  /// \param IsSigned True for signed subtract overflow.
  /// \param N0 Minuend.
  /// \param N1 Subtrahend.
  /// \return True if the result of the sub of 2 nodes can never overflow.
  bool willNotOverflowSub(bool IsSigned, SDValue N0, SDValue N1) const {
    return computeOverflowForSub(IsSigned, N0, N1) == OFK_Never;
  }

  /// Determine if the result of the signed mul of 2 nodes can overflow.
  ///
  /// \param N0 First multiply operand.
  /// \param N1 Second multiply operand.
  /// \return True if the result of the signed mul of 2 nodes can overflow.
  LLVM_ABI OverflowKind computeOverflowForSignedMul(SDValue N0,
                                                    SDValue N1) const;

  /// Determine if the result of the unsigned mul of 2 nodes can overflow.
  ///
  /// \param N0 First multiply operand.
  /// \param N1 Second multiply operand.
  /// \return True if the result of the unsigned mul of 2 nodes can overflow.
  LLVM_ABI OverflowKind computeOverflowForUnsignedMul(SDValue N0,
                                                      SDValue N1) const;

  /// Determine if the result of the mul of 2 nodes can overflow.
  ///
  /// \param IsSigned True for signed multiply overflow.
  /// \param N0 First multiply operand.
  /// \param N1 Second multiply operand.
  /// \return True if the result of the mul of 2 nodes can overflow.
  OverflowKind computeOverflowForMul(bool IsSigned, SDValue N0,
                                     SDValue N1) const {
    return IsSigned ? computeOverflowForSignedMul(N0, N1)
                    : computeOverflowForUnsignedMul(N0, N1);
  }

  /// Determine if the result of the mul of 2 nodes can never overflow.
  ///
  /// \param IsSigned True for signed multiply overflow.
  /// \param N0 First multiply operand.
  /// \param N1 Second multiply operand.
  /// \return True if the result of the mul of 2 nodes can never overflow.
  bool willNotOverflowMul(bool IsSigned, SDValue N0, SDValue N1) const {
    return computeOverflowForMul(IsSigned, N0, N1) == OFK_Never;
  }

  /// Return true if \p V is an identity element of \p Opc with \p Flags.
  ///
  /// When OperandNo is 0, it checks that V is a left identity. Otherwise, it
  /// checks that V is a right identity.
  ///
  /// \param Opc Opcode whose identity is tested.
  /// \param Flags Node flags that select the identity.
  /// \param V Value that may be an identity element.
  /// \param OperandNo 0 for a left identity, nonzero for a right identity.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if \p V is an identity element of \p Opc with \p Flags.
  LLVM_ABI bool isIdentityElement(unsigned Opc, SDNodeFlags Flags, SDValue V,
                                  unsigned OperandNo, unsigned Depth = 0) const;

  /// Return true if demanded elements of \p V are an identity of \p Opc.
  ///
  /// When OperandNo is 0, it checks that V is a left identity. Otherwise, it
  /// checks that V is a right identity.
  ///
  /// \param Opc Opcode whose identity is tested.
  /// \param Flags Node flags that select the identity.
  /// \param V Value that may be an identity element.
  /// \param DemandedElts Vector elements that must be identity values.
  /// \param OperandNo 0 for a left identity, nonzero for a right identity.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if demanded elements of \p V are an identity of \p Opc.
  LLVM_ABI bool isIdentityElement(unsigned Opc, SDNodeFlags Flags, SDValue V,
                                  const APInt &DemandedElts, unsigned OperandNo,
                                  unsigned Depth = 0) const;

  /// Return true if \p Val is known to have exactly one bit set.
  ///
  /// This differs from computeKnownBits in that it doesn't necessarily
  /// determine which bit is set. If \p OrZero is set, then return true if the
  /// given value is either a power of two or zero.
  ///
  /// \param Val Value to test.
  /// \param OrZero True to also accept a zero value.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if \p Val is known to have exactly one bit set.
  LLVM_ABI bool isKnownToBeAPowerOfTwo(SDValue Val, bool OrZero = false,
                                       unsigned Depth = 0) const;

  /// Return true if \p Val is known to have exactly one bit set.
  ///
  /// This differs from computeKnownBits in that it doesn't necessarily
  /// determine which bit is set. The DemandedElts argument allows us to only
  /// collect the minimum sign bits of the requested vector elements. If
  /// \p OrZero is set, then return true if the given value is either a power of
  /// two or zero.
  ///
  /// \param Val Value to test.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param OrZero True to also accept a zero value.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if \p Val is known to have exactly one bit set.
  LLVM_ABI bool isKnownToBeAPowerOfTwo(SDValue Val, const APInt &DemandedElts,
                                       bool OrZero = false,
                                       unsigned Depth = 0) const;

  /// Test if the given _fp_ value is known to be an integer power-of-2, either
  /// positive or negative.
  ///
  /// \param Val Floating-point value to test.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if the given _fp_ value is known to be an integer power-of-2, either positive or negative.
  LLVM_ABI bool isKnownToBeAPowerOfTwoFP(SDValue Val, unsigned Depth = 0) const;

  /// Return how many times the sign bit of \p Op is replicated.
  ///
  /// At least 1 bit is always equal to the sign bit (itself). Other cases can
  /// give more information. For example, immediately after an "SRA X, 2", the
  /// top 3 bits are all equal, so this returns 3. Targets can implement
  /// ComputeNumSignBitsForTarget in TargetLowering to allow target nodes to be
  /// understood.
  ///
  /// \param Op Value whose sign bits are counted.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return How many times the sign bit of \p Op is replicated.
  LLVM_ABI unsigned ComputeNumSignBits(SDValue Op, unsigned Depth = 0) const;

  /// Return how many times the sign bit of \p Op is replicated.
  ///
  /// At least 1 bit is always equal to the sign bit (itself). Other cases can
  /// give more information. For example, immediately after an "SRA X, 2", the
  /// top 3 bits are all equal, so this returns 3. The DemandedElts argument
  /// allows us to only collect the minimum sign bits of the requested vector
  /// elements. Targets can implement ComputeNumSignBitsForTarget in
  /// TargetLowering to allow target nodes to be understood.
  ///
  /// \param Op Value whose sign bits are counted.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return How many times the sign bit of \p Op is replicated.
  LLVM_ABI unsigned ComputeNumSignBits(SDValue Op, const APInt &DemandedElts,
                                       unsigned Depth = 0) const;

  /// Get the upper bound on signed significant bits of \p Op.
  ///
  /// i.e. x == sext(trunc(x to MaxSignedBits) to bitwidth(x)).
  /// Similar to the APInt::getSignificantBits function.
  /// Helper wrapper to ComputeNumSignBits.
  ///
  /// \param Op Value whose significant-bit bound is computed.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return The upper bound on signed significant bits of \p Op.
  LLVM_ABI unsigned ComputeMaxSignificantBits(SDValue Op,
                                              unsigned Depth = 0) const;

  /// Get the upper bound on signed significant bits of \p Op.
  ///
  /// i.e. x == sext(trunc(x to MaxSignedBits) to bitwidth(x)).
  /// Similar to the APInt::getSignificantBits function.
  /// Helper wrapper to ComputeNumSignBits.
  ///
  /// \param Op Value whose significant-bit bound is computed.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return The upper bound on signed significant bits of \p Op.
  LLVM_ABI unsigned ComputeMaxSignificantBits(SDValue Op,
                                              const APInt &DemandedElts,
                                              unsigned Depth = 0) const;

  /// Return true if this function can prove that \p Op is never poison
  /// and, \p Kind can be used to track poison and/or undef bits.
  ///
  /// \param Op Value to test.
  /// \param Kind Which of undef, poison, or both to track.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if this function can prove that \p Op is never poison and, \p Kind can be used to track poison and/or undef bits.
  LLVM_ABI bool isGuaranteedNotToBeUndefOrPoison(
      SDValue Op, UndefPoisonKind Kind = UndefPoisonKind::UndefOrPoison,
      unsigned Depth = 0) const;

  /// Return true if \p Op is never poison or undef in the demanded elements.
  ///
  /// \p Kind selects whether poison, undef, or both are tracked. The
  /// DemandedElts argument limits the check to the requested vector elements.
  ///
  /// \param Op Value to test.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param Kind Which of undef, poison, or both to track.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if \p Op is never poison or undef in the demanded elements.
  LLVM_ABI bool isGuaranteedNotToBeUndefOrPoison(
      SDValue Op, const APInt &DemandedElts,
      UndefPoisonKind Kind = UndefPoisonKind::UndefOrPoison,
      unsigned Depth = 0) const;

  /// Return true if this function can prove that \p Op is never poison.
  ///
  /// \param Op Value to test.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if this function can prove that \p Op is never poison.
  bool isGuaranteedNotToBePoison(SDValue Op, unsigned Depth = 0) const {
    return isGuaranteedNotToBeUndefOrPoison(Op, UndefPoisonKind::PoisonOnly,
                                            Depth);
  }

  /// Return true if this function can prove that \p Op is never poison. The
  /// DemandedElts argument limits the check to the requested vector elements.
  ///
  /// \param Op Value to test.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if this function can prove that \p Op is never poison.
  bool isGuaranteedNotToBePoison(SDValue Op, const APInt &DemandedElts,
                                 unsigned Depth = 0) const {
    return isGuaranteedNotToBeUndefOrPoison(Op, DemandedElts,
                                            UndefPoisonKind::PoisonOnly, Depth);
  }

  /// Return true if Op can create undef or poison from non-undef & non-poison
  /// operands. The DemandedElts argument limits the check to the requested
  /// vector elements.
  ///
  /// \p ConsiderFlags controls whether poison producing flags on the
  /// instruction are considered.  This can be used to see if the instruction
  /// could still introduce undef or poison even without poison generating flags
  /// which might be on the instruction.  (i.e. could the result of
  /// Op->dropPoisonGeneratingFlags() still create poison or undef)
  ///
  /// \param Op Operation to test.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param Kind Which of undef, poison, or both to track.
  /// \param ConsiderFlags True to treat poison-generating flags as sources.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if Op can create undef or poison from non-undef & non-poison operands.
  LLVM_ABI bool
  canCreateUndefOrPoison(SDValue Op, const APInt &DemandedElts,
                         UndefPoisonKind Kind = UndefPoisonKind::UndefOrPoison,
                         bool ConsiderFlags = true, unsigned Depth = 0) const;

  /// Return true if Op can create undef or poison from non-undef & non-poison
  /// operands.
  ///
  /// \p ConsiderFlags controls whether poison producing flags on the
  /// instruction are considered.  This can be used to see if the instruction
  /// could still introduce undef or poison even without poison generating flags
  /// which might be on the instruction.  (i.e. could the result of
  /// Op->dropPoisonGeneratingFlags() still create poison or undef)
  ///
  /// \param Op Operation to test.
  /// \param Kind Which of undef, poison, or both to track.
  /// \param ConsiderFlags True to treat poison-generating flags as sources.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if Op can create undef or poison from non-undef & non-poison operands.
  LLVM_ABI bool
  canCreateUndefOrPoison(SDValue Op,
                         UndefPoisonKind Kind = UndefPoisonKind::UndefOrPoison,
                         bool ConsiderFlags = true, unsigned Depth = 0) const;

  /// Return true if \p Op is an OR or XOR that can be treated as ADD.
  ///
  /// or(x,y) == add(x,y) iff haveNoCommonBitsSet(x,y)
  /// xor(x,y) == add(x,y) iff isMinSignedConstant(y) && !NoWrap
  /// If \p NoWrap is true, this will not match ISD::XOR.
  ///
  /// \param Op Operand that may be add-like.
  /// \param NoWrap True to reject ISD::XOR matches.
  /// \return True if \p Op is an OR or XOR that can be treated as ADD.
  LLVM_ABI bool isADDLike(SDValue Op, bool NoWrap = false) const;

  /// Return true if \p Op is an add of a pointer and a constant offset.
  ///
  /// The operand is an ISD::ADD with a ConstantSDNode on the right-hand side,
  /// or an ISD::OR with a ConstantSDNode that is guaranteed to have the same
  /// semantics as an ADD. This handles the equivalence:
  ///     X|Cst == X+Cst iff X&Cst = 0.
  ///
  /// \param Op Operand that may be a base plus constant offset.
  /// \return True if \p Op is an add of a pointer and a constant offset.
  LLVM_ABI bool isBaseWithConstantOffset(SDValue Op) const;

  /// Determine floating-point class information about \p Op.
  ///
  /// For vectors, the known FP classes are those shared by every demanded
  /// vector element. \p InterestedClasses is a hint for which FP classes we
  /// care about; the implementation may bail out early if it can determine that
  /// none of the interested classes are possible.
  ///
  /// \param Op Value whose FP class is computed.
  /// \param InterestedClasses FP classes the caller cares about.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if floating-point class information about \p Op.
  LLVM_ABI KnownFPClass computeKnownFPClass(SDValue Op,
                                            FPClassTest InterestedClasses,
                                            unsigned Depth = 0) const;

  /// Determine floating-point class information about \p Op.
  ///
  /// The DemandedElts argument allows us to only collect the known FP classes
  /// that are shared by the requested vector elements. \p InterestedClasses is
  /// a hint for which FP classes we care about.
  ///
  /// \param Op Value whose FP class is computed.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param InterestedClasses FP classes the caller cares about.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if floating-point class information about \p Op.
  LLVM_ABI KnownFPClass computeKnownFPClass(SDValue Op,
                                            const APInt &DemandedElts,
                                            FPClassTest InterestedClasses,
                                            unsigned Depth = 0) const;

  /// Return true if \p Op is known to never be NaN in \p DemandedElts.
  ///
  /// Tests the given SDValue (or the demanded elements of a vector). If \p SNaN
  /// is true, returns whether \p Op is known to never be a signaling NaN (it
  /// may still be a qNaN).
  ///
  /// \param Op Value to test.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param SNaN True to test only for signaling NaN.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if \p Op is known to never be NaN in \p DemandedElts.
  LLVM_ABI bool isKnownNeverNaN(SDValue Op, const APInt &DemandedElts,
                                bool SNaN = false, unsigned Depth = 0) const;

  /// Return true if \p Op is known to never be NaN.
  ///
  /// Tests the given SDValue (or all elements of it, if it is a vector). If
  /// \p SNaN is true, returns whether \p Op is known to never be a signaling
  /// NaN (it may still be a qNaN).
  ///
  /// \param Op Value to test.
  /// \param SNaN True to test only for signaling NaN.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if \p Op is known to never be NaN.
  LLVM_ABI bool isKnownNeverNaN(SDValue Op, bool SNaN = false,
                                unsigned Depth = 0) const;

  /// Return true if \p Op is known to never be a signaling NaN in \p
  /// DemandedElts.
  ///
  /// \param Op Value to test.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if \p Op is known to never be a signaling NaN in \p DemandedElts.
  bool isKnownNeverSNaN(SDValue Op, const APInt &DemandedElts,
                        unsigned Depth = 0) const {
    return isKnownNeverNaN(Op, DemandedElts, true, Depth);
  }

  /// Return true if \p Op is known to never be a signaling NaN.
  ///
  /// \param Op Value to test.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if \p Op is known to never be a signaling NaN.
  bool isKnownNeverSNaN(SDValue Op, unsigned Depth = 0) const {
    return isKnownNeverNaN(Op, true, Depth);
  }

  /// Test whether the given floating point SDValue (or all elements of it, if
  /// it is a vector) is known to never be interpretable as zero in \p
  /// DemandedElts.
  ///
  /// \param Op Floating-point value to test.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if the given floating point SDValue (or all elements of it, if it is a vector) is known to never be interpretable as zero in \p DemandedElts.
  LLVM_ABI bool isKnownNeverLogicalZero(SDValue Op, const APInt &DemandedElts,
                                        unsigned Depth = 0) const;

  /// Test whether the given floating point SDValue (or all elements of it, if
  /// it is a vector) is known to never be interpretable as zero.
  ///
  /// \param Op Floating-point value to test.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if the given floating point SDValue (or all elements of it, if it is a vector) is known to never be interpretable as zero.
  LLVM_ABI bool isKnownNeverLogicalZero(SDValue Op, unsigned Depth = 0) const;

  /// Test whether the given SDValue is known to contain non-zero value(s).
  ///
  /// \param Op Value to test.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if the given SDValue is known to contain non-zero value(s).
  LLVM_ABI bool isKnownNeverZero(SDValue Op, unsigned Depth = 0) const;

  /// Test whether the given SDValue is known to contain non-zero value(s).
  /// The DemandedElts argument limits the check to the requested vector
  /// elements.
  ///
  /// \param Op Value to test.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if the given SDValue is known to contain non-zero value(s).
  LLVM_ABI bool isKnownNeverZero(SDValue Op, const APInt &DemandedElts,
                                 unsigned Depth = 0) const;

  /// Test whether the given float value is known to be positive. +0.0, +inf and
  /// +nan are considered positive, -0.0, -inf and -nan are not.
  ///
  /// \param Op Floating-point value to test.
  /// \return True if the given float value is known to be positive.
  LLVM_ABI bool cannotBeOrderedNegativeFP(SDValue Op) const;

  /// Check if a use of a float value is insensitive to signed zeros.
  ///
  /// \param Use Use whose sensitivity to signed zero is tested.
  /// \return True if a use of a float value is insensitive to signed zeros.
  LLVM_ABI bool canIgnoreSignBitOfZero(const SDUse &Use) const;

  /// Check if \p Op has no-signed-zeros, or all users (limited to checking two
  /// for compile-time performance) are insensitive to signed zeros.
  ///
  /// \param Op Floating-point value whose signed-zero sensitivity is tested.
  /// \return True if \p Op has no-signed-zeros, or all users (limited to checking two for compile-time performance) are insensitive to signed zeros.
  LLVM_ABI bool canIgnoreSignBitOfZero(SDValue Op) const;

  /// Test whether two SDValues are known to compare equal. This
  /// is true if they are the same value, or if one is negative zero and the
  /// other positive zero.
  ///
  /// \param A First value.
  /// \param B Second value.
  /// \return True if two SDValues are known to compare equal.
  LLVM_ABI bool isEqualTo(SDValue A, SDValue B) const;

  /// Return true if A and B have no common bits set. As an example, this can
  /// allow an 'add' to be transformed into an 'or'.
  ///
  /// \param A First value.
  /// \param B Second value.
  /// \return True if A and B have no common bits set.
  LLVM_ABI bool haveNoCommonBitsSet(SDValue A, SDValue B) const;

  /// Test whether \p V has a splatted value for all the demanded elements.
  ///
  /// On success \p UndefElts will indicate the elements that have UNDEF
  /// values instead of the splat value, this is only guaranteed to be correct
  /// for \p DemandedElts.
  ///
  /// NOTE: The function will return true for a demanded splat of UNDEF values.
  ///
  /// \param V Value to test.
  /// \param DemandedElts Vector elements that must match the splat.
  /// \param UndefElts Set to the demanded elements that are UNDEF.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return True if \p V has a splatted value for all the demanded elements.
  LLVM_ABI bool isSplatValue(SDValue V, const APInt &DemandedElts,
                             APInt &UndefElts, unsigned Depth = 0) const;

  /// Test whether \p V has a splatted value.
  ///
  /// \param V Value to test.
  /// \param AllowUndefs True to treat UNDEF lanes as matching the splat.
  /// \return True if \p V has a splatted value.
  LLVM_ABI bool isSplatValue(SDValue V, bool AllowUndefs = false) const;

  /// If V is a splatted value, return the source vector and its splat index.
  ///
  /// \param V Value that may be a splat.
  /// \param SplatIndex Set to the splat element index on success.
  /// \return The source vector and its splat index.
  LLVM_ABI SDValue getSplatSourceVector(SDValue V, int &SplatIndex);

  /// If \p V is a splat vector, return its scalar source operand.
  ///
  /// Extracts that element from the source vector. If \p LegalTypes is true,
  /// this method may only return a legally-typed splat value. If it cannot
  /// legalize the splatted value it will return SDValue().
  ///
  /// \param V Value that may be a splat vector.
  /// \param LegalTypes True to require a legally typed splat value.
  /// \return The scalar source operand of the splat, or an empty SDValue.
  LLVM_ABI SDValue getSplatValue(SDValue V, bool LegalTypes = false);

  /// If a SHL/SRA/SRL node \p V has shift amounts that are all less than the
  /// element bit-width of the shift node, return the valid constant range.
  ///
  /// \param V Shift node to inspect.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return The valid constant range.
  LLVM_ABI std::optional<ConstantRange>
  getValidShiftAmountRange(SDValue V, const APInt &DemandedElts,
                           unsigned Depth) const;

  /// If a SHL/SRA/SRL node \p V has a uniform shift amount
  /// that is less than the element bit-width of the shift node, return it.
  ///
  /// \param V Shift node to inspect.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return The uniform shift amount, if it is valid.
  LLVM_ABI std::optional<unsigned>
  getValidShiftAmount(SDValue V, const APInt &DemandedElts,
                      unsigned Depth = 0) const;

  /// If a SHL/SRA/SRL node \p V has a uniform shift amount
  /// that is less than the element bit-width of the shift node, return it.
  ///
  /// \param V Shift node to inspect.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return The uniform shift amount, if it is valid.
  LLVM_ABI std::optional<unsigned>
  getValidShiftAmount(SDValue V, unsigned Depth = 0) const;

  /// If a SHL/SRA/SRL node \p V has shift amounts that are all less than the
  /// element bit-width of the shift node, return the minimum possible value.
  ///
  /// \param V Shift node to inspect.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return The minimum possible value.
  LLVM_ABI std::optional<unsigned>
  getValidMinimumShiftAmount(SDValue V, const APInt &DemandedElts,
                             unsigned Depth = 0) const;

  /// If a SHL/SRA/SRL node \p V has shift amounts that are all less than the
  /// element bit-width of the shift node, return the minimum possible value.
  ///
  /// \param V Shift node to inspect.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return The minimum possible value.
  LLVM_ABI std::optional<unsigned>
  getValidMinimumShiftAmount(SDValue V, unsigned Depth = 0) const;

  /// If a SHL/SRA/SRL node \p V has shift amounts that are all less than the
  /// element bit-width of the shift node, return the maximum possible value.
  ///
  /// \param V Shift node to inspect.
  /// \param DemandedElts Vector elements that participate in the analysis.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return The maximum possible value.
  LLVM_ABI std::optional<unsigned>
  getValidMaximumShiftAmount(SDValue V, const APInt &DemandedElts,
                             unsigned Depth = 0) const;

  /// If a SHL/SRA/SRL node \p V has shift amounts that are all less than the
  /// element bit-width of the shift node, return the maximum possible value.
  ///
  /// \param V Shift node to inspect.
  /// \param Depth Recursion depth limit for the analysis.
  /// \return The maximum possible value.
  LLVM_ABI std::optional<unsigned>
  getValidMaximumShiftAmount(SDValue V, unsigned Depth = 0) const;

  /// Match a binop plus shuffle pyramid as a horizontal vector reduction.
  ///
  /// Starts from the EXTRACT_VECTOR_ELT node \p Extract. The reduction must use
  /// one of the opcodes listed in \p CandidateBinOps; on success \p BinOp
  /// receives the matching opcode. Returns the vector being reduced, or
  /// SDValue() if unmatched. If \p AllowPartials is set, a partial match
  /// returns the extracted subvector at the start of the reduction.
  ///
  /// \param Extract EXTRACT_VECTOR_ELT node at the reduction root.
  /// \param BinOp Set to the matched binary opcode on success.
  /// \param CandidateBinOps Opcodes that may implement the reduction.
  /// \param AllowPartials True to accept a prefix of the reduction pyramid.
  /// \return The matched reduction value, or an empty SDValue.
  LLVM_ABI SDValue matchBinOpReduction(SDNode *Extract, ISD::NodeType &BinOp,
                                       ArrayRef<ISD::NodeType> CandidateBinOps,
                                       bool AllowPartials = false);

  /// Unroll a vector operation into per-element scalar operations.
  ///
  /// Used by legalize and lowering. If \p ResNE is 0, fully unroll the vector
  /// op. If \p ResNE is less than the width of the vector op, unroll up to
  /// ResNE. If \p ResNE is greater than the width, unroll and fill the remainder
  /// with UNDEFs.
  ///
  /// \param N Vector operation to unroll.
  /// \param ResNE Result element count, or 0 to fully unroll.
  /// \return The unrolled vector operation result.
  LLVM_ABI SDValue UnrollVectorOp(SDNode *N, unsigned ResNE = 0);

  /// Like UnrollVectorOp(), but for the [US](ADD|SUB|MUL)O family of opcodes.
  /// This is a separate function because those opcodes have two results.
  ///
  /// \param N Overflow vector operation to unroll.
  /// \param ResNE Result element count, or 0 to fully unroll.
  /// \return The unrolled overflow operation result.
  LLVM_ABI std::pair<SDValue, SDValue>
  UnrollVectorOverflowOp(SDNode *N, unsigned ResNE = 0);

  /// Return true if \p LD and \p Base are consecutive mergeable loads.
  ///
  /// Both loads must be nonvolatile. \p LD must load \p Bytes bytes from a
  /// location that is \p Dist units away from the location that \p Base loads.
  ///
  /// \param LD Candidate load that may follow \p Base.
  /// \param Base Base load of the consecutive pair.
  /// \param Bytes Number of bytes loaded by \p LD.
  /// \param Dist Distance in units from \p Base to \p LD.
  /// \return True if \p LD and \p Base are consecutive mergeable loads.
  LLVM_ABI bool areNonVolatileConsecutiveLoads(LoadSDNode *LD, LoadSDNode *Base,
                                               unsigned Bytes, int Dist) const;

  /// Infer alignment of a load / store address. Return std::nullopt if it
  /// cannot be inferred.
  ///
  /// \param Ptr Address value whose alignment is inferred.
  /// \return The inferred pointer alignment, if known.
  LLVM_ABI MaybeAlign InferPtrAlign(SDValue Ptr) const;

  /// Split the scalar node with EXTRACT_ELEMENT using the provided VTs and
  /// return the low/high part.
  ///
  /// \param N Scalar value to split.
  /// \param DL Debug location for the extract nodes.
  /// \param LoVT Value type of the low part.
  /// \param HiVT Value type of the high part.
  /// \return The split low and high halves of the scalar.
  LLVM_ABI std::pair<SDValue, SDValue> SplitScalar(const SDValue &N,
                                                   const SDLoc &DL,
                                                   const EVT &LoVT,
                                                   const EVT &HiVT);

  /// Compute the VTs needed for the low/hi parts of a type
  /// which is split (or expanded) into two not necessarily identical pieces.
  ///
  /// \param VT Type being split.
  /// \return The destination value types for the split.
  LLVM_ABI std::pair<EVT, EVT> GetSplitDestVTs(const EVT &VT) const;

  /// Compute split destination VTs relative to an enveloping split type.
  ///
  /// Computes the VTs needed for the low/hi parts of a type, dependent on an
  /// enveloping VT that has been split into two identical pieces. Sets the
  /// HiIsEmpty flag when the hi type has zero storage size.
  ///
  /// \param VT Type being split.
  /// \param EnvVT Enveloping type that has already been split.
  /// \param HiIsEmpty Set to true when the high type has zero storage size.
  /// \return The destination value types for the dependent split.
  LLVM_ABI std::pair<EVT, EVT> GetDependentSplitDestVTs(const EVT &VT,
                                                        const EVT &EnvVT,
                                                        bool *HiIsEmpty) const;

  /// Split the vector with EXTRACT_SUBVECTOR using the provided
  /// VTs and return the low/high part.
  ///
  /// \param N Vector value to split.
  /// \param DL Debug location for the extract nodes.
  /// \param LoVT Value type of the low half.
  /// \param HiVT Value type of the high half.
  /// \return The split low and high halves of the vector.
  LLVM_ABI std::pair<SDValue, SDValue> SplitVector(const SDValue &N,
                                                   const SDLoc &DL,
                                                   const EVT &LoVT,
                                                   const EVT &HiVT);

  /// Split the vector with EXTRACT_SUBVECTOR and return the low/high part.
  ///
  /// \param N Vector value to split.
  /// \param DL Debug location for the extract nodes.
  /// \return The split low and high halves of the vector.
  std::pair<SDValue, SDValue> SplitVector(const SDValue &N, const SDLoc &DL) {
    EVT LoVT, HiVT;
    std::tie(LoVT, HiVT) = GetSplitDestVTs(N.getValueType());
    return SplitVector(N, DL, LoVT, HiVT);
  }

  /// Split the explicit vector length parameter of a VP operation.
  ///
  /// \param N Explicit vector-length value to split.
  /// \param VecVT Vector type that determines the split.
  /// \param DL Debug location for the split nodes.
  /// \return The split effective vector lengths.
  LLVM_ABI std::pair<SDValue, SDValue> SplitEVL(SDValue N, EVT VecVT,
                                                const SDLoc &DL);

  /// Split the node's operand with EXTRACT_SUBVECTOR and
  /// return the low/high part.
  ///
  /// \param N Node whose operand is split.
  /// \param OpNo Operand number to split.
  /// \return The split low and high halves of the operand.
  std::pair<SDValue, SDValue> SplitVectorOperand(const SDNode *N, unsigned OpNo)
  {
    return SplitVector(N->getOperand(OpNo), SDLoc(N));
  }

  /// Widen the vector up to the next power of two using INSERT_SUBVECTOR.
  ///
  /// \param N Vector value to widen.
  /// \param DL Debug location for the widening nodes.
  /// \return The widened vector value.
  LLVM_ABI SDValue WidenVector(const SDValue &N, const SDLoc &DL);

  /// Append extracted vector elements from \p Op into \p Args.
  ///
  /// Extracts elements from \p Start for \p Count elements. If \p Count is 0,
  /// all remaining elements are extracted. Extracted elements have type \p
  /// EltVT when provided, otherwise Op's element type.
  ///
  /// \param Op Vector value to extract from.
  /// \param Args Destination list that receives extracted elements.
  /// \param Start First element index to extract.
  /// \param Count Number of elements to extract, or 0 for all remaining.
  /// \param EltVT Optional type of each extracted element.
  LLVM_ABI void ExtractVectorElements(SDValue Op,
                                      SmallVectorImpl<SDValue> &Args,
                                      unsigned Start = 0, unsigned Count = 0,
                                      EVT EltVT = EVT());

  /// Compute the default alignment value for the given type.
  ///
  /// \param MemoryVT Value type whose default alignment is requested.
  /// \return The default alignment for the given type.
  LLVM_ABI Align getEVTAlign(EVT MemoryVT) const;

  /// Test whether the given value is a constant int or similar node.
  ///
  /// \param N Value to test.
  /// \param AllowOpaques True to treat opaque constants as constants.
  /// \return True if the given value is a constant int or similar node.
  LLVM_ABI bool
  isConstantIntBuildVectorOrConstantInt(SDValue N,
                                        bool AllowOpaques = true) const;

  /// Test whether the given value is a constant FP or similar node.
  ///
  /// \param N Value to test.
  /// \return True if the given value is a constant FP or similar node.
  LLVM_ABI bool isConstantFPBuildVectorOrConstantFP(SDValue N) const;

  /// Return true if \p N is a constant or a build_vector of constants.
  ///
  /// Accepts integer or floating-point constants. A vector need not be a splat.
  ///
  /// \param N Value to test.
  /// \return True if \p N is a constant or a build_vector of constants.
  inline bool isConstantValueOfAnyType(SDValue N) const {
    return isConstantIntBuildVectorOrConstantInt(N) ||
           isConstantFPBuildVectorOrConstantFP(N);
  }

  /// Check if a value \p N is a constant using the target's BooleanContent for
  /// its type.
  ///
  /// \param N Value to test as a boolean constant.
  /// \return True if a value \p N is a constant using the target's BooleanContent for its type.
  LLVM_ABI std::optional<bool> isBoolConstant(SDValue N) const;

  /// Set CallSiteInfo to be associated with Node.
  ///
  /// \param Node Node that receives the call-site info.
  /// \param CallInfo Call-site info to attach.
  void addCallSiteInfo(const SDNode *Node, CallSiteInfo &&CallInfo) {
    SDEI[Node].CSInfo = std::move(CallInfo);
  }
  /// Return CallSiteInfo associated with Node, or a default if none exists.
  ///
  /// \param Node Node whose call-site info is requested.
  /// \return CallSiteInfo associated with Node, or a default if none exists.
  CallSiteInfo getCallSiteInfo(const SDNode *Node) {
    auto I = SDEI.find(Node);
    return I != SDEI.end() ? std::move(I->second).CSInfo : CallSiteInfo();
  }
  /// Set HeapAllocSite to be associated with Node.
  ///
  /// \param Node Node that receives the heap-alloc site metadata.
  /// \param MD Heap-alloc site metadata node.
  void addHeapAllocSite(const SDNode *Node, MDNode *MD) {
    SDEI[Node].HeapAllocSite = MD;
  }
  /// Return HeapAllocSite associated with Node, or nullptr if none exists.
  ///
  /// \param Node Node whose heap-alloc site metadata is requested.
  /// \return HeapAllocSite associated with Node, or nullptr if none exists.
  MDNode *getHeapAllocSite(const SDNode *Node) const {
    auto I = SDEI.find(Node);
    return I != SDEI.end() ? I->second.HeapAllocSite : nullptr;
  }
  /// Set PCSections to be associated with Node.
  ///
  /// \param Node Node that receives the PC-section metadata.
  /// \param MD PC-section metadata node.
  void addPCSections(const SDNode *Node, MDNode *MD) {
    SDEI[Node].PCSections = MD;
  }
  /// Set MMRAMetadata to be associated with Node.
  ///
  /// \param Node Node that receives the MMRA metadata.
  /// \param MMRA Memory-region annotation metadata node.
  void addMMRAMetadata(const SDNode *Node, MDNode *MMRA) {
    SDEI[Node].MMRA = MMRA;
  }
  /// Return PCSections associated with Node, or nullptr if none exists.
  ///
  /// \param Node Node whose PC-section metadata is requested.
  /// \return PCSections associated with Node, or nullptr if none exists.
  MDNode *getPCSections(const SDNode *Node) const {
    auto It = SDEI.find(Node);
    return It != SDEI.end() ? It->second.PCSections : nullptr;
  }
  /// Return the MMRA MDNode associated with Node, or nullptr if none
  /// exists.
  ///
  /// \param Node Node whose MMRA metadata is requested.
  /// \return The MMRA MDNode associated with Node, or nullptr if none exists.
  MDNode *getMMRAMetadata(const SDNode *Node) const {
    auto It = SDEI.find(Node);
    return It != SDEI.end() ? It->second.MMRA : nullptr;
  }
  /// Set CalledGlobal to be associated with Node.
  ///
  /// \param Node Node that receives the called-global info.
  /// \param GV Global value that is called.
  /// \param OpFlags Target-specific operand flags for the global.
  void addCalledGlobal(const SDNode *Node, const GlobalValue *GV,
                       unsigned OpFlags) {
    SDEI[Node].CalledGlobal = {GV, OpFlags};
  }
  /// Return CalledGlobal associated with Node, or a nullopt if none exists.
  ///
  /// \param Node Node whose called-global info is requested.
  /// \return CalledGlobal associated with Node, or a nullopt if none exists.
  std::optional<CalledGlobalInfo> getCalledGlobal(const SDNode *Node) {
    auto I = SDEI.find(Node);
    return I != SDEI.end()
               ? std::make_optional(std::move(I->second).CalledGlobal)
               : std::nullopt;
  }
  /// Set NoMergeSiteInfo to be associated with Node if NoMerge is true.
  ///
  /// \param Node Node that receives the nomerge site info.
  /// \param NoMerge True if the call must not be merged with others.
  void addNoMergeSiteInfo(const SDNode *Node, bool NoMerge) {
    if (NoMerge)
      SDEI[Node].NoMerge = NoMerge;
  }
  /// Return NoMerge info associated with Node.
  ///
  /// \param Node Node whose nomerge site info is requested.
  /// \return NoMerge info associated with Node.
  bool getNoMergeSiteInfo(const SDNode *Node) const {
    auto I = SDEI.find(Node);
    return I != SDEI.end() ? I->second.NoMerge : false;
  }

  /// Copy extra info associated with one node to another.
  ///
  /// \param From Node whose extra info is copied.
  /// \param To Node that receives the copied extra info.
  LLVM_ABI void copyExtraInfo(SDNode *From, SDNode *To);

  /// Return the current function's default denormal handling kind for the given
  /// floating point type.
  ///
  /// \param VT Floating-point value type whose denormal mode is requested.
  /// \return The current function's default denormal handling kind for the given floating point type.
  DenormalMode getDenormalMode(EVT VT) const {
    return MF->getDenormalMode(VT.getFltSemantics());
  }

  /// Return true if the current function should be optimized for size.
  ///
  /// \return True if the current function should be optimized for size.
  LLVM_ABI bool shouldOptForSize() const;

  /// Get the (commutative) identity element for the given opcode, if it exists.
  ///
  /// \param Opcode Opcode whose identity element is requested.
  /// \param DL Debug location for the identity node.
  /// \param VT Value type of the identity element.
  /// \param Flags Node flags that select the identity (for example NSW).
  /// \return The identity element for the opcode and type.
  LLVM_ABI SDValue getIdentityElement(unsigned Opcode, const SDLoc &DL, EVT VT,
                                      SDNodeFlags Flags);

  /// Get an expression that implements a partial multiply-subtract reduction.
  ///
  /// In practice this means that parts of the expression are negated, e.g.
  ///
  ///     partial_reduce_fmls acc, lhs, rhs
  /// <=> partial_reduce_fmla acc, lhs, -rhs
  ///
  ///      partial_reduce_umls acc, lhs, rhs
  /// <=> -partial_reduce_umla -acc, lhs, rhs
  ///
  /// \param Opc Partial-reduce multiply-subtract opcode.
  /// \param DL Debug location for the expression.
  /// \param Acc Accumulator operand.
  /// \param LHS Left-hand multiply operand.
  /// \param RHS Right-hand multiply operand.
  /// \return A partial-reduce MLS node.
  LLVM_ABI SDValue getPartialReduceMLS(unsigned Opc, const SDLoc &DL,
                                       SDValue Acc, SDValue LHS, SDValue RHS);

  /// Return true if \p Opcode is generally safe to speculatively execute.
  ///
  /// Some opcodes may create immediate undefined behavior when used with some
  /// values (integer division-by-zero for example). Therefore, these operations
  /// are not generally safe to move around or change.
  ///
  /// \param Opcode ISD opcode to test.
  /// \return True if \p Opcode is generally safe to speculatively execute.
  bool isSafeToSpeculativelyExecute(unsigned Opcode) const {
    switch (Opcode) {
    case ISD::SDIV:
    case ISD::SREM:
    case ISD::SDIVREM:
    case ISD::UDIV:
    case ISD::UREM:
    case ISD::UDIVREM:
      return false;
    default:
      return true;
    }
  }

  /// Return true if \p N is safe to speculatively execute with its operands.
  ///
  /// Some opcodes are not generally safe (for example `udiv`), but a given node
  /// may still be if its arguments rule out undefined behavior (a nonzero
  /// denominator).
  ///
  /// \param N Node whose opcode and operands are tested.
  /// \return True if \p N is safe to speculatively execute with its operands.
  bool isSafeToSpeculativelyExecuteNode(const SDNode *N) const {
    switch (N->getOpcode()) {
    case ISD::UDIV:
      return isKnownNeverZero(N->getOperand(1));
    default:
      return isSafeToSpeculativelyExecute(N->getOpcode());
    }
  }

  /// Create a call to a floating-point environment state library function.
  ///
  /// \param LibFunc Runtime library function identifier.
  /// \param Ptr Pointer to the environment state.
  /// \param InChain Incoming token chain.
  /// \param DLoc Debug location for the call.
  /// \return The call result value.
  LLVM_ABI SDValue makeStateFunctionCall(unsigned LibFunc, SDValue Ptr,
                                         SDValue InChain, const SDLoc &DLoc);

private:
#ifndef NDEBUG
  void verifyNode(SDNode *N) const;
#endif
  void InsertNode(SDNode *N);
  bool RemoveNodeFromCSEMaps(SDNode *N);
  void AddModifiedNodeToCSEMaps(SDNode *N);
  SDNode *FindModifiedNodeSlot(SDNode *N, SDValue Op, void *&InsertPos);
  SDNode *FindModifiedNodeSlot(SDNode *N, SDValue Op1, SDValue Op2,
                               void *&InsertPos);
  SDNode *FindModifiedNodeSlot(SDNode *N, ArrayRef<SDValue> Ops,
                               void *&InsertPos);
  SDNode *UpdateSDLocOnMergeSDNode(SDNode *N, const SDLoc &loc);

  void DeleteNodeNotInCSEMaps(SDNode *N);
  void DeallocateNode(SDNode *N);

  void allnodes_clear();

  /// Look up the node specified by ID in CSEMap.  If it exists, return it.  If
  /// not, return the insertion token that will make insertion faster.  This
  /// overload is for nodes other than Constant or ConstantFP, use the other one
  /// for those.
  SDNode *FindNodeOrInsertPos(const FoldingSetNodeID &ID, void *&InsertPos);

  /// Look up the node specified by ID in CSEMap.  If it exists, return it.  If
  /// not, return the insertion token that will make insertion faster.  Performs
  /// additional processing for constant nodes.
  SDNode *FindNodeOrInsertPos(const FoldingSetNodeID &ID, const SDLoc &DL,
                              void *&InsertPos);

  /// Maps to auto-CSE operations.
  std::vector<CondCodeSDNode*> CondCodeNodes;

  std::vector<SDNode*> ValueTypeNodes;
  std::map<EVT, SDNode*, EVT::compareRawBits> ExtendedValueTypeNodes;
  StringMap<SDNode*> ExternalSymbols;

  std::map<std::pair<std::string, unsigned>, SDNode *> TargetExternalSymbols;
  DenseMap<MCSymbol *, SDNode *> MCSymbols;

  FlagInserter *Inserter = nullptr;
};

/// GraphTraits specialization that iterates every node in a SelectionDAG.
template <> struct GraphTraits<SelectionDAG*> : public GraphTraits<SDNode*> {
  /// Iterator over all nodes in the SelectionDAG.
  using nodes_iterator = pointer_iterator<SelectionDAG::allnodes_iterator>;

  /// Return an iterator to the first node in \p G.
  ///
  /// \param G SelectionDAG whose nodes are iterated.
  /// \return An iterator to the first node in \p G.
  static nodes_iterator nodes_begin(SelectionDAG *G) {
    return nodes_iterator(G->allnodes_begin());
  }

  /// Return an iterator one past the last node in \p G.
  ///
  /// \param G SelectionDAG whose nodes are iterated.
  /// \return An iterator one past the last node in \p G.
  static nodes_iterator nodes_end(SelectionDAG *G) {
    return nodes_iterator(G->allnodes_end());
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_SELECTIONDAG_H
