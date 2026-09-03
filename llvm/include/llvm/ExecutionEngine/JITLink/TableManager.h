//===---------------------- TableManager.h ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Fix edge for edge that needs an entry to reference the target symbol
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_TABLEMANAGER_H
#define LLVM_EXECUTIONENGINE_JITLINK_TABLEMANAGER_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/Support/Debug.h"

namespace llvm {
namespace jitlink {

/// A CRTP base for tables built on demand, such as GOT and PLT.
///
/// The getEntryForTarget function returns the table entry corresponding to the
/// given target, calling down to the implementation class to build an entry if
/// one does not already exist.
template <typename TableManagerImplT> class TableManager {
public:
  /// Return the constructed entry.
  ///
  /// Constructs an entry for the target symbol when one does not already exist.
  /// \param G Link graph used to construct a new entry if needed.
  /// \param Target Target symbol to look up or create an entry for.
  /// \return The table entry symbol for \p Target.
  Symbol &getEntryForTarget(LinkGraph &G, Symbol &Target) {
    assert(Target.hasName() && "Edge cannot point to anonymous target");

    auto EntryI = Entries.find(Target.getName());

    // Build the entry if it doesn't exist.
    if (EntryI == Entries.end()) {
      auto &Entry = impl().createEntry(G, Target);
      DEBUG_WITH_TYPE("jitlink", {
        dbgs() << "    Created " << impl().getSectionName() << " entry for "
               << Target.getName() << ": " << Entry << "\n";
      });
      EntryI = Entries.insert(std::make_pair(Target.getName(), &Entry)).first;
    }

    assert(EntryI != Entries.end() && "Could not get entry symbol");
    DEBUG_WITH_TYPE("jitlink", {
      dbgs() << "    Using " << impl().getSectionName() << " entry "
             << *EntryI->second << "\n";
    });
    return *EntryI->second;
  }

  /// Register a pre-existing entry.
  ///
  /// Objects may include pre-existing table entries (e.g. for GOTs).
  /// This method can be used to register those entries so that they will not
  /// be duplicated by createEntry the first time that getEntryForTarget is
  /// called.
  /// \param Target Target symbol associated with the pre-existing entry.
  /// \param Entry Pre-existing table entry symbol to register.
  /// \return True if the entry was newly registered, false if one already
  /// existed for \p Target.
  bool registerPreExistingEntry(Symbol &Target, Symbol &Entry) {
    assert(Target.hasName() && "Edge cannot point to anonymous target");
    auto Res = Entries.insert({
        Target.getName(),
        &Entry,
    });
    return Res.second;
  }

private:
  TableManagerImplT &impl() { return static_cast<TableManagerImplT &>(*this); }
  DenseMap<orc::SymbolStringPtr, Symbol *> Entries;
};

} // namespace jitlink
} // namespace llvm

#endif
