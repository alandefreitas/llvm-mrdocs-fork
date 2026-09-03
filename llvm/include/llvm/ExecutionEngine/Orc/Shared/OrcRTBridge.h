//===---- OrcRTBridge.h -- Utils for interacting with orc-rt ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares types and symbol names provided by the ORC runtime.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_ORCRTBRIDGE_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_ORCRTBRIDGE_H

#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/ExecutionEngine/Orc/Shared/TargetProcessControlTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace orc {
/// ORC runtime symbol names, SPS signatures, and controller-interface descriptors.
namespace rt {

/// Name of the SimpleExecutorMemoryManager instance bootstrap symbol.
LLVM_ABI extern const char *SimpleExecutorMemoryManagerInstanceName;
/// Name of the SimpleExecutorMemoryManager reserve wrapper function.
LLVM_ABI extern const char *SimpleExecutorMemoryManagerReserveWrapperName;
/// Name of the SimpleExecutorMemoryManager initialize wrapper function.
LLVM_ABI extern const char *SimpleExecutorMemoryManagerInitializeWrapperName;
/// Name of the SimpleExecutorMemoryManager deinitialize wrapper function.
LLVM_ABI extern const char *SimpleExecutorMemoryManagerDeinitializeWrapperName;
/// Name of the SimpleExecutorMemoryManager release wrapper function.
LLVM_ABI extern const char *SimpleExecutorMemoryManagerReleaseWrapperName;

/// Name of the ExecutorSharedMemoryMapperService instance bootstrap symbol.
LLVM_ABI extern const char *ExecutorSharedMemoryMapperServiceInstanceName;
/// Name of the ExecutorSharedMemoryMapperService reserve wrapper function.
LLVM_ABI extern const char *ExecutorSharedMemoryMapperServiceReserveWrapperName;
/// Name of the ExecutorSharedMemoryMapperService initialize wrapper function.
LLVM_ABI extern const char
    *ExecutorSharedMemoryMapperServiceInitializeWrapperName;
/// Name of the ExecutorSharedMemoryMapperService deinitialize wrapper function.
LLVM_ABI extern const char
    *ExecutorSharedMemoryMapperServiceDeinitializeWrapperName;
/// Name of the ExecutorSharedMemoryMapperService release wrapper function.
LLVM_ABI extern const char *ExecutorSharedMemoryMapperServiceReleaseWrapperName;

/// Name of the EH-frame section registration allocation action.
LLVM_ABI extern const char *RegisterEHFrameSectionAllocActionName;
/// Name of the EH-frame section deregistration allocation action.
LLVM_ABI extern const char *DeregisterEHFrameSectionAllocActionName;

/// Name of the JIT loader GDB registration allocation action.
LLVM_ABI extern const char *RegisterJITLoaderGDBAllocActionName;
/// Name of the JIT loader GDB deregistration allocation action.
LLVM_ABI extern const char *DeregisterJITLoaderGDBAllocActionName;

/// Name of the ORC runtime JIT dispatch function bootstrap symbol.
LLVM_ABI extern const char *const DispatchName;
/// Name of the ORC runtime JIT dispatch context bootstrap symbol.
LLVM_ABI extern const char *const DispatchCtxName;

/// Symbol names for the ORC runtime's StandaloneMachOUnwindInfoRegistrar
/// SPS interface.
struct MachOUnwindInfoRegistrarSymbolNames {
  /// Name of the register-sections SPS wrapper function.
  StringRef RegisterSectionsName;
  /// Name of the deregister-sections SPS wrapper function.
  StringRef DeregisterSectionsName;
};

/// Default symbol names for the ORC runtime's
/// StandaloneMachOUnwindInfoRegistrar SPS interface.
extern const LLVM_ABI MachOUnwindInfoRegistrarSymbolNames
    orc_rt_MachOUnwindInfoRegistrarSPSSymbols;

/// SPS signature for SimpleExecutorMemoryManager::reserve.
using SPSSimpleExecutorMemoryManagerReserveSignature =
    shared::SPSExpected<shared::SPSExecutorAddr>(shared::SPSExecutorAddr,
                                                 uint64_t);
/// SPS signature for SimpleExecutorMemoryManager::initialize.
using SPSSimpleExecutorMemoryManagerInitializeSignature =
    shared::SPSExpected<shared::SPSExecutorAddr>(shared::SPSExecutorAddr,
                                                 shared::SPSFinalizeRequest);
/// SPS signature for SimpleExecutorMemoryManager::deinitialize.
using SPSSimpleExecutorMemoryManagerDeinitializeSignature = shared::SPSError(
    shared::SPSExecutorAddr, shared::SPSSequence<shared::SPSExecutorAddr>);
/// SPS signature for SimpleExecutorMemoryManager::release.
using SPSSimpleExecutorMemoryManagerReleaseSignature = shared::SPSError(
    shared::SPSExecutorAddr, shared::SPSSequence<shared::SPSExecutorAddr>);

// ExecutorSharedMemoryMapperService
/// SPS signature for ExecutorSharedMemoryMapperService::reserve.
using SPSExecutorSharedMemoryMapperServiceReserveSignature =
    shared::SPSExpected<
        shared::SPSTuple<shared::SPSExecutorAddr, shared::SPSString>>(
        shared::SPSExecutorAddr, uint64_t);
/// SPS signature for ExecutorSharedMemoryMapperService::initialize.
using SPSExecutorSharedMemoryMapperServiceInitializeSignature =
    shared::SPSExpected<shared::SPSExecutorAddr>(
        shared::SPSExecutorAddr, shared::SPSExecutorAddr,
        shared::SPSSharedMemoryFinalizeRequest);
/// SPS signature for ExecutorSharedMemoryMapperService::deinitialize.
using SPSExecutorSharedMemoryMapperServiceDeinitializeSignature =
    shared::SPSError(shared::SPSExecutorAddr,
                     shared::SPSSequence<shared::SPSExecutorAddr>);
/// SPS signature for ExecutorSharedMemoryMapperService::release.
using SPSExecutorSharedMemoryMapperServiceReleaseSignature = shared::SPSError(
    shared::SPSExecutorAddr, shared::SPSSequence<shared::SPSExecutorAddr>);

} // end namespace rt

/// Alternate ORC runtime symbol names for unwind-info registration actions.
namespace rt_alt {
/// Name of the UnwindInfoManager register allocation action.
LLVM_ABI extern const char *UnwindInfoManagerRegisterActionName;
/// Name of the UnwindInfoManager deregister allocation action.
LLVM_ABI extern const char *UnwindInfoManagerDeregisterActionName;
} // end namespace rt_alt
} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_ORCRTBRIDGE_H
