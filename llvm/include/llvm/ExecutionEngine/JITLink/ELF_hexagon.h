//===--- ELF_hexagon.h - JIT link functions for ELF/hexagon ---*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// jit-link functions for ELF/hexagon.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_ELF_HEXAGON_H
#define LLVM_EXECUTIONENGINE_JITLINK_ELF_HEXAGON_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"

namespace llvm {
namespace jitlink {

/// Create a LinkGraph from an ELF/Hexagon relocatable object.
///
/// Note: The graph does not take ownership of the underlying buffer, nor copy
/// its contents. The caller is responsible for ensuring that the object buffer
/// outlives the graph.
/// \param ObjectBuffer Buffer containing the ELF/Hexagon relocatable object.
/// \param SSP Symbol string pool used to intern symbol names in the graph.
/// \return A LinkGraph for the object, or an error if parsing fails.
LLVM_ABI Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject_hexagon(
    MemoryBufferRef ObjectBuffer, std::shared_ptr<orc::SymbolStringPool> SSP);

/// jit-link the given object buffer, which must be a ELF Hexagon relocatable
/// object file.
/// \param G Link graph to link.
/// \param Ctx JITLink context providing memory management and callbacks.
LLVM_ABI void link_ELF_hexagon(std::unique_ptr<LinkGraph> G,
                               std::unique_ptr<JITLinkContext> Ctx);

} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_ELF_HEXAGON_H
