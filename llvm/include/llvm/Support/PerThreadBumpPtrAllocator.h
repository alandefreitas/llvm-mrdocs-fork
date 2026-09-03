//===- PerThreadBumpPtrAllocator.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PERTHREADBUMPPTRALLOCATOR_H
#define LLVM_SUPPORT_PERTHREADBUMPPTRALLOCATOR_H

#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <mutex>
#include <vector>

namespace llvm {
namespace parallel {

namespace detail {
/// Return a new process-unique PerThreadAllocator instance id. Ids are never
/// reused.
LLVM_ABI unsigned claimPerThreadAllocatorId();
} // namespace detail

/// Concurrent wrapper that gives each thread its own sub-allocator.
///
/// PerThreadAllocator wraps a thread-unsafe allocator (e.g. BumpPtrAllocator)
/// for lock-free concurrent allocation: each thread receives its own
/// sub-allocator on first allocation, and the PerThreadAllocator owns all
/// sub-allocators. Recommended to used with the thread pool in Parallel.h even
/// if there is no dependency on it.
template <typename AllocatorTy>
class PerThreadAllocator
    : public AllocatorBase<PerThreadAllocator<AllocatorTy>> {
  // Heap-allocated so that the class stays movable while holding a mutex.
  struct State {
    std::mutex Mutex;
    std::vector<std::unique_ptr<AllocatorTy>> Allocators;
  };

public:
  /// Construct an empty per-thread allocator with a fresh instance id.
  PerThreadAllocator()
      : S(std::make_unique<State>()), Id(detail::claimPerThreadAllocatorId()) {}

  /// \defgroup Methods which could be called asynchronously:
  ///
  /// @{

  /// Bring AllocatorBase typed Allocate overloads into scope.
  using AllocatorBase<PerThreadAllocator<AllocatorTy>>::Allocate;

  /// Bring AllocatorBase typed Deallocate overloads into scope.
  using AllocatorBase<PerThreadAllocator<AllocatorTy>>::Deallocate;

  /// Allocate \a Size bytes of \a Alignment aligned memory.
  ///
  /// \param Size Number of bytes to allocate.
  /// \param Alignment Required alignment of the allocated memory in bytes.
  /// \return Pointer to the allocated memory.
  void *Allocate(size_t Size, size_t Alignment) {
    return getThreadLocalAllocator().Allocate(Size, Alignment);
  }

  /// Deallocate \a Ptr to \a Size bytes of memory allocated by this
  /// allocator.
  ///
  /// \param Ptr Memory previously returned by Allocate.
  /// \param Size Size in bytes of the allocation being freed.
  /// \param Alignment Alignment of the allocation being freed.
  void Deallocate(const void *Ptr, size_t Size, size_t Alignment) {
    return getThreadLocalAllocator().Deallocate(Ptr, Size, Alignment);
  }

  /// Return the calling thread's sub-allocator, creating it on first use.
  ///
  /// \return Reference to this thread's sub-allocator for this instance.
  AllocatorTy &getThreadLocalAllocator() {
    // The calling thread's sub-allocator of each instance, indexed by a
    // process-unique instance id.
    //
    // mlir::ThreadLocalCache keys an analogous per-thread map on the instance
    // pointer and reclaims a thread's slot once the instance dies, but pays a
    // map lookup and shared_ptr bookkeeping per allocation. Instances here are
    // few and short-lived, so we prefer the O(1) vector index and accept that a
    // thread's Cache only grows with the number of instances created.
    static LLVM_THREAD_LOCAL std::vector<AllocatorTy *> Cache;
    if (LLVM_UNLIKELY(Cache.size() <= Id))
      Cache.resize(Id + 1);
    AllocatorTy *&A = Cache[Id];
    if (LLVM_UNLIKELY(!A)) {
      // Heap-allocate sub-allocators so that their addresses are stable and
      // different threads' bump pointers do not share a cache line.
      auto New = std::make_unique<AllocatorTy>();
      A = New.get();
      std::lock_guard<std::mutex> Lock(S->Mutex);
      S->Allocators.push_back(std::move(New));
    }
    return *A;
  }

  /// Return the number of sub-allocators, i.e. threads that have allocated.
  ///
  /// \return Count of sub-allocators created so far.
  size_t getNumberOfAllocators() const {
    std::lock_guard<std::mutex> Lock(S->Mutex);
    return S->Allocators.size();
  }
  /// @}

  /// \defgroup Methods which could not be called asynchronously:
  ///
  /// @{

  /// Reset state of allocators.
  void Reset() {
    for (const auto &A : S->Allocators)
      A->Reset();
  }

  /// Return total memory size used by all allocators.
  ///
  /// \return Total number of bytes used by all sub-allocators.
  size_t getTotalMemory() const {
    size_t TotalMemory = 0;
    for (const auto &A : S->Allocators)
      TotalMemory += A->getTotalMemory();
    return TotalMemory;
  }

  /// Set red zone for all allocators.
  ///
  /// \param NewSize Number of red-zone bytes to place between allocations.
  void setRedZoneSize(size_t NewSize) {
    for (const auto &A : S->Allocators)
      A->setRedZoneSize(NewSize);
  }

  /// Print statistic for each allocator.
  void PrintStats() const {
    size_t Idx = 0;
    for (const auto &A : S->Allocators) {
      errs() << "\n Allocator " << Idx++ << "\n";
      A->PrintStats();
    }
  }
  /// @}

protected:
  /// Shared mutex and owned list of per-thread sub-allocators.
  std::unique_ptr<State> S;
  /// Process-unique id indexing this instance in each thread's cache.
  unsigned Id;
};

/// Per-thread bump-pointer allocator for lock-free concurrent allocation.
using PerThreadBumpPtrAllocator = PerThreadAllocator<BumpPtrAllocator>;

} // end namespace parallel
} // end namespace llvm

#endif // LLVM_SUPPORT_PERTHREADBUMPPTRALLOCATOR_H
