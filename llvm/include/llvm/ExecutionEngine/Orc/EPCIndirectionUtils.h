//===--- EPCIndirectionUtils.h - EPC based indirection utils ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Indirection utilities (stubs, trampolines, lazy call-throughs) that use the
// ExecutorProcessControl API to interact with the executor process.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EPCINDIRECTIONUTILS_H
#define LLVM_EXECUTIONENGINE_ORC_EPCINDIRECTIONUTILS_H

#include "llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"
#include "llvm/ExecutionEngine/Orc/LazyReexports.h"
#include "llvm/Support/Compiler.h"

#include <mutex>

namespace llvm {
namespace orc {

class ExecutorProcessControl;
class MemoryAccess;

/// Provides ExecutorProcessControl based indirect stubs, trampoline pool and
/// lazy call through manager.
class EPCIndirectionUtils {
  /// Helper granting privileged access to EPCIndirectionUtils internals.
  friend class EPCIndirectionUtilsAccess;

public:
  /// ABI support base class. Used to write resolver, stub, and trampoline
  /// blocks.
  class LLVM_ABI ABISupport {
  protected:
    /// Construct ABI support with the given architecture size parameters.
    /// \param PointerSize Size in bytes of a pointer on the target.
    /// \param TrampolineSize Size in bytes of one trampoline.
    /// \param StubSize Size in bytes of one indirect stub.
    /// \param StubToPointerMaxDisplacement Maximum allowed displacement from a
    ///        stub to its pointer.
    /// \param ResolverCodeSize Size in bytes of the resolver code block.
    ABISupport(unsigned PointerSize, unsigned TrampolineSize, unsigned StubSize,
               unsigned StubToPointerMaxDisplacement, unsigned ResolverCodeSize)
        : PointerSize(PointerSize), TrampolineSize(TrampolineSize),
          StubSize(StubSize),
          StubToPointerMaxDisplacement(StubToPointerMaxDisplacement),
          ResolverCodeSize(ResolverCodeSize) {}

  public:
    /// Destroy the ABI support object.
    virtual ~ABISupport();

    /// Return the size in bytes of a pointer on the target.
    /// @return Size in bytes of a pointer on the target.
    unsigned getPointerSize() const { return PointerSize; }
    /// Return the size in bytes of one trampoline.
    /// @return Size in bytes of one trampoline.
    unsigned getTrampolineSize() const { return TrampolineSize; }
    /// Return the size in bytes of one indirect stub.
    /// @return Size in bytes of one indirect stub.
    unsigned getStubSize() const { return StubSize; }
    /// Return the maximum allowed displacement from a stub to its pointer.
    /// @return Maximum allowed displacement from a stub to its pointer.
    unsigned getStubToPointerMaxDisplacement() const {
      return StubToPointerMaxDisplacement;
    }
    /// Return the size in bytes of the resolver code block.
    /// @return Size in bytes of the resolver code block.
    unsigned getResolverCodeSize() const { return ResolverCodeSize; }

    /// Write resolver code into the given working memory.
    /// \param ResolverWorkingMem Working memory to receive the resolver code.
    /// \param ResolverTargetAddr Address at which the resolver will execute.
    /// \param ReentryFnAddr Address of the re-entry function to call.
    /// \param ReentryCtxAddr Context address passed to the re-entry function.
    virtual void writeResolverCode(char *ResolverWorkingMem,
                                   ExecutorAddr ResolverTargetAddr,
                                   ExecutorAddr ReentryFnAddr,
                                   ExecutorAddr ReentryCtxAddr) const = 0;

    /// Write trampolines into the given working memory.
    /// \param TrampolineBlockWorkingMem Working memory to receive the
    ///        trampolines.
    /// \param TrampolineBlockTragetAddr Address at which the trampoline block
    ///        will execute.
    /// \param ResolverAddr Address of the resolver the trampolines jump to.
    /// \param NumTrampolines Number of trampolines to write.
    virtual void writeTrampolines(char *TrampolineBlockWorkingMem,
                                  ExecutorAddr TrampolineBlockTragetAddr,
                                  ExecutorAddr ResolverAddr,
                                  unsigned NumTrampolines) const = 0;

    /// Write indirect stubs into the given working memory.
    /// \param StubsBlockWorkingMem Working memory to receive the stubs.
    /// \param StubsBlockTargetAddress Address at which the stubs will execute.
    /// \param PointersBlockTargetAddress Address of the corresponding pointers
    ///        block.
    /// \param NumStubs Number of stubs to write.
    virtual void writeIndirectStubsBlock(
        char *StubsBlockWorkingMem, ExecutorAddr StubsBlockTargetAddress,
        ExecutorAddr PointersBlockTargetAddress, unsigned NumStubs) const = 0;

  private:
    unsigned PointerSize = 0;
    unsigned TrampolineSize = 0;
    unsigned StubSize = 0;
    unsigned StubToPointerMaxDisplacement = 0;
    unsigned ResolverCodeSize = 0;
  };

  /// Create using the given ABI class.
  /// \param EPC Executor process control for the target process.
  /// \param MemMgr Memory manager used to allocate stub and trampoline memory.
  /// \param MemAccess Memory-access interface for the executor process.
  /// @return A new EPCIndirectionUtils configured for the given ABI.
  template <typename ORCABI>
  static std::unique_ptr<EPCIndirectionUtils>
  CreateWithABI(ExecutorProcessControl &EPC,
                jitlink::JITLinkMemoryManager &MemMgr, MemoryAccess &MemAccess);

  /// Create based on the ExecutorProcessControl triple.
  /// \param EPC Executor process control for the target process.
  /// \param MemMgr Memory manager used to allocate stub and trampoline memory.
  /// \param MemAccess Memory-access interface for the executor process.
  /// @return A new EPCIndirectionUtils, or an error if the triple is
  ///         unsupported.
  LLVM_ABI static Expected<std::unique_ptr<EPCIndirectionUtils>>
  Create(ExecutorProcessControl &EPC, jitlink::JITLinkMemoryManager &MemMgr,
         MemoryAccess &MemAccess);

  /// Return a reference to the ExecutorProcessControl object.
  /// @return Reference to the ExecutorProcessControl for this instance.
  ExecutorProcessControl &getExecutorProcessControl() const { return EPC; }

  /// Return a reference to the MemoryAccess object for this instance.
  /// @return Reference to the MemoryAccess for this instance.
  MemoryAccess &getMemoryAccess() const { return MemAccess; }

  /// Return a reference to the JITLinkMemoryManager object for this instance.
  /// @return Reference to the JITLinkMemoryManager for this instance.
  jitlink::JITLinkMemoryManager &getMemManager() const { return MemMgr; }

  /// Return a reference to the ABISupport object for this instance.
  /// @return Reference to the ABISupport for this instance.
  ABISupport &getABISupport() const { return *ABI; }

  /// Release memory for resources held by this instance. This *must* be called
  /// prior to destruction of the class.
  /// @return Success, or an error if resources cannot be released.
  LLVM_ABI Error cleanup();

  /// Write resolver code to the executor process and return its address.
  /// This must be called before any call to createTrampolinePool or
  /// createLazyCallThroughManager.
  /// \param ReentryFnAddr Address of the re-entry function to invoke.
  /// \param ReentryCtxAddr Context address passed to the re-entry function.
  /// @return Address of the written resolver block, or an error on failure.
  LLVM_ABI Expected<ExecutorAddr>
  writeResolverBlock(ExecutorAddr ReentryFnAddr, ExecutorAddr ReentryCtxAddr);

  /// Returns the address of the Resolver block. Returns zero if the
  /// writeResolverBlock method has not previously been called.
  /// @return Address of the resolver block, or zero if not yet written.
  ExecutorAddr getResolverBlockAddress() const { return ResolverBlockAddr; }

  /// Create an IndirectStubsManager for the executor process.
  /// @return A new IndirectStubsManager for the executor process.
  LLVM_ABI std::unique_ptr<IndirectStubsManager> createIndirectStubsManager();

  /// Create a TrampolinePool for the executor process.
  /// @return Reference to the TrampolinePool for this instance.
  LLVM_ABI TrampolinePool &getTrampolinePool();

  /// Create a LazyCallThroughManager.
  /// This function should only be called once.
  /// \param ES Execution session that owns this manager.
  /// \param ErrorHandlerAddr Address of the error-handler function.
  /// @return Reference to the newly created LazyCallThroughManager.
  LLVM_ABI LazyCallThroughManager &
  createLazyCallThroughManager(ExecutionSession &ES,
                               ExecutorAddr ErrorHandlerAddr);

  /// Create a LazyCallThroughManager for the executor process.
  /// @return Reference to the existing LazyCallThroughManager.
  LazyCallThroughManager &getLazyCallThroughManager() {
    assert(LCTM && "createLazyCallThroughManager must be called first");
    return *LCTM;
  }

private:
  using FinalizedAlloc = jitlink::JITLinkMemoryManager::FinalizedAlloc;

  struct IndirectStubInfo {
    IndirectStubInfo() = default;
    IndirectStubInfo(ExecutorAddr StubAddress, ExecutorAddr PointerAddress)
        : StubAddress(StubAddress), PointerAddress(PointerAddress) {}
    ExecutorAddr StubAddress;
    ExecutorAddr PointerAddress;
  };

  using IndirectStubInfoVector = std::vector<IndirectStubInfo>;

  /// Create an EPCIndirectionUtils instance.
  EPCIndirectionUtils(ExecutorProcessControl &EPC,
                      jitlink::JITLinkMemoryManager &MemMgr,
                      MemoryAccess &MemAccess, std::unique_ptr<ABISupport> ABI);

  Expected<IndirectStubInfoVector> getIndirectStubs(unsigned NumStubs);

  std::mutex EPCUIMutex;
  ExecutorProcessControl &EPC;
  jitlink::JITLinkMemoryManager &MemMgr;
  MemoryAccess &MemAccess;
  std::unique_ptr<ABISupport> ABI;
  ExecutorAddr ResolverBlockAddr;
  FinalizedAlloc ResolverBlock;
  std::unique_ptr<TrampolinePool> TP;
  std::unique_ptr<LazyCallThroughManager> LCTM;

  std::vector<IndirectStubInfo> AvailableIndirectStubs;
  std::vector<FinalizedAlloc> IndirectStubAllocs;
};

/// Set up in-process lazy call-through re-entry via the given EPCIU.
///
/// This will call writeResolver on the given EPCIndirectionUtils instance
/// to set up re-entry via a function that will directly return the trampoline
/// landing address.
///
/// The EPCIndirectionUtils' LazyCallThroughManager must have been previously
/// created via EPCIndirectionUtils::createLazyCallThroughManager.
///
/// The EPCIndirectionUtils' writeResolver method must not have been previously
/// called.
///
/// This function is experimental and likely subject to revision.
/// \param EPCIU Indirection utilities whose resolver and LCTM are configured.
/// @return Success, or an error if re-entry setup fails.
LLVM_ABI Error setUpInProcessLCTMReentryViaEPCIU(EPCIndirectionUtils &EPCIU);

namespace detail {

template <typename ORCABI>
class ABISupportImpl : public EPCIndirectionUtils::ABISupport {
public:
  ABISupportImpl()
      : ABISupport(ORCABI::PointerSize, ORCABI::TrampolineSize,
                   ORCABI::StubSize, ORCABI::StubToPointerMaxDisplacement,
                   ORCABI::ResolverCodeSize) {}

  void writeResolverCode(char *ResolverWorkingMem,
                         ExecutorAddr ResolverTargetAddr,
                         ExecutorAddr ReentryFnAddr,
                         ExecutorAddr ReentryCtxAddr) const override {
    ORCABI::writeResolverCode(ResolverWorkingMem, ResolverTargetAddr,
                              ReentryFnAddr, ReentryCtxAddr);
  }

  void writeTrampolines(char *TrampolineBlockWorkingMem,
                        ExecutorAddr TrampolineBlockTargetAddr,
                        ExecutorAddr ResolverAddr,
                        unsigned NumTrampolines) const override {
    ORCABI::writeTrampolines(TrampolineBlockWorkingMem,
                             TrampolineBlockTargetAddr, ResolverAddr,
                             NumTrampolines);
  }

  void writeIndirectStubsBlock(char *StubsBlockWorkingMem,
                               ExecutorAddr StubsBlockTargetAddress,
                               ExecutorAddr PointersBlockTargetAddress,
                               unsigned NumStubs) const override {
    ORCABI::writeIndirectStubsBlock(StubsBlockWorkingMem,
                                    StubsBlockTargetAddress,
                                    PointersBlockTargetAddress, NumStubs);
  }
};

} // end namespace detail

template <typename ORCABI>
std::unique_ptr<EPCIndirectionUtils>
EPCIndirectionUtils::CreateWithABI(ExecutorProcessControl &EPC,
                                   jitlink::JITLinkMemoryManager &MemMgr,
                                   MemoryAccess &MemAccess) {
  return std::unique_ptr<EPCIndirectionUtils>(new EPCIndirectionUtils(
      EPC, MemMgr, MemAccess,
      std::make_unique<detail::ABISupportImpl<ORCABI>>()));
}

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_EPCINDIRECTIONUTILS_H
