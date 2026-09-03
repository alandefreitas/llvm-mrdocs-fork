//===---- MachOObjectFormat.h - MachO format details for ORC ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ORC-specific MachO object format details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_MACHOOBJECTFORMAT_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_MACHOOBJECTFORMAT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace orc {

// FIXME: Move these to BinaryFormat?

// MachO section names.

/// Name of the MachO \c __DATA,__common section.
LLVM_ABI extern StringRef MachODataCommonSectionName;
/// Name of the MachO \c __DATA,__data section.
LLVM_ABI extern StringRef MachODataDataSectionName;
/// Name of the MachO \c __TEXT,__eh_frame section.
LLVM_ABI extern StringRef MachOEHFrameSectionName;
/// Name of the MachO \c __LD,__compact_unwind section.
LLVM_ABI extern StringRef MachOCompactUnwindSectionName;
/// Name of the MachO \c __TEXT,__cstring section.
LLVM_ABI extern StringRef MachOCStringSectionName;
/// Name of the MachO \c __DATA,__mod_init_func section.
LLVM_ABI extern StringRef MachOModInitFuncSectionName;
/// Name of the MachO \c __DATA,__objc_catlist section.
LLVM_ABI extern StringRef MachOObjCCatListSectionName;
/// Name of the MachO \c __DATA,__objc_catlist2 section.
LLVM_ABI extern StringRef MachOObjCCatList2SectionName;
/// Name of the MachO \c __DATA,__objc_classlist section.
LLVM_ABI extern StringRef MachOObjCClassListSectionName;
/// Name of the MachO \c __TEXT,__objc_classname section.
LLVM_ABI extern StringRef MachOObjCClassNameSectionName;
/// Name of the MachO \c __DATA,__objc_classrefs section.
LLVM_ABI extern StringRef MachOObjCClassRefsSectionName;
/// Name of the MachO \c __DATA,__objc_const section.
LLVM_ABI extern StringRef MachOObjCConstSectionName;
/// Name of the MachO \c __DATA,__objc_data section.
LLVM_ABI extern StringRef MachOObjCDataSectionName;
/// Name of the MachO \c __DATA,__objc_imageinfo section.
LLVM_ABI extern StringRef MachOObjCImageInfoSectionName;
/// Name of the MachO \c __TEXT,__objc_methname section.
LLVM_ABI extern StringRef MachOObjCMethNameSectionName;
/// Name of the MachO \c __TEXT,__objc_methtype section.
LLVM_ABI extern StringRef MachOObjCMethTypeSectionName;
/// Name of the MachO \c __DATA,__objc_nlcatlist section.
LLVM_ABI extern StringRef MachOObjCNLCatListSectionName;
/// Name of the MachO \c __DATA,__objc_nlclslist section.
LLVM_ABI extern StringRef MachOObjCNLClassListSectionName;
/// Name of the MachO \c __DATA,__objc_protolist section.
LLVM_ABI extern StringRef MachOObjCProtoListSectionName;
/// Name of the MachO \c __DATA,__objc_protorefs section.
LLVM_ABI extern StringRef MachOObjCProtoRefsSectionName;
/// Name of the MachO \c __DATA,__objc_selrefs section.
LLVM_ABI extern StringRef MachOObjCSelRefsSectionName;
/// Name of the MachO \c __TEXT,__swift5_proto section.
LLVM_ABI extern StringRef MachOSwift5ProtoSectionName;
/// Name of the MachO \c __TEXT,__swift5_protos section.
LLVM_ABI extern StringRef MachOSwift5ProtosSectionName;
/// Name of the MachO \c __TEXT,__swift5_types section.
LLVM_ABI extern StringRef MachOSwift5TypesSectionName;
/// Name of the MachO \c __TEXT,__swift5_typeref section.
LLVM_ABI extern StringRef MachOSwift5TypeRefSectionName;
/// Name of the MachO \c __TEXT,__swift5_fieldmd section.
LLVM_ABI extern StringRef MachOSwift5FieldMetadataSectionName;
/// Name of the MachO \c __TEXT,__swift5_entry section.
LLVM_ABI extern StringRef MachOSwift5EntrySectionName;
/// Name of the MachO \c __TEXT,__text section.
LLVM_ABI extern StringRef MachOTextTextSectionName;
/// Name of the MachO \c __DATA,__thread_bss section.
LLVM_ABI extern StringRef MachOThreadBSSSectionName;
/// Name of the MachO \c __DATA,__thread_data section.
LLVM_ABI extern StringRef MachOThreadDataSectionName;
/// Name of the MachO \c __DATA,__thread_vars section.
LLVM_ABI extern StringRef MachOThreadVarsSectionName;
/// Name of the MachO \c __TEXT,__unwind_info section.
LLVM_ABI extern StringRef MachOUnwindInfoSectionName;

/// MachO section names that hold static constructors / initializers.
LLVM_ABI extern StringRef MachOInitSectionNames[22];

/// Returns true if the given segment and section form a MachO initializer
/// section.
/// @param SegName Segment name to test.
/// @param SecName Section name to test.
/// @return True if \p SegName and \p SecName form a MachO initializer section.
LLVM_ABI bool isMachOInitializerSection(StringRef SegName, StringRef SecName);
/// Returns true if \p QualifiedName is a MachO initializer section.
/// @param QualifiedName Qualified \c SegName,SecName to test.
/// @return True if \p QualifiedName names a MachO initializer section.
LLVM_ABI bool isMachOInitializerSection(StringRef QualifiedName);

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_MACHOOBJECTFORMAT_H
