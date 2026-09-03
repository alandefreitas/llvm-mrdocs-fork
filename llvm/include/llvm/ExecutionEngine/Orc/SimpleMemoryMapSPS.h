//===- SimpleMemoryMapSPS.h - SPS memory-map bindings -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Binds SimpleMemoryMapBindings to the ORC runtime's SPS controller
// interface: a ProxySpec per operation, plus operations that resolve them.
//
// Each spec pairs one of the bindings' proxies with its controller-interface
// descriptor in Shared/SPSCI/SimpleNativeMemoryMapSPSCI.h, which supplies the
// wrapper name and wire signature. The specs are public so that clients can
// resolve the operations under non-default names, using
// recordProxy<Spec>(&P, Name) with lookupAndApply.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SIMPLEMEMORYMAPSPS_H
#define LLVM_EXECUTIONENGINE_ORC_SIMPLEMEMORYMAPSPS_H

#include "llvm/ExecutionEngine/Orc/LookupAndApply.h"
#include "llvm/ExecutionEngine/Orc/SPSProxySpec.h"
#include "llvm/ExecutionEngine/Orc/Shared/SPSCI/SimpleNativeMemoryMapSPSCI.h"
#include "llvm/ExecutionEngine/Orc/SimpleMemoryMap.h"
#include "llvm/Support/Compiler.h"

namespace llvm::orc::sps {

/// SPS proxy for SimpleMemoryMapBindings::ReserveProxy: reserves an address
/// range via the MemMgrReserve controller interface.
using MemMgrReserveProxySpec =
    ProxySpec<SimpleMemoryMapBindings::ReserveProxy, rt::sps_ci::MemMgrReserve>;
/// SPS proxy for SimpleMemoryMapBindings::InitializeProxy: applies a finalize
/// request via the MemMgrInitialize controller interface.
using MemMgrInitializeProxySpec =
    ProxySpec<SimpleMemoryMapBindings::InitializeProxy,
              rt::sps_ci::MemMgrInitialize>;
/// SPS proxy for SimpleMemoryMapBindings::DeinitializeProxy: deinitializes
/// allocations via the MemMgrDeinitialize controller interface.
using MemMgrDeinitializeProxySpec =
    ProxySpec<SimpleMemoryMapBindings::DeinitializeProxy,
              rt::sps_ci::MemMgrDeinitialize>;
/// SPS proxy for SimpleMemoryMapBindings::ReleaseProxy: releases reservations
/// via the MemMgrRelease controller interface.
using MemMgrReleaseProxySpec =
    ProxySpec<SimpleMemoryMapBindings::ReleaseProxy, rt::sps_ci::MemMgrRelease>;

/// Build bindings over the SPS controller interface, resolving the operations
/// in the given JITDylib under the specs' default (SimpleNativeMemoryMap)
/// names.
///
/// To bind a different executor-side implementation, use the specs above with
/// recordProxy<Spec>(&P, Name) to resolve its own names instead.
/// \param JD JITDylib in which to resolve the memory-manager operations.
/// \return SimpleMemoryMapBindings, or an error if symbol resolution fails.
LLVM_ABI Expected<SimpleMemoryMapBindings>
createSimpleMemoryMapBindings(JITDylib &JD);

/// As above, resolving the operations in ES's bootstrap JITDylib.
/// \param ES Execution session whose bootstrap JITDylib is searched.
/// \return SimpleMemoryMapBindings, or an error if symbol resolution fails.
LLVM_ABI Expected<SimpleMemoryMapBindings>
createSimpleMemoryMapBindings(ExecutionSession &ES);

} // namespace llvm::orc::sps

#endif // LLVM_EXECUTIONENGINE_ORC_SIMPLEMEMORYMAPSPS_H
