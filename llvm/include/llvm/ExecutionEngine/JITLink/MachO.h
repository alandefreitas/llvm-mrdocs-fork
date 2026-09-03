//===------- MachO.h - Generic JIT link function for MachO ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic jit-link functions for MachO.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_MACHO_H
#define LLVM_EXECUTIONENGINE_JITLINK_MACHO_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/Orc/Shared/MachOObjectFormat.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace jitlink {

/// Create a LinkGraph from a MachO relocatable object.
///
/// Note: The graph does not take ownership of the underlying buffer, nor copy
/// its contents. The caller is responsible for ensuring that the object buffer
/// outlives the graph.
/// \param ObjectBuffer Buffer containing the MachO relocatable object.
/// \param SSP Symbol string pool used to intern symbol names in the graph.
/// \return A LinkGraph for the object, or an error if parsing fails.
LLVM_ABI Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromMachOObject(MemoryBufferRef ObjectBuffer,
                               std::shared_ptr<orc::SymbolStringPool> SSP);

/// jit-link the given ObjBuffer, which must be a MachO object file.
///
/// Uses conservative defaults for GOT and stub handling based on the target
/// platform.
/// \param G Link graph to link.
/// \param Ctx JITLink context providing memory management and callbacks.
LLVM_ABI void link_MachO(std::unique_ptr<LinkGraph> G,
                         std::unique_ptr<JITLinkContext> Ctx);

/// Get a pointer to the standard MachO data section (creates an empty
/// section with RW- permissions and standard lifetime if one does not
/// already exist).
/// \param G Link graph to get or create the default RW data section in.
/// \return The standard MachO RW data section.
inline Section &getMachODefaultRWDataSection(LinkGraph &G) {
  if (auto *DataSec = G.findSectionByName(orc::MachODataDataSectionName))
    return *DataSec;
  return G.createSection(orc::MachODataDataSectionName,
                         orc::MemProt::Read | orc::MemProt::Write);
}

/// Get a pointer to the standard MachO text section (creates an empty
/// section with R-X permissions and standard lifetime if one does not
/// already exist).
/// \param G Link graph to get or create the default text section in.
/// \return The standard MachO text section.
inline Section &getMachODefaultTextSection(LinkGraph &G) {
  if (auto *TextSec = G.findSectionByName(orc::MachOTextTextSectionName))
    return *TextSec;
  return G.createSection(orc::MachOTextTextSectionName,
                         orc::MemProt::Read | orc::MemProt::Exec);
}

/// Gets or creates a MachO header for the current LinkGraph.
/// \param G Link graph to get or create the local MachO header in.
/// \return A symbol for the local MachO header, or an error on failure.
LLVM_ABI Expected<Symbol &> getOrCreateLocalMachOHeader(LinkGraph &G);

} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_MACHO_H
