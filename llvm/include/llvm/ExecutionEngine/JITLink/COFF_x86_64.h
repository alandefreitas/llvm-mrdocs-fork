//===--- COFF_x86_64.h - JIT link functions for COFF/x86-64 ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// jit-link functions for COFF/x86-64.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_COFF_X86_64_H
#define LLVM_EXECUTIONENGINE_JITLINK_COFF_X86_64_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"

namespace llvm {
namespace jitlink {

/// Create a LinkGraph from an COFF/x86-64 relocatable object.
///
/// Note: The graph does not take ownership of the underlying buffer, nor copy
/// its contents. The caller is responsible for ensuring that the object buffer
/// outlives the graph.
/// \param ObjectBuffer Buffer containing the COFF/x86-64 relocatable object.
/// \param SSP Symbol string pool used to intern symbol names in the graph.
/// \return A LinkGraph for the object, or an error if parsing fails.
LLVM_ABI Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromCOFFObject_x86_64(
    MemoryBufferRef ObjectBuffer, std::shared_ptr<orc::SymbolStringPool> SSP);

/// jit-link the given object buffer, which must be a COFF x86-64 object file.
/// \param G Link graph to link.
/// \param Ctx JITLink context providing memory management and callbacks.
LLVM_ABI void link_COFF_x86_64(std::unique_ptr<LinkGraph> G,
                               std::unique_ptr<JITLinkContext> Ctx);

/// Return the string name of the given COFF x86-64 edge kind.
/// \param R COFF x86-64 edge kind to name.
/// \return A human-readable name for \p R.
LLVM_ABI const char *getCOFFX86RelocationKindName(Edge::Kind R);
} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_COFF_X86_64_H
