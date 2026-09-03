//===-- llvm/MC/SectionKind.h - Classification of sections ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_SECTIONKIND_H
#define LLVM_MC_SECTIONKIND_H

namespace llvm {

/// POD value that classifies the properties of a section.
///
/// A section is classified into the deepest possible classification, and then
/// the target maps them onto their sections based on what capabilities they
/// have.
///
/// The comments below describe these as if they were an inheritance hierarchy
/// in order to explain the predicates below.
class SectionKind {
  enum Kind {
    /// Metadata - Debug info sections or other metadata.
    Metadata,

    /// Exclude - This section will be excluded from the final executable or
    /// shared library. Only valid for ELF / COFF targets.
    Exclude,

    /// Text - Text section, used for functions and other executable code.
    Text,

           /// ExecuteOnly, Text section that is not readable.
           ExecuteOnly,

    /// ReadOnly - Data that is never written to at program runtime by the
    /// program or the dynamic linker.  Things in the top-level readonly
    /// SectionKind are not mergeable.
    ReadOnly,

        /// MergableCString - Any null-terminated string which allows merging.
        /// These values are known to end in a nul value of the specified size,
        /// not otherwise contain a nul value, and be mergable.  This allows the
        /// linker to unique the strings if it so desires.

           /// Mergeable1ByteCString - 1 byte mergable, null terminated, string.
           Mergeable1ByteCString,

           /// Mergeable2ByteCString - 2 byte mergable, null terminated, string.
           Mergeable2ByteCString,

           /// Mergeable4ByteCString - 4 byte mergable, null terminated, string.
           Mergeable4ByteCString,

        /// MergeableConst - These are sections for merging fixed-length
        /// constants together.  For example, this can be used to unique
        /// constant pool entries etc.

            /// MergeableConst4 - This is a section used by 4-byte constants,
            /// for example, floats.
            MergeableConst4,

            /// MergeableConst8 - This is a section used by 8-byte constants,
            /// for example, doubles.
            MergeableConst8,

            /// MergeableConst16 - This is a section used by 16-byte constants,
            /// for example, vectors.
            MergeableConst16,

            /// MergeableConst32 - This is a section used by 32-byte constants,
            /// for example, vectors.
            MergeableConst32,

    /// Writeable - This is the base of all segments that need to be written
    /// to during program runtime.

       /// ThreadLocal - This is the base of all TLS segments.  All TLS
       /// objects must be writeable, otherwise there is no reason for them to
       /// be thread local!

           /// ThreadBSS - Zero-initialized TLS data objects.
           ThreadBSS,

           /// ThreadData - Initialized TLS data objects.
           ThreadData,

           /// ThreadBSSLocal - Zero-initialized TLS data objects with local linkage.
           ThreadBSSLocal,

       /// GlobalWriteableData - Writeable data that is global (not thread
       /// local).

           /// BSS - Zero initialized writeable data.
           BSS,

               /// BSSLocal - This is BSS (zero initialized and writable) data
               /// which has local linkage.
               BSSLocal,

               /// BSSExtern - This is BSS data with normal external linkage.
               BSSExtern,

           /// Common - Data with common linkage.  These represent tentative
           /// definitions, which always have a zero initializer and are never
           /// marked 'constant'.
           Common,

           /// This is writeable data that has a non-zero initializer.
           Data,

           /// ReadOnlyWithRel - These are global variables that are never
           /// written to by the program, but that have relocations, so they
           /// must be stuck in a writeable section so that the dynamic linker
           /// can write to them.  If it chooses to, the dynamic linker can
           /// mark the pages these globals end up on as read-only after it is
           /// done with its relocation phase.
           ReadOnlyWithRel
  } K : 8;
public:

  /// Return true if this is a metadata section kind.
  /// @return True if this is a metadata section kind.
  bool isMetadata() const { return K == Metadata; }

  /// Return true if this is an exclude section kind.
  /// @return True if this is an exclude section kind.
  bool isExclude() const { return K == Exclude; }

  /// Return true if this is a text or execute-only section kind.
  /// @return True if this is a text or execute-only section kind.
  bool isText() const { return K == Text || K == ExecuteOnly; }

  /// Return true if this is an execute-only section kind.
  /// @return True if this is an execute-only section kind.
  bool isExecuteOnly() const { return K == ExecuteOnly; }

  /// Return true if this is a read-only section kind (including mergeable).
  /// @return True if this is a read-only section kind (including mergeable).
  bool isReadOnly() const {
    return K == ReadOnly || isMergeableCString() ||
           isMergeableConst();
  }

  /// Return true if this is a mergeable C-string section kind.
  /// @return True if this is a mergeable C-string section kind.
  bool isMergeableCString() const {
    return K == Mergeable1ByteCString || K == Mergeable2ByteCString ||
           K == Mergeable4ByteCString;
  }
  /// Return true if this is a 1-byte mergeable C-string section kind.
  /// @return True if this is a 1-byte mergeable C-string section kind.
  bool isMergeable1ByteCString() const { return K == Mergeable1ByteCString; }
  /// Return true if this is a 2-byte mergeable C-string section kind.
  /// @return True if this is a 2-byte mergeable C-string section kind.
  bool isMergeable2ByteCString() const { return K == Mergeable2ByteCString; }
  /// Return true if this is a 4-byte mergeable C-string section kind.
  /// @return True if this is a 4-byte mergeable C-string section kind.
  bool isMergeable4ByteCString() const { return K == Mergeable4ByteCString; }

  /// Return true if this is a mergeable constant-data section kind.
  /// @return True if this is a mergeable constant-data section kind.
  bool isMergeableConst() const {
    return K == MergeableConst4 || K == MergeableConst8 ||
           K == MergeableConst16 || K == MergeableConst32;
  }
  /// Return true if this is a 4-byte mergeable constant section kind.
  /// @return True if this is a 4-byte mergeable constant section kind.
  bool isMergeableConst4() const { return K == MergeableConst4; }
  /// Return true if this is an 8-byte mergeable constant section kind.
  /// @return True if this is an 8-byte mergeable constant section kind.
  bool isMergeableConst8() const { return K == MergeableConst8; }
  /// Return true if this is a 16-byte mergeable constant section kind.
  /// @return True if this is a 16-byte mergeable constant section kind.
  bool isMergeableConst16() const { return K == MergeableConst16; }
  /// Return true if this is a 32-byte mergeable constant section kind.
  /// @return True if this is a 32-byte mergeable constant section kind.
  bool isMergeableConst32() const { return K == MergeableConst32; }

  /// Return true if this is a writable section kind (TLS or global).
  /// @return True if this is a writable section kind (TLS or global).
  bool isWriteable() const {
    return isThreadLocal() || isGlobalWriteableData();
  }

  /// Return true if this is a thread-local section kind.
  /// @return True if this is a thread-local section kind.
  bool isThreadLocal() const {
    return K == ThreadData || K == ThreadBSS || K == ThreadBSSLocal;
  }

  /// Return true if this is a thread-local BSS section kind.
  /// @return True if this is a thread-local BSS section kind.
  bool isThreadBSS() const { return K == ThreadBSS || K == ThreadBSSLocal; }
  /// Return true if this is a thread-local data section kind.
  /// @return True if this is a thread-local data section kind.
  bool isThreadData() const { return K == ThreadData; }
  /// Return true if this is a local-linkage thread-local BSS section kind.
  /// @return True if this is a local-linkage thread-local BSS section kind.
  bool isThreadBSSLocal() const { return K == ThreadBSSLocal; }

  /// Return true if this is global writable data (BSS, common, data, or rel).
  /// @return True if this is global writable data (BSS, common, data, or rel).
  bool isGlobalWriteableData() const {
    return isBSS() || isCommon() || isData() || isReadOnlyWithRel();
  }

  /// Return true if this is a BSS section kind (local, extern, or general).
  /// @return True if this is a BSS section kind (local, extern, or general).
  bool isBSS() const { return K == BSS || K == BSSLocal || K == BSSExtern; }
  /// Return true if this is a local-linkage BSS section kind.
  /// @return True if this is a local-linkage BSS section kind.
  bool isBSSLocal() const { return K == BSSLocal; }
  /// Return true if this is an external-linkage BSS section kind.
  /// @return True if this is an external-linkage BSS section kind.
  bool isBSSExtern() const { return K == BSSExtern; }

  /// Return true if this is a common-linkage section kind.
  /// @return True if this is a common-linkage section kind.
  bool isCommon() const { return K == Common; }

  /// Return true if this is initialized writable data.
  /// @return True if this is initialized writable data.
  bool isData() const { return K == Data; }

  /// Return true if this is read-only data that requires dynamic relocations.
  /// @return True if this is read-only data that requires dynamic relocations.
  bool isReadOnlyWithRel() const {
    return K == ReadOnlyWithRel;
  }
private:
  static SectionKind get(Kind K) {
    SectionKind Res;
    Res.K = K;
    return Res;
  }
public:

  /// Return a metadata section kind (debug info or other metadata).
  /// @return A metadata section kind.
  static SectionKind getMetadata() { return get(Metadata); }
  /// Return an exclude section kind (omitted from the final image).
  /// @return An exclude section kind.
  static SectionKind getExclude() { return get(Exclude); }
  /// Return a text section kind for executable code.
  /// @return A text section kind.
  static SectionKind getText() { return get(Text); }
  /// Return an execute-only text section kind.
  /// @return An execute-only text section kind.
  static SectionKind getExecuteOnly() { return get(ExecuteOnly); }
  /// Return a non-mergeable read-only data section kind.
  /// @return A non-mergeable read-only data section kind.
  static SectionKind getReadOnly() { return get(ReadOnly); }
  /// Return a 1-byte mergeable C-string section kind.
  /// @return A 1-byte mergeable C-string section kind.
  static SectionKind getMergeable1ByteCString() {
    return get(Mergeable1ByteCString);
  }
  /// Return a 2-byte mergeable C-string section kind.
  /// @return A 2-byte mergeable C-string section kind.
  static SectionKind getMergeable2ByteCString() {
    return get(Mergeable2ByteCString);
  }
  /// Return a 4-byte mergeable C-string section kind.
  /// @return A 4-byte mergeable C-string section kind.
  static SectionKind getMergeable4ByteCString() {
    return get(Mergeable4ByteCString);
  }
  /// Return a 4-byte mergeable constant section kind.
  /// @return A 4-byte mergeable constant section kind.
  static SectionKind getMergeableConst4() { return get(MergeableConst4); }
  /// Return an 8-byte mergeable constant section kind.
  /// @return An 8-byte mergeable constant section kind.
  static SectionKind getMergeableConst8() { return get(MergeableConst8); }
  /// Return a 16-byte mergeable constant section kind.
  /// @return A 16-byte mergeable constant section kind.
  static SectionKind getMergeableConst16() { return get(MergeableConst16); }
  /// Return a 32-byte mergeable constant section kind.
  /// @return A 32-byte mergeable constant section kind.
  static SectionKind getMergeableConst32() { return get(MergeableConst32); }
  /// Return a thread-local BSS section kind.
  /// @return A thread-local BSS section kind.
  static SectionKind getThreadBSS() { return get(ThreadBSS); }
  /// Return a thread-local initialized data section kind.
  /// @return A thread-local initialized data section kind.
  static SectionKind getThreadData() { return get(ThreadData); }
  /// Return a local-linkage thread-local BSS section kind.
  /// @return A local-linkage thread-local BSS section kind.
  static SectionKind getThreadBSSLocal() { return get(ThreadBSSLocal); }
  /// Return a BSS (zero-initialized writable) section kind.
  /// @return A BSS (zero-initialized writable) section kind.
  static SectionKind getBSS() { return get(BSS); }
  /// Return a local-linkage BSS section kind.
  /// @return A local-linkage BSS section kind.
  static SectionKind getBSSLocal() { return get(BSSLocal); }
  /// Return an external-linkage BSS section kind.
  /// @return An external-linkage BSS section kind.
  static SectionKind getBSSExtern() { return get(BSSExtern); }
  /// Return a common-linkage section kind.
  /// @return A common-linkage section kind.
  static SectionKind getCommon() { return get(Common); }
  /// Return a section kind for writable initialized data.
  /// @return A section kind for writable initialized data.
  static SectionKind getData() { return get(Data); }
  /// Return a SectionKind for read-only data that requires dynamic relocations.
  /// @return A SectionKind for read-only data that requires dynamic relocations.
  static SectionKind getReadOnlyWithRel() { return get(ReadOnlyWithRel); }
};

} // end namespace llvm

#endif
