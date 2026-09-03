//===------- COFF.h - Generic JIT link function for COFF ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic jit-link functions for COFF.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_COFF_H
#define LLVM_EXECUTIONENGINE_JITLINK_COFF_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"

namespace llvm {
/// Just-in-time linking APIs for relocatable object formats.
namespace jitlink {

/// Create a LinkGraph from an COFF relocatable object.
///
/// Note: The graph does not take ownership of the underlying buffer, nor copy
/// its contents. The caller is responsible for ensuring that the object buffer
/// outlives the graph.
/// \param ObjectBuffer Buffer containing the COFF relocatable object.
/// \param SSP Symbol string pool used to intern symbol names in the graph.
/// \return A LinkGraph for the object, or an error if parsing fails.
LLVM_ABI Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromCOFFObject(MemoryBufferRef ObjectBuffer,
                              std::shared_ptr<orc::SymbolStringPool> SSP);

/// Link the given graph.
///
/// Uses conservative defaults for GOT and stub handling based on the target
/// platform.
/// \param G Link graph to link.
/// \param Ctx JITLink context providing memory management and callbacks.
LLVM_ABI void link_COFF(std::unique_ptr<LinkGraph> G,
                        std::unique_ptr<JITLinkContext> Ctx);

/// GetImageBaseSymbol is a function object that finds the __ImageBase symbol
/// in the given graph if one is present.
///
/// The result is cached across calls, and can be reset by calling the reset
/// method.
class GetImageBaseSymbol {
public:
  /// Construct a finder for the COFF image-base symbol named \p ImageBaseName.
  /// \param ImageBaseName Name of the image-base symbol to look up.
  GetImageBaseSymbol(StringRef ImageBaseName = "__ImageBase")
      : ImageBaseName(ImageBaseName) {}
  /// Return the image-base symbol in \p G, caching the result.
  /// \param G Link graph to search for the image-base symbol.
  /// \return The image-base symbol, or nullptr if none is present.
  LLVM_ABI Symbol *operator()(LinkGraph &G);
  /// Reset the cached image-base symbol, optionally seeding it with
  /// \p CacheValue.
  /// \param CacheValue New cached value, or empty to clear the cache.
  void reset(std::optional<Symbol *> CacheValue = std::nullopt) {
    ImageBase = CacheValue;
  }

private:
  StringRef ImageBaseName;
  std::optional<Symbol *> ImageBase;
};

} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_COFF_H
