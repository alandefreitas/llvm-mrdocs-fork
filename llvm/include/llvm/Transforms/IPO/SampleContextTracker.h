//===- Transforms/IPO/SampleContextTracker.h --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for context-sensitive profile tracker used
/// by CSSPGO.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_SAMPLECONTEXTTRACKER_H
#define LLVM_TRANSFORMS_IPO_SAMPLECONTEXTTRACKER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/Compiler.h"
#include <map>
#include <queue>
#include <vector>

namespace llvm {
class CallBase;
class DILocation;
class Function;
class Instruction;

/// Trie node representing a calling context and its sample profile.
///
/// Internal trie tree representation used for tracking context tree and sample
/// profiles. The path from root node to a given node represents the context of
/// that nodes' profile.
class ContextTrieNode {
public:
  /// Construct a context trie node.
  ///
  /// \param Parent Parent context node, or null for the root.
  /// \param FName Function name for this context.
  /// \param FSamples Function samples associated with this context, or null.
  /// \param CallLoc Callsite location in the parent context.
  ContextTrieNode(ContextTrieNode *Parent = nullptr,
                  FunctionId FName = FunctionId(),
                  FunctionSamples *FSamples = nullptr,
                  LineLocation CallLoc = {0, 0})
      : ParentContext(Parent), FuncName(FName), FuncSamples(FSamples),
        CallSiteLoc(CallLoc){};
  /// Return the child context for \p ChildName at \p CallSite, or null.
  ///
  /// \param CallSite Callsite location in this context.
  /// \param ChildName Callee function name identifying the child context.
  /// \return The matching child context node, or null if none exists.
  LLVM_ABI ContextTrieNode *getChildContext(const LineLocation &CallSite,
                                            FunctionId ChildName);
  /// Return the hottest child context at \p CallSite, or null if none exist.
  ///
  /// \param CallSite Callsite location whose child contexts are compared.
  /// \return The hottest child context at \p CallSite, or null if none exist.
  LLVM_ABI ContextTrieNode *
  getHottestChildContext(const LineLocation &CallSite);
  /// Return the child context for \p ChildName at \p CallSite, creating it when
  /// allowed.
  ///
  /// \param CallSite Callsite location in this context.
  /// \param ChildName Callee function name identifying the child context.
  /// \param AllowCreate If true, create the child when it is missing.
  /// \return The existing or newly created child context, or null if creation
  /// is disallowed and the child is missing.
  LLVM_ABI ContextTrieNode *
  getOrCreateChildContext(const LineLocation &CallSite, FunctionId ChildName,
                          bool AllowCreate = true);
  /// Remove the child context for \p ChildName at \p CallSite.
  ///
  /// \param CallSite Callsite location in this context.
  /// \param ChildName Callee function name identifying the child to remove.
  LLVM_ABI void removeChildContext(const LineLocation &CallSite,
                                   FunctionId ChildName);
  /// Return the map of all child contexts keyed by location and callee.
  ///
  /// \return The map of all child contexts keyed by location and callee.
  LLVM_ABI std::map<uint64_t, ContextTrieNode> &getAllChildContext();
  /// Return the function name associated with this context node.
  ///
  /// \return The function name associated with this context node.
  LLVM_ABI FunctionId getFuncName() const;
  /// Return the function samples for this context, or null if unset.
  ///
  /// \return The function samples for this context, or null if unset.
  LLVM_ABI FunctionSamples *getFunctionSamples() const;
  /// Set the function samples associated with this context.
  ///
  /// \param FSamples Function samples to attach, or null to clear them.
  LLVM_ABI void setFunctionSamples(FunctionSamples *FSamples);
  /// Return the accumulated function size for this context, if known.
  ///
  /// \return The accumulated function size for this context, if known.
  LLVM_ABI std::optional<uint32_t> getFunctionSize() const;
  /// Add \p FSize to the accumulated function size for this context.
  ///
  /// \param FSize Function size contribution to accumulate.
  LLVM_ABI void addFunctionSize(uint32_t FSize);
  /// Return the callsite location of this node in its parent context.
  ///
  /// \return The callsite location of this node in its parent context.
  LLVM_ABI LineLocation getCallSiteLoc() const;
  /// Return the parent context node, or null for the root.
  ///
  /// \return The parent context node, or null for the root.
  LLVM_ABI ContextTrieNode *getParentContext() const;
  /// Set the parent context node.
  ///
  /// \param Parent New parent context, or null for the root.
  LLVM_ABI void setParentContext(ContextTrieNode *Parent);
  /// Set the callsite location of this node in its parent context.
  ///
  /// \param Loc New callsite location in the parent.
  LLVM_ABI void setCallSiteLoc(const LineLocation &Loc);
  /// Dump this context node to the debug stream.
  LLVM_ABI void dumpNode();
  /// Dump the subtree rooted at this node to the debug stream.
  LLVM_ABI void dumpTree();

private:
  // Map line+discriminator location to child context
  std::map<uint64_t, ContextTrieNode> AllChildContext;

  // Link to parent context node
  ContextTrieNode *ParentContext;

  // Function name for current context
  FunctionId FuncName;

  // Function Samples for current context
  FunctionSamples *FuncSamples;

  // Function size for current context
  std::optional<uint32_t> FuncSize;

  // Callsite location in parent context
  LineLocation CallSiteLoc;
};

/// Tracker that manages context-sensitive profiles for CSSPGO.
///
/// Profile tracker that manages profiles and its associated context. It
/// provides interfaces used by sample profile loader to query context profile
/// or base profile for given function or location; it also manages context
/// tree manipulation that is needed to accommodate inline decisions so we have
/// accurate post-inline profile for functions. Internally context profiles are
/// organized in a trie, with each node representing profile for specific
/// calling context and the context is identified by path from root to the
/// node.
class SampleContextTracker {
public:
  /// Vector of context-sensitive function sample profiles.
  using ContextSamplesTy = std::vector<FunctionSamples *>;

  /// Construct an empty context tracker.
  SampleContextTracker() = default;
  /// Construct a tracker from \p Profiles and an optional GUID name map.
  ///
  /// \param Profiles Sample profile map used to build the context trie.
  /// \param GUIDToFuncNameMap Optional map from function GUID to real name.
  LLVM_ABI
  SampleContextTracker(SampleProfileMap &Profiles,
                       const DenseMap<uint64_t, StringRef> *GUIDToFuncNameMap);
  /// Populate the FuncToCtxtProfiles map after the trie is built.
  LLVM_ABI void populateFuncToCtxtMap();
  /// Query context profile for a specific callee at a callsite.
  ///
  /// The full context is identified by location of call instruction.
  ///
  /// \param Inst Call instruction locating the callsite context.
  /// \param CalleeName Name of the callee whose context profile is requested.
  /// \return The context profile for the callee at the callsite, or null.
  LLVM_ABI FunctionSamples *getCalleeContextSamplesFor(const CallBase &Inst,
                                                       StringRef CalleeName);
  /// Get samples for indirect call targets at the callsite given by \p DIL.
  ///
  /// \param DIL Debug location identifying the indirect callsite.
  /// \return The context profiles for indirect call targets at \p DIL.
  LLVM_ABI std::vector<const FunctionSamples *>
  getIndirectCalleeContextSamplesFor(const DILocation *DIL);
  /// Query context profile for the location identified by \p DIL.
  ///
  /// The full context is identified by input DILocation.
  ///
  /// \param DIL Debug location identifying the context to look up.
  /// \return The context profile for \p DIL, or null if none exists.
  LLVM_ABI FunctionSamples *getContextSamplesFor(const DILocation *DIL);
  /// Query context profile for the sample context \p Context.
  ///
  /// \param Context Sample context identifying the profile to look up.
  /// \return The context profile for \p Context, or null if none exists.
  LLVM_ABI FunctionSamples *getContextSamplesFor(const SampleContext &Context);
  /// Get all context profiles for \p Func.
  ///
  /// \param Func Function whose context profiles are returned.
  /// \return All context profiles associated with \p Func.
  LLVM_ABI ContextSamplesTy &getAllContextSamplesFor(const Function &Func);
  /// Get all context profiles for the function named \p Name.
  ///
  /// \param Name Function name whose context profiles are returned.
  /// \return All context profiles associated with \p Name.
  LLVM_ABI ContextSamplesTy &getAllContextSamplesFor(StringRef Name);
  /// Return the trie node for \p Context, creating the path when allowed.
  ///
  /// \param Context Sample context whose trie path is retrieved or created.
  /// \param AllowCreate If true, create missing nodes along the context path.
  /// \return The trie node for \p Context, or null if creation is disallowed
  /// and the path is incomplete.
  LLVM_ABI ContextTrieNode *getOrCreateContextPath(const SampleContext &Context,
                                                   bool AllowCreate);
  /// Query the base profile for \p Func.
  ///
  /// A base profile is a merged view of all context profiles for contexts that
  /// are not inlined.
  ///
  /// \param Func Function whose base profile is requested.
  /// \param MergeContext If true, merge remaining context profiles into the
  /// base profile.
  /// \return The base profile for \p Func, or null if none exists.
  LLVM_ABI FunctionSamples *getBaseSamplesFor(const Function &Func,
                                              bool MergeContext = true);
  /// Query the base profile for the function named \p Name.
  ///
  /// \param Name Function name whose base profile is requested.
  /// \param MergeContext If true, merge remaining context profiles into the
  /// base profile.
  /// \return The base profile for \p Name, or null if none exists.
  LLVM_ABI FunctionSamples *getBaseSamplesFor(FunctionId Name,
                                              bool MergeContext = true);
  /// Retrieve the context trie node for \p Context.
  ///
  /// \param Context Sample context identifying the trie node to return.
  /// \return The context trie node for \p Context, or null if none exists.
  LLVM_ABI ContextTrieNode *getContextFor(const SampleContext &Context);
  /// Get the real function name for trie node \p Node.
  ///
  /// \param Node Context trie node whose function name is returned.
  /// \return The real function name for \p Node.
  LLVM_ABI StringRef getFuncNameFor(ContextTrieNode *Node) const;
  /// Mark context profile \p InlinedSamples as inlined.
  ///
  /// This makes sure that inlined context profile will be excluded in
  /// function's base profile.
  ///
  /// \param InlinedSamples Context profile belonging to an inlined function.
  LLVM_ABI void
  markContextSamplesInlined(const FunctionSamples *InlinedSamples);
  /// Return the root node of the context trie.
  ///
  /// \return The root node of the context trie.
  LLVM_ABI ContextTrieNode &getRootContext();
  /// Promote and merge the context samples tree for callee \p CalleeName at
  /// \p Inst.
  ///
  /// \param Inst Call instruction locating the context to promote.
  /// \param CalleeName Callee whose context samples tree is promoted and
  /// merged.
  LLVM_ABI void promoteMergeContextSamplesTree(const Instruction &Inst,
                                               FunctionId CalleeName);

  /// Create a merged context-less profile map in \p ContextLessProfiles.
  ///
  /// \param ContextLessProfiles Output map filled with context-less profiles.
  LLVM_ABI void
  createContextLessProfileMap(SampleProfileMap &ContextLessProfiles);
  /// Return the trie node associated with profile \p FSamples, or null.
  ///
  /// \param FSamples Function samples whose owning context node is returned.
  /// \return The trie node associated with \p FSamples, or null.
  ContextTrieNode *
  getContextNodeForProfile(const FunctionSamples *FSamples) const {
    auto I = ProfileToNodeMap.find(FSamples);
    if (I == ProfileToNodeMap.end())
      return nullptr;
    return I->second;
  }
  /// Return the map from function name to context profiles.
  ///
  /// \return The map from function name to context profiles.
  HashKeyMap<std::unordered_map, FunctionId, ContextSamplesTy>
      &getFuncToCtxtProfiles() {
    return FuncToCtxtProfiles;
  }

  /// Forward iterator over context trie nodes in breadth-first order.
  class Iterator : public llvm::iterator_facade_base<
                       Iterator, std::forward_iterator_tag, ContextTrieNode *,
                       std::ptrdiff_t, ContextTrieNode **, ContextTrieNode *> {
    std::queue<ContextTrieNode *> NodeQueue;

  public:
    /// Construct an end iterator.
    explicit Iterator() = default;
    /// Construct an iterator starting at \p Node.
    ///
    /// \param Node Context trie node where iteration begins.
    explicit Iterator(ContextTrieNode *Node) { NodeQueue.push(Node); }
    /// Advance to the next node in breadth-first order.
    ///
    /// \return A reference to this iterator after advancing.
    Iterator &operator++() {
      assert(!NodeQueue.empty() && "Iterator already at the end");
      ContextTrieNode *Node = NodeQueue.front();
      NodeQueue.pop();
      for (auto &It : Node->getAllChildContext())
        NodeQueue.push(&It.second);
      return *this;
    }

    /// Return whether this iterator equals \p Other.
    ///
    /// \param Other Iterator to compare against.
    /// \return True if both iterators point to the same position.
    bool operator==(const Iterator &Other) const {
      if (NodeQueue.empty() && Other.NodeQueue.empty())
        return true;
      if (NodeQueue.empty() || Other.NodeQueue.empty())
        return false;
      return NodeQueue.front() == Other.NodeQueue.front();
    }

    /// Return the context trie node currently pointed to.
    ///
    /// \return The context trie node currently pointed to.
    ContextTrieNode *operator*() const {
      assert(!NodeQueue.empty() && "Invalid access to end iterator");
      return NodeQueue.front();
    }
  };

  /// Return an iterator to the first context trie node.
  ///
  /// \return An iterator to the first context trie node.
  Iterator begin() { return Iterator(&RootContext); }
  /// Return an iterator past the last context trie node.
  ///
  /// \return An iterator past the last context trie node.
  Iterator end() { return Iterator(); }

#ifndef NDEBUG
  /// Get a context string from root to the node for \p FSamples.
  ///
  /// \param FSamples Function samples whose context path is formatted.
  /// \return A string describing the context path from root to the node for
  /// \p FSamples.
  LLVM_ABI std::string getContextString(const FunctionSamples &FSamples) const;
  /// Get a context string from root to \p Node.
  ///
  /// \param Node Context trie node whose path from the root is formatted.
  /// \return A string describing the context path from root to \p Node.
  LLVM_ABI std::string getContextString(ContextTrieNode *Node) const;
#endif
  /// Dump the internal context profile trie.
  LLVM_ABI void dump();

private:
  ContextTrieNode *getContextFor(const DILocation *DIL);
  ContextTrieNode *getCalleeContextFor(const DILocation *DIL,
                                       FunctionId CalleeName);
  ContextTrieNode *getTopLevelContextNode(FunctionId FName);
  ContextTrieNode &addTopLevelContextNode(FunctionId FName);
  ContextTrieNode &promoteMergeContextSamplesTree(ContextTrieNode &NodeToPromo);
  void mergeContextNode(ContextTrieNode &FromNode, ContextTrieNode &ToNode);
  ContextTrieNode &
  promoteMergeContextSamplesTree(ContextTrieNode &FromNode,
                                 ContextTrieNode &ToNodeParent);
  ContextTrieNode &moveContextSamples(ContextTrieNode &ToNodeParent,
                                      const LineLocation &CallSite,
                                      ContextTrieNode &&NodeToMove);
  void setContextNode(const FunctionSamples *FSample, ContextTrieNode *Node) {
    ProfileToNodeMap[FSample] = Node;
  }
  // Map from function name to context profiles (excluding base profile)
  HashKeyMap<std::unordered_map, FunctionId, ContextSamplesTy>
      FuncToCtxtProfiles;

  // Map from current FunctionSample to the belonged context trie.
  DenseMap<const FunctionSamples *, ContextTrieNode *> ProfileToNodeMap;

  // Map from function guid to real function names. Only used in md5 mode.
  const DenseMap<uint64_t, StringRef> *GUIDToFuncNameMap;

  // Root node for context trie tree
  ContextTrieNode RootContext;
};

} // end namespace llvm
#endif // LLVM_TRANSFORMS_IPO_SAMPLECONTEXTTRACKER_H
