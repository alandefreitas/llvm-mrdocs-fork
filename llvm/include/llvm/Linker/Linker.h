//===- Linker.h - Module Linker Interface -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LINKER_LINKER_H
#define LLVM_LINKER_LINKER_H

#include "llvm/ADT/StringSet.h"
#include "llvm/Linker/IRMover.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Module;

/// Provides the core functionality of linking LLVM modules.
///
/// Keeps a pointer to the merged module so far. It doesn't take ownership of
/// the module since it is assumed that the user of this class will want to do
/// something with it after the linking.
class Linker {
  IRMover Mover;

public:
  /// Flags that control how modules are linked together.
  enum Flags {
    /// No special linking behavior.
    None = 0,
    /// Have symbols from Src shadow those in the Dest.
    OverrideFromSrc = (1 << 0),
    /// Link only symbols that are needed by the destination module.
    LinkOnlyNeeded = (1 << 1),
  };

  /// Construct a Linker that appends into composite module \p M.
  /// \param M Destination module that receives linked IR.
  LLVM_ABI Linker(Module &M);

  /// Link \p Src into the composite.
  ///
  /// Passing InternalizeCallback will have the linker call the function with
  /// the new module and a list of global value names to be internalized by the
  /// callback.
  ///
  /// \param Src Source module to link in; ownership is taken.
  /// \param Flags Bitmask of \a Flags controlling link behavior.
  /// \param InternalizeCallback Optional callback invoked with the composite
  ///        module and the set of global value names to internalize.
  /// \returns true on error.
  LLVM_ABI bool linkInModule(std::unique_ptr<Module> Src,
                             unsigned Flags = Flags::None,
                             std::function<void(Module &, const StringSet<> &)>
                                 InternalizeCallback = {});

  /// Link \p Src into destination module \p Dest.
  ///
  /// \param Dest Destination module that receives linked IR.
  /// \param Src Source module to link in; ownership is taken.
  /// \param Flags Bitmask of \a Flags controlling link behavior.
  /// \param InternalizeCallback Optional callback invoked with the composite
  ///        module and the set of global value names to internalize.
  /// \returns true on error.
  LLVM_ABI static bool linkModules(
      Module &Dest, std::unique_ptr<Module> Src, unsigned Flags = Flags::None,
      std::function<void(Module &, const StringSet<> &)> InternalizeCallback =
          {});
};

} // End llvm namespace

#endif
