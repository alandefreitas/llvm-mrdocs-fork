//===- llvm/Support/Memory.h - Memory Support -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the llvm::sys::Memory class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MEMORY_H
#define LLVM_SUPPORT_MEMORY_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include <system_error>
#include <utility>

namespace llvm {

// Forward declare raw_ostream: it is used for debug dumping below.
class raw_ostream;

namespace sys {

  /// Memory block with an address and a size.
  ///
  /// This class encapsulates the notion of a memory block which has an address
  /// and a size. It is used by the Memory class (a friend) as the result of
  /// various memory allocation operations.
  /// @see Memory
  class MemoryBlock {
  public:
    /// Create an empty memory block with a null address and zero size.
    MemoryBlock() : Address(nullptr), AllocatedSize(0) {}
    /// Create a memory block at \p addr with size \p allocatedSize.
    ///
    /// \param addr Address of the first byte of the memory area.
    /// \param allocatedSize Size in bytes of the allocated memory area.
    MemoryBlock(void *addr, size_t allocatedSize)
        : Address(addr), AllocatedSize(allocatedSize) {}
    /// Return the address of the first byte of the memory area.
    ///
    /// \returns Address of the first byte of the memory area.
    void *base() const { return Address; }
    /// The size as it was allocated. This is always greater or equal to the
    /// size that was originally requested.
    ///
    /// \returns The allocated size in bytes of the memory block.
    size_t allocatedSize() const { return AllocatedSize; }

  private:
    void *Address;    ///< Address of first byte of memory area
    size_t AllocatedSize; ///< Size, in bytes of the memory area
    unsigned Flags = 0;
    friend class Memory;
  };

  /// This class provides various memory handling functions that manipulate
  /// MemoryBlock instances.
  /// @since 1.4
  /// An abstraction for memory operations.
  class Memory {
  public:
    /// Flags that control mapped-memory protection and allocation hints.
    enum ProtectionFlags {
      /// Allow reads from the memory block.
      MF_READ = 0x1000000,
      /// Allow writes to the memory block.
      MF_WRITE = 0x2000000,
      /// Allow execution of code in the memory block.
      MF_EXEC = 0x4000000,
      /// Mask of the read, write, and execute protection bits.
      MF_RWE_MASK = 0x7000000,

      /// The \p MF_HUGE_HINT flag is used to indicate that the request for
      /// a memory block should be satisfied with large pages if possible.
      /// This is only a hint and small pages will be used as fallback.
      ///
      /// The presence or absence of this flag in the returned memory block
      /// is (at least currently) *not* a reliable indicator that the memory
      /// block will use or will not use large pages. On some systems a request
      /// without this flag can be backed by large pages without this flag being
      /// set, and on some other systems a request with this flag can fallback
      /// to small pages without this flag being cleared.
      MF_HUGE_HINT = 0x0000001
    };

    /// Allocate a mapped memory block suitable for dynamically generated code.
    ///
    /// This method allocates a block of memory that is suitable for loading
    /// dynamically generated code (e.g. JIT). An attempt to allocate
    /// \p NumBytes bytes of virtual memory is made.
    /// The actual allocated address is not guaranteed to be near the requested
    /// address.
    ///
    /// This method may allocate more than the number of bytes requested.  The
    /// actual number of bytes allocated is indicated in the returned
    /// MemoryBlock.
    ///
    /// The start of the allocated block must be aligned with the
    /// system allocation granularity (64K on Windows, page size on Linux).
    /// If the address following \p NearBlock is not so aligned, it will be
    /// rounded up to the next allocation granularity boundary.
    ///
    /// \param NumBytes Number of bytes of virtual memory to allocate.
    /// \param NearBlock Optional existing block; if non-null, attempt to
    ///        allocate near it.
    /// \param Flags Initial protection flags for the allocated block.
    /// \param EC [out] Set to describe any error that occurs.
    /// \returns A non-null MemoryBlock if successful, otherwise a null
    ///          MemoryBlock with \p EC describing the error.
    LLVM_ABI static MemoryBlock
    allocateMappedMemory(size_t NumBytes, const MemoryBlock *const NearBlock,
                         unsigned Flags, std::error_code &EC);

    /// Release a memory block previously allocated with allocateMappedMemory.
    ///
    /// This method releases a block of memory that was allocated with the
    /// allocateMappedMemory method. It should not be used to release any
    /// memory block allocated any other way.
    ///
    /// \param Block Memory block to release.
    /// \returns error_success if the function was successful, or an error_code
    ///          describing the failure if an error occurred.
    LLVM_ABI static std::error_code releaseMappedMemory(MemoryBlock &Block);

    /// Set the protection flags for a mapped memory block.
    ///
    /// This method sets the protection flags for a block of memory to the
    /// state specified by \p Flags.  The behavior is not specified if the
    /// memory was not allocated using the allocateMappedMemory method.
    ///
    /// If \p Flags is MF_WRITE, the actual behavior varies
    /// with the operating system (i.e. MF_READ | MF_WRITE on Windows) and the
    /// target architecture (i.e. MF_WRITE -> MF_READ | MF_WRITE on i386).
    ///
    /// \param Block Memory block to protect.
    /// \param Flags New protection state to assign to the block.
    /// \returns error_success if the function was successful, or an error_code
    ///          describing the failure if an error occurred.
    LLVM_ABI static std::error_code
    protectMappedMemory(const MemoryBlock &Block, unsigned Flags);

    /// Invalidate the instruction cache for a range of emitted code.
    ///
    /// Before the JIT can run a block of code that has been emitted it must
    /// invalidate the instruction cache on some platforms.
    ///
    /// \param Addr Start address of the code range to invalidate.
    /// \param Len Length in bytes of the range to invalidate.
    LLVM_ABI static void InvalidateInstructionCache(const void *Addr,
                                                    size_t Len);
  };

  /// Owning version of MemoryBlock.
  class OwningMemoryBlock {
  public:
    /// Create an empty owning memory block.
    OwningMemoryBlock() = default;
    /// Take ownership of the mapped memory described by \p M.
    ///
    /// \param M Memory block to take ownership of.
    explicit OwningMemoryBlock(MemoryBlock M) : M(std::move(M)) {}
    /// Move-construct by taking ownership from \p Other.
    ///
    /// \param Other Owning block to move from; left empty.
    OwningMemoryBlock(OwningMemoryBlock &&Other) {
      M = Other.M;
      Other.M = MemoryBlock();
    }
    /// Move-assign by taking ownership from \p Other.
    ///
    /// \param Other Owning block to move from; left empty.
    /// \returns Reference to this owning memory block.
    OwningMemoryBlock& operator=(OwningMemoryBlock &&Other) {
      M = Other.M;
      Other.M = MemoryBlock();
      return *this;
    }
    /// Release owned mapped memory if any.
    ~OwningMemoryBlock() {
      if (M.base())
        Memory::releaseMappedMemory(M);
    }
    /// Return the base address of the owned memory block.
    ///
    /// \returns Address of the first byte of the owned memory block.
    void *base() const { return M.base(); }
    /// The size as it was allocated. This is always greater or equal to the
    /// size that was originally requested.
    ///
    /// \returns The allocated size in bytes of the owned memory block.
    size_t allocatedSize() const { return M.allocatedSize(); }
    /// Return a copy of the underlying MemoryBlock.
    ///
    /// \returns A copy of the owned MemoryBlock.
    MemoryBlock getMemoryBlock() const { return M; }
    /// Release ownership of the mapped memory without destroying this object.
    ///
    /// \returns error_success on success, or an error_code describing the
    ///          failure. An empty block yields success.
    std::error_code release() {
      std::error_code EC;
      if (M.base()) {
        EC = Memory::releaseMappedMemory(M);
        M = MemoryBlock();
      }
      return EC;
    }
  private:
    MemoryBlock M;
  };

#ifndef NDEBUG
  /// Debugging output for Memory::ProtectionFlags.
  ///
  /// \param OS Stream to write to.
  /// \param PF Protection flags to print.
  /// \returns \p OS after writing the protection flags.
  raw_ostream &operator<<(raw_ostream &OS, const Memory::ProtectionFlags &PF);

  /// Debugging output for MemoryBlock.
  ///
  /// \param OS Stream to write to.
  /// \param MB Memory block to print.
  /// \returns \p OS after writing the memory block.
  raw_ostream &operator<<(raw_ostream &OS, const MemoryBlock &MB);
#endif // ifndef NDEBUG
  }    // end namespace sys
  }    // end namespace llvm

#endif
