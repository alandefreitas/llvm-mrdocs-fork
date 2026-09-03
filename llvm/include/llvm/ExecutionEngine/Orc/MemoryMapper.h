//===- MemoryMapper.h - Cross-process memory mapper -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Cross-process (and in-process) memory mapping and transfer
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_MEMORYMAPPER_H
#define LLVM_EXECUTIONENGINE_ORC_MEMORYMAPPER_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Shared/MemoryFlags.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Process.h"

#include <mutex>

namespace llvm {
namespace jitlink {
class LinkGraph;
} // namespace jitlink

namespace orc {

/// Manages mapping, content transfer and protections for JIT memory.
class LLVM_ABI MemoryMapper {
public:
  /// Represents a single allocation containing multiple segments and
  /// initialization and deinitialization actions.
  struct AllocInfo {
    /// Describes one segment within an allocation.
    struct SegInfo {
      /// Offset of this segment from the mapping base.
      ExecutorAddrDiff Offset;
      /// Pointer to the working-memory copy of this segment's content.
      const char *WorkingMem;
      /// Size in bytes of the initialized content.
      size_t ContentSize;
      /// Size in bytes of trailing zero-fill after the content.
      size_t ZeroFillSize;
      /// Allocation group describing this segment's memory protections.
      AllocGroup AG;
    };

    /// Base address of the reserved mapping in the executor.
    ExecutorAddr MappingBase;
    /// Segments that make up this allocation.
    std::vector<SegInfo> Segments;
    /// Initialization and deinitialization actions for this allocation.
    shared::AllocActions Actions;
  };

  /// Callback invoked when a reserve operation completes.
  using OnReservedFunction = unique_function<void(Expected<ExecutorAddrRange>)>;

  /// Returns the page size of the target process.
  /// \return Page size in bytes of the target process.
  virtual unsigned int getPageSize() = 0;

  /// Reserves address space in the executor process.
  /// \param NumBytes Number of bytes to reserve.
  /// \param OnReserved Continuation invoked with the reserved range or an
  ///        error.
  virtual void reserve(size_t NumBytes, OnReservedFunction OnReserved) = 0;

  /// Provides working memory for writing content before initialization.
  ///
  /// The LinkGraph parameter is included to allow implementations to allocate
  /// working memory from the LinkGraph's allocator, in which case it will be
  /// deallocated when the LinkGraph is destroyed.
  /// \param G Link graph that may own the working-memory allocator.
  /// \param Addr Executor address whose working-memory copy is requested.
  /// \param ContentSize Size in bytes of the working-memory region.
  /// \return Pointer to writable working memory of at least ContentSize bytes.
  virtual char *prepare(jitlink::LinkGraph &G, ExecutorAddr Addr,
                        size_t ContentSize) = 0;

  /// Callback invoked when an initialize operation completes.
  using OnInitializedFunction = unique_function<void(Expected<ExecutorAddr>)>;

  /// Synchronizes executor memory, applies protections, and runs finalizers.
  ///
  /// Ensures executor memory is synchronized with working copy memory, sends
  /// functions to be called after initialization and before deinitialization,
  /// and applies memory protections. Returns a unique address identifying the
  /// allocation. This address should be passed to deinitialize to run
  /// deallocation actions (and reset permissions where possible).
  /// \param AI Allocation describing segments and actions to apply.
  /// \param OnInitialized Continuation invoked with the allocation key or an
  ///        error.
  virtual void initialize(AllocInfo &AI,
                          OnInitializedFunction OnInitialized) = 0;

  /// Callback invoked when a deinitialize operation completes.
  using OnDeinitializedFunction = unique_function<void(Error)>;

  /// Runs previously specified deinitialization actions.
  ///
  /// Executor addresses returned by initialize should be passed.
  /// \param Allocations Allocation keys previously returned by initialize.
  /// \param OnDeInitialized Continuation invoked when deinitialization
  ///        completes.
  virtual void deinitialize(ArrayRef<ExecutorAddr> Allocations,
                            OnDeinitializedFunction OnDeInitialized) = 0;

  /// Callback invoked when a release operation completes.
  using OnReleasedFunction = unique_function<void(Error)>;

  /// Releases address space acquired through reserve().
  /// \param Reservations Base addresses of ranges previously returned by
  ///        reserve.
  /// \param OnRelease Continuation invoked when the release completes.
  virtual void release(ArrayRef<ExecutorAddr> Reservations,
                       OnReleasedFunction OnRelease) = 0;

  /// Destroys the memory mapper.
  virtual ~MemoryMapper();
};

/// MemoryMapper that maps and transfers memory within the current process.
class LLVM_ABI InProcessMemoryMapper : public MemoryMapper {
public:
  /// Creates an in-process memory mapper for the given page size.
  /// \param PageSize Page size of the current process.
  InProcessMemoryMapper(size_t PageSize);

  /// Creates an InProcessMemoryMapper using the host process page size.
  /// \return An InProcessMemoryMapper on success, or an error on failure.
  static Expected<std::unique_ptr<InProcessMemoryMapper>> Create();

  /// Returns the page size used by this mapper.
  /// \return Page size in bytes used by this mapper.
  unsigned int getPageSize() override { return PageSize; }

  /// Reserves address space in the current process.
  /// \param NumBytes Number of bytes to reserve.
  /// \param OnReserved Continuation invoked with the reserved range or an
  ///        error.
  void reserve(size_t NumBytes, OnReservedFunction OnReserved) override;

  /// Synchronizes in-process memory, applies protections, and runs finalizers.
  /// \param AI Allocation describing segments and actions to apply.
  /// \param OnInitialized Continuation invoked with the allocation key or an
  ///        error.
  void initialize(AllocInfo &AI, OnInitializedFunction OnInitialized) override;

  /// Provides working memory for writing content before initialization.
  /// \param G Link graph that may own the working-memory allocator.
  /// \param Addr Executor address whose working-memory copy is requested.
  /// \param ContentSize Size in bytes of the working-memory region.
  /// \return Pointer to writable working memory of at least ContentSize bytes.
  char *prepare(jitlink::LinkGraph &G, ExecutorAddr Addr,
                size_t ContentSize) override;

  /// Runs previously specified deinitialization actions.
  /// \param Allocations Allocation keys previously returned by initialize.
  /// \param OnDeInitialized Continuation invoked when deinitialization
  ///        completes.
  void deinitialize(ArrayRef<ExecutorAddr> Allocations,
                    OnDeinitializedFunction OnDeInitialized) override;

  /// Releases address space acquired through reserve().
  /// \param Reservations Base addresses of ranges previously returned by
  ///        reserve.
  /// \param OnRelease Continuation invoked when the release completes.
  void release(ArrayRef<ExecutorAddr> Reservations,
               OnReleasedFunction OnRelease) override;

  /// Destroys the in-process memory mapper and releases remaining mappings.
  ~InProcessMemoryMapper() override;

private:
  struct Allocation {
    size_t Size;
    std::vector<shared::WrapperFunctionCall> DeinitializationActions;
  };
  using AllocationMap = DenseMap<ExecutorAddr, Allocation>;

  struct Reservation {
    size_t Size;
    std::vector<ExecutorAddr> Allocations;
  };
  using ReservationMap = DenseMap<void *, Reservation>;

  std::mutex Mutex;
  ReservationMap Reservations;
  AllocationMap Allocations;

  size_t PageSize;
};

/// MemoryMapper that shares memory with a remote executor process.
class LLVM_ABI SharedMemoryMapper final : public MemoryMapper {
public:
  /// Executor-side symbol addresses for shared-memory mapper operations.
  struct SymbolAddrs {
    /// Address of the executor-side mapper instance.
    ExecutorAddr Instance;
    /// Address of the executor-side reserve function.
    ExecutorAddr Reserve;
    /// Address of the executor-side initialize function.
    ExecutorAddr Initialize;
    /// Address of the executor-side deinitialize function.
    ExecutorAddr Deinitialize;
    /// Address of the executor-side release function.
    ExecutorAddr Release;
  };

  /// Creates a shared-memory mapper for the given executor and symbols.
  /// \param EPC Process-control object used to communicate with the executor.
  /// \param SAs Executor-side symbol addresses for mapper operations.
  /// \param PageSize Page size of the executor process.
  SharedMemoryMapper(ExecutorProcessControl &EPC, SymbolAddrs SAs,
                     size_t PageSize);

  /// Creates a SharedMemoryMapper using the executor's page size.
  /// \param EPC Process-control object used to communicate with the executor.
  /// \param SAs Executor-side symbol addresses for mapper operations.
  /// \return A SharedMemoryMapper on success, or an error on failure.
  static Expected<std::unique_ptr<SharedMemoryMapper>>
  Create(ExecutorProcessControl &EPC, SymbolAddrs SAs);

  /// Returns the page size used by this mapper.
  /// \return Page size in bytes of the executor process.
  unsigned int getPageSize() override { return PageSize; }

  /// Reserves shared address space in the executor process.
  /// \param NumBytes Number of bytes to reserve.
  /// \param OnReserved Continuation invoked with the reserved range or an
  ///        error.
  void reserve(size_t NumBytes, OnReservedFunction OnReserved) override;

  /// Provides working memory for writing content before initialization.
  /// \param G Link graph that may own the working-memory allocator.
  /// \param Addr Executor address whose working-memory copy is requested.
  /// \param ContentSize Size in bytes of the working-memory region.
  /// \return Pointer to writable working memory of at least ContentSize bytes.
  char *prepare(jitlink::LinkGraph &G, ExecutorAddr Addr,
                size_t ContentSize) override;

  /// Synchronizes shared memory, applies protections, and runs finalizers.
  /// \param AI Allocation describing segments and actions to apply.
  /// \param OnInitialized Continuation invoked with the allocation key or an
  ///        error.
  void initialize(AllocInfo &AI, OnInitializedFunction OnInitialized) override;

  /// Runs previously specified deinitialization actions.
  /// \param Allocations Allocation keys previously returned by initialize.
  /// \param OnDeInitialized Continuation invoked when deinitialization
  ///        completes.
  void deinitialize(ArrayRef<ExecutorAddr> Allocations,
                    OnDeinitializedFunction OnDeInitialized) override;

  /// Releases shared address space acquired through reserve().
  /// \param Reservations Base addresses of ranges previously returned by
  ///        reserve.
  /// \param OnRelease Continuation invoked when the release completes.
  void release(ArrayRef<ExecutorAddr> Reservations,
               OnReleasedFunction OnRelease) override;

  /// Destroys the shared-memory mapper and releases remaining mappings.
  ~SharedMemoryMapper() override;

private:
  struct Reservation {
    void *LocalAddr;
    size_t Size;
    int SharedMemoryId;
  };

  ExecutorProcessControl &EPC;
  SymbolAddrs SAs;

  std::mutex Mutex;

  std::map<ExecutorAddr, Reservation> Reservations;

  size_t PageSize;
};

} // namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_MEMORYMAPPER_H
