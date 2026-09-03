//===-- JITLinkMemoryManager.h - JITLink mem manager interface --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains the JITLinkMemoryManager interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_JITLINKMEMORYMANAGER_H
#define LLVM_EXECUTIONENGINE_JITLINK_JITLINKMEMORYMANAGER_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ExecutionEngine/JITLink/JITLinkDylib.h"
#include "llvm/ExecutionEngine/Orc/Shared/AllocationActions.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/MemoryFlags.h"
#include "llvm/ExecutionEngine/Orc/SymbolStringPool.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MSVCErrorWorkarounds.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/RecyclingAllocator.h"
#include "llvm/TargetParser/Triple.h"

#include <cassert>
#include <cstdint>
#include <future>
#include <mutex>

namespace llvm {
namespace jitlink {

class Block;
class LinkGraph;
class Section;

/// Manages allocations of JIT memory.
///
/// Instances of this class may be accessed concurrently from multiple threads
/// and their implemetations should include any necessary synchronization.
class LLVM_ABI JITLinkMemoryManager {
public:

  /// Represents a finalized allocation.
  ///
  /// Finalized allocations must be passed to the
  /// JITLinkMemoryManager:deallocate method prior to being destroyed.
  ///
  /// The interpretation of the Address associated with the finalized allocation
  /// is up to the memory manager implementation. Common options are using the
  /// base address of the allocation, or the address of a memory management
  /// object that tracks the allocation.
  class FinalizedAlloc {
    friend class JITLinkMemoryManager;

    static constexpr auto InvalidAddr = ~uint64_t(0);

  public:
    /// Construct a default (invalid) finalized allocation.
    FinalizedAlloc() = default;
    /// Construct a finalized allocation for the given executor address.
    /// \param A Executor address associated with this allocation.
    explicit FinalizedAlloc(orc::ExecutorAddr A) : A(A) {
      assert(A.getValue() != InvalidAddr &&
             "Explicitly creating an invalid allocation?");
    }
    /// Deleted copy constructor; finalized allocations are move-only.
    /// \param Other Unused; copy construction is deleted.
    FinalizedAlloc(const FinalizedAlloc &Other) = delete;
    /// Move-construct, leaving \p Other in the default (invalid) state.
    /// \param Other Source finalized allocation whose address is taken.
    FinalizedAlloc(FinalizedAlloc &&Other) : A(Other.A) {
      Other.A.setValue(InvalidAddr);
    }
    /// Deleted copy assignment; finalized allocations are move-only.
    /// \param Other Unused; copy assignment is deleted.
    FinalizedAlloc &operator=(const FinalizedAlloc &Other) = delete;
    /// Move-assign, leaving \p Other in the default (invalid) state.
    /// \param Other Source finalized allocation whose address is taken.
    /// \return Reference to this finalized allocation.
    FinalizedAlloc &operator=(FinalizedAlloc &&Other) {
      assert(A.getValue() == InvalidAddr &&
             "Cannot overwrite active finalized allocation");
      std::swap(A, Other.A);
      return *this;
    }
    /// Destroy this finalized allocation.
    ///
    /// Asserts that the allocation was deallocated or released beforehand.
    ~FinalizedAlloc() {
      assert(A.getValue() == InvalidAddr &&
             "Finalized allocation was not deallocated");
    }

    /// FinalizedAllocs convert to false for default-constructed, and
    /// true otherwise. Default-constructed allocs need not be deallocated.
    /// \return False if default-constructed (invalid), true otherwise.
    explicit operator bool() const { return A.getValue() != InvalidAddr; }

    /// Returns the address associated with this finalized allocation.
    /// The allocation is unmodified.
    /// \return Executor address associated with this allocation.
    orc::ExecutorAddr getAddress() const { return A; }

    /// Release ownership of the associated address and reset to default.
    ///
    /// Returns the address associated with this finalized allocation and
    /// resets this object to the default state. This should only be used by
    /// allocators when deallocating memory.
    /// \return Executor address previously associated with this allocation.
    orc::ExecutorAddr release() {
      orc::ExecutorAddr Tmp = A;
      A.setValue(InvalidAddr);
      return Tmp;
    }

  private:
    orc::ExecutorAddr A{InvalidAddr};
  };

  /// Represents an allocation which has not been finalized yet.
  ///
  /// InFlightAllocs manage both executor memory allocations and working
  /// memory allocations.
  ///
  /// On finalization, the InFlightAlloc should transfer the content of
  /// working memory into executor memory, apply memory protections, and
  /// run any finalization functions.
  ///
  /// Working memory should be kept alive at least until one of the following
  /// happens: (1) the InFlightAlloc instance is destroyed, (2) the
  /// InFlightAlloc is abandoned, (3) finalized target memory is destroyed.
  ///
  /// If abandon is called then working memory and executor memory should both
  /// be freed.
  class LLVM_ABI InFlightAlloc {
  public:
    /// Continuation called when finalization completes.
    using OnFinalizedFunction = unique_function<void(Expected<FinalizedAlloc>)>;
    /// Continuation called when an abandon operation completes.
    using OnAbandonedFunction = unique_function<void(Error)>;

    /// Destroy this in-flight allocation.
    virtual ~InFlightAlloc();

    /// Called prior to finalization if the allocation should be abandoned.
    /// \param OnAbandoned Continuation invoked when abandon completes.
    virtual void abandon(OnAbandonedFunction OnAbandoned) = 0;

    /// Called to transfer working memory to the target and apply finalization.
    /// \param OnFinalized Continuation invoked with the finalized allocation.
    virtual void finalize(OnFinalizedFunction OnFinalized) = 0;

    /// Synchronous convenience version of finalize.
    /// \return The finalized allocation, or an error on failure.
    Expected<FinalizedAlloc> finalize() {
      std::promise<MSVCPExpected<FinalizedAlloc>> FinalizeResultP;
      auto FinalizeResultF = FinalizeResultP.get_future();
      finalize([&](Expected<FinalizedAlloc> Result) {
        FinalizeResultP.set_value(std::move(Result));
      });
      return FinalizeResultF.get();
    }
  };

  /// Typedef for the argument to be passed to OnAllocatedFunction.
  using AllocResult = Expected<std::unique_ptr<InFlightAlloc>>;

  /// Called when allocation has been completed.
  using OnAllocatedFunction = unique_function<void(AllocResult)>;

  /// Called when deallocation has been completed.
  using OnDeallocatedFunction = unique_function<void(Error)>;

  /// Destroy this memory manager.
  virtual ~JITLinkMemoryManager();

  /// Start the allocation process.
  ///
  /// If the initial allocation is successful then the OnAllocated function will
  /// be called with a std::unique_ptr<InFlightAlloc> value. If the assocation
  /// is unsuccessful then the OnAllocated function will be called with an
  /// Error.
  /// \param JD JITLink dylib associated with the allocation, or null.
  /// \param G Link graph whose segments are being allocated.
  /// \param OnAllocated Continuation invoked when allocation completes.
  virtual void allocate(const JITLinkDylib *JD, LinkGraph &G,
                        OnAllocatedFunction OnAllocated) = 0;

  /// Convenience function for blocking allocation.
  /// \param JD JITLink dylib associated with the allocation, or null.
  /// \param G Link graph whose segments are being allocated.
  /// \return An in-flight allocation on success, or an error on failure.
  AllocResult allocate(const JITLinkDylib *JD, LinkGraph &G) {
    std::promise<MSVCPExpected<std::unique_ptr<InFlightAlloc>>> AllocResultP;
    auto AllocResultF = AllocResultP.get_future();
    allocate(JD, G, [&](AllocResult Alloc) {
      AllocResultP.set_value(std::move(Alloc));
    });
    return AllocResultF.get();
  }

  /// Deallocate a list of allocation objects.
  ///
  /// Dealloc actions will be run in reverse order (from the end of the vector
  /// to the start).
  /// \param Allocs Finalized allocations to deallocate.
  /// \param OnDeallocated Continuation invoked when deallocation completes.
  virtual void deallocate(std::vector<FinalizedAlloc> Allocs,
                          OnDeallocatedFunction OnDeallocated) = 0;

  /// Convenience function for deallocation of a single alloc.
  /// \param Alloc Finalized allocation to deallocate.
  /// \param OnDeallocated Continuation invoked when deallocation completes.
  void deallocate(FinalizedAlloc Alloc, OnDeallocatedFunction OnDeallocated) {
    std::vector<FinalizedAlloc> Allocs;
    Allocs.push_back(std::move(Alloc));
    deallocate(std::move(Allocs), std::move(OnDeallocated));
  }

  /// Convenience function for blocking deallocation.
  /// \param Allocs Finalized allocations to deallocate.
  /// \return Success, or an error if deallocation failed.
  Error deallocate(std::vector<FinalizedAlloc> Allocs) {
    std::promise<MSVCPError> DeallocResultP;
    auto DeallocResultF = DeallocResultP.get_future();
    deallocate(std::move(Allocs),
               [&](Error Err) { DeallocResultP.set_value(std::move(Err)); });
    return DeallocResultF.get();
  }

  /// Convenience function for blocking deallocation of a single alloc.
  /// \param Alloc Finalized allocation to deallocate.
  /// \return Success, or an error if deallocation failed.
  Error deallocate(FinalizedAlloc Alloc) {
    std::vector<FinalizedAlloc> Allocs;
    Allocs.push_back(std::move(Alloc));
    return deallocate(std::move(Allocs));
  }

  /// Called when a JITLinkDylib that this manager previously registered
  /// with (via JITLinkDylib::notifyOnDestruction) is being destroyed.
  ///
  /// May be used to free resources held on behalf of the JITLinkDylib (e.g.
  /// reserved address ranges). The JITLinkDylib is guaranteed not to make
  /// any further use of those resources after this call returns, so
  /// clean-up may be deferred and completed asynchronously.
  /// \param JD JITLink dylib that is being destroyed.
  virtual void notifyDestroying(JITLinkDylib &JD) {}
};

/// BasicLayout simplifies the implementation of JITLinkMemoryManagers.
///
/// BasicLayout groups Sections into Segments based on their memory protection
/// and deallocation policies. JITLinkMemoryManagers can construct a BasicLayout
/// from a Graph, and then assign working memory and addresses to each of the
/// Segments. These addreses will be mapped back onto the Graph blocks in
/// the apply method.
class BasicLayout {
public:
  /// One segment of a basic layout, holding alignment, sizes, and addresses.
  ///
  /// The Alignment, ContentSize and ZeroFillSize of each segment will be
  /// pre-filled from the Graph. Clients must set the Addr and WorkingMem fields
  /// prior to calling apply.
  class Segment {
    friend class BasicLayout;

    // FIXME: The C++98 initializer is an attempt to work around compile
    // failures due to
    // http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_defects.html#1397. We
    // should be able to switch this back to member initialization once that
    // issue is fixed.

  public:
    /// Construct a default segment with zero sizes and null working memory.
    Segment() = default;
    /// Required alignment for this segment.
    Align Alignment;
    /// Size in bytes of initialized content in this segment.
    size_t ContentSize = 0;
    /// Size in bytes of zero-fill following the content.
    uint64_t ZeroFillSize = 0;
    /// Executor address assigned to the start of this segment.
    orc::ExecutorAddr Addr;
    /// Pointer to working memory for this segment's content.
    char *WorkingMem = nullptr;

  private:
    size_t NextWorkingMemOffset = 0;
    std::vector<Block *> ContentBlocks, ZeroFillBlocks;
  };

  /// Groups segments by memory deallocation policy for slab allocation.
  ///
  /// A convenience class that further groups segments based on memory
  /// deallocation policy. This allows clients to make two slab allocations:
  /// one for all standard segments, and one for all finalize segments.
  struct ContiguousPageBasedLayoutSizes {
    /// Total page-rounded size of all standard (non-finalize) segments.
    uint64_t StandardSegs = 0;
    /// Total page-rounded size of all finalize segments.
    uint64_t FinalizeSegs = 0;

    /// Return the combined size of standard and finalize segments.
    /// \return Sum of StandardSegs and FinalizeSegs.
    uint64_t total() const { return StandardSegs + FinalizeSegs; }
  };

private:
  using SegmentMap = orc::AllocGroupSmallMap<Segment>;

public:
  /// Construct a basic layout by grouping sections of \p G into segments.
  /// \param G Link graph whose sections are laid out.
  LLVM_ABI BasicLayout(LinkGraph &G);

  /// Return a reference to the graph this allocation was created from.
  /// \return The link graph associated with this layout.
  LinkGraph &getGraph() { return G; }

  /// Returns the total number of required to allocate all segments (with each
  /// segment padded out to page size) for all standard segments, and all
  /// finalize segments.
  ///
  /// This is a convenience function for the common case where the segments will
  /// be allocated contiguously.
  ///
  /// This function will return an error if any segment has an alignment that
  /// is higher than a page.
  /// \param PageSize Page size used to round up each segment.
  /// \return Page-rounded sizes for standard and finalize segments, or an
  /// error if any segment alignment exceeds the page size.
  LLVM_ABI Expected<ContiguousPageBasedLayoutSizes>
  getContiguousPageBasedLayoutSizes(uint64_t PageSize);

  /// Returns an iterator over the segments of the layout.
  /// \return Range covering each segment in the layout.
  iterator_range<SegmentMap::iterator> segments() {
    return {Segments.begin(), Segments.end()};
  }

  /// Apply the layout to the graph.
  /// \return Success, or an error if the layout could not be applied.
  LLVM_ABI Error apply();

  /// Return a reference to the graph's allocation-action list.
  ///
  /// This convenience function saves callers from having to #include
  /// LinkGraph.h if all they need are allocation actions.
  /// \return The link graph's allocation-action list.
  LLVM_ABI orc::shared::AllocActions &graphAllocActions();

private:
  LinkGraph &G;
  SegmentMap Segments;
};

/// A utility class for making simple allocations using JITLinkMemoryManager.
///
/// SimpleSegementAlloc takes a mapping of AllocGroups to Segments and uses
/// this to create a LinkGraph with one Section (containing one Block) per
/// Segment. Clients can obtain a pointer to the working memory and executor
/// address of that block using the Segment's AllocGroup. Once memory has been
/// populated, clients can call finalize to finalize the memory.
///
/// Note: Segments with MemLifetime::NoAlloc are not permitted, since they would
/// not be useful, and their presence is likely to indicate a bug.
class SimpleSegmentAlloc {
public:
  /// Describes a segment to be allocated.
  struct Segment {
    /// Construct a default segment with zero content size.
    Segment() = default;
    /// Construct a segment with the given content size and alignment.
    /// \param ContentSize Size in bytes of initialized content.
    /// \param ContentAlign Required alignment of the content.
    Segment(size_t ContentSize, Align ContentAlign)
        : ContentSize(ContentSize), ContentAlign(ContentAlign) {}

    /// Size in bytes of initialized content in this segment.
    size_t ContentSize = 0;
    /// Required alignment of this segment's content.
    Align ContentAlign;
  };

  /// Describes the segment working memory and executor address.
  struct SegmentInfo {
    /// Executor address of the allocated segment.
    orc::ExecutorAddr Addr;
    /// Mutable view of working memory for the segment.
    MutableArrayRef<char> WorkingMem;
  };

  /// Map from allocation group to segment description.
  using SegmentMap = orc::AllocGroupSmallMap<Segment>;

  /// Continuation called when Create completes.
  using OnCreatedFunction = unique_function<void(Expected<SimpleSegmentAlloc>)>;

  /// Continuation called when finalization completes.
  using OnFinalizedFunction =
      JITLinkMemoryManager::InFlightAlloc::OnFinalizedFunction;

  /// Continuation called when an abandon operation completes.
  using OnAbandonedFunction = unique_function<void(Error)>;

  /// Asynchronously create a simple segment allocation.
  /// \param MemMgr Memory manager used to allocate segments.
  /// \param SSP Symbol string pool for the constructed link graph.
  /// \param TT Target triple for the constructed link graph.
  /// \param JD JITLink dylib associated with the allocation, or null.
  /// \param Segments Map of allocation groups to segment descriptions.
  /// \param OnCreated Continuation invoked with the created allocator.
  LLVM_ABI static void Create(JITLinkMemoryManager &MemMgr,
                              std::shared_ptr<orc::SymbolStringPool> SSP,
                              Triple TT, const JITLinkDylib *JD,
                              SegmentMap Segments, OnCreatedFunction OnCreated);

  /// Synchronously create a simple segment allocation.
  /// \param MemMgr Memory manager used to allocate segments.
  /// \param SSP Symbol string pool for the constructed link graph.
  /// \param TT Target triple for the constructed link graph.
  /// \param JD JITLink dylib associated with the allocation, or null.
  /// \param Segments Map of allocation groups to segment descriptions.
  /// \return The created allocator, or an error on failure.
  LLVM_ABI static Expected<SimpleSegmentAlloc>
  Create(JITLinkMemoryManager &MemMgr,
         std::shared_ptr<orc::SymbolStringPool> SSP, Triple TT,
         const JITLinkDylib *JD, SegmentMap Segments);

  /// Move-construct, transferring ownership from \p Other.
  /// \param Other Source allocator left in a moved-from state.
  LLVM_ABI SimpleSegmentAlloc(SimpleSegmentAlloc &&Other);
  /// Move-assign, transferring ownership from the right-hand side.
  /// \param Other Source allocator left in a moved-from state.
  /// \return Reference to this allocator.
  LLVM_ABI SimpleSegmentAlloc &operator=(SimpleSegmentAlloc &&Other);
  /// Destroy this simple segment allocator.
  LLVM_ABI ~SimpleSegmentAlloc();

  /// Returns the SegmentInfo for the given group.
  /// \param AG Allocation group whose segment info is requested.
  /// \return Working memory and executor address for the segment.
  LLVM_ABI SegmentInfo getSegInfo(orc::AllocGroup AG);

  /// Finalize all groups (async version).
  /// \param OnFinalized Continuation invoked with the finalized allocation.
  void finalize(OnFinalizedFunction OnFinalized) {
    Alloc->finalize(std::move(OnFinalized));
  }

  /// Finalize all groups.
  /// \return The finalized allocation, or an error on failure.
  Expected<JITLinkMemoryManager::FinalizedAlloc> finalize() {
    return Alloc->finalize();
  }

  /// Free allocated memory if finalize won't be called.
  /// \param OnAbandoned Continuation invoked when abandon completes.
  void abandon(OnAbandonedFunction OnAbandoned) {
    Alloc->abandon(std::move(OnAbandoned));
  }

private:
  SimpleSegmentAlloc(
      std::unique_ptr<LinkGraph> G,
      orc::AllocGroupSmallMap<Block *> ContentBlocks,
      std::unique_ptr<JITLinkMemoryManager::InFlightAlloc> Alloc);

  std::unique_ptr<LinkGraph> G;
  orc::AllocGroupSmallMap<Block *> ContentBlocks;
  std::unique_ptr<JITLinkMemoryManager::InFlightAlloc> Alloc;
};

/// A JITLinkMemoryManager that allocates in-process memory.
class LLVM_ABI InProcessMemoryManager : public JITLinkMemoryManager {
public:
  /// In-flight allocation for in-process memory management.
  class IPInFlightAlloc;

  /// Attempts to auto-detect the host page size.
  /// \return An in-process memory manager, or an error if page size detection
  /// fails.
  static Expected<std::unique_ptr<InProcessMemoryManager>> Create();

  /// Create an instance using the given page size.
  /// \param PageSize Host page size; must be a power of two.
  InProcessMemoryManager(uint64_t PageSize) : PageSize(PageSize) {
    assert(isPowerOf2_64(PageSize) && "PageSize must be a power of 2");
  }

  /// Allocate memory for the segments of \p G in the current process.
  /// \param JD JITLink dylib associated with the allocation, or null.
  /// \param G Link graph whose segments are being allocated.
  /// \param OnAllocated Continuation invoked when allocation completes.
  void allocate(const JITLinkDylib *JD, LinkGraph &G,
                OnAllocatedFunction OnAllocated) override;

  /// Inherit the convenience allocate overloads from the base class.
  using JITLinkMemoryManager::allocate;

  /// Deallocate a list of finalized in-process allocations.
  /// \param Alloc Finalized allocations to deallocate.
  /// \param OnDeallocated Continuation invoked when deallocation completes.
  void deallocate(std::vector<FinalizedAlloc> Alloc,
                  OnDeallocatedFunction OnDeallocated) override;

  /// Inherit the convenience deallocate overloads from the base class.
  using JITLinkMemoryManager::deallocate;

private:
  // FIXME: Use an in-place array instead of a vector for DeallocActions.
  //        There shouldn't need to be a heap alloc for this.
  struct FinalizedAllocInfo {
    sys::MemoryBlock StandardSegments;
    std::vector<orc::shared::WrapperFunctionCall> DeallocActions;
  };

  FinalizedAlloc createFinalizedAlloc(
      sys::MemoryBlock StandardSegments,
      std::vector<orc::shared::WrapperFunctionCall> DeallocActions);

  uint64_t PageSize;
  std::mutex FinalizedAllocsMutex;
  RecyclingAllocator<BumpPtrAllocator, FinalizedAllocInfo> FinalizedAllocInfos;
};

} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_JITLINKMEMORYMANAGER_H
