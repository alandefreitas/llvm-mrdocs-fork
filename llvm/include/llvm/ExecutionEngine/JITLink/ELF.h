//===------- ELF.h - Generic JIT link function for ELF ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic jit-link functions for ELF.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_ELF_H
#define LLVM_EXECUTIONENGINE_JITLINK_ELF_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"

namespace llvm {
namespace jitlink {

/// Create a LinkGraph from an ELF relocatable object.
///
/// Note: The graph does not take ownership of the underlying buffer, nor copy
/// its contents. The caller is responsible for ensuring that the object buffer
/// outlives the graph.
/// \param ObjectBuffer Buffer containing the ELF relocatable object.
/// \param SSP Symbol string pool used to intern symbol names in the graph.
/// \return A LinkGraph for the object, or an error if parsing fails.
LLVM_ABI Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject(MemoryBufferRef ObjectBuffer,
                             std::shared_ptr<orc::SymbolStringPool> SSP);

/// Link the given graph.
///
/// Uses conservative defaults for GOT and stub handling based on the target
/// platform.
/// \param G Link graph to link.
/// \param Ctx JITLink context providing memory management and callbacks.
LLVM_ABI void link_ELF(std::unique_ptr<LinkGraph> G,
                       std::unique_ptr<JITLinkContext> Ctx);

} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_ELF_H
