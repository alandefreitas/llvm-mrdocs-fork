//===------ ObjectFormats.h - Object format details for ORC -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ORC-specific object format details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_OBJECTFORMATS_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_OBJECTFORMATS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/Shared/MachOObjectFormat.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace orc {

// ELF section names.
/// Name of the ELF \c .eh_frame section.
LLVM_ABI extern StringRef ELFEHFrameSectionName;

/// Name of the ELF \c .init_array section.
LLVM_ABI extern StringRef ELFInitArrayFuncSectionName;
/// Name of the ELF \c .init section.
LLVM_ABI extern StringRef ELFInitFuncSectionName;
/// Name of the ELF \c .fini_array section.
LLVM_ABI extern StringRef ELFFiniArrayFuncSectionName;
/// Name of the ELF \c .fini section.
LLVM_ABI extern StringRef ELFFiniFuncSectionName;
/// Name of the ELF \c .ctors section.
LLVM_ABI extern StringRef ELFCtorArrayFuncSectionName;
/// Name of the ELF \c .dtors section.
LLVM_ABI extern StringRef ELFDtorArrayFuncSectionName;

/// ELF section names that hold static constructors / initializers.
LLVM_ABI extern StringRef ELFInitSectionNames[3];
/// ELF section names that hold static destructors / finalizers.
LLVM_ABI extern StringRef ELFFiniSectionNames[3];

/// Name of the ELF \c .tbss (TLS BSS) section.
LLVM_ABI extern StringRef ELFThreadBSSSectionName;
/// Name of the ELF \c .tdata (TLS data) section.
LLVM_ABI extern StringRef ELFThreadDataSectionName;

/// Returns true if \p SecName is an ELF initializer section.
/// @param SecName Section name to test.
/// @return True if \p SecName is an ELF initializer section.
LLVM_ABI bool isELFInitializerSection(StringRef SecName);
/// Returns true if \p SecName is an ELF finalizer section.
/// @param SecName Section name to test.
/// @return True if \p SecName is an ELF finalizer section.
LLVM_ABI bool isELFFinalizerSection(StringRef SecName);

/// Returns true if \p Name is a COFF initializer section.
/// @param Name Section name to test.
/// @return True if \p Name is a COFF initializer section.
LLVM_ABI bool isCOFFInitializerSection(StringRef Name);

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_OBJECTFORMATS_H
