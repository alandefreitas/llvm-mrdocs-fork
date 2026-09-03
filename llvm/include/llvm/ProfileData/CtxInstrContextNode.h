//===--- CtxInstrContextNode.h - Contextual Profile Node --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//==============================================================================
//
// NOTE!
// llvm/include/llvm/ProfileData/CtxInstrContextNode.h and
//   compiler-rt/lib/ctx_profile/CtxInstrContextNode.h
// must be exact copies of each other.
//
// compiler-rt creates these objects as part of the instrumentation runtime for
// contextual profiling. LLVM only consumes them to convert a contextual tree
// to a bitstream.
//
//==============================================================================

/// The contextual profile is a directed tree where each node has one parent. A
/// node (ContextNode) corresponds to a function activation. The root of the
/// tree is at a function that was marked as entrypoint to the compiler. A node
/// stores counter values for edges and a vector of subcontexts. These are the
/// contexts of callees. The index in the subcontext vector corresponds to the
/// index of the callsite (as was instrumented via llvm.instrprof.callsite). At
/// that index we find a linked list, potentially empty, of ContextNodes. Direct
/// calls will have 0 or 1 values in the linked list, but indirect callsites may
/// have more.
///
/// The ContextNode has a fixed sized header describing it - the GUID of the
/// function, the size of the counter and callsite vectors. It is also an
/// (intrusive) linked list for the purposes of the indirect call case above.
///
/// Allocation is expected to happen on an Arena. The allocation lays out inline
/// the counter and subcontexts vectors. The class offers APIs to correctly
/// reference the latter.
///
/// The layout is as follows:
///
/// [[declared fields][counters vector][vector of ptrs to subcontexts]]
///
/// See also documentation on the counters and subContexts members below.
///
/// The structure of the ContextNode is known to LLVM, because LLVM needs to:
///   (1) increment counts, and
///   (2) form a GEP for the position in the subcontext list of a callsite
/// This means changes to LLVM contextual profile lowering and changes here
/// must be coupled.
/// Note: the header content isn't interesting to LLVM (other than its size)
///
/// Part of contextual collection is the notion of "scratch contexts". These are
/// buffers that are "large enough" to allow for memory-safe acceses during
/// counter increments - meaning the counter increment code in LLVM doesn't need
/// to be concerned with memory safety. Their subcontexts never get populated,
/// though. The runtime code here produces and recognizes them.

#ifndef LLVM_PROFILEDATA_CTXINSTRCONTEXTNODE_H
#define LLVM_PROFILEDATA_CTXINSTRCONTEXTNODE_H

#include <stdint.h>
#include <stdlib.h>

namespace llvm {
/// Types and helpers for contextual instrumentation profiles.
namespace ctx_profile {
/// Stable identifier for a function in a contextual profile.
using GUID = uint64_t;

/// Node in a contextual profile tree for one function activation.
class ContextNode final {
  const GUID Guid;
  ContextNode *const Next;
  const uint32_t NumCounters;
  const uint32_t NumCallsites;

public:
  /// Construct a context node header for \p Guid with the given vector sizes.
  /// @param Guid Function identifier for this activation.
  /// @param NumCounters Number of counter slots laid out after the header.
  /// @param NumCallsites Number of callsite slots laid out after the counters.
  /// @param Next Next node in an indirect-call linked list, if any.
  ContextNode(GUID Guid, uint32_t NumCounters, uint32_t NumCallsites,
              ContextNode *Next = nullptr)
      : Guid(Guid), Next(Next), NumCounters(NumCounters),
        NumCallsites(NumCallsites) {}

  /// Return the byte size needed to allocate a node with the given vector sizes.
  /// @param NumCounters Number of counter slots.
  /// @param NumCallsites Number of callsite slots.
  /// @return Total allocation size including the inline vectors.
  static inline size_t getAllocSize(uint32_t NumCounters,
                                    uint32_t NumCallsites) {
    return sizeof(ContextNode) + sizeof(uint64_t) * NumCounters +
           sizeof(ContextNode *) * NumCallsites;
  }

  /// Return a pointer to the counters vector laid out after the header.
  /// @return Mutable pointer to the inline counters vector.
  uint64_t *counters() {
    ContextNode *addr_after = &(this[1]);
    return reinterpret_cast<uint64_t *>(addr_after);
  }

  /// Return the number of counter slots in this node.
  /// @return Number of counter slots.
  uint32_t counters_size() const { return NumCounters; }
  /// Return the number of callsite slots in this node.
  /// @return Number of callsite slots.
  uint32_t callsites_size() const { return NumCallsites; }

  /// Return a const pointer to the counters vector laid out after the header.
  /// @return Const pointer to the inline counters vector.
  const uint64_t *counters() const {
    return const_cast<ContextNode *>(this)->counters();
  }

  /// Return a pointer to the subcontext vector laid out after the counters.
  /// @return Mutable pointer to the inline subcontext vector.
  ContextNode **subContexts() {
    return reinterpret_cast<ContextNode **>(&(counters()[NumCounters]));
  }

  /// Return a const pointer to the subcontext vector after the counters.
  /// @return Const pointer to the inline subcontext vector.
  ContextNode *const *subContexts() const {
    return const_cast<ContextNode *>(this)->subContexts();
  }

  /// Return the function GUID for this activation.
  /// @return Function GUID for this node.
  GUID guid() const { return Guid; }
  /// Return the next node in the indirect-call linked list, if any.
  /// @return Next node in the list, or nullptr if none.
  ContextNode *next() const { return Next; }

  /// Return the total allocation size of this node including inline vectors.
  /// @return Total allocation size of this node.
  size_t size() const { return getAllocSize(NumCounters, NumCallsites); }

  /// Return the entry count stored in the first counter slot.
  /// @return Entry count from the first counter slot.
  uint64_t entrycount() const { return counters()[0]; }
};

/// Declares the fields of FunctionData shared with LLVM.
///
/// The internal structure of FunctionData. This makes sure that changes to
/// the fields of FunctionData either get automatically captured on the llvm
/// side, or force a manual corresponding update. See CtxInstrProfiling.h for
/// example expansions.
/// @param PTRDECL Macro taking a type and field name for a pointer field.
/// @param CONTEXT_PTR Expansion that declares the context-pointer field.
/// @param VOLATILE_PTRDECL Macro taking a type and field name for a volatile
///        pointer field.
/// @param MUTEXDECL Macro taking the name of a mutex field.
#define CTXPROF_FUNCTION_DATA(PTRDECL, CONTEXT_PTR, VOLATILE_PTRDECL,          \
                              MUTEXDECL)                                       \
  PTRDECL(FunctionData, Next)                                                  \
  VOLATILE_PTRDECL(void, EntryAddress)                                         \
  CONTEXT_PTR                                                                  \
  VOLATILE_PTRDECL(ContextNode, FlatCtx)                                       \
  MUTEXDECL(Mutex)

/// Callback interface passed to `__llvm_ctx_profile_fetch`.
///
/// Abstraction for the parameter passed to `__llvm_ctx_profile_fetch`.
/// `startContextSection` is called before any context roots are sent for
/// writing. Then one or more `writeContextual` calls are made; finally,
/// `endContextSection` is called.
class ProfileWriter {
public:
  /// Begin writing the contextual profile section.
  virtual void startContextSection() = 0;
  /// Write one contextual profile root and optional unhandled contexts.
  /// @param RootNode Root context node to serialize.
  /// @param Unhandled Optional list of contexts not attached to a root.
  /// @param TotalRootEntryCount Aggregate entry count for the root.
  virtual void writeContextual(const ctx_profile::ContextNode &RootNode,
                               const ctx_profile::ContextNode *Unhandled,
                               uint64_t TotalRootEntryCount) = 0;
  /// Finish writing the contextual profile section.
  virtual void endContextSection() = 0;

  /// Begin writing the flat profile section.
  virtual void startFlatSection() = 0;
  /// Write one flat profile buffer for \p Guid.
  /// @param Guid Function identifier for the flat profile.
  /// @param Buffer Counter values to write.
  /// @param BufferSize Number of elements in \p Buffer.
  virtual void writeFlat(ctx_profile::GUID Guid, const uint64_t *Buffer,
                         size_t BufferSize) = 0;
  /// Finish writing the flat profile section.
  virtual void endFlatSection() = 0;

  /// Destroy the profile writer.
  virtual ~ProfileWriter() = default;
};
} // namespace ctx_profile
} // namespace llvm
#endif
