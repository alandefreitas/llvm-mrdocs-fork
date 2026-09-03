//===--- UnwindInfoManager.h -- Register unwind info sections ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for managing eh-frame and compact-unwind registration and lookup
// through libunwind's find_dynamic_unwind_sections mechanism.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_UNWINDINFOMANAGER_H
#define LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_UNWINDINFOMANAGER_H

#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <map>
#include <mutex>

namespace llvm::orc {

/// Manages dynamic unwind-info section registration for libunwind.
class UnwindInfoManager {
public:
  /// Unwind section addresses matching libunwind's dynamic unwind layout.
  ///
  /// This struct's layout should match the unw_dynamic_unwind_sections struct
  /// from libunwind/src/libunwid_ext.h.
  struct UnwindSections {
    /// Base address of the DSO containing the unwind sections.
    uintptr_t dso_base;
    /// Address of the DWARF eh-frame section.
    uintptr_t dwarf_section;
    /// Length in bytes of the DWARF eh-frame section.
    size_t dwarf_section_length;
    /// Address of the compact-unwind section.
    uintptr_t compact_unwind_section;
    /// Length in bytes of the compact-unwind section.
    size_t compact_unwind_section_length;
  };

  /// Deleted move constructor.
  /// @param Other Instance that would be moved.
  UnwindInfoManager(UnwindInfoManager &&Other) = delete;
  /// Deleted move assignment operator.
  /// @param Other Instance that would be moved.
  /// @return Reference to this manager.
  UnwindInfoManager &operator=(UnwindInfoManager &&Other) = delete;
  /// Destroys the manager and unregisters its libunwind find-sections callback.
  LLVM_ABI ~UnwindInfoManager();

  /// Enable a process-global UnwindInfoManager when libunwind supports it.
  ///
  /// If the libunwind find-dynamic-unwind-info callback registration APIs are
  /// available then this method will instantiate a global UnwindInfoManager
  /// instance suitable for the process and return true. Otherwise it will
  /// return false.
  /// @return True if a process-global manager was enabled; false otherwise.
  LLVM_ABI static bool TryEnable();

  /// Adds this manager's bootstrap symbols to \p M.
  /// \param M Map of bootstrap symbol names to executor addresses to update.
  LLVM_ABI static void addBootstrapSymbols(StringMap<ExecutorAddr> &M);

  /// Register unwind-info sections for the given code ranges.
  /// \param CodeRanges Code address ranges covered by the unwind info.
  /// \param DSOBase Base address of the DSO containing the sections.
  /// \param DWARFEHFrame Address range of the DWARF eh-frame section.
  /// \param CompactUnwind Address range of the compact-unwind section.
  /// \return Success, or an error if registration fails.
  LLVM_ABI static Error
  registerSections(ArrayRef<orc::ExecutorAddrRange> CodeRanges,
                   orc::ExecutorAddr DSOBase,
                   orc::ExecutorAddrRange DWARFEHFrame,
                   orc::ExecutorAddrRange CompactUnwind);

  /// Deregister unwind-info sections for the given code ranges.
  /// \param CodeRanges Code address ranges whose unwind info should be removed.
  /// \return Success, or an error if no registration exists for a range.
  LLVM_ABI static Error
  deregisterSections(ArrayRef<orc::ExecutorAddrRange> CodeRanges);

private:
  UnwindInfoManager() = default;

  int findSectionsImpl(uintptr_t Addr, UnwindSections *Info);
  static int findSections(uintptr_t Addr, UnwindSections *Info);

  Error registerSectionsImpl(ArrayRef<orc::ExecutorAddrRange> CodeRanges,
                             orc::ExecutorAddr DSOBase,
                             orc::ExecutorAddrRange DWARFEHFrame,
                             orc::ExecutorAddrRange CompactUnwind);

  Error deregisterSectionsImpl(ArrayRef<orc::ExecutorAddrRange> CodeRanges);

  std::mutex M;
  std::map<uintptr_t, UnwindSections> UWSecs;
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_UNWINDINFOMANAGER_H
