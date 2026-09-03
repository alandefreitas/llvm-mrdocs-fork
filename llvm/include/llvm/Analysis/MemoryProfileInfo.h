//===- llvm/Analysis/MemoryProfileInfo.h - memory profile info ---*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains utilities to analyze memory profile information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MEMORYPROFILEINFO_H
#define LLVM_ANALYSIS_MEMORYPROFILEINFO_H

#include "llvm/IR/Metadata.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Support/Compiler.h"
#include <map>

namespace llvm {

class OptimizationRemarkEmitter;

/// Utilities for analyzing and applying memory profile information.
namespace memprof {

/// Whether the alloc memeprof metadata will include context size info for all
/// MIBs.
/// @return True if every MIB includes context size info.
LLVM_ABI bool metadataIncludesAllContextSizeInfo();

/// Whether the alloc memprof metadata may include context size info for some
/// MIBs (but possibly not all).
/// @return True if some MIBs may include context size info.
LLVM_ABI bool metadataMayIncludeContextSizeInfo();

/// Whether we need to record the context size info in the alloc trie used to
/// build metadata.
/// @return True if context size info must be recorded in the alloc trie.
LLVM_ABI bool recordContextSizeInfoForAnalysis();

/// Build callstack metadata from the provided list of call stack ids.
/// @param CallStack Stack ids forming the call stack context.
/// @param Ctx LLVM context used to create the metadata nodes.
/// @return Metadata node representing the call stack.
LLVM_ABI MDNode *buildCallstackMetadata(ArrayRef<uint64_t> CallStack,
                                        LLVMContext &Ctx);

/// Returns the stack node from an MIB metadata node.
/// @param MIB MIB metadata node to inspect.
/// @return Stack-id metadata node contained in \p MIB.
LLVM_ABI MDNode *getMIBStackNode(const MDNode *MIB);

/// Returns the allocation type from an MIB metadata node.
/// @param MIB MIB metadata node to inspect.
/// @return Allocation type encoded in \p MIB.
LLVM_ABI AllocationType getMIBAllocType(const MDNode *MIB);

/// Returns the string to use in attributes with the given type.
/// @param Type Allocation type to convert to an attribute string.
/// @return Attribute string corresponding to \p Type.
LLVM_ABI std::string getAllocTypeAttributeString(AllocationType Type);

/// True if the AllocTypes bitmask contains just a single type.
/// @param AllocTypes Bitmask of allocation types to test.
/// @return True if exactly one allocation type bit is set.
LLVM_ABI bool hasSingleAllocType(uint8_t AllocTypes);

/// Removes any existing "ambiguous" memprof attribute. Called before we apply a
/// specific allocation type such as "cold", "notcold", or "hot".
/// @param CB Call whose ambiguous memprof attribute should be removed.
LLVM_ABI void removeAnyExistingAmbiguousAttribute(CallBase *CB);

/// Adds an "ambiguous" memprof attribute to call with a matched allocation
/// profile but that we haven't yet been able to disambiguate.
/// @param CB Call that should receive the ambiguous memprof attribute.
LLVM_ABI void addAmbiguousAttribute(CallBase *CB);

// During matching we also keep the AllocationType along with the
// ContextTotalSize in the Trie for the most accurate reporting when we decide
// to hint unambiguously where there is a dominant type. We don't put the
// AllocationType in the ContextTotalSize struct as it isn't needed there
// during the LTO step, because due to context trimming a summarized
// context with its allocation type can correspond to multiple context/size
// pairs. Here the redundancy is a short-lived convenience.
/// Pair of profiled context size data and its allocation type.
using ContextSizeTypePair = std::pair<ContextTotalSize, AllocationType>;

/// Trie of profiled call-stack contexts for one allocation call.
///
/// Class to build a trie of call stack contexts for a particular profiled
/// allocation call, along with their associated allocation types. The
/// allocation will be at the root of the trie, which is then used to compute
/// the minimum lists of context ids needed to associate a call context with a
/// single allocation type.
class CallStackTrie {
private:
  struct CallStackTrieNode {
    // Allocation types for call context sharing the context prefix at this
    // node.
    uint8_t AllocTypes;
    // If the user has requested reporting of hinted sizes, keep track of the
    // associated full stack id and profiled sizes. Can have more than one
    // after trimming (e.g. when building from metadata). This is only placed on
    // the last (root-most) trie node for each allocation context. Also
    // track the original allocation type of the context.
    std::vector<ContextSizeTypePair> ContextInfo;
    // Map of caller stack id to the corresponding child Trie node.
    std::map<uint64_t, CallStackTrieNode *> Callers;
    CallStackTrieNode(AllocationType Type)
        : AllocTypes(static_cast<uint8_t>(Type)) {}
    void addAllocType(AllocationType AllocType) {
      AllocTypes |= static_cast<uint8_t>(AllocType);
    }
    void removeAllocType(AllocationType AllocType) {
      AllocTypes &= ~static_cast<uint8_t>(AllocType);
    }
    bool hasAllocType(AllocationType AllocType) const {
      return AllocTypes & static_cast<uint8_t>(AllocType);
    }
  };

  // The node for the allocation at the root.
  CallStackTrieNode *Alloc = nullptr;
  // The allocation's leaf stack id.
  uint64_t AllocStackId = 0;

  // If the client provides a remarks emitter object, we will emit remarks on
  // allocations for which we apply non-context sensitive allocation hints.
  OptimizationRemarkEmitter *ORE;

  // The maximum size of a cold allocation context, from the profile summary.
  uint64_t MaxColdSize;

  // Tracks whether we have built the Trie from existing MD_memprof metadata. We
  // apply different heuristics for determining whether to discard non-cold
  // contexts when rebuilding as we have lost information available during the
  // original profile match.
  bool BuiltFromExistingMetadata = false;

  void deleteTrieNode(CallStackTrieNode *Node) {
    if (!Node)
      return;
    for (auto C : Node->Callers)
      deleteTrieNode(C.second);
    delete Node;
  }

  // Recursively build up a complete list of context information from the
  // trie nodes reached form the given Node, including each context's
  // ContextTotalSize and AllocationType, for hint size reporting.
  void collectContextInfo(CallStackTrieNode *Node,
                          std::vector<ContextSizeTypePair> &ContextInfo);

  // Recursively convert hot allocation types to notcold, since we don't
  // actually do any cloning for hot contexts, to facilitate more aggressive
  // pruning of contexts.
  void convertHotToNotCold(CallStackTrieNode *Node);

  // Recursive helper to trim contexts and create metadata nodes.
  bool buildMIBNodes(CallStackTrieNode *Node, LLVMContext &Ctx,
                     std::vector<uint64_t> &MIBCallStack,
                     std::vector<Metadata *> &MIBNodes,
                     bool CalleeHasAmbiguousCallerContext, uint64_t &TotalBytes,
                     uint64_t &ColdBytes);

public:
  /// Construct an empty call stack trie.
  /// @param ORE Optional remarks emitter for non-context-sensitive hints.
  /// @param MaxColdSize Maximum cold allocation context size from the profile.
  CallStackTrie(OptimizationRemarkEmitter *ORE = nullptr,
                uint64_t MaxColdSize = 0)
      : ORE(ORE), MaxColdSize(MaxColdSize) {}
  /// Destroy the trie and free all nodes.
  ~CallStackTrie() { deleteTrieNode(Alloc); }

  /// Return true if the trie contains no allocation contexts.
  /// @return True if the trie has no allocation node.
  bool empty() const { return Alloc == nullptr; }

  /// Add a call stack context with the given allocation type to the Trie.
  ///
  /// The context is represented by the list of stack ids (computed during
  /// matching via a debug location hash), expected to be in order from the
  /// allocation call down to the bottom of the call stack (i.e. callee to
  /// caller order).
  /// @param AllocType Allocation type associated with this context.
  /// @param StackIds Stack ids forming the call stack, callee to caller.
  /// @param ContextSizeInfo Optional profiled size info for this context.
  LLVM_ABI void
  addCallStack(AllocationType AllocType, ArrayRef<uint64_t> StackIds,
               std::vector<ContextTotalSize> ContextSizeInfo = {});

  /// Add the call stack context along with its allocation type from the MIB
  /// metadata to the Trie.
  /// @param MIB MIB metadata node providing the call stack and allocation type.
  LLVM_ABI void addCallStack(MDNode *MIB);

  /// Build and attach the minimal necessary MIB metadata.
  ///
  /// If the alloc has a single allocation type, add a function attribute
  /// instead. The reason for adding an attribute in this case is that it
  /// matches how the behavior for allocation calls will be communicated to lib
  /// call simplification after cloning or another optimization to distinguish
  /// the allocation types, which is lower overhead and more direct than
  /// maintaining this metadata.
  /// @param CI Allocation call to annotate with MIB metadata or an attribute.
  /// @return True if memprof metadata was attached; false if an attribute was
  /// added instead.
  LLVM_ABI bool buildAndAttachMIBMetadata(CallBase *CI);

  /// Add an attribute for the given allocation type to the call instruction.
  ///
  /// If hinted by reporting is enabled, a message is emitted with the given
  /// descriptor used to identify the category of single allocation type.
  /// @param CI Call instruction to annotate.
  /// @param AT Allocation type whose attribute string is applied.
  /// @param Descriptor Label identifying the single-allocation-type category.
  LLVM_ABI void addSingleAllocTypeAttribute(CallBase *CI, AllocationType AT,
                                            StringRef Descriptor);
};

/// Helper to iterate stack ids in memprof metadata or ThinLTO summaries.
///
/// Helper class to iterate through stack ids in both metadata (memprof MIB and
/// callsite) and the corresponding ThinLTO summary data structures
/// (CallsiteInfo and MIBInfo). This simplifies implementation of client code
/// which doesn't need to worry about whether we are operating with IR (Regular
/// LTO), or summary (ThinLTO).
template <class NodeT, class IteratorT> class CallStack {
public:
  /// Construct a call stack view over node \p N.
  /// @param N Node providing stack ids, or null for an empty stack.
  CallStack(const NodeT *N = nullptr) : N(N) {}

  // Implement minimum required methods for range-based for loop.
  // The default implementation assumes we are operating on ThinLTO data
  // structures, which have a vector of StackIdIndices. There are specialized
  // versions provided to iterate through metadata.
  /// Iterator over stack ids in a \c CallStack.
  struct CallStackIterator {
    /// Node whose stack ids are being iterated, or null.
    const NodeT *N = nullptr;
    /// Underlying iterator into the node's stack-id sequence.
    IteratorT Iter;
    /// Construct an iterator at the beginning or end of \p N.
    /// @param N Node whose stack ids are iterated, or null.
    /// @param End If true, position at end; otherwise at begin.
    CallStackIterator(const NodeT *N, bool End);
    /// Return the stack id at the current iterator position.
    /// @return Stack id at the current position.
    uint64_t operator*();
    /// Return true if this iterator equals \p rhs.
    /// @param rhs Iterator to compare against.
    /// @return True if both iterators refer to the same position.
    bool operator==(const CallStackIterator &rhs) { return Iter == rhs.Iter; }
    /// Return true if this iterator differs from \p rhs.
    /// @param rhs Iterator to compare against.
    /// @return True if the iterators refer to different positions.
    bool operator!=(const CallStackIterator &rhs) { return !(*this == rhs); }
    /// Advance this iterator to the next stack id.
    void operator++() { ++Iter; }
  };

  /// Return true if this call stack is empty.
  /// @return True if there is no underlying node.
  bool empty() const { return N == nullptr; }

  /// Return an iterator to the first stack id.
  /// @return Iterator to the first stack id.
  CallStackIterator begin() const;
  /// Return an iterator past the last stack id.
  /// @return Iterator past the last stack id.
  CallStackIterator end() const { return CallStackIterator(N, /*End*/ true); }
  /// Return an iterator just after the shared prefix with \p Other.
  /// @param Other Call stack whose leading stack ids are compared.
  /// @return Iterator just after the shared leading stack ids.
  CallStackIterator beginAfterSharedPrefix(const CallStack &Other);
  /// Return the last stack id in this call stack.
  /// @return Last stack id in the call stack.
  uint64_t back() const;

private:
  const NodeT *N = nullptr;
};

/// Construct a call stack iterator at the beginning or end of \p N.
/// @param N Node whose stack ids are iterated, or null.
/// @param End If true, position at end; otherwise at begin.
template <class NodeT, class IteratorT>
CallStack<NodeT, IteratorT>::CallStackIterator::CallStackIterator(
    const NodeT *N, bool End)
    : N(N) {
  if (!N) {
    Iter = nullptr;
    return;
  }
  Iter = End ? N->StackIdIndices.end() : N->StackIdIndices.begin();
}

/// Return the stack id at the current iterator position.
/// @return Stack id at the current position.
template <class NodeT, class IteratorT>
uint64_t CallStack<NodeT, IteratorT>::CallStackIterator::operator*() {
  assert(Iter != N->StackIdIndices.end());
  return *Iter;
}

/// Return the last stack id in this call stack.
/// @return Last stack id in the call stack.
template <class NodeT, class IteratorT>
uint64_t CallStack<NodeT, IteratorT>::back() const {
  assert(N);
  return N->StackIdIndices.back();
}

/// Return an iterator to the first stack id.
/// @return Iterator to the first stack id.
template <class NodeT, class IteratorT>
typename CallStack<NodeT, IteratorT>::CallStackIterator
CallStack<NodeT, IteratorT>::begin() const {
  return CallStackIterator(N, /*End*/ false);
}

/// Return an iterator just after the shared prefix with \p Other.
/// @param Other Call stack whose leading stack ids are compared.
/// @return Iterator just after the shared leading stack ids.
template <class NodeT, class IteratorT>
typename CallStack<NodeT, IteratorT>::CallStackIterator
CallStack<NodeT, IteratorT>::beginAfterSharedPrefix(const CallStack &Other) {
  CallStackIterator Cur = begin();
  for (CallStackIterator OtherCur = Other.begin();
       Cur != end() && OtherCur != Other.end(); ++Cur, ++OtherCur)
    assert(*Cur == *OtherCur);
  return Cur;
}

// Specializations for iterating through IR metadata stack contexts.
/// Construct a metadata call stack iterator at the beginning or end of \p N.
/// @param N Metadata node whose stack ids are iterated, or null.
/// @param End If true, position at end; otherwise at begin.
template <>
LLVM_ABI
CallStack<MDNode, MDNode::op_iterator>::CallStackIterator::CallStackIterator(
    const MDNode *N, bool End);
/// Return the stack id at the current metadata iterator position.
/// @return Stack id at the current metadata iterator position.
template <>
LLVM_ABI uint64_t
CallStack<MDNode, MDNode::op_iterator>::CallStackIterator::operator*();
/// Return the last stack id in a metadata call stack.
/// @return Last stack id in the metadata call stack.
template <>
LLVM_ABI uint64_t CallStack<MDNode, MDNode::op_iterator>::back() const;

} // end namespace memprof
} // end namespace llvm

#endif
