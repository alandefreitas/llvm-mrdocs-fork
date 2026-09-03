//===- OrcABISupport.h - ABI support code -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ABI specific code for Orc, e.g. callback assembly.
//
// ABI classes should be part of the JIT *target* process, not the host
// process (except where you're doing hosted JITing and the two are one and the
// same).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_ORCABISUPPORT_H
#define LLVM_EXECUTIONENGINE_ORC_ORCABISUPPORT_H

#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>

namespace llvm {
namespace orc {

/// Allocation sizes for a block of indirect stubs and their pointers.
struct IndirectStubsAllocationSizes {
  /// Number of bytes required for the stub block.
  uint64_t StubBytes = 0;
  /// Number of bytes required for the pointer block.
  uint64_t PointerBytes = 0;
  /// Number of stubs (and pointers) covered by the allocation.
  unsigned NumStubs = 0;
};

/// Compute stub and pointer block sizes for at least \p MinStubs stubs.
///
/// @param MinStubs Minimum number of stubs required.
/// @param RoundToMultipleOf If non-zero, round stub bytes up to this multiple
///        (must itself be a multiple of ORCABI::StubSize).
/// @return Stub and pointer byte counts and the number of stubs covered.
template <typename ORCABI>
IndirectStubsAllocationSizes
getIndirectStubsBlockSizes(unsigned MinStubs, unsigned RoundToMultipleOf = 0) {
  assert(
      (RoundToMultipleOf == 0 || (RoundToMultipleOf % ORCABI::StubSize == 0)) &&
      "RoundToMultipleOf is not a multiple of stub size");
  uint64_t StubBytes = MinStubs * ORCABI::StubSize;
  if (RoundToMultipleOf)
    StubBytes = alignTo(StubBytes, RoundToMultipleOf);
  unsigned NumStubs = StubBytes / ORCABI::StubSize;
  uint64_t PointerBytes = NumStubs * ORCABI::PointerSize;
  return {StubBytes, PointerBytes, NumStubs};
}

/// Generic ORC ABI support.
///
/// This class can be substituted as the target architecture support class for
/// ORC templates that require one (e.g. IndirectStubsManagers). It does not
/// support lazy JITing however, and any attempt to use that functionality
/// will result in execution of an llvm_unreachable.
class OrcGenericABI {
public:
  /// Size in bytes of a pointer on the generic host.
  static constexpr unsigned PointerSize = sizeof(uintptr_t);
  /// Size in bytes of one trampoline (placeholder; lazy JIT unsupported).
  static constexpr unsigned TrampolineSize = 1;
  /// Size in bytes of one indirect stub (placeholder; lazy JIT unsupported).
  static constexpr unsigned StubSize = 1;
  /// Maximum displacement from a stub to its pointer (placeholder value).
  static constexpr unsigned StubToPointerMaxDisplacement = 1;
  /// Size in bytes of the resolver code block (placeholder; lazy JIT
  /// unsupported).
  static constexpr unsigned ResolverCodeSize = 1;

  /// Write resolver code (unsupported on the generic ABI).
  ///
  /// @param ResolveWorkingMem Working memory where resolver code would be
  ///        written.
  /// @param ResolverTargetAddr Executor address at which the resolver is
  ///        linked.
  /// @param ReentryFnAddr Address of the reentry function.
  /// @param ReentryCtxAddr Context pointer passed to the reentry function.
  static void writeResolverCode(char *ResolveWorkingMem,
                                ExecutorAddr ResolverTargetAddr,
                                ExecutorAddr ReentryFnAddr,
                                ExecutorAddr ReentryCtxAddr) {
    llvm_unreachable("writeResolverCode is not supported by the generic host "
                     "support class");
  }

  /// Write trampolines (unsupported on the generic ABI).
  ///
  /// @param TrampolineBlockWorkingMem Working memory for the trampoline block.
  /// @param TrampolineBlockTargetAddr Executor address of the trampoline block.
  /// @param ResolverAddr Address of the resolver to jump to.
  /// @param NumTrampolines Number of trampolines to write.
  static void writeTrampolines(char *TrampolineBlockWorkingMem,
                               ExecutorAddr TrampolineBlockTargetAddr,
                               ExecutorAddr ResolverAddr,
                               unsigned NumTrampolines) {
    llvm_unreachable("writeTrampolines is not supported by the generic host "
                     "support class");
  }

  /// Write an indirect stubs block (unsupported on the generic ABI).
  ///
  /// @param StubsBlockWorkingMem Working memory for the stubs block.
  /// @param StubsBlockTargetAddress Executor address of the stubs block.
  /// @param PointersBlockTargetAddress Executor address of the pointer block.
  /// @param NumStubs Number of stubs to write.
  static void writeIndirectStubsBlock(char *StubsBlockWorkingMem,
                                      ExecutorAddr StubsBlockTargetAddress,
                                      ExecutorAddr PointersBlockTargetAddress,
                                      unsigned NumStubs) {
    llvm_unreachable(
        "writeIndirectStubsBlock is not supported by the generic host "
        "support class");
  }
};

/// AArch64 ORC ABI support for lazy JITing.
class OrcAArch64 {
public:
  /// Size in bytes of a pointer on AArch64.
  static constexpr unsigned PointerSize = 8;
  /// Size in bytes of one trampoline.
  static constexpr unsigned TrampolineSize = 12;
  /// Size in bytes of one indirect stub.
  static constexpr unsigned StubSize = 8;
  /// Maximum allowed displacement from a stub to its pointer.
  static constexpr unsigned StubToPointerMaxDisplacement = 1U << 27;
  /// Size in bytes of the resolver code block.
  static constexpr unsigned ResolverCodeSize = 0x120;

  /// Write the resolver code into the given memory.
  ///
  /// The user is responsible for allocating the memory and setting
  /// permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  /// @param ResolverWorkingMem Working memory where resolver code is written.
  /// @param ResolverTargetAddress Executor address at which the resolver is
  ///        linked.
  /// @param ReentryFnAddr Address of the reentry function.
  /// @param RentryCtxAddr Context pointer passed to the reentry function.
  LLVM_ABI static void writeResolverCode(char *ResolverWorkingMem,
                                         ExecutorAddr ResolverTargetAddress,
                                         ExecutorAddr ReentryFnAddr,
                                         ExecutorAddr RentryCtxAddr);

  /// Write the requested number of trampolines into the given memory.
  ///
  /// The memory must be big enough to hold 1 pointer, plus NumTrampolines
  /// trampolines.
  /// @param TrampolineBlockWorkingMem Working memory for the trampoline block.
  /// @param TrampolineBlockTargetAddress Executor address of the trampoline
  ///        block.
  /// @param ResolverAddr Address of the resolver to jump to.
  /// @param NumTrampolines Number of trampolines to write.
  LLVM_ABI static void
  writeTrampolines(char *TrampolineBlockWorkingMem,
                   ExecutorAddr TrampolineBlockTargetAddress,
                   ExecutorAddr ResolverAddr, unsigned NumTrampolines);

  /// Write indirect stubs into working memory.
  ///
  /// Stubs are written as if linked at StubsBlockTargetAddress, with the Nth
  /// stub using the Nth pointer in memory starting at
  /// PointersBlockTargetAddress.
  /// @param StubsBlockWorkingMem Working memory for the stubs block.
  /// @param StubsBlockTargetAddress Executor address of the stubs block.
  /// @param PointersBlockTargetAddress Executor address of the pointer block.
  /// @param MinStubs Number of stubs to write.
  LLVM_ABI static void writeIndirectStubsBlock(
      char *StubsBlockWorkingMem, ExecutorAddr StubsBlockTargetAddress,
      ExecutorAddr PointersBlockTargetAddress, unsigned MinStubs);
};

/// X86_64 code that's common to all ABIs.
///
/// X86_64 supports lazy JITing.
class OrcX86_64_Base {
public:
  /// Size in bytes of a pointer on X86_64.
  static constexpr unsigned PointerSize = 8;
  /// Size in bytes of one trampoline.
  static constexpr unsigned TrampolineSize = 8;
  /// Size in bytes of one indirect stub.
  static constexpr unsigned StubSize = 8;
  /// Maximum allowed displacement from a stub to its pointer.
  static constexpr unsigned StubToPointerMaxDisplacement = 1 << 31;

  /// Write the requested number of trampolines into the given memory.
  ///
  /// The memory must be big enough to hold 1 pointer, plus NumTrampolines
  /// trampolines.
  /// @param TrampolineBlockWorkingMem Working memory for the trampoline block.
  /// @param TrampolineBlockTargetAddress Executor address of the trampoline
  ///        block.
  /// @param ResolverAddr Address of the resolver to jump to.
  /// @param NumTrampolines Number of trampolines to write.
  LLVM_ABI static void
  writeTrampolines(char *TrampolineBlockWorkingMem,
                   ExecutorAddr TrampolineBlockTargetAddress,
                   ExecutorAddr ResolverAddr, unsigned NumTrampolines);

  /// Write indirect stubs into working memory.
  ///
  /// Stubs are written as if linked at StubsBlockTargetAddress, with the Nth
  /// stub using the Nth pointer in memory starting at
  /// PointersBlockTargetAddress.
  /// @param StubsBlockWorkingMem Working memory for the stubs block.
  /// @param StubsBlockTargetAddress Executor address of the stubs block.
  /// @param PointersBlockTargetAddress Executor address of the pointer block.
  /// @param NumStubs Number of stubs to write.
  LLVM_ABI static void writeIndirectStubsBlock(
      char *StubsBlockWorkingMem, ExecutorAddr StubsBlockTargetAddress,
      ExecutorAddr PointersBlockTargetAddress, unsigned NumStubs);
};

/// X86_64 support for SysV ABI (Linux, MacOSX).
///
/// X86_64_SysV supports lazy JITing.
class OrcX86_64_SysV : public OrcX86_64_Base {
public:
  /// Size in bytes of the resolver code block.
  static constexpr unsigned ResolverCodeSize = 0x6C;

  /// Write the resolver code into the given memory.
  ///
  /// The user is responsible for allocating the memory and setting
  /// permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  /// @param ResolverWorkingMem Working memory where resolver code is written.
  /// @param ResolverTargetAddress Executor address at which the resolver is
  ///        linked.
  /// @param ReentryFnAddr Address of the reentry function.
  /// @param ReentryCtxAddr Context pointer passed to the reentry function.
  LLVM_ABI static void writeResolverCode(char *ResolverWorkingMem,
                                         ExecutorAddr ResolverTargetAddress,
                                         ExecutorAddr ReentryFnAddr,
                                         ExecutorAddr ReentryCtxAddr);
};

/// X86_64 support for Win32.
///
/// X86_64_Win32 supports lazy JITing.
class OrcX86_64_Win32 : public OrcX86_64_Base {
public:
  /// Size in bytes of the resolver code block.
  static constexpr unsigned ResolverCodeSize = 0x74;

  /// Write the resolver code into the given memory.
  ///
  /// The user is responsible for allocating the memory and setting
  /// permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  /// @param ResolverWorkingMem Working memory where resolver code is written.
  /// @param ResolverTargetAddress Executor address at which the resolver is
  ///        linked.
  /// @param ReentryFnAddr Address of the reentry function.
  /// @param ReentryCtxAddr Context pointer passed to the reentry function.
  LLVM_ABI static void writeResolverCode(char *ResolverWorkingMem,
                                         ExecutorAddr ResolverTargetAddress,
                                         ExecutorAddr ReentryFnAddr,
                                         ExecutorAddr ReentryCtxAddr);
};

/// I386 support.
///
/// I386 supports lazy JITing.
class OrcI386 {
public:
  /// Size in bytes of a pointer on I386.
  static constexpr unsigned PointerSize = 4;
  /// Size in bytes of one trampoline.
  static constexpr unsigned TrampolineSize = 8;
  /// Size in bytes of one indirect stub.
  static constexpr unsigned StubSize = 8;
  /// Maximum allowed displacement from a stub to its pointer.
  static constexpr unsigned StubToPointerMaxDisplacement = 1 << 31;
  /// Size in bytes of the resolver code block.
  static constexpr unsigned ResolverCodeSize = 0x4a;

  /// Write the resolver code into the given memory.
  ///
  /// The user is responsible for allocating the memory and setting
  /// permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  /// @param ResolverWorkingMem Working memory where resolver code is written.
  /// @param ResolverTargetAddress Executor address at which the resolver is
  ///        linked.
  /// @param ReentryFnAddr Address of the reentry function.
  /// @param ReentryCtxAddr Context pointer passed to the reentry function.
  LLVM_ABI static void writeResolverCode(char *ResolverWorkingMem,
                                         ExecutorAddr ResolverTargetAddress,
                                         ExecutorAddr ReentryFnAddr,
                                         ExecutorAddr ReentryCtxAddr);

  /// Write the requested number of trampolines into the given memory.
  ///
  /// The memory must be big enough to hold 1 pointer, plus NumTrampolines
  /// trampolines.
  /// @param TrampolineBlockWorkingMem Working memory for the trampoline block.
  /// @param TrampolineBlockTargetAddress Executor address of the trampoline
  ///        block.
  /// @param ResolverAddr Address of the resolver to jump to.
  /// @param NumTrampolines Number of trampolines to write.
  LLVM_ABI static void
  writeTrampolines(char *TrampolineBlockWorkingMem,
                   ExecutorAddr TrampolineBlockTargetAddress,
                   ExecutorAddr ResolverAddr, unsigned NumTrampolines);

  /// Write indirect stubs into working memory.
  ///
  /// Stubs are written as if linked at StubsBlockTargetAddress, with the Nth
  /// stub using the Nth pointer in memory starting at
  /// PointersBlockTargetAddress.
  /// @param StubsBlockWorkingMem Working memory for the stubs block.
  /// @param StubsBlockTargetAddress Executor address of the stubs block.
  /// @param PointersBlockTargetAddress Executor address of the pointer block.
  /// @param NumStubs Number of stubs to write.
  LLVM_ABI static void writeIndirectStubsBlock(
      char *StubsBlockWorkingMem, ExecutorAddr StubsBlockTargetAddress,
      ExecutorAddr PointersBlockTargetAddress, unsigned NumStubs);
};

/// Mips32 shared ABI support for lazy JITing.
class OrcMips32_Base {
public:
  /// Size in bytes of a pointer on Mips32.
  static constexpr unsigned PointerSize = 4;
  /// Size in bytes of one trampoline.
  static constexpr unsigned TrampolineSize = 20;
  /// Size in bytes of one indirect stub.
  static constexpr unsigned StubSize = 8;
  /// Maximum allowed displacement from a stub to its pointer.
  static constexpr unsigned StubToPointerMaxDisplacement = 1 << 31;
  /// Size in bytes of the resolver code block.
  static constexpr unsigned ResolverCodeSize = 0xfc;

  /// Write the requested number of trampolines into the given memory.
  ///
  /// The memory must be big enough to hold 1 pointer, plus NumTrampolines
  /// trampolines.
  /// @param TrampolineBlockWorkingMem Working memory for the trampoline block.
  /// @param TrampolineBlockTargetAddress Executor address of the trampoline
  ///        block.
  /// @param ResolverAddr Address of the resolver to jump to.
  /// @param NumTrampolines Number of trampolines to write.
  LLVM_ABI static void
  writeTrampolines(char *TrampolineBlockWorkingMem,
                   ExecutorAddr TrampolineBlockTargetAddress,
                   ExecutorAddr ResolverAddr, unsigned NumTrampolines);

  /// Write the resolver code into the given memory.
  ///
  /// The user is responsible for allocating the memory and setting
  /// permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  /// @param ResolverBlockWorkingMem Working memory where resolver code is
  ///        written.
  /// @param ResolverBlockTargetAddress Executor address at which the resolver
  ///        is linked.
  /// @param ReentryFnAddr Address of the reentry function.
  /// @param ReentryCtxAddr Context pointer passed to the reentry function.
  /// @param isBigEndian True to emit big-endian instruction encoding.
  LLVM_ABI static void
  writeResolverCode(char *ResolverBlockWorkingMem,
                    ExecutorAddr ResolverBlockTargetAddress,
                    ExecutorAddr ReentryFnAddr, ExecutorAddr ReentryCtxAddr,
                    bool isBigEndian);

  /// Write indirect stubs into working memory.
  ///
  /// Stubs are written as if linked at StubsBlockTargetAddress, with the Nth
  /// stub using the Nth pointer in memory starting at
  /// PointersBlockTargetAddress.
  /// @param StubsBlockWorkingMem Working memory for the stubs block.
  /// @param StubsBlockTargetAddress Executor address of the stubs block.
  /// @param PointersBlockTargetAddress Executor address of the pointer block.
  /// @param NumStubs Number of stubs to write.
  LLVM_ABI static void writeIndirectStubsBlock(
      char *StubsBlockWorkingMem, ExecutorAddr StubsBlockTargetAddress,
      ExecutorAddr PointersBlockTargetAddress, unsigned NumStubs);
};

/// Mips32 little-endian ORC ABI support.
class OrcMips32Le : public OrcMips32_Base {
public:
  /// Write little-endian Mips32 resolver code into the given memory.
  ///
  /// @param ResolverWorkingMem Working memory where resolver code is written.
  /// @param ResolverTargetAddress Executor address at which the resolver is
  ///        linked.
  /// @param ReentryFnAddr Address of the reentry function.
  /// @param ReentryCtxAddr Context pointer passed to the reentry function.
  static void writeResolverCode(char *ResolverWorkingMem,
                                ExecutorAddr ResolverTargetAddress,
                                ExecutorAddr ReentryFnAddr,
                                ExecutorAddr ReentryCtxAddr) {
    OrcMips32_Base::writeResolverCode(ResolverWorkingMem, ResolverTargetAddress,
                                      ReentryFnAddr, ReentryCtxAddr, false);
  }
};

/// Mips32 big-endian ORC ABI support.
class OrcMips32Be : public OrcMips32_Base {
public:
  /// Write big-endian Mips32 resolver code into the given memory.
  ///
  /// @param ResolverWorkingMem Working memory where resolver code is written.
  /// @param ResolverTargetAddress Executor address at which the resolver is
  ///        linked.
  /// @param ReentryFnAddr Address of the reentry function.
  /// @param ReentryCtxAddr Context pointer passed to the reentry function.
  static void writeResolverCode(char *ResolverWorkingMem,
                                ExecutorAddr ResolverTargetAddress,
                                ExecutorAddr ReentryFnAddr,
                                ExecutorAddr ReentryCtxAddr) {
    OrcMips32_Base::writeResolverCode(ResolverWorkingMem, ResolverTargetAddress,
                                      ReentryFnAddr, ReentryCtxAddr, true);
  }
};

/// Mips64 ORC ABI support for lazy JITing.
class OrcMips64 {
public:
  /// Size in bytes of a pointer on Mips64.
  static constexpr unsigned PointerSize = 8;
  /// Size in bytes of one trampoline.
  static constexpr unsigned TrampolineSize = 40;
  /// Size in bytes of one indirect stub.
  static constexpr unsigned StubSize = 32;
  /// Maximum allowed displacement from a stub to its pointer.
  static constexpr unsigned StubToPointerMaxDisplacement = 1 << 31;
  /// Size in bytes of the resolver code block.
  static constexpr unsigned ResolverCodeSize = 0x120;

  /// Write the resolver code into the given memory.
  ///
  /// The user is responsible for allocating the memory and setting
  /// permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  /// @param ResolverWorkingMem Working memory where resolver code is written.
  /// @param ResolverTargetAddress Executor address at which the resolver is
  ///        linked.
  /// @param ReentryFnAddr Address of the reentry function.
  /// @param ReentryCtxAddr Context pointer passed to the reentry function.
  LLVM_ABI static void writeResolverCode(char *ResolverWorkingMem,
                                         ExecutorAddr ResolverTargetAddress,
                                         ExecutorAddr ReentryFnAddr,
                                         ExecutorAddr ReentryCtxAddr);

  /// Write the requested number of trampolines into the given memory.
  ///
  /// The memory must be big enough to hold 1 pointer, plus NumTrampolines
  /// trampolines.
  /// @param TrampolineBlockWorkingMem Working memory for the trampoline block.
  /// @param TrampolineBlockTargetAddress Executor address of the trampoline
  ///        block.
  /// @param ResolverFnAddr Address of the resolver to jump to.
  /// @param NumTrampolines Number of trampolines to write.
  LLVM_ABI static void
  writeTrampolines(char *TrampolineBlockWorkingMem,
                   ExecutorAddr TrampolineBlockTargetAddress,
                   ExecutorAddr ResolverFnAddr, unsigned NumTrampolines);

  /// Write indirect stubs into working memory.
  ///
  /// Stubs are written as if linked at StubsBlockTargetAddress, with the Nth
  /// stub using the Nth pointer in memory starting at
  /// PointersBlockTargetAddress.
  /// @param StubsBlockWorkingMem Working memory for the stubs block.
  /// @param StubsBlockTargetAddress Executor address of the stubs block.
  /// @param PointersBlockTargetAddress Executor address of the pointer block.
  /// @param NumStubs Number of stubs to write.
  LLVM_ABI static void writeIndirectStubsBlock(
      char *StubsBlockWorkingMem, ExecutorAddr StubsBlockTargetAddress,
      ExecutorAddr PointersBlockTargetAddress, unsigned NumStubs);
};

/// RISC-V 64 ORC ABI support for lazy JITing.
class OrcRiscv64 {
public:
  /// Size in bytes of a pointer on RISC-V 64.
  static constexpr unsigned PointerSize = 8;
  /// Size in bytes of one trampoline.
  static constexpr unsigned TrampolineSize = 16;
  /// Size in bytes of one indirect stub.
  static constexpr unsigned StubSize = 16;
  /// Maximum allowed displacement from a stub to its pointer.
  static constexpr unsigned StubToPointerMaxDisplacement = 1 << 31;
  /// Size in bytes of the resolver code block.
  static constexpr unsigned ResolverCodeSize = 0x148;

  /// Write the resolver code into the given memory.
  ///
  /// The user is responsible for allocating the memory and setting
  /// permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  /// @param ResolverWorkingMem Working memory where resolver code is written.
  /// @param ResolverTargetAddress Executor address at which the resolver is
  ///        linked.
  /// @param ReentryFnAddr Address of the reentry function.
  /// @param ReentryCtxAddr Context pointer passed to the reentry function.
  LLVM_ABI static void writeResolverCode(char *ResolverWorkingMem,
                                         ExecutorAddr ResolverTargetAddress,
                                         ExecutorAddr ReentryFnAddr,
                                         ExecutorAddr ReentryCtxAddr);

  /// Write the requested number of trampolines into the given memory.
  ///
  /// The memory must be big enough to hold 1 pointer, plus NumTrampolines
  /// trampolines.
  /// @param TrampolineBlockWorkingMem Working memory for the trampoline block.
  /// @param TrampolineBlockTargetAddress Executor address of the trampoline
  ///        block.
  /// @param ResolverFnAddr Address of the resolver to jump to.
  /// @param NumTrampolines Number of trampolines to write.
  LLVM_ABI static void
  writeTrampolines(char *TrampolineBlockWorkingMem,
                   ExecutorAddr TrampolineBlockTargetAddress,
                   ExecutorAddr ResolverFnAddr, unsigned NumTrampolines);

  /// Write indirect stubs into working memory.
  ///
  /// Stubs are written as if linked at StubsBlockTargetAddress, with the Nth
  /// stub using the Nth pointer in memory starting at
  /// PointersBlockTargetAddress.
  /// @param StubsBlockWorkingMem Working memory for the stubs block.
  /// @param StubsBlockTargetAddress Executor address of the stubs block.
  /// @param PointersBlockTargetAddress Executor address of the pointer block.
  /// @param NumStubs Number of stubs to write.
  LLVM_ABI static void writeIndirectStubsBlock(
      char *StubsBlockWorkingMem, ExecutorAddr StubsBlockTargetAddress,
      ExecutorAddr PointersBlockTargetAddress, unsigned NumStubs);
};

/// LoongArch 64 ORC ABI support for lazy JITing.
class OrcLoongArch64 {
public:
  /// Size in bytes of a pointer on LoongArch 64.
  static constexpr unsigned PointerSize = 8;
  /// Size in bytes of one trampoline.
  static constexpr unsigned TrampolineSize = 16;
  /// Size in bytes of one indirect stub.
  static constexpr unsigned StubSize = 16;
  /// Maximum allowed displacement from a stub to its pointer.
  static constexpr unsigned StubToPointerMaxDisplacement = 1 << 31;
  /// Size in bytes of the resolver code block.
  static constexpr unsigned ResolverCodeSize = 0xc8;

  /// Write the resolver code into the given memory.
  ///
  /// The user is responsible for allocating the memory and setting
  /// permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  /// @param ResolverWorkingMem Working memory where resolver code is written.
  /// @param ResolverTargetAddress Executor address at which the resolver is
  ///        linked.
  /// @param ReentryFnAddr Address of the reentry function.
  /// @param ReentryCtxAddr Context pointer passed to the reentry function.
  LLVM_ABI static void writeResolverCode(char *ResolverWorkingMem,
                                         ExecutorAddr ResolverTargetAddress,
                                         ExecutorAddr ReentryFnAddr,
                                         ExecutorAddr ReentryCtxAddr);

  /// Write the requested number of trampolines into the given memory.
  ///
  /// The memory must be big enough to hold 1 pointer, plus NumTrampolines
  /// trampolines.
  /// @param TrampolineBlockWorkingMem Working memory for the trampoline block.
  /// @param TrampolineBlockTargetAddress Executor address of the trampoline
  ///        block.
  /// @param ResolverFnAddr Address of the resolver to jump to.
  /// @param NumTrampolines Number of trampolines to write.
  LLVM_ABI static void
  writeTrampolines(char *TrampolineBlockWorkingMem,
                   ExecutorAddr TrampolineBlockTargetAddress,
                   ExecutorAddr ResolverFnAddr, unsigned NumTrampolines);

  /// Write indirect stubs into working memory.
  ///
  /// Stubs are written as if linked at StubsBlockTargetAddress, with the Nth
  /// stub using the Nth pointer in memory starting at
  /// PointersBlockTargetAddress.
  /// @param StubsBlockWorkingMem Working memory for the stubs block.
  /// @param StubsBlockTargetAddress Executor address of the stubs block.
  /// @param PointersBlockTargetAddress Executor address of the pointer block.
  /// @param NumStubs Number of stubs to write.
  LLVM_ABI static void writeIndirectStubsBlock(
      char *StubsBlockWorkingMem, ExecutorAddr StubsBlockTargetAddress,
      ExecutorAddr PointersBlockTargetAddress, unsigned NumStubs);
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_ORCABISUPPORT_H
